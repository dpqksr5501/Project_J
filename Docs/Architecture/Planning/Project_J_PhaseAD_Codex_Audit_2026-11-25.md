# Project J Phase A~D 독립 감사

기준은 2026-11-25의 Phase A~D 작업 트리와 설치형 UE 5.8이다. Wave 1~4와 A-1~A-3의 멀티스레딩, 영속 Outbox, 8모듈 의존성, GAS 피해 권위를 기반으로 감사했다.

## 발견과 조치

감사 범위에서 P0로 확정한 결함은 없었다. 아래 표는 수정한 코드와 정적 배포 연결을 기록하며, 운영 환경 실행 결과는 별도 범위에 둔다.

| 우선순위 | 파일과 경계 | 근본 원인과 조치 |
| --- | --- | --- |
| P1 | [Iris 군중 필터](../../../Source/Project_JCharacter/Private/Network/Project_JCrowdDensityNetObjectFilter.cpp#L282) | 컴포넌트·GAS·소셜 조회는 기존부터 후보당 패스 1회였다. 연결마다 반복한 소유자 탐색을 스냅샷으로 옮기고, 전체 랭킹 정렬을 상위 K개만 유지하는 힙으로 교체했다. 객체 제거에서 이전 허용 집합도 정리하고 통계를 ReplicationSystem별로 읽는다. |
| P1 | [서버 Governor](../../../Source/Project_JCore/Private/Optimization/Project_JServerLoadGovernorSubsystem.cpp#L72) | 틱에 공급된 시뮬레이션 `DeltaSeconds`가 작업 시간으로 쓰였다. 현재 GameInstance 월드의 `OnWorldTickStart`와 `OnWorldTickEnd`를 같은 단조 시계로 짝지어 월드 작업 경과를 측정한다. |
| P1 | [이동 감사](../../../Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp#L667), [무결성 상태](../../../Source/Project_JCore/Private/Security/Project_JServerIntegritySubsystem.cpp) | 순간이동 허용은 월드 시간, 실제 감사 샘플은 프로세스 단조 시계를 사용했다. 정상 순간이동 경계 이벤트에서 위치 연속성 기준을 재수립하고 탑승·물리 시뮬레이션·사망 상태는 그 이동 단위에서 분리했다. 권위 대시의 제한된 허용과 아바타별 상태 반환을 연결했다. 서버가 확정한 물리 속도를 참고하도록 이동 관측을 조정했고, 최종 교정은 기존 Mover 권위가 맡는다. |
| P1 | [Melee](../../../Source/Project_JCharacter/Private/Combat/Project_JGameplayAbility_Melee.cpp), [감사 브리지](../../../Source/Project_J/Game/Project_JGameMode.cpp#L46) | 고정 80ms의 패킷 도착 간격으로 서로 다른 공격 속도와 콤보를 재단하던 감사 호출을 제거했다. GAS의 실제 커밋·쿨다운·콤보 정책을 유지하면서, 기존 무결성 위반 창의 임계치를 처음 지난 경우 서버 전용 Nakama 감사 RPC로 보낸다. |
| P1 | [메트릭 HTTP](../../../Source/Project_J/Backend/Project_JServerMetricsSubsystem.cpp#L229) | 연결당 동기 대기와 단발 헤더 송수신으로 게임 스레드 히치 및 잘린 응답이 가능했다. 논블로킹 접수·분할 읽기·분할 쓰기, 상한 32연결·헤더 4KiB·수명 2초를 적용했다. 기본 루프백과 컨테이너의 명시적 바인딩을 맞췄고, UTF-8 바이트 길이와 Prometheus HELP/TYPE을 제공한다. |
| P1 | [서버 드레인](../../../Source/Project_J/Backend/Project_JServerDrainSubsystem.cpp#L193) | 기존 강제 퇴장은 클라이언트 RPC에 의존했다. 유예가 끝나면 서버가 강제 퇴장시키고 연결을 닫으며, 로그아웃 이후 다음 게임 스레드 패스에서 첫 Outbox 정착 판단을 한다. 플러시 진입 시점부터 10초 상한을 계산하며 복구·쓰기·정리 작업을 포함한다. |
| P1 | [Handover HTTP](../../../Source/Project_J/Backend/Project_JHttpHandoverTransport.cpp#L124) | JSON 숫자 변환으로 64비트 틱 정밀도가 소실됐다. 십진 정수 문자열과 크기/시각 검사를 적용했다. 등록 URL 권한부와 포트를 검증하고, 송신 성공 응답은 등록 URL과 같은 최종 목적지에서 받아야 한다. 늦은 완료는 동일 요청 객체와 송신자 수명이 남은 경우에만 반영한다. |
| P1 | [SQL 012](../../../Infra/migrations/012_audit_partitioning_and_queue.sql#L111) | 채널 조회가 존재하지 않는 `heartbeat_at`을 읽고 기존 활성 티켓을 정원에 반영하지 않았다. 실제 `leased_until`을 사용하며, 기존 티켓 발행 함수의 채널 행 잠금과 활성 예약 수를 공유한다. 대기열은 존별로 직렬화하고 FIFO·만료·상한을 적용한다. 기존 직행 티켓 발행과 최종 DS 입장 권위를 보존한다. |
| P1 | [컨테이너](../../../Infra/deploy/Dockerfile.ue-server#L24), [Agones](../../../Infra/deploy/agones/fleet-zone-server.yaml#L26) | 래퍼와 게임 바이너리 사이의 종료 신호 전달, Pod IP 준비 프로브, Agones SDK 연결이 불명확했다. 리눅스 실행 파일을 PID 1로 시작하고, SIGTERM은 게임 스레드 드레인으로 전달한다. SDK 환경 포트로 준비·헬스 콜을 연결하고, 메트릭 TCP는 Pod 내부에 둔다. 실제 Linux/클러스터 검증은 아래 범위를 따른다. |
| P2 | [012 영수증 정리](../../../Infra/migrations/012_audit_partitioning_and_queue.sql#L111), [Go/Valkey](../../../Infra/nakama/go_plugin/main.go), [Compose](../../../Infra/docker-compose.yml#L47) | 나이만 보던 영수증 삭제에 실제 만료와 범위 인덱스를 추가했다. 선택적 Valkey 큐에 유효 수명과 용량 상한을 두고, 신규 연결 확인과 초기화 실패 시 닫기를 정의했다. PgBouncer 헬스체크는 인증과 실제 질의를 확인한다. |

## 유지한 계약

- 월드별 Iris 우선순위와 피해 판정은 그대로 유지한다. 보스·소유자·기존 소셜 예외가 우선하며, 동적 OwnerOnly는 소유 연결에만 공개한다. 필터를 이미 할당한 상태에서 CVar를 끄면 인원 상한이 해제되고 공간 반경은 계속 적용된다.
- 기존 HandoverManager가 `TransferId`, 본문 해시, 도착 액터 소유권, 적용 중 상태와 bounded 적용 영수증으로 재생 효과를 제어한다. HTTP 서명 파서는 시각 신선도와 입력 검증을 수행한다. 배포 시 신뢰된 HTTPS 목적지와 출구 정책, 인증된 수신 HTTP 라우트와 승인된 도착 액터 해석이 필요하다. UE HTTP 구현의 자동 리다이렉트 자체는 이 전송 클래스에서 차단되지 않으므로 최종 URL 검사는 송신 뒤 성공 판정에 적용한다.
- 재시작 복구의 시작점은 기존과 같이 SQLite 최초 커밋이다. 플러시 시간 초과는 네트워크 확정 성공으로 기록하지 않는다. 외부 백엔드나 저장 볼륨 장애는 별도 운영 정책을 적용한다.
- PgBouncer는 개발용 SCRAM과 transaction pooling, 이름 있는 준비문 추적을 유지한다. Nakama 3.25.0의 Go 1.23.3, `nakama-common` 1.35.0, pgx v5.7.1을 대조했다. Go/Valkey 구현은 선택 경로이며 SQL 티켓이 입장 권위다. [Nakama 3.25.0 모듈](https://raw.githubusercontent.com/heroiclabs/nakama/v3.25.0/go.mod) · [PgBouncer 설정](https://www.pgbouncer.org/config.html)

## 최초 검증

- `python -B Scripts/Validation/Validate-MMOArchitecture.py`에서 8모듈 DAG, 20도메인, 205기능을 통과했다. 설치형 UE 5.8의 직접 UBT `Project_JEditor Win64 Development` 대상은 최종 종료 코드 0, 컴파일 오류·경고 각 0건이다. UBA의 기존 세션 디렉터리 접근 메시지는 별도로 로그에 남겼다. [최종 빌드](../../../Saved/Validation/Continuation_20261014/PhaseAD_IndependentBuildFinal_20261125/UBT.log)
- RHI 집중 회귀 3/3 통과. 500후보 예산 정책, 8개 무응답 연결과 분할 HTTP 헤더, 드레인 상태와 헬스 응답, 64비트 타임스탬프와 HMAC 경계 벡터를 확인했다. [집중 회귀](../../../Saved/Validation/Continuation_20261014/PhaseAD_Focused1_20261125/index.json)
- PGlite에서 SQL 001–012 적용과 전체 기존 DB 회귀가 종료 코드 0으로 통과했다. Phase 012의 소유권, 채널 예약·오버플로, FIFO, 영수증 만료, 서버 전용 RPC를 추가로 확인했다. [DB 로그](../../../Saved/Validation/Continuation_20261014/PhaseAD_PGlite2_20261125/Run.log)
- 루프백의 실제 PostgreSQL 16.15에 001–012 전체를 적용하고, pgx v5.7.1의 최대 12개 동시 DB 연결에서 직접·경유 발급 40건을 경쟁시켰다. 정원 4인 채널에서 직접 성공 2건, 경유 성공 2건, 대기 18건, 거절 18건과 활성 티켓 정확히 4건을 확인했다. 테스트 인스턴스는 정상 종료했다. [PostgreSQL 실행 로그](../../../Saved/Validation/Continuation_20261014/PhaseAD_Postgres16_20261125/Run.log)
- Node 내장 Acorn ES5 구문과 Nakama 3.25.0이 고정한 Goja 버전의 모듈 등록·권한 계약을 확인했다. 공식 SHA-256으로 검증한 Go 1.23.3으로 `go test ./...`를 실행했다. Go 플러그인에는 현재 단위 테스트가 없어 이 실행은 Windows 컴파일 검증이다.
- PyYAML 중복 키 검사를 포함해 Compose, Prometheus, Grafana 프로비저닝 및 Agones YAML을 읽었고 Grafana 대시보드 JSON과 PowerShell 파서 검사를 통과했다.
- 전체 `Automation RunTests ProjectJ` RHI 실행 589건 중 585건 성공, 실패 4건, 미실행 0건, 프로세스 종료 코드 255다. 성공에는 경고 동반 6건이 포함된다. `GroupB.CombatContinuity` 5건의 Mover 시작 위치 경고와 `GatewayRealHTTP`의 의도된 초과 응답 거절이 해당한다. 실패는 `GroupA.VisualPressure`, `PlayerLocomotion.MannyFoundation`, `MontageAuthoringIdempotence`, `PlayerMasterHierarchy`다. 저장 에셋 계약과 시각 자산 fixture는 Phase A~D 수정 범위 밖이므로 전체 결과에 그대로 남겼다. [전체 회귀](../../../Saved/Validation/Continuation_20261014/PhaseAD_AllProjectJ_20261125/index.json)
- 새 Editor 프로세스에서 실패 4건만 재실행해 동일 실패를 재현했다. 각각 시각 로드 예약 수, 매니 마스터 클래스, 저장 몽타주 순서, BP의 능력 소유권 기대값이다. [단독 재검증](../../../Saved/Validation/Continuation_20261014/PhaseAD_FullFailuresIsolated_20261125/index.json)
- `Run-LoopbackCohort.ps1`의 필드 보스·Iris·ThinClient·코어 격리와 메모리 4,096MiB 보호선을 사용했다. 연결·타격·체력 수신·종료 **24/24/24/24**, 세 구간 표본 각 24개, 서버·생성기 종료 코드 각 0이다. 30Hz 예산 통과, Idle p95 **4.688ms**, Burst p95/p99/최대 **5.165/5.446/7.071ms**, 33.3ms 초과 0틱이다. Burst 지연 p95는 요청→첫 검증 **529.804ms**, 첫 검증→권위 피해 **99.523ms**, 권위 피해→복제 수신 **134.318ms**다. 최저 물리 메모리 여유는 4,514MiB였다. [24인 Iris](../../../Saved/Validation/Continuation_20261014/PhaseAD_Field24_Iris_20261125/verification.json)
- 실제 메트릭 엔드포인트가 **24 CCU, 후보 25개, 허용 연결·객체 쌍 600개, 제거 0개**를 보고했다. 24명은 기본 상한 60 아래다. 이 실행은 실연결에서 필터가 동작했다는 근거이고, 상한 초과 정책은 500후보 집중 회귀에서 확인했다. Agones 루프백 응답기는 **준비 1회, 헬스 16회**를 받았다. [메트릭 원본](../../../Saved/Validation/Continuation_20261014/PhaseAD_Field24_Iris_20261125/server-metrics.prom) · [SDK 호출 기록](../../../Saved/Validation/Continuation_20261014/PhaseAD_AgonesLoopback_20261125/calls.json)

## 최초 감사의 운영 검증 범위

Docker·Kubernetes·WSL 배포 환경은 현재 머신에 없다. Nakama/PgBouncer 컨테이너의 실제 기동, 분산 서버의 로그인·장애 복구, Linux DS PID 1의 SIGTERM, 실제 Agones Pod 준비, 플러그인 `.so` 로드, 분리 호스트 100~200인 교전은 운영 검증 항목이다. 실제 접속자의 드레인 강제 종료 및 HTTP Handover 수신·재생은 종단 실행이 필요하다. [Agones SDK 기준](https://agones.dev/site/docs/guides/client-sdks/rest/) · [Prometheus 텍스트 포맷](https://prometheus.io/docs/instrumenting/exposition_formats/)

**최초 판정**. Phase A~D의 로컬 수정 검증은 집중 회귀·PostgreSQL 16·24인 Iris 기준을 충족했다. 전체 프로젝트 0실패·0경고 게이트와 운영 배포는 보류한다. 원본 검증 자료 17개를 SHA-256으로 확인해 기존 중앙 보관소에 등록하고 기존 보고 경로를 유지했다.

## 후속 조치 및 로컬 운영 재검증

### 전체 회귀 실패 4건

| 실패 | 원인과 조치 |
| --- | --- |
| `GroupA.VisualPressure` | 시험용 미완료 로드가 시작 시각 0을 유지해 다음 패스에서 바로 오래된 예약으로 판정됐다. 실제 시작 시각과 같은 기준으로 시험 상태를 만든다. 실제 런타임도 마지막 소비자가 사라지고 완료 콜백이 오지 않으면 만료를 처리할 틱이 없었다. 가장 이른 만료 시각에만 정리 패스를 깨워 예약을 반환하고, 그 전에는 유휴 틱을 실행하지 않는다. |
| `PlayerLocomotion.MannyFoundation` | 공유 Manny 골격과 공유 플레이어 AnimBP를 같은 계약으로 검사했다. 모든 저장 아바타의 골격 호환성은 계속 검증하고, 플레이어만 공통 AnimBP를 요구한다. NPC는 별도의 플레이어 Motion Matching 드라이버 없는 경로와 기존 NPC 전용 그래프 회귀로 확인한다. |
| `PlayerLocomotion.MontageAuthoringIdempotence` | 편집 유틸리티가 기존 직결 그래프만 알아봤다. 저장된 그래프는 보정된 포즈 캐시를 스킬 슬롯과 하체 복귀에 함께 사용하고 최종 가중치 합성을 마친 뒤 다음 단계로 보낸다. 정상 연결을 정확히 확인해 재적용해도 그래프나 저장 에셋을 수정하지 않는다. |
| `PlayerLocomotion.PlayerMasterHierarchy` | 이전 검증은 아바타 기본 어빌리티 목록을 비워야 한다고 가정했다. 저장 BP 두 개는 같은 공통 Sprint 정의를 제공한다. 정확히 한 개의 Sprint를 상속하는지 확인하며 실제 부여는 기존 서버 권위 ASC가 맡는다. |

시각 자산, 애니메이션과 어빌리티의 수정 대상은 실행 코드와 저장 에셋을 검증하는 테스트다. 저장 BP·AnimBP와 월드 에셋은 재저장하지 않았다. [실패 4건 집중 재검증](../../../Saved/Validation/Continuation_20261014/PhaseAD_CloseoutFour_20261125/index.json)에서 4/4, 경고 0건이다. [설치형 UE 5.8 직접 UBT 최종 빌드](../../../Saved/Validation/Continuation_20261014/PhaseAD_CloseoutRuntimeBuildFinal_20261125/UBT.log)가 종료 코드 0과 컴파일 오류·경고 각각 0건으로 완료됐고, 8모듈 DAG 검사는 20도메인·205기능을 통과했다.


[최종 전체 RHI 자동 회귀](../../../Saved/Validation/Continuation_20261014/PhaseAD_CloseoutAllProjectJ_20261125/index.json)는 **589/589 통과, 실패·미실행 0건**, 프로세스 종료 코드 0이다. 583건은 경고 없이 성공했고 6건은 기존 경고와 함께 성공했다. Mover 시작 위치를 직접 바꾸는 `CombatContinuity` fixture 5개와 크기 제한에 걸린 HTTP 응답을 의도적으로 차단하는 `GatewayRealHTTP` 1개다. 새 서명 HTTP 왕복 확인과 이전 실패 4건도 전체 실행에서 통과했다.

### 운영 검증 연결

- 실제 Windows Nakama 3.25.0과 PostgreSQL 16.15를 각 실행용 DB와 루프백 포트에서 구동하고 SQL 001–012를 적용했다. 두 서비스 재시작 후 우편·아이템·골드·세션·복귀 nonce 검증을 실행했다. 동시 우편 수령은 `COMMITTED` 1건과 `CONFLICT` 1건이었다. 서버 전용 Nakama RPC에서 정원 1인 채널 두 개의 우선 채널·우회 채널·대기를 확인했고, 일반 로그인과 경유 예약이 같은 정원을 공유했다. 플레이어 토큰으로 호출한 서버 전용 경유 RPC는 거절됐다. [로컬 통합 실행기](../../../Infra/scripts/test-local-native-backend.ps1) · [Nakama 공식 릴리스](https://github.com/heroiclabs/nakama/releases/tag/v3.25.0)
- 종료 검증은 로컬 로그인 시험 모드에 명시적으로 활성화한다. PostgreSQL로 입장한 접속자의 캐릭터 점유, 헬스 200→503, 서버 강제 연결 종료, 권위 로그아웃, Outbox 정착과 정상 프로세스 종료를 각각 확인한다.
- Handover HTTP에서는 실제 전송기의 서명 헤더와 원본 POST 바이트를 루프백 시험 응답기가 검증하도록 했다. 전송 ID·바이너리 본문·64비트 틱 시각·도착 노드가 정확히 전달되어야 성공을 반환한다. 별도의 HandoverManager 회귀가 실제 도착 액터에 같은 이관을 두 번 반영하지 않는 계약을 확인한다. 운영용 HTTP 수신 라우트와 신뢰된 액터 해석은 서버 배포 단계에서 연결해야 한다.
- 루프백 전투 하네스가 메트릭 HTTP 본문을 파일로 보존할 때 PowerShell의 추가 CRLF를 제거했다. 공식 Prometheus의 config 검사를 사용한다. 기존 메트릭 이름과 대시보드 호환을 위해 `projectj_server_tick_ms{quantile=...}` 형식은 유지하며, `promtool` 린트에 표시되는 기존 단위·라벨 관례는 별도 기록한다. [Prometheus 3.2.1 릴리스](https://github.com/prometheus/prometheus/releases/tag/v3.2.1)
- Fleet과 FleetAutoscaler를 운영 가이드가 고정한 Agones 1.47.0의 공식 CRD와 대조했다. 두 매니페스트의 스키마를 통과했다. Windows에서 Docker·Podman·kubectl과 등록된 WSL 배포판을 확인했으며 사용 가능한 항목이 없었다. 스테이징 LinuxServer 바이너리도 없었다. [스키마 검사 결과](../../../Saved/Validation/Continuation_20261014/PhaseAD_LocalRuntimeTools_20261125/AgonesVerification.log) · [Agones 1.47.0 공식 CRD](https://raw.githubusercontent.com/googleforgames/agones/release-1.47.0/install/yaml/install.yaml)

### Linux 배포를 막는 저장소 제약

**P1 · 정적 코드로 확인.** [프로필 SQLite 연결](../../../Source/Project_J/Backend/Project_JProfileDatabase.cpp#L324)은 Win32 롤백 VFS를 선택하며, Linux에서 `Open()`이 `false`를 반환한다. 비-Mock 전용 서버는 [온라인 Outbox 초기화](../../../Source/Project_J/Backend/Project_JOnlineWorldSubsystem.cpp#L113)에서 경로를 자동으로 잡으므로 Linux에서는 열기 실패가 전파된다. 이후 [헬스 응답](../../../Source/Project_J/Backend/Project_JServerMetricsSubsystem.cpp#L206)이 503이고, [전용 서버의 기본 정책](../../../Source/Project_J/Backend/Project_JServerDrainSubsystem.cpp#L30)은 자동 드레인이다. SQL 서버 주소나 SQLite 파일 경로를 지정해도 VFS 지원은 생기지 않는다.

Linux에서 파일 잠금·동기화·롤백을 지원하는 SQLite 연결 경로가 필요하다. 설치형 UE 5.8의 기본 HAL VFS는 공유 메모리와 세분화된 파일 잠금이 빠져 있으므로 자동 fallback을 추가하지 않았다. Linux 이미지에서 신규 열기, 프로세스 간 동일 파일 잠금, 핫 저널 롤백, 크래시 이후 미확정 보상 복원, Outbox 초기 열기 완료 후 준비 상태 노출을 확인해야 한다. Pod 교체 시에는 채널별 저장 볼륨 소유권과 재연결도 확인한다. 현재 호스트의 Windows 로컬 결과와 구분한다. [Agones 로컬 가이드](../../../Infra/deploy/agones/k3s-local-setup.md)

### 운영 실측 결과와 최종 판정

| 운영 흐름 | 실측 결과 |
| --- | --- |
| 로그인·우편·가방 | 실제 UE 전용 서버와 클라이언트에서 로그인했다. 우편 아이템 3개를 수령해 2+1로 분할했고 PostgreSQL 소유권과 총수량을 확인했다. 백엔드 두 서비스 재시작 후 동일 키는 `REPLAYED`, 새 키의 중복 수령은 `CONFLICT`였다. 원장 전체의 증감 합계는 0이었다. |
| 던전·퀘스트·복귀 | 저장된 샌드박스와 던전에 전용 서버 두 대를 띄웠다. 채집 2건과 제작 목표 1건, 입장, 테스트 케이스의 서버 권위 처치, 보상 영수증 1건, 출발 지점 복귀를 확인했다. 세션 세대가 3회 증가했고 세 프로세스가 정상 종료했다. 첫 전투 타격부터의 성능 근거는 위 24인 Iris 실행에 있다. |
| 접속자 드레인 | 실제 PostgreSQL 로그인 접속자 1명에서 헬스 200→503, 5초 유예, 서버의 강제 연결 종료와 권위 로그아웃, `evacuated=1 outboxSettled=1`, 서버·클라이언트 종료 코드 0을 확인했다. 첫 시도에서는 채널 임대가 월드의 헬스 응답보다 먼저 준비돼 첫 단발 요청이 시간 초과했다. 실행기가 두 조건을 같은 시작 상한 안에서 기다리도록 수정 후 재실행했다. |
| 관측 | 공식 Prometheus 3.2.1이 방금 실행한 UE 서버에서 실제로 `up=1`, 최대 CCU 1, 드레인 상태 1을 수집했다. 공식 `promtool` 구성 검사와 본문 보존 확인을 수행했다. 기존 틱 지표의 단위·`quantile` 라벨 관례에 대한 린트는 남아 있다. |

[로컬 통합 영수증](../../../Saved/Validation/Continuation_20261014/PhaseAD_CloseoutNativeOperationsRetry_20261125/NativeVerification.json) · [드레인 이벤트](../../../Saved/Validation/Continuation_20261014/PhaseAD_CloseoutNativeOperationsRetry_20261125/DrainEvents.log) · [실제 Prometheus 조회](../../../Saved/Validation/Continuation_20261014/PhaseAD_CloseoutNativeOperationsRetry_20261125/PrometheusLive.json) · [메트릭 린트](../../../Saved/Validation/Continuation_20261014/PhaseAD_LocalRuntimeTools_20261125/PromtoolMetricLint.log)

**최종 판정.** 이전 실패 4건을 해소했고 Windows 로컬 회귀·온라인 운영 흐름을 완료했다. 유료 인프라나 콘텐츠 빌드·쿠킹 없이 검증했다. 실제 배포에는 Nakama 주소·인증 키·채널 광고 주소, PostgreSQL 및 Outbox 영속 볼륨을 연결해야 한다. Linux는 위 SQLite VFS 제약을 풀고 SIGTERM·Agones Pod·PgBouncer/선택 Go·Valkey를 실행 검증해야 한다. 운영 Handover 수신 라우트와 분리 호스트 100~200인 교전도 별도 배포 검증 항목이다.

이번 실행의 원본 증거 13개를 SHA-256으로 검증해 기존 중앙 보관소에 등록하고 문서의 원래 링크를 유지했다.
