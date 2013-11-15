KERNEL_DIR ?= kernel
CROSS_COMPILE ?= arm-eabi-

# Comment this out if you build your kernel in-tree
#KERNEL_BUILD ?= out/KERNEL

TARGET = armperf.ko
obj-m = armperf.o

armperf-objs = armperf_entry.o v7_pmu.o

MAKE_ENV = ARCH=arm CROSS_COMPILE=$(CROSS_COMPILE)
ifneq ($(KERNEL_BUILD),)
	MAKE_ENV += O=$(KERNEL_BUILD)
endif

.PHONY: release clean

#default: release

release:
	make -C $(KERNEL_DIR) M=`pwd` $(MAKE_ENV) modules

modules_install:
	make -C $(KERNEL_DIR) SUBDIRS=`pwd` modules_install

android_install: release
	adb remount
	adb shell "mkdir /system/lib/modules"
	adb push $(TARGET) /system/lib/modules/

android_measure: android_install
	adb push android_measure.sh /data/local/
	adb shell "/data/local/android_measure.sh" | tee perf.log

clean:
	rm -f Module.symvers modules.order
	rm -f armperf.mod.* armperf.o
	rm -f $(armperf-objs)

distclean: clean
	rm -f $(TARGET)
