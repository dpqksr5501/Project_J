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

- `python -B Scripts/Validation/Validate-MMOArchitecture.py`에서 8모듈 DAG, 20도메인, 205기능을 통과했다. 설치형 UE 5.8의 직접 UBT `Project_JEditor Win64 Development` 대상은 최종 종료 코드 0, 컴파일 오류·경고 각 0건이다. UBA의 기존 세션 디렉터리 접근 메시지는 별도로 로그에 남겼다. 최종 빌드 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_IndependentBuildFinal_20261125/UBT.log`)
- RHI 집중 회귀 3/3 통과. 500후보 예산 정책, 8개 무응답 연결과 분할 HTTP 헤더, 드레인 상태와 헬스 응답, 64비트 타임스탬프와 HMAC 경계 벡터를 확인했다. 집중 회귀 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_Focused1_20261125/index.json`)
- PGlite에서 SQL 001–012 적용과 전체 기존 DB 회귀가 종료 코드 0으로 통과했다. Phase 012의 소유권, 채널 예약·오버플로, FIFO, 영수증 만료, 서버 전용 RPC를 추가로 확인했다. DB 로그 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_PGlite2_20261125/Run.log`)
- 루프백의 실제 PostgreSQL 16.15에 001–012 전체를 적용하고, pgx v5.7.1의 최대 12개 동시 DB 연결에서 직접·경유 발급 40건을 경쟁시켰다. 정원 4인 채널에서 직접 성공 2건, 경유 성공 2건, 대기 18건, 거절 18건과 활성 티켓 정확히 4건을 확인했다. 테스트 인스턴스는 정상 종료했다. PostgreSQL 실행 로그 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_Postgres16_20261125/Run.log`)
- Node 내장 Acorn ES5 구문과 Nakama 3.25.0이 고정한 Goja 버전의 모듈 등록·권한 계약을 확인했다. 공식 SHA-256으로 검증한 Go 1.23.3으로 `go test ./...`를 실행했다. Go 플러그인에는 현재 단위 테스트가 없어 이 실행은 Windows 컴파일 검증이다.
- PyYAML 중복 키 검사를 포함해 Compose, Prometheus, Grafana 프로비저닝 및 Agones YAML을 읽었고 Grafana 대시보드 JSON과 PowerShell 파서 검사를 통과했다.
- 전체 `Automation RunTests ProjectJ` RHI 실행 589건 중 585건 성공, 실패 4건, 미실행 0건, 프로세스 종료 코드 255다. 성공에는 경고 동반 6건이 포함된다. `GroupB.CombatContinuity` 5건의 Mover 시작 위치 경고와 `GatewayRealHTTP`의 의도된 초과 응답 거절이 해당한다. 실패는 `GroupA.VisualPressure`, `PlayerLocomotion.MannyFoundation`, `MontageAuthoringIdempotence`, `PlayerMasterHierarchy`다. 저장 에셋 계약과 시각 자산 fixture는 Phase A~D 수정 범위 밖이므로 전체 결과에 그대로 남겼다. 전체 회귀 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_AllProjectJ_20261125/index.json`)
- 새 Editor 프로세스에서 실패 4건만 재실행해 동일 실패를 재현했다. 각각 시각 로드 예약 수, 매니 마스터 클래스, 저장 몽타주 순서, BP의 능력 소유권 기대값이다. 단독 재검증 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_FullFailuresIsolated_20261125/index.json`)
- `Run-LoopbackCohort.ps1`의 필드 보스·Iris·ThinClient·코어 격리와 메모리 4,096MiB 보호선을 사용했다. 연결·타격·체력 수신·종료 **24/24/24/24**, 세 구간 표본 각 24개, 서버·생성기 종료 코드 각 0이다. 30Hz 예산 통과, Idle p95 **4.688ms**, Burst p95/p99/최대 **5.165/5.446/7.071ms**, 33.3ms 초과 0틱이다. Burst 지연 p95는 요청→첫 검증 **529.804ms**, 첫 검증→권위 피해 **99.523ms**, 권위 피해→복제 수신 **134.318ms**다. 최저 물리 메모리 여유는 4,514MiB였다. 24인 Iris (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_Field24_Iris_20261125/verification.json`)
- 실제 메트릭 엔드포인트가 **24 CCU, 후보 25개, 허용 연결·객체 쌍 600개, 제거 0개**를 보고했다. 24명은 기본 상한 60 아래다. 이 실행은 실연결에서 필터가 동작했다는 근거이고, 상한 초과 정책은 500후보 집중 회귀에서 확인했다. Agones 루프백 응답기는 **준비 1회, 헬스 16회**를 받았다. 메트릭 원본 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_Field24_Iris_20261125/server-metrics.prom`) · SDK 호출 기록 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_AgonesLoopback_20261125/calls.json`)

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

시각 자산, 애니메이션과 어빌리티의 수정 대상은 실행 코드와 저장 에셋을 검증하는 테스트다. 저장 BP·AnimBP와 월드 에셋은 재저장하지 않았다. 실패 4건 집중 재검증 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_CloseoutFour_20261125/index.json`)에서 4/4, 경고 0건이다. 설치형 UE 5.8 직접 UBT 최종 빌드 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_CloseoutRuntimeBuildFinal_20261125/UBT.log`)가 종료 코드 0과 컴파일 오류·경고 각각 0건으로 완료됐고, 8모듈 DAG 검사는 20도메인·205기능을 통과했다.


최종 전체 RHI 자동 회귀 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_CloseoutAllProjectJ_20261125/index.json`)는 **589/589 통과, 실패·미실행 0건**, 프로세스 종료 코드 0이다. 583건은 경고 없이 성공했고 6건은 기존 경고와 함께 성공했다. Mover 시작 위치를 직접 바꾸는 `CombatContinuity` fixture 5개와 크기 제한에 걸린 HTTP 응답을 의도적으로 차단하는 `GatewayRealHTTP` 1개다. 새 서명 HTTP 왕복 확인과 이전 실패 4건도 전체 실행에서 통과했다.

### 운영 검증 연결

- 실제 Windows Nakama 3.25.0과 PostgreSQL 16.15를 각 실행용 DB와 루프백 포트에서 구동하고 SQL 001–012를 적용했다. 두 서비스 재시작 후 우편·아이템·골드·세션·복귀 nonce 검증을 실행했다. 동시 우편 수령은 `COMMITTED` 1건과 `CONFLICT` 1건이었다. 서버 전용 Nakama RPC에서 정원 1인 채널 두 개의 우선 채널·우회 채널·대기를 확인했고, 일반 로그인과 경유 예약이 같은 정원을 공유했다. 플레이어 토큰으로 호출한 서버 전용 경유 RPC는 거절됐다. [로컬 통합 실행기](../../../Infra/scripts/test-local-native-backend.ps1) · [Nakama 공식 릴리스](https://github.com/heroiclabs/nakama/releases/tag/v3.25.0)
- 종료 검증은 로컬 로그인 시험 모드에 명시적으로 활성화한다. PostgreSQL로 입장한 접속자의 캐릭터 점유, 헬스 200→503, 서버 강제 연결 종료, 권위 로그아웃, Outbox 정착과 정상 프로세스 종료를 각각 확인한다.
- Handover HTTP에서는 실제 전송기의 서명 헤더와 원본 POST 바이트를 루프백 시험 응답기가 검증하도록 했다. 전송 ID·바이너리 본문·64비트 틱 시각·도착 노드가 정확히 전달되어야 성공을 반환한다. 별도의 HandoverManager 회귀가 실제 도착 액터에 같은 이관을 두 번 반영하지 않는 계약을 확인한다. 운영용 HTTP 수신 라우트와 신뢰된 액터 해석은 서버 배포 단계에서 연결해야 한다.
- 루프백 전투 하네스가 메트릭 HTTP 본문을 파일로 보존할 때 PowerShell의 추가 CRLF를 제거했다. 공식 Prometheus의 config 검사를 사용한다. 기존 메트릭 이름과 대시보드 호환을 위해 `projectj_server_tick_ms{quantile=...}` 형식은 유지하며, `promtool` 린트에 표시되는 기존 단위·라벨 관례는 별도 기록한다. [Prometheus 3.2.1 릴리스](https://github.com/prometheus/prometheus/releases/tag/v3.2.1)
- Fleet과 FleetAutoscaler를 운영 가이드가 고정한 Agones 1.47.0의 공식 CRD와 대조했다. 두 매니페스트의 스키마를 통과했다. Windows에서 Docker·Podman·kubectl과 등록된 WSL 배포판을 확인했으며 사용 가능한 항목이 없었다. 스테이징 LinuxServer 바이너리도 없었다. 스키마 검사 결과 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_LocalRuntimeTools_20261125/AgonesVerification.log`) · [Agones 1.47.0 공식 CRD](https://raw.githubusercontent.com/googleforgames/agones/release-1.47.0/install/yaml/install.yaml)

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

로컬 통합 영수증 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_CloseoutNativeOperationsRetry_20261125/NativeVerification.json`) · 드레인 이벤트 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_CloseoutNativeOperationsRetry_20261125/DrainEvents.log`) · 실제 Prometheus 조회 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_CloseoutNativeOperationsRetry_20261125/PrometheusLive.json`) · 메트릭 린트 (로컬 `Saved/Validation/Continuation_20261014/PhaseAD_LocalRuntimeTools_20261125/PromtoolMetricLint.log`)

**최종 판정.** 이전 실패 4건을 해소했고 Windows 로컬 회귀·온라인 운영 흐름을 완료했다. 유료 인프라나 콘텐츠 빌드·쿠킹 없이 검증했다. 실제 배포에는 Nakama 주소·인증 키·채널 광고 주소, PostgreSQL 및 Outbox 영속 볼륨을 연결해야 한다. Linux는 위 SQLite VFS 제약을 풀고 SIGTERM·Agones Pod·PgBouncer/선택 Go·Valkey를 실행 검증해야 한다. 운영 Handover 수신 라우트와 분리 호스트 100~200인 교전도 별도 배포 검증 항목이다.

이번 실행의 원본 증거 13개를 SHA-256으로 검증해 기존 중앙 보관소에 등록하고 문서의 원래 링크를 유지했다.

## 온라인 게임플레이 후속 변경

| 경로 | 변경된 계약 |
| --- | --- |
| 퀘스트 수령·재접속 | 서버의 보상 정의와 레벨 곡선을 적용해 인벤토리·XP·레벨·완료 상태·영수증을 PostgreSQL 한 트랜잭션에서 확정한다. 128개 제한은 활성 퀘스트에 적용한다. 완료 이력은 캐릭터 세션을 확인한 뒤 최신순으로 32개씩 읽는다. [마이그레이션 013](../../../Infra/migrations/013_online_quest_rewards.sql) |
| 필드 보스 처치·복구 | 처치 ID, 개인별 지급 키, 획득한 처치 퀘스트 진척과 쿨다운을 기존 SQLite 작업자에서 함께 커밋한다. 커밋 뒤 서버가 중단돼도 같은 파일로 재시도할 수 있다. 체력 0부터 첫 디스크 커밋까지의 프로세스 유실과 백엔드에서 개별 명령이 재시도 한도를 넘은 경우는 별도 복구 경계다. [Outbox 저장 코드](../../../Source/Project_J/Backend/Project_JOnlineOutboxStore.cpp) · [트랜잭셔널 아웃박스 패턴](https://docs.aws.amazon.com/prescriptive-guidance/latest/cloud-design-patterns/transactional-outbox.html) |
| 입장과 드레인 | 온라인 입장은 Outbox 복구 완료와 유효한 백엔드 등록 응답을 요구한다. 인증된 플레이어의 첫 채널 선택·순번 조회·취소에 같은 FIFO를 적용한다. 드레인을 시작한 인스턴스는 지연된 하트비트로 입장을 재개할 수 없다. [마이그레이션 014](../../../Infra/migrations/014_player_channel_queue.sql) |
| 이동·군중 | 비행은 상승·급강하의 수직 속도를 포함해 스트리밍 범위를 예측한다. 속도 제한 판단에는 활성 Data Layer와 실제 공간 셀을 사용한다. 과부하 시 군중 제한의 하한이 원래 설정한 상한보다 커지는 경우를 수정했다. 시험에서는 파티가 다른 플레이어를 섞어 Iris 필터의 허용·제외 수를 확인한다. |
| 관측 | Prometheus에 실제 서버 틱 시작 간격의 누적 히스토그램, Outbox 접수 거절 누계, 가장 오래 대기한 활성 작업의 연령을 노출한다. 연령은 재시작 후 복구한 작업도 포함한다. 운영자는 같은 버킷으로 측정한 인스턴스들의 틱 분포를 합산할 수 있다. [Prometheus 히스토그램 집계](https://prometheus.io/docs/practices/histograms/) |

Linux 전용 서버 빌드는 SQLite의 표준 `unix` VFS를 요구한다. 설치형 Windows 로컬 검증 이후에도 소스 엔진 Linux 빌드, 볼륨 재연결, SIGTERM과 Agones Pod 교체가 남는다. 실제 두 호스트에서의 대규모 전투는 전용 실행기의 서버·생성기 역할을 각 장비에 배치해 측정한다. [로컬 실행 가이드](../../../Infra/deploy/agones/k3s-local-setup.md) · [SQLite VFS](https://www.sqlite.org/vfs.html)

#### Win64 실측

최종 Win64 Development Editor·Game 타깃을 direct UBT로 빌드했다. UE RHI 집중 회귀는 20/20, 전체 회귀는 591/591을 통과했다. 전체에서 585건은 경고 없이 성공했고 이전 실행과 같은 CombatContinuity 5건·GatewayRealHTTP 1건이 경고와 함께 성공했다. 실패·미실행 0건, 종료 코드 0이다. SQLite 회귀는 200명분의 보상과 퀘스트 진척 1건을 한 처치로 구성해 쓰기 실패 후 롤백, 재시도, 재기동 후 원래 키로 201건 송신 및 중복 확정을 확인했다. 구조 검사(8모듈)와 Python 77건도 통과했다. 집중 테스트 (로컬 `Saved/Validation/Continuation_20261014/GameplayArchitectureFocusedFinal_20261125/index.json`) · 전체 결과 (로컬 `Saved/Validation/Continuation_20261014/GameplayArchitectureAll_20261125/index.json`)

- PostgreSQL 16.15·Nakama·실제 UE 서버와 클라이언트에서 대기 2번→입장, 우편 수령, 보상 물약 2개와 누적 XP 40, 이전 세션의 완료 이력 복제를 확인했다. 같은 캐릭터가 샌드박스에서 채집 2회와 제작 1회를 수행해 보유 물약이 2개에서 3개가 됐다. 필드 보스의 서버 권위 피해로 HP를 0으로 만든 뒤 개인 보상 영수증이 한 건 늘었고, 던전 이동·별도 보스 보상·출발점 복귀·접속자 드레인까지 완료했다. 처치 시험의 마지막 피해는 시험이 주입했다. 다인 클라이언트 공격 입력은 아래 12인 시험에서 다뤘다. 온라인 통합 결과 (로컬 `Saved/Validation/Continuation_20261014/ArchitectureGameplayIntegrated_20261125/NativeVerification.json`) · 대기 순번 실화면 (로컬 `Saved/Validation/Continuation_20261014/ArchitectureGameplayIntegrated_20261125/LoginQueue.png`)
- 같은 PC의 서버와 생성기에 CPU 코어를 나눠 2인 파티 6개가 필드 보스를 공격했다. 12명 모두 타격·체력 복제·정상 종료를 완료했다. 제한 4에서 필터는 추적 객체 13개, 허용 84쌍, 제외 72쌍을 보고했다. 교전 구간 151틱의 처리 시간 p95는 3.411ms, 실제 틱 시작 간격 p95는 34.010ms였다. 타격 창까지 p95 501.246ms, 창에서 서버 피해 확정까지 101.609ms, 피해 복제까지 89.128ms였다. 마지막 값은 이 단일 호스트의 공통 시계를 사용했다. 12인 실행 결과 (로컬 `Saved/Validation/Continuation_20261014/GameplayArchitectureMixed12_20261125/verification.json`)
- D3D12 1280×720 Development 패키지의 MainWorld에서 네이티브 Mover가 572m를 비행했다. 최고 속도 33.0m/s, 최대 상승 9.6m/s, 하강 22.0m/s, 누적 회전 175.8도였다. Loaded와 Activated 소스를 함께 관측한 표본은 123개, 양쪽 모두 수직 예측이 나타난 표본은 53개였다. 프레임 p95/p99는 13.62/14.86ms였다. 실행 도중 Landscape 렌더링 PSO 사전 준비 ensure와 셀 컴포넌트 등록 702ms 로그가 있어 이 경로의 순간 멈춤 문제는 남아 있다. 텍스처·HLOD의 시각적 팝인과 실제 감속 발동은 이 수치로 확정할 수 없다. 비행 측정 (로컬 `Saved/Validation/Continuation_20261014/GameplayArchitecturePackagedFlight/flight.json`) · 패키지 실행 로그 (로컬 `Saved/Validation/Continuation_20261014/GameplayArchitecturePackagedFlight/Run.log`)

#### 패키지 재측정과 엔진 버전

온라인 시험 한 차례에서 Editor가 월드를 열기 전 `UObject::OnGetPreviewPlatform` 델리게이트에 대한 게임 스레드 쓰기와 정적 메시 충돌 데이터 작업자의 읽기가 겹쳐 엔진의 스레드 접근 검사가 발생했다. 새 프로세스에서 전체 온라인 흐름을 다시 통과했지만 설치형 Editor의 해당 경쟁 상태는 남아 있다.

패키지에서 관측한 Landscape PSO 호출 스택은 설치된 UE 5.8.4(CL 58902052)의 재등록 결함과 일치한다. 등록 해제 때 준비 상태를 초기화한 Landscape가 기존 셀로 돌아오면서 준비 요청 없이 렌더 프록시를 다시 만든다. Epic의 수정 대상은 5.8.5다. 수정이 포함된 엔진에서 셀을 내렸다가 GC 전에 재진입해 최대 프레임 시간과 ensure를 다시 확인해야 한다. [Epic UE-404796](https://issues.unrealengine.com/issue/UE-404796)

최종 실행 파일 해시를 대조한 패키지에서 같은 경로를 반복했다. 572m 비행 중 관측한 프레임 3,060개의 p95/p99는 11.24/15.75ms, 최대는 702.46ms였다. 50ms 초과가 2회, 100ms 초과가 1회였다. 소비한 Mover 입력 표본 124개 중 속도 제한이 적용된 표본은 0개였다. 이 경로에서 텍스처·HLOD가 항상 준비된다는 보장은 없다. 같은 PC에서 분리 실행기 6인도 완료해 제한 2에서 허용 30쌍·제외 12쌍을 확인했다. 재측정 (로컬 `Saved/Validation/Continuation_20261014/GameplayArchitecturePackagedFlightTail/flight.json`) · 분리 실행기 로컬 결과 (로컬 `Saved/Validation/Continuation_20261014/SplitHostCapSmoke_20261125_SplitHost/verification.json`)

#### 콘텐츠 제작과 병렬 처리의 적용 범위

이 목록은 후속 수정 전의 상태다. B01, 온라인 차단, 필드 보상 자격의 변경 결과는 아래 후속 점검에서 이어진다.

- MainWorld B01·B02는 현재 `SPATIAL_BLOCKOUT`이며 NPC 클래스와 자동 보상이 비어 있다. 이번 필드 보스 시험의 제작 위치는 MMO 샌드박스다. 보스 일정·채널 간 분산을 설계할 때 ArenaNet의 월드 보스 운영 사례를 참고할 수 있다. 한 장소의 동시 교전 수용량은 그 장소에서 별도로 측정해야 한다. [MainWorld 제작 원본](../../../Content/World/MainWorld/Source/MainWorld.bosses.json) · [ArenaNet 월드 보스 사례](https://www.guildwars2.com/en/news/the-megaserver-system-world-bosses-and-events/)
- 원격 캐릭터 애니메이션 예산과 긴급 포즈 갱신은 캐릭터 실행 경로에 연결돼 있다. Mass 등록과 VAT 표시, 범용 투사체의 병렬 탄도 계산은 구현돼 있으나 일반 제작 콘텐츠에서의 사용 범위는 확인이 필요하다. 특히 현재 Mover NPC는 Mass 전환 대상에서 제외된다. 성능 비교에는 화면 안의 전투원 수, 필수 포즈·경고, 서버와 클라이언트의 처리 시간을 함께 기록한다. [Mass 전환 정책](../../../Source/Project_JCharacter/Private/Mass/Project_JMassRepresentationSubsystem.cpp) · [Epic의 City Sample 군중 표현](https://dev.epicgames.com/documentation/unreal-engine/city-sample-project-unreal-engine-demonstration)
- 채집·제작·시스템 우편은 온라인 흐름이 있다. 경매 등록·구매, 길드 창고 출금, 차단은 온라인 브리지에서 보류 응답을 반환한다. 하우징과 항해도 별도 콘텐츠 제작 범위다. 채널을 넘어 상태를 변경하는 기능을 추가할 때는 어떤 서버가 원본을 갱신하는지, 복제본의 지연을 어디까지 허용하는지 먼저 정한다. [현재 온라인 브리지](../../../Source/Project_J/Network/Project_JMMOClientBridgeComponent.cpp) · [USENIX Colyseus의 원본 상태와 관심 영역](https://www.usenix.org/conference/nsdi-06/colyseus-distributed-architecture-online-multiplayer-games)
- 보스 보상은 같은 월드에 있는 파티원에게 참가 자격을 확장한다. 거리·지원 행동을 기준으로 보상을 차등화할 콘텐츠에서는 파티원 이탈과 원거리 치유를 포함해 자격을 검증해야 한다. [현재 수령자 계산](../../../Source/Project_J/World/Project_JWorldEncounter.cpp)

## 제작·런타임 적용 후속 점검

이 절은 위 감사 뒤에 수정한 작업 트리를 기준으로 한다. [콘텐츠 카탈로그](../Extensions/MMO_Content_Catalog.md)의 20개 도메인·205개 항목에는 각 기능의 상태 소유자와 의존성이 있다. 카탈로그 검사와 실플레이의 검증 단위는 다르므로 아래에 실제 진입점과 확인 경계를 함께 기록한다.

NPC 대상 평가의 병렬 실행 단위는 액터에서 읽은 값 스냅샷이다. `UE::Tasks` 워커가 계산하고, 배치 크기와 실행 정책에 따라 `ParallelFor`를 사용한다. 게임 스레드는 완료된 결과만 가져오고, 프레임당 반영 수와 전체 작업 수를 제한하며 월드 세대·소유자 수명·만료 여부를 확인한다. 회귀는 직렬·병렬의 대상 선택 및 순서 동등성을 확인한다. 실제로 화면에 보이는 전투원의 포즈나 서버 틱이 얼마나 좋아졌는지는 별도 부하 측정이 필요하다. [작업 수명 관리](../../../Source/Project_JCore/Private/System/Project_JTargetScoringSubsystem.cpp) · [배치 점수 계산](../../../Source/Project_JCore/Private/Optimization/Project_JTargetScoring.cpp) · [Epic Tasks System](https://dev.epicgames.com/documentation/en-us/unreal-engine/tasks-systems-in-unreal-engine)

| 영역 | 현재 사용하는 경로와 검증 경계 |
| --- | --- |
| NPC·AI·전투 병렬화 | 제작된 전투 조우가 NPC 활성화, 의사결정, 대상 점수 작업까지 연결된다. 공간 질의는 기존의 팀·거리 조건과 ID 순서를 유지하며 결과가 이미 정렬된 경우 중복 정렬을 건너뛴다. 서로 다른 셀이나 큰 반경에서 순서가 뒤집히면 기존 정렬을 수행한다. 실제 교전 프레임 절감량은 별도 측정 대상이다. |
| 캐릭터·군중·이동 | 원격 애니메이션 예산, 긴급 포즈, Cloth 제어는 실제 캐릭터에 연결된다. 애니메이션 중요도는 자체 카메라 평가를 소비한다. SignificanceManager 등록만으로 매 프레임 평가된다고 판단할 수 없다. 현재 Mover NPC는 Mass 강등 조건에서 제외된다. VAT 표현과 범용 투사체 생성은 일반 제작 콘텐츠에서의 소비가 미확인이다. [Epic 애니메이션 예산](https://dev.epicgames.com/documentation/en-us/unreal-engine/animation-budget-allocator-in-unreal-engine) |
| 복제·서버 운영 | Iris 군중 필터와 파티 보호는 위 혼합 파티 시험에서 확인했다. 실제 서버 틱 간격과 작업 시간이 서로 다른 지표로 노출된다. 분리 호스트 100~200인, Linux 서버 프로세스·볼륨·Agones 교체와 운영 핸드오버 수신은 추가 검증 범위다. |
| 월드·시각 효과·오디오 | 비행과 WP 셀 재등록은 패키지에서 측정한다. 식생 제작 검증은 MainWorld의 지정 제작 태그·해당 HLOD 참여 컴포넌트·실제 WPO 재질에 한정한다. 전투 Niagara의 경고 수명과 렌더 예산, 실제 발소리를 재생하는 Foley와 범용 오디오 예산은 별개로 확인해야 한다. 전체 숲·모든 스킬·음원에서의 품질은 전수 검증되지 않았다. |
| 전리품·퀘스트·사회 관계 | 직접 기여자의 처치 자격을 유지하고 파티 공유에는 처치 지점과 실제 파티원 위치를 사용한다. 기본 반경은 60m로 제작자가 조정할 수 있다. 온라인 차단·해제는 캐릭터 세션과 관계 버전으로 보호한다. 재접속·서버 이동 때 서버가 개인 차단 목록을 읽고 채팅 전달 때 확인한다. 차단·해제 응답을 잃으면 미확정 차단은 보수적으로 유지한다. |
| 경제·생활·확장 콘텐츠 | 우편·채집·제작·던전 왕복은 기존 온라인 증거가 있다. 경매와 직거래 원장 시험, 길드 창고의 로컬 계약, 실제 온라인 UI·운영은 서로 다른 단계다. 경매 등록·구매와 길드 출금의 온라인 브리지는 여전히 보류 응답을 반환한다. 하우징·항해·대규모 PvP·크로스월드 커뮤니티의 제작 완료 및 수용량은 카탈로그만으로 판정할 수 없다. |

B01은 기존 보스·클리브·개인 보상 데이터를 재사용했다. 별도 에디터 프로세스에서 저장된 조우를 다시 열었고, D3D12 단독 PIE에서 접근 전 대기, 접근 후 생성, 경고 표시와 종료, 일반 공격 10회 처치, 개인 우편 2,645골드와 300초 재생성 대기 진입을 확인했다. 시험은 플레이어 위치와 체력을 준비했다. 실제로 300초 뒤 다시 생성되는 동작, 추적·퇴각, 다수 온라인 플레이어의 B01 처치는 이 실행의 범위 밖이다. B01 전투 결과 (로컬 `Saved/Validation/Continuation_20261014/MainWorldB01CombatFinal_20261125/PIE/verification.json`)

식생 검증을 실제 MainWorld의 외부 액터 `Nature_5_11`에 적용했다. 엔진의 저장 시 검사 경로에서 검증기 1회가 실행됐고, Instancing HLOD를 사용하는 가문비나무 컴포넌트 1개에서 활성 바람 변형(WPO)의 중지 거리 0cm를 확인했다. 인스턴스 표시 거리와 WPO 중지 거리는 별도 값이다. 수종·품질 단계별 거리는 지상과 비행에서 바람 전환·그림자를 비교한 뒤 정해야 한다. 맵과 외부 액터의 검사 전후 해시는 일치했다. 엔진 판정과 원본 로그 위치 (로컬 `Saved/Validation/Continuation_20261014/MainWorldFoliageOFPA_20261125/engine-verification.json`)

Windows Cook 3,319개 패키지(오류·경고 0)와 로컬 스테이징을 마친 D3D12 실행 파일에서 같은 비행 경로를 새 프로세스 두 개로 순차 측정했다. UE 5.8.4의 프로젝트 측 호환 처리로 MainWorld 셀이 다시 활성화될 때 등록 전 Landscape 컴포넌트 4개의 PSO 준비 요청이 두 번 모두 실제로 복구됐다. 이전 실행의 `PSOPrecache.cpp:296` ensure는 두 로그에서 0건이었다. 엔진 버전, 맵, RHI, 셀 재등록 시점이 일치할 때만 적용한다. [Epic UE-404796](https://issues.unrealengine.com/issue/UE-404796)

| 패키지 비행 | 이동 거리 | 프레임 수 | p95 / p99 | 최대 | 50ms 초과 |
| --- | ---: | ---: | ---: | ---: | ---: |
| 이전 측정 (로컬 `Saved/Validation/Continuation_20261014/GameplayArchitecturePackagedFlightTail/flight.json`) | 572m | 3,060 | 11.24 / 15.75ms | 702.46ms | 2 |
| 수정 후 1차 (로컬 `Saved/Validation/Continuation_20261014/AppliedMMOPackagedFlight/flight.json`) | 578m | 3,242 | 10.68 / 11.11ms | 13.71ms | 0 |
| 수정 후 2차 (로컬 `Saved/Validation/Continuation_20261014/AppliedMMOPackagedFlightRepeat/flight.json`) | 578m | 3,229 | 10.82 / 11.48ms | 16.94ms | 0 |
| 최종 실행 파일 재스테이징 (로컬 `Saved/Validation/Continuation_20261014/AppliedMMOFinalFlight/flight.json`) | 578m | 3,255 | 10.67 / 11.08ms | 13.85ms | 0 |

수정 후 세 번의 실행에서 각각 이동 입력 표본 125개를 확인했고 속도 제한 개입은 0회였다. 수정 후 첫 실행에는 스트리밍 미완료 표본 1개가 있었다. 이 프레임 통계만으로 모든 에셋의 시각적 팝인이나 다른 하드웨어의 장시간 지연을 판정할 수 없다.

최종 스테이징은 같은 Cook 데이터와 UMG 채팅 노출까지 반영한 Game 실행 파일을 사용했다. 로컬 빌드와 패키지 바이너리의 해시 일치를 확인하고 다시 실행해 같은 셀의 컴포넌트 4개 준비 요청과 해당 ensure 0건을 확인했다.

별도 로컬 시험에서는 전용 서버와 생성기를 분리된 CPU 코어에 두고, 샌드박스 필드 보스에 혼합 파티 12명이 접속해 12명의 타격과 정상 종료를 확인했다. Iris 제한 4에서 연결·객체 조합 84쌍을 허용하고 72쌍을 제외했다. 공격 집중 구간 151틱의 월드 처리 p95는 3.262ms, 실제 틱 간격 p95는 34.008ms였다. 입력부터 응답까지 p95 597.909ms에는 공격 입력부터 타격 창이 열릴 때까지의 495.977ms가 포함된다. 한 호스트의 경량 클라이언트 시험이며, B01 제작 맵에서 렌더링하는 100~200인의 성능은 별도 측정이 필요하다. 12인 필드 결과 (로컬 `Saved/Validation/Continuation_20261014/AppliedMMOField12_20261125/verification.json`)

온라인 차단 목록을 사용하는 버전은 DB 마이그레이션 015, 이를 호출하는 Nakama 모듈, 실시간 프로토콜 15의 UE 서버·클라이언트 조합이다. 다른 세션이 관계를 갱신한 경우 오래된 세션의 DB 쓰기를 거절하고, 서버가 `STALE_SESSION`을 받는 순간 해당 브리지에서 채팅 송수신을 중지한다. 월드 채팅의 전달 범위는 현재 UE 월드다. 다른 서버에서 새 로그인한 사실을 기존 서버의 연결에 즉시 푸시하는 기능과 크로스월드 채팅 전송은 별도 범위로 남아 있다. [관계 원장과 로그인 스냅샷](../../../Infra/migrations/015_chat_blocks.sql) · [실시간 프로토콜](../../../Source/Project_J/Project_J.h)

PostgreSQL·Nakama 재시작 뒤 차단 복구 및 이전 세션 쓰기 거절을 확인했다. 같은 월드에 로그인한 두 UE 클라이언트 중 한 명이 차단 전·중·해제 후 한 번씩 메시지를 보냈을 때, 발신자는 자기 메시지 3개를 확인했고 상대는 첫 번째와 세 번째 메시지만 수신했다. 같은 통합 실행에서 실제 우편 수령과 인벤토리 복제, 퀘스트 아이템·경험치·완료 이력, 제작 맵의 채집·제작·보스, 두 서버 던전 왕복, 접속자 드레인도 통과했다. 채팅 전송·차단·해제와 수신 델리게이트는 UMG 제작자가 연결할 수 있다. 일반 플레이용 최종 채팅 창의 제작·사용성은 이 시험에 포함되지 않는다. 통합 실행 결과 (로컬 `Saved/Validation/Continuation_20261014/AppliedMMOIntegrated_20261125/NativeVerification.json`)

채널을 넘어 확장할 때는 두 가지 설계 결정을 구분한다. ArenaNet은 맵 인스턴스가 수요에 따라 열리고 닫혀도 각 맵의 월드 보스 시각을 맞추기 위해 공통 실시간 일정을 썼다. Project J에서도 B01을 서버 간 정시 이벤트로 바꾸려면 일정과 각 인스턴스의 처치·지급 기록을 따로 소유해야 한다. Nakama는 방·그룹·개인 채널과 기록 보존 옵션을 제공한다. Project J에 적용하려면 계정 단위 채널 구성원과 현재 캐릭터 단위 차단 목록을 연결하고, 월드를 바꾼 뒤에도 같은 수신 규칙을 검증해야 한다. [ArenaNet 제작 사례](https://www.guildwars2.com/en/news/the-megaserver-system-world-bosses-and-events/) · [Heroic Labs 채팅 설계](https://heroiclabs.com/docs/nakama/concepts/chat/)

프로토콜 15의 RHI 전체 회귀 592건이 통과했다(경고 없이 586, 경고 동반 6, 실패·미실행 0). 경고가 있는 항목은 전투 연속성 fixture 다섯 건의 Mover 위치 차이와 Gateway HTTP 제한 시험이다. 구조 검사는 프로젝트 모듈 8개의 비순환 의존성과 20개 도메인·205개 기능 정의를 확인했고 Python 회귀 77건을 통과했다. 기능 정의 수는 플레이 가능한 콘텐츠 수와 구별한다. 전체 RHI 결과 (로컬 `Saved/Validation/Continuation_20261014/AppliedMMOAll_20261125/index.json`)

마지막으로 채팅 발신 RPC에 BlueprintCallable을 노출한 뒤 Editor·Game을 직접 UBT로 빌드했다. 최종 빌드의 집중 RHI 회귀 21건이 경고·실패 없이 통과했다. 소셜 검사는 리플렉션에서 실제 발신 함수가 Blueprint 호출 가능·서버 전용 RPC·신뢰 전송 플래그를 모두 갖는지 확인한다. 최종 집중 회귀 (로컬 `Saved/Validation/Continuation_20261014/AppliedMMOFocused_20261125/index.json`)
