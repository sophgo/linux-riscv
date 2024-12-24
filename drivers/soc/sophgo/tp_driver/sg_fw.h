/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _SG_FW_H_
#define _SG_FW_H_

#define TPU_SCALAR_RVBA_ADDR	0xa0000000ULL
#define TPU_FW_OFFSET			0x20000000ULL
#define LAST_INI_REG_VAL		0x76125438

struct sg_dev;

enum fw_downlod_stage {
	FW_START = 0,
	DDR_INIT_DONE = 1,
	DL_DDR_IMG_DONE	= 2
};

enum a53lite_fw_mode {
	FW_PCIE_MODE,
	FW_SOC_MODE,
	FW_MIX_MODE
};

int sgdrv_fw_load(struct sg_dev *sgdev);
void sgdrv_fw_unload(struct sg_dev *sgdev);
void sgdrv_set_fw_mode(struct sg_dev *sgdev, struct platform_device *pdev);

#endif
