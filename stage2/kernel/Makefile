obj-m += hp_victus_rgb.o

KDIR ?= /usr/lib/modules/$(shell uname -r)/build
PWD  := $(shell pwd)

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean

install:
	$(MAKE) -C $(KDIR) M=$(PWD) modules_install
	depmod -a

# Manual load for development. For daily use install via DKMS / the PKGBUILD.
load:
	sudo modprobe -r hp_victus_rgb 2>/dev/null || true
	sudo insmod ./hp_victus_rgb.ko

unload:
	sudo rmmod hp_victus_rgb || true
