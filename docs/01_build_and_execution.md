# Build and Execution

Korean version: [01_build_and_execution_KOR.md](01_build_and_execution_KOR.md)

This document summarizes how the HA/TCP-based WAN Accelerator was built and executed in the F-Stack environment.

## 1. Experiment Topology

```text
Client / wrkwrk
-> Node 1: MS WAN Accelerator
-> F-Stack / DPDK WAN path
-> Node 2: ES WAN Accelerator
-> Remote Application Server
```

## 2. Build

Build the WAN Accelerator in `apps/wan_acc`.

```bash
cd ~/kwon/hatcp/apps/wan_acc
make -f makefile_somig clean
make -f makefile_somig -j$(nproc)
```

A successful build produces the `wanacc` executable.

## 3. Run ES

Start the ES WAN Accelerator on Node 2 first.

```bash
cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M es -S 192.168.1.2 -p 3301 -E 10.20.24.171 -e 3302 -f 2 -b 3
```

## 4. Run MS

Start the MS WAN Accelerator on Node 1.

```bash
cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M ms -S 10.20.17.225 -p 3300 -E 192.168.1.2 -e 3301 -f 2 -b 3
```

## 5. Client Measurement

On the client, use `wrkwrk` to send requests to MS.

```bash
cd ~/kwon/hatcp/apps/wrkwrk
./wrkwrk -m wanacc -s 10.20.17.165 -p 3300 -T 1 -c 8 -d 30 -f http://10.20.24.171:3302/Bible_10MiB.txt
```

## 6. Main Options

- `-M ms` : run in MS mode
- `-M es` : run in ES mode
- `-S` : local IP
- `-p` : local port
- `-E` : remote or backend IP
- `-e` : remote or backend port
- `-f` : number of Front Workers
- `-b` : number of Back Workers

## 7. Feature Disable Flags

The `-o` option is a bit flag used to disable deduplication and compression.

- `-o 0` : deduplication enabled, compression enabled
- `-o 1` : deduplication disabled, compression enabled
- `-o 2` : deduplication enabled, compression disabled
- `-o 3` : deduplication disabled, compression disabled

Bit definitions:

```text
0x1 : NO_DEDUP
0x2 : NO_COMPRESSION
```

## 8. Representative Result

```text
Average latency      : ~135.558 ms
Average throughput   : ~333.742 MB/s = ~2.734 Gbps
Average request rate : ~32.586 requests/s
TCP retransmissions  : 0
```

## 9. Notes

To run with F-Stack, hugepages, DPDK NIC configuration, and the F-Stack configuration file must be set correctly.

The ES-side WAN listener must be registered with F-Stack epoll rather than Linux epoll in order to accept incoming WAN connections from MS.

> **Note**
>
> This document records the build and execution procedure used in the HA/TCP-side development and experiment environment.
> For the final F-Stack-integrated version, refer to the `integrated` branch of `DaEun2Lee/f-stack`.
