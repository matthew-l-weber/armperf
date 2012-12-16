
KERNEL_SRC ?= /Volumes/WorkSpace/n1/codeaurora_msm/
KERNEL_BUILD ?= /Volumes/WorkSpace/n1/out/KERNEL
TOOLCHAIN_PATH ?= /Volumes/WorkSpace/n1/prebuilt/darwin-x86/toolchain/arm-eabi-4.4.3/bin/arm-eabi-

TARGET = armperf.ko
obj-m = armperf.o

armperf-objs = armperf_entry.o v7_pmu.o

MAKE_ENV = ARCH=arm CROSS_COMPILE=$(TOOLCHAIN_PATH)
ifneq ($(KERNEL_BUILD),)
	MAKE_ENV += O=$(KERNEL_BUILD)
endif

.PHONY: release clean

default: release

release:
	make -C $(KERNEL_SRC) M=`pwd` $(MAKE_ENV) modules

clean:
	rm -f Module.symvers modules.order
	rm -f armperf.mod.* armperf.o
	rm -f $(armperf-objs)

distclean: clean
	rm -f $(TARGET)
