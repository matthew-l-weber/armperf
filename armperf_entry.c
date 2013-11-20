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
#include <linux/io.h>     //ioread, iowrite
#include <linux/ioport.h> //request_mem_region

#include "v7_pmu.h"

#define MODULE_NAME "armperf"

static void pmu_start(unsigned int event_array[],unsigned int count);
static void pmu_stop(void);
static int __pmu_init(void);
static void __exit armperf_exit(void);

#if defined(CONFIG_ARCH_ZYNQ)
#define DDR_PERFMON 1
#define DDRCNT_MAP_LEN       0x2C0
#define DDRCNT_MAP_BASE_ADDR 0xF8006000

/******************************************************************
 * AXI IDs - Sec 5.1.6, Table 5-3, page 98 Zynq_TRM (ug585)
 *
 * CPUs, AXI_ACP via L2 M1 port
 *        13â€™b011xxxxxxxx00
 * CPUs, AXI_ACP via L2 M0 port
 *        13â€™b100xxxxxxxx00
 * AHB masters
 *        13â€™b00100000xxx01
 ******************************************************************
 * DDRI Block Diagram 
 *     Sec 10.2.2, Figure 10-3, page 211 Zynq_TRM (ug585)
 * Port 2/3 - AXI_HP to DDR Interconnect
 * Port 1 - Other Masters Interconnect
 * Port 0 - CPUs/ACP via L2
 ******************************************************************/
static int ddr_readOffset  = 0x270;  //Port0
static int ddr_writeOffset = 0x280;  //Port0

#define DDR_PERF_CONFIG() 

#endif

#if defined(CONFIG_ARCH_SOCFPGA)
#define DDR_PERFMON 1
#define DDRCNT_MAP_LEN       0x2bad
#define DDRCNT_MAP_BASE_ADDR 0x2bad
static int ddr_readOffset  = 0x2bad;
static int ddr_writeOffset = 0x2bad;

#define DDR_PERF_CONFIG() 

#endif

#if defined(CONFIG_ARCH_OMAP2PLUS)
#define DDR_PERFMON 1
#define DDRCNT_MAP_LEN       0x200
#define DDRCNT_MAP_BASE_ADDR 0x4c000000
static int ddr_readOffset  = 0x84;
static int ddr_writeOffset = 0x80;
static int ddr_ctlOffset   = 0x88;

#define DDR_PERF_CONFIG() \
	regval2 = 0x8000; \
	regval2 |= (ddrlist[0] & 0xF); \
	regval2 = (regval2 << 16) & 0xFFFF0000; \
	regval1 = 0x8000; \
	regval1 |= (ddrlist[1] & 0xF); \
	iowrite32((regval2|regval1),(void*)(ddrcnt_reg_base+ddr_ctlOffset)); 

#endif

#ifdef DDR_PERFMON
static struct resource *ddrcnt_regs;
static int ddrcnt = 0;
static int ddr_readcount = 0;
static int ddr_writecount = 0;
static resource_size_t ddrcnt_reg_base;
static int ddrlist[2] = {1, 2};  //1 - read, 2 - write
static int ddrlist_count = 2;
#endif

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
#ifdef DDR_PERFMON
	if(ddrcnt == 1)
	{
                //Now read the READ+WRITE monitoring counters
                 ddr_writecount = ioread32((void*)(ddrcnt_reg_base + ddr_writeOffset));
                 ddr_readcount = ioread32((void*)(ddrcnt_reg_base + ddr_readOffset));
        }
#endif
#if defined(CONFIG_PROC_FS)
	spin_lock(&pblock);

	for(i = 0;i < counters;i ++) {
		currProcBufLen += sprintf(proc_buf + currProcBufLen,
					  "PMU.counter[%d]= %u\n", i, read_pmn(i));
	}
	currProcBufLen += sprintf(proc_buf + currProcBufLen,
				  "PMU.overflow= %u\nPMU.CCNT= %u\n",
				  overflow,cycle_count);
#ifdef DDR_PERFMON				  
	currProcBufLen += sprintf(proc_buf + currProcBufLen,
				  "DDR.readcount= %u\nDDR.writecount= %u\n",
				  ddr_readcount, ddr_writecount);
#endif
	spin_unlock(&pblock);
#endif

	if (evdebug) {
		printk("%u\t%u\t", cycle_count, overflow);

		for (i = 0;i < counters;i ++)
			printk("%u\t", read_pmn(i));

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
#ifdef DDR_PERFMON
	u32 regval1, regval2;
#endif
	available_evcount = getPMN();
	printk(MODULE_NAME ": evdelay=%d, available_events: %d "
	       "Event inputs: %d %d %d %d\n",
	       evdelay, available_evcount,
	       evlist[0], evlist[1], evlist[2], evlist[3]);

	armperf_kthread = kthread_run(armperf_thread, (void *)0,
				      "armperf_sampler");
#ifdef DDR_PERFMON	
        if(ddrcnt == 1)
        {
                ddrcnt_regs = request_mem_region(DDRCNT_MAP_BASE_ADDR, DDRCNT_MAP_LEN, "ddrcnt");
                if (!ddrcnt_regs)
                        return 1;
                ddrcnt_reg_base = (resource_size_t)ioremap_nocache(ddrcnt_regs->start, DDRCNT_MAP_LEN);
                if (!ddrcnt_reg_base)
                        return 1;
                //Now enable monitoring counters
                DDR_PERF_CONFIG();
        }	
#endif	
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

#ifdef DDR_PERFMON
        if(ddrcnt == 1)
        {
                release_mem_region(DDRCNT_MAP_BASE_ADDR, DDRCNT_MAP_LEN);
                iounmap((void __iomem *)ddrcnt_reg_base);
        }
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
#ifdef DDR_PERFMON
module_param(ddrcnt, int, 0);
module_param_array(ddrlist, int, &ddrlist_count, 0000);
#endif

MODULE_DESCRIPTION(MODULE_NAME ": PMU driver (e.g. insmod armperf.ko "
		   "evdelay=500 evlist=1,68,3,4 evdebug=0");
MODULE_AUTHOR("Prabindh Sundareson <prabu@ti.com>");
MODULE_AUTHOR("Jeremy C. Andrus <jeremy@jeremya.com>");
MODULE_AUTHOR("Matthew L. Weber <mlweber1@rockwellcollins.com>");
MODULE_LICENSE("GPL v2");
