// ------------------------------------------------------------------------
//
//                (C) COPYRIGHT 2011 - 2015 SYNOPSYS, INC.
//                          ALL RIGHTS RESERVED
//
//  This program is free software; you can redistribute it and/or
//  modify it under the terms of the GNU General Public License
//  version 2 as published by the Free Software Foundation.
//
//  This program is distributed in the hope that it will be useful,
//  but WITHOUT ANY WARRANTY; without even the implied warranty of
//  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//  GNU General Public License for more details.
//
//  You should have received a copy of the GNU General Public License
//  along with this program; if not, see <https://gnu.org/licenses/>.
//
// ------------------------------------------------------------------------

#include <linux/vmalloc.h>
#include <linux/ctype.h>

#include "dwc-pkahw.h"
#include "dwc-elppka.h"
#include "dwc-elperror.h"
#include "dwc-elppka_elf.h"

struct elppka_fw_priv {
    uint32_t *ram_base, *rom_base;
    unsigned long nsyms, nstr;

    Elf32_Ehdr ehdr;

    char *strtab;
    Elf32_Sym *symtab;
};

static uint16_t read_le16(const unsigned char *buf)
{
    return (buf[0] & 0xff) |
           ((buf[1] & 0xff) << 8);
}

static uint32_t read_le32(const unsigned char *buf)
{
    return (buf[0] & 0xff)
        | ((buf[1] & 0xff) << 8)
        | ((buf[2] & 0xff) << 16)
        | ((buf[3] & 0xff) << 24);
}

static void copy_ehdr(Elf32_Ehdr *ehdr, const unsigned char *buf)
{
    memcpy(ehdr->e_ident, buf, 16);
    ehdr->e_type      = read_le16(buf + 16);
    ehdr->e_machine   = read_le16(buf + 18);
    ehdr->e_version   = read_le32(buf + 20);
    ehdr->e_entry     = read_le32(buf + 24);
    ehdr->e_phoff     = read_le32(buf + 28);
    ehdr->e_shoff     = read_le32(buf + 32);
    ehdr->e_flags     = read_le32(buf + 36);
    ehdr->e_ehsize    = read_le16(buf + 40);
    ehdr->e_phentsize = read_le16(buf + 42);
    ehdr->e_phnum     = read_le16(buf + 44);
    ehdr->e_shentsize = read_le16(buf + 46);
    ehdr->e_shnum     = read_le16(buf + 48);
    ehdr->e_shstrndx  = read_le16(buf + 50);
}

static void copy_shdr(Elf32_Shdr *shdr, const unsigned char *buf)
{
    shdr->sh_name      = read_le32(buf +  0);
    shdr->sh_type      = read_le32(buf +  4);
    shdr->sh_flags     = read_le32(buf +  8);
    shdr->sh_addr      = read_le32(buf + 12);
    shdr->sh_offset    = read_le32(buf + 16);
    shdr->sh_size      = read_le32(buf + 20);
    shdr->sh_link      = read_le32(buf + 24);
    shdr->sh_info      = read_le32(buf + 28);
    shdr->sh_addralign = read_le32(buf + 32);
    shdr->sh_entsize   = read_le32(buf + 36);
}

static void copy_sym(Elf32_Sym *sym, const unsigned char *buf)
{
    sym->st_name  = read_le32(buf +  0);
    sym->st_value = read_le32(buf +  4);
    sym->st_size  = read_le32(buf +  8);
    sym->st_info  = buf[12];
    sym->st_other = buf[13];
    sym->st_shndx = read_le16(buf + 14);
}

static void copy_words(uint32_t *out, const unsigned char *in, size_t count)
{
    size_t i;

    for (i = 0; i < count; i++)
        out[i] = read_le32(in + 4 * i);
}

/*
 * Check that (count*entsize + offset) < total, while avoiding any overflow.
 */
static int region_fits(unsigned long total, unsigned long offset,
            unsigned long entsize, unsigned long count)
{
    if (total < offset)
        return 0;

    if ((total - offset) / count < entsize)
        return 0;

    return 1;
}

/*
 * Parse ELF header and initialize the F/W struct
 */
static int pka_fw_init(struct pka_fw *fw, const unsigned char *base,
                unsigned long len)
{
    static const unsigned char expected_magic[EI_NIDENT] = {
        [EI_MAG0]    = ELFMAG0,
        [EI_MAG1]    = ELFMAG1,
        [EI_MAG2]    = ELFMAG2,
        [EI_MAG3]    = ELFMAG3,
        [EI_CLASS]   = ELFCLASS32,
        [EI_DATA]    = ELFDATA2LSB,
        [EI_VERSION] = EV_CURRENT,
        [EI_OSABI]   = 255, /* ELFOSABI_STANDALONE */
    };

    struct elppka_fw_priv tmp_priv = {0};
    struct pka_fw tmp = {0};
    Elf32_Ehdr ehdr;

    /* Ensure a sane default state so that pka_fw_free can be called. */
    fw->priv = NULL;

    if (len < sizeof ehdr) {
        fw->errmsg = "cannot read ELF header";
        return CRYPTO_INVALID_FIRMWARE;
    }
    copy_ehdr(&ehdr, base);

    /* Verify ELF header sanity. */
    if (memcmp(ehdr.e_ident, expected_magic, sizeof expected_magic)
            || ehdr.e_machine != 0xe117
            || ehdr.e_type != ET_EXEC
            || (ehdr.e_shnum && ehdr.e_shnum <= ehdr.e_shstrndx)
            || ehdr.e_shentsize != sizeof (Elf32_Shdr)) {
        fw->errmsg = "invalid ELF header";
        return CRYPTO_INVALID_FIRMWARE;
    }

    /* Verify that section header table fits in the data. */
    if (!region_fits(len, ehdr.e_shoff, ehdr.e_shentsize, ehdr.e_shnum)) {
        fw->errmsg = "cannot read section header table";
        return CRYPTO_INVALID_FIRMWARE;
    }

    tmp.priv = vmalloc(sizeof *tmp.priv);
    if (!tmp.priv)
        return CRYPTO_NO_MEM;

    tmp_priv.ehdr = ehdr;
    *tmp.priv = tmp_priv;

    tmp.errmsg = "";
    *fw = tmp;

    return 0;
}

/*
 * Read the section header at the corresponding index.  The fields of the
 * section header are validated for sanity, including that the size/offset
 * refer to a region that is actually present within the file.
 */
static int pka_fw_read_shdr(struct pka_fw *fw,
        const unsigned char *base, unsigned long len,
        unsigned long index, Elf32_Shdr *shdr)
{
    Elf32_Off pos = fw->priv->ehdr.e_shoff;

    if (fw->priv->ehdr.e_shnum <= index)
        return CRYPTO_INVALID_ARGUMENT;

    pos += index * fw->priv->ehdr.e_shentsize;
    copy_shdr(shdr, base + pos);

    /* Alignment of 0 means the same thing as 1. */
    if (shdr->sh_addralign == 0)
        shdr->sh_addralign = 1;

    if (fw->priv->ehdr.e_shnum <= shdr->sh_link) {
        fw->errmsg = "invalid section link";
        return CRYPTO_INVALID_FIRMWARE;
    }

    if (shdr->sh_type != SHT_NOBITS) {
        if (!region_fits(len, shdr->sh_offset, shdr->sh_size, 1)) {
            fw->errmsg = "cannot read section";
            return CRYPTO_INVALID_FIRMWARE;
        }

        /* Section alignment must be a power of 2. */
        if (shdr->sh_addralign & (shdr->sh_addralign - 1)) {
            fw->errmsg = "invalid section alignement";
            return CRYPTO_INVALID_FIRMWARE;
        }

        if ((shdr->sh_offset & (shdr->sh_addralign - 1)) != 0) {
            fw->errmsg = "unaligned section";
            return CRYPTO_INVALID_FIRMWARE;
        }

        if (shdr->sh_entsize && shdr->sh_size % shdr->sh_entsize != 0) {
            fw->errmsg = "incomplete section";
            return CRYPTO_INVALID_FIRMWARE;
        }
    }

    return 0;
}

/*
 * Look up a symbol by name; if found, return the index of the matching symbol
 * table entry.
 */
static int lookup_symbol(struct pka_fw *fw, const char *name)
{
    unsigned long i;

    for (i = 0; i < fw->priv->nsyms && i <= INT_MAX; i++) {
        if (fw->priv->symtab[i].st_name < fw->priv->nstr
            && !strcmp(fw->priv->strtab + fw->priv->symtab[i].st_name, name)) {
            return i;
        }
    }

    return CRYPTO_NOT_FOUND;
}

int elppka_fw_lookup_entry(struct pka_fw *fw, const char *entry)
{
    char symname[64];
    int i, rc;

    if (strlen(entry) + sizeof "ENTRY_" > sizeof symname)
        return CRYPTO_INVALID_ARGUMENT;

    sprintf(symname, "ENTRY_%s", entry);
    for (i = 0; symname[i]; i++) {
        symname[i] = toupper(symname[i]);
    }

    rc = lookup_symbol(fw, symname);
    if (rc < 0)
        return rc;

    if (fw->priv->symtab[rc].st_value / 4 > INT_MAX) {
        /* Bogus symbol; typical firmware images have ~1-2K words. */
        fw->errmsg = "invalid entry point in symbol table";
        return CRYPTO_INVALID_FIRMWARE;
    }

    return fw->priv->symtab[rc].st_value / 4;
}

static int tag_parse_integer(uint32_t val, unsigned long *out)
{
    if ((val >> 24) != 0xf8)
        return CRYPTO_INVALID_FIRMWARE;

    *out = val & 0x00ffffff;
    return 0;
}

/*
 * Extract the MD5 hash out of the firmware tag block.  The inbase pointer
 * specifies the first hash word in the tag (i.e., tag base + 2).  The tag
 * must have 6 words after base.  The result is stored in the buffer
 * indicated by md5, which must be 16 bytes long.
 *
 * The firmware has the MD5 encoded in a rather annoying format.
 * It's stored from least to most significant byte, with 3 bytes
 * per firmware word.
 */
static int tag_parse_md5(const uint32_t *inbase, unsigned char *md5)
{
    int i;

    for (i = 0; i < 6; i++) {
        uint32_t val = inbase[i];

        if ((val >> 24) != 0xf8)
            return CRYPTO_INVALID_FIRMWARE;

        md5[15 - 3 * i] = val & 0xff;
        if (i < 5) {
            md5[15 - 3 * i - 1] = (val >>  8) & 0xff;
            md5[15 - 3 * i - 2] = (val >> 16) & 0xff;
        }
    }

    return 0;
}

static int pka_fw_parse_tag(const uint32_t *tag_base, unsigned long tag_buflen,
        struct pka_fw_tag *tag_out)
{
    struct pka_fw_tag tag = {0};
    int rc;

    if (tag_buflen == 0)
        return CRYPTO_INVALID_FIRMWARE;

    rc = tag_parse_integer(tag_base[0], &tag.tag_length);
    if (rc < 0)
        return CRYPTO_INVALID_FIRMWARE;

    tag.origin = tag.tag_length & 0xffff00;
    tag.tag_length &= 0xff;

    if (tag.tag_length < 8 || tag_buflen < tag.tag_length)
        return CRYPTO_INVALID_FIRMWARE;

    rc = tag_parse_integer(tag_base[1], &tag.timestamp);
    if (rc < 0)
        return CRYPTO_INVALID_FIRMWARE;

    rc = tag_parse_md5(&tag_base[2], tag.md5);
    if (rc < 0)
        return CRYPTO_INVALID_FIRMWARE;

    /* MD5 coverage is only available in newer firmwares. */
    if (tag.tag_length > 8) {
        rc = tag_parse_integer(tag_base[8], &tag.md5_coverage);
        if (rc < 0)
            return CRYPTO_INVALID_FIRMWARE;
    }

    *tag_out = tag;
    return 0;
}

/*
 * Copy the symbol table from the input data into the private struct.
 */
static int prepare_symtab(struct pka_fw *fw, const unsigned char *base,
                unsigned long len)
{
    Elf32_Shdr shsym, shstr;
    unsigned long i;
    int rc;

    /* Copy symbol table into private structure */
    rc = pka_fw_read_shdr(fw, base, len, 3, &shsym);
    if (rc < 0)
        return rc;

    fw->errmsg = "invalid symbol table";
    if (shsym.sh_type != SHT_SYMTAB)
        return CRYPTO_INVALID_FIRMWARE;
    if (shsym.sh_addralign < 4)
        return CRYPTO_INVALID_FIRMWARE;
    if (shsym.sh_entsize != sizeof (Elf32_Sym))
        return CRYPTO_INVALID_FIRMWARE;

    fw->priv->nsyms = shsym.sh_size / shsym.sh_entsize;
    fw->priv->symtab = vmalloc(fw->priv->nsyms * sizeof *fw->priv->symtab);
    if (!fw->priv->symtab)
        return CRYPTO_NO_MEM;

    for (i = 0; i < fw->priv->nsyms; i++) {
        copy_sym(&fw->priv->symtab[i], base + shsym.sh_offset
                + i*shsym.sh_entsize);
    }

    /* Copy string table into private structure */
    rc = pka_fw_read_shdr(fw, base, len, shsym.sh_link, &shstr);
    if (rc < 0)
        return rc;

    fw->errmsg = "invalid string table";
    if (shstr.sh_type != SHT_STRTAB)
        return CRYPTO_INVALID_FIRMWARE;
    if ((base + shstr.sh_offset)[shstr.sh_size-1] != 0)
        return CRYPTO_INVALID_FIRMWARE;

    fw->priv->strtab = vmalloc(shstr.sh_size);
    if (!fw->priv->strtab)
        return CRYPTO_NO_MEM;

    fw->priv->nstr = shstr.sh_size;
    memcpy(fw->priv->strtab, base + shstr.sh_offset, shstr.sh_size);

    return 0;
}

int elppka_fw_parse(struct pka_fw *fw, const unsigned char *base,
            unsigned long len)
{
    Elf32_Shdr shdr;
    int rc;

    rc = pka_fw_init(fw, base, len);
    if (rc < 0)
        goto err;

    /* RAM data is in section 1. */
    rc = pka_fw_read_shdr(fw, base, len, 1, &shdr);
    if (rc < 0)
        goto err;

    if (shdr.sh_type == SHT_PROGBITS) {
        if (shdr.sh_addralign < 4 || shdr.sh_size % 4) {
            fw->errmsg = "RAM section is not 4-byte aligned";
            rc = CRYPTO_INVALID_FIRMWARE;
            goto err;
        }

        fw->ram_size = shdr.sh_size / 4;
        fw->priv->ram_base = vmalloc(fw->ram_size * sizeof *fw->priv->ram_base);
        if (!fw->priv->ram_base) {
            rc = CRYPTO_NO_MEM;
            goto err;
        }

        copy_words(fw->priv->ram_base, base + shdr.sh_offset, fw->ram_size);
        rc = pka_fw_parse_tag(fw->priv->ram_base, fw->ram_size, &fw->ram_tag);
        if (rc < 0) {
            fw->errmsg = "invalid RAM tag block";
            rc = CRYPTO_INVALID_FIRMWARE;
            goto err;
        }
    }

    rc = pka_fw_read_shdr(fw, base, len, 2, &shdr);
    if (rc < 0)
        goto err;

    if (shdr.sh_type == SHT_PROGBITS) {
        if (shdr.sh_addralign < 4 || shdr.sh_size % 4) {
            fw->errmsg = "ROM section is not 4-byte aligned";
            rc = CRYPTO_INVALID_FIRMWARE;
            goto err;
        }

        fw->rom_size = shdr.sh_size / 4;
        fw->priv->rom_base = vmalloc(fw->rom_size * sizeof *fw->priv->rom_base);
        if (!fw->priv->rom_base) {
            rc = CRYPTO_NO_MEM;
            goto err;
        }

        copy_words(fw->priv->rom_base, base + shdr.sh_offset, fw->rom_size);
        rc = pka_fw_parse_tag(fw->priv->rom_base, fw->rom_size, &fw->rom_tag);
        if (rc < 0) {
            fw->errmsg = "invalid ROM tag block";
            rc = CRYPTO_INVALID_FIRMWARE;
            goto err;
        }
    }

    if (!fw->ram_size && !fw->rom_size) {
        fw->errmsg = "no code found in firmware image";
        rc = CRYPTO_INVALID_FIRMWARE;
        goto err;
    }

    rc = prepare_symtab(fw, base, len);
    if (rc < 0)
        goto err;

    return 0;
err:
    elppka_fw_free(fw);
    return rc;
}

void elppka_fw_free(struct pka_fw *fw)
{
    if (fw->priv) {
        vfree(fw->priv->ram_base);
        vfree(fw->priv->rom_base);
        vfree(fw->priv->symtab);
        vfree(fw->priv->strtab);
    }

    vfree(fw->priv);
}

int elppka_fw_load(struct pka_state *pka_state, struct pka_fw *fw)
{
    uint32_t tmp, *rambase, *rombase;
    unsigned long i;

    rambase = pka_state->regbase + (pka_state->cfg.ram_offset >> 2);
    rombase = pka_state->regbase + (pka_state->cfg.rom_offset >> 2);

    if (fw->rom_size) {
        if (fw->rom_size > pka_state->cfg.fw_rom_size) {
            fw->errmsg = "cannot use ROM image as device has no ROM";
            return CRYPTO_INVALID_FIRMWARE;
        }

        /* Check hash in the device against the hash in the image. */
        for (i = 2; i <= 7; i++) {
            tmp = readl(&rombase[i]);

            if (fw->priv->rom_base[i] != tmp) {
                fw->errmsg = "ROM image does not match device";
                return CRYPTO_INVALID_FIRMWARE;
            }
        }
    }

    if (fw->ram_size) {
        if (fw->ram_size > pka_state->cfg.fw_ram_size) {
            fw->errmsg = "insufficient device memory to load firmware image";
            return CRYPTO_INVALID_FIRMWARE;
        }

        for (i = 0; i < fw->ram_size; i++) {
            writel(fw->priv->ram_base[i], &rambase[i]);
        }
    }

    return 0;
}

struct pka_fw_priv *pka_get_firmware(struct pka_fw_priv *fw_priv)
{
    if (fw_priv && !atomic_inc_not_zero(&fw_priv->refcount))
        return NULL;

    return fw_priv;
}

void pka_put_firmware(struct pka_fw_priv *fw_priv)
{                                                        
    if (!fw_priv)                                        
        return;                                          
                                                         
    if (atomic_dec_and_test(&fw_priv->refcount)) {       
        complete(&fw_priv->fw_released);                 
        elppka_fw_free(&fw_priv->data);                  
    }                                                    
                                                         
    WARN_ON(atomic_read(&fw_priv->refcount) < 0);        
}                                                        
