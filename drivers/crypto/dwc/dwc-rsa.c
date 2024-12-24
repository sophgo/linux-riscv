#include <linux/crypto.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-direct.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/io.h>
#include <linux/mod_devicetable.h>
#include <crypto/akcipher.h>
#include <crypto/algapi.h>
#include <crypto/internal/akcipher.h>
#include <crypto/internal/rsa.h>

#include "dwc-elperror.h"
#include "dwc-elppka.h"
#include "dwc-pka.h"

static void dwc_rsa_free_key(struct dwc_rsa_key *key)
{
    if (key->d)
        kfree_sensitive(key->d);
    if (key->e)
        kfree_sensitive(key->e);
    if (key->n)
        kfree_sensitive(key->n);
    memset(key, 0, sizeof(*key));
}

static int dwc_rsa_precalc(struct dwc_pka_ctx *ctx, u8 *m, int key_sz)
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry_rinv = "calc_r_inv";
    char* entry_mp = "calc_mp";
    char* entry_rsqrt = "calc_r_sqr";
    u8* result;
    int rc;

    rc = elppka_load_operand_rsa(pka_state, PKA_OPERAND_D_BASE, 0, key_sz, m);
    if (rc)
        return dwc_elp_errorcode(rc);
    rc = pka_run(ctx, entry_rinv, key_sz, 1);
    elppka_unload_operand_rsa(pka_state, PKA_OPERAND_C_BASE, 0, key_sz, result);
    
    rc = pka_run(ctx, entry_mp, key_sz, 1);
    if (rc)
        return rc;

    rc = elppka_load_operand_rsa(pka_state, PKA_OPERAND_C_BASE, 0, key_sz, result);
    if (rc)
        return dwc_elp_errorcode(rc);
    rc = pka_run(ctx, entry_rsqrt, key_sz, 1);

    return rc;
}

static int dwc_rsa_modexp(struct dwc_pka_ctx *ctx, u8 *result, u8* key, u8* m, unsigned key_sz)
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct dwc_pka_request_ctx *rctx = ctx->rctx;
    struct dwc_rsa_key *rsa_key = &ctx->rsa_key;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry = "modexp";
    int rc, keylen;

    keylen = rsa_key->bitlen >> 3;
    if (rsa_key->bitlen & 7) {
        keylen += 1;
    }

    // 1. load the operand
    rc = elppka_load_operand_rsa(pka_state, PKA_OPERAND_A_BASE, 0, rctx->total, result);
    rc |= elppka_load_operand_rsa(pka_state, PKA_OPERAND_D_BASE, 2, keylen, key);
    rc |= elppka_load_operand_rsa(pka_state, PKA_OPERAND_D_BASE, 0, key_sz, m);
    if (rc) 
        return dwc_elp_errorcode(rc);
     
    rc = pka_run(ctx, entry, key_sz, 1);
    if (rc)
        return rc;

    // 5. unload operand
    elppka_unload_operand_rsa(pka_state, PKA_OPERAND_A_BASE, 0, key_sz, result);
    return 0;
}

static int dwc_rsa_enc_core(struct dwc_pka_ctx *ctx, int enc)
{
    struct dwc_pka_request_ctx *rctx = ctx->rctx;
    struct dwc_rsa_key *key = &ctx->rsa_key;
    int ret = 0;

    rctx->total = sg_copy_to_buffer(rctx->in_sg, rctx->nents,
                    rctx->rsa_data, rctx->total);

    ret = dwc_rsa_precalc(ctx, key->n, key->key_sz);
    if (ret)
        goto err_rsa_crypt;

    if (enc) {
        key->bitlen = key->e_bitlen;
        ret = dwc_rsa_modexp(ctx, rctx->rsa_data, key->e, key->n, key->key_sz);
    } else {
        key->bitlen = key->d_bitlen;
        ret = dwc_rsa_modexp(ctx, rctx->rsa_data, key->d, key->n, key->key_sz);
    }

    if (ret)
        goto err_rsa_crypt;

    sg_copy_buffer(rctx->out_sg, sg_nents(rctx->out_sg),
               rctx->rsa_data, key->key_sz, 0, 0);

err_rsa_crypt:
    kfree(rctx->rsa_data);
    return ret;
}

static int dwc_rsa_enc(struct akcipher_request *req)
{
    struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct dwc_pka_dev *pka = ctx->pka;
    struct dwc_rsa_key *key = &ctx->rsa_key;
    struct dwc_pka_request_ctx *rctx = akcipher_request_ctx(req);
    int ret;

    if (!key->key_sz) {
        // shift to software processing, then shift back
        akcipher_request_set_tfm(req, ctx->akcipher_fbk);
        ret = crypto_akcipher_encrypt(req);
        akcipher_request_set_tfm(req, tfm);
        return ret;
    }

    if (unlikely(!key->n || !key->e))
        return -EINVAL;

    if (req->dst_len < key->key_sz)
        return dev_err_probe(pka->dev, -EOVERFLOW,
                     "Output buffer length less than parameter n\n");

    rctx->in_sg = req->src;
    rctx->out_sg = req->dst;
    rctx->total = req->src_len;
    rctx->nents = sg_nents(rctx->in_sg);  
    ctx->rctx = rctx;

    return dwc_rsa_enc_core(ctx, 1);
}


static int dwc_rsa_dec(struct akcipher_request *req)
{
    struct crypto_akcipher *tfm = crypto_akcipher_reqtfm(req);
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct dwc_pka_dev *pka = ctx->pka;
    struct dwc_rsa_key *key = &ctx->rsa_key;
    struct dwc_pka_request_ctx *rctx = akcipher_request_ctx(req);
    int ret;

    if (!key->key_sz) {
        akcipher_request_set_tfm(req, ctx->akcipher_fbk);
        ret = crypto_akcipher_decrypt(req);
        akcipher_request_set_tfm(req, tfm);
        return ret;
    }

    if (unlikely(!key->n || !key->d))
        return -EINVAL;

    if (req->dst_len < key->key_sz)
        return dev_err_probe(pka->dev, -EOVERFLOW,
                     "Output buffer length less than parameter n\n");

    rctx->in_sg = req->src;
    rctx->out_sg = req->dst;
    ctx->rctx = rctx;
    rctx->total = req->src_len;

    return dwc_rsa_enc_core(ctx, 0);
}

static int dwc_rsa_set_n(struct dwc_rsa_key *rsa_key,
                  const char *value, size_t vlen)
{
    const char *ptr = value;
    unsigned int bitslen;
    int ret;

    while (!*ptr && vlen) {
        ptr++;
        vlen--;
    }
    rsa_key->key_sz = vlen;
    bitslen = rsa_key->key_sz << 3;

    /* check valid key size */
    // key_size must larger then 32 bits
    if (bitslen & 0x1f)
        return -EINVAL;

    ret = -ENOMEM;
    rsa_key->n = kmemdup(ptr, rsa_key->key_sz, GFP_KERNEL);
    if (!rsa_key->n)
        goto err;

    return 0;
 err:
    rsa_key->key_sz = 0;
    rsa_key->n = NULL;
    dwc_rsa_free_key(rsa_key);
    return ret;
}

static int dwc_rsa_set_e(struct dwc_rsa_key *rsa_key,
                  const char *value, size_t vlen)
{
    const char *ptr = value;
    unsigned char pt;
    int loop;

    printk("pka rsa set e raw value: %s\n", value);

    while (!*ptr && vlen) {
        ptr++;
        vlen--;
    }
    pt = *ptr;

    if (!rsa_key->key_sz || !vlen || vlen > rsa_key->key_sz) {
        rsa_key->e = NULL;
        return -EINVAL;
    }

    rsa_key->e = kzalloc(rsa_key->key_sz, GFP_KERNEL);
    if (!rsa_key->e)
        return -ENOMEM;

    for (loop = 8; loop > 0; loop--) {
        if (pt >> (loop - 1))
            break;
    }

    rsa_key->e_bitlen = (vlen - 1) * 8 + loop;

    memcpy(rsa_key->e, ptr, vlen);
    printk("pka rsa key set: %x, keylen: %x\n", *(rsa_key->e), rsa_key->e_bitlen);

    return 0;
}

static int dwc_rsa_set_d(struct dwc_rsa_key *rsa_key,
                  const char *value, size_t vlen)
{
    const char *ptr = value;
    unsigned char pt;
    int loop;
    int ret;

    while (!*ptr && vlen) {
        ptr++;
        vlen--;
    }
    pt = *ptr;

    ret = -EINVAL;
    if (!rsa_key->key_sz || !vlen || vlen > rsa_key->key_sz)
        goto err;

    ret = -ENOMEM;
    rsa_key->d = kzalloc(rsa_key->key_sz, GFP_KERNEL);
    if (!rsa_key->d)
        goto err;

    for (loop = 8; loop > 0; loop--) {
        if (pt >> (loop - 1))
            break;
    }

    rsa_key->d_bitlen = (vlen - 1) * 8 + loop;

    memcpy(rsa_key->d, ptr, vlen);

    return 0;
 err:
    rsa_key->d = NULL;
    return ret;
}

static int dwc_rsa_setkey(struct crypto_akcipher *tfm, const void *key,
                   unsigned int keylen, bool private)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct rsa_key raw_key = {NULL};
    struct dwc_rsa_key *rsa_key = &ctx->rsa_key;
    struct dwc_pka_dev *pka = ctx->pka;
    int ret;

    if (private)
        ret = rsa_parse_priv_key(&raw_key, key, keylen);
    else
        ret = rsa_parse_pub_key(&raw_key, key, keylen);
    if (ret < 0)
        goto err;

    dwc_rsa_free_key(rsa_key);

    /* Use fallback for mod > 256 + 1 byte prefix */
    if (raw_key.n_sz > pka->pka_state.cfg.rsa_size + 1)
        return 0;

    ret = dwc_rsa_set_n(rsa_key, raw_key.n, raw_key.n_sz);
    if (ret)
        return ret;

    ret = dwc_rsa_set_e(rsa_key, raw_key.e, raw_key.e_sz);
    if (ret)
        goto err;

    if (private) {
        ret = dwc_rsa_set_d(rsa_key, raw_key.d, raw_key.d_sz);
        if (ret)
            goto err;
    }

    if (!rsa_key->n || !rsa_key->e) {
        ret = -EINVAL;
        goto err;
    }

    if (private && !rsa_key->d) {
        ret = -EINVAL;
        goto err;
    }

    return 0;
 err:
    dwc_rsa_free_key(rsa_key);
    return ret;
}

static int dwc_rsa_set_pub_key(struct crypto_akcipher *tfm, const void *key,
                    unsigned int keylen)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    int ret;

    ret = crypto_akcipher_set_pub_key(ctx->akcipher_fbk, key, keylen);
    if (ret)
        return ret;

    return dwc_rsa_setkey(tfm, key, keylen, false);
}

static int dwc_rsa_set_priv_key(struct crypto_akcipher *tfm, const void *key,
                     unsigned int keylen)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    int ret;

    ret = crypto_akcipher_set_priv_key(ctx->akcipher_fbk, key, keylen);
    if (ret)
        return ret;

    return dwc_rsa_setkey(tfm, key, keylen, true);
}

static unsigned int dwc_rsa_max_size(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);

    if (ctx->rsa_key.key_sz)
        return ctx->rsa_key.key_sz;

    return crypto_akcipher_maxsize(ctx->akcipher_fbk);
}

static int dwc_rsa_init_tfm(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);

    ctx->akcipher_fbk = crypto_alloc_akcipher("rsa-generic", 0, 0);
    if (IS_ERR(ctx->akcipher_fbk))
        return PTR_ERR(ctx->akcipher_fbk);

    ctx->pka = pka_find_dev(ctx);
    if (!ctx->pka) {
        crypto_free_akcipher(ctx->akcipher_fbk);
        return -ENODEV;
    }

    akcipher_set_reqsize(tfm, sizeof(struct dwc_pka_request_ctx) +
                 sizeof(struct crypto_akcipher) + 32);

    return 0;
}

static void dwc_rsa_exit_tfm(struct crypto_akcipher *tfm)
{
    struct dwc_pka_ctx *ctx = akcipher_tfm_ctx(tfm);
    struct dwc_rsa_key *key = (struct dwc_rsa_key *)&ctx->rsa_key;

    crypto_free_akcipher(ctx->akcipher_fbk);
    dwc_rsa_free_key(key);
}

static struct akcipher_alg dwc_rsa = {
    .encrypt = dwc_rsa_enc,
    .decrypt = dwc_rsa_dec,
    .set_pub_key = dwc_rsa_set_pub_key,
    .set_priv_key = dwc_rsa_set_priv_key,
    .max_size = dwc_rsa_max_size,
    .init = dwc_rsa_init_tfm,
    .exit = dwc_rsa_exit_tfm,
    .base = {
        .cra_name = "rsa",
        .cra_driver_name = "dwc-rsa",
        .cra_flags = CRYPTO_ALG_TYPE_AKCIPHER |
                 CRYPTO_ALG_NEED_FALLBACK,
        .cra_priority = 3000,
        .cra_module = THIS_MODULE,
        .cra_ctxsize = sizeof(struct dwc_pka_ctx),
    },
};

int dwc_rsa_register_algs(void)
{
    return crypto_register_akcipher(&dwc_rsa);
}

void dwc_rsa_unregister_algs(void)
{
    crypto_unregister_akcipher(&dwc_rsa);
}
