KERNEL_DIR:=/lib/modules/`uname -r`/build

ifndef KERNELRELEASE
PWD := $(shell pwd)
all:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules
clean:
	rm -f *.o *.ko *.ko.unsigned *.mod.* .*.cmd Module.symvers
	rm -rf .tmp_versions Module.markers modules.order

install:
	$(MAKE) -C $(KERNEL_DIR) SUBDIRS=$(PWD) modules_install
else
     obj-m := kinterval.o kinterval-example.o
endif
