# HA/TCP WAN Accelerator

이 저장소는 HA/TCP 기반 WAN Accelerator의 개발 코드, 실험 코드, 그리고 HA/TCP/F-Stack 통합 과정에서 작성된 관련 문서를 포함합니다.

원본 HA/TCP 프로젝트:

https://github.com/rcslab/hatcp

이 저장소는 WAN Accelerator 개발 및 실험을 위해 수정한 fork입니다.

HA/TCP를 F-Stack에 실제로 통합한 최종 포팅본은 아래 저장소에서 관리합니다.

https://github.com/DaEun2Lee/f-stack/tree/integrated

영문 기본 문서는 [README.md](README.md)를 참고하세요.

---

## 작업 기간

2026-07-01 ~ 2026-07-30

---

## 작업 목표

기존 `wan_acc` 애플리케이션은 Linux socket API를 기반으로 동작했습니다.

본 작업의 목표는 WAN 측 통신 경로를 F-Stack/DPDK 환경에서 동작하도록 변경하고, HA/TCP 관련 기능을 포함한 WAN Accelerator를 실행할 수 있도록 수정하는 것이었습니다.

또한 WAN 연결 흐름, Worker 구조, 빌드 설정, 성능 측정, HA/TCP/F-Stack 통합 과정에서 발생한 문제에 대한 디버깅도 함께 수행했습니다.

---

## 저장소 역할

이 저장소와 수정된 F-Stack 저장소는 역할이 다릅니다.

### `DaEun2Lee/hatcp`

이 저장소에는 HA/TCP 측 WAN Accelerator 개발 내용이 포함됩니다.

주요 내용:

- WAN Accelerator 소스 수정
- Front Worker / Back Worker 구조 분리
- 비동기 WAN 연결 처리
- `apps/wan_acc` 내 F-Stack 호환 코드
- `wrkwrk` 수정
- 실험 스크립트
- 성능 분석 문서
- 디버깅 기록

### `DaEun2Lee/f-stack`

HA/TCP를 실제 F-Stack에 포팅한 코드는 별도 저장소에서 관리합니다.

https://github.com/DaEun2Lee/f-stack

F-Stack 기반 HA/TCP WAN Accelerator를 사용할 때는 `integrated` 브랜치를 사용합니다.

해당 저장소에는 HA/TCP TCP migration, SMCP, socket/TCP stack 수정, F-Stack 환경에 맞춘 WAN Accelerator 코드가 포함되어 있습니다.

---

## 브랜치 구조

### `master`

- 원본 HA/TCP 기준 코드를 보존하는 브랜치입니다.
- upstream HA/TCP 저장소 계보를 유지합니다.
- `rcslab/hatcp`와 가까운 상태를 유지하기 위한 기준 브랜치입니다.

### `main`

- 이 저장소의 기본 브랜치입니다.
- 실험에 사용한 수정 HA/TCP/WAN Accelerator 코드가 포함되어 있습니다.
- 소스 수정, 실험 스크립트, 문서, 성능 분석 자료가 포함됩니다.

원본 HA/TCP 기준 코드가 필요한 경우가 아니라면 `main`을 사용합니다.

---

## 주요 작업 내용

이번 작업에서 수행한 주요 내용은 다음과 같습니다.

- WAN 측 socket 처리를 F-Stack 환경에 맞게 수정
- MS-to-ES WAN 연결 구조 수정
- Front Worker / Back Worker 역할 분리
- SOMIGRATION, SMCP, F-Stack 관련 빌드 설정 추가
- MS-to-ES 비동기 WAN connect 처리
- ES WAN listener를 F-Stack epoll에 등록
- non-blocking WAN 연결의 TX queue 처리 보강
- stream/fd 정리 및 잘못된 stream 접근 방지
- 실험 트래픽 생성을 위한 `wrkwrk` 수정
- `wrkwrk`, `perf`, FlameGraph를 이용한 성능 측정
- MS와 ES의 CPU 병목 분석

HA/TCP TCP migration 및 FreeBSD TCP stack 자체의 F-Stack 이식은 별도의 F-Stack 저장소에서 관리합니다.

---

## 실험 구조

```text
Client / wrkwrk
      |
      v
MS WAN Accelerator
      |
      v
F-Stack / DPDK WAN 구간
      |
      v
ES WAN Accelerator
      |
      v
Remote Application Server
```

MS와 ES 사이의 WAN 구간이 F-Stack 통합의 주요 대상입니다.

---

## 주요 수정 파일

### WAN Accelerator

- `apps/wan_acc/server.cc`
  - MS/ES 연결 처리
  - 비동기 WAN connect
  - F-Stack epoll 이벤트 처리
  - WAN path 상태 관리

- `apps/wan_acc/worker.cc`
  - Front Worker / Back Worker 처리 흐름
  - chunk/reference 처리
  - TX/RX 경로 처리

- `apps/wan_acc/netutils.cc`
  - socket abstraction
  - F-Stack API 처리
  - WAN 측 socket 처리

- `apps/wan_acc/netutils.h`
  - WAN 네트워크 관련 선언 및 호환 정의

- `apps/wan_acc/main.cc`
  - 실행 옵션
  - 애플리케이션 초기화

- `apps/wan_acc/acc.cc`
  - WAN Accelerator 데이터 처리

- `apps/wan_acc/acc.h`
  - accelerator 관련 선언

- `apps/wan_acc/hatcp_compat.h`
  - HA/TCP/F-Stack 호환 정의

### 빌드 파일

- `apps/wan_acc/makefile`
- `apps/wan_acc/Makefile`
- `apps/wan_acc/makefile_fstack`
- `apps/wan_acc/makefile_somig`

위 파일들은 실험 단계와 HA/TCP/F-Stack 통합 방식에 따라 사용한 빌드 설정을 포함합니다.

### Workload Generator

- `apps/wrkwrk/wrkwrk.cc`
- `apps/wrkwrk/netutil.cc`
- `apps/wrkwrk/utils.h`

WAN Accelerator 트래픽 생성 및 측정을 위해 수정한 파일입니다.

---

## Front Worker / Back Worker 구조

WAN Accelerator는 단순히 데이터를 전달하는 프로그램이 아닙니다.

전체 처리 흐름은 다음과 같이 볼 수 있습니다.

```text
데이터 수신
  -> Front Worker
  -> chunk 분할
  -> 중복 검사
  -> 신규/reference chunk 구분
  -> Back Worker
  -> 압축/해제 또는 복원
  -> 출력 buffer 생성
  -> 전송
```

### Front Worker

Front Worker는 입력 측 처리를 담당합니다.

- Client 또는 WAN 측 데이터 수신
- 입력 buffer 관리
- chunk 분할
- chunk hash 계산
- 중복 chunk 확인
- 신규 chunk와 reference chunk 구분
- Back Worker로 작업 전달

### Back Worker

Back Worker는 출력 측 처리를 담당합니다.

- Front Worker에서 chunk 작업 수신
- 신규 chunk 처리
- reference chunk 복원
- 압축/압축 해제
- 출력 buffer 생성
- WAN 또는 Backend Application 방향으로 전송

상세 문서:

[docs/02_front_back_worker.md](docs/02_front_back_worker.md)

---

## 빌드 및 실행

상세 빌드 및 실행 방법:

[docs/01_build_and_execution.md](docs/01_build_and_execution.md)

HA/TCP 측 WAN Accelerator의 대표 빌드 명령은 다음과 같습니다.

```bash
cd ~/kwon/hatcp/apps/wan_acc
make -f makefile_somig clean
make -f makefile_somig -j$(nproc)
```

정상적으로 빌드되면 `wanacc` 실행 파일이 생성됩니다.

> **주의**
>
> 이 저장소는 HA/TCP 측 개발 및 실험 환경을 정리한 저장소입니다.
> 최종 F-Stack 통합 소스는 아래 저장소의 `integrated` 브랜치를 사용하세요.
>
> https://github.com/DaEun2Lee/f-stack

---

## 대표 실행 방법

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

IP 주소와 테스트 파일 경로는 현재 실험 환경에 맞게 변경해야 합니다.

---

## 주요 실행 옵션

- `-M ms`
  - MS WAN Accelerator 모드

- `-M es`
  - ES WAN Accelerator 모드

- `-S`
  - local IP address

- `-p`
  - local listening port

- `-E`
  - remote WAN Accelerator 또는 backend IP address

- `-e`
  - remote/backend port

- `-f`
  - Front Worker 개수

- `-b`
  - Back Worker 개수

### 기능 비활성화 옵션

`-o` 옵션은 bit flag입니다.

- `-o 0`
  - 중복 제거 사용
  - 압축 사용

- `-o 1`
  - 중복 제거 미사용
  - 압축 사용

- `-o 2`
  - 중복 제거 사용
  - 압축 미사용

- `-o 3`
  - 중복 제거 미사용
  - 압축 미사용

bit 정의:

```text
0x1 = NO_DEDUP
0x2 = NO_COMPRESSION
```

---

## 대표 성능 결과

대표 실험 조건:

- 전송 파일 크기: 10 MiB
- Connection 수: 8
- Front Worker: 2개
- Back Worker: 3개
- 측정 시간: 30초

대표 측정 결과:

- 평균 지연시간: 약 135.558 ms
- 평균 처리량: 약 333.742 MB/s
- 환산 처리량: 약 2.734 Gbps
- 평균 요청 처리량: 약 32.586 requests/s
- TCP 재전송: 0회

위 값은 특정 실험 환경에서 측정한 대표 결과이며 다른 시스템에서 동일한 성능을 보장하지 않습니다.

성능은 CPU allocation, NIC/DPDK 설정, Worker 수, workload, 서버 환경에 따라 달라질 수 있습니다.

상세 결과:

[docs/04_performance_and_flamegraph.md](docs/04_performance_and_flamegraph.md)

---

## 성능 분석

CPU 병목을 확인하기 위해 `perf`와 FlameGraph를 사용했습니다.

대표적으로 다음과 같은 특성이 확인되었습니다.

### MS

CPU 사용 비중이 크게 나타난 영역:

- memory initialization
- `rte_rdtsc`
- F-Stack `main_loop`
- ring 관련 처리

### ES

CPU 사용 비중이 크게 나타난 영역:

- `rbkp_chunker`
- chunk 처리
- 중복 제거 관련 처리
- reference 복원

이를 통해 MS와 ES에서 서로 다른 CPU 병목이 발생할 수 있음을 확인했습니다.

상세 분석:

[docs/04_performance_and_flamegraph.md](docs/04_performance_and_flamegraph.md)

---

## 디버깅 및 통합 기록

개발 과정에서 발생한 주요 문제는 다음과 같습니다.

- HA/TCP와 F-Stack 사이의 FreeBSD 버전 차이
- HA/TCP FreeBSD 전체 소스를 F-Stack에 그대로 덮어쓸 수 없는 문제
- WAN 경로에 Linux socket 처리가 남아 있던 문제
- non-blocking `connect()` / `EINPROGRESS` 처리
- `EPOLLOUT` 이벤트 처리 순서
- WAN 연결 완료 전 TX queue 처리
- ES WAN listener 등록 문제
- stale stream/fd 정리
- Makefile 및 link option 문제
- hugepage / DPDK 설정
- perf / FlameGraph 분석

FreeBSD 전체 소스를 교체하는 대신 HA/TCP 관련 핵심 기능을 F-Stack/FreeBSD stack에 선별적으로 이식하는 방식으로 변경했습니다.

상세 디버깅 기록:

[docs/03_debugging_history.md](docs/03_debugging_history.md)

---

## 문서 목록

- [빌드 및 실행 방법](docs/01_build_and_execution.md)
- [Front Worker / Back Worker 구조](docs/02_front_back_worker.md)
- [디버깅 및 문제 해결 기록](docs/03_debugging_history.md)
- [성능 측정 및 perf/FlameGraph 분석](docs/04_performance_and_flamegraph.md)
- [2026년 7월 작업 일지](timeline/2026-07-01_to_07-30.md)

---

## 관련 저장소

### 원본 HA/TCP

https://github.com/rcslab/hatcp

### 수정 HA/TCP / WAN Accelerator

https://github.com/DaEun2Lee/hatcp

### HA/TCP가 통합된 F-Stack

https://github.com/DaEun2Lee/f-stack/tree/integrated

---

## 현재 상태

2026-08-13 기준:

- `main`에는 수정된 HA/TCP/WAN Accelerator 구현이 포함되어 있습니다.
- `master`는 원본 HA/TCP 기준 코드를 보존합니다.
- `main`을 이 저장소의 기본 브랜치로 사용합니다.
- HA/TCP를 실제 F-Stack에 통합한 코드는 `DaEun2Lee/f-stack`의 `integrated` 브랜치에서 관리합니다.
- 빌드, 디버깅, Worker 구조, 성능 분석 문서가 저장소에 포함되어 있습니다.

---

## 주의 사항

이 저장소는 연구 및 실험용 코드입니다.

다른 서버에서 환경을 재현하기 전에 다음 항목을 반드시 확인해야 합니다.

- NIC 설정
- DPDK binding
- hugepage 설정
- F-Stack 설정
- CPU/lcore allocation
- IP 주소 및 포트
- Worker 개수
- 빌드 옵션

기존 실험 서버의 설정값을 다른 환경에서 그대로 사용할 수 있다고 가정하면 안 됩니다.
