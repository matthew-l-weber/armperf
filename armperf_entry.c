/*
 * armperf.c
 *
 * Slightly modified version of armperf_entry.c by:
 * Prabindh Sundareson (prabu@ti.com) 2012
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

#include <linux/ioport.h>
#include <asm/io.h>

#include "v7_pmu.h"

#define MODULE_NAME "armperf"

static void pmu_start(unsigned int event_array[],unsigned int count);
static void pmu_stop(void);
static int __armperf_init_checks(void);
static void __exit armperf_exit(void);


#define EMIFCNT_MAP_LEN 0x200
#define EMIFCNT_MAP_BASE_ADDR 0x4c000000
static int emifcnt = 0;
static int emif_readcount = 0;
static int emif_writecount = 0;
static resource_size_t emifcnt_reg_base;
static int emiflist[2] = {2, 3};
static int emiflist_count = 2;

#if defined(CONFIG_PROC_FS)
#include <linux/proc_fs.h>
#define MAX_PROC_BUF_LEN 1000
static DEFINE_SPINLOCK(pblock);
static char proc_buf[MAX_PROC_BUF_LEN];
#endif

static int evlist[6] = {0x1, 0x8, 0x3, 0x4, 0, 0};
static int evlist_count = 4;
static int available_evcount = 0;
static int evdelay = 100; /* mSec */
static int evdebug = 0;

static void pmu_start(unsigned int event_array[],unsigned int count)
{
	int i;

	enable_pmu();              /* Enable the PMU */
	reset_ccnt();              /* Reset the CCNT (cycle counter) */
	reset_pmn();               /* Reset the configurable counters */
	write_flags((1 << 31) | 0xf); /*Reset overflow flags */

	for (i = 0; i < count; i++)
		pmn_config(i, event_array[i]);

	ccnt_divider(1); /* Enable divide by 64 */
	enable_ccnt();   /* Enable CCNT */

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

	disable_ccnt();

	for (i = 0;i < counters;i ++)
		disable_pmn(i);

	cycle_count = read_ccnt(); /*  Read CCNT */
	overflow = read_flags();   /* Check for overflow flag */

	if (emifcnt == 1) {
		/* Now read the READ+WRITE monitoring counters */
		 emif_writecount = ioread32(emifcnt_reg_base+0x80);
		 emif_readcount = ioread32(emifcnt_reg_base+0x84);
	}

#if defined(CONFIG_PROC_FS)
	spin_lock(&pblock);

	for(i = 0;i < counters;i ++) {
		currProcBufLen += sprintf(proc_buf + currProcBufLen,
					  "PMU.counter[%d]= %u\n", i, read_pmn(i));
	}
	currProcBufLen += sprintf(proc_buf + currProcBufLen,
				  "PMU.overflow= %u\nPMU.CCNT= %u\n",
				  overflow,cycle_count);
	currProcBufLen += sprintf(proc_buf + currProcBufLen,
				  "EMIF.readcount= %u\nEMIF.writecount= %u\n",
				  emif_readcount, emif_writecount);
	spin_unlock(&pblock);
#endif
	for(i = 0;i < counters;i ++) {
		if (evdebug == 1)
			printk("%u\t", read_pmn(i));
	}

	if (evdebug == 1)
		printk("%u\t%u\t", overflow,cycle_count);

	if (evdebug == 1)
		printk("%u\t%u\n", emif_readcount, emif_writecount);
}


/*
 * Monitoring Thread
 *
 */
static int armperf_thread(void* data)
{
	if (evdebug == 1)
		printk("Entering thread loop...\n");
	while(1) {
		if (kthread_should_stop())
			break;

		/* sample at regular-ish intervals */
		pmu_start(evlist, available_evcount);
		msleep(evdelay);
		pmu_stop();
		msleep(10);
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
	memset(proc_buf, 0, MAX_PROC_BUF_LEN);
	spin_unlock(&pblock);

	return outlen;
}

static __init int register_proc(void)
{
	struct proc_dir_entry *proc_armperf;

	proc_armperf = create_proc_entry("armperf", S_IRUGO, NULL);
	if (proc_armperf)
		proc_armperf->read_proc = armperf_proc_read;

	memset(proc_buf, 0, MAX_PROC_BUF_LEN);
	return proc_armperf != NULL;
}
#endif /* CONFIG_PROC_FS */


/*
 * Module entry points
 *
 */
static struct task_struct *armperf_kthread;
static struct resource *emifcnt_regs;

static int __armperf_init_checks()
{
	u32 regval1, regval2;
	available_evcount = getPMN();
	printk("armperf: evdelay=%d, available_events: %d "
	       "Event inputs: %d %d %d %d\n",
	       evdelay, available_evcount,
	       evlist[0], evlist[1], evlist[2], evlist[3]);

	armperf_kthread = kthread_run(armperf_thread, (void *)0,
				      "armperf_sampler");
	
	if (emifcnt == 1) {
		emifcnt_regs = request_mem_region(EMIFCNT_MAP_BASE_ADDR, EMIFCNT_MAP_LEN, "emifcnt");
		if (!emifcnt_regs)
			return 1;
		emifcnt_reg_base = (resource_size_t)ioremap_nocache(emifcnt_regs->start, EMIFCNT_MAP_LEN);
		if (!emifcnt_reg_base)
			return 1;
		/*
		 * Now enable READ+WRITE monitoring counters using PERF_CNT_CFG
		 */
		/* (READ=0x2 for CNT_2, WRITE=0x3 for CNT_1) */
		regval2 = 0x8000;
		regval2 |= (emiflist[0] & 0xF);
		regval2 = (regval2 << 16) & 0xFFFF0000;
		regval1 = 0x8000;
		regval1 |= (emiflist[1] & 0xF);
		iowrite32((regval2|regval1), emifcnt_reg_base+0x88);
	}

	return 0;
}

static int __init armperf_init(void)
{
	unsigned int retVal = 0;

	retVal = __armperf_init_checks();
	if (retVal) {
		printk("%s: armperf checks failed\n", __func__);
		return -ENODEV;
	}
#if defined(CONFIG_PROC_FS)
	register_proc();
#endif
	return 0;
}

static void __exit armperf_exit()
{
	kthread_stop(armperf_kthread);
#if defined(CONFIG_PROC_FS)
	remove_proc_entry("armperf", NULL);
#endif

	if (emifcnt == 1) {
		release_mem_region(EMIFCNT_MAP_BASE_ADDR, EMIFCNT_MAP_LEN);
		iounmap((void __iomem *)emifcnt_reg_base);
	}
}


/*
 * Configuration
 *
 */
module_param(evdelay, int, 100);
module_param(evdebug, int, 0);
module_param_array(evlist, int, &evlist_count, 0000);
module_param(emifcnt, int, 0);
module_param_array(emiflist, int, &emiflist_count, 0000);

late_initcall(armperf_init);
module_exit(armperf_exit);

MODULE_DESCRIPTION("PMU driver - insmod armperf.ko evdelay=500 evlist=1,68,3,4 evdebug=0 emifcnt=0 emiflist=2,3");
MODULE_AUTHOR("Prabindh Sundareson <prabu@ti.com>, Jeremy C. Andrus <jeremy@jeremya.com>");
MODULE_LICENSE("GPL v2");

