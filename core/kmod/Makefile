MODNAME := bess

ifeq ($(KERNELRELEASE),)

KERNELDIR ?= /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

.PHONY: build clean

build:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -rf *.o *~ core .depend .*.cmd *.ko *.mod.c *.symvers .tmp_versions
else

$(info Building with KERNELRELEASE = ${KERNELRELEASE})

obj-m := $(MODNAME).o

$(MODNAME)-objs := sndrv.o sn_host.o sn_netdev.o sn_ethtool.o
ccflags-y := -g

endif
