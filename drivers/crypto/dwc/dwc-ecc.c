#include <linux/kernel.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <crypto/internal/ecc.h>

#include "dwc-elperror.h"
#include "dwc-elppka.h"
#include "dwc-pka.h"

/* ------ PKA base operations -------- */

int pka_modinv(struct dwc_pka_ctx *ctx, const u64 *m, const u64 *x, u64 *result, unsigned digits)
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry_modinv = "modinv";
    int rc;
    unsigned key_sz = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)m);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)x);
    if (rc) {
        dev_dbg(pka->dev, "pka_modinv: failed when load operand, elp error code: %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry_modinv, key_sz, 0);
    if (rc) {
        dev_dbg(pka->dev, "pka_modinv: failed when running, status/rc: %d\n", rc);
        return -1;
    }

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_C_BASE, 0, digits, (u32*)result);
    return 0;
}

int pka_modmult(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64* result, unsigned digits) 
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry = "modmult";
    int rc;
    unsigned key_sz = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 0, digits, (u32*)y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)m);
    if (rc) {
        dev_err(pka->dev, "pka_modmult: failed when load operand, elp error code: %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, key_sz, 0);
    if (rc) {
        dev_err(pka->dev, "pka_modmult: failed when running, status/rc: %d\n", rc);
        return -1;
    }

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)result);
    return 0;
}

int pka_modadd(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64 *result, unsigned digits) 
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry = "modadd";
    int rc;
    unsigned key_sz = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 0, digits, (u32*)y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)m);
    if (rc) {
        dev_err(pka->dev, "pka_modadd: failed when load operand, elp error code: %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, key_sz, 0);
    if (rc) {
        dev_err(pka->dev, "pka_modadd: failed when running, status/rc: %d\n", rc);
        return -1;
    }

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)result);
    return 0;
}

int pka_modsub(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64 *result, unsigned digits) 
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry = "modsub";
    int rc;
    unsigned key_sz = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 0, digits, (u32*)y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)m);
    if (rc) {
        dev_err(pka->dev, "pka_modsub: failed when load operand, elp error code: %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, key_sz, 0);
    if (rc) {
        dev_err(pka->dev, "pka_modsub: failed when running, status/rc: %d\n", rc);
        return -1;
    }

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)result);
    return 0;
}

int pka_modreduce(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *m, u64* result, unsigned digits) 
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry = "reduce";
    int rc;
    unsigned key_sz = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_C_BASE, 0, digits, (u32*)x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)m);
    if (rc) {
        dev_err(pka->dev, "pka_modreduce: failed when load operand, elp error code: %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, key_sz, 0);
    if (rc) {
        dev_err(pka->dev, "pka_modreduce: failed when running, status/rc: %d\n", rc);
        return -1;
    }

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)result);
    return 0;
}

int pka_moddiv(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64 *result, unsigned digits) 
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry = "moddiv";
    int rc;
    unsigned key_sz = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_C_BASE, 0, digits, (u32*)y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 0, digits, (u32*)x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)m);
    if (rc) {
        dev_err(pka->dev, "pka_moddiv: failed when load operand, elp error code: %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, key_sz, 0);
    if (rc) { 
        dev_err(pka->dev, "pka_moddiv: failed when running, status/rc: %d\n", rc);
        return -1;
    }

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_C_BASE, 0, digits, (u32*)result);
    return 0;
}

/* ------ PKA ECC operations   ------ */

//TODO: not include 521 functions

int pka_pver(struct dwc_pka_ctx *ctx, const struct ecc_curve *curve, struct ecc_point *pk)
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char* entry = "pver";
    unsigned digits, bytelen;
    int rc; 

    digits = curve->g.ndigits;
    bytelen = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 2, digits, (u32*)pk->x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 2, digits, (u32*)pk->y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 6, digits, (u32*)curve->a);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 7, digits, (u32*)curve->b);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)curve->p);
    if (rc) {
        dev_err(pka->dev, "pka_pver: failed when load operand, elp error code %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, bytelen, 0);
    if (rc) {
        dev_err(pka->dev, "pka_pver: failed when running, status/rc %d\n", rc);
        return -1;
    } 

    return pka_testflag(pka->dev, "Z");
}
 
int pka_pmult(struct dwc_pka_ctx *ctx, const u64 *mult, const struct ecc_point *pk, 
                const struct ecc_point *nQ, const struct ecc_curve *curve, unsigned digits)
{
    struct dwc_pka_dev *pka = ctx->pka;                                             
    struct pka_state *pka_state = &pka->pka_state;
    char *entry = "pmult";
    int rc;
    unsigned bytelen = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    char zero[ECC_MAX_DIGITS << ECC_DIGITS_TO_BYTES_SHIFT];
    memset(zero, 0, bytelen);

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 2, digits, (u32*)pk->x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 2, digits, (u32*)pk->y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 7, digits, (u32*)mult);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 7, digits, (u32*)zero);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 6, digits, (u32*)curve->a);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)curve->p);
    if (rc) {
        dev_err(pka->dev, "pka_pmult: failed when load operand, elp error code %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, bytelen, 0);
    if (rc) {
        dev_err(pka->dev, "pka_pmult: failed when running, status/rc %d\n", rc);
        return -1;
    } 

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 2, digits, (u32*)nQ->x);
    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 2, digits, (u32*)nQ->y);
    return 0;
}

int pka_shamir(struct dwc_pka_ctx *ctx, const struct ecc_curve *curve, const u64 *u1, const u64 *u2,
                const struct ecc_point *p, const struct ecc_point *q, struct ecc_point *r)
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_state *pka_state = &pka->pka_state;
    char *entry = "shamir";
    int rc;
    unsigned bytelen, digits;

    digits = curve->g.ndigits;
    bytelen = digits << ECC_DIGITS_TO_BYTES_SHIFT;

    rc = elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 2, digits, (u32*)p->x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 2, digits, (u32*)p->y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 3, digits, (u32*)q->x);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 3, digits, (u32*)q->y);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 7, digits, (u32*)u1);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 7, digits, (u32*)u2);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 6, digits, (u32*)curve->a);
    rc |= elppka_load_operand_ecc(pka_state, PKA_OPERAND_D_BASE, 0, digits, (u32*)curve->p);
    if (rc) {
        dev_err(pka->dev, "pka_shamir: failed when load operand, elp error code %d\n", rc);
        return dwc_elp_errorcode(rc);
    }

    rc = pka_run(ctx, entry, bytelen, 0);
    if (rc) {
        dev_err(pka->dev, "pka_shamir: failed when running, status/rc %d\n", rc);
        return -1;
    }

    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_A_BASE, 2, digits, (u32*)r->x);
    elppka_unload_operand_ecc(pka_state, PKA_OPERAND_B_BASE, 2, digits, (u32*)r->y);
    return 0;
}

/* ------ Point operations ------ */

int dwc_ecc_make_pub_key(struct dwc_pka_ctx *ctx, unsigned int curve_id, unsigned int ndigits,
             const u64 *private_key, u64 *public_key)
{
    int ret = 0;
    struct ecc_point *pk;
    const struct ecc_curve *curve = ecc_get_curve(curve_id);

    if (!private_key) {
        ret = -EINVAL;
        goto out;
    }

    pk = ecc_alloc_point(ndigits);
    if (!pk) {
        ret = -ENOMEM;
        goto out;
    }

    pka_pmult(ctx, private_key, &curve->g, pk, curve, ndigits);

    /* SP800-56A rev 3 5.6.2.1.3 key check */
    if (dwc_ecc_is_pubkey_valid_full(ctx, curve, pk)) {
        ret = -EAGAIN;
        goto err_free_point;
    }

    ecc_swap_digits(pk->x, public_key, ndigits);
    ecc_swap_digits(pk->y, &public_key[ndigits], ndigits);

err_free_point:
    ecc_free_point(pk);
out:
    return ret;
}

/* SP800-56A section 5.6.2.3.4 partial verification: ephemeral keys only */
int dwc_ecc_is_pubkey_valid_partial(struct dwc_pka_ctx *ctx, 
                            const struct ecc_curve *curve, struct ecc_point *pk)
{
    if (WARN_ON(pk->ndigits != curve->g.ndigits))
        return -EINVAL;

    /* Check 1: Verify key is not the zero point. */
    if (ecc_point_is_zero(pk))
        return -EINVAL;

    /* Check 2: Verify key is in the range [1, p-1]. */
    if (vli_cmp(curve->p, pk->x, pk->ndigits) != 1)
        return -EINVAL;
    if (vli_cmp(curve->p, pk->y, pk->ndigits) != 1)
        return -EINVAL;

    /* Check 3: Verify that y^2 == (x^3 + a·x + b) mod p */
    if (pka_pver(ctx, curve, pk) != -1) {
        return -EINVAL;
    }

    return 0;
}

/* SP800-56A section 5.6.2.3.3 full verification */
int dwc_ecc_is_pubkey_valid_full(struct dwc_pka_ctx *ctx,
                    const struct ecc_curve *curve, struct ecc_point *pk)
{
    struct ecc_point *nQ;

    /* Checks 1 through 3 */
    int ret = dwc_ecc_is_pubkey_valid_partial(ctx, curve, pk);

    if (ret)
        return ret;

    /* Check 4: Verify that nQ is the zero point. */
    nQ = ecc_alloc_point(pk->ndigits);
    if (!nQ)
        return -ENOMEM;

    ret = pka_pmult(ctx, curve->n, pk, nQ, curve, pk->ndigits);
    if (ret) {
        return ret;
    }

    if (!ecc_point_is_zero(nQ))
        ret = -EINVAL;

    ecc_free_point(nQ);

    return ret;
}

