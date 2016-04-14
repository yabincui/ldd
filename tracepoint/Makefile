INCLUDES = -I.
KERNELDIR := /lib/modules/$(shell uname -r)/build
PWD := $(shell pwd)

CFLAGS_tracepoint.o = -I$(src)

default:
	$(MAKE) -C $(KERNELDIR) M=$(PWD) modules

obj-m := tracepoint.o

RM = rm -rf
clean:
	$(RM) *.o *.ko *.mod.c modules.order Module.symvers

distclean: clean
	$(RM) .*.cmd .tmp_versions/
