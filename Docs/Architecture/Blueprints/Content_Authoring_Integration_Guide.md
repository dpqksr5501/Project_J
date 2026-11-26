# Project J 콘텐츠 제작 & 시스템 연결 실전 가이드 (Authoring & Integration Playbook)

> 작성일: 2026-11-23\
> 목적: Phase 0~3으로 구축·배선된 MMORPG 시스템 위에 **실제 콘텐츠(몬스터·거대 필드 보스·던전·지상/비행 탈것·스킬·8×8km 월드/나무·아이템/퀘스트·UI·사운드)**를 제작해 꽂을 때, **정확히 어떤 부모 클래스·컴포넌트·데이터 에셋·소켓·설정을 연결해야 그대로 잘 돌아가는지**와 **절대 피해야 할 함정(Gotchas)**을 한곳에 정리한 기준서다.

---

## 1. 구현된 런타임과 제작 연결

| 구분 | 항목 | 상태 및 설명 |
|---|---|---|
| 공통 런타임 | 기존 부모와 서비스 | Mover 탈것·NPC 어그로와 CC·인카운터·채집/제작·UI·오디오의 공용 경로, 에셋 교차 검증기와 개발 치트는 구축돼 있다. 제작 시에는 각 부모의 실제 입력·데이터·에셋을 아래 레시피대로 연결한다. |
| 저장 샌드박스 | 플레이와 연결 확인 | 두 테스트 맵에서 비모션매칭 보스의 공격·경고·실제 처치, Starter 비행, 채집·제작대와 테스트 하네스를 확인했다. 온라인에서는 별도 로컬 Unreal 서버와 Nakama·PostgreSQL에서 던전 입장·보상·복귀를 검증했다. |
| 새 콘텐츠 | 역할과 제작 설정 | 탈것 메시·소켓·입력, NPC 전투 정의와 Behavior 데이터, 실제 UI 조작 및 백엔드 모드를 해당 콘텐츠에서 확인한다. 운영에 쓰는 인원·품질·서버 구성은 해당 장면으로 검증한다. |

---

## 2. 콘텐츠 종류별 연결 레시피 (복사해서 바로 쓰는 체크리스트)

### 2.1 새 지상 / 비행 탈것 제작 (`BP03`)
- **부모 클래스**: `Content/DataAssetSets/MMO/BP_MountMover_Starter` (`AProject_JMountMoverPawn`) 상속.
  - ⚠️ **주의**: 신규 탈것을 구형 CMC 기반 `AProject_JMountCharacter`로 늘리지 않는다(기존 테스트 호환용으로만 유지).
- **자동 연결되는 항목**: `IMoverInputProducerInterface::ProduceInput`, Enhanced Input 바인딩(`MoveAction`, `LookAction`, `PitchOrAscendAction`, `BoostAction`, `GlideAction`, `TakeOffAction`, `LandAction`, `DismountAction`), 비행 급강하 FOV/암 길이 보간 + 마우스 휠 줌, 라이더 고삐 Hand IK(`UProject_JCharacterAnimInstance` 자동 폴링), 탑승/하차 안티치트 화이트리스트.
- **필수 메시 소켓**:
  - `RiderSocket` (안장 탑승 위치 — 누락 시 저장 검증기 경고)
  - `Reins_L`, `Reins_R` (라이더 고삐 Hand IK 타깃 — 누락 시 저장 검증기 경고)
- **필수 설정**:
  - `MountMover->MotionProfile`: 지상 전용이면 `bCanFly = false`, 비행 탈것이면 `bCanFly = true`, `FlightCruiseSpeed`, `FlightDiveMaxSpeed`, `MaxStamina` 조정.
  - `MaxHealth`: 탈것 체력(0이 되면 자동으로 `ForceStopFlightToFalling()` + `DismountRider(true)` 실행되어 공중 부유 방지).
  - **애니메이션**: 탈것은 **비-모션매칭** 유지(`UProject_JFlyingMountAnimInstance` / BlendSpace에서 `GetMountMover()->GetLatestMountSyncState()`의 `AirSpeed`, `PitchDeg`, `BankDeg`, `FlightState` 소비).

### 2.2 새 일반 몬스터 & 거대 필드 보스 제작 (`BP04` / `BP05`)
- **부모 클래스**: `AProject_JNPCCharacter` (`UProject_JThreatComponent` + `UProject_JCCResistanceComponent` 기본 내장. ⚠️ **NPC AnimBP에 PoseSearch/Chooser 연결 절대 금지** — `UProject_JCrossAssetEditorValidator`가 저장 시 자동 차단).
- **일반 몬스터 필수 구성**:
  1. `UProject_JNPCBrainDefinition` DataAsset 복제 (`SightRadiusCm`, `LeashRadiusCm`, `MaxChaseZDeltaCm` 설정).
  2. BP의 `BrainDefinition` 프로퍼티에만 지정 → `BeginPlay` 시 자동으로 `ThreatComponent`, `HomeLocation`, `OnConfirmedHealthGELoss`(모든 GAS 데미지 자동 어그로/기여도 누적)가 연결된다.
- **거대 필드 보스 추가 구성**:
  1. `Content/DataAssetSets/MMO/DA_NPCBrain_FieldBoss_Starter` 복제 (`IsGiantBoss = true`, `BossPhaseHealthThresholds = {0.70, 0.35}`) 후 BP의 `BrainDefinition`에 지정 → `CCResistanceComponent->bIsBossCCImmune = true` 자동 설정.
  2. `UProject_JMultiPartHitComponent` 부착 → 최대 8개 부위(`Head` 약점 `2.0x`, `Body` `1.0x`, `Tail` 등) 소켓/반경 설정 (30Hz 소켓 히스토리 자동 기록, 미부착 시 저장 검증기 경고).
  3. 월드에 `AProject_JWorldEncounter` 배치 → `EncounterId` 고유값, `NPCClass`, `bUseSchedule`/`StartUtc`/`EndUtc`(UTC 스케줄), **`BossLootTable = DA_LootTable_FieldBoss_Starter`**(복제본), 선택 `QuestKillEventId` 연결.
  4. **보상과 카메라**: 인카운터가 전투 중 카메라 요청을 소유하고 종료 시 회수한다. 수령자는 전투 기여자와 처치 위치의 `PartyRewardRadiusCm` 안에 있는 자격 충족 파티원이다. `ActivationRadius`는 스폰 준비에 사용한다. 온라인 보상은 처치 ID를 담은 서버 명령으로 제출해 지급 결과를 확인한다. 퀘스트 처치 기록과 우편 소유권을 각 실행의 결과에서 확인한다.
  5. **공격과 위험 경고**: 인카운터의 `CombatDefinition`, NPC의 비모션매칭 AnimBP, 경고와 타격 시간이 연결된 몽타주를 지정한다. 활성 `ActiveWarningCues`는 원격 장식 VFX의 드롭·불투명도 제한에서 제외한다. 저장된 테스트 맵에서는 실제 마커의 생성·해제를 확인했다. 200인 화면의 가독성·메시 가림·개별 FX 품질은 실제 장면에서 확인한다.

MainWorld B01은 기존 테스트 보스 모델·클리브·경고와 초기 골드 보상 테이블을 재사용한다. 단독 PIE에서는 테스트 플레이어의 배치와 체력만 명령으로 준비한 뒤, 실제 입력 10회로 보스를 처치하고 개인 우편과 300초 재등장 대기 진입을 확인했다. 최종 보스 에셋·Nav 추적과 리셋·재등장 완료·온라인 재접속·다인원 교전은 별도 검증이 필요하다. [저장 연결과 전투 근거](../../WorldGen/07_GAMEPLAY_SPATIAL_INTEGRATION.md)

### 2.3 새 던전 콘텐츠 제작 (월드 입구 + 메뉴 UI + 파티 동반 입장, `BP01`)
1. **던전 정의서(`UProject_JDungeonDefinition`) 생성**:
   - `Content/DataAssetSets/MMO/DA_Dungeon_Starter`를 복제해 `DungeonId`, `DungeonMap`(목적지 `.umap`), `MinPartySize`, `MaxPartySize`, `TimeLimitSeconds`, `ClearLootTable`을 설정한다. (미래의 개인/길드 인스턴스 하우징 내부도 동일 계약으로 재사용 가능)
2. **던전 맵 제작**:
   - 던전 전용 GameMode(부모 `AProject_JGameMode`)에서 **`bRequireDungeonTicket = true`** 설정 → 위조·만료·재사용 티켓 접속 자동 차단 및 이탈(`Logout`) 시 **180초 재접속 유예창 자동 개방**.
   - 최종 보스 인카운터에 `DungeonDefinition`과 `bTriggerDungeonClearOnDefeat=true`를 연결한다. 인카운터가 전투 시작·종료의 부활 잠금을 관리한다. 처치 시 입장한 참가자를 확인하고 `ClearLootTable`과 선택 `QuestClearEventId`로 완료를 처리한다. 온라인 복귀는 완료 커밋이 확인된 뒤에 열린다.
3. **오픈월드 입구 포탈 / 메뉴 UI 연결**:
   - 오픈월드 입구에 `AProject_JWorldPortal` 배치 → **`DungeonDefinition`** 지정 (`BeginPlay` 시 자동 등록 + `DungeonId` 자동 동기화, `bAutoTravelOnTicket = true` 기본 활성).
   - 메뉴 UI에서는 `PlayerController->MMOClientBridge->ServerRequestDungeonEntryFromMenu(DungeonId)` 호출.
   - 양쪽 모두 `DispatchDungeonEntryForPlayer`를 사용한다. 현재 월드 세션, 파티·생존 조건과 멤버별 앵커를 확인한다. 온라인에서는 워커가 준비를 확인한 뒤 이동 티켓을 보낸다.

### 2.4 새 스킬 · 투사체 · 생활/채집 콘텐츠 제작 (`BP04` / `BP05`)
- **스킬 부모 클래스**: `UProject_JGameplayAbility` 상속 + **`InstancingPolicy`는 반드시 `InstancedPerActor` 또는 `InstancedPerExecution`**.
  - **근접 무기**는 라이브 소켓 또는 검증된 오프라인 `MeleeTrajectory`를 사용한다. 타격 창을 완전히 포함하는 몽타주 세그먼트와 스켈레톤 호환성, 루트 기준과 서버 권위 타격을 확인한다. SpellbladeKit은 기본 `Light01`을 포함해 활성 근접 공격 전체를 베이크했다. 여러 타격 창은 하나의 공격 DA에 각각 저장한다. [베이크 계약과 제작 절차](../../Animation/Authoring/Spellblade_CombatKit.md#근접-타격-궤적-베이크)
  - **장판(AoE 4형상) / 거대 보스 다부위**: `FProject_JHitQuerySpec` + `HitValidation->ServerExecuteHitQuery(...)`.
  - **투사체(화살/파이어볼 등)**: ⚠️ **`AActor` 스폰 금지** — `UProject_JProjectileManagerSubsystem::SpawnCombatProjectile(...)` 원콜로 최대 2,048개 풀링 발사 + GAS `DamageEffect` + 거대 보스 다부위 배율 자동 적용.
  - **CC / 보스 무력화**: `HitValidation->ApplyValidatedCC(Target, Category, BaseDuration, BossStaggerDamage)` 호출 (`AProject_JBaseCharacter` 기본 내장 `CCResistanceComponent` 자동 사용).
  - **Motion Warping**: `Project_J::Combat::ClampAttackWarpTarget(...)`으로 서버 최대 거리(기본 250cm) 클램프 후 적용.
- **생활 · 채집 · 제작 공통 부모 (중복 액터 생성 금지)**:
  - **광석 채광 / 약초 채집 / 벌목 / 낚시 포인트 / 일반 채집**: 모두 단일 공통 부모 **`AProject_JGatheringNode`** 상속 후 `NodeVisual` 메시, `ActivityType`(`Mining`/`Herbalism`/`Logging`/`Fishing`/`Foraging`), `NodeId`, `YieldItemId`, `CooldownSeconds`, `bUsePersonalPhaseLifeSkill = true`, `ResourceNodeMode`(`PersonalPhase` 기본 / 희귀 노드만 `CompetitiveShared`), 선택 `QuestGatherEventId` 지정.
  - **대장간 / 연금 / 요리 / 목공 제작대**: 단일 공통 부모 **`AProject_JCraftingStation`** 상속.
  - **오디오**: 전투/보스/파티/월드 SFX는 `UProject_JAudioBudgetSubsystem::PlayBudgetedSoundAtLocation(...)`으로 재생해 48보이스 캡·25m 컬링·그룹 덕킹·자동 보이스 해제를 일괄 보장.

### 2.5 야외 지역 · 나무와 식생 배치 (`BP02`)

맵을 처음부터 만들거나 기존 MainWorld를 교체하는 순서는 [맵 신설·교체 절차](../../World/World_Map_Authoring_and_Replacement.md)에 있다.

1. 원경에 남길 수관·랜드마크와 근경 전용 소품을 정한다. 월드의 기본 HLOD 레이어를 상속하거나 적합한 레이어를 직접 지정한다. 수관 인스턴싱과 건축물 병합은 같은 맵 안에서도 함께 사용할 수 있다.
2. 원경 수관은 WPO 종료 거리와 그림자 비용을 확인한다. 현재 수관 검사의 공통 상한은 10,000cm다. 선택한 배치를 검증하고 원본이 언로드되는 구간의 프록시와 실제 수관을 왕복 확인한다. [공통 상한과 메시 역할](../../../Source/Project_JCore/Public/World/Project_JDistantMeshPolicy.h)
3. 지역 정의와 컨트롤러가 소비하는 이동 프로필을 확인한다. 지상 탈것은 다리와 급선회, 비행은 강하와 착지를 대표 경로로 사용한다. 기존 8×8km의 그리드·거리 제안과 고밀도 식생의 빌드 메모리 이력은 [BP02 설계](BP02_OpenWorld_HighSpeedStreaming_Foliage_Rendering.md)에 있다.

### 2.6 UI · 로컬라이제이션 · 사운드 · 밸런스 · 개발 테스트 규율 (`BP06`)
- **UI 모달 창**: 인벤토리·던전 메뉴·거래소·우편·길드 창을 열 때 `UProject_JUIWindowStackSubsystem::PushModalWindow(WindowId)` 호출, ESC 입력은 `HandleEscapeCloseTop()`으로 단일 라우팅.
- **텍스트**: 모든 플레이어 노출 문자열은 `LOCTEXT` / `StringTable` 키 사용(하드코딩 금지). 서버→클라이언트 통신은 `SubjectKey` / `ReasonCode`(`FName`)만 전송.
- **사운드**: `Config/DefaultEngine.ini`의 `AudioMaxChannels=48` 유지, 타인 전투 SFX는 `UProject_JAudioBudgetSubsystem`의 `OtherPlayerSFX`(25m 컬링·최대 6보이스·-9dB 덕킹) 그룹 사용.
- **개발 중 빠른 반복 (Mode 0 vs Mode 1 & 테스트 샌드박스)**:
  - **일상 제작/전투/애니메이션 테스트**: `Mode 0 (StandaloneMock)` — Docker 없이 PIE 1초 실행 + 콘솔 치트(`PJ_ToggleTestUI`, `PJ_Teleport`, `PJ_HealFull`, `PJ_KillSelf`, `PJ_SimulateLag 100 5`, `PJ_DumpRuntimeMetrics`).
  - **바로 열어 테스트하는 샌드박스 레벨 & 테스트 UI**:
    - 오픈월드 샌드박스: `Content/Maps/Test_MMO_Sandbox.umap` (`BP_MountMover_Starter` 비행 활성, `BP_DungeonPortal_Test`, `BP_GatherNode_Mining_Test`, `BP_GatherNode_Fishing_Test`, `AProject_JCraftingStation`, 필드 보스 `BP_FieldBoss_Test` 조우 배치 완료)
    - 던전 보스 아레나: `Content/Maps/Test_MMO_Dungeon.umap` (`DA_Dungeon_Starter.DungeonMap`, 최종 보스의 `DungeonDefinition`·`bTriggerDungeonClearOnDefeat` 연결). D3D12 PIE에서 실제 공격과 경고·처치를 확인했다. Standalone 로그인에서는 참가 기록을 이어 받지 못해 던전 보상이 승인되지 않았다.
    - 인터랙티브 테스트 UI: PIE 콘솔에 **`PJ_ToggleTestUI`** 입력 (또는 `Content/DataAssetSets/MMO/WBP_MMOTestHarness.uasset` 확장) — 실시간 텔레메트리, 던전 입장/복귀, 우편함 조회/수령, 자결/완치/부활(거점·제자리), 비행 토글, 타인 VFX 투명도(`ProjectJ.Combat.Presentation.OtherPlayerOpacityScale`), 백엔드 모드 전환을 원클릭으로 테스트 가능.
  - **백엔드와 영속성 검증**: 로컬 Windows Nakama·PostgreSQL과 두 서버에서 `Infra/scripts/test-local-native-backend.ps1 -VerifyAuthoredSandbox`로 입장, 테스트 처치 신호, 우편 영수증과 복귀를 확인했다. 백엔드 모드는 실행 구성에서 정한다. 접속 중인 온라인 서버의 저장 모드를 하네스에서 변경할 수 없게 했다.

---

## 3. 실전 콘텐츠 제작 · 확장 시 반드시 지킬 6대 고려사항 (버그 방지 수칙)

1. **`GameInstanceSubsystem`은 맵 이동(`ClientTravel`)을 넘어 살아남는다**
   - 오디오 보이스 카운트(`UProject_JAudioBudgetSubsystem`), UI 모달 스택(`UProject_JUIWindowStackSubsystem`), 채팅/쿨타임 타임스탬프(`NowSeconds`)처럼 `UWorld` 수명에 묶인 상태는 반드시 `FWorldDelegates::OnWorldCleanup`에서 초기화하거나 `NowSeconds < LastTime` 시계 리셋을 허용해야 한다. (현재 적용 완료)
2. **전용 서버 클라이언트에서는 `UnPossessed()`가 호출되지 않는다**
   - 탈것·차량·변신 Pawn처럼 컨트롤러가 바뀌는 액터는 서버 `UnPossessed()`뿐 아니라 **클라이언트 `OnRep_Controller()`**에서도 Enhanced Input Mapping Context 제거와 로컬 Tick 비활성화를 수행해야 입력·틱 누수가 생기지 않는다. (`AProject_JMountMoverPawn` 적용 완료)
3. **경제·우편·거래 변경은 반드시 "모든 사전 검증 → 실제 차감/지급" 순서를 지킨다**
   - 골드를 먼저 지급하고 아이템 에스크로를 뒤에서 검증하면 실패 시 골드만 반복 지급되는 치명적 복제 버그가 된다. 새 경제 API를 추가할 때도 `ClaimMailAttachments`처럼 **아이템 소유권·인벤토리 공간·중복 ID 검증을 모두 통과한 뒤** 원장 쓰기를 실행한다.
4. **풀링 배열 순회(`ActiveProjectiles` 등) 안에서 게임플레이 콜백을 즉시 실행하지 않는다**
   - 명중 콜백이 분열 투사체를 스폰하거나 다른 투사체를 취소하면 순회 인덱스가 깨진다. 반드시 로컬 `Pending` 배열에 수집한 뒤 루프 종료 후 일괄 디스패치한다. (`UProject_JProjectileManagerSubsystem` 적용 완료)
5. **로컬 카메라 오버라이드·타깃 참조는 `bool`이 아니라 `TWeakObjectPtr<Component>`로 소유한다**
   - 보스 조우 중 탈것 탑승·사망·포탈 이동으로 Pawn이 바뀌면 `bool` 플래그만으로는 원래 카메라의 오버라이드를 해제하지 못해 시야가 영구 고착된다. (`AProject_JWorldEncounter` 적용 완료)
6. **주간/일간 리셋과 수치 입력은 항상 UTC 기준 달력 계산 + `FMath::IsFinite` 가드**
   - 주간 리셋(`WeeklyMonUtc00`)은 `(DayOfYear-1)/7` 대신 실제 월요일 00:00 UTC 날짜를 계산해 연말/연초(12/31 ↔ 1/1) 주간 분리를 막고, 데미지·이동·속도·어그로 입력은 모두 `FMath::IsFinite`로 `NaN/Inf`를 차단한다.

---

## 4. 새 콘텐츠 추가 후 최종 점검 체크리스트

제작 중에는 [바꾼 콘텐츠에 맞는 가장 작은 검사](../Planning/Project_J_OpenWorld_Astra_Independent_Review_Handoff_2026-11-26.md#콘텐츠-추가-시-가장-작은-검사)부터 실행한다. 아래는 구조 변경·회귀·메모리 증가처럼 확인할 조건이 생겼을 때 고르는 도구다. 직업·스킬의 실제 입력과 늦은 경고, 탈것의 이동과 군중·오디오·VFX의 실행 결과는 [오픈월드·MMORPG 통합 인계](../Planning/Project_J_OpenWorld_Project_Handoff.md)에서 찾는다.

1. **에디터 DataValidation 실행**: `UProject_JAbilityAuthoringAssetValidator` + `UProject_JCrossAssetEditorValidator` 오류 0.
2. **아키텍처 DAG 검증**: `python Scripts/Validation/Validate-MMOArchitecture.py` 통과(`passed: true`).
3. **핵심 자동화 회귀 실행**:
   ```powershell
   powershell -NoProfile -ExecutionPolicy Bypass -File "Scripts/Validation/Measure-CrowdE.ps1" -RunName Check_Run1 -Suite LocalCheck -Filters 'ProjectJ.Systems.+ProjectJ.Hardening.Phase0.'
   ```
4. **메모리 누수 의심 시 (Batch 106 도구)**: 10분/60분 시점 `obj list -alphasort` 덤프 후 `python Scripts/Validation/Diff-MemoryCensus.py base.txt later.txt`로 증가 상위 20개 클래스 확인.
