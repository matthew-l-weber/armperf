
KERNEL_SRC ?= kernel
CROSS_COMPILE ?= arm-eabi-

# Comment this out if you build your kernel in-tree
KERNEL_BUILD ?= out/KERNEL

TARGET = armperf.ko
obj-m = armperf.o

armperf-objs = armperf_entry.o v7_pmu.o

MAKE_ENV = ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE)
ifneq ($(KERNEL_BUILD),)
	MAKE_ENV += O=$(KERNEL_BUILD)
endif

.PHONY: release clean

default: release

release:
	make -C $(KERNEL_SRC) M=`pwd` $(MAKE_ENV) modules

android_install: release
	adb remount
	adb push $(TARGET) /system/lib/modules

clean:
	rm -f Module.symvers modules.order
	rm -f armperf.mod.* armperf.o
	rm -f $(armperf-objs)

distclean: clean
	rm -f $(TARGET)
