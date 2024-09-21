obj-m += epaper.o

KERNEL_SRC := ~/buildroot-2024.02.3/output/build/linux-custom/

# If '~' is used instead of $(HOME), something will be wrong.
CROSS_COMPILE := $(HOME)/buildroot-2024.02.3/output/host/bin/arm-buildroot-linux-gnueabihf-

CFLAGS += "-save-temps=obj"

all:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) CFLAGS=$(CFLAGS) ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE) modules

clean:
	$(MAKE) -C $(KERNEL_SRC) M=$(PWD) clean

