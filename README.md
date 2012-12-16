armperf
=========

Performance Monitoring using ARM PMU extensions - ARM Cycles, Cache misses, and more ...

Usage
=========
--> insmod armperf.ko evdelay=500 evlist=1,68,3,4 evdebug=1

--> rmmod armperf.ko

NOTE: on many version of Android the insmod utility will _not_ pass module parameters!

Parameters:

evdelay = Delay between successive reading of samples from event counters (milliSec)

evlist = Array of decimal values of event IDs to be monitored (refer ARM TRM). If not specified, first four of below are used:

	1 ==> Instruction fetch that causes a refill at the lowest level of instruction or unified cache

	8 ==> Instructions architecturally executed (retired)

	3 ==> Data read or write operation that causes a refill at the lowest level of data or unified cache

	4 ==> Data read or write operation that causes a cache access at the lowest level of data or unified cache

	67 ==> All cache accesses

	68 ==> Any cacheable miss in the L2 cache

	0x56 ==> Increment for every cycle that no instructions are available for issue

NOTE: not all events are available on all processors!

(for other valid values, refer to Cortex-A TRM)

evdebug = 0 (default - no messages from kernel module) / 1 (event counters are printed out - warning: will flood the console)

Output
=======
An example console output when evdebug = 1, is below (CPU MHz = 720 MHz, sampling every 1000 millisec):
(EMIF outputs are not shown - truncated)

[  100.066070] 49031    2388256 173679  20677707        0       11362862       
[  101.096008] 50458    2322475 169335  20240942        0       11362603       
[  102.126007] 75056    2384952 184637  20614303        0       11362506       
[  103.156036] 49902    2322703 169203  20224376        0       11362827       
[  104.186035] 48974    2320928 168278  20198781        0       11362768       

NOTE: (this formatting is old, the new output is: cycle-count, overflow, counter-0, counter-1, ...)


The Counter values, Overflow indication, Cycle count, EMIF read and write count is provided.

NOTE: PMU Cycle count is configured to be divided by 64, compared to CPU clock

Usage of proc entry
===================
armperf exposes event information (same information as above) via proc entry, and can be used by userland applications

cat /proc/armperf

root@android# cat /proc/armperf
PMU.counter[0]= 54363
PMU.counter[1]= 2339356
PMU.counter[2]= 172534
PMU.counter[3]= 20313519
PMU.overflow= 0
PMU.CCNT= 11362787

Validation
=========
Tested on kernel 3.2, with gcc4.5 toolchain. (by prabindh)

Tested ond kernels 2.6.29 - 3.2 using gcc toolchains provided by Google in the "prebuilt" Android repo.

Validated on AM335x (OMAP3/Beagle variants will work) - for Cortex-A8. Cortex-A9 has larger counter list, and will also work

Also validated on Nexus One and Nexus S.

Limitations
===========
PMU interrupts not enabled and not supported, so watchout for overflow flags manually

===
jeremya@cs.columbia.edu
