KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

obj-m := start_user_application.o

RM = rm -rf
clean:
	$(RM) *.o *.ko *.mod.c modules.order Module.symvers

distclean: clean
	$(RM) .*.cmd .tmp_versions/
