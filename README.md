# HA/TCP WAN Accelerator

이 저장소는 HA/TCP 기반 WAN Accelerator를 F-Stack 환경에서 동작하도록 수정하고, 실험 및 성능 분석을 진행한 내용을 정리한 저장소입니다.

## 작업 기간

2026.07.01 ~ 2026.07.30

## 작업 목표

기존 wan_acc 애플리케이션은 Linux socket API 기반으로 동작했습니다.

본 작업에서는 WAN 구간 통신을 F-Stack API 기반으로 변경하여, DPDK/F-Stack 환경에서 HA/TCP 관련 기능을 포함한 WAN Accelerator를 실행할 수 있도록 수정했습니다.

## 주요 작업 내용

- HA/TCP의 TCP migration 관련 코드를 F-Stack/FreeBSD TCP stack에 이식
- wan_acc의 WAN 측 socket 처리를 Linux socket API에서 F-Stack API로 변경
- MS와 ES 사이의 WAN 연결 구조 수정
- Front Worker와 Back Worker 역할 분리
- SOMIGRATION, SMCP, F-Stack link 관련 빌드 설정 추가
- MS-to-ES 비동기 WAN connect 문제 해결
- ES WAN listener를 F-Stack epoll에 등록하도록 수정
- wrkwrk, perf, FlameGraph를 이용한 성능 측정 및 병목 분석

## 전체 구조

Client / wrkwrk
→ MS WAN Accelerator
→ F-Stack / DPDK WAN 구간
→ ES WAN Accelerator
→ Remote Application Server

## 주요 수정 파일

- apps/wan_acc/server.cc : MS/ES 연결 처리, F-Stack epoll, WAN path 처리
- apps/wan_acc/worker.cc : Front/Back Worker 데이터 처리 흐름
- apps/wan_acc/netutils.cc : socket abstraction 및 F-Stack API 처리
- apps/wan_acc/main.cc : 실행 옵션 및 초기화 처리
- apps/wan_acc/makefile_somig : SOMIGRATION/F-Stack 빌드 설정
- apps/wan_acc/hatcp_compat.h : HA/TCP 호환 정의
- apps/wrkwrk/wrkwrk.cc : 측정 도구 수정

## 대표 성능 결과

- 평균 지연시간: 약 135.558 ms
- 평균 처리량: 약 333.742 MB/s = 약 2.734 Gbps
- 평균 요청 처리량: 약 32.586 requests/s
- TCP 재전송: 0회

## 현재 상태

2026년 7월 30일 기준, 수정된 WAN Accelerator 소스코드는 GitHub 저장소에 업로드되었습니다.

## 문서 목록

자세한 정리 문서는 아래 파일에서 확인할 수 있습니다.

- [빌드 및 실행 방법](docs/01_build_and_execution.md)
- [Front Worker / Back Worker 구조](docs/02_front_back_worker.md)
- [디버깅 및 문제 해결 기록](docs/03_debugging_history.md)
- [성능 측정 및 perf/FlameGraph 분석](docs/04_performance_and_flamegraph.md)
- [2026년 7월 작업 일지](timeline/2026-07-01_to_07-30.md)
