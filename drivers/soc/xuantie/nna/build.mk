##
 # Copyright (C) 2020 Alibaba Group Holding Limited
##
include Makefile

ifeq ($(BUILDROOT_DIR),)
  $(error BUILDROOT_DIR is empty)
endif

LINUX_DIR     ?= $(BUILDROOT_DIR)/output/build/linux-custom
TOOLCHAIN_DIR ?= $(BUILDROOT_DIR)/output/host/bin
CROSS_COMPILE ?= ${TOOLCHAIN_DIR}/bin/riscv64-unknown-linux-gnu-
export ARCH   ?= riscv

# if building the kernel module in-tree, these config options
# should be put into a Kconfig file.
# if building out-of-tree, defining them here is as good as any.
# CONFIG_VHA: build the VHA driver
export CONFIG_VHA := m

# hardware options:
# CONFIG_VHA_DUMMY:                 driver runs without hardware
# CONFIG_VHA_XUANTIE_TH1520_FPGA_C910: driver runs with XuanTie Th1520-FPGA hw platform using a device tree

# system configuration options
# CONFIG_VHA_SYS_MIRAGE:   driver runs with Mirage system configuration file. It is set by default when CONFIG_HW_AX2 is set.
# CONFIG_VHA_SYS_AURA:     driver runs with Aura system configuration file. It is set by default when CONFIG_HW_AX3 is set.
# CONFIG_VHA_SYS_VAGUS:    driver runs with Vagus system configuration file. Applicable when CONFIG_HW_AX3 is set.

subdir-ccflags-y += -Wall -g
EXTRA_CFLAGS :=

# fail if target not specified
ifeq ($(CONFIG_VHA_XUANTIE_TH1520_FPGA_C910),y)
  EXTRA_CFLAGS += -DCONFIG_VHA_XUANTIE_TH1520_FPGA_C910
  export CONFIG_NAME=CONFIG_VHA_XUANTIE_TH1520_FPGA_C910
  export CONFIG_HW_AX3 := y
else ifeq ($(CONFIG_VHA_XUANTIE_TH1520),y)
  EXTRA_CFLAGS += -DCONFIG_VHA_XUANTIE_TH1520
  export CONFIG_NAME=CONFIG_VHA_XUANTIE_TH1520
  export CONFIG_HW_AX3 := y
  export CONFIG_VHA_LO_PRI_SUBSEGS := y
else ifeq ($(CONFIG_VHA_DUMMY),y)
  export CONFIG_NAME=CONFIG_VHA_DUMMY
  export CONFIG_HW_AX3 := y
else
  $(error no VHA platform specified. Try CONFIG_VHA_DUMMY=y or CONFIG_VHA_XUANTIE_XXX=y etc)
endif

#------------------------------------
# for internal use
#------------------------------------

ifeq ($(KERNELRELEASE),)

KDIR ?= $(LINUX_DIR)

PWD := $(shell pwd)

# Hardware layout
ifeq ($(CONFIG_HW_AX2)$(CONFIG_HW_AX3),)
  $(info no HW layout specified. Defaulting to Mirage: CONFIG_HW_AX2=y. To build for Aura, please specify: CONFIG_HW_AX3=y)
  export CONFIG_HW_AX2 := y
endif

ifeq ($(CONFIG_HW_AX2), y)
  $(info Building Mirage target!)

  ifeq ($(CONFIG_VHA_SCF), y)
    $(error Safety cirtical features not supported by Mirage!)
  endif

  export CONFIG_VHA_MMU_MIRRORED_CTX ?= n

  ifeq ($(CONFIG_VHA_MMU_MIRRORED_CTX),)
    $(error Separate MMU contexts not supported by Mirage!)
  endif
endif

ifeq ($(CONFIG_HW_AX3), y)
  # Default OS target = 0
  export CONFIG_TARGET_OSID ?= 0
  $(info Building Aura/Vagus target for OS$(CONFIG_TARGET_OSID) !)

  export CONFIG_VHA_MMU_MIRRORED_CTX ?= y
  ifeq ($(CONFIG_VHA_MMU_MIRRORED_CTX),y)
    $(info Building Aura/Vagus target with mirrored mmu contexts!)
  else
    $(info Building Aura/Vagus target with separate mmu contexts!)
  endif

  # System config
  ifeq ($(CONFIG_VHA_SYS_AURA)$(CONFIG_VHA_SYS_VAGUS),)
    # $(info no system config specified. Defaulting to Aura: CONFIG_VHA_SYS_AURA=y. To build for Vagus, please specify: CONFIG_VHA_SYS_VAGUS=y)
    export CONFIG_VHA_SYS_AURA := y
  endif

  ifeq ($(CONFIG_VHA_SYS_VAGUS),)
    ifeq ($(CONFIG_VHA_SCF), y)
      # No safety critical features
      $(error Safety cirtical features not supported by Aura!)
    endif
  else
    # Safety critical features
    ifeq ($(CONFIG_VHA_SCF), y)
      $(info Building with Vagus safety cirtical features !)
    endif
  endif
endif

# kernel warning levels: as high as possible:
# W=12 and W=123 seem to warn about linux kernel headers, so use W=1.
KWARN := W=1

ifneq (,$(shell which sparse))
# C=1: use 'sparse' for extra warnings, if it is avail.
SPARSE := C=1
endif

all: info modules
	@echo Build \"$(CONFIG_NAME)\" finished

info:
	@echo
	@echo =====================================
	@echo ==== The build options are below ====
	@echo =====================================
	@echo CONFIG_NAME=$(CONFIG_NAME)
	@echo BUILDROOT_DIR=$(BUILDROOT_DIR)
	@echo LINUX_DIR=$(LINUX_DIR)
#	@echo PATH_TO_SYSROOT=$(PATH_TO_SYSROOT)
	@echo TOOLCHAIN_DIR=$(TOOLCHAIN_DIR)
	@echo CROSS_COMPILE=$(CROSS_COMPILE)
	@echo KDIR=$(KDIR)
	@echo ARCH=$(ARCH)
	@echo subdir-ccflags-y=$(subdir-ccflags-y)
	@echo EXTRA_CFLAGS=$(EXTRA_CFLAGS)
	@echo
	@echo CONFIG_VHA=$(CONFIG_VHA)
	@echo CONFIG_HW_AX3=$(CONFIG_HW_AX3)
	@echo CONFIG_TARGET_OSID=$(CONFIG_TARGET_OSID)
	@echo CONFIG_VHA_MMU_MIRRORED_CTX=$(CONFIG_VHA_MMU_MIRRORED_CTX)
	@echo CONFIG_VHA_SYS_MIRAGE=$(CONFIG_VHA_SYS_MIRAGE)
	@echo CONFIG_VHA_SYS_AURA=$(CONFIG_VHA_SYS_AURA)
	@echo CONFIG_VHA_SYS_VAGUS=$(CONFIG_VHA_SYS_VAGUS)
	@echo CONFIG_VHA_SCF=$(CONFIG_VHA_SCF)
	@echo CONFIG_VHA_NEXEF=$(CONFIG_VHA_NEXEF)
	@echo CONFIG_LOKI=$(CONFIG_LOKI)
	@echo =====================================
	@echo

modules: info
	$(MAKE) -C $(KDIR) M=$(PWD) $(SPARSE) $(KWARN) EXTRA_CFLAGS="$(EXTRA_CFLAGS)" CROSS_COMPILE=$(CROSS_COMPILE)
clean:
	if [ -d "$(KDIR)" ]; then \
	    $(MAKE) -C $(KDIR) M=$(PWD) clean; \
	fi
check:
	cd $(KDIR); scripts/checkpatch.pl -f `find $(PWD)/vha $(PWD)/include $(PWD)/img_mem -name "*\.[ch]"`

endif # KERNELRELEASE
