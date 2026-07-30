#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <paths.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <limits.h>
#include <sched.h>
#include <string.h>

#include "stats.h" 


/* Per-cpu time states */
static long *pcpu_cp_time;
static long *pcpu_cp_old;
static long *pcpu_cp_diff;
static int *pcpu_cpu_states;
static long *times;
static int maxcpu;
static int maxid;
static int ncpus;
static unsigned long cpumask;
static int init = 0;

static uint64_t mbuf_init_count = 0;
static uint64_t mbuf_9k_init_count = 0;
static uint64_t net_mem_init_size = 0;
static long percentages(int cnt, int *out, long *ne, long *old, long *diffs);

void
stats_init()
{
        if (init != 0)
                return;

        init = 1;
        somig_stat_get_mbuf_usage(&mbuf_init_count, &mbuf_9k_init_count);
        somig_stat_get_net_memory_usage(&net_mem_init_size);
}

void
somig_stat_get_mbuf_usage(uint64_t *mbuf_count, uint64_t *mbuf_9k_count)
{
        if (mbuf_count)
                *mbuf_count = 0;
        if (mbuf_9k_count)
                *mbuf_9k_count = 0;
}

void
somig_stat_get_net_memory_usage(uint64_t *bt)
{
        if (bt)
                *bt = 0;
}

void
somig_stat_refresh_cpu_usage()
{
        /* FreeBSD per-CPU sysctl statistics are unavailable on Linux. */
}

int
somig_stat_get_current_app_cpu()
{
        return sched_getcpu();
}

static void
cpu_util(int *syscpu, int *usrcpu, struct rusage *rlast)
{
    clock_t ctemp;
    struct rusage rtemp;
    double timediff;
    double userdiff;
    double systemdiff;

    //iperf_time_now(&now);
    getrusage(RUSAGE_SELF, &rtemp);
 
    //iperf_time_diff(&now, &last, &temp_time);
    //timediff = iperf_time_in_usecs(&temp_time);
    timediff = 1 * 1000 * 1000;

    userdiff = ((rtemp.ru_utime.tv_sec * 1000000.0 + rtemp.ru_utime.tv_usec) -
                (rlast->ru_utime.tv_sec * 1000000.0 + rlast->ru_utime.tv_usec));
    systemdiff = ((rtemp.ru_stime.tv_sec * 1000000.0 + rtemp.ru_stime.tv_usec) -
                  (rlast->ru_stime.tv_sec * 1000000.0 + rlast->ru_stime.tv_usec));

    //pcpu[0] = (((ctemp - clast) * 1000000.0 / CLOCKS_PER_SEC) / 1) * 100;
    *usrcpu = (userdiff / timediff) * 100;
    *syscpu = (systemdiff / timediff) * 100;

    memcpy(rlast, &rtemp, sizeof(struct rusage));
}

void
somig_stat_get_app_cpu(int *app, struct rusage *rlast)
{
	int syscpu, usrcpu, total;

	if (!app)
		return;

	cpu_util(&syscpu, &usrcpu, rlast);
	total = (syscpu + usrcpu) * 10;
	//printf("app usage(avg) %d: sys %d usr %d, sum %d\n", total, syscpu, usrcpu, syscpu + usrcpu);

	*app = total;
}



void
somig_stat_get_cpu_usage(int *cli, int *smg)
{
        if (cli)
                *cli = 0;
        if (smg)
                *smg = 0;
}

static long
percentages(int cnt, int *out, long *ne, long *old, long *diffs)
{
    int i;
    long change;
    long total_change;
    long *dp;
    long half_total;

    /* initialization */
    total_change = 0;
    dp = diffs;

    /* calculate changes for each state and the overall change */
    for (i = 0; i < cnt; i++)
    {
        if ((change = *ne - *old) < 0)
        {
            /* this only happens when the counter wraps */
            change = (int)
                ((unsigned long)*ne-(unsigned long)*old);
        }
        total_change += (*dp++ = change);
        *old++ = *ne++;
    }

    /* avoid divide by zero potential */
    if (total_change == 0)
    {
        total_change = 1;
    }

    /* calculate percentages based on overall change, rounding up */
    half_total = total_change / 2l;

        for (i = 0; i < cnt; i++)
        {
                *out++ = (int)((*diffs++ * 1000 + half_total) / total_change);
        }

    /* return the total in case the caller wants to use it */
    return(total_change);
}



