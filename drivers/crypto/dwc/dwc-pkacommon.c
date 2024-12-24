#include <linux/interrupt.h>      
#include <linux/ctype.h>          
#include "dwc-pkahw.h"
#include "dwc-elppka.h"
#include "dwc-elperror.h"
#include "dwc-pka.h"

struct dwc_dev_list dev_list = {
    .dev_list = LIST_HEAD_INIT(dev_list.dev_list),
    .lock     = __SPIN_LOCK_UNLOCKED(dev_list.lock),
};

/* ------------ pka helper functions-------------*/
static int pka_lookup_flag(char* flagname)  
{                                                           
    int flagbit;                                            
    
    /* Absolute flag references. */                         
    if (flagname[0] == 'F') {                             
        /* User flags */                                    
       if (!isdigit(flagname[1]) || flagname[1] > '3') 
            return -ENOENT;                                 
       flagbit = PKA_FLAG_F0 + (flagname[1] - '0');      
       
       if (flagname[2] != 0)                             
            return -ENOENT;                                 
       
       return flagbit;                                     
   }                                                       
                                                            
   switch (flagname[0]) {                                
       case 'Z': flagbit = PKA_FLAG_ZERO;   break;             
       case 'M': flagbit = PKA_FLAG_MEMBIT; break;             
       case 'B': flagbit = PKA_FLAG_BORROW; break;             
       case 'C': flagbit = PKA_FLAG_CARRY;  break;             
       default:                                                
           return -ENOENT;                                   
   }                                                       
                                                            
   if (flagname[1] != 0)                                 
       return -ENOENT;                                     
                                                            
   return flagbit;                                         
}                                                           

int pka_testflag(struct device *dev, char* flagname)
{
   struct dwc_pka_dev *pka = dev_get_drvdata(dev);   
   u32 saved_flags;                                
   int rc;                                         
                                                   
   rc = pka_lookup_flag(flagname);                
   if (rc < 0)                                     
           return rc;                                  
                                                   
   rc = down_interruptible(&pka->core_running);   
   if (rc < 0)                                     
           return rc;                                  
                                                   
   saved_flags = pka->saved_flags;                

   up(&pka->core_running);

   return (saved_flags & (1ul << rc)) != 0;        

}

struct dwc_pka_dev *pka_find_dev(struct dwc_pka_ctx *ctx)
{
    struct dwc_pka_dev *pka = NULL, *tmp;

    spin_lock_bh(&dev_list.lock);
    if (!ctx->pka) {
        list_for_each_entry(tmp, &dev_list.dev_list, list) {
            pka = tmp;
            break;
        }
        ctx->pka = pka;
    } else {
        pka = ctx->pka;
    }

    spin_unlock_bh(&dev_list.lock);

    return pka;
}

int pka_run(struct dwc_pka_ctx* ctx, char* entry, unsigned size, bool rsaorecc)
{
    struct dwc_pka_dev *pka = ctx->pka;
    struct pka_fw_priv *fw_priv;
    unsigned code;
    int rc;

    printk("dwc pka start to run \n");

    mutex_lock(&pka->fw_mutex);         
    fw_priv = pka_get_firmware(pka->fw);
    mutex_unlock(&pka->fw_mutex);       
                                         
    if (!fw_priv)
        return -ENOENT;
    printk("pka: get firmware success\n");

    if (down_trylock(&pka->core_running)) {
        rc = -EBUSY;
        goto err;
    }

    // 2. get firmware entry point
    rc = elppka_fw_lookup_entry(&pka->fw->data, entry);
    if (rc < 0) {
        if (rc == CRYPTO_INVALID_FIRMWARE)                                     
            dev_err(pka->dev, "invalid firmware image: %s\n", fw_priv->data.errmsg);
        rc = dwc_elp_errorcode(rc);  
        goto err_unlock;
    }
    printk("pka: firmware look up entry: %s success", entry);

    // 2.5 set byteswap for different operation
    elppka_byteswap(&pka->pka_state, rsaorecc);

    // 3. run modexp
    dev_dbg(pka->dev, "CALL %s (0x%.4x)\n", entry, (unsigned)rc);
    rc = elppka_start(&pka->pka_state, rc, pka->work_flags, size);
    if (rc < 0) { 
        rc = dwc_elp_errorcode(rc);
        goto err_unlock;
    }
    printk("pka: start the firmware\n");
                                                                                
    // 4. wait
    dev_dbg(pka->dev, "WAIT\n");
    while(1) {
        rc = down_interruptible(&pka->core_running);
        if (rc == -1) {
            continue;
        }
        else if(rc < 0) {
            return rc;
        }
                                                                                
        rc = elppka_get_status(&pka->pka_state, &code);
        up(&pka->core_running);
        if(rc != 0) {
            printk("elppka error status %d\n", rc);
            return dwc_elp_errorcode(rc);
        }
        break;
    }
    if(code != 0) {
        dev_err(pka->dev, "pka get status: %d", code);
    }
    return code;

err_unlock:
    up(&pka->core_running);
err:
    pka_put_firmware(fw_priv);

    return rc;
}                                                                                   

