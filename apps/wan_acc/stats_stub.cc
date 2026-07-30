#include <stdint.h>
#include <sys/resource.h>

void stats_init() {}

void somig_stat_get_mbuf_usage(uint64_t *mbuf_count, uint64_t *mbuf_9k_count) {
    if (mbuf_count) *mbuf_count = 0;
    if (mbuf_9k_count) *mbuf_9k_count = 0;
}

void somig_stat_get_net_memory_usage(uint64_t *bt) {
    if (bt) *bt = 0;
}

void somig_stat_refresh_cpu_usage() {}

int somig_stat_get_current_app_cpu() {
    return -1;
}

void somig_stat_get_cpu_usage(int *cli, int *smg) {
    if (cli) *cli = 0;
    if (smg) *smg = 0;
}

void somig_stat_get_app_cpu(int *app, struct rusage *rlast) {
    if (app) *app = 0;
}
