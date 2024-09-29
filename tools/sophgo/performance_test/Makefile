LINUX_SRC ?= $(RV_KERNEL_BUILD_DIR)
CROSS_COMPILE := $(CROSS_COMPILE)

CC := $(CROSS_COMPILE)gcc
LD := $(CROSS_COMPILE)ld
ARCH := riscv

SG_MODULE_NAME = sg_performance_test
DEBUG = 0

# Compiler flags
ccflags-y = -Wall
ccflags-y += -DDEBUG

DRVSRC = sg_performance.c

$(SG_MODULE_NAME)-objs = $(patsubst %.c,%.o,$(DRVSRC))

obj-m = $(SG_MODULE_NAME).o

# Targets
all: clean
ifndef CROSS_COMPILE
	make -C $(LINUX_SRC) M=$(PWD) modules
else
	make ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) -C $(LINUX_SRC) M=$(PWD) modules
endif

clean:
	make -C $(LINUX_SRC) M=$(PWD) clean
