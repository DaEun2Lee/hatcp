# Debugging and Troubleshooting History

Korean version: [03_debugging_history_KOR.md](03_debugging_history_KOR.md)

This document summarizes the major problems encountered while moving the HA/TCP-based WAN Accelerator to the F-Stack environment and the approaches used to resolve them.

## 1. Problem with Replacing the Entire HA/TCP FreeBSD Tree

The initial approach attempted to overwrite the F-Stack FreeBSD source tree with the complete HA/TCP FreeBSD source tree.

However, the FreeBSD version used inside F-Stack differed from the FreeBSD version used by HA/TCP, which caused build failures.

F-Stack also contains its own DPDK-related modifications. Replacing the entire FreeBSD tree could therefore break F-Stack-specific behavior.

For this reason, the full replacement approach was abandoned in favor of selectively porting the HA/TCP functionality that was actually required.

## 2. Selective Porting of HA/TCP Core Functionality

Instead of replacing the complete source tree, only core TCP-migration-related functionality was integrated into F-Stack.

Major integrated items included:

- SOMIGRATION-related code
- SMCP-related code
- `tcp_migration`-related code
- SOMIG option handling in `uipc_socket`
- `TT_SOMIG` handling in TCP timers
- HA/TCP-related headers and build flags

This approach preserved the F-Stack structure while adding the required HA/TCP functionality.

## 3. Linux Socket Path Problem

During early `wan_acc` modifications, some paths still used the Linux socket API.

In that case, WAN traffic could be processed by the Linux TCP stack instead of the F-Stack TCP/IP stack.

The WAN-side socket path was therefore changed to use F-Stack APIs:

- `socket()` -> `ff_socket()`
- `bind()` -> `ff_bind()`
- `listen()` -> `ff_listen()`
- `accept()` -> `ff_accept()`
- `connect()` -> `ff_connect()`
- epoll handling was also changed to F-Stack epoll

The key goal was to make the WAN path use the F-Stack TCP/IP stack rather than the Linux TCP stack.

## 4. Asynchronous MS-to-ES Connect Problem

MS must establish a connection to the ES WAN Accelerator after receiving a Client request.

With F-Stack-based non-blocking connect, the connection may not complete immediately and can enter the `EINPROGRESS` state.

To handle this, a `WAN_CONNECTING` state was added and connection completion was checked on `EPOLLOUT`.

The related changes included:

- adding the `WAN_CONNECTING` state
- prioritizing `EPOLLOUT` event handling
- adding `finish_wan_connect()`
- flushing the TX queue after connect completion
- preventing writes before the connection is complete

## 5. TX Queue Blocking Problem

If data is transmitted before the WAN connection is fully established, data can accumulate in the TX queue and fail to flush correctly.

To address this, writes are deferred while the stream is in the `WAN_CONNECTING` state, and the TX queue is flushed after connection completion.

Cleanup logic was also added for TX queue entries associated with closed file descriptors or invalid streams.

## 6. ES WAN Listener Registration Problem

The ES WAN Accelerator must accept WAN connections from MS.

If the listener fd is registered only with Linux epoll/libev, accepts may not be handled correctly through the F-Stack path.

The ES WAN listener was therefore registered with F-Stack epoll.

The key requirement is:

The ES WAN listener must be registered with F-Stack epoll rather than Linux epoll.

## 7. EPOLLOUT Event Ordering Problem

For asynchronous connect, `EPOLLOUT` can indicate that connection establishment has completed.

In the initial implementation, the read/write event order could cause data to be sent before the connection was complete or prevent the connection state from being updated correctly.

The event-processing order in `server_loop` was therefore changed so that `EPOLLOUT` is checked first.

## 8. Stream/File Descriptor Cleanup Problem

If a TX queue remains associated with a disconnected stream or fd, later processing may access an invalid descriptor.

The following cleanup was added:

- remove TX queues associated with closed fds
- strengthen stream cleanup
- add NULL stream guards
- prevent access to invalid WAN streams

## 9. Build Problems

Several Makefile-related issues occurred while building F-Stack and `wan_acc` together.

Major causes included:

- missing F-Stack link options
- HA/TCP header include problems
- conflicts between FreeBSD and Linux libraries
- `makefile_somig` configuration problems
- debug-symbol and perf-profiling option configuration

To address these issues, files such as `makefile_somig`, `makefile_fstack`, and `hatcp_compat.h` were added or modified.

## 10. Hugepage and DPDK Configuration Problems

F-Stack is based on DPDK, so hugepage and NIC configuration are required.

Before experiments, hugepages were configured and both the F-Stack configuration file and the DPDK NIC state were checked.

A representative setting was:

```text
vm.nr_hugepages = 1024
```

## 11. perf and FlameGraph Analysis

`perf` and FlameGraph were used to identify performance bottlenecks.

On MS, large CPU shares were observed in `memset`, `rte_rdtsc`, `main_loop`, and ring-related processing.

On ES, `rbkp_chunker` showed a large self-time share.

These measurements helped identify where worker processing and deduplication/compression work consumed CPU time.

## 12. Summary

The main purpose of this debugging work was not only to fix build errors, but also to ensure that the actual WAN communication path of `wan_acc` used F-Stack and that the asynchronous connection flow between MS and ES was stable.

The final experiment included execution of the F-Stack-based WAN Accelerator, measurements with `wrkwrk`, and perf/FlameGraph analysis.
