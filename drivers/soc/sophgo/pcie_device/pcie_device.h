/* SPDX-License-Identifier: GPL-2.0 */

#ifndef __PCIE_DEVICE_H__
#define __PCIE_DEVICE_H__

#define C2C_PCIE_DBI2_OFFSET                    0x100000
#define C2C_PCIE_ATU_OFFSET                     0x300000

#define REG_OFFSET_PCIE_iATU	0x300000
#define BAR1_PART0_OFFSET	0x0
#define BAR1_PART1_OFFSET	0x200000
#define BAR1_PART2_OFFSET	0x280000
#define BAR1_PART3_OFFSET	0x288000
#define BAR1_PART4_OFFSET	0x300000
#define BAR1_PART5_OFFSET	0x392000
#define BAR1_PART6_OFFSET	0x393000

#define C2C_CDMA0_OFFSET	0x10000
#define C2C_CDMA1_OFFSET	0x20000
#define C2C_CDMA2_OFFSET	0x30000
#define C2C_CDMA3_OFFSET	0x40000
#define C2C_TOP_REG_OFFSET	0x50000

#define REG_WRITE8(base, off, val)	iowrite8((val), (u8 *)(base + off))
#define REG_WRITE16(base, off, val) iowrite16((val), (u16 *)(base + off))
#define REG_WRITE32(base, off, val) \
{\
	iowrite32((val), (u32 *)(base + off)); \
	ioread32((u32 *)(base + off)); \
}
#define REG_READ8(base, off)	ioread8((u8 *)(base + off))
#define REG_READ16(base, off)	ioread16((u16 *)(base + off))
#define REG_READ32(base, off)	ioread32((u32 *)(base + off))


#define PCIE_INFO_DEF_VAL	0x5a

#define PCIE_DISABLE	0
#define PCIE_ENABLE	1

#define PCIE_DATA_LINK_PCIE	0
#define PCIE_DATA_LINK_C2C	1

#define PCIE_LINK_ROLE_RC	0
#define PCIE_LINK_ROLE_EP	1
#define PCIE_LINK_ROLE_RCEP	2

#define RC_GPIO_LEVEL	1
#define EP_GPIO_LEVEL	0

#define PCIE_PHY_X8	0
#define PCIE_PHY_X44	1

#define CHIP_ID_MAX	5

struct sys_if {
	uint64_t slot_id;
	uint64_t opensbi_addr;
	uint64_t kernel_addr;
	uint64_t ramdisk_addr;
	uint64_t dtb_addr;
	char ip[16];
};

enum pcie_id {
	PCIE0 = 0,
	PCIE1,
	PCIE2,
	PCIE3,
	PCIE4,
	PCIE5,
	PCIE6,
	PCIE7,
	PCIE8,
	PCIE9,
	PCIE_MAX,
};

struct pcie_info {
	uint64_t slot_id;
	uint64_t socket_id;
	uint64_t pcie_id;
	uint64_t send_port;
	uint64_t recv_port;
	uint64_t enable;
	uint64_t data_link_type;
	uint64_t link_role;
	uint64_t link_role_gpio;
	uint64_t perst_gpio;
	uint64_t phy_role;
	uint64_t peer_slotid;
	uint64_t peer_socketid;
	uint64_t peer_pcie_id;
};

#define BM1690_SRAM_BASE	(0X7010000000)
#define CONFIG_FILE_ADDR	(0x70101ff000)
#define SYS_IF_BASE	(0x70101fe000)
#define SYS_IF_SIZE	(0x1000)
#define PER_CONFIG_STR_OFFSET	(0x1000)
#define CONFIG_STRUCT_BASE	(0x70101fe000 - PCIE_MAX * PER_CONFIG_STR_OFFSET)
#define CONFIG_STRUCT_OFFSET	(CONFIG_STRUCT_BASE - BM1690_SRAM_BASE)

#define PCIE_INFO_BASE	CONFIG_STRUCT_BASE
#define PER_INFO_SIZE	PER_CONFIG_STR_OFFSET
#define PCIE_INFO_SIZE	(10 * 1024 * 1024)

#endif
