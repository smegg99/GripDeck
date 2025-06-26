MODULE_NAME := gripdeck_battery
obj-m       := $(MODULE_NAME).o

PWD         := $(shell pwd)
KDIR        := /lib/modules/$(shell uname -r)/build

all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean