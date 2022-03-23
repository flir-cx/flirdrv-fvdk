
# typically use the following to compile
# make ARCH=arm CROSS_COMPILE=/home/fredrik/mentor/arm-2011.03/bin/arm-none-linux-gnueabi
#
# Also modify 'KERNEL_SRC' to fit your system

ifeq ($(KERNEL_SRC),)
	KERNEL_SRC ?= ~/linux/flir-yocto/build_pico/tmp-eglibc/work/neco-oe-linux-gnueabi/linux-boundary/3.0.35-r0/git
endif

ifneq ($(KERNEL_PATH),)
       KERNEL_SRC = $(KERNEL_PATH)
endif

ifeq ($(INCLUDE_SRC),)
	INCLUDE_SRC ?=$(ALPHAREL)/SDK/FLIR/Include
endif

EXTRA_CFLAGS = -I$(INCLUDE_SRC) -DFVD_DEPRECIATED_OK=0 -Werror

	obj-m := fvdk.o
	fvdk-objs += fvdk_main.o
	fvdk-objs += load_fpga.o
	fvdk-objs += fvdk_mx6s.o
	fvdk-objs += roco_header.o
	fvdk-objs += fvdk_mx6q.o
	fvdk-objs += fvdk_mx6s_ec101.o
	fvdk-objs += fvdk_mx6s_ec501.o
	fvdk-objs += fvdk_flir_eoco.o
	PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	rm -f *.o *~ core .depend .*.cmd *.ko *.mod.c *.mod
	rm -f Module.markers Module.symvers modules.order
	rm -rf .tmp_versions Modules.symvers
