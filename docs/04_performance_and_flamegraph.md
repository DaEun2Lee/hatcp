# 성능 측정 및 perf/FlameGraph 분석

이 문서는 HA/TCP 기반 WAN Accelerator의 대표 성능 측정 결과와 perf/FlameGraph 분석 내용을 정리한 문서이다.

## 1. 측정 목적

본 실험의 목적은 F-Stack 기반으로 수정한 WAN Accelerator가 MS와 ES 사이에서 정상적으로 동작하는지 확인하고, worker 구조와 중복 제거/압축 처리 과정에서 발생하는 CPU 사용 위치를 분석하는 것이다.

측정 항목은 다음과 같다.

- 평균 지연시간
- 평균 처리량
- 평균 요청 처리량
- TCP 재전송 횟수
- perf 기반 CPU 사용 함수
- FlameGraph 기반 병목 위치

## 2. 대표 실험 환경

대표 실험 환경은 다음과 같다.

Node 1: MS WAN Accelerator
Node 2: ES WAN Accelerator
Client: wrkwrk
Backend: Remote Application Server
WAN 구간: F-Stack / DPDK
전송 파일 크기: 10 MiB
Connection 수: 8
Front Worker: 2개
Back Worker: 3개
측정 시간: 30초

## 3. 대표 실행 명령

ES 측 실행:

cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M es -S 192.168.1.2 -p 3301 -E 10.20.24.171 -e 3302 -f 2 -b 3

MS 측 실행:

cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M ms -S 10.20.17.225 -p 3300 -E 192.168.1.2 -e 3301 -f 2 -b 3

Client 측 측정:

cd ~/kwon/hatcp/apps/wrkwrk
./wrkwrk -m wanacc -s 10.20.17.165 -p 3300 -T 1 -c 8 -d 30 -f http://10.20.24.171:3302/Bible_10MiB.txt

## 4. 대표 측정 결과

대표 측정 결과는 다음과 같다.

평균 지연시간    : 약 135.558 ms
평균 처리량      : 약 333.742 MB/s = 약 2.734 Gbps
평균 요청 처리량 : 약 32.586 requests/s
TCP 재전송       : 0회

이 결과는 MS와 ES 사이의 WAN Accelerator 경로가 정상적으로 연결되었고, wrkwrk를 통해 10 MiB 파일 요청을 처리할 수 있음을 보여준다.

## 5. 이전 측정 결과와 비교

이전 측정에서는 다음과 같은 결과도 확인했다.

평균 지연시간    : 약 180.9 ms
평균 처리량      : 약 305.9 MB/s = 약 2.45 Gbps
평균 요청 처리량 : 약 29.2 requests/s
TCP 재전송       : 0회

최신 측정에서는 평균 지연시간이 감소하고, 평균 처리량과 요청 처리량이 증가했다.

## 6. perf 분석 목적

perf는 프로그램 실행 중 CPU가 어느 함수에서 시간을 많이 사용하는지 확인하기 위한 성능 분석 도구이다.

본 실험에서는 MS와 ES에서 각각 perf를 사용하여 WAN Accelerator 실행 중 CPU 사용 비중이 큰 함수를 확인했다.

## 7. MS 측 perf 결과

MS 측에서 주요하게 나타난 함수는 다음과 같다.

__memset_avx2_erms : 약 19~20%
rte_rdtsc          : 약 15%
main_loop          : 약 11~13%
ring 관련 처리     : 약 7~9%

MS에서는 메모리 초기화, F-Stack main loop, DPDK 시간 측정, worker 간 ring 전달 비용이 크게 나타났다.

이는 MS가 Client 요청을 받아 WAN 방향으로 데이터를 넘기면서 F-Stack loop와 worker 전달 구조의 영향을 크게 받는다는 것을 의미한다.

## 8. ES 측 perf 결과

ES 측에서는 rbkp_chunker 함수의 self time 비중이 크게 나타났다.

rbkp_chunker self time : 약 40.6~41%

이는 ES 측에서 chunk 분할 및 중복 제거 관련 처리가 큰 CPU 비중을 차지한다는 것을 의미한다.

따라서 ES에서는 단순 송수신보다 chunk/reference 해석과 복원 과정이 중요한 병목 후보로 볼 수 있다.

## 9. FlameGraph 분석

FlameGraph는 perf 결과를 시각화하여 어떤 함수 호출 경로에서 CPU 시간이 많이 사용되는지 확인하기 위한 도구이다.

본 실험에서는 다음과 같은 산출물을 생성했다.

MS:
- ms_out.perf
- ms_out.folded
- ms_flamegraph.svg

ES:
- es_out.perf
- es_out.folded
- es_flamegraph.svg

FlameGraph를 통해 MS와 ES의 병목 위치가 서로 다르게 나타나는 것을 확인했다.

MS는 F-Stack loop, 메모리 처리, ring 전달 비용이 중요하게 나타났고, ES는 rbkp_chunker를 중심으로 chunk 처리 비용이 크게 나타났다.

## 10. 분석 정리

MS 측 병목 후보:
- F-Stack main loop
- DPDK 시간 측정
- worker 간 ring 전달
- 메모리 초기화 및 buffer 처리

ES 측 병목 후보:
- rbkp_chunker
- chunk 분할
- 중복 제거 관련 처리
- reference 복원 처리

따라서 WAN Accelerator의 성능을 높이기 위해서는 단순히 worker 수를 늘리는 것만으로는 부족하며, MS와 ES에서 각각 다른 병목을 고려해야 한다.

## 11. 결론

본 실험을 통해 F-Stack 기반 WAN Accelerator가 정상적으로 동작함을 확인했고, wrkwrk를 이용해 처리량과 지연시간을 측정했다.

또한 perf와 FlameGraph를 통해 MS와 ES의 CPU 사용 특성이 다르다는 것을 확인했다.

MS는 F-Stack loop와 worker 전달 비용의 영향을 많이 받았고, ES는 chunking 및 중복 제거 처리 비용의 영향을 크게 받았다.
