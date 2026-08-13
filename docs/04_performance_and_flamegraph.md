# Performance Measurement and perf/FlameGraph Analysis

Korean version: [04_performance_and_flamegraph_KOR.md](04_performance_and_flamegraph_KOR.md)

This document summarizes representative performance measurements and perf/FlameGraph analysis for the HA/TCP-based WAN Accelerator.

## 1. Measurement Goal

The purpose of the experiment was to verify that the F-Stack-modified WAN Accelerator operated correctly between MS and ES and to identify where CPU time was spent in the worker structure and the deduplication/compression processing path.

The measured items included:

- average latency
- average throughput
- average request rate
- TCP retransmission count
- CPU-intensive functions identified by perf
- bottleneck locations identified by FlameGraph

## 2. Representative Experiment Environment

```text
Node 1: MS WAN Accelerator
Node 2: ES WAN Accelerator
Client: wrkwrk
Backend: Remote Application Server
WAN path: F-Stack / DPDK
Transfer file size: 10 MiB
Connections: 8
Front Workers: 2
Back Workers: 3
Measurement duration: 30 seconds
```

## 3. Representative Commands

Run ES:

```bash
cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M es -S 192.168.1.2 -p 3301 -E 10.20.24.171 -e 3302 -f 2 -b 3
```

Run MS:

```bash
cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M ms -S 10.20.17.225 -p 3300 -E 192.168.1.2 -e 3301 -f 2 -b 3
```

Run the client measurement:

```bash
cd ~/kwon/hatcp/apps/wrkwrk
./wrkwrk -m wanacc -s 10.20.17.165 -p 3300 -T 1 -c 8 -d 30 -f http://10.20.24.171:3302/Bible_10MiB.txt
```

## 4. Representative Results

```text
Average latency      : ~135.558 ms
Average throughput   : ~333.742 MB/s = ~2.734 Gbps
Average request rate : ~32.586 requests/s
TCP retransmissions  : 0
```

These results show that the WAN Accelerator path between MS and ES was connected successfully and that 10 MiB file requests could be processed using `wrkwrk`.

## 5. Comparison with an Earlier Measurement

An earlier measurement produced the following results:

```text
Average latency      : ~180.9 ms
Average throughput   : ~305.9 MB/s = ~2.45 Gbps
Average request rate : ~29.2 requests/s
TCP retransmissions  : 0
```

In the later measurement, average latency decreased while average throughput and request rate increased.

## 6. Purpose of perf Analysis

`perf` is a performance-analysis tool used to determine which functions consume CPU time while a program is running.

In this experiment, perf was used separately on MS and ES to identify functions with large CPU-time shares during WAN Accelerator execution.

## 7. MS perf Results

Major functions observed on MS included:

```text
__memset_avx2_erms : ~19-20%
rte_rdtsc          : ~15%
main_loop          : ~11-13%
ring processing    : ~7-9%
```

On MS, large costs were observed in memory initialization, the F-Stack main loop, DPDK time measurement, and inter-worker ring transfer.

This indicates that MS is strongly affected by the F-Stack loop and worker-transfer structure while receiving Client requests and forwarding data toward the WAN.

## 8. ES perf Results

On ES, `rbkp_chunker` showed a large self-time share.

```text
rbkp_chunker self time : ~40.6-41%
```

This indicates that chunk splitting and deduplication-related processing consumed a large portion of CPU time on ES.

Therefore, on ES, chunk/reference interpretation and reconstruction are important bottleneck candidates in addition to simple send/receive processing.

## 9. FlameGraph Analysis

FlameGraph visualizes perf data to show which function-call paths consume CPU time.

The following artifacts were generated during the experiment.

MS:

- `ms_out.perf`
- `ms_out.folded`
- `ms_flamegraph.svg`

ES:

- `es_out.perf`
- `es_out.folded`
- `es_flamegraph.svg`

The FlameGraphs showed that MS and ES had different bottleneck locations.

On MS, the F-Stack loop, memory processing, and ring-transfer costs were important. On ES, chunk-processing cost around `rbkp_chunker` was dominant.

## 10. Analysis Summary

Potential MS bottlenecks:

- F-Stack main loop
- DPDK time measurement
- inter-worker ring transfer
- memory initialization and buffer processing

Potential ES bottlenecks:

- `rbkp_chunker`
- chunk splitting
- deduplication-related processing
- reference reconstruction

Therefore, improving WAN Accelerator performance requires more than simply increasing the worker count. Different bottlenecks on MS and ES must be considered separately.

## 11. Conclusion

The experiment confirmed that the F-Stack-based WAN Accelerator operated correctly and that throughput and latency could be measured using `wrkwrk`.

perf and FlameGraph also showed that MS and ES had different CPU-usage characteristics.

MS was more strongly affected by the F-Stack loop and worker-transfer costs, while ES was more strongly affected by chunking and deduplication processing.
