#include <linux/string.h>
#include "dwc-elperror.h"
#include "dwc-elppka.h"
#include "dwc-pkahw.h"


/* Parse out the fields from a type-0 BUILD_CONF register in bc. */
static void elppka_get_config_type0(u32 bc, struct pka_config *out)
{  
    struct pka_config cfg = {0};
      
    if (bc & (1ul << PKA_BC_FW_HAS_RAM)) {
        cfg.fw_ram_size = 256u << ((bc >> PKA_BC_FW_RAM_SZ)
                            & ((1ul << PKA_BC_FW_RAM_SZ_BITS)-1));
    } 
    if (bc & (1ul << PKA_BC_FW_HAS_ROM)) {
        cfg.fw_rom_size = 256u << ((bc >> PKA_BC_FW_ROM_SZ)
                                   & ((1ul << PKA_BC_FW_ROM_SZ_BITS)-1));
    }

    cfg.alu_size = 32u << ((bc >> PKA_BC_ALU_SZ)
                           & ((1ul << PKA_BC_ALU_SZ_BITS)-1));
    cfg.rsa_size = 512u << ((bc >> PKA_BC_RSA_SZ)
                            & ((1ul << PKA_BC_RSA_SZ_BITS)-1));
    cfg.ecc_size = 256u << ((bc >> PKA_BC_ECC_SZ)
                            & ((1ul << PKA_BC_ECC_SZ_BITS)-1));

    *out = cfg;
}

/* Parse out the fields from a type-1 BUILD_CONF register in bc. */
static void elppka_get_config_type1(u32 bc, struct pka_config *out)
{
    struct pka_config cfg = {0};
    u32 tmp;

    tmp = (bc >> PKA_BC1_FW_RAM_SZ) & ((1ul << PKA_BC1_FW_RAM_SZ_BITS)-1);
    if (tmp)
        cfg.fw_ram_size = 256u << (tmp-1);

    tmp = (bc >> PKA_BC1_FW_ROM_SZ) & ((1ul << PKA_BC1_FW_ROM_SZ_BITS)-1);
    if (tmp)
        cfg.fw_rom_size = 256u << (tmp-1);

    tmp = (bc >> PKA_BC1_RSA_SZ) & ((1ul << PKA_BC1_RSA_SZ_BITS)-1);
    if (tmp)
        cfg.rsa_size = 512u << (tmp-1);

    tmp = (bc >> PKA_BC1_ECC_SZ) & ((1ul << PKA_BC1_ECC_SZ_BITS)-1);
    if (tmp)
        cfg.ecc_size = 256u << (tmp-1);

    tmp = (bc >> PKA_BC1_ALU_SZ) & ((1ul << PKA_BC1_ALU_SZ_BITS)-1);
    cfg.alu_size = 32u << tmp;

    *out = cfg;
}

/* Read out PKA H/W configuration into config structure. */
static int elppka_get_config(u32 *regs, struct pka_config *out)
{
    u32 bc = readl(&regs[PKA_BUILD_CONF]);

    unsigned type = bc >> PKA_BC_FORMAT_TYPE;
    type &= (1ul << PKA_BC_FORMAT_TYPE_BITS) - 1;

    switch (type) {
    case 0:
        elppka_get_config_type0(bc, out);
        break;
    case 1:
    case 2: /* Type 2 has same format as type 1 */
        elppka_get_config_type1(bc, out);
        break;
    }

    /* RAM/ROM base addresses depend on core version */
    if (type < 2) {
        out->ram_offset = PKA_FIRMWARE_BASE;
        out->rom_offset = PKA_FIRMWARE_BASE + out->fw_ram_size;
    } else {
        out->ram_offset = out->rom_offset = PKA_FIRMWARE_T2_BASE;
        if (out->fw_ram_size)
             out->rom_offset = PKA_FIRMWARE_T2_SPLIT;
    }

    return 0;
}

int elppka_start(struct pka_state *pka_state, u32 entry, u32 flags,
                    unsigned size)
{
    u32 ctrl;

    /* Handle ECC-521 oddities as a special case. */
    if (size == PKA_ECC521_OPERAND_SIZE) {
        flags |= 1ul << PKA_FLAG_F1;
        ctrl  |= PKA_CTRL_M521_ECC521 << PKA_CTRL_M521_MODE;

        /* Round up partial radix to multiple of ALU size. */
        size = (512 + pka_state->cfg.alu_size)/8;

        // and need to set base_radix, not implemented here.
        ctrl |= (size & (size-1) ? (size+3)/4 : 0) << PKA_CTRL_PARTIAL_RADIX;
    }

    ctrl = readl(&pka_state->regbase[PKA_CTRL]);
    ctrl |= 1ul << PKA_CTRL_GO;
    
    writel(0, &pka_state->regbase[PKA_INDEX_I]);
    writel(0, &pka_state->regbase[PKA_INDEX_J]);
    writel(0, &pka_state->regbase[PKA_INDEX_K]);
    writel(0, &pka_state->regbase[PKA_INDEX_L]);

    writel(0, &pka_state->regbase[PKA_F_STACK]);
    writel(flags, &pka_state->regbase[PKA_FLAGS]);
    writel(entry, &pka_state->regbase[PKA_ENTRY]);
    writel(ctrl, &pka_state->regbase[PKA_CTRL]);

    return 0;
}

int elppka_get_status(struct pka_state *pka_state, unsigned *code)            
{                                                                       
    u32 status = readl(&pka_state->regbase[PKA_RC]);                 
 
    if (status & (1 << PKA_RC_BUSY)) {                                              
        return CRYPTO_INPROGRESS;                                                      
    }                                                                                          
    
    if (code) {                                                                             
        *code = (status >> PKA_RC_REASON) & ((1 << PKA_RC_REASON_BITS)-1);
    }                                                                                          

    return 0;                                                                                
}                                                                                              


int elppka_load_operand_rsa(struct pka_state *pka_state, unsigned bank, 
                               unsigned index, unsigned size, const u8 *data)
{
    u32 __iomem *opbase, tmp;
    unsigned i, n;     

    if (size > pka_state->cfg.rsa_size) {
        printk("pka: operand size out of limit\n");
        return CRYPTO_INVALID_SIZE;
    }

    opbase = pka_state->regbase + (bank >> 2) + (index << PKA_RSA_DATA_BITS >> 2);

    n = size >> 2;
    for (i = 0; i < n; i++) {
         /*
          * For lengths that are not a multiple of 4, the incomplete word is
          * at the _start_ of the data buffer, so we must add the remainder.
          */
printk("data: %p, opbase: %p, i: %d, n: %d, size: %d\n", data, opbase, i, n, size);
        memcpy(&tmp, data+((n-i-1)<<2)+(size&3), 4);
printk("data: %x\n", tmp);
        writel(tmp, &opbase[i]);
    }
                                                                                                                
     /* Write the incomplete word, if any. */
    if (size & 3) {
         tmp = 0;
         memcpy((char *)&tmp + sizeof tmp - (size&3), data, size & 3);
printk("data: %x\n", tmp); 
         writel(tmp, &opbase[i++]);
    }
printk("here 2\n");

    return 0;
}

static inline void dwc_ecc_swap_bytes(const u32 *in, u32 *out, unsigned digits)
{
    int num32 = digits << 1;
     
    for (int i = 0; i < num32; i++) {
        if (i % 2) {
            out[i] = in[num32 - i];
        } else {
            out[i] = in[num32 - i - 2];
        }
    }
}

int elppka_load_operand_ecc(struct pka_state *pka_state, unsigned bank, 
                                unsigned index, unsigned digits, const u32 *data)
{
    u32 __iomem *opbase, *tmp;
    int i;

    if (digits << 3 > pka_state->cfg.ecc_size) {
        printk("pka: operand size out of limit\n");
        return CRYPTO_INVALID_SIZE;
    }

    opbase = pka_state->regbase + (bank >> 2) + (index << PKA_ECC_DATA_BITS >> 2);

    tmp = kmalloc(digits << 3, GFP_KERNEL);
    memset(tmp, 0, digits << 3);
    dwc_ecc_swap_bytes(data, tmp, digits);
    for (i = 0; i < digits << 1; i++) {
        printk("ecc load data: %x\n", tmp[i]);
        writel(tmp[i], &opbase[i]);
    }
    kfree(tmp);

    return 0;
}

void elppka_unload_operand_rsa(struct pka_state *pka_state, unsigned bank, unsigned index, unsigned size, u8 *data)
{
    u32 *opbase, tmp;
    unsigned i, n;

    printk("pka unload result data rsa: ");
    opbase = pka_state->regbase + (bank >> 2) + (index << PKA_RSA_DATA_BITS >> 2);

    n = size >> 2;

    for (i = 0; i < n; i++) {
        tmp = readl(&opbase[i]);
        memcpy(data+((n-i-1)<<2)+(size&3), &tmp, 4);
        printk(" %x ", tmp);
    }

    if (size & 3) {
        tmp = readl(&opbase[i]);
        memcpy(data, (char *)&tmp + sizeof tmp - (size&3), size & 3);
        printk(" %x ", tmp);
    }
    printk("\n");
}

void elppka_unload_operand_ecc(struct pka_state *pka_state, unsigned bank, unsigned index, unsigned digits, u32 *data)
{
    u32 *opbase;
    int i;

    printk("pka unload result data ecc: ");
    opbase = pka_state->regbase + (bank >> 2) + (index << PKA_ECC_DATA_BITS >> 2);

    for (i = 0; i < digits << 1; i++) {
        data[i] = readl(&opbase[i]); 
        printk(" %x ", data[i]);
    }
    printk("\n");
}

int elppka_setup(struct pka_state *pka_state, u32 *regbase)
{
    int rc;
    unsigned i;

    pka_state->regbase = regbase;
    rc = elppka_get_config(regbase, &pka_state->cfg);
    if (rc < 0)
        return rc;
    
    for (i = PKA_OPERAND_A_BASE >> 2; i < PKA_BANK_END >> 2; i++) {
        writel(0, regbase + i);
    }

    return 0;
}

void elppka_byteswap(struct pka_state *pka_state, bool doswap) 
{
    u32 val;
    val = readl(&pka_state->regbase[PKA_CONF]);
    if (doswap) {
        val |= 1 << PKA_CONF_BYTESWAP;
    } else {
        val &= ~(1 << PKA_CONF_BYTESWAP);
    }
    writel(val, &pka_state->regbase[PKA_CONF]);
}
