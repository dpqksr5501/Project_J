# Project J 전면 감사 기반 안정화(길 A) 및 핵심 시스템 청사진(길 B) 작업 계획 — 2026-11-22

> 기준: UE 5.8, 167차 필수 마감 이후 전체 8개 C++ 모듈·`Config/*.ini`·문서·20개 MMORPG 도메인(205개 확장 계약) 읽기 전용 실코드 감사 결과.\
> 이 문서는 **길 A(치명적 버그·최적화 구멍 수정 및 검증)**를 먼저 완료한 뒤 **길 B(부족한 핵심 시스템 상세 설계 청사진)**로 이어가는 실행 기준이다. 기존 167차까지의 검증 근거·보호 에셋·NPC non-MM 원칙은 유지한다.

---

## 1. 사용자 합의 아키텍처 방향

| 영역 | 확정 방향 | 근거 및 적용 단계 |
|---|---|---|
| **백엔드·영속성** | `UE 5.8 Dedicated Server`(실시간 월드·전투·물리) + **`Nakama` + `PostgreSQL` + `Redis(Valkey)`** (100% 오픈소스·무료) | 초기 Docker Compose 로컬 개발 → 클라우드 확장. 계정/인증/친구/차단/길드/파티/채팅/던전 매칭을 Nakama로 분리하고, 거래소·우편·화폐·영속 인벤토리는 PostgreSQL 복식부기 원장/ACID 트랜잭션으로 처리. 기존 `Project_JMMO`의 `IRepository`/`IdempotencyKey`와 `Project_JGatewaySubsystem` 어댑터 계약을 재사용한다. (**길 B 상세 설계**) |
| **오픈월드 분산** | **채널 기반 오픈월드 존 서버(UTC 거대 필드 보스) + 온디맨드 던전 인스턴스 서버** | 오픈월드(8×8km)는 채널당 UE Dedicated Server 1대 + `AProject_JWorldEncounter` UTC 스케줄 기반 필드 보스. 던전은 월드 입구 진입 또는 메뉴 UI 클릭 시 백엔드가 던전 인스턴스 서버를 배정해 `ClientTravel` → 클리어 후 원래 채널 좌표 복귀. (**길 B 상세 설계**) |
| **탈것(Mount)** | **장기: `Mover` 기반 통합** (`GroundMountMode`, `FlyingMountMode`, 예측 이착륙) | 현재 `Project_JMount`(CMC)와 플레이어(`Mover`) 이원화로 고무줄·하차 우회·Iris 누락이 발생. **길 A**에서 현재 비행/지상 탈것의 치명 버그(공중 영구 부유, 수면 착륙 오인, 활강 RPC 부재, 지연 관련성 `MOVE_None` 고착, Iris 누락)를 먼저 복구하고, **길 B**에서 Mover 통합 청사진을 설계한다. |
| **렌더링(VSM·RT·Substrate)** | **VSM 유지 + 10Hz 태양 회전 스냅 / 스켈레탈 RT 기본 OFF / Scalability 복원 완비 / Substrate 점진 통제** | VSM 캐시 무효화 원인을 제거하고, MMO 군중 프레임 방어를 위해 High/Epic에서도 `r.RayTracing.Geometry.SkeletalMeshes=0`, `r.MeshCardRepresentation.SkeletalMesh=0`을 기본으로 한다. `DefaultScalability.ini`의 `@2/@3/@Cine` CVar 복원 누락을 즉시 고친다. `r.Substrate` 전면 변경은 기존 에셋·머티리얼 호환성 확인 후 **길 B**에서 타깃 GPU 스펙과 함께 최종 결정한다. |
| **작업 순서** | **길 A(Phase 0: 치명적 버그·최적화 구멍 수정 + 빌드·테스트 검증)** → **길 B(Phase 1–3: 핵심 시스템 상세 설계 청사진)** | 기존 코드의 멀티플레이·보안·판정·최적화 구멍을 먼저 봉합·검증한 뒤, 부족한 시스템(고속 이동 스트리밍·원거리 식생·Dedicated Server 스트리핑·Nakama/DB·던전/필드보스·PvE AI/루팅/어그로·범용 전투 A09·군중 LOD/Mass A03·탈것 Mover·경제/소셜·UI/오디오/PSO/관측성/안티치트) 청사진을 설계한다. |

---

## 2. 길 A (Phase 0) — 치명적 버그 및 최적화 구멍 수정·검증 범위

기존 167차 검증 계약과 현재 워킹 트리의 기존 변경사항을 보존하면서, 실제 코드에서 확인된 아래 결함들을 원인 수준에서 수정하고 직접 UBT 빌드 + 집중 네이티브 회귀 + 신규 단위 테스트로 검증한다.

### 2.1 멀티플레이 영속성 · 보안 · 세션 · 도메인 결함
1. **멀티플레이 영속 프로필 `RequestId` 분리 (`Inventory` / `Equipment`)**: 원격 클라이언트가 생성한 임의 GUID를 `SubmitPersistentEdit`에 그대로 전달해 `OpenTrustedProfile` 서버에서 100% 거절되던 결함을 `CraftingJobs`와 동일한 패턴(클라이언트 상관 ID 보존 + 서버 `CreatePersistentRequest()`)으로 수정.
2. **RPC 보안 & `CommandBudget` 우회 차단**:
   - `UProject_JReplicatedAnimEventComponent`: `ServerDispatchMoveState` / `ServerDispatchEvent`에 `AdmitClientCommand` 적용 및 `int32` 시퀀스 오버플로 안전 비교(wrap-safe) 보강.
   - `AProject_JPlayerCharacter::ServerSetCombatIntroPresentation`: `AdmitClientCommand` 적용 및 동일 상태 중복 `ForceNetUpdate()` 차단.
   - `UProject_JCommandBudgetComponent`: 탑승 중(`UnPossessed`) 라이더 폰의 `GetPlayerState()==nullptr`로 예산이 우회되던 결함을 탑승 소유자(`MountComponent` / `AttachParent`) 추적으로 봉합.
3. **TrustedSession 폐기 시 `SocialSubsystem` 해제 및 권한·툼스톤 보강**:
   - `AProject_JGameMode::RetireSession`에서 `UProject_JSocialSubsystem::UnbindPlayerState(State)` 호출.
   - `UProject_JSocialSubsystem::ResolveAuthoritativeCharacter`에서 `CanAcceptSessionCommand()` 확인, 장기 운영 시 삭제된 그룹 툼스톤 누적 한도 고갈 방지(기존 스냅샷/툼스톤 계약 유지 범위 내 정리).
4. **`State_Dead` 사망 태그 파이프라인 최소 연결 & 사망자 탈것 소환 차단**:
   - `AProject_JBaseCharacter`: 권위 서버에서 `Health <= 0.0f` 시 `State_Dead` 복제 루즈 태그 부여, `Health > 0.0f` 복구 시 제거.
   - `AProject_JPlayerCharacter::UseMountItemAuthority`: `IsDead()`/`State_Dead` 검사 및 비동기 로드 완료 콜백 내 세션·생존 재검증.
5. **기타 도메인/백엔드/컴포넌트 결함**:
   - `UProject_JCharacterDataSubsystem`: `UProject_JMountItemDefinition` PrimaryAsset 스캔 추가.
   - `UProject_JGatewaySubsystem`: 연결 실패 메시지(`Connection Failed`)가 `Buffer->Read()`로 덮어써지던 버그 및 URL `/` 결합 정규화 수정.
   - `UProject_JCompanionComponent`: `BeginPlay()` ASC null 가드, `UseCompanionAuthority()` InventoryComponent null 가드, `CompletePreparation()` 취소 시 `RetireLease()` 누락 및 중복 `UProject_JNPCActionComponent` 생성 수정.
   - `AProject_JPlayerCharacter::TryApplyHandoverSnapshot`: `CastChecked` → null-safe `Cast`, 레벨 적용 사전 검증으로 부분 적용 오염 방지.
   - `AProject_JWorldEncounter`: `Active` 상태 `!IsValid(CurrentNPC)` 영구 교착 복구, 캡슐 반높이 기반 스폰 오프셋, 과잉 `NetCullDistance` 보정.
   - `UProject_JEquipmentRuntimeComponent`: `Instant` GE 건너뜀 뒤 스탯 폴백 중복 실행 버그 및 `ItemLevel` 미반영 수정.
   - `AProject_JBaseCharacter::InitializeAbilitySystem`: 클라이언트 조기 반환 시 `BoundProgression` 구독 누락 방지.
   - `UProject_JGameplayAbility_Dash` / `UProject_JGameplayAbility_NPCAttack`: `CreatePlayMontageAndWaitProxy` null 가드 추가.
   - `UProject_JNetObjectPrioritizer_Combat`: 사망자 우선순위 감쇠 `FMath::Max(1.0, 0.25)` 논리 버그 수정.
   - `UProject_JInputLeaseSubsystem` / `UProject_JSkillInputRouterComponent`: IMC 우선순위 지원(기본값 0 하위호환) + 특정 컨텍스트 해제, ModifierTag 참조 카운팅.

### 2.2 전투 판정(SSR) · 애니메이션 스레드 안전성 · 최적화 구멍 복구
6. **SSR 저지연 33ms 샘플링 갭 보간 & 되감기 좌표 기반 LOS 정합성**:
   - `UProject_JServerSideRewindComponent`: `Time > Newest`이지만 현재 서버 시각 이하의 최신 틱 갭 이내일 때 최신 기록과 현재 라이브 캡슐 사이를 보간해 저지연(0~33ms) 정상 타격 오거부 해결.
   - `UProject_JCombatHitValidationComponent`: SSR 검증 시 되감긴 타겟 위치 기준으로 LOS 및 거리 정합성 보강(기존 162/167차 테스트 계약 보존).
7. **`USignificanceManager` 방향 반전 수정 및 월드 틱 구동 연결**:
   - `AProject_JBaseCharacter`: `SignificanceFunc`를 UE 표준 규약(가까울수록 높은 값, `double` 거리 비교)으로 수정하고 `AProject_JNPCCharacter::GetNPCUpdateBudgetTier`와 일치시킴.
   - `UProject_JCharacterAnimationBudgetSubsystem`: 기존 월드 패스의 수집된 뷰포인트를 재사용해 `USignificanceManager::Update()`를 월드당 1회 구동.
8. **애니메이션 워커 스레드 안전성 위반 3건 수정**:
   - `UProject_JPlayerAnimInstance`: 워커 스레드에서 `IsSlotActive`/`Blueprint_GetSlotMontageLocalWeight` 직접 호출 대신 GT 스냅샷(`ThreadSafeData.Montage`) 사용.
   - `FProject_JAnimNode_GuidedHandIK` / `FProject_JAnimNode_HandContactGoals`: `GetAnimInstanceObject()` UObject 직접 접근을 프록시/이중 버퍼 경로로 전환.
   - `FProject_JAnimNode_FootClearance`: `IsValidToEvaluate` 내 매 프레임 본 이름 재초기화 및 배열 뮤테이션 제거.
9. **Core/AI 비동기 서브시스템 누수·병목 수정**:
   - `UProject_JTargetScoringSubsystem`: 완료 태스크 정적 배열 잔류(`IsTickable`), 취소 작업의 `MaxCompletionsPerTick` 예산 소모, 완료 수집/신규 Launch 루프 기아 수정.
   - `UProject_JVisualAssetSubsystem`: `!FApp::CanEverRender()` 가드, 동기 캐시 히트 시 `InFlight` 과다 계상, 고아 그룹 타임아웃, 성공 시 `FailedUntil` 제거.
   - `UProject_JNPCPathSubsystem`: `Requests.Contains(R)` `O(N²)` 제거.

### 2.3 오픈월드 · 비행 탈것 · 렌더링 · UI · 에디터 치명 결함
10. **렌더링/VSM/Scalability 복구**:
    - `AProject_JWorldEnvironment`: 10Hz 연속 태양 회전 → 각도 임계치 스냅으로 VSM 정적 그림자 페이지 상시 무효화 제거 + `Fog`/`MPC` null 가드.
    - `Config/DefaultScalability.ini`: `@2/@3/@Cine` CVar 복원 완비 + High/Epic 스켈레탈 RT(`r.RayTracing.Geometry.SkeletalMeshes=0`, `r.MeshCardRepresentation.SkeletalMesh=0`) 기본 OFF로 MMO 군중 프레임 방어.
11. **월드 스트리밍 · 포탈 도착 안정화**:
    - `UProject_JWorldStreamingComponent`: `FindArrivalPlacement` 점유 폰 회피 나선 오프셋 탐색 추가, 정지 시 불필요한 600m 추가 예측 구 방출 억제, `MaxArrivalCells` 안전 상향.
12. **비행·지상 탈것 치명 버그 복구 (Mover 통합 전 안정화)**:
    - `AProject_JFlyingMountCharacter`: `DismountRider`/`HandleHealthDepleted` 오버라이드로 공중 사망·강제 하차 시 영구 부유 및 서버 틱 누수 제거, `ECC_Visibility` → 충돌 채널 기반 이착륙 트레이스(수면 착륙 오인 제거), `ServerSetGliding` RPC 추가.
    - `AProject_JMountCharacter`: 지연 관련성(Late Relevancy) 클라이언트 하차 시 `MOVE_None` 고착 폴백 수정.
    - `Config/DefaultEngine.ini`: Iris `ProjectJPlayerMover` 우선순위에 `Project_JMountCharacter` 등록.
13. **UI 핫패스 & 에디터 모듈 결함 수정**:
    - `UProject_JPlayerHUDWidget` / `UProject_JPlayerUIComponent`: 버프/디버프 위젯 재사용(`ClearChildren`+`CreateWidget` 제거), HP/MP 틱마다 전체 레이아웃 `ApplyPreferences` 재호출 분리, 대시 중 루트 전체 `Invalidate(Paint)` 제거, `"Lv.{0}"` 포맷 수정.
    - `Project_JCharacterEditorModule`: `!IsRunningCommandlet()` 가드 확장(커맨드렛/비사용 시 매틱 `TObjectIterator` 스캔 제거) 및 `FAnimationViewportClient` 안전 검사.

### 2.4 검증 기준 및 길 A 완료 결과 (2026-11-22 완료)

- **직접 UBT 빌드 (오류 0)**:
  - `Project_JEditor Win64 Development`: 성공(종료 코드 0, 최종 증분 5.99초, `WriteMetadata Project_JEditor.target` 완료).
  - `Project_J Win64 Development` (Game 타깃): 성공(종료 코드 0, 115 액션 101.71초, `Binaries/Win64/Project_J.exe` 출력).
- **집중 네이티브 회귀 검증 (`Phase0Hardening_Run3`)**:
  - 실행 근거: `Saved/Validation/Phase0Hardening_20261122/Phase0Hardening_Run3/verification.json`
  - 결과: **`passed: true`, `exitCode: 0`, `succeeded: 140`, `succeededWithWarnings: 1`, `errors: 0`, `missing: []`** (유일한 경고 1건은 의도적 연결 실패 음성 대조 테스트인 `ProjectJ.MMO.Architecture.GatewayRealHTTP`).
  - 신규 Phase 0 단위 테스트 6종 전부 오류·경고 0 통과: `DeadStateTag`, `CommandBudgetMounted`, `CombatPrioritizerDead`, `SSRLowLatencyGap`, `WorldEnvironmentSunSnap`, `FlyingMountForceStop`.
  - 추가 발견·수정 크래시 검증: Run1의 `ProjectJ.MMO.Persistence.RuntimeFrequencyAndProgression` 월드 종료 단계에서 발견된 `UProject_JMoverComponent::EndPlay()` → `CancelFeaturesWithTag()` → `ModeFSM` null 역참조(`0x420`) 크래시(비결정적 `TSet` 컴포넌트 순서로 `UMoverNetworkPredictionLiaisonComponent::EndPlay`가 먼저 `Simulation->Uninit()`을 호출한 뒤 발생)를 원인 수정하고, Run3에서 `RuntimeFrequencyAndProgression` 포함 140건 무크래시 통과 확인.
- **아키텍처/모듈 DAG 검증**:
  - `python Scripts/Validation/Validate-MMOArchitecture.py`: 통과 (`passed: true`, `features: 205`, `domains: 20`, 8개 모듈 DAG 유효).
- **기존 계약 보존 및 의도적 유지/이관 항목**:
  - `USignificanceManager` 값 방향 반전 및 `AnimationBudgetSubsystem::Refresh()` 내 자동 `Update()` 주입은 기존 `Project_JAnimationObserverTests.cpp`(관측이 `CurrentSignificance`를 덮어쓰지 않고 레거시 티어 `2.f→Far` 매핑 검증) 계약을 보존하기 위해 변경하지 않고 `double` 거리 비교 정밀도만 보강.
  - 정지 상태 `UProject_JWorldStreamingComponent::BuildPreparationShapes` 방출 억제 및 2단계 스트리밍은 기존 `Project_JWorldPreparationTests.cpp` 계약 보존을 위해 길 B(고속 이동 스트리밍 청사진)로 이관.
  - `UProject_JVisualAssetSubsystem`의 `!FApp::CanEverRender()` 차단은 `-NullRHI` 헤드리스 테스트 계약을 유지하기 위해 제외(고아 그룹 타임아웃·`InFlight` 과다 계상·`FailedUntil` 정리는 적용).
  - `UProject_JSocialSubsystem`의 삭제 툼스톤 이력 카운트는 기존 테스트 계약을 유지.
  - `Project_JAnimNode_GuidedHandIK` / `HandContactGoals` 및 `PlayerUIComponent` 레이아웃 세분화는 기존 구조 안정성 확인 후 길 B 범위로 정리.
- **기존 워킹 트리 선행/무관 실패 구분 및 미검증 경계**:
  - Run1/Run2에서 관측된 `ProjectJ.MMO.Persistence.ProgressAdmission`(기존 dirty 파일 `Project_JLocalProfileSubsystem.cpp`의 `IOGate` 상한 32→`MaxActiveBindings` 변경에 기인), `ProjectJ.Combat.WeaponSkeletalVisualLifecycle`, `ProjectJ.PlayerLocomotion.{MannyFoundation,MontageAuthoringIdempotence,PlayerMasterAssembly,PlayerMasterHierarchy,SnapshotPublicationAndReinitialize}`(이전 커밋 `6a0b55a5` 및 기존 대검 에셋 삭제 워킹 트리 상태)는 이번 길 A 수정과 무관한 선행 항목이므로 원복·은폐하지 않고 그대로 보존했다.
  - `Validate-DocLinks.mjs`는 현재 머신에 `node` CLI가 미설치되어 미실행 상태다.
  - 멀티 소켓 전용 서버 실전 와이어, Cook/Shipping 패키징, 실 RHI GPU/FPS/메모리 측정은 길 A 범위에 포함되지 않은 별도 검증 항목이다.
  - **길 A 완료 게이트 충족 → 다음 단계: 길 B(핵심 시스템 상세 설계 청사진 작성).**

---

## 3. 길 B — 핵심 시스템 상세 설계 청사진 & Phase 1–3 전체 시스템 구축 완료 (2026-11-23 완료)

길 A 검증 완료 후 마스터 인덱스 및 6개 영역의 구현 수준 상세 설계 청사진([청사진 마스터 인덱스](../Blueprints/README.md))을 완성했고, 사용자 승인에 따라 **라이브 운영 인프라(외부 클라우드 호스팅·Prometheus/Sentry 운영 서버·EAC 상용 SDK·24h 200봇 Soak)는 인터페이스/훅만 남기고 Phase 1 ~ Phase 3 전체 MMORPG 시스템 구축 및 로컬 자동 검증**을 완료했다.

### 3.1 구현 완료 요약 (Phase 1 ~ Phase 3)
- **Phase 1 (기반·서버·인프라·고속 스트리밍)**:
  - `Source/Project_JServer.Target.cs`, `Config/DefaultServerEngine.ini`, `Project_JVisualAssetSubsystem`의 `IsRunningDedicatedServer()` 스킵 보강.
  - `Infra/docker-compose.yml` (`PostgreSQL 16 + Valkey 8 + Nakama 3.25`), `Infra/migrations/001_mmo_core_schema.sql`, `Infra/nakama/modules/project_j_mmo.js`, `Infra/scripts/dev-reset.ps1`.
  - `UProject_JNakamaServicePortSubsystem` (`IServicePort` 어댑터 + 3-Mode 런타임: `StandaloneMock` / `LocalDockerNakama` / `DedicatedExternal`, 멱등 영수증·충돌 거절·`session_epoch` 펜싱).
  - `UProject_JCheatManager` (비-Shipping 권위 GM 명령: 텔레포트·체력회복·즉사·인공지연·백엔드 모드 전환·런타임 메트릭 덤프).
  - `Project_JWorldStreamingPolicy.h` + `UProject_JWorldStreamingComponent` 2단계(`Loaded`+`Activated`) 고속 예측 스트리밍·급선회 ±45° 확장·고도 `Grid_Detail` 억제·언로드 히스테리시스·미준비 전방 셀 소프트 감속 + `Config/DefaultEngine.ini` PSO Precaching / 스트리밍 시간분할 / VSM `StaticSeparate` / `AudioMaxChannels=48`.
- **Phase 2 (탈것 Mover·동적 카메라·던전/채널·전투 A09·군중 Mass A03·PvE 코어)**:
  - `UProject_JMountMoverComponent` + `UProject_JGroundMountMode` + `UProject_JFlyingMountMode`(3D 에너지 비행 급강하/상승/활강/실속방지/캡슐채널 안전착지) + `AProject_JMountMoverPawn` + Iris `ProjectJPlayerMover` 등록.
  - `UProject_JCameraPolicyAsset` (동적 액션 카메라 프리셋: `OnFoot`, `CombatSoftLock`, `GiantBossZoomOut`, `GroundMount`, `FlyingMountDive`).
  - `UProject_JDungeonInstanceSubsystem` (월드 입구 포탈 / 메뉴 UI 단일 통합 던전 진입, Warm Pool 우선 할당, 서명·단발 Nonce·TTL JoinTicket, 180초 튕김 재접속 유예, `ReturnAnchor` 만석 시 인접 채널 안전 복귀, 멱등 클리어 보상, 필드 보스 40인 초과 군중 스로틀 정책).
  - 범용 전투 `A09`: `Project_JCombatQueryTypes.h`(AoE Circle/Cone/Rect/Donut, 서버 클램프 Motion Warping, 원격 MM 거리별 LOD, 보스 필수 경고 보호 VFX 예산), `UProject_JProjectileManagerSubsystem`(2,048 액터 없는 풀링 투사체·다단히트 쿨다운), `UProject_JMultiPartHitComponent`(거대 보스 최대 8부위 30Hz 링버퍼 SSR), `UProject_JCCResistanceComponent`(15초 CC 점감 100→50→25→면역 + 보스 무력화 게이지/그로기).
  - 군중 `A03`: `UProject_JNPCPoolSubsystem`(3-Tier Mass↔Pooled Actor + 원거리 VAT/ISM 프록시, 전투/보스 강등 금지).
  - PvE 코어: `UProject_JThreatComponent` + `UProject_JNPCBrainDefinition`(무제한 어그로 맵, 근접 110%/원거리 130% 히스테리시스, 도발, 사망 시에도 유지되는 보스 기여도 맵, 리싱/이베이드, HP% 보스 페이즈 전이), `UProject_JDeathReviveSubsystem`(사망 행동 차단, 3경로 부활, 3초 무적, 180초 자동 부활, 던전 보스전 거점 부활 잠금), `UProject_JLootSubsystem`(서버 `FRandomStream` 개인 루팅, 30초 파티 주사위, 보스 기여도 멱등 개인 루팅 + 만석 시 `MAIL_ESCROW` 폴백).
- **Phase 3 (복식부기 경제·영속 소셜·성장/퀘스트/생활·안티치트·오디오·UI·에디터 밸리데이터)**:
  - `ProjectJ::MMO::FEconomyLedger` (`Project_JMMO` 순수 C++ 복식부기 원장 `SUM(delta)==0`, 멱등키, 단일 소유자 글로벌 `item_instance_id` CAS).
  - `UProject_JEconomySubsystem` (거래소 에스크로·검색·동시 구매 단일 승자·5% 수수료·만료 우편 반환, 1:1 개인 거래 2단계 Lock/Confirm·편집 시 양측 자동 잠금 해제, 우편 `MAIL_ESCROW` 만석 롤백 수령).
  - `UProject_JGuildAndSocialExtensionsSubsystem` (길드 창고 RBAC·일일 출금 한도·슬롯 리비전 낙관적 잠금·감사 로그, 친구/전역 차단, 채팅 채널 레이트 리밋, Presence).
  - `UProject_JProgressionAndLifeSubsystem` (레벨/XP 곡선·스탯 배율, 강화 안전구간 +0→+7 및 천장(Pity)·2/4세트 효과, 체인/일일·주간 UTC 멱등 반복 퀘스트, 개인 위상 vs 경쟁 자원 채집, 제작 대성공 확률).
  - `UProject_JServerIntegritySubsystem` + `IProject_JAntiCheatBridge` (1.15x 속도 엔벨로프·KillZ·포탈/대시 화이트리스트·전투 주기 위반 단계적 제재·경량 텔레메트리 스냅샷), `UProject_JAudioBudgetSubsystem`(48보이스 Concurrency·25m 타인 컬링·덕킹), `UProject_JUIWindowStackSubsystem`(모달 ESC 스택·게임패드/마우스·접근성 클램프), `Project_JCrossAssetValidator`(NPC 비-MM·수목 Impostor HLOD/WPO·`DefaultScalability.ini` 스켈레탈 RT OFF 검증), `Scripts/Validation/Diff-MemoryCensus.py`.

### 3.2 Phase 1–3 런타임 배선 · 스타터 에셋 · 최종 검증 결과 (2026-11-23 마감)
- **런타임 배선(Wiring) 완료**:
  - `UProject_JWorldPreloadStreamingComponent`(Stage 1 `Loaded`) + `UProject_JWorldStreamingComponent`(Stage 2 `Activated` + 히스테리시스)를 `AProject_JPlayerController`에 부착·실전 이동 직결.
  - `AProject_JMountMoverPawn`: `IAbilitySystemInterface`·`IProject_JInteractable`·`UProject_JMountAttributeSet`·`TryMountRider`/`DismountRider`·`ApplyMountDamage`(사망 시 비행 강제 정지+하차)·고삐 Hand IK 완비.
  - `UProject_JCameraComponent`: `UProject_JCameraPolicyAsset`(`GiantBossZoomOut`·`FlyingMountDive` 등) 틱 보간 직결.
  - `AProject_JWorldPortal`(`DungeonId` 진입)·`AProject_JGameMode::PreLogin`(`bRequireDungeonTicket` 서명 단발 티켓 검증)·`AProject_JWorldEncounter`(보스 처치 시 `ThreatComponent` 기여도 → `LootSubsystem` → `EconomySubsystem` 우편 폴백 직결).
  - `UProject_JMMOClientBridgeComponent`: 던전 메뉴·부활·거래소·우편·길드창고·차단·채팅·채집 서버 권위 RPC 브리지(`CommandBudget`+신뢰 세션+`State_Dead`+`ServerIntegrity` 게이트)를 `AProject_JPlayerController`에 부착.
  - `UProject_JCombatHitValidationComponent::ServerExecuteHitQuery` / `ApplyValidatedCC`(범용 AoE·보스 다부위 SSR·CC 점감/무력화 직결), `UProject_JPlayerAnimInstance::RemoteLocomotionLOD` GT 거리별 갱신, `UProject_JMassRepresentationSubsystem` ↔ `UProject_JNPCPoolSubsystem::SyncVATProxiesToISM` 직결, `UProject_JCrossAssetEditorValidator : UEditorValidatorBase` 에디터 자동 검증 등록.
  - 선행 테스트 2건 계약 정합: `ProjectJ.MMO.Persistence.ProgressAdmission`(`MaxActiveBindings=128` 정합) 및 `ProjectJ.Combat.WeaponSkeletalVisualLifecycle`(멀티-LOD `UpdateLODStatus` 틱 계약 정합) 통과 복구.
- **실물 스타터 UE 에셋 6종 생성·저장 (`Scripts/Editor/Build-Phase1To3StarterAssets.py` 실행 완료)**:
  - `Content/DataAssetSets/MMO/DA_WorldStreamingPolicy_Default.uasset`
  - `Content/DataAssetSets/MMO/DA_CameraPolicy_Default.uasset`
  - `Content/DataAssetSets/MMO/DA_LootTable_FieldBoss_Starter.uasset`
  - `Content/DataAssetSets/MMO/DA_NPCBrain_FieldBoss_Starter.uasset`
  - `Content/DataAssetSets/MMO/BP_MountMover_Starter.uasset`
  - `Content/World/MainWorld/HLOD/HLODLayer_Foliage_Impostor.uasset` (`HLODLayerType=Instancing`)
- **직접 UBT 빌드 (오류 0)**:
  - `Project_JEditor Win64 Development`: 성공 (종료 코드 0, 8개 모듈 링크 완료).
  - `Project_J Win64 Development` (Game 타깃): 성공 (종료 코드 0, 67.18초, `Binaries/Win64/Project_J.exe` 출력).
- **최종 통합 자동화 회귀 검증 (`WiringFinal_Run1`)**:
  - 근거: `Saved/Validation/WiringAndSystems_20261123/WiringFinal_Run1/verification.json`
  - 결과: **`passed: true`, `exitCode: 0`, `succeeded: 101`, `errors: 0`, `missing: []`** (`ProjectJ.Systems.Wiring.EndToEndRuntimeIntegration` 포함 신규 Phase 0~3 + 배선 통합 19종 및 `ProgressAdmission`·`WeaponSkeletalVisualLifecycle` 전부 **오류 0·경고 0 통과**).
- **아키텍처/스크립트 검증**:
  - `python Scripts/Validation/Validate-MMOArchitecture.py`: 통과 (`passed: true`, 205개 기능 / 20도메인 / 8모듈 DAG).
  - `python -m py_compile Scripts/Validation/Diff-MemoryCensus.py`: 통과.
- **호스트 환경 제약 / 별도 범위**:
  - Docker CLI 미설치 환경이므로 `Infra/docker-compose.yml` 실기동 E2E는 Docker Desktop 설치 후 수행한다.
  - Epic Launcher 설치형 UE 5.8에는 `UnrealServer-Core.dll` 서버 라이브러리가 없어 `Project_JServer.Target.cs` 독립 서버 링크는 소스 빌드 UE 5.8 전환 시 수행한다(현재는 `-server` 인자 전용 서버 실행 가능).
  - 8×8km 맵 내부 수동 레벨 배치(3-Tier 그리드 재배치·Impostor 아틀라스 베이크·Landscape RVT·와이어프레임 UMG 위젯 아트) 및 기존 대검 삭제 워킹 트리에 의존하는 `PlayerLocomotion` 5건은 콘텐츠/사용자 에셋 범위로 보존했다.

1. **[BP01 — 백엔드·영속성·Dedicated Server·던전/필드보스·경제/소셜](../Blueprints/BP01_Backend_Persistence_Dungeon_FieldBoss.md)**: `Project_JServer` 독립 타깃 및 에셋 스트리핑(`A08`), Docker `Nakama + PostgreSQL + Valkey`, 정규화 RDBMS·복식부기 원장·`session_epoch`/Outbox, 채널 오픈월드(UTC 거대 필드 보스) + 온디맨드 던전(입구 포탈/메뉴 UI 통합 티켓·복귀), 거래소·개인거래·우편·길드·길드창고·친구·차단·채팅.
2. **[BP02 — 8×8km 고속 이동 스트리밍·원거리 식생·렌더링/VSM/PSO/Substrate](../Blueprints/BP02_OpenWorld_HighSpeedStreaming_Foliage_Rendering.md)**: 3-Tier WP 그리드(`Grid_Macro/Main/Detail`), 2단계(`Loaded`+`Activated`) 속도·헤딩 예측 StreamingSource + 언로드 히스테리시스 + 비상 속도 안전장치, UE 5.8 `FastGeo`, 82,758그루 수목 **Instanced HLOD + Octahedral Impostor**(91GB Embree OOM 해소), WPO 거리 컷, Landscape RVT, VSM·RT·PSO Precaching·Substrate 도입 게이트.
3. **[BP03 — 탈것(지상·비행) Mover 통합](../Blueprints/BP03_Mount_Mover_Unification.md)**: `UProject_JMountMoverComponent : UProject_JMoverComponent`, `UProject_JGroundMountMode` / `UProject_JFlyingMountMode`(3D 에너지 비행), 롤백 안전 이착륙 `FLayeredMove`, 라이더 부착·공중 강제 하차·Iris 일원화 및 4단계 무중단 마이그레이션.
4. **[BP04 — 전투(A09)·모션 매칭 연계·군중 LOD & Mass(A03)](../Blueprints/BP04_Combat_MotionMatching_CrowdLOD_A03_A09.md)**: 모션 매칭+Motion Warping+HitStop/Additive 피격, 원격 플레이어 거리별 PoseSearch DB/Graph LOD + ABA 보호, 범용 판정 `FProject_JHitQuerySpec`(AoE/풀링 투사체/다단/거대 보스 다부위 SSR), CC 점감·무력화, Mass 3-Tier + NPC 풀링 + VAT/ISM(`A03`, NPC 비-MM 유지), 군중 Niagara 예산.
5. **[BP05 — PvE·AI(StateTree)·어그로·루팅·사망/부활·성장·퀘스트·생활](../Blueprints/BP05_PvE_AI_Progression_Quest_LifeSkills.md)**: `StateTree` 일반/다페이즈 보스 AI, 리싱/이베이드, 근접 110%/원거리 130% 히스테리시스 어그로, `State_Dead` 사망/부활, 기여도 개인 루팅 + 우편 폴백, 레벨/장비 강화·세트 스케일링, 체인/일일·주간 UTC 반복 퀘스트, 개인 위상/경쟁 채집 및 제작 확장.
6. **[BP06 — 운영·관측성·안티치트·오디오·에디터/CI·로컬라이제이션/접근성](../Blueprints/BP06_Operations_Observability_AntiCheat_Audio_QA.md)**: 이동/전투/경제 서버 무결성 안티치트, Batch 106 메모리 귀속(`-llm` + UObject Census), Prometheus/Grafana/Sentry/`DemoNetDriver`, 200봇 부하·24시간 Soak, MetaSounds 48보이스 Concurrency 예산, 크로스-에셋 CI 밸리데이터, `StringTable` 다국어·접근성.
