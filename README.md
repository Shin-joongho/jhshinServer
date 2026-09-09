# jhshin_Server

Windows IOCP 기반 게임 서버 네트워크 계층과 룸 브로드캐스트.

TCP 스트림에서 패킷 경계를 복원하고, 다중 워커 스레드 환경에서 세션 수명을 안전하게 관리하며, 룸 단위 브로드캐스트를 공유 락 없이 처리하는 것을 목표로 합니다. 외부 네트워크 라이브러리 없이 Winsock2 위에서 직접 구현했습니다.

설계 근거는 아래 [설계 판단](#설계-판단), 성능 측정과 그 과정에서 기각한 가설들은 [`TestClient/RESULTS.md`](TestClient/RESULTS.md)에 있습니다.

- 언어/환경: C++20, Visual Studio 2022 (v143), x64
- 의존성: 없음 (Winsock2 / mswsock)

---

## 구조

```
main
 ├ ServiceManager ── 서비스용 IOCP (워커 N)
 │   │                세션 맵, SendBuffer 풀
 │   │
 │   ├ ListenManager ── 리슨 소켓, AcceptEx 사전 게시
 │   │
 │   └ SessionManager ── 세션 풀, 대기 큐
 │         └ SessionData ── 소켓, RecvBuffer, 송신 큐
 │               ├ RecvObject (OVERLAPPED)
 │               └ SendObject (OVERLAPPED)
 │
 └ RoomManager ───── 룸 N개, 룸마다 전용 스레드 1개
       └ Room ────── 입장자 맵, 브로드캐스트
             └ JobQueue ── JobObject 큐 (Enter / Leave / Broadcast)
```

| 파일 | 역할 |
|---|---|
| `IOCP.*` | 완료 포트, 워커 스레드, 완료 라우팅 (`IOCPObject::Execute`) |
| `ListenManager.*` | 리슨 소켓 소유, `AcceptEx` 사전 게시 및 재무장 |
| `ServiceManager.*` | 서비스 IOCP, 접속 세션 관리, 송신 버퍼 직렬화 |
| `SessionManager.*` | 세션 풀, 풀 고갈 시 대기 큐 |
| `SessionData.*` | 한 연결의 상태. 수신 처리, 송신 큐 |
| `Buffer.*` | `RecvBuffer`(수신 누적/조립), `SendBuffer`(송신 청크) |
| `ObjectPool.h` | 고정 크기 객체 풀 |
| `PacketStruct.h` | 프로토콜 정의. X-매크로로 `PacketType` enum 과 핸들러 등록을 한 목록에서 생성 |
| `PacketHandler.*` | 함수 포인터 배열 기반 디스패치 |
| `RoomManager.*` | 룸 소유, 룸별 스레드 기동, 잡 라우팅 |
| `Room.*` | 한 룸의 상태. 입장자 맵, 브로드캐스트 |
| `JobQueue.*` | 룸 하나의 잡 큐 (mutex + condition_variable) |
| `JobObject.*` | 잡 기반 클래스와 `Job_Enter` / `Job_Leave` / `Job_Broadcast` |

`IOCPObject`가 `OVERLAPPED`를 상속하고 가상 `Execute()`를 가집니다. 워커는 `GetQueuedCompletionStatus`로 받은 `LPOVERLAPPED`를 `IOCPObject*`로 캐스팅해 `Execute()`만 호출하면 되므로, IO 종류가 늘어도 워커 코드는 바뀌지 않습니다.

수신된 패킷은 IOCP 워커에서 `PacketHandler::Dispatch`로 넘어갑니다. 룸 상태를 건드리는 요청(입장/퇴장/브로드캐스트)은 워커가 직접 처리하지 않고 잡으로 만들어 해당 룸의 큐에 넣고, 룸 스레드가 꺼내서 실행합니다. **룸 상태는 그 룸의 스레드만 만지므로 `Room` 내부에는 락이 없습니다.**

---

## 설계 판단

### 1. 수신 버퍼 — 링 버퍼 대신 선형 + compaction

`RecvBuffer`는 이름 그대로 링 버퍼가 아닙니다. wraparound 없이 선형으로 쌓고, 소비된 만큼 앞으로 밀어냅니다(`memmove`).

**이유는 패킷 파싱에 연속 메모리가 필요하기 때문입니다.** 진짜 링 버퍼는 패킷이 버퍼 끝과 시작에 걸쳐 두 조각으로 나뉠 수 있습니다. 그러면 헤더 4바이트를 읽을 때조차 임시 버퍼로 합쳐야 하고, 바디도 두 조각을 이어붙여야 합니다. 링의 장점인 "복사 없음"이 파싱 단계에서 도로 사라집니다.

선형 방식은 데이터가 항상 연속이라 헤더를 `memcpy` 한 번으로 읽고, 바디는 포인터 하나로 넘길 수 있습니다. `WSARecv`에 넘길 `WSABUF`도 하나면 됩니다.

대가는 compaction 시 `memmove`인데, 옮기는 양은 **아직 소비되지 않은 나머지**뿐이라 보통 부분 패킷 하나 크기입니다.

### 2. 패킷 경계 복원

```
struct PacketID { PacketType _type; uint16 _size; };   // _size = 바디 크기 (헤더 4바이트 제외)
```

핵심은 **완료 통지 1회가 패킷 1개를 의미하지 않는다**는 점입니다. TCP는 메시지 경계를 보존하지 않으므로 한 번의 수신에 패킷이 0개일 수도, 여러 개일 수도 있습니다. `TCP_NODELAY`를 켜도 마찬가지입니다 — Nagle은 송신 측이 모아 보낼지를 정할 뿐이고, 수신 측은 `recv` 호출 시점에 커널 버퍼에 쌓인 것을 전부 돌려줍니다. IOCP 서버는 완료 통지를 처리하는 동안 계속 쌓이므로, **부하가 높을수록 더 많이 뭉칩니다.**

그래서 소비 루프가 호출부에 있습니다.

```cpp
int divideByte = transferByte;
while( m_Recv.GetRecvBuffer().DivideBuffer( divideByte ) )
{
    divideByte = 0;   // 이후 반복은 누적분 재파싱만
    int size = GetPacketID()._size + PacketID_SIZE;
    // ... 패킷 처리 ...
    m_Recv.GetRecvBuffer().SetReadPos( size );
}
```

`divideByte = 0`은 "새로 받은 바이트는 없지만 이미 쌓인 것으로 다음 패킷이 완성됐는지 다시 봐 달라"는 뜻입니다.

**크기 상한 검증**: `_size`는 클라이언트가 보내는 `uint16`이라 최대 65535까지 조작 가능합니다. 버퍼 용량을 넘는 값이면 그 패킷은 영원히 완성될 수 없고, 검증이 없으면 버퍼가 가득 찬 채 회수되지 않아 세션이 영구 점유됩니다. `RecvBuffer::Clear()`에서 이를 판별해 폐기합니다.

### 3. 세션 수명 — `shared_ptr` + 커스텀 deleter

세션은 풀에서 꺼내 쓰고 되돌립니다. 문제는 **언제 되돌릴 수 있는가**입니다.

`WSARecv`/`WSASend`를 게시하면 커널이 `OVERLAPPED`와 버퍼 주소를 들고 있습니다. 이때 세션이 풀로 반납되어 다른 연결에 재할당되면, 뒤늦게 도착한 완료 통지가 **다른 사람의 세션**을 건드립니다. 풀이 메모리를 살려두므로 크래시는 잘 나지 않고, 대신 조용히 잘못 동작합니다.

해결은 참조 계수로 재사용 시점을 통제하는 것입니다.

```cpp
SessionDataRef SessionManager::PopSession()
{
    SessionData* session = m_SessionPools.Pop();
    if( nullptr == session ) return nullptr;

    // 소멸이 아니라 "풀로 반납" 이 deleter
    return SessionDataRef( session, []( SessionData* p ) {
        SessionManager::This()->PushSession( p );
    });
}
```

`shared_ptr`이 여기서 하는 일은 메모리 수명 관리가 아니라 **재사용 장벽**입니다.

```
게시 중인 IO 1개  →  refcount +1
세션 맵            →  refcount +1
                      ↓ 전부 놓아야
refcount 0        →  deleter → 풀 반납 → 재사용 가능
```

게시 직전에 참조를 잡고, 완료 시 놓습니다.

```cpp
bool SessionData::RecvStart()
{
    m_Recv.SetSession( shared_from_this() );   // 게시 전 참조 획득
    ...
}

void RecvObject::Execute( int transferByte )
{
    SessionDataRef session = m_Session;   // 지역으로 옮겨 생존 보장
    SetSession( nullptr );                // 멤버 자기참조 해제 (순환 방지)
    ...
}
```

`RecvObject`는 `SessionData`의 멤버이므로 `shared_from_this()`를 그대로 들면 순환 참조가 되어 refcount가 0에 도달하지 못합니다. 완료 처리 첫머리에서 반드시 끊습니다.

이 구조 덕분에 송신을 추가할 때 별도 작업이 없었습니다. 수신·송신 두 IO가 각각 참조를 들면 **둘 다 끝나야 반납**이 자동으로 성립합니다.

### 4. 세션 풀 고갈 시 대기 큐

`AcceptEx`는 accept 소켓과 세션을 미리 확보해 게시합니다. 풀이 비어 있으면 게시할 수 없는데, 그때 `AcceptObject`를 버리지 않고 대기 큐에 넣어 두었다가 세션이 반납될 때 재무장합니다.

여기서 지켜야 할 순서가 있습니다 — **세션을 손에 쥔 뒤에만 대기자를 큐에서 꺼냅니다.**

```cpp
void SessionManager::ReWaiting()
{
    { lock_guard lg( m_WaitLock ); if( m_WaitQueue.empty() ) return; }  // ① 있는지 확인만

    SessionDataRef session = PopSession();                              // ② 세션 확보
    if( nullptr == session ) return;                                    //    실패 시 대기자는 큐에 그대로

    AcceptObject* acceptObject = nullptr;
    { lock_guard lg( m_WaitLock );                                      // ③ 이제 꺼냄
      if( m_WaitQueue.empty() ) return;
      acceptObject = m_WaitQueue.front(); m_WaitQueue.pop(); }

    acceptObject->SetSession( session );
    ListenManager::This()->Accept( acceptObject, false );               // ④ 락 밖에서
}
```

먼저 꺼냈다가 실패하면 되돌리는 방식은, 큐가 잠깐 비는 구간에 다른 스레드가 세션을 반납하면 **대기자를 깨우지 못하고 놓치는 경쟁**이 생깁니다. 그리고 ④를 락 안에서 호출하면 `Accept` → `InsertWait` → 같은 락 재요청으로 데드락이 납니다. 순서와 락 범위가 모두 의도된 것입니다.

### 5. 송신 — 큐 + 게시 플래그

세션 하나에 `WSASend`는 동시에 하나만 걸 수 있습니다. 진행 중인 `OVERLAPPED`를 재사용하는 것은 정의되지 않은 동작이고, 겹쳐 게시하면 전송 순서도 보장되지 않습니다.

"먼저 넣은 쪽이 게시 책임을 지고, 나머지는 큐에만 넣는다"로 해결합니다.

```cpp
void SessionData::InsertSendQueue( SendChunk sendChunk )
{
    bool check = false;
    {
        lock_guard lg( m_SendLock );
        m_SendQueue.push( sendChunk );
        if( false == SendFlag ) { SendFlag = true; check = true; }   // 내가 첫 번째
    }
    if( check ) Send();      // 락 밖에서
}
```

완료 통지에서 배턴을 넘깁니다.

```cpp
void SessionData::CheckSendComplete()
{
    bool check = false;
    {
        lock_guard lg( m_SendLock );
        if( m_SendQueue.empty() ) SendFlag = false;   // 배턴 내려놓기
        else                      check = true;       // 남았으면 이어서
    }
    if( check ) Send();
}
```

**부하가 높을수록 자동으로 묶입니다.** 게시 중에 쌓인 패킷들은 다음 게시 때 `WSABUF` 배열로 한 번에 나갑니다. 별도 배칭 로직 없이 구조에서 나오는 성질입니다.

### 6. 송신 버퍼 — 청크 + 참조 계수

`SendBuffer`는 40KB 청크이고 풀에서 관리됩니다. 직렬화된 패킷은 청크 안의 한 조각이며, `SendChunk`가 그 조각을 가리킵니다.

```cpp
struct SendChunk
{
    SendBufferRef m_sendBuffer;   // 청크 수명 유지
    char*         m_buffer;       // 조각 시작 위치
    int           m_size;
};
```

`SendObject`가 전송 중인 `SendChunk`들을 보관하다가 완료 시 놓습니다. 마지막 참조가 사라지면 청크가 풀로 돌아갑니다.

이 구조의 목적은 **브로드캐스트에서 복사를 없애는 것**입니다. `WSASend`는 소켓당 호출이라 100명에게 보내려면 100번 불러야 하지만, 직렬화와 복사는 한 번이면 됩니다. 100개 세션이 같은 `SendChunk`를 참조하면 청크 refcount만 올라갑니다.

### 7. `GetAcceptExSockaddrs`는 `WSAIoctl`로 얻어야 합니다

`mswsock.lib`의 정적 export를 직접 호출하면 이 함수는 아무것도 채우지 않고 반환합니다. 반환형이 `void`라 실패가 드러나지 않고, `memcpy_s(dst, size, src, 0)`은 0바이트만 복사하므로 결과 구조체가 **초기화되지 않은 스택값 그대로** 남습니다. 모든 접속이 동일한 잘못된 주소를 갖게 됩니다.

`AcceptEx`와 마찬가지로 `WSAIoctl(SIO_GET_EXTENSION_FUNCTION_POINTER, WSAID_GETACCEPTEXSOCKADDRS, ...)`로 런타임에 포인터를 얻습니다. 길이 검증도 함께 둡니다.

### 8. `ObjectPool`의 저장소

`vector<T>`가 아니라 `unique_ptr<T[]>`를 씁니다.

`vector<T>::resize`는 원소의 복사 또는 이동을 요구하는데, `std::mutex`를 멤버로 가진 타입(`SessionData`의 송신 락)은 둘 다 불가능합니다. `make_unique<T[]>(n)`은 기본 생성만 요구하므로 제약이 없고, 한 번 할당하면 재할당되지 않아 **나눠준 포인터의 안정성이 타입 차원에서 보장**됩니다.

### 9. 룸 상태 — 공유 락 대신 스레드 소유

룸의 입장자 맵(`m_roomUser`)에는 락이 없습니다. 대신 **룸마다 스레드를 하나 두고, 그 룸의 상태는 그 스레드만 만집니다.**

IOCP 워커가 `CLIENT_ENTER` / `CLIENT_BROADCAST` / `CLIENT_LEAVE`를 받으면 직접 처리하지 않고 `JobObject`로 만들어 해당 룸의 큐에 넣습니다. 룸 스레드가 `JobQueue::PopAll`로 꺼내 실행합니다.

**공유 락을 쓰지 않은 이유는 브로드캐스트가 O(N) 구간이기 때문입니다.** 브로드캐스트 1회는 룸 인원 수만큼 `InsertSendQueue`를 부릅니다. 이걸 락으로 감싸면 룸 전체가 그 시간 동안 멈추고, 워커 8개가 같은 락에서 줄을 섭니다. 스레드 소유로 바꾸면 락 획득 자체가 없어지고, 룸이 여러 개면 서로 독립적으로 돕니다.

`JobQueue::PopAll`은 큐를 하나씩 꺼내지 않고 `swap`으로 통째로 가져옵니다. 락을 잡는 횟수가 잡 개수가 아니라 배치 개수가 됩니다.

**대가는 룸 하나가 단일 스레드라는 것입니다.** 부하 측정에서 이게 그대로 나타납니다 — 병목은 CPU가 아니라 룸당 순회이고, 초당 메시지 수보다 룸 크기가 먼저 걸립니다. 같은 유출량(약 12만/초)에서 팬아웃 11인 구성은 p50 0.37 ms, 팬아웃 24인 구성은 244 ms 입니다. 룸을 더 잘게 쪼개는 것 외에는 이 구조로 해결되지 않습니다. (RESULTS.md 13절)

---

## 검증

**패킷 재조립**

| 시나리오 | 결과 |
|---|---|
| 헤더/바디 2분할 (4 + 6) | 정상 조립 |
| 헤더 자체가 분할 (2 + 2 + 6) | 정상 조립 |
| 바디 분할 (4 + 5 + 1) | 정상 조립 |
| 1바이트씩 10회 | 정상 조립 |
| 바디 없는 패킷 (`_size = 0`) | 정상 처리 |
| 한 번에 여러 패킷 + 잘린 나머지 | 완성분 소비, 나머지 보존 |
| 랜덤 fuzz (바디 0~200B, 청크 1~400B, 500회) | 9,520 패킷 전부 순서·내용 일치 |

**악성 입력**: `_size = 60000`을 64바이트 버퍼에 반복 전송 — 버퍼 가용 공간이 회복되며 점유되지 않음.

**실서버 동작** — 대기 큐 경로를 강제하기 위해 세션 풀을 3으로 줄이고 5개를 접속시킨 구성 (현재 기본값은 10,000):
- 접속 3개 처리 후 나머지는 대기 큐, 연결 종료 시 대기 중이던 `AcceptEx` 재무장 확인
- 클라이언트가 4패킷을 한 번의 `send()`로 전송 → 완료 통지 1회에서 4개 모두 처리
- 1바이트씩 전송 → 정확히 조립
- 에코 응답 왕복 확인. 연속 요청 시 응답이 하나의 `WSASend`로 묶여 나가는 것 확인

**룸 / 브로드캐스트** (`TestClient/LoadTestRoom.exe verify`, 연결 16)

| 시나리오 | 결과 |
|---|---|
| ENTER → `SERVER_ENTER` ack 로 roomID 수신 | 16/16 |
| 같은 룸의 나머지 전원이 브로드캐스트 수신, 페이로드 일치 | 정상 |
| 송신자 본인은 수신하지 않음 | 정상 |
| **다른 룸으로 누출 없음** | 정상 |
| 연속 100건 브로드캐스트, 전 연결 프레이밍 | 정상 |

부하를 5회 돌린 뒤 다시 실행해도 6/6 통과합니다.

**부하** — 측정 결과와 방법은 [`TestClient/RESULTS.md`](TestClient/RESULTS.md)에 13개 절로 정리했습니다. 요약하면

- ECHO 경로: 왕복 661,169/초, p50 0.499 ms (연결 50, 논리 프로세서 6 머신, 루프백)
- 브로드캐스트 경로: 유출 약 104,000 메시지/초까지 전달률 100%, p50 1.07 ms
- 브로드캐스트의 병목은 CPU가 아니라 **룸당 단일 스레드의 동기 순회**. 초당 메시지 수보다 룸 크기가 먼저 걸립니다

측정 도구 자신이 병목이었던 경우(8절), 변수를 둘 동시에 바꿔 결과를 해석할 수 없던 경우(10~11절), 클라이언트 버그를 서버 지연으로 오독한 경우(13절)를 기각 과정까지 함께 적어두었습니다.

**테스트 하니스**

| 파일 | 용도 |
|---|---|
| `TestClient/TestClient.cpp` | 패킷 재조립 정확성 (분할/병합/fuzz) |
| `TestClient/LoadTest.cpp` | 연결당 스레드 1개 방식 부하 클라이언트 |
| `TestClient/LoadTestFast.cpp` | 논블로킹 + `WSAPoll` 경량 부하 클라이언트 (ECHO) |
| `TestClient/LoadTestRoom.cpp` | 룸/브로드캐스트 정확성 + 부하 (`verify` / `load` 모드) |

세 부하 클라이언트 모두 서버의 `PacketStruct.h`를 직접 include 합니다. 규격을 두 곳에 두면 반드시 어긋나기 때문입니다.

---

## 현재 상태

**구현됨**

- IOCP 워커 풀, 완료 라우팅
- `AcceptEx` 사전 게시 및 재무장, 주소 파싱
- 수신 누적 / 패킷 경계 복원 / 크기 상한 방어
- 세션 풀 + 참조 계수 기반 수명 관리
- 세션 풀 고갈 시 대기 큐
- 송신 큐 + 게시 직렬화 + 배칭
- 송신 청크 풀 + 참조 계수
- 패킷 핸들러 / 디스패치 — X-매크로로 enum 과 등록을 한 목록에서 생성, 함수 포인터 배열로 분기
- 룸 + 잡 큐 — 룸마다 스레드 1개, 룸 상태는 그 스레드만 접근 (락 없음)
- 브로드캐스트 — `MakeSendPacket` 1회로 만든 청크를 같은 룸의 N 세션 큐에 배포
- 세션 종료 시 룸에서 제거 (모든 종료 경로가 `Job_Leave`를 해당 룸에 게시)
- 프로세스 자기 CPU 사용량 모니터 스레드

**미구현**

- 부분 전송(`transferByte < 요청량`) 처리
- Graceful shutdown — `JobQueue::Stop()`은 호출부가 없고, `RoomManager::Work`의 `while(true)`가 `StartJob()`을 감싸고 있어 stop 으로 리턴해도 다시 들어감. `Room::LeaveAll()`도 비어 있음
- 로깅 — `cout` 직접 사용
- `ConfigManager` 미연결 — 포트 27130 하드코딩, 워커/accept/룸 수는 `main`의 리터럴 (`Initalize( 8, 1, 128 )`, `roomManager->Initalize( 5 )`)
- DB, 인증, 재접속, 프로토콜 버저닝 — 이 저장소의 범위 밖

**알려진 정리 대상**

- **`SendObject::Execute`의 송신 실패 경로(`IOCP.cpp:247`)만 `CloseSession`을 직접 호출합니다.** 다른 종료 경로는 모두 `Job_Leave`를 거쳐 룸에서 제거되는데 여기만 빠져 있어, 송신 중 끊긴 세션이 `m_roomUser`에 남습니다. 소켓 핸들이 재사용되면 무관한 연결로 브로드캐스트가 나갈 수 있습니다 (RESULTS.md 13절 「발견 1」과 동일한 경로)
- 같은 세션에 대해 `Job_Leave`가 두 번 게시될 수 있습니다 (수신 경로와 송신 경로가 동시에 실패하는 경우). `Room::Leave`가 두 번 돌면 `closesocket()`도 두 번 불립니다
- `PushJobByRooms`가 인덱스 범위 밖일 때 잡 종류와 무관하게 세션을 닫습니다. BROADCAST 핸들러도 `GetRoomID()`를 넘기므로 ENTER 전에 BROADCAST를 보내면 연결이 끊깁니다
- `Room.h` 인코딩이 CP949 — 프로젝트가 `/utf-8`로 컴파일되어 경고 다수. 룸 관련 신규 파일들(`JobObject.*`, `JobQueue.h`, `RoomManager.*`, `Room.cpp`)은 BOM 없는 ASCII 로, 기존 파일(UTF-8 BOM)과 다릅니다
- `Room::BroadCast`의 `for( auto session : m_roomUser )`가 `auto&`가 아니라 멤버마다 `shared_ptr`를 복사합니다
- 룸 배정에 `rand() % 5`를 씁니다. MSVC `rand()`는 스레드마다 독립 상태를 시드 1로 시작하므로 IOCP 워커들이 같은 수열을 냅니다 — 실제로 분포가 고르지 않습니다
- 위치 변수 `uint16` → `int32` (64KB 초과 버퍼에서 래핑)
- `ServiceManager` / `ListenManager` / `SessionManager` / `RoomManager` 싱글톤 상호 참조 — 단위 테스트 불가
- 반환값이 복수 의미를 겸하는 곳 (`Accept`의 `bool`이 "게시됨"과 "대기 등록됨"을 구분하지 못함)
- `RecvObject::Execute`가 `SessionData::Recv`의 false 를 모두 `"PakcetHandler Error"`로 출력합니다. `Recv`는 마지막이 `return RecvStart()`라 **WSARecv 재등록 실패에도 false**가 나오므로, 정상 종료가 패킷 오류로 찍힙니다 (오타도 포함)

---

## 빌드 및 실행

```
jhshin_Server/jhshin_Server.sln 을 Visual Studio 2022 로 열고 x64 빌드
```

기본 포트 `27130`. 실행하면 리슨을 시작하고 `AcceptEx`를 미리 게시합니다.

`main`의 인자는 `ServiceManager::Initalize( 서비스 워커 수, 리슨 워커 수, accept 사전 게시 수 )`,
`RoomManager::Initalize( 룸 수 )` 입니다. 현재 각각 `( 8, 1, 128 )`, `( 5 )`.

세션 풀 10,000 / SendBuffer 청크 1,024개는 `ServiceManager::Initalize` 안에 고정돼 있습니다.

**테스트 클라이언트**

```
cl /nologo /utf-8 /std:c++20 /O2 /EHsc /I jhshin_Server\jhshin_Server ^
   TestClient\LoadTestRoom.cpp /Fe:LoadTestRoom.exe ws2_32.lib

LoadTestRoom.exe verify
LoadTestRoom.exe load [연결수] [지속초] [초당송신/연결] [스레드수]
```

---

## TODO

1. **`SendObject::Execute`의 송신 실패 경로를 `Job_Leave`로 통일** — 위 「알려진 정리 대상」 첫 항목
2. Graceful shutdown (`JobQueue::Stop()` 호출 경로 + `PostQueuedCompletionStatus` 기반 워커 종료)
3. `Job_Leave` 중복 게시 방지 (세션에 종료 플래그 1회 전이)
4. 테스트 하니스를 `tests/` 로 편입, 빌드 스크립트 추가
5. `ConfigManager` 연결, 고정치 제거
6. 룸 배정을 `rand()` 대신 명시적 정책으로 (라운드로빈 또는 클라이언트 지정)
7. 로깅 도입, `"PakcetHandler Error"` 메시지 분리
8. 싱글톤 상호 참조 정리
