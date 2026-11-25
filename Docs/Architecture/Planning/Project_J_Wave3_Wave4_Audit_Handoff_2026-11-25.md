# Project J Wave 3·4 독립 감사 결과 인계

Project J의 Wave 3·4 독립 감사에서 확인한 결함, 수정 근거, 실측 결과를 전달한다. 저장소의 `AGENTS.md`와 [MMO 감사 후속 기록](Project_J_MMO_Audit_Followup_2026-11-24.md)을 먼저 읽고, 아래 결과를 실제 코드와 보고서에 대조해 줘. 먼저 결과와 남은 검증 범위를 검토하고, 추가 수정이나 실험이 필요하면 근거와 범위를 제안해 줘.

검증 기준은 설치형 UE 5.8, Direct UBT `Project_JEditor Win64 Development`, 로컬 Windows 환경이다. 8모듈 DAG와 전투 서버 단일 권위는 유지했다. 각 단계의 1차 결과와 독립 감사 결과, 그 뒤의 A-1~A-3 후속을 시간순으로 읽어 줘.

## Wave 3 · 보스 보상 큐, 그래픽 설정, 원격 전투 표현

| 우선순위 | 독립 감사에서 발견한 문제 | 수정 |
| --- | --- | --- |
| P1 | Gateway 설정 누락이나 포화로 완료가 동기 호출되면 Outbox 펌프에 재진입할 수 있었다. 배열 앞부분을 반복 제거하는 비용도 대량 처리에서 커졌다. | 게임 스레드 단일 펌프, 전송 시도별 일회 완료, 최대 64건 전송, 시각·접수 순서 기준 힙을 적용했다. 최초 1회와 재시도 5회로 시도 수를 제한한다. |
| P1 | 월드가 종료되면 접수된 보상도 사라질 수 있었다. ServicePort는 전송 전에 거절된 요청과 전송 뒤 결과가 불명확한 요청을 구분하기 어려웠다. | 보상 큐 수명을 GameInstance로 옮겼다. 전송 전 거절을 `PRE_DISPATCH_REJECTED`로 전달하고, 재시도에는 같은 멱등 키와 본문을 사용했다. Wave 3 시점의 큐는 프로세스 메모리에 있었다. |
| P1/P2 | VFX CVar에 `ECVF_Scalability`가 없어 UE가 실제 프리셋 적용을 거절했다. 이전 프로필의 `SetByGameSetting` 값이 프리셋을 덮을 수도 있었다. | CVar 등록을 수정하고 `bUseCustomOtherPlayerEffects`로 프리셋과 커스텀 소유권을 구분했다. Effects 행만 수정하면 커스텀을 유지하고, 전체 프리셋 전환 시 이전 오버라이드를 해제한다. 0.35/0.65/0.85 왕복과 프로세스 재시작도 검증했다. |
| P1 | UE의 최근 렌더 상태는 메시 컴포넌트별이다. 숨은 리더를 그대로 최적화하면 화면에 보이는 Follower가 갱신을 놓칠 수 있었다. | 전투 표현의 무기 컴포넌트 조회 5곳을 기존 수명 관리 캐시로 통합했다. 원격 `MontageTiming`은 당시 보이는 소스와 호환 정책에 한해 적용했다. 저장된 Spellblade Hidden Leader는 Wave 3에서 Full Pose 보호를 유지했다. 후속 A-2에서 이 경우를 별도로 연결했다. |

- 1차 회귀 **120/120**, 독립 감사 최종 고유 회귀 **225/225**. 최초 확장 실행에서 발견한 장비 효과 및 월드 종료 fixture의 잘못된 기대값·정리 순서를 수정해 재검증했다. Direct UBT 컴파일 오류·경고 0건.
- 합성 부하에서 2보스 보상 400건의 공유 64슬롯 페이싱, 과부하·타임아웃 재시도, 잘못된 요청의 즉시 실패를 확인했다. 별도 실제 ServicePort → Gateway → 로컬 HTTP 응답기에서는 전송 전 거절과 503 뒤 `REPLAYED`를 확인했다. PGlite에서는 실제 Nakama 보상 RPC가 만든 SQL을 실행해 DB 재시작 뒤 같은 우편 ID와 변경 본문의 `KEY_REUSED`를 확인했다.
- 독립 24인 Iris 시험은 연결·타격 확정·체력 수신·종료 각각 **24/24**. Idle 월드 작업 p95 **5.197ms**, Burst p95/p99/최대 **5.362/7.068/7.496ms**, 33.3ms 초과 0틱. 기존 타격 지연 p95 **627.513ms**는 서버 하네스 요청 발행부터 서버 피해 확정까지다. A-3에서 클라이언트 복제 지연을 별도로 계측했다.
- D3D12 관측을 포함한 별도 12인 네트워크 실행은 접속 전에 4,096MiB 메모리 보호선에 걸렸다. 단일 D3D12 24인 Spellblade 실행은 이동·동시 몽타주와 정상 종료를 확인했다. 이 단일 프로세스 결과는 원격 프록시 시각 품질의 근거로 사용하지 않는다.

**당시 판정**은 로컬 조건부 통과였다. 월드 종료 이후 큐는 유지됐으며, 서버 프로세스 종료 이후의 재시작 복구와 데드레터 재처리는 Wave 4의 우선 과제가 됐다.

## Wave 4 · SQLite 영속 Outbox, 크래시 복구, 재처리

기존 `FProject_JProfileDatabase` Win32 SQLite VFS에서 DELETE 롤백 저널, `synchronous=FULL`, `quick_check`를 사용한다. SQLite I/O는 단일 워커가 직렬화하고, **SQLite 커밋 뒤**에만 네트워크로 보낸다. 온라인 전용 서버에서 포트별 기본 DB를 쓰며 경로 우선순위는 CLI → INI → 기본 경로다.

| 우선순위 | 독립 감사에서 확인하고 고친 항목 |
| --- | --- |
| P1 | 혼합 SQLite 배치가 롤백돼도 확정 삭제·격리 작업과 용량 예약을 유지한다. 기존 정리만 재시도해 정리 실패에 따른 HTTP 재발송을 막는다. 새 작업의 SQLite 커밋이 실패하면 송신 전에 실패 처리한다. |
| P1 | 복구 중 중복 키가 들어오면 디스크의 서비스·작업·본문을 대조한다. 먼저 커밋된 내용이 권위를 가지며 격리 항목은 명시적으로 재처리한다. |
| P1 | 4,096건을 넘는 대기는 `created_seq` 기준으로 페이지 복구한다. 리드라이브 슬롯을 사전에 예약하고, 데드레터 16,384건 한도에서 기존 기록을 지우는 동작을 신규 접수 차단으로 바꿨다. |
| P1 | 저장소 경로를 GameInstance 수명 동안 고정하고, 종료 시 콜백 세대를 무효화한 뒤 워커와 최종 디스크 트랜잭션을 정리한다. 워커 미가용 또는 DB 오픈 실패에는 송신을 차단한다. |
| P1 | Win32 VFS 잠금과 DB 식별·무결성 검사로 외부 파일, 손상 DB와 동일 경로의 중복 열기를 거절한다. |
| P2 | 잘못된 리드라이브 콘솔 인자를 전체 재처리로 해석하던 동작을 고쳤다. 활성 키 중복은 기존 계약대로 접수를 거절하고 계측에 남긴다. |

- 1차 회귀 **125/125**, 독립 확장 RHI 회귀 **233/233**, 실패·미실행 0. 기존 Gateway 초과 응답 차단의 libcurl 경고와 군중 RHI 가상 예약 경고가 기록됐고, 후자는 단독 RHI 재시험에서 재현되지 않았다. Direct UBT 컴파일 오류·경고 0건.
- **실제 프로세스 크래시 시험**에서 파일 동기화 직후의 핫 저널을 감지하고 재오픈 시 혼합 배치 전체 롤백을 확인했다. 별도의 커밋 이후 크래시에서는 추가·삭제·격리가 보존됐다. 종료 코드는 **0/99/0/99/0**이며 각 재오픈은 `quick_check`를 통과했다. GT 콜백 대기 중 종료, 4,099건 페이지 복구, 부분 리드라이브, 포트/CLI/INI 경로 격리도 확인했다.
- SQLite → 실제 ServicePort → Gateway → 로컬 HTTP 응답기에서 커밋 후 송신·503·같은 키와 본문으로 재시도·`REPLAYED`·로컬 삭제·재오픈 잔여 0건을 확인했다. 실제 PostgreSQL/Nakama 전체 스택의 다중 보스 운영 부하는 별도다.
- 독립 24인 Iris 시험은 연결·타격 확정·체력 수신·종료 각각 **24/24**. Burst 월드 작업 p95/p99/최대 **5.436/6.399/6.439ms**, 33.3ms 초과 0틱. 보스 체력은 **25,000→24,760**이다. 이 코호트는 살아 있는 보스로 종료했고, 처치 보상 내구성은 별도 HTTP·DB 시험에서 검증했다.

**당시 판정**은 로컬 최적화 사이클 종결 가능이었다. `EnqueueDurableMutation()` 성공은 비동기 접수이며, 크래시 복구는 첫 SQLite 커밋부터 보장한다. 커밋 전 프로세스 손실이나 저장 볼륨 손실은 별도 운영 설계가 필요하다.

## 이후 적용한 A-1~A-3 · 현재 로컬 기준

1. **A-1 영속 쓰기 통합**. 서버가 획득한 온라인 퀘스트 진척과 `Dungeon.Clear`를 보스 보상과 같은 Outbox로 통합했다. 퀘스트는 캐릭터별 FIFO와 단조 리비전을 유지하고, 직접 편집은 현재 세션에 묶는다. 마이그레이션 011과 서버 전용 RPC는 이미 획득한 진척의 세션 종료 후 재생을 지원한다. 던전은 인스턴스 키·불변 보상 본문과 원 워커 리스 조건을 유지하며 만료로 확정할 수 없는 항목은 격리한다. **400캐릭터, 802건** 혼합 회귀와 별도 로컬 HTTP 503, PGlite 001–011을 통과했다.
2. **A-2 저장된 Spellblade Hidden Leader**. 카메라 갱신 다음에 실제 모듈러 파츠·부착 메시의 뷰와 최근 렌더 상태를 검사한다. 긴급 창, 가시 파츠 또는 미확인 뷰에선 Full Pose를 사용한다. 저장 BP의 가시성을 바꾸지 않은 PIE 회귀는 몽타주만 진행하는 경우, 첫 시야 진입 틱의 Full Pose, 종료 시 복원을 확인했다. 네트워크 없는 단일 D3D12 1280×720 24인 군중 공격 구간은 **중앙 127.95 FPS**, 프레임 p95 **10.738ms**였다. 이 수치는 원격 최적화의 절감량으로 귀속하지 않는다.
3. **A-3 전투 지연 3구간**. 기존 `strikeRoundTripMs`를 유지하고 동일 호스트의 Windows QPC로 세 구간을 분리했다. 최종 24인 Iris는 **24/24/24/24**, 30Hz 월드 틱 예산 통과, Burst p95 **5.489ms**, 33.3ms 초과 0틱이다. 3구간 모두 표본 24개를 확보했다.

| 최종 24인 Burst 지연, ms | p50 | p95 | p99 | 최대 |
| --- | ---: | ---: | ---: | ---: |
| 공격 요청 → 첫 유효 타격 검증 | 497.892 | 530.978 | 531.000 | 531.000 |
| 첫 유효 검증 → 서버 권위 피해 확정 | 99.560 | 99.574 | 99.599 | 99.599 |
| 피해 확정 → 클라이언트 체력 복제 수신 | 48.083 | 149.614 | 153.468 | 153.468 |

서버 권위 구간에는 첫 검증 뒤 접촉까지의 틱 대기가 포함된다. 호스트의 권위 판정과 실제 피해 처리 규칙은 유지했다. 최종 Direct UBT 컴파일 오류·경고 0건, 확장 회귀 **235/235** 통과(일반 성공 234, 경고 동반 성공 1), 8모듈 DAG 유지다.

## 교차 확인해 줬으면 하는 부분

1. Wave 3의 메모리 수명, Wave 4의 커밋 이후 내구성, A-1의 퀘스트·던전 통합이 현재 코드에서 각각 어떤 구간을 보장하는지 확인해 줘. SQLite 커밋 전 유실, 만료 워커 데드레터, 운영 볼륨 손실은 재시작 복구와 별개 경계로 봐 줘.
2. Wave 3 시점의 Spellblade Full Pose 보호와 A-2에서 추가한 Hidden Leader 경로를 구분해 줘. 실제 24접속과 D3D12 관측을 동시 구동한 최신 실험은 메모리 여유 때문에 보류했다. 첫 가시 프레임의 의상·무기·트레일 품질과 원격 FPS 절감량은 분리 호스트에서 확인해야 한다.
3. **실제 로컬 24인 교전, 합성 온라인 쓰기 부하, 로컬 HTTP·PGlite 검증**의 세 근거를 구분해 줘. 라이브 Nakama·PostgreSQL에서의 동시 보스 처치, 100~200인 반복 교전·채널 정원, 소스 엔진 Iris 슬롯 확장, 장시간 상주·이동은 운영 인프라 검증 범위다. 배포 시에는 PostgreSQL 011 → Nakama 모듈 → 게임 서버 순서를 확인해 줘.
4. 답변은 **확인된 변경과 근거**, **현재 코드와 문서가 어긋나는 부분**, **운영 단계에서 우선 확인할 항목** 순서로 정리해 줘. 결함이 있다면 P0/P1/P2와 소스 위치, 영향, 검증 방법을 함께 제시해 줘.

## 바로 볼 근거

- **Wave 3** — [독립 회귀 225건](../../../Saved/Validation/Continuation_20261014/MassiveScaleWave3IndependentFixtureRegression_20261125/Wave3RegressionSummary.json) · [24인 Iris](../../../Saved/Validation/Continuation_20261014/MassiveScaleWave3Independent_FieldIris_20261125/verification.json) · [그래픽 설정 재시작](../../../Saved/Validation/Continuation_20261014/MassiveScaleWave3Independent_GraphicsFinal_20261125/Restart.log).
- **Wave 4** — [확장 회귀 233건](../../../Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentExpanded_20261125/index.json) · [실제 크래시 테스트](../../../Saved/Validation/Continuation_20261014/MassiveScaleWave4IndependentNative_20261125/processes.json) · [24인 Iris](../../../Saved/Validation/Continuation_20261014/MassiveScaleWave4Independent_FieldIris_20261125/verification.json).
- **최종 후속** — [확장 회귀 235건](../../../Saved/Validation/Continuation_20261014/LocalFinalA123_Expanded_20261125/index.json) · [3구간 지연 원자료](../../../Saved/Validation/Continuation_20261014/LocalFinalA123_Field24_Iris_20261125/verification.json) · [단일 D3D12 24인](../../../Saved/Validation/WholeProjectAudit_20261013/LocalFinalA123_Rendered24_20261125/verification.json).
- **주요 구현** — [Outbox 워커](../../../Source/Project_J/Backend/Project_JOnlineWorldSubsystem.cpp) · [SQLite VFS 저장소](../../../Source/Project_J/Backend/Project_JOnlineOutboxStore.cpp) · [마이그레이션 011](../../../Infra/migrations/011_durable_quest_progress.sql) · [Hidden Leader](../../../Source/Project_JCharacter/Private/Animation/Project_JBudgetedSkeletalMeshComponent.cpp) · [전투 계측 하네스](../../../Source/Project_J/Testing/Project_JLoopbackCohort.Boss.cpp).

`Saved/` 자료는 로컬 산출물이다. 같은 워크스페이스를 사용하는 에이전트는 위 경로에서 원자료를 열 수 있다. 외부 환경에 전달할 때는 Markdown과 함께 필요한 검증 보고서만 첨부하면 된다.
