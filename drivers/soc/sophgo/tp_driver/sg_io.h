/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SG_IO_H_
#define _SG_IO_H_
#include "sg_common.h"

/* General purpose register bit definition */
#define GP_REG_FW_STATUS	9
#define GP_REG_FW_MODE		13

#ifdef PLAT_BM1686
#define TOP_GP_OFFSET		0x80
#define TOP_SW_RESET		0xc00

#define TOP_BASE_ADDR           0x50010000
#define GP_BASE_ADDR            0x50010080
#define GDMA_BASE_ADDR          0x58000000
#define BDC_BASE_ADDR           0x58001000
#define SMMU_BASE_ADDR          0x58002000
#define CDMA_BASE_ADDR          0x58003000

#define TOP_GP_REG14_A53LITE_IRQ_STATUS_OFFSET   0x0b8
#define TOP_GP_REG14_A53LITE_IRQ_SET_OFFSET      0x190
#define TOP_GP_REG14_A53LITE_IRQ_CLEAR_OFFSET    0x194

#else
#define TOP_GP_OFFSET		0x1c0
#define TPU_SYS_OFFSET		0x10000000
/* TPU sys reg offset */
#define TPU_SYS_REG_CLK_EN	0x0
#define TPU_SYS_REG_RST		0x4
#define TPU_SYS_REG_RVBA_L32	0x16c
#define TPU_SYS_REG_RVBA_H32	0x170
#define TPU_SYS_REG_DUMMY	0x304
#endif

struct sg_vaddr {
	void __iomem *top_base_vaddr;
	void __iomem *gp_base_vaddr;
#ifdef PLAT_BM1686
	void __iomem *gdma_base_vaddr;
	void __iomem *bdc_base_vaddr;
	void __iomem *smmu_base_vaddr;
	void __iomem *cdma_base_vaddr;
#else
	void __iomem *tpu_sys_base_vaddr;
#endif
};

struct platform_device;
struct sg_dev;

int sgdrv_io_init(struct platform_device *pdev);

void top_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val);
u32 top_reg_read(struct sg_dev *sgdev, u32 reg_offset);
void gp_reg_write(struct sg_dev *sgdev, u32 idx, u32 data);
u32 gp_reg_read(struct sg_dev *sgdev, u32 idx);
#ifdef PLAT_BM1686
void bdc_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val);
u32 bdc_reg_read(struct sg_dev *sgdev, u32 reg_offset);
void smmu_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val);
u32 smmu_reg_read(struct sg_dev *sgdev, u32 reg_offset);
void cdma_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val);
u32 cdma_reg_read(struct sg_dev *sgdev, u32 reg_offset);
#else
void tpu_sys_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val, u32 core_id);
void tpu_sys_reg_setbits(struct sg_dev *sgdev, u32 reg_offset, u32 val, u32 core_id);
u32 tpu_sys_reg_read(struct sg_dev *sgdev, u32 reg_offset, u32 core_id);
void tpu_sys_reg_clrbits(struct sg_dev *sgdev, u32 reg_offset, u32 val, u32 core_id);
#endif

#endif
