#!/system/bin/sh
#
# This script will continuously poll the proc file entry
# for new data. There should not be any duplicated measurements
# because the kernel module atomically zeros the read-out
# buffer on each read.
#
# This isn't the most efficient, but it gets the job done :-)

echo "Removing armperf kernel module..."
rmmod armperf.ko 2>/dev/null
sleep 2
echo "Loading armperf..."
insmod /system/lib/modules/armperf.ko
sleep 1
echo "Measuring!"
while (()); do cat /proc/armperf; done
