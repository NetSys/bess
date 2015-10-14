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

obj-m := softnic.o 
#softnic_ivs_test.o

softnic-objs := sndrv.o sn_host.o sn_guest.o sn_netdev.o sn_ethtool.o sn_ivshmem.o

#softnic_ivs_test-objs := sn_ivshmem.o sn_ivs_test.o

endif
