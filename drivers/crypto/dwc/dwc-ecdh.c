#include <crypto/kpp.h>
#include <crypto/ecdh.h>
#include <linux/scatterlist.h>
#include <linux/random.h>
#include <crypto/internal/kpp.h>
#include <crypto/internal/ecc.h>

#include "dwc-pka.h"

#define ECC_CURVE_NIST_P192_DIGITS  3
#define ECC_CURVE_NIST_P256_DIGITS  4
#define ECC_CURVE_NIST_P384_DIGITS  6
#define ECC_CURVE_NIST_P521_DIGITS  9

static int dwc_crypto_ecdh_shared_secret(struct dwc_pka_ctx *ctx, unsigned int curve_id, 
            unsigned int ndigits, const u64 *private_key, const u64 *public_key, u64 *secret)
{
    int ret = 0;
    struct ecc_point *product, *pk;
    const struct ecc_curve *curve = ecc_get_curve(curve_id);

    if (!private_key || !public_key) {
        ret = -EINVAL;
        goto out;
    }

    pk = ecc_alloc_point(ndigits);
    if (!pk) {
        ret = -ENOMEM;
        goto out;
    }

    ecc_swap_digits(public_key, pk->x, ndigits);
    ecc_swap_digits(&public_key[ndigits], pk->y, ndigits);
    ret = dwc_ecc_is_pubkey_valid_partial(ctx, curve, pk);
    if (ret)
        goto err_alloc_product;

    product = ecc_alloc_point(ndigits);
    if (!product) {
        ret = -ENOMEM;
        goto err_alloc_product;
    }

    pka_pmult(ctx, private_key, pk, product, curve, ndigits);

    if (ecc_point_is_zero(product)) {
        ret = -EFAULT;
        goto err_validity;
    }

    ecc_swap_digits(product->x, secret, ndigits);

err_validity:
    ecc_free_point(product);
err_alloc_product:
    ecc_free_point(pk);
out:
    return ret;
}


static int ecdh_set_secret(struct crypto_kpp *tfm, const void *buf,
               unsigned int len)
{
    struct dwc_pka_ctx *ctx = kpp_tfm_ctx(tfm);
    struct ecdh_ctx *ecdh_ctx = ctx->ecdh_ctx;
    struct ecdh params;
    int ret = 0;

    if (crypto_ecdh_decode_key(buf, len, &params) < 0 ||
        params.key_size > sizeof(u64) * ecdh_ctx->ndigits)
        return -EINVAL;

    memset(ecdh_ctx->private_key, 0, sizeof(ecdh_ctx->private_key));

    //TODO: not implement random secret
    if (!params.key || !params.key_size) 
        return ecc_gen_privkey(ecdh_ctx->curve_id, ecdh_ctx->ndigits, ecdh_ctx->private_key);

    ecc_digits_from_bytes(params.key, params.key_size,
                  ecdh_ctx->private_key, ecdh_ctx->ndigits);

    if (ecc_is_key_valid(ecdh_ctx->curve_id, ecdh_ctx->ndigits,
                 ecdh_ctx->private_key, params.key_size) < 0) {
        memzero_explicit(ecdh_ctx->private_key, params.key_size);
        ret = -EINVAL;
    }

    return ret;
}

static int ecdh_compute_value(struct kpp_request *req)
{
    struct crypto_kpp *tfm = crypto_kpp_reqtfm(req);
    struct dwc_pka_ctx *ctx = kpp_tfm_ctx(tfm);
    struct ecdh_ctx *ecdh_ctx = ctx->ecdh_ctx;
    u64 *public_key;
    u64 *shared_secret = NULL;
    void *buf;
    size_t copied, nbytes, public_key_sz;
    int ret = -ENOMEM;

    nbytes = ecdh_ctx->ndigits << ECC_DIGITS_TO_BYTES_SHIFT;
    /* Public part is a point thus it has both coordinates */
    public_key_sz = nbytes << 1;

    public_key = kmalloc(public_key_sz, GFP_KERNEL);
    if (!public_key)
        return -ENOMEM;

    if (req->src) {
        shared_secret = kmalloc(nbytes, GFP_KERNEL);
        if (!shared_secret)
            goto free_pubkey;

        /* from here on it's invalid parameters */
        ret = -EINVAL;

        /* must have exactly two points to be on the curve */
        if (public_key_sz != req->src_len)
            goto free_all;

        copied = sg_copy_to_buffer(req->src,
                       sg_nents_for_len(req->src,
                                public_key_sz),
                       public_key, public_key_sz);
        if (copied != public_key_sz)
            goto free_all;

        ret = dwc_crypto_ecdh_shared_secret(ctx, ecdh_ctx->curve_id, ecdh_ctx->ndigits,
                        ecdh_ctx->private_key, public_key,
                        shared_secret);

        buf = shared_secret;
    } else {
        ret = dwc_ecc_make_pub_key(ctx, ecdh_ctx->curve_id, ecdh_ctx->ndigits,
                       ecdh_ctx->private_key, public_key);
        buf = public_key;
        nbytes = public_key_sz;
    }

    if (ret < 0)
        goto free_all;

    /* might want less than we've got */
    nbytes = min_t(size_t, nbytes, req->dst_len);
    copied = sg_copy_from_buffer(req->dst, sg_nents_for_len(req->dst,
                                nbytes),
                     buf, nbytes);
    if (copied != nbytes)
        ret = -EINVAL;

    /* fall through */
free_all:
    kfree_sensitive(shared_secret);
free_pubkey:
    kfree(public_key);
    return ret;
}

static unsigned int ecdh_max_size(struct crypto_kpp *tfm)
{
    struct dwc_pka_ctx *ctx = kpp_tfm_ctx(tfm);
    struct ecdh_ctx *ecdh_ctx = ctx->ecdh_ctx;

    /* Public key is made of two coordinates, add one to the left shift */
    return ecdh_ctx->ndigits << (ECC_DIGITS_TO_BYTES_SHIFT + 1);
}

static int ecdh_ecc_ctx_init(struct dwc_pka_ctx *ctx) 
{
    struct ecdh_ctx *ecdh_ctx;

    ctx->ecdh_ctx = kmalloc(sizeof(ecdh_ctx), GFP_KERNEL);
    ecdh_ctx = ctx->ecdh_ctx;

    ctx->pka = pka_find_dev(ctx);
    if (!ctx->pka) {
        crypto_free_kpp(ctx->kpp_fbk);
        return -ENODEV;
    }
                                                 
    return 0;
}

static void ecdh_nist_exit_tfm(struct crypto_kpp *tfm) 
{
    struct dwc_pka_ctx *ctx = kpp_tfm_ctx(tfm);
    struct ecdh_ctx *ecdh_ctx = ctx->ecdh_ctx;
                                                        
    crypto_free_kpp(ctx->kpp_fbk);
    kfree_sensitive(ecdh_ctx);
}

static int ecdh_nist_p192_init_tfm(struct crypto_kpp *tfm)
{
    struct dwc_pka_ctx *ctx = kpp_tfm_ctx(tfm);
    int rc;

    ctx->kpp_fbk = crypto_alloc_kpp("ecdh-nist-p192-generic", 0, 0);
    if (IS_ERR(ctx->kpp_fbk))
        return PTR_ERR(ctx->kpp_fbk);

    rc = ecdh_ecc_ctx_init(ctx);
    if (rc)
        return rc;

    ctx->ecdh_ctx->curve_id = ECC_CURVE_NIST_P192;
    ctx->ecdh_ctx->ndigits = ECC_CURVE_NIST_P192_DIGITS;
    return 0;
}

static struct kpp_alg dwc_ecdh_nist_p192 = {
    .set_secret = ecdh_set_secret,
    .generate_public_key = ecdh_compute_value,
    .compute_shared_secret = ecdh_compute_value,
    .max_size = ecdh_max_size,
    .init = ecdh_nist_p192_init_tfm,
    .exit = ecdh_nist_exit_tfm,
    .base = {
        .cra_name = "ecdh-nist-p192",
        .cra_driver_name = "dwc-ecc-ecdh-p192-no_test",
        .cra_priority = 4000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};

static int ecdh_nist_p256_init_tfm(struct crypto_kpp *tfm)
{
    struct dwc_pka_ctx *ctx = kpp_tfm_ctx(tfm);
    int rc;

    ctx->kpp_fbk = crypto_alloc_kpp("ecdh-nist-p256-generic", 0, 0);
    if (IS_ERR(ctx->kpp_fbk))
        return PTR_ERR(ctx->kpp_fbk);

    rc = ecdh_ecc_ctx_init(ctx);
    if (rc)
        return rc;

    ctx->ecdh_ctx->curve_id = ECC_CURVE_NIST_P256;
    ctx->ecdh_ctx->ndigits = ECC_CURVE_NIST_P256_DIGITS;
    return 0;
}

static struct kpp_alg dwc_ecdh_nist_p256 = {
    .set_secret = ecdh_set_secret,
    .generate_public_key = ecdh_compute_value,
    .compute_shared_secret = ecdh_compute_value,
    .max_size = ecdh_max_size,
    .init = ecdh_nist_p256_init_tfm,
    .exit = ecdh_nist_exit_tfm,
    .base = {
        .cra_name = "ecdh-nist-p256-no_test",
        .cra_driver_name = "dwc-ecc-ecdh-p256",
        .cra_priority = 4000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};

static int ecdh_nist_p384_init_tfm(struct crypto_kpp *tfm)
{
    struct dwc_pka_ctx *ctx = kpp_tfm_ctx(tfm);
    int rc;

    ctx->kpp_fbk = crypto_alloc_kpp("ecdh-nist-p384-generic", 0, 0);
    if (IS_ERR(ctx->kpp_fbk))
        return PTR_ERR(ctx->kpp_fbk);

    rc = ecdh_ecc_ctx_init(ctx);
    if (rc)
        return rc;

    ctx->ecdh_ctx->curve_id = ECC_CURVE_NIST_P384;
    ctx->ecdh_ctx->ndigits = ECC_CURVE_NIST_P384_DIGITS;
    return 0;
}

static struct kpp_alg dwc_ecdh_nist_p384 = {
    .set_secret = ecdh_set_secret,
    .generate_public_key = ecdh_compute_value,
    .compute_shared_secret = ecdh_compute_value,
    .max_size = ecdh_max_size,
    .init = ecdh_nist_p384_init_tfm,
    .exit = ecdh_nist_exit_tfm,
    .base = {
        .cra_name = "ecdh-nist-p384-notest",
        .cra_driver_name = "dwc-ecc-ecdh-p384",
        .cra_priority = 4000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};


int dwc_ecdh_register_algs(void)
{
    int rc;

    rc = crypto_register_kpp(&dwc_ecdh_nist_p192);
    if (rc) 
        return rc;

    rc = crypto_register_kpp(&dwc_ecdh_nist_p256);
    if (rc) 
        goto unreg192;

    rc = crypto_register_kpp(&dwc_ecdh_nist_p384);
    if (rc) 
        goto unreg256;

    return 0;

unreg256:
    crypto_unregister_kpp(&dwc_ecdh_nist_p256);
unreg192:
    crypto_unregister_kpp(&dwc_ecdh_nist_p192);
    return rc;
}

void dwc_ecdh_unregister_algs(void)
{
    crypto_unregister_kpp(&dwc_ecdh_nist_p192);
    crypto_unregister_kpp(&dwc_ecdh_nist_p256);
    crypto_unregister_kpp(&dwc_ecdh_nist_p384);
}
