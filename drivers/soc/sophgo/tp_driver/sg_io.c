// SPDX-License-Identifier: GPL-2.0

#include <linux/platform_device.h>
#include <linux/of_address.h>

#include "sg_common.h"
#include "sg_io.h"

int sgdrv_io_init(struct platform_device *pdev)
{
	struct resource *res;
	struct sg_dev *sgdev = platform_get_drvdata(pdev);
	int i = 0;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (res == NULL)
		return -EINVAL;

	sgdev->vaddr.top_base_vaddr = of_iomap(pdev->dev.of_node, 0);
	sgdev->vaddr.gp_base_vaddr = sgdev->vaddr.top_base_vaddr + TOP_GP_OFFSET;
#ifdef PLAT_BM1686
	sgdev->vaddr.gdma_base_vaddr = of_iomap(pdev->dev.of_node, 1);
	sgdev->vaddr.bdc_base_vaddr = sgdev->vaddr.gdma_base_vaddr + 0x1000;
	sgdev->vaddr.smmu_base_vaddr = sgdev->vaddr.gdma_base_vaddr + 0x2000;
	sgdev->vaddr.cdma_base_vaddr = sgdev->vaddr.gdma_base_vaddr + 0x3000;
#else
	sgdev->vaddr.tpu_sys_base_vaddr = of_iomap(pdev->dev.of_node, 1);
	for (i = 0; i < CORE_NUM; i++) {
		res = platform_get_resource(pdev, IORESOURCE_MEM, i + 1);
		if (res == NULL) {
			sgdev->tpu_num = i;
			break;
		}
		sgdev->core_info[i]->tpu_sys_base_vaddr = of_iomap(pdev->dev.of_node, i + 1);
		sgdev->tpu_num = i + 1;
	}
	if (!sgdev->tpu_num)
		return -EINVAL;
#endif
	return 0;
}

void top_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val)
{
	iowrite32(val, sgdev->vaddr.top_base_vaddr + reg_offset);
}

u32 top_reg_read(struct sg_dev *sgdev, u32 reg_offset)
{
	return ioread32(sgdev->vaddr.top_base_vaddr + reg_offset);
}

void gp_reg_write(struct sg_dev *sgdev, u32 idx, u32 data)
{
	iowrite32(data, sgdev->vaddr.gp_base_vaddr + idx * 4);
}

u32 gp_reg_read(struct sg_dev *sgdev, u32 idx)
{
	return ioread32(sgdev->vaddr.gp_base_vaddr + idx * 4);
}

#ifdef PLAT_BM1686
void bdc_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val)
{
	iowrite32(val, sgdev->vaddr.bdc_base_vaddr + reg_offset);
}

u32 bdc_reg_read(struct sg_dev *sgdev, u32 reg_offset)
{
	return ioread32(sgdev->vaddr.bdc_base_vaddr + reg_offset);
}

void smmu_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val)
{
	iowrite32(val, sgdev->vaddr.smmu_base_vaddr + reg_offset);
}

u32 smmu_reg_read(struct sg_dev *sgdev, u32 reg_offset)
{
	return ioread32(sgdev->vaddr.smmu_base_vaddr + reg_offset);
}

void cdma_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val)
{
	iowrite32(val, sgdev->vaddr.cdma_base_vaddr + reg_offset);
}

u32 cdma_reg_read(struct sg_dev *sgdev, u32 reg_offset)
{
	return ioread32(sgdev->vaddr.cdma_base_vaddr + reg_offset);
}
#else
void tpu_sys_reg_write(struct sg_dev *sgdev, u32 reg_offset, u32 val, u32 core_id)
{
	iowrite32(val, sgdev->core_info[core_id]->tpu_sys_base_vaddr + reg_offset);
}

void tpu_sys_reg_setbits(struct sg_dev *sgdev, u32 reg_offset, u32 val, u32 core_id)
{
	u32 reg = tpu_sys_reg_read(sgdev, reg_offset, core_id);

	reg = reg | val;
	iowrite32(reg, sgdev->core_info[core_id]->tpu_sys_base_vaddr + reg_offset);
}

void tpu_sys_reg_clrbits(struct sg_dev *sgdev, u32 reg_offset, u32 val, u32 core_id)
{
	u32 reg = tpu_sys_reg_read(sgdev, reg_offset, core_id);

	reg = reg & (~val);
	iowrite32(reg, sgdev->core_info[core_id]->tpu_sys_base_vaddr + reg_offset);
}


u32 tpu_sys_reg_read(struct sg_dev *sgdev, u32 reg_offset, u32 core_id)
{
	return ioread32(sgdev->core_info[core_id]->tpu_sys_base_vaddr + reg_offset);
}
#endif
