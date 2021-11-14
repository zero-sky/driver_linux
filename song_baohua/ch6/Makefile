ifneq ($(KERNELRELEASE), )
	obj-m := globalmemMutil.o
	obj-m += globalmem.o
else
#链接指向/usr/src下的header文件夹
	KERNELDIR ?= /lib/modules/$(shell uname -r)/build
	PWD := $(shell pwd)
default:
#make -C /usr/src/linux-source M = ~/Documents/LDD/p22 modules
	make -C $(KERNELDIR) M=$(PWD) modules

clean:
	rm -f *.o *.mod.c .*.cmd *.mod *.symvers *.order *.ko
endif
