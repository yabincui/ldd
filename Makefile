KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

obj-m := hello.o scull_pipe.o scull_delay.o scull_timer2.o

scull_timer2-objs := scull_timer.o

RM = rm -rf
clean:
	$(RM) *.o *.ko *.mod.c modules.order Module.symvers

distclean: clean
	$(RM) .*.cmd .tmp_versions/ *.mod.c
