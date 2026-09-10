# jhshin_Server

**Windows IOCP 기반 C++ 게임 서버 네트워크 계층 프로젝트**

TCP 스트림의 패킷 경계를 복원하고, 다중 워커 스레드 환경에서 세션 수명을 관리하며, **룸 단위 JobQueue를 통해 공유 락 없이 게임 상태를 처리하는 서버 구조**를 직접 구현했습니다.

외부 네트워크 라이브러리 없이 **Winsock2 / MS IOCP**를 직접 사용했으며, 구현뿐 아니라 테스트와 부하 측정을 통해 설계의 장단점을 검증했습니다.

---

## 프로젝트 한눈에 보기

| 구분 | 내용 |
|---|---|
| Language | C++20 |
| Platform | Windows x64 |
| Network | Winsock2 / MS IOCP |
| IDE | Visual Studio 2022 (v143) |
| Dependency | 없음 |
| 핵심 구조 | IOCP · Session Pool · Object Pool · JobQueue · Room · Broadcast |

### 핵심 설계

```text
Client
  │
  ▼
IOCP Worker
  │
  ▼
Packet Handler
  │
  ├─ 일반 요청 ──────────────▶ 즉시 처리
  │
  └─ 룸 상태 요청
          │
          ▼
      JobQueue
          │
          ▼
      Room Thread
          │
          ▼
      Room State
```

**설계의 핵심은 락을 추가하는 대신 데이터의 소유권과 실행 단위를 분리하는 것**입니다.

- 세션 수명 → `shared_ptr` 기반 참조 계수
- 룸 상태 → 룸별 단일 스레드 소유
- 송신 순서 → 세션별 송신 큐 + 게시 플래그
- 브로드캐스트 데이터 → `SendChunk` 공유

---

## 성능 요약

측정은 **Release x64 / C++20 / 루프백** 환경에서 수행했습니다.

| 경로 | 결과 |
|---|---:|
| ECHO | **왕복 661,169 /초** · p50 0.499 ms |
| Broadcast | **약 104,000 msg/초**까지 전달률 100% · p50 1.07 ms |

### 확인한 병목 특성

브로드캐스트는 초당 메시지 수보다 **룸 크기가 먼저 병목**이 되는 특성을 확인했습니다.

| 구성 | 팬아웃 | 유출/초 | p50 |
|---|---:|---:|---:|
| 연결 50 × rate 200 | 약 11 | 120,994 | **0.37 ms** |
| 연결 100 × rate 50 | 약 24 | 121,676 | **244 ms** |

`Room::BroadCast`가 룸 멤버를 한 스레드에서 동기로 순회하기 때문에, 룸이 커질수록 뒤쪽 멤버의 지연이 증가합니다. **단일 소유권 구조의 장점과 한계를 모두 측정으로 확인한 결과**입니다.

> 상세 측정 과정과 가설 검증은 [`TestClient/RESULTS.md`](TestClient/RESULTS.md)에 기록했습니다.

---

## 목차

- [1. 프로젝트 목표](#1-프로젝트-목표)
- [2. 전체 구조](#2-전체-구조)
- [3. 핵심 설계](#3-핵심-설계)
  - [3-1. TCP 패킷 경계 복원](#3-1-tcp-패킷-경계-복원)
  - [3-2. 세션 수명 관리](#3-2-세션-수명-관리)
  - [3-3. 송신 직렬화와 배칭](#3-3-송신-직렬화와-배칭)
  - [3-4. SendBuffer 공유](#3-4-sendbuffer-공유)
  - [3-5. 룸 단위 JobQueue](#3-5-룸-단위-jobqueue)
- [4. 추가 설계 판단](#4-추가-설계-판단)
- [5. 검증](#5-검증)
- [6. 부하 측정과 병목 분석](#6-부하-측정과-병목-분석)
- [7. 테스트 하니스](#7-테스트-하니스)
- [8. 현재 상태](#8-현재-상태)
- [9. 빌드 및 실행](#9-빌드-및-실행)
- [10. 프로젝트 문서](#10-프로젝트-문서)

---

# 1. 프로젝트 목표

### 구현 목표

- IOCP 기반 서버 네트워크 계층 직접 구현
- TCP 스트림에서 패킷 경계 복원
- 비동기 I/O 완료 시점까지 안전한 세션 수명 관리
- 룸 단위 게임 상태 처리 및 브로드캐스트
- 동시성 문제를 **소유권과 실행 단위 분리**로 해결
- 부하 테스트를 통한 병목 가설 검증

---

# 2. 전체 구조

```text
main
├─ ServiceManager
│  ├─ IOCP              워커 풀 / 완료 라우팅
│  ├─ SendBuffer 풀     40KB 청크
│  └─ 세션 맵
│
├─ ListenManager        리슨 소켓 · AcceptEx 사전 게시
│
├─ SessionManager
│  └─ 세션 풀 + Accept 대기 큐
│      └─ SessionData   소켓 · RecvBuffer · 송신 큐
│           ├─ RecvObject (OVERLAPPED)
│           └─ SendObject (OVERLAPPED)
│
└─ RoomManager
   └─ Room × N          룸마다 전용 스레드 1개
       └─ JobQueue
          ├─ Enter
          ├─ Leave
          └─ Broadcast
```

룸 상태 변경 요청은 IOCP 워커가 직접 처리하지 않고 해당 룸의 `JobQueue`로 전달합니다.
따라서 하나의 룸 상태는 해당 룸의 스레드만 접근하며, `Room` 내부에 별도의 공유 락을 두지 않습니다.

---

# 3. 핵심 설계

## 3-1. TCP 패킷 경계 복원

TCP는 메시지 경계를 보장하지 않기 때문에 IOCP 완료 통지 1회가 패킷 1개를 의미하지 않습니다.

한 번의 수신에서 패킷이 분할되거나 여러 개가 합쳐질 수 있으므로 `RecvBuffer`에 데이터를 누적하고, 헤더의 크기를 기준으로 **완성된 패킷만 소비**하도록 구현했습니다.

```cpp
int divideByte = transferByte;
RecvBuffer& recvBuffer = m_Recv.GetRecvBuffer();

while (recvBuffer.DivideBuffer(divideByte))
{
    divideByte = 0;

    const int size = recvBuffer.GetPacketID()._size + PacketID_SIZE;

    // packet processing

    recvBuffer.SetReadPos(size);
}
```

또한 클라이언트가 비정상적으로 큰 패킷 크기를 전달해 버퍼를 영구 점유하지 않도록 **패킷 크기 상한 검증**을 적용했습니다.

---

## 3-2. 세션 수명 관리

IOCP에 `WSARecv` / `WSASend`를 게시하면 커널이 `OVERLAPPED`와 버퍼 주소를 계속 참조합니다.

세션을 풀로 즉시 반납하면 늦게 도착한 완료 통지가 재사용된 세션을 잘못 참조할 수 있기 때문에, 세션을 `shared_ptr` 기반 참조 계수로 관리하고 마지막 참조가 해제될 때 custom deleter를 통해 **세션 풀로 반환**하도록 구성했습니다.

```cpp
return SessionDataRef(
    session,
    [](SessionData* p)
    {
        SessionManager::This()->PushSession(p);
    }
);
```

`shared_ptr`은 여기서 단순한 메모리 관리 도구가 아니라 **세션 재사용 시점을 제어하는 장벽**으로 사용됩니다.

```text
게시 중인 IO  → reference +1
세션 맵       → reference +1

모든 참조 해제
      ↓
reference = 0
      ↓
custom deleter
      ↓
session pool 반환
```

`RecvObject`와 `SendObject`가 각각 세션을 참조하므로, 비동기 작업이 남아 있는 동안에는 세션이 재사용되지 않습니다.

---

## 3-3. 송신 직렬화와 배칭

하나의 세션에는 동시에 하나의 `WSASend`만 게시하도록 구성했습니다.

송신 요청은 먼저 큐에 넣고, 게시 중인 송신이 없을 때만 새로운 `WSASend`를 시작합니다.

```text
Send Request
     │
     ▼
 Send Queue
     │
     ├─ Send 진행 중 → Queue에 저장
     │
     └─ Send 없음 → WSASend 게시
                      │
                      ▼
                  완료 통지
                      │
                      ▼
                  다음 Send
```

송신 완료 시 남은 큐를 확인해 다음 전송을 이어가며, 부하가 높을수록 큐에 쌓인 여러 패킷이 하나의 `WSABUF` 배열로 묶입니다.

즉 별도 batching 로직을 추가하기보다 **송신 직렬화 구조 자체에서 자연스럽게 배칭이 발생하도록 설계**했습니다.

---

## 3-4. SendBuffer 공유

브로드캐스트에서는 동일한 데이터를 여러 세션으로 보내야 합니다.

직렬화된 데이터를 40KB `SendBuffer` 청크에 저장하고, 여러 세션은 같은 `SendChunk`를 참조하도록 구성했습니다.

```cpp
struct SendChunk
{
    SendBufferRef m_sendBuffer;
    char*         m_buffer;
    int           m_size;
};
```

```text
직렬화 / 복사
      ↓
     1회
      ↓
같은 SendChunk를 N개 세션이 공유
```

브로드캐스트에서 패킷 데이터를 반복 복사하는 비용을 줄이고, 실제 메모리 수명은 참조 계수로 관리합니다.

---

## 3-5. 룸 단위 JobQueue

룸 상태를 여러 IOCP 워커가 직접 변경하면 `Room` 내부에 공유 락이 필요합니다.

하지만 브로드캐스트는 룸 인원 수에 비례하는 O(N) 작업이므로, 룸 전체 상태를 하나의 락으로 보호하면 경합 비용이 커질 수 있다고 판단했습니다.

대신 룸마다 전용 스레드를 두고, IOCP 워커는 룸 상태 변경 요청을 `JobQueue`에 전달합니다.

```text
IOCP Worker
    │
    ├─ ENTER
    ├─ LEAVE
    └─ BROADCAST
          │
          ▼
       JobQueue
          │
          ▼
      Room Thread
          │
          ▼
       Room State
```

이 구조의 핵심은 **룸 상태의 단일 소유권**입니다.

`Room` 내부에는 상태 보호를 위한 mutex가 필요하지 않고, 동일 룸의 작업은 자연스럽게 순서대로 처리됩니다.

`JobQueue::PopAll()`은 큐를 하나씩 꺼내는 대신 `swap`으로 작업 목록을 가져와 락 획득 횟수를 배치 단위로 줄입니다.

다만 룸 하나의 처리량이 스레드 하나로 제한된다는 대가가 있으며, 실제 부하 측정에서 이 특성이 확인되었습니다.

---

# 4. 추가 설계 판단

### RecvBuffer — 선형 버퍼 + compaction

링 버퍼 대신 선형 버퍼를 사용했습니다.

패킷 파싱에 연속된 메모리가 필요하기 때문에, 패킷이 버퍼 끝을 넘어갈 때 다시 조립해야 하는 링 버퍼의 복잡성을 피하고 **일반적인 패킷 크기에서는 단순한 구조를 우선**했습니다.

### Session Pool — `unique_ptr<T[]>`

`SessionData`에 mutex가 포함되어 있어 `vector<T>` 기반 저장소의 재할당 제약을 피하기 위해 `unique_ptr<T[]>`를 사용했습니다.

한 번 할당한 고정 저장소를 사용해 풀에서 꺼낸 객체의 주소가 재할당으로 바뀌지 않도록 했습니다.

### Accept 대기 큐

세션 풀이 고갈된 상태에서도 `AcceptEx` 요청을 버리지 않고 대기 큐에 보관한 뒤, 세션이 반납되면 다시 게시하도록 구성했습니다.

### AcceptEx 주소 파싱

`GetAcceptExSockaddrs`는 `WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER, ...)`을 이용해 런타임에 함수 포인터를 취득하도록 구성했습니다.

---

# 5. 검증

## 패킷 재조립

| 테스트 | 결과 |
|---|---|
| 헤더/바디 2분할 | 정상 |
| 헤더 분할 | 정상 |
| 바디 분할 | 정상 |
| 1바이트씩 수신 | 정상 |
| 여러 패킷 + 잘린 패킷 | 정상 |
| 랜덤 fuzz 500회 | **9,520 패킷 전부 일치** |
| 비정상 `_size = 60000` | 버퍼 점유 없이 방어 |

## 룸 / 브로드캐스트

16개 연결 기준으로 다음 항목을 검증했습니다.

- ENTER → SERVER_ENTER 응답
- 동일 룸 전체 브로드캐스트
- 송신자 본인 제외
- 다른 룸으로 패킷 유출 없음
- 연속 100회 브로드캐스트 프레이밍 정상

---

# 6. 부하 측정과 병목 분석

성능 측정은 최고 수치를 뽑는 것보다 **병목 가설을 세우고 측정으로 검증하거나 기각하는 방식**으로 진행했습니다.

### 측정 환경

| | ECHO 스윕 | 룸 / 브로드캐스트 |
|---|---|---|
| 논리 프로세서 | 20 (i5-13500) | 6 |
| 빌드 | Release x64 · MSVC v143 · C++20 | 동일 |
| 네트워크 | 루프백 | 루프백 |

**머신이 다르므로 두 측정 집합의 숫자를 직접 비교하지 않았습니다.**

### 주요 결과

| 경로 | 구성 | 결과 |
|---|---|---|
| ECHO | 연결 50 | 왕복 661,169 /초 · p50 0.499 ms |
| Broadcast | 룸 다수 · 팬아웃 약 9 | 유출 약 104,000 /초까지 전달률 100% · p50 1.07 ms |

### 병목 분석

유출량이 비슷해도 룸 크기에 따라 지연이 크게 달라졌습니다.

| 구성 | 팬아웃 | 유출/초 | p50 |
|---|---:|---:|---:|
| 연결 50 × rate 200 | 약 11 | 120,994 | **0.37 ms** |
| 연결 100 × rate 50 | 약 24 | 121,676 | **244 ms** |

브로드캐스트는 룸 멤버를 한 스레드에서 동기로 순회하므로, **초당 메시지 수보다 룸 크기가 먼저 병목**이 됩니다.

### 기각한 가설

| 가설 | 검증 방법 | 결과 |
|---|---|---|
| 전역 SendBuffer 락 | `thread_local` 적용 후 재측정 | 처리량 불변 → 기각 |
| 클라이언트 프로세스 한계 | 부하 클라이언트 2프로세스 분할 | 총량 불변 → 기각 |
| 대역폭 | 패킷 바디 32배 확대 | 왕복/초 불변 → 기각 |

이 과정에서 실제 병목은 **부하 클라이언트 자체**였던 경우도 확인했습니다. 서버 CPU와 클라이언트 CPU를 따로 측정하고 부하 클라이언트를 논블로킹 + 배칭 구조로 다시 구성해 서버의 실제 처리 특성을 확인했습니다.

상세한 측정 과정은 [`TestClient/RESULTS.md`](TestClient/RESULTS.md)의 13개 절에 기록했습니다.

---

# 7. 테스트 하니스

| 파일 | 용도 |
|---|---|
| `TestClient/TestClient.cpp` | 패킷 분할·병합·fuzz 검증 |
| `TestClient/LoadTest.cpp` | 연결당 스레드 기반 부하 테스트 |
| `TestClient/LoadTestFast.cpp` | 논블로킹 + `WSAPoll` 기반 ECHO 부하 |
| `TestClient/LoadTestRoom.cpp` | 룸/브로드캐스트 정확성 및 부하 |

테스트 클라이언트는 서버의 `PacketStruct.h`를 직접 include하여 **프로토콜 정의를 서버와 테스트 코드에서 분리하지 않도록** 했습니다.

---

# 8. 현재 상태

## 구현 완료

- IOCP 워커 풀 및 완료 라우팅
- `AcceptEx` 사전 게시 및 재무장
- TCP 패킷 경계 복원
- 비정상 패킷 크기 방어
- 세션 풀 및 참조 계수 기반 수명 관리
- 세션 풀 고갈 시 대기 큐
- 송신 큐 / 송신 직렬화 / 배칭
- SendBuffer 풀 및 공유
- 패킷 핸들러 / 디스패치
- 룸 + JobQueue
- 룸 단위 브로드캐스트
- 세션 종료 시 룸 정리
- CPU 사용량 모니터링

## 현재 한계

- 부분 전송(`transferByte < 요청량`) 처리
- Graceful Shutdown 완성
- 세션 종료 경로 일원화
- `Job_Leave` 중복 게시 방지

## 개선 예정

- 로깅 시스템 도입
- 설정값 하드코딩 제거
- 룸 배정 정책 개선
- 싱글톤 의존성 정리
- 테스트 프로젝트 빌드 자동화

## 범위에서 제외

DB · 인증 · 재접속 · 프로토콜 버저닝은 이 저장소의 범위에서 제외했습니다.

이 프로젝트는 **네트워크 계층과 룸 처리 구조를 직접 구현하고 검증하는 것**에 집중했습니다.

---

# 9. 빌드 및 실행

Visual Studio 2022에서 다음 솔루션을 x64로 빌드합니다.

```text
jhshin_Server/jhshin_Server.sln
```

기본 포트:

```text
27130
```

### 테스트 클라이언트

**x64 Native Tools Command Prompt**에서 빌드합니다.

```bat
cl /nologo /utf-8 /std:c++20 /O2 /EHsc ^
   /I jhshin_Server ^
   TestClient\LoadTestRoom.cpp ^
   /Fe:LoadTestRoom.exe ws2_32.lib
```

```bat
LoadTestRoom.exe verify
LoadTestRoom.exe load [연결수] [지속초] [초당송신/연결] [스레드수]
```

---

# 10. 프로젝트 문서

- [성능 측정 상세 — TestClient/RESULTS.md](TestClient/RESULTS.md)
- [테스트 클라이언트](TestClient/)

---

## 프로젝트를 통해 확인한 것

이 프로젝트에서 가장 중요하게 본 것은 **“어떻게 구현했는가”보다 “왜 이 구조를 선택했고, 실제 측정 결과가 그 선택을 뒷받침하는가”**였습니다.

세션의 수명은 참조 계수로, 룸 상태는 단일 스레드 소유권으로, 송신 순서는 직렬화 구조로 관리하고, 그 결과의 장점과 한계를 직접 테스트했습니다.
