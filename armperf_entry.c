/*
 * armperf_entry.c
 *
 * Slightly modified version of peemuperf_entry.c by:
 * Prabindh Sundareson (prabu@ti.com) 2012
 * Originally from here: https://github.com/prabindh/peemuperf
 *
 * Additional changes by:
 * Jeremy C. Andrus <jeremy@jeremya.com>
 *
 */
#include <linux/version.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/kthread.h>
#include <linux/delay.h>
#include <linux/slab.h>

#include "v7_pmu.h"

#define MODULE_NAME "armperf"

static void pmu_start(unsigned int event_array[],unsigned int count);
static void pmu_stop(void);
static int __pmu_init(void);
static void __exit armperf_exit(void);

#if defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#define MAX_PROC_BUF_LEN 1000
static DEFINE_SPINLOCK(pblock);
static char proc_buf[MAX_PROC_BUF_LEN];
#endif

static int evlist[6] = {0x1, 0x8, 0x3, 0x4, 0, 0};
static int evlist_count = 4;
static int available_evcount = 0;
static int evdelay = 100; /* milliseconds */
static int evdebug = 0;

static void pmu_start(unsigned int event_array[],unsigned int count)
{
	int i;

	enable_pmu();			/* Enable the PMU */
	reset_ccnt();			/* Reset the CCNT (cycle counter) */
	reset_pmn();			/* Reset the configurable counters */
	write_flags((1 << 31) | 0xf);	/*Reset overflow flags */

	/* configure counters with events */
	for (i = 0; i < count; i++)
		pmn_config(i, event_array[i]);

	ccnt_divider(1); /* Enable divide by 64 */
	enable_ccnt();   /* Enable CCNT */

	/* enable all the counters */
	for (i = 0; i < count; i++)
		enable_pmn(i);
}

static void pmu_stop(void)
{
	int i;
	unsigned int cycle_count, overflow;
	unsigned int counters = available_evcount;
#if defined(CONFIG_PROC_FS)
	int currProcBufLen = 0;
#endif

	/*
	 * disable sampling / counting
	 */

	disable_ccnt();
	for (i = 0;i < counters;i ++)
		disable_pmn(i);

	cycle_count = read_ccnt(); /*  Read CCNT */
	overflow = read_flags();   /* Check for overflow flag */

#if defined(CONFIG_PROC_FS)
	spin_lock(&pblock);

	for(i = 0;i < counters;i ++) {
		currProcBufLen += sprintf(proc_buf + currProcBufLen,
					  "PMU.counter[%d]= %u\n", i, read_pmn(i));
	}
	currProcBufLen += sprintf(proc_buf + currProcBufLen,
				  "PMU.overflow= %u\nPMU.CCNT= %u\n",
				  overflow,cycle_count);
	spin_unlock(&pblock);
#endif

	if (evdebug) {
		for (i = 0;i < counters;i ++)
			printk("%u\t", read_pmn(i));

		printk("%u\t%u\t", overflow, cycle_count);
	}
}


/*
 * Monitoring Thread
 *
 */
static int armperf_thread(void* data)
{
	if (evdebug == 1)
		printk("Entering thread loop...\n");
	while (1) {
		if (kthread_should_stop())
			break;

		/* sample at regular-ish intervals */
		pmu_start(evlist, available_evcount);
		msleep(evdelay);
		pmu_stop();
	}
	if (evdebug == 1)
		printk("Exiting thread...\n");
	return 0;
}

#if defined(CONFIG_PROC_FS)
/*
 * procfs entries
 *
 */
static int armperf_proc_read(char *buf, char **start, off_t offset,
			       int len, int *unused_i, void *unused_v)
{
	int blen, outlen;

	spin_lock(&pblock);

	blen = strlen(proc_buf);
	outlen = min(len, blen);
	memcpy(buf + offset, proc_buf, outlen);
	buf[len-1] = 0;

	/* reset the buffer to 0 to avoid double-sampling */
	memset(proc_buf, 0, MAX_PROC_BUF_LEN);

	spin_unlock(&pblock);

	return outlen;
}

static __init int register_proc_entry(const char *entry_name)
{
	struct proc_dir_entry *proc_armperf;

	proc_armperf = create_proc_entry(entry_name, S_IRUGO, NULL);
	if (proc_armperf)
		proc_armperf->read_proc = armperf_proc_read;

	memset(proc_buf, 0, MAX_PROC_BUF_LEN);
	return proc_armperf != NULL;
}
#endif /* CONFIG_PROC_FS */

static struct task_struct *armperf_kthread;

static int __pmu_init()
{
	available_evcount = getPMN();
	printk(MODULE_NAME ": evdelay=%d, available_events: %d "
	       "Event inputs: %d %d %d %d\n",
	       evdelay, available_evcount,
	       evlist[0], evlist[1], evlist[2], evlist[3]);

	armperf_kthread = kthread_run(armperf_thread, (void *)0,
				      "armperf_sampler");
	
	return 0;
}

/*
 * Module entry points
 *
 */
static int __init armperf_init(void)
{
	unsigned int retVal = 0;

	retVal = __pmu_init();
	if (retVal) {
		printk(MODULE_NAME ": PMU initialization failed\n");
		return -ENODEV;
	}
#if defined(CONFIG_PROC_FS)
	register_proc_entry(MODULE_NAME);
#endif
	return 0;
}
late_initcall(armperf_init);

static void __exit armperf_exit()
{
	kthread_stop(armperf_kthread);
#if defined(CONFIG_PROC_FS)
	remove_proc_entry(MODULE_NAME, NULL);
#endif
}
module_exit(armperf_exit);


/*
 * Configuration / Parameters
 *
 */
module_param(evdelay, int, 100);
module_param(evdebug, int, 0);
module_param_array(evlist, int, &evlist_count, 0000);

MODULE_DESCRIPTION(MODULE_NAME ": PMU driver (e.g. insmod armperf.ko "
		   "evdelay=500 evlist=1,68,3,4 evdebug=0");
MODULE_AUTHOR("Prabindh Sundareson <prabu@ti.com>");
MODULE_AUTHOR("Jeremy C. Andrus <jeremy@jeremya.com>");
MODULE_LICENSE("GPL v2");

