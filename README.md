# HA/TCP WAN Accelerator

This repository contains the HA/TCP-side WAN Accelerator implementation, experiment code, and related documentation used during the HA/TCP/F-Stack integration work.

The original HA/TCP project is available at:

https://github.com/rcslab/hatcp

This repository is a modified fork used for WAN Accelerator development and experiments.

For the final F-Stack-integrated HA/TCP implementation, use:

https://github.com/DaEun2Lee/f-stack/tree/integrated

Korean documentation is available in [README_KOR.md](README_KOR.md).

---

## Project Period

2026-07-01 to 2026-07-30

---

## Project Goal

The original `wan_acc` application was based on the Linux socket API.

The goal of this work was to adapt the WAN-side communication path so that the WAN Accelerator could operate with F-Stack/DPDK while incorporating HA/TCP-related functionality.

The work also included changes to the WAN connection flow, worker organization, build configuration, performance measurement, and debugging of the HA/TCP/F-Stack integration.

---

## Repository Roles

This repository and the modified F-Stack repository serve different purposes.

### `DaEun2Lee/hatcp`

This repository contains the HA/TCP-side WAN Accelerator development, including:

- WAN Accelerator source changes
- Front/Back Worker restructuring
- asynchronous WAN connection handling
- F-Stack compatibility code in `apps/wan_acc`
- `wrkwrk` modifications
- experiment scripts
- performance-analysis documents
- debugging history

### `DaEun2Lee/f-stack`

The actual HA/TCP port integrated into F-Stack is maintained separately:

https://github.com/DaEun2Lee/f-stack

Use the `integrated` branch for the F-Stack-based HA/TCP WAN Accelerator.

That repository contains the F-Stack/FreeBSD-side integration, including HA/TCP TCP migration support, SMCP-related changes, socket/TCP-stack modifications, and the F-Stack-compatible WAN Accelerator source.

---

## Branches

### `master`

- Preserves the original HA/TCP baseline.
- Corresponds to the upstream HA/TCP repository lineage.
- Intended to remain close to `rcslab/hatcp`.

### `main`

- Default branch of this repository.
- Contains the modified HA/TCP/WAN Accelerator implementation used for experiments.
- Includes source changes, experiment scripts, documentation, and performance-analysis material.

For this repository, use `main` unless you specifically need the original HA/TCP baseline.

---

## Main Work

Major work performed during this project includes:

- adapting WAN-side socket handling for F-Stack
- modifying the MS-to-ES WAN connection flow
- separating Front Worker and Back Worker roles
- adding SOMIGRATION-, SMCP-, and F-Stack-related build configuration
- handling asynchronous MS-to-ES WAN connection establishment
- registering the ES WAN listener with F-Stack epoll
- improving TX queue handling for non-blocking WAN connections
- fixing stream/fd cleanup and invalid-stream handling
- modifying `wrkwrk` for experiment traffic generation
- measuring performance with `wrkwrk`, `perf`, and FlameGraph
- analyzing CPU bottlenecks on MS and ES

The HA/TCP TCP migration and FreeBSD TCP-stack integration itself is maintained in the separate F-Stack repository.

---

## Experiment Topology

```text
Client / wrkwrk
      |
      v
MS WAN Accelerator
      |
      v
F-Stack / DPDK WAN path
      |
      v
ES WAN Accelerator
      |
      v
Remote Application Server
```

The WAN path between MS and ES was the primary target of the F-Stack integration.

---

## Important Modified Files

### WAN Accelerator

- `apps/wan_acc/server.cc`
  - MS/ES connection handling
  - asynchronous WAN connect
  - F-Stack epoll event handling
  - WAN path state handling

- `apps/wan_acc/worker.cc`
  - Front Worker / Back Worker processing flow
  - chunk/reference processing
  - TX/RX path handling

- `apps/wan_acc/netutils.cc`
  - socket abstraction
  - F-Stack API handling
  - WAN-side socket operations

- `apps/wan_acc/netutils.h`
  - WAN networking declarations and compatibility definitions

- `apps/wan_acc/main.cc`
  - execution options
  - application initialization

- `apps/wan_acc/acc.cc`
  - accelerator data-path logic

- `apps/wan_acc/acc.h`
  - accelerator-related declarations

- `apps/wan_acc/hatcp_compat.h`
  - HA/TCP/F-Stack compatibility definitions

### Build Files

- `apps/wan_acc/makefile`
- `apps/wan_acc/Makefile`
- `apps/wan_acc/makefile_fstack`
- `apps/wan_acc/makefile_somig`

These files contain build configurations used for different experiment stages and HA/TCP/F-Stack integration modes.

### Workload Generator

- `apps/wrkwrk/wrkwrk.cc`
- `apps/wrkwrk/netutil.cc`
- `apps/wrkwrk/utils.h`

These files contain modifications used for WAN Accelerator traffic generation and measurement.

---

## Front Worker / Back Worker Architecture

The WAN Accelerator does more than simply forward data.

The processing path includes:

```text
Receive data
  -> Front Worker
  -> chunking
  -> duplicate detection
  -> classify new/reference chunks
  -> Back Worker
  -> compression/decompression or reconstruction
  -> output-buffer generation
  -> transmit
```

### Front Worker

The Front Worker is responsible for input-side processing:

- receiving data from the Client or WAN side
- managing input buffers
- splitting data into chunks
- calculating chunk hashes
- checking duplicate chunks
- classifying new chunks and reference chunks
- dispatching work to Back Workers

### Back Worker

The Back Worker is responsible for output-side processing:

- receiving chunk work from Front Workers
- processing new chunks
- reconstructing reference chunks
- compression/decompression
- generating output buffers
- transmitting data toward the WAN side or backend application

Detailed documentation:

[docs/02_front_back_worker.md](docs/02_front_back_worker.md)

---

## Build and Execution

Detailed build and execution instructions are documented in:

[docs/01_build_and_execution.md](docs/01_build_and_execution.md)

A representative build command for the HA/TCP-side WAN Accelerator is:

```bash
cd ~/kwon/hatcp/apps/wan_acc
make -f makefile_somig clean
make -f makefile_somig -j$(nproc)
```

A successful build produces the `wanacc` executable.

> **Note**
>
> This repository documents the HA/TCP-side development and experiment environment.
> For the final F-Stack-integrated source tree, use the `integrated` branch of:
>
> https://github.com/DaEun2Lee/f-stack

---

## Representative Execution

### ES

```bash
cd ~/kwon/hatcp/apps/wan_acc

./wanacc \
  -M es \
  -S <ES_LOCAL_IP> \
  -p 3301 \
  -E <BACKEND_SERVER_IP> \
  -e 3302 \
  -f 2 \
  -b 3
```

### MS

```bash
cd ~/kwon/hatcp/apps/wan_acc

./wanacc \
  -M ms \
  -S <MS_LOCAL_IP> \
  -p 3300 \
  -E <ES_WAN_IP> \
  -e 3301 \
  -f 2 \
  -b 3
```

### Client / wrkwrk

```bash
cd ~/kwon/hatcp/apps/wrkwrk

./wrkwrk \
  -m wanacc \
  -s <MS_CLIENT_SIDE_IP> \
  -p 3300 \
  -T 1 \
  -c 8 \
  -d 30 \
  -f http://<BACKEND_SERVER_IP>:3302/<TEST_FILE>
```

Replace all addresses and test-file paths with values appropriate for the current environment.

---

## Important Runtime Options

- `-M ms`
  - run as MS WAN Accelerator

- `-M es`
  - run as ES WAN Accelerator

- `-S`
  - local IP address

- `-p`
  - local listening port

- `-E`
  - remote WAN Accelerator or backend IP address

- `-e`
  - remote or backend port

- `-f`
  - number of Front Workers

- `-b`
  - number of Back Workers

### Feature-disable flags

The `-o` option is a bit flag.

- `-o 0`
  - deduplication enabled
  - compression enabled

- `-o 1`
  - deduplication disabled
  - compression enabled

- `-o 2`
  - deduplication enabled
  - compression disabled

- `-o 3`
  - deduplication disabled
  - compression disabled

Bit definitions:

```text
0x1 = NO_DEDUP
0x2 = NO_COMPRESSION
```

---

## Representative Performance Result

One representative experiment used:

- transfer file size: 10 MiB
- concurrent connections: 8
- Front Workers: 2
- Back Workers: 3
- measurement duration: 30 seconds

Representative result:

- average latency: approximately 135.558 ms
- average throughput: approximately 333.742 MB/s
- equivalent throughput: approximately 2.734 Gbps
- average request rate: approximately 32.586 requests/s
- TCP retransmissions: 0

These values are representative experiment results, not guaranteed performance for other systems.

Performance depends on CPU allocation, NIC/DPDK configuration, worker count, test workload, and server environment.

Detailed results and analysis are available in:

[docs/04_performance_and_flamegraph.md](docs/04_performance_and_flamegraph.md)

---

## Performance Analysis

`perf` and FlameGraph were used to identify CPU bottlenecks.

Representative observations included:

### MS

High CPU usage was observed in areas such as:

- memory initialization
- `rte_rdtsc`
- F-Stack `main_loop`
- ring-related processing

### ES

A large CPU share was observed in:

- `rbkp_chunker`
- chunk processing
- duplicate-elimination-related processing
- reference reconstruction

The measurements showed that MS and ES can have different CPU bottlenecks.

See:

[docs/04_performance_and_flamegraph.md](docs/04_performance_and_flamegraph.md)

---

## Debugging and Integration Notes

Important issues encountered during development included:

- FreeBSD version mismatch between HA/TCP and F-Stack
- inability to replace the entire F-Stack FreeBSD tree with HA/TCP code
- Linux socket paths remaining in WAN-side processing
- non-blocking `connect()` / `EINPROGRESS` handling
- `EPOLLOUT` event ordering
- TX queue handling before WAN connection completion
- ES WAN listener registration
- stale stream/fd cleanup
- Makefile and link-option problems
- hugepage and DPDK configuration
- perf/FlameGraph analysis

Instead of replacing the complete FreeBSD source tree, HA/TCP-related functionality was selectively ported into the F-Stack/FreeBSD stack.

Detailed debugging history:

[docs/03_debugging_history.md](docs/03_debugging_history.md)

---

## Documentation

- [Build and Execution](docs/01_build_and_execution.md)
- [Front Worker / Back Worker Architecture](docs/02_front_back_worker.md)
- [Debugging and Troubleshooting History](docs/03_debugging_history.md)
- [Performance and perf/FlameGraph Analysis](docs/04_performance_and_flamegraph.md)
- [Development Timeline](timeline/2026-07-01_to_07-30.md)

---

## Related Repositories

### Original HA/TCP

https://github.com/rcslab/hatcp

### Modified HA/TCP / WAN Accelerator

https://github.com/DaEun2Lee/hatcp

### HA/TCP Port Integrated into F-Stack

https://github.com/DaEun2Lee/f-stack/tree/integrated

---

## Current Status

As of 2026-08-13:

- `main` contains the modified HA/TCP/WAN Accelerator implementation.
- `master` preserves the original HA/TCP baseline.
- `main` is intended to be the default branch of this repository.
- the HA/TCP port integrated into F-Stack is maintained in `DaEun2Lee/f-stack` on the `integrated` branch.
- detailed build, debugging, worker-architecture, and performance documentation is included in this repository.

---

## Notes

This repository contains research and experiment code.

Before reproducing the environment on another server, verify:

- NIC configuration
- DPDK binding
- hugepage configuration
- F-Stack configuration
- CPU/lcore allocation
- IP addresses and ports
- worker counts
- build options

Server-specific values from the original experiment should not be assumed to work unchanged in a different environment.
