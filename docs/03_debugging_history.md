# 디버깅 및 문제 해결 기록

이 문서는 HA/TCP 기반 WAN Accelerator를 F-Stack 환경으로 옮기는 과정에서 발생한 주요 문제와 해결 과정을 정리한 문서이다.

## 1. HA/TCP 코드 전체 덮어쓰기 문제

초기에는 HA/TCP의 FreeBSD 코드 전체를 F-Stack의 FreeBSD 코드 위에 덮어쓰는 방식으로 시도했다.

하지만 F-Stack 내부 FreeBSD 코드와 HA/TCP 논문의 FreeBSD 코드 버전이 달라서 빌드 오류가 발생했다.

또한 F-Stack은 DPDK와 연동되는 별도 수정 코드가 포함되어 있기 때문에, FreeBSD 코드를 전체 덮어쓰면 F-Stack 고유 구조가 깨질 수 있었다.

따라서 전체 덮어쓰기 방식은 중단하고, HA/TCP의 핵심 기능만 선별적으로 이식하는 방식으로 변경했다.

## 2. HA/TCP 핵심 기능 선별 이식

전체 코드를 덮어쓰는 대신 TCP migration과 관련된 핵심 코드만 F-Stack에 반영했다.

주요 반영 항목은 다음과 같다.

- SOMIGRATION 관련 코드
- SMCP 관련 코드
- tcp_migration 관련 코드
- uipc_socket의 SOMIG option 처리
- TCP timer의 TT_SOMIG 처리
- HA/TCP 관련 header 및 build flag

이를 통해 F-Stack 구조를 유지하면서 HA/TCP 기능을 포함할 수 있도록 수정했다.

## 3. Linux socket path 문제

초기 wan_acc 수정 과정에서는 일부 경로가 여전히 Linux socket API를 사용하고 있었다.

이 경우 실제 WAN 구간 통신이 F-Stack이 아니라 Linux TCP stack을 통해 처리될 수 있다.

따라서 WAN 측 socket 처리 경로를 확인하고 다음과 같이 F-Stack API를 사용하도록 수정했다.

- socket() 대신 ff_socket()
- bind() 대신 ff_bind()
- listen() 대신 ff_listen()
- accept() 대신 ff_accept()
- connect() 대신 ff_connect()
- epoll 처리도 F-Stack epoll 기반으로 수정

핵심은 WAN 구간이 Linux TCP stack을 우회하고 F-Stack TCP/IP stack을 사용하도록 만드는 것이었다.

## 4. MS-to-ES 비동기 connect 문제

MS는 Client 요청을 받은 뒤 ES 측 WAN Accelerator로 연결해야 한다.

이때 F-Stack 기반 non-blocking connect를 사용하면서 연결이 즉시 완료되지 않고 EINPROGRESS 상태가 발생할 수 있었다.

이 문제를 해결하기 위해 WAN_CONNECTING 상태를 추가하고, EPOLLOUT 이벤트에서 연결 완료 여부를 확인하도록 수정했다.

관련 처리 항목은 다음과 같다.

- WAN_CONNECTING 상태 추가
- EPOLLOUT 이벤트 우선 처리
- finish_wan_connect() 추가
- connect 완료 후 TX queue flush
- 연결 완료 전 write 시도 방지

## 5. TX queue blocking 문제

WAN 연결이 아직 완료되지 않은 상태에서 데이터를 전송하려고 하면 TX queue에 데이터가 쌓이고 정상적으로 flush되지 않는 문제가 발생했다.

이를 해결하기 위해 WAN_CONNECTING 상태에서는 즉시 write하지 않고, 연결 완료 후 TX queue를 다시 비우도록 처리했다.

또한 닫힌 fd나 잘못된 stream에 남아 있는 TX queue를 정리하는 처리도 추가했다.

## 6. ES WAN listener 등록 문제

ES 측 WAN Accelerator는 MS에서 들어오는 WAN 연결을 받아야 한다.

하지만 listener fd가 Linux epoll/libev 쪽에만 등록되면 F-Stack 경로에서 accept가 정상적으로 처리되지 않는다.

따라서 ES WAN listener fd를 F-Stack epoll에 등록하도록 수정했다.

핵심은 다음과 같다.

ES WAN listener는 Linux epoll이 아니라 F-Stack epoll에 등록되어야 한다.

## 7. EPOLLOUT 이벤트 처리 순서 문제

비동기 connect에서는 EPOLLOUT 이벤트가 연결 완료 신호로 사용될 수 있다.

초기에는 read/write 이벤트 처리 순서가 맞지 않아 연결 완료 전에 데이터를 보내거나, 연결 완료 상태를 제대로 갱신하지 못하는 문제가 있었다.

이를 해결하기 위해 server_loop의 이벤트 처리 순서를 조정하고, EPOLLOUT 이벤트를 먼저 확인하도록 수정했다.

## 8. stream/fd 정리 문제

연결이 끊어진 stream이나 fd에 대해 TX queue가 남아 있으면 이후 처리에서 잘못된 fd에 접근할 수 있다.

이를 해결하기 위해 다음 처리를 추가했다.

- closed fd에 대한 TX queue 제거
- stream clean 처리 보강
- NULL stream guard 추가
- 잘못된 WAN stream 접근 방지

## 9. 빌드 문제

F-Stack과 wan_acc를 함께 빌드하는 과정에서 Makefile 관련 문제가 발생했다.

주요 원인은 다음과 같다.

- F-Stack link option 누락
- HA/TCP 관련 header include 문제
- FreeBSD library와 Linux library 충돌
- makefile_somig 설정 문제
- debug symbol 및 perf profiling option 설정 문제

이를 해결하기 위해 makefile_somig, makefile_fstack, hatcp_compat.h 등을 추가하거나 수정했다.

## 10. hugepage 및 DPDK 설정 문제

F-Stack은 DPDK 기반으로 동작하기 때문에 hugepage 설정과 NIC 설정이 필요하다.

실험 전 hugepage를 설정하고, F-Stack 설정 파일과 DPDK NIC 상태를 확인했다.

대표 설정 예시는 다음과 같다.

vm.nr_hugepages = 1024

## 11. perf 및 FlameGraph 분석 과정

성능 병목을 확인하기 위해 perf와 FlameGraph를 사용했다.

MS 측에서는 memset, rte_rdtsc, main_loop, ring 관련 처리 비중이 크게 나타났다.

ES 측에서는 rbkp_chunker의 self time 비중이 크게 나타났다.

이를 통해 worker 역할과 중복 제거/압축 처리의 CPU 사용 위치를 확인했다.

## 12. 정리

이번 디버깅 과정의 핵심은 단순히 빌드 오류를 고친 것이 아니라, wan_acc의 실제 WAN 통신 경로가 F-Stack을 사용하도록 바꾸고, MS와 ES 사이의 비동기 연결 흐름을 안정화한 것이다.

최종적으로 F-Stack 기반 WAN Accelerator 실행, wrkwrk 측정, perf/FlameGraph 분석까지 수행했다.
