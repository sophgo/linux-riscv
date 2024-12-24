#pragma once
#include <linux/interrupt.h>      
#include <linux/ctype.h>          
#include <asm/io.h>
#include <linux/types.h>

struct pka_state {
   u32 __iomem    *regbase;
   
   struct pka_config {
       unsigned alu_size, rsa_size, ecc_size;
       unsigned fw_ram_size, fw_rom_size;
       unsigned ram_offset, rom_offset;
   } cfg;
}; 

struct pka_fw {                                                  
    unsigned long ram_size, rom_size;                             
    const char *errmsg;                                           
                                                                 
    struct pka_fw_tag {                                           
        unsigned long origin, tag_length, timestamp, md5_coverage; 
        unsigned char md5[16];                                     
    } ram_tag, rom_tag;                                           
                                                                 
    /* For internal use */                                        
    struct elppka_fw_priv *priv;                                  
};                                                               

struct pka_fw_priv {               
    struct pka_fw data;           
    struct completion fw_released;
    atomic_t refcount;            
};                                 

enum {
    PKA_OPERAND_A_BASE = 0x400,
    PKA_OPERAND_B_BASE = 0x800,
    PKA_OPERAND_C_BASE = 0xc00,
    PKA_OPERAND_D_BASE = 0x1000,
    PKA_BANK_END = 0x1400,    
};

int elppka_setup(struct pka_state *pka,  u32 *regbase);

int elppka_start(struct pka_state *pka, u32 entry, u32 flags, unsigned size);

void elppka_abort(struct pka_state *pka);

int elppka_get_status(struct pka_state *pka, unsigned *code);

int elppka_load_operand_rsa(struct pka_state *pka, unsigned bank, unsigned index, unsigned size, const u8 *data);

int elppka_load_operand_ecc(struct pka_state *pka, unsigned bank, unsigned index, unsigned size, const u32 *data);

void elppka_unload_operand_rsa(struct pka_state *pka, unsigned bank, unsigned index, unsigned size, u8 *data);

void elppka_unload_operand_ecc(struct pka_state *pka, unsigned bank, unsigned index, unsigned size, u32 *data);

void elppka_byteswap(struct pka_state *pka_state, bool doswap); 

/* Firmware image handling */                                    
int elppka_fw_parse(struct pka_fw *fw, const unsigned char *data, unsigned long len);       

void elppka_fw_free(struct pka_fw *fw);                          
                                                                 
int elppka_fw_lookup_entry(struct pka_fw *fw, const char *entry);
                                                                 
int elppka_fw_load(struct pka_state *pka, struct pka_fw *fw);    

struct pka_fw_priv *pka_get_firmware(struct pka_fw_priv *fw_priv);

void pka_put_firmware(struct pka_fw_priv *fw_priv);

/* The firmware timestamp epoch (2009-11-11 11:00:00Z) as a UNIX timestamp. */
#define PKA_FW_TS_EPOCH 1257937200ull

/* Resolution of the timestamp, in seconds. */
#define PKA_FW_TS_RESOLUTION 20
