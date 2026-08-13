# 빌드 및 실행 방법

English version: [01_build_and_execution.md](01_build_and_execution.md)

이 문서는 HA/TCP 기반 WAN Accelerator를 F-Stack 환경에서 빌드하고 실행한 방법을 정리한 문서이다.

## 1. 실험 구조

```text
Client / wrkwrk
-> Node 1: MS WAN Accelerator
-> F-Stack / DPDK WAN 구간
-> Node 2: ES WAN Accelerator
-> Remote Application Server
```

## 2. 빌드 방법

WAN Accelerator는 `apps/wan_acc` 폴더에서 빌드한다.

```bash
cd ~/kwon/hatcp/apps/wan_acc
make -f makefile_somig clean
make -f makefile_somig -j$(nproc)
```

빌드가 성공하면 `wanacc` 실행 파일이 생성된다.

## 3. ES 측 실행

Node 2에서 ES WAN Accelerator를 먼저 실행한다.

```bash
cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M es -S 192.168.1.2 -p 3301 -E 10.20.24.171 -e 3302 -f 2 -b 3
```

## 4. MS 측 실행

Node 1에서 MS WAN Accelerator를 실행한다.

```bash
cd ~/kwon/hatcp/apps/wan_acc
./wanacc -M ms -S 10.20.17.225 -p 3300 -E 192.168.1.2 -e 3301 -f 2 -b 3
```

## 5. Client 측 측정

Client에서는 `wrkwrk`를 이용해 MS로 요청을 보낸다.

```bash
cd ~/kwon/hatcp/apps/wrkwrk
./wrkwrk -m wanacc -s 10.20.17.165 -p 3300 -T 1 -c 8 -d 30 -f http://10.20.24.171:3302/Bible_10MiB.txt
```

## 6. 주요 옵션

- `-M ms` : MS 모드 실행
- `-M es` : ES 모드 실행
- `-S` : local IP
- `-p` : local port
- `-E` : remote 또는 backend IP
- `-e` : remote 또는 backend port
- `-f` : Front Worker 개수
- `-b` : Back Worker 개수

## 7. 기능 비활성화 옵션

`-o` 옵션은 중복 제거와 압축 기능을 끄기 위한 bit flag이다.

- `-o 0` : 중복 제거 사용, 압축 사용
- `-o 1` : 중복 제거 미사용, 압축 사용
- `-o 2` : 중복 제거 사용, 압축 미사용
- `-o 3` : 중복 제거 미사용, 압축 미사용

bit flag 의미는 다음과 같다.

```text
0x1 : NO_DEDUP
0x2 : NO_COMPRESSION
```

## 8. 대표 측정 결과

```text
평균 지연시간    : 약 135.558 ms
평균 처리량      : 약 333.742 MB/s = 약 2.734 Gbps
평균 요청 처리량 : 약 32.586 requests/s
TCP 재전송       : 0회
```

## 9. 주의 사항

F-Stack 기반 실행을 위해서는 hugepage, DPDK NIC 설정, F-Stack 설정 파일이 올바르게 준비되어 있어야 한다.

ES 측 WAN listener는 Linux epoll이 아니라 F-Stack epoll에 등록되어야 MS에서 들어오는 WAN 연결을 받을 수 있다.

> **Note**
>
> 이 문서는 HA/TCP 측 개발 및 실험 환경의 빌드/실행 절차를 기록한 것이다.
> 최종 F-Stack 통합본은 `DaEun2Lee/f-stack` 저장소의 `integrated` 브랜치를 참고한다.
