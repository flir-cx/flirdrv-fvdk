
# typically use the following to compile
# make ARCH=arm CROSS_COMPILE=/home/fredrik/mentor/arm-2011.03/bin/arm-none-linux-gnueabi
#
# Also modify 'KERNEL_SRC' to fit your system

ifeq ($(KERNEL_SRC),)
	KERNEL_SRC ?= ~/linux/flir-yocto/build_pico/tmp-eglibc/work/neco-oe-linux-gnueabi/linux-boundary/3.0.35-r0/git
endif


EXTRA_CFLAGS = -I$(ALPHAREL)/SDK/FLIR/Include -DFVD_DEPRECIATED_OK=0

	obj-m := fvdk.o
	fvdk-objs += fvdk_main.o
	fvdk-objs += load_fpga.o
	fvdk-objs += fvdk_mx51.o
	fvdk-objs += fvdk_mx6s.o
	PWD := $(shell pwd)

all: 
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean
