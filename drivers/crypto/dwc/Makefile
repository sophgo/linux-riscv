obj-$(CONFIG_CRYPTO_DEV_DWC) := dwc-crypto.o

ccflags-y += -I$(src)

ifneq ($(CONFIG_CRYPTO_DEV_DWC_PKA), ) 
dwc-crypto-objs += dwc-elperror.o dwc-elppka.o dwc-firmware.o dwc-pkacommon.o dwc-rsa.o dwc-ecc.o dwc-ecdh.o dwc-ecdsa.o dwc-pka.o
endif

