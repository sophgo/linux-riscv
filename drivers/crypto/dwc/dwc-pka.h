#pragma once
#include <crypto/scatterwalk.h>
#include <linux/scatterlist.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/interrupt.h>
#include <crypto/ecdh.h>
#include <crypto/akcipher.h>
#include <crypto/ecc_curve.h>

#include "dwc-elppka.h"

#define ECC_CURVE_NIST_P192_DIGITS  3
#define ECC_CURVE_NIST_P256_DIGITS  4
#define ECC_CURVE_NIST_P384_DIGITS  6
#define ECC_CURVE_NIST_P521_DIGITS  9
#define ECC_MAX_DIGITS              DIV_ROUND_UP(521, 64) /* NIST P521 */

#define ECC_DIGITS_TO_BYTES_SHIFT 3

#define ECC_MAX_BYTES (ECC_MAX_DIGITS << ECC_DIGITS_TO_BYTES_SHIFT)

#define ECC_POINT_INIT(x, y, ndigits)    (struct ecc_point) { x, y, ndigits }

struct dwc_rsa_key {
    u8    *n;    // modulus: p*q
    u8    *e;    // public exponent
    u8    *d;    // private exponent
    int    e_bitlen;
    int    d_bitlen;
    int    bitlen;
    size_t    key_sz;
};

struct ecc_ctx {
    unsigned int curve_id;
    const struct ecc_curve *curve;

    bool pub_key_set;
    u64 x[ECC_MAX_DIGITS]; /* pub key x and y coordinates */
    u64 y[ECC_MAX_DIGITS];
    struct ecc_point pub_key;
};

struct ecdh_ctx {
    unsigned int curve_id;
    unsigned int ndigits;
    u64 private_key[ECC_MAX_DIGITS];
};

struct dwc_pka_dev{
    struct semaphore            firmware_loading, core_running;
    struct mutex                fw_mutex;
    struct pka_fw_priv          *fw;
    char                        fw_name[32];
    struct pka_state            pka_state;
    
    struct list_head            list;
    struct device               *dev;              // 设备指针
    struct clk                  *clk;              // 硬件时钟
    unsigned long               rate;
    struct reset_control        *rst;              // 复位控制

    u32 work_flags, saved_flags;
    u32 __iomem                 *base;             // I/O 内存基地址

    struct scatter_walk         in_walk;           // 输入散列遍历
    struct scatter_walk         out_walk;          // 输出散列遍历

    struct crypto_engine        *engine;           // 加密引擎
};

struct dwc_pka_request_ctx {
    struct scatterlist                *in_sg;
    struct scatterlist                *out_sg;
    size_t                            total;
    size_t                            nents;
    unsigned long                     in_sg_len;
    u8 rsa_data[] __aligned(sizeof(u32));
};

struct dwc_pka_ctx {
    struct dwc_pka_dev            *pka;
    struct dwc_pka_request_ctx    *rctx; 

    struct dwc_rsa_key            rsa_key;
    struct ecc_ctx                   *ecc_ctx;
    struct ecdh_ctx                  *ecdh_ctx;

    struct crypto_akcipher           *akcipher_fbk;
    struct crypto_kpp                *kpp_fbk;
};

struct dwc_dev_list {
    struct list_head        dev_list;
    spinlock_t              lock; /* protect dev_list */
};

/* ------- Hardware related helper functions ------- */
int pka_testflag(struct device *dev, char *flagname);
struct dwc_pka_dev *pka_find_dev(struct dwc_pka_ctx *ctx);
int pka_run(struct dwc_pka_ctx* ctx, char* entry, unsigned size, bool rsaorecc);

/* -------- ECC Functions --------- */
int pka_modinv(struct dwc_pka_ctx *ctx, const u64 *m, const u64 *x, u64 *result, unsigned key_sz);
int pka_modmult(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64 *result, unsigned key_sz);
int pka_modadd(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64 *result, unsigned key_sz); 
int pka_modsub(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64 *result, unsigned key_sz);
int pka_modreduce(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *m, u64 *result, unsigned key_sz); 
int pka_moddiv(struct dwc_pka_ctx *ctx, const u64 *x, const u64 *y, const u64 *m, u64 *result, unsigned key_sz);
int pka_pver(struct dwc_pka_ctx *ctx, const struct ecc_curve *curve, struct ecc_point *pk);
int pka_pmult(struct dwc_pka_ctx *ctx, const u64 *mult, const struct ecc_point *pk, const struct ecc_point *nQ, const struct ecc_curve *curve, unsigned bytelen);
int pka_shamir(struct dwc_pka_ctx *ctx, const struct ecc_curve *curve, const u64 *u1, const u64 *u2,
                const struct ecc_point *p, const struct ecc_point *q, struct ecc_point *r);

int dwc_ecc_make_pub_key(struct dwc_pka_ctx *ctx, unsigned int curve_id, unsigned int ndigits,
                        const u64 *private_key, u64 *public_key);
int dwc_ecc_is_pubkey_valid_partial(struct dwc_pka_ctx *ctx, const struct ecc_curve *curve, struct ecc_point *pk);
int dwc_ecc_is_pubkey_valid_full(struct dwc_pka_ctx *ctx, const struct ecc_curve *curve, struct ecc_point *pk);

/* -------- Register alg --------- */
int dwc_rsa_register_algs(void);
void dwc_rsa_unregister_algs(void);

int dwc_ecdsa_register_algs(void);
void dwc_ecdsa_unregister_algs(void);

int dwc_ecdh_register_algs(void);
void dwc_ecdh_unregister_algs(void);


