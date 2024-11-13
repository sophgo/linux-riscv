ifeq ($(CONFIG_VHA_NEXEF), y)
  obj-$(CONFIG_VHA) += nexef_platform/
endif
obj-$(CONFIG_VHA) += vha/ img_mem/
obj-$(CONFIG_LOKI) += fenrir_loki/

ccflags-y += -I$(srctree)/$(src)/include
