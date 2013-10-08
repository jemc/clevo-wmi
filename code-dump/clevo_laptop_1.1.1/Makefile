
EXTRA_CFLAGS += -Wall -O2
CFILES = clevo_laptop.c

obj-m += clevo_laptop.o


all:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

install:
	install -m 644 *.ko /lib/modules/$(shell uname -r)/kernel/drivers/misc/
	/sbin/depmod -ae

clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	rm -f $(PWD)/modules.order $(PWD)/Module.markers

