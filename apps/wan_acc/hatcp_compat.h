#ifndef HATCP_COMPAT_H
#define HATCP_COMPAT_H

#include <stdint.h>

/* FreeBSD socket/TCP option numbers used by ff_*_freebsd(). */
#define FF_SOL_SOCKET 0xffff
#define FF_TCP_INFO   32

/* HA/TCP socket roles. */
#define SOMIG_REPLICA 1
#define SOMIG_PRIMARY 3

/* HA/TCP socket options. */
#define SO_MIG_ROLE     0x1020
#define SO_MIG_BIND     0x1024
#define SO_MIG_PREBIND  0x1025
#define SO_MIG_LISTEN   0x1026
#define SO_MIG_CONNECT  0x1027
#define SO_MIG_MIGRATE  0x1028

#define SOMIG_MIGRATION_FLAG_FORCE_FAIL 0x80000000U

struct somig_migopt {
        uint32_t node;
        uint32_t flag;
};

/* FreeBSD/HA-TCP TCP_INFO layout. */
struct ff_tcp_info {
        uint8_t  tcpi_state;
        uint8_t  __tcpi_ca_state;
        uint8_t  __tcpi_retransmits;
        uint8_t  __tcpi_probes;
        uint8_t  __tcpi_backoff;
        uint8_t  tcpi_options;
        uint8_t  tcpi_snd_wscale:4;
        uint8_t  tcpi_rcv_wscale:4;

        uint32_t tcpi_rto;
        uint32_t __tcpi_ato;
        uint32_t tcpi_snd_mss;
        uint32_t tcpi_rcv_mss;

        uint32_t __tcpi_unacked;
        uint32_t __tcpi_sacked;
        uint32_t __tcpi_lost;
        uint32_t __tcpi_retrans;
        uint32_t __tcpi_fackets;

        uint32_t __tcpi_last_data_sent;
        uint32_t __tcpi_last_ack_sent;
        uint32_t tcpi_last_data_recv;
        uint32_t __tcpi_last_ack_recv;

        uint32_t __tcpi_pmtu;
        uint32_t __tcpi_rcv_ssthresh;
        uint32_t tcpi_rtt;
        uint32_t tcpi_rttvar;
        uint32_t tcpi_snd_ssthresh;
        uint32_t tcpi_snd_cwnd;
        uint32_t __tcpi_advmss;
        uint32_t __tcpi_reordering;

        uint32_t __tcpi_rcv_rtt;
        uint32_t tcpi_rcv_space;

        uint32_t tcpi_snd_wnd;
        uint32_t tcpi_snd_bwnd;
        uint32_t tcpi_snd_nxt;
        uint32_t tcpi_rcv_nxt;
        uint32_t tcpi_toe_tid;
        uint32_t tcpi_snd_rexmitpack;
        uint32_t tcpi_rcv_ooopack;
        uint32_t tcpi_snd_zerowin;

        uint32_t tcpi_smg_bufsize;
        uint32_t tcpi_smg_bufsize_cso;
        uint32_t tcpi_cli_cpu_id;
        uint32_t tcpi_smg_cpu_id;
        uint32_t __tcpi_pad[22];
};

#endif
