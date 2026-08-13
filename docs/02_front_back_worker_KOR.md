# Front Worker / Back Worker 구조

이 문서는 WAN Accelerator에서 Front Worker와 Back Worker의 역할을 구분하여 정리한 문서이다.

## 1. Worker 역할을 구분한 이유

WAN Accelerator는 단순히 데이터를 전달하는 프로그램이 아니라, 수신한 데이터를 chunk 단위로 나누고, 중복 제거와 압축 처리를 수행한 뒤 다시 상대 노드로 전달한다.

이 과정에서 모든 작업을 하나의 worker가 처리하면 CPU 사용률이 한쪽으로 몰리고, 데이터 수신과 처리, 전송이 서로 영향을 줄 수 있다.

따라서 본 작업에서는 worker 역할을 Front Worker와 Back Worker로 구분하여 데이터 입력 경로와 처리/출력 경로를 나누었다.

## 2. 전체 처리 흐름

Client 또는 WAN 측에서 데이터 수신
-> Front Worker가 입력 데이터 처리
-> chunk 분할
-> 중복 chunk 검색
-> 신규 chunk와 reference chunk 구분
-> Back Worker로 작업 전달
-> Back Worker가 압축/복원/출력 버퍼 생성
-> WAN 또는 Application 방향으로 전송

## 3. Front Worker 역할

Front Worker는 입력 경로에서 데이터를 먼저 받는 역할을 한다.

주요 역할은 다음과 같다.

- Client 또는 WAN 측에서 데이터 수신
- 입력 stream의 buffer 관리
- 수신 데이터를 chunk 단위로 분할
- chunk hash 계산
- 중복 chunk 여부 확인
- 신규 chunk와 reference chunk 구분
- 처리할 작업을 Back Worker로 전달

즉 Front Worker는 데이터를 받아서 "어떤 데이터인지 판단하는 앞단 처리"를 담당한다.

## 4. Back Worker 역할

Back Worker는 Front Worker에서 넘겨받은 작업을 실제 출력 가능한 형태로 만드는 역할을 한다.

주요 역할은 다음과 같다.

- Front Worker에서 전달된 chunk 작업 수신
- 신규 chunk 처리
- reference chunk 복원
- 압축 또는 압축 해제 처리
- 출력 buffer 생성
- 상대 WAN Accelerator 또는 Application으로 데이터 전송

즉 Back Worker는 Front Worker가 분류한 데이터를 바탕으로 "실제 전송 가능한 데이터로 만드는 뒷단 처리"를 담당한다.

## 5. MS 측 Worker 흐름

MS는 Client로부터 요청을 받고 ES로 데이터를 전달한다.

MS 측 흐름은 다음과 같이 볼 수 있다.

Client 요청 수신
-> MS Front Worker
-> chunk 분할 및 중복 검사
-> MS Back Worker
-> WAN 측 F-Stack path
-> ES로 전송

MS에서는 Client 요청을 WAN 구간으로 넘기는 과정에서 F-Stack API 기반 송신 처리가 중요하다.

## 6. ES 측 Worker 흐름

ES는 MS에서 전달된 WAN 데이터를 받고 Backend Application으로 전달한다.

ES 측 흐름은 다음과 같이 볼 수 있다.

MS에서 WAN 데이터 수신
-> ES Front Worker
-> chunk/reference 해석
-> ES Back Worker
-> 원본 stream 복원
-> Backend Application으로 전달

ES에서는 전달받은 chunk/reference를 해석하고 원본 데이터를 복원하는 작업이 중요하다.

## 7. CPU 역할 분리

실험에서는 전체 6개 코어 중 일부를 F-Stack loop와 worker 처리에 나누어 사용했다.

대표적인 worker 설정은 다음과 같다.

Front Worker: 2개
Back Worker : 3개
Total Worker: 5개

이 구조에서는 F-Stack network loop와 Front/Back Worker가 서로 다른 CPU 역할을 갖도록 구성하는 것이 중요하다.

## 8. 정리

Front Worker는 데이터를 받는 앞단에서 chunk 분할과 중복 여부 판단을 담당한다.

Back Worker는 판단된 데이터를 바탕으로 압축, 복원, 출력 buffer 생성, 전송 처리를 담당한다.

따라서 WAN Accelerator의 전체 흐름은 단순 송수신이 아니라, Front Worker의 입력 분석과 Back Worker의 출력 생성이 연결된 구조로 이해해야 한다.
