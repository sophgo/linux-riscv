#include <linux/asn1_decoder.h>
#include <linux/kernel.h>
#include <crypto/akcipher.h>
#include <crypto/algapi.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/ecc.h>

#include "dwc-ecdsasignature.asn1.h"
#include "dwc-pka.h"

struct ecdsa_signature_ctx {
    const struct ecc_curve *curve;
    u64 r[ECC_MAX_DIGITS];
    u64 s[ECC_MAX_DIGITS];
};


static int _ecdsa_verify(struct dwc_pka_ctx *ctx, const u64 *hash, const u64 *r, const u64 *s)
{
    struct ecc_ctx *ecc_ctx = ctx->ecc_ctx;
    const struct ecc_curve *curve = ecc_ctx->curve;
    unsigned int ndigits = curve->g.ndigits;
    u64 s1[ECC_MAX_DIGITS];
    u64 u1[ECC_MAX_DIGITS];
    u64 u2[ECC_MAX_DIGITS];
    u64 x1[ECC_MAX_DIGITS];
    u64 y1[ECC_MAX_DIGITS];
    struct ecc_point res = ECC_POINT_INIT(x1, y1, ndigits);

    /* 0 < r < n  and 0 < s < n */
    if (vli_is_zero(r, ndigits) || vli_cmp(r, curve->n, ndigits) >= 0 ||
        vli_is_zero(s, ndigits) || vli_cmp(s, curve->n, ndigits) >= 0)
        return -EBADMSG;

    /* hash is given */
    pr_devel("hash : %016llx %016llx ... %016llx\n",
         hash[ndigits - 1], hash[ndigits - 2], hash[0]);

    /* s1 = (s^-1) mod n */
    pka_modinv(ctx, curve->n, s, s1, ndigits);
    /* u1 = (hash * s1) mod n */
    pka_modmult(ctx, hash, s1, curve->n, u1, ndigits);
    /* u2 = (r * s1) mod n */
    pka_modmult(ctx, r, s1, curve->n, u2, ndigits);
    /* res = u1*G + u2 * pub_key */
    pka_shamir(ctx, curve, u1, u2, &curve->g, &ecc_ctx->pub_key, &res);
    /* res.x = res.x mod n (if res.x > order) */
    if (unlikely(vli_cmp(res.x, curve->n, ndigits) == 1))
        /* faster alternative for NIST p521, p384, p256 & p192 */
        pka_modreduce(ctx, res.x, curve->n, res.x, ndigits);

    if (!vli_cmp(res.x, r, ndigits))
        return 0;
                                                                  
    return -EKEYREJECTED;
}

/*
 * Verify an ECDSA signature.
 */
static int ecdsa_verify(struct akcipher_request *req)
{
    struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct ecc_ctx *ecc_ctx = ctx->ecc_ctx;
    size_t bufsize = ecc_ctx->curve->g.ndigits * sizeof(u64);
    struct ecdsa_signature_ctx sig_ctx = {
        .curve = ecc_ctx->curve,
    };
    u64 hash[ECC_MAX_DIGITS];
    unsigned char *buffer;
    int ret;

    if (unlikely(!ecc_ctx->pub_key_set)) {
        akcipher_request_set_tfm(req, ctx->akcipher_fbk);
        ret = crypto_akcipher_verify(req);
        akcipher_request_set_tfm(req, tfm);
        return ret;
    }

    buffer = kmalloc(req->src_len + req->dst_len, GFP_KERNEL);
    if (!buffer)
        return -ENOMEM;

    sg_pcopy_to_buffer(req->src,
        sg_nents_for_len(req->src, req->src_len + req->dst_len),
        buffer, req->src_len + req->dst_len, 0);

    ret = asn1_ber_decoder(&dwc_ecdsasignature_decoder, &sig_ctx,
                   buffer, req->src_len);
    if (ret < 0)
        goto error;

    if (bufsize > req->dst_len)
        bufsize = req->dst_len;

    ecc_digits_from_bytes(buffer + req->src_len, bufsize,
                  hash, ecc_ctx->curve->g.ndigits);

    ret = _ecdsa_verify(ctx, hash, sig_ctx.r, sig_ctx.s);

error:
    kfree(buffer);

    return ret;
}

static int ecdsa_ecc_ctx_init(struct dwc_pka_ctx *ctx, unsigned int curve_id)
{
    struct ecc_ctx *ecc_ctx;

    ctx->ecc_ctx = kmalloc(sizeof(ecc_ctx), GFP_KERNEL);
    ecc_ctx = ctx->ecc_ctx;
    
    ctx->pka = pka_find_dev(ctx);
    if (!ctx->pka) {
        crypto_free_akcipher(ctx->akcipher_fbk);
        return -ENODEV;
    }

    ecc_ctx->curve_id = curve_id;
    ecc_ctx->curve = ecc_get_curve(curve_id);
    if (!ecc_ctx->curve) {
        crypto_free_akcipher(ctx->akcipher_fbk);
        return -EINVAL;    
    }
    return 0;
}

static void ecdsa_ecc_ctx_deinit(struct ecc_ctx *ecc_ctx)
{
    ecc_ctx->pub_key_set = false;
}

/*
 * Set the public ECC key as defined by RFC5480 section 2.2 "Subject Public
 * Key". Only the uncompressed format is supported.
 */
static int ecdsa_set_pub_key(struct crypto_akcipher *tfm, const void *key, unsigned int keylen)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct ecc_ctx *ecc_ctx = ctx->ecc_ctx;
    unsigned int digitlen, ndigits;
    const unsigned char *d = key;
    int ret;

    ecdsa_ecc_ctx_deinit(ecc_ctx);
    ecc_ctx->curve = ecc_get_curve(ecc_ctx->curve_id);
    if (!ecc_ctx->curve)
         return -EINVAL;

    if (keylen < 1 || ((keylen - 1) & 1) != 0)
         return -EINVAL;
    /* we only accept uncompressed format indicated by '4' */
    if (d[0] != 4)
         return -EINVAL;

    keylen--;
    digitlen = keylen >> 1;

    ndigits = DIV_ROUND_UP(digitlen, sizeof(u64));
    if (ndigits != ecc_ctx->curve->g.ndigits)
        return -EINVAL;

    d++;

    ecc_digits_from_bytes(d, digitlen, ecc_ctx->pub_key.x, ndigits);
    ecc_digits_from_bytes(&d[digitlen], digitlen, ecc_ctx->pub_key.y, ndigits);

    ret = dwc_ecc_is_pubkey_valid_full(ctx, ecc_ctx->curve, &ecc_ctx->pub_key);

    ecc_ctx->pub_key_set = ret == 0;

    return ret;
}

static void ecdsa_exit_tfm(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct ecc_ctx *ecc_ctx = ctx->ecc_ctx;

    crypto_free_akcipher(ctx->akcipher_fbk);
    ecdsa_ecc_ctx_deinit(ecc_ctx);
    kfree_sensitive(ecc_ctx);
}

static unsigned int ecdsa_max_size(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct ecc_ctx *ecc_ctx = ctx->ecc_ctx;

    return DIV_ROUND_UP(ecc_ctx->curve->nbits, 8);
}

static int ecdsa_nist_p192_init_tfm(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    ctx->akcipher_fbk = crypto_alloc_akcipher("ecdsa-nist-p192-generic", 0, 0);
    if (IS_ERR(ctx->akcipher_fbk)) {
        return PTR_ERR(ctx->akcipher_fbk);
    }
    return ecdsa_ecc_ctx_init(ctx, ECC_CURVE_NIST_P192);
}

static struct akcipher_alg dwc_ecdsa_nist_p192 = {
    .verify = ecdsa_verify,
    .set_pub_key = ecdsa_set_pub_key,
    .max_size = ecdsa_max_size,
    .init = ecdsa_nist_p192_init_tfm,
    .exit = ecdsa_exit_tfm,
    .base = {
        .cra_name = "ecdsa-nist-p192-notest",
        .cra_driver_name = "dwc-ecc-ecdsa-p192",
        .cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
                 CRYPTO_ALG_NEED_FALLBACK,
        .cra_priority = 5000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};

static int ecdsa_nist_p256_init_tfm(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    ctx->akcipher_fbk = crypto_alloc_akcipher("ecdsa-nist-p256-generic", 0, 0);
    if (IS_ERR(ctx->akcipher_fbk)) {
        return PTR_ERR(ctx->akcipher_fbk);
    }
    return ecdsa_ecc_ctx_init(ctx, ECC_CURVE_NIST_P256);
}

static struct akcipher_alg dwc_ecdsa_nist_p256 = {
    .verify = ecdsa_verify,
    .set_pub_key = ecdsa_set_pub_key,
    .max_size = ecdsa_max_size,
    .init = ecdsa_nist_p256_init_tfm, 
    .exit = ecdsa_exit_tfm,
    .base = {
        .cra_name = "ecdsa-nist-p256-notest",
        .cra_driver_name = "dwc-ecc-ecdsa-p256",
        .cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
                 CRYPTO_ALG_NEED_FALLBACK,
        .cra_priority = 5000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};

static int ecdsa_nist_p384_init_tfm(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    ctx->akcipher_fbk = crypto_alloc_akcipher("ecdsa-nist-p384-generic", 0, 0);
    if (IS_ERR(ctx->akcipher_fbk)) {
        return PTR_ERR(ctx->akcipher_fbk);
    }
    return ecdsa_ecc_ctx_init(ctx, ECC_CURVE_NIST_P384);
}

static struct akcipher_alg dwc_ecdsa_nist_p384 = {
    .verify = ecdsa_verify,
    .set_pub_key = ecdsa_set_pub_key,
    .max_size = ecdsa_max_size,
    .init = ecdsa_nist_p384_init_tfm,
    .exit = ecdsa_exit_tfm,
    .base = {
        .cra_name = "ecdsa-nist-p384-notest",
        .cra_driver_name = "dwc-ecc-ecdsa-p384",
        .cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
                 CRYPTO_ALG_NEED_FALLBACK,
        .cra_priority = 5000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};
/*
static int ecdsa_nist_p521_init_tfm(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    ctx->akcipher_fbk = crypto_alloc_akcipher("ecdsa-nist-p521-generic", 0, 0);
    if (IS_ERR(ctx->akcipher_fbk)) {
        return PTR_ERR(ctx->akcipher_fbk);
    }
    return ecdsa_ecc_ctx_init(ctx, ECC_CURVE_NIST_P521);
}

static struct akcipher_alg dwc_ecdsa_nist_p521 = {
    .verify = ecdsa_verify,
    .set_pub_key = ecdsa_set_pub_key,
    .max_size = ecdsa_max_size,
    .init = ecdsa_nist_p521_init_tfm,
    .exit = ecdsa_exit_tfm,
    .base = {
        .cra_name = "ecdsa-nist-p521-notest",
        .cra_driver_name = "dwc-ecc-ecdsa-p521",
        .cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
                 CRYPTO_ALG_NEED_FALLBACK,
        .cra_priority = 5000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};
*/

int dwc_ecdsa_register_algs(void)
{
    int rc;

    rc = crypto_register_akcipher(&dwc_ecdsa_nist_p192);
    if (rc)
        return rc;

    rc = crypto_register_akcipher(&dwc_ecdsa_nist_p256);
    if (rc)
        goto unreg192;

    rc = crypto_register_akcipher(&dwc_ecdsa_nist_p384);
    if (rc)
        goto unreg256;

    // TODO: 521 functions not implemented 
    // rc = crypto_register_akcipher(&dwc_ecdsa_nist_p521);
    // if (rc)
    //    goto unreg384;

    return 0;

//unreg384:
//    crypto_unregister_akcipher(&dwc_ecdsa_nist_p384);
unreg256:
    crypto_unregister_akcipher(&dwc_ecdsa_nist_p256);
unreg192:
    crypto_unregister_akcipher(&dwc_ecdsa_nist_p192);
    return rc;
}

void dwc_ecdsa_unregister_algs(void)
{
    crypto_unregister_akcipher(&dwc_ecdsa_nist_p192);
    crypto_unregister_akcipher(&dwc_ecdsa_nist_p256);
    crypto_unregister_akcipher(&dwc_ecdsa_nist_p384);
    // crypto_unregister_akcipher(&dwc_ecdsa_nist_p521);
}
