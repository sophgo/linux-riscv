#include <crypto/engine.h>
#include <linux/module.h>         
#include <linux/kernel.h>         
                                  
#include <linux/io.h>             
#include <linux/fs.h>             
#include <linux/ctype.h>          
#include <linux/platform_device.h>
#include <linux/firmware.h>       
#include <linux/interrupt.h>      
#include <linux/time.h>           
#include <linux/mutex.h>          
#include <linux/atomic.h>         
#include <linux/completion.h>     
#include <linux/semaphore.h>      
#include <linux/of.h>             
#include <asm/byteorder.h>        
#include <crypto/hash.h>          
                                  
#include <linux/clk.h>            
#include <linux/reset.h>          

#include "dwc-pkahw.h"
#include "dwc-elppka.h"
#include "dwc-elperror.h"
#include "dwc-pka.h"

static bool do_verify_fw = true;
module_param_named(noverify, do_verify_fw, invbool, S_IRUGO | S_IWUSR);
MODULE_PARM_DESC(noverify, "Skip firmware MD5 verification.");

extern struct dwc_dev_list dev_list;

/* ------------ static functions for device ---------*/
static irqreturn_t pka_irq_handler(int irq, void *dev)         
{                                                              
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);              
    u32 status;                                                
             
    status = readl(&pka->base[PKA_STATUS]);           
    if (!(status & (1 << PKA_STAT_IRQ))) {                     
        return IRQ_NONE;                                       
    }                                                          
                                                               
    writel(1 << PKA_STAT_IRQ, &pka->base[PKA_STATUS]); 
    pka->saved_flags = readl(&pka->base[PKA_FLAGS]); 
    pka->work_flags = 0;                                      
                                                               
    up(&pka->core_running);                                   
    pka_put_firmware(pka->fw);                                
                                                               
    return IRQ_HANDLED;                                        
}                                                              

static struct shash_desc *alloc_shash_desc(const char *name)
{
    struct crypto_shash *tfm;
    struct shash_desc *desc;

    tfm = crypto_alloc_shash(name, 0, 0);
    if (IS_ERR(tfm))
        return ERR_CAST(tfm);

    desc = kmalloc(sizeof(*desc) + crypto_shash_descsize(tfm), GFP_KERNEL);
    if (!desc) {
        crypto_free_shash(tfm);
        return ERR_PTR(-ENOMEM);
    }

    desc->tfm = tfm;

    return desc;
}

static void free_shash_desc(struct shash_desc *desc)
{
    crypto_free_shash(desc->tfm);
    kfree(desc);
}

/*
 * Calculate the MD5 hash of a region in the PKA firmware memory, starting
 * at the word pointed to by base with a length of nwords 32-bit words.  The
 * result is compared against the MD5 hash given by expect_md5.
*/
static int pka_fw_md5_readback(struct device *dev, u32 __iomem *base, size_t nwords,
                                  const unsigned char *expect_md5)
{
    unsigned char readback_md5[16];
    struct shash_desc *md5_desc;
    size_t i;
    int rc;

    if (!do_verify_fw) {
        dev_notice(dev, "skipping firmware verification as requested by user.\n");
        return 0;
    }

    md5_desc = alloc_shash_desc("md5");
    if (IS_ERR(md5_desc)) {
        /* non-fatal */
        dev_notice(dev, "skipping firmware verification as MD5 is not available.\n");
        return 0;
    }

    rc = crypto_shash_init(md5_desc);
    if (rc < 0)
        goto out;

    for (i = 0; i < nwords; i++) {
        u32 tmp = cpu_to_le32(readl(&base[i]));

        rc = crypto_shash_update(md5_desc, (u8 *)&tmp, sizeof tmp);
        if (rc < 0)
            goto out;
    }

    rc = crypto_shash_final(md5_desc, readback_md5);
    if (rc < 0)
        goto out;

    if (!memcmp(readback_md5, expect_md5, sizeof readback_md5))
        rc = 0;
    else
        rc = -EIO;
out:
    free_shash_desc(md5_desc);
    return rc;
}

/*
 * Readback the loaded RAM firmware and compare its MD5 hash against the hash
 * embedded in the tag.
*/
static int verify_ram_firmware(struct device *dev, const struct pka_fw *fw)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    unsigned long covlen;
    u32 __iomem *fw_base;
    int rc;

    if (WARN_ON_ONCE(!fw->ram_size))
        return -EINVAL;

    fw_base = pka->base + (pka->pka_state.cfg.ram_offset >> 2);

    if (fw->ram_tag.md5_coverage)
        covlen = fw->ram_tag.md5_coverage;
    else
        covlen = fw->ram_size - fw->ram_tag.tag_length;

    if (covlen > pka->pka_state.cfg.fw_ram_size - fw->ram_tag.tag_length) {
        dev_err(dev, "RAM hash coverage exceeds total RAM size\n");
        return -EINVAL;
    }

    rc = pka_fw_md5_readback(dev, &fw_base[fw->ram_tag.tag_length],
                                 covlen, fw->ram_tag.md5);
    if (rc < 0) {
        dev_err(dev, "RAM readback failed MD5 validation\n");
        return rc;
    }

    return 0;
}

/*
 * Ensure that the PKA ROM firmware matches the provided image by comparing
 * its MD5 hash against the tag in the image.
*/
static int verify_rom_firmware(struct device *dev, const struct pka_fw *fw)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    unsigned long covlen;
    u32 __iomem *fw_base;
    int rc;

    if (WARN_ON_ONCE(!fw->rom_size))
        return -EINVAL;

    fw_base = pka->base + (pka->pka_state.cfg.rom_offset >> 2);

    covlen = fw->rom_tag.md5_coverage;
    if (!covlen) {
        /* non-fatal */
        dev_notice(dev, "Skipping ROM verification as tag format is too old\n");

        return 0;
    }

    if (covlen > pka->pka_state.cfg.fw_rom_size - fw->rom_tag.tag_length) {
        dev_err(dev, "ROM hash coverage exceeds total ROM size\n");

        return -EINVAL;
    }

    rc = pka_fw_md5_readback(dev, &fw_base[fw->rom_tag.tag_length],
                                covlen, fw->rom_tag.md5);
    if (rc < 0) {
        dev_err(dev, "ROM readback failed MD5 validation\n");

        return rc;
    }

    return 0;
}

static int verify_firmware(struct device *dev, const struct pka_fw *fw)
{
    int rc;

    if (fw->ram_size) {
        rc = verify_ram_firmware(dev, fw);
        if (rc < 0)
            return rc;
    }

    if (fw->rom_size) {
        rc = verify_rom_firmware(dev, fw);
        if (rc < 0)
            return rc;
    }

    return 0;
}

static void pka_describe_firmware(struct device *dev, const struct pka_fw *fw)             
{                                                                                          
    char md5_hex[33];                                                                      
    struct tm tm;                                                                          
    size_t i;                                                                              
                                                                                           
    if (fw->ram_size) {                                                                    
        for (i = 0; i < sizeof fw->ram_tag.md5; i++)                                       
            sprintf(md5_hex+2*i, "%.2hhx", fw->ram_tag.md5[i]);                            
                                                                                               
        time64_to_tm(PKA_FW_TS_EPOCH + PKA_FW_TS_RESOLUTION*fw->ram_tag.timestamp, 0, &tm);
        dev_info(dev, "firmware RAM built on %.4ld-%.2d-%.2d %.2d:%.2d:%.2d UTC\n",        
                    tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,                                  
                    tm.tm_hour, tm.tm_min, tm.tm_sec);                                         
                                                                                               
        dev_info(dev, "firmware RAM size: %lu words\n", fw->ram_size);                     
        dev_info(dev, "firmware RAM MD5: %s\n", md5_hex);                                  
    }                                                                                      

    if (fw->rom_size) {                                                                    
        for (i = 0; i < sizeof fw->rom_tag.md5; i++)                                       
            sprintf(md5_hex + 2 * i, "%.2hhx", fw->rom_tag.md5[i]);                        
                                                                                               
        time64_to_tm(PKA_FW_TS_EPOCH + PKA_FW_TS_RESOLUTION*fw->rom_tag.timestamp, 0, &tm);
        dev_info(dev, "firmware ROM built on %.4ld-%.2d-%.2d %.2d:%.2d:%.2d UTC\n",        
                    tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,                                  
                    tm.tm_hour, tm.tm_min, tm.tm_sec);                                         
                                                                                               
        dev_info(dev, "firmware ROM MD5: %s\n", md5_hex);                                  
    }                                                                                      
}                                                                                          

static int pka_setup_firmware(struct device *dev, const struct firmware *fw,
                                 struct pka_fw_priv *fw_priv)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    int rc;

    atomic_set(&fw_priv->refcount, 1);
    init_completion(&fw_priv->fw_released);

    rc = elppka_fw_parse(&fw_priv->data, fw->data, fw->size);
    if (rc < 0) {
        if (rc == CRYPTO_INVALID_FIRMWARE)
            dev_err(dev, "invalid firmware image: %s\n", fw_priv->data.errmsg);
        return dwc_elp_errorcode(rc);
    }

    pka_describe_firmware(dev, &fw_priv->data);

    rc = elppka_fw_load(&pka->pka_state, &fw_priv->data);
    if (rc < 0) {
        if (rc == CRYPTO_INVALID_FIRMWARE)
            dev_err(dev, "cannot load firmware: %s\n", fw_priv->data.errmsg);
        return dwc_elp_errorcode(rc);
    }

    rc = verify_firmware(dev, &fw_priv->data);
    if (rc < 0)
        return rc;

    return 0;
}

static void pka_receive_firmware(const struct firmware *fw, void *dev)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    struct pka_fw_priv *fw_priv = NULL;
    int rc;

    if (!fw) {
        dev_info(dev, "firmware load cancelled\n");
        goto out;
    }

    fw_priv = kmalloc(sizeof *fw_priv, GFP_KERNEL);
    if (!fw_priv)
        goto out;

    rc = pka_setup_firmware(dev, fw, fw_priv);
    if (rc < 0) {
        kfree(fw_priv);
        goto out;
    }

    mutex_lock(&pka->fw_mutex);
    WARN_ON(pka->fw != NULL);
    pka->fw = fw_priv;
    mutex_unlock(&pka->fw_mutex);

out:
    up(&pka->firmware_loading);
    release_firmware(fw);

    return;
}

/*
 ** Remove the PKA firmware.  This involves preventing new users from accessing
 ** the firmware, waiting for all existing users to finish, then releasing the
 ** firmware resources.  If interruptible is false, then the wait cannot be
 ** interrupted and this function always succeeds (returns 0).  Otherwise, this
 ** function may be interrupted, in which case it will return a -ve error code
 ** only if the firmware was not freed (i.e., the firmware is still usable).
**/
static int pka_destroy_firmware(struct device *dev, bool interruptible)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    int rc = 0;

    mutex_lock(&pka->fw_mutex);

    if (!pka->fw) {
        /* Nothing to do. */
        goto out_unlock;
    }
    pka_put_firmware(pka->fw);

    if (interruptible)
        rc = wait_for_completion_interruptible(&pka->fw->fw_released);
    else
        wait_for_completion(&pka->fw->fw_released);

    if (rc < 0 && pka_get_firmware(pka->fw)) {
        /* Interrupted; firmware reference was restored. */
        goto out_unlock;
    }

    /* All references are gone; we can free the firmware. */
    kfree(pka->fw);

    pka->fw = NULL;
    rc = 0;

out_unlock:
    mutex_unlock(&pka->fw_mutex);

    return rc;
}

static int
pka_request_firmware(struct device *dev, bool uevent, const char *fmt, ...)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    va_list ap;
    int rc;

    va_start(ap, fmt);
    rc = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);

    if (rc >= sizeof pka->fw_name)
        return -EINVAL;

    if (down_trylock(&pka->firmware_loading))
        return -EAGAIN;

    rc = pka_destroy_firmware(dev, true);
    if (rc < 0) {
        up(&pka->firmware_loading);

        return rc;
    }

    va_start(ap, fmt);
    vsprintf(pka->fw_name, fmt, ap);
    va_end(ap);

    dev_info(dev, "requesting %s (%s)\n", pka->fw_name,
                uevent ? "auto" : "manual");

    rc = request_firmware_nowait(THIS_MODULE, uevent, pka->fw_name, dev,
                GFP_KERNEL, dev, pka_receive_firmware);
    if (rc < 0)
        up(&pka->firmware_loading);

    return rc;
}

static ssize_t
store_load_firmware(struct device *dev, struct device_attribute *devattr,
                         const char *buf, size_t count)
{
    char *c;
    int rc;

    c = strchr(buf, '\n');
    if (c) {
        if (c[1] != '\0')
            return -EINVAL;
        *c = '\0';
    }

    if (strchr(buf, '/'))
        return -EINVAL;

    rc = pka_request_firmware(dev, false, "%s", buf);

    return rc < 0 ? rc : count;
}

static ssize_t
show_watchdog(struct device *dev, struct device_attribute *devattr, char *buf)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    unsigned long val;

    val = readl(&pka->base[PKA_WATCHDOG]);
    return sprintf(buf, "%lu\n", val);
}

static ssize_t
store_watchdog(struct device *dev, struct device_attribute *devattr,
                  const char *buf, size_t count)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    u32 val;
    int rc;

    rc = kstrtou32(buf, 0, &val);
    if (rc < 0)
        return rc;

    rc = down_interruptible(&pka->core_running);
    if (rc < 0)
        return rc;

    writel(val, &pka->base[PKA_WATCHDOG]);

    up(&pka->core_running);

    return count;
}

static ssize_t
show_prob(struct device *dev, struct device_attribute *devattr, char *buf)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    unsigned long val;

    val = readl(&pka->base[PKA_DTA_JUMP]) >> PKA_DTA_JUMP_PROBABILITY;
    val &= (1ul << PKA_DTA_JUMP_PROBABILITY_BITS) - 1;

    return sprintf(buf, "%lu\n", val);
}

static ssize_t
store_prob(struct device *dev, struct device_attribute *devattr,
             const char *buf, size_t count)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    u32 val;
    int rc;

    rc = kstrtou32(buf, 0, &val);
    if (rc < 0)
        return rc;

    if (val >= (1ul << PKA_DTA_JUMP_PROBABILITY_BITS))
        return -ERANGE;

    rc = down_interruptible(&pka->core_running);
    if (rc < 0)
        return rc;

    writel(val, &pka->base[PKA_DTA_JUMP]);

    up(&pka->core_running);

    return count;
}

static int get_fw_tag(struct device *dev, bool rom, struct pka_fw_tag *tag)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    struct pka_fw_priv *fw_priv;
    int rc;

    rc = down_interruptible(&pka->firmware_loading);
    if (rc < 0)
        return rc;

    mutex_lock(&pka->fw_mutex);
    fw_priv = pka_get_firmware(pka->fw);
    mutex_unlock(&pka->fw_mutex);

    up(&pka->firmware_loading);

    if (!fw_priv)
        return -ENODEV;

    if ((!rom && !fw_priv->data.ram_size) ||
         (rom && !fw_priv->data.rom_size)) {
        pka_put_firmware(fw_priv);
        return -ENXIO;
    }

    if (rom)
        *tag = fw_priv->data.rom_tag;
    else
        *tag = fw_priv->data.ram_tag;

    pka_put_firmware(fw_priv);

    return 0;
}

static ssize_t
show_fw_ts(struct device *dev, struct device_attribute *devattr, char *buf)
{
    unsigned long long seconds;
    struct pka_fw_tag tag;
    bool rom = false;
    int rc;

    if (!strncmp(devattr->attr.name, "fw_rom", 6))
        rom = true;

    rc = get_fw_tag(dev, rom, &tag);
    if (rc < 0)
        return rc;

    seconds = tag.timestamp * PKA_FW_TS_RESOLUTION;

    return sprintf(buf, "%llu\n", PKA_FW_TS_EPOCH + seconds);
}

static ssize_t
show_fw_md5(struct device *dev, struct device_attribute *devattr, char *buf)
{
    struct pka_fw_tag tag;
    bool rom = false;
    size_t i;
    int rc;

    if (!strncmp(devattr->attr.name, "fw_rom", 6))
        rom = true;

    rc = get_fw_tag(dev, rom, &tag);
    if (rc < 0)
        return rc;

    for (i = 0; i < sizeof tag.md5; i++)
        sprintf(buf + 2 * i, "%.2hhx\n", tag.md5[i]);

    return 2 * i + 1;
}

static ssize_t
show_size(struct device *dev, struct device_attribute *devattr, char *buf)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    unsigned val;

    switch (devattr->attr.name[0]) {
    case 'a': val = pka->pka_state.cfg.alu_size; break;
    case 'r': val = pka->pka_state.cfg.rsa_size; break;
    case 'e': val = pka->pka_state.cfg.ecc_size; break;
    default: return -EINVAL;
    }

    return sprintf(buf, "%u\n", val);
}


/* Control settings */
static DEVICE_ATTR(watchdog,         0644, show_watchdog, store_watchdog);
static DEVICE_ATTR(jump_probability, 0600, show_prob,     store_prob);

/* Configuration information */
static DEVICE_ATTR(alu_size, 0444, show_size, NULL);
static DEVICE_ATTR(rsa_size, 0444, show_size, NULL);
static DEVICE_ATTR(ecc_size, 0444, show_size, NULL);

/* Firmware information */
static DEVICE_ATTR(load_firmware,   0200, NULL,        store_load_firmware);
static DEVICE_ATTR(fw_timestamp,     0444, show_fw_ts,  NULL);
static DEVICE_ATTR(fw_md5sum,       0444, show_fw_md5, NULL);
static DEVICE_ATTR(fw_rom_timestamp, 0444, show_fw_ts,  NULL);
static DEVICE_ATTR(fw_rom_md5sum,   0444, show_fw_md5, NULL);

static const struct attribute_group pka_attr_group = {
    .attrs = (struct attribute *[]) {
        &dev_attr_watchdog.attr,
        &dev_attr_jump_probability.attr,
        &dev_attr_alu_size.attr,
        &dev_attr_rsa_size.attr,
        &dev_attr_ecc_size.attr,
        &dev_attr_load_firmware.attr,
        &dev_attr_fw_timestamp.attr,
        &dev_attr_fw_md5sum.attr,
        &dev_attr_fw_rom_timestamp.attr,
        &dev_attr_fw_rom_md5sum.attr,
        NULL
    },
};

/* Print out a description of the probed device. */
static void pka_describe_device(struct device *dev)
{
    struct dwc_pka_dev *pka = dev_get_drvdata(dev);
    const struct pka_config *cfg = &pka->pka_state.cfg;

    dev_info(dev, "Synopsys, Inc. Public Key Accelerator\n");
    if (cfg->rsa_size && cfg->ecc_size) {
        dev_info(dev, "supports %u-bit RSA, %u-bit ECC with %u-bit ALU\n",
                 cfg->rsa_size, cfg->ecc_size, cfg->alu_size);
    } else if (cfg->rsa_size) {
        dev_info(dev, "supports %u-bit RSA (no ECC) with %u-bit ALU\n",
                 cfg->rsa_size, cfg->alu_size);
    } else if (cfg->ecc_size) {
        dev_info(dev, "supports %u-bit ECC (no RSA) with %u-bit ALU\n",
                 cfg->ecc_size, cfg->alu_size);
    }

    dev_info(dev, "firmware RAM: %u words, ROM: %u words\n",
             cfg->fw_ram_size, cfg->fw_rom_size);
}

static int plat_probe(struct platform_device *pdev)
{
    struct dwc_pka_dev *pka = platform_get_drvdata(pdev);
    struct device *dev = &pdev->dev;
    int err;
    dev_info(pka->dev, "plat_probe\n");

    pka->clk = devm_clk_get(dev, NULL);
    if (IS_ERR(pka->clk)) {
        dev_err(dev, "cannot get clock\n");
        return PTR_ERR(pka->clk);
    }

    pka->rst = devm_reset_control_get_optional_shared(dev, NULL);
    if (IS_ERR(pka->rst)) {
        dev_err(dev, "cannot get reset control\n");
        return PTR_ERR(pka->rst);
    }

    dev_dbg(dev, "enable pka clock\n");
    err = clk_prepare_enable(pka->clk);
    if (err) {
        dev_err(dev, "prepare enable clock failed\n");
        return err;
    }

    dev_dbg(dev, "get pka clock rate\n");
    pka->rate = clk_get_rate(pka->clk);
    if (pka->rate == 0) {
        dev_err(dev, "clock frequency is 0\n");
        err = -ENODEV;
        return err;
    }

    dev_dbg(dev, "pka clock rate is %lu\n", pka->rate);

    dev_dbg(dev, "deassert pka reset\n");
    err = reset_control_deassert(pka->rst);
    if (err) {
        dev_err(dev, "deassert reset failed\n");
        return err;
    }

    return 0;
}

static int dwc_pka_probe(struct platform_device *pdev)
{                                                           
    struct dwc_pka_dev *pka;
    struct resource *mem_resource;
    int irq;
    int rc;

    pka = devm_kzalloc(&pdev->dev, sizeof *pka, GFP_KERNEL);
    if(!pka)
        return -ENOMEM;
    
    sema_init(&pka->firmware_loading, 1);
    sema_init(&pka->core_running, 1);
    mutex_init(&pka->fw_mutex);

    platform_set_drvdata(pdev, pka);
    pka->dev = &pdev->dev;
    pka->base = devm_platform_get_and_ioremap_resource(pdev, 0, &mem_resource);
    if (IS_ERR(pka->base)) 
        return dev_err_probe(&pdev->dev, PTR_ERR(pka->base),
            "Error remapping memory for platform device\n");
    dev_info(pka->dev, "Resource start: 0x%llx, end: 0x%llx, flags: 0x%lx, virtual base = %p\n",
         (unsigned long long)mem_resource->start,
         (unsigned long long)mem_resource->end,
         mem_resource->flags, 
         pka->base);

    irq = platform_get_irq(pdev, 0);
    if (!irq) {
        printk("pka: Error get irq number\n");
        return -EINVAL;
    }                                            
    rc = devm_request_irq(&pdev->dev, irq, pka_irq_handler,
                              IRQF_SHARED, dev_name(&pdev->dev), &pdev->dev);      
    if (rc < 0) {
        printk("pka: Error devm request irq\n");
        return rc;
    }                                                            
    
    printk("pka: start plat probe\n");
    rc = plat_probe(pdev);
    if (rc)
        return rc;

    printk("pka: elppka start setup\n");
    rc = elppka_setup(&pka->pka_state, pka->base);
    if (rc < 0) {
        dev_err(&pdev->dev, "Failed to initialize PKA\n");
        clk_disable_unprepare(pka->clk);
        reset_control_assert(pka->rst);
        return dwc_elp_errorcode(rc);
    }

    pka_describe_device(&pdev->dev);
    
    writel(100000000, &pka->base[PKA_WATCHDOG]);
    writel(1 << PKA_IRQ_EN_STAT, &pka->base[PKA_IRQ_EN]);
    
    printk("pka: request firmware\n");
    rc = pka_request_firmware(&pdev->dev, true, "elppka.elf");
    if (rc < 0) 
        goto err;

    printk("pka: engine init\n");
    pka->engine = crypto_engine_alloc_init(&pdev->dev, 1);
    if (!pka->engine) {
        rc = -ENOMEM;
        goto err;
    } 

    rc = crypto_engine_start(pka->engine);
    if (rc)
        goto err_engine_start;

    printk("pka: rsa register algs\n");
    rc = dwc_rsa_register_algs();
    if (rc) 
        goto err_algs_rsa;        

    printk("pka: ecdh register algs\n");
    rc = dwc_ecdh_register_algs();
    if (rc)
        goto err_algs_ecdh;

    printk("pka: ecdsa register algs\n");
    rc = dwc_ecdsa_register_algs();
    if (rc)
        goto err_algs_ecdsa;

    rc = sysfs_create_group(&pdev->dev.kobj, &pka_attr_group);
    if (rc)
        goto err_sysfsgroup;

    spin_lock(&dev_list.lock);
    list_add(&pka->list, &dev_list.dev_list);
    spin_unlock(&dev_list.lock);

    printk("pka: probe success.\n");
    return 0;

err_sysfsgroup:
    dwc_ecdsa_unregister_algs();
err_algs_ecdsa:
    dwc_ecdh_unregister_algs();
err_algs_ecdh:
    dwc_rsa_unregister_algs();
err_algs_rsa:
    crypto_engine_stop(pka->engine);
err_engine_start:
    crypto_engine_exit(pka->engine);
err:
    clk_disable_unprepare(pka->clk);
    reset_control_assert(pka->rst);

    return rc;
}

static void dwc_pka_remove(struct platform_device *pdev)
{
    struct dwc_pka_dev *pka = platform_get_drvdata(pdev);

    sysfs_remove_group(&pdev->dev.kobj, &pka_attr_group);

    /* Wait for a pending firmware load to complete. */
    if (down_trylock(&pka->firmware_loading)) {
        dev_warn(&pdev->dev, "device removal blocked on pending firmware load\n");
        down(&pka->firmware_loading);
    }

    pka_destroy_firmware(&pdev->dev, false);
    writel(0, &pka->base[PKA_IRQ_EN]);

    spin_lock(&dev_list.lock);
    list_del(&pka->list);
    spin_unlock(&dev_list.lock);

    dwc_rsa_unregister_algs();
    crypto_engine_stop(pka->engine);
    crypto_engine_exit(pka->engine);

    clk_disable_unprepare(pka->clk);
    reset_control_assert(pka->rst);
}

static const struct of_device_id pka_match[] = {
    { .compatible = "snps,designware-pka", },   
    { .compatible = "snps,pka", },
    { },                                        
};                                              

static struct platform_driver pka_driver = {
    .probe  = dwc_pka_probe,                    
    .remove = dwc_pka_remove,                   
                                            
    .driver = {                             
        .name   = "pka",                  
        .owner  = THIS_MODULE,              
        .of_match_table = pka_match,        
    },                                      
};                                          

module_platform_driver(pka_driver);

MODULE_LICENSE("GPL");          
MODULE_AUTHOR("Synopsys, Inc.");
