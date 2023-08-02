################################################################################
#
# WangXun 10GbE PCI Express Linux Network Driver
# Copyright(c) 2015 - 2017 Beijing WangXun Technology Co., Ltd.
#
# This program is free software; you can redistribute it and/or modify it
# under the terms and conditions of the GNU General Public License,
# version 2, as published by the Free Software Foundation.
#
# This program is distributed in the hope it will be useful, but WITHOUT
# ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
# FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
# more details.
#
# The full GNU General Public License is included in this distribution in
# the file called "COPYING".
#
################################################################################

obj-$(CONFIG_TXGBE) += txgbe.o

txgbe-y := txgbe_main.o txgbe_dcb.o txgbe_ethtool.o txgbe_lib.o \
	   txgbe_mbx.o txgbe_sriov.o txgbe_param.o txgbe_phy.o \
	   txgbe_procfs.o txgbe_hw.o txgbe_mtd.o txgbe_pcierr.o \
	   txgbe_bp.o kcompat.o


txgbe-${CONFIG_DCB} += txgbe_dcb_nl.o

txgbe-${CONFIG_DEBUG_FS} += txgbe_debugfs.o

txgbe-${CONFIG_FCOE:m=y} += txgbe_fcoe.o

txgbe-$(CONFIG_PTP_1588_CLOCK:m=y) += txgbe_ptp.o

txgbe-${CONFIG_SYSFS} += txgbe_sysfs.o

