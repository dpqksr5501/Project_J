# Motion Matching Next Steps

## Animation Budget Source

`UProject_JCharacterAnimInstance` now resolves Near/Mid/Far/Hidden motion matching values through `FProject_JAnimationBudgetSettings`.

- `UProject_JLocomotionProfile::GetResolvedAnimationBudgetSettings()` remains the migration bridge from legacy per-field floats.
- AnimInstance fallback values are still used when no locomotion profile is assigned.
- Motion matching update interval, hidden remote interval, far distance, and far-disable policy now share one budget source.
- This keeps MMO animation cost tuning data-driven without changing the Start/RemoteStart replicated event semantics.

## PIE Profiling Snapshot

`DumpMMOProfilingSnapshot [MaxDetailedCharacters]` captures an early MMORPG-scale snapshot from the current PIE world.

- Counts player characters and NPC characters separately.
- Counts authority, autonomous proxy, and simulated proxy roles.
- Aggregates animation budget tiers as Local/Near/Mid/Far/Hidden.
- Reports how many characters update animation data, use full chooser rows, or use far-only chooser rows.
- Samples distance/combat replication policy decisions from the local viewer position.

Use this before changing budget values. In early development, the goal is not a perfect benchmark; it is to build a repeatable baseline for 10/30/50 character tests and catch cases where simulated proxies stay too expensive or fall into the wrong tier.

Current early pass:

- Use player characters for Motion Matching quality and remote proxy validation.
- Use NPC characters as a non-MM low-cost baseline.
- Compare player proxy tier distribution against NPC counts before increasing Motion Matching quality.
- Keep backend and inventory work out of this loop until character movement and replication costs are measured.

## Start Responsive Exit Policy

Run/Sprint Start는 "정지에서 이동으로 들어가는 짧은 진입 포즈"로만 유지합니다. MMORPG 입력에서는 WASD 방향을 빠르게 바꾸거나 마우스로 control yaw를 크게 돌리는 일이 잦기 때문에, Start 상태가 `StartMinDuration` 동안 무조건 붙잡히면 실제 이동 방향과 포즈가 어긋납니다.

현재 C++ 정책:

- `StartResponsiveTurnExitMinTime` 이후에는 조기 탈출을 허용합니다.
- `StartResponsiveTurnExitAngle` 이상의 입력 방향 변화, actor yaw 변화, control yaw 변화가 감지되면 바로 `Locomotion`으로 전환합니다.
- local/autonomous 캐릭터는 입력과 control yaw를 사용합니다.
- simulated proxy는 reference를 갱신하기 전에 replicated velocity와 actor yaw 변화량을 먼저 판정합니다.
- 짧게 이동하고 바로 멈춘 경우에는 `StartInputReleaseExitMinTime` 이후 `Stop` 또는 `Idle`로 빠집니다.
- `Locomotion`에 진입한 뒤에는 derived start가 다시 `Start` phase를 만들지 않습니다.
- 원격 캐릭터의 phase/gait/start 상태가 바뀌면 motion matching update interval을 기다리지 않고 즉시 PSD를 다시 평가합니다.
- 리팩터링 전 동작처럼 `StartAutoPromoteDelay` 뒤에는 Start/RemoteStart를 현재 이동 의도에 따라 `Locomotion`, `Stop`, `Idle`로 강제 정리합니다.
- 별도 replicated animation event를 추가하지 않으므로 네트워크 대역폭은 늘리지 않습니다.

튜닝 기준:

- Start 포즈가 너무 빨리 끊기면 `StartResponsiveTurnExitMinTime`을 조금 올립니다.
- 빠른 방향 전환에도 Start가 남으면 `StartResponsiveTurnExitAngle`을 낮춥니다.
- 짧은 탭 이동에서 Stop이 너무 빨리 나오거나 너무 늦게 나오면 `StartInputReleaseExitMinTime`을 조정합니다.
- 원격 캐릭터가 `PSD_run_Start_Remote` 또는 sprint start에 남으면 `StartAutoPromoteDelay`를 낮춥니다.
- 원격 캐릭터가 잦은 보정처럼 보이면 `RemoteStartTurnExitAngle`을 조정합니다.

이 정책은 Motion Matching DB 구조를 바꾸지 않고 Chooser에 들어가는 phase만 빠르게 `Cycle/Locomotion` 쪽으로 돌리는 방식입니다. 그래서 Start/RemoteStart PSD는 그대로 사용할 수 있고, 네트워킹 측면에서는 replicated counter 의미도 유지됩니다.

Project J의 locomotion motion matching은 C++ locomotion state component, animation thread-safe data, profile 기반 fallback으로 구성되어 있습니다. 앞으로의 작업은 motion matching 품질을 유지하면서 MMORPG 환경의 다수 캐릭터 비용을 줄이는 데 초점을 둡니다.

## 현재 기준

- `UProject_JLocomotionAnimStateComponent`가 locomotion phase와 derived context를 계산합니다.
- `UProject_JCharacterAnimInstance`가 game thread에서 thread-safe snapshot을 만들고 anim proxy로 전달합니다.
- `UProject_JMotionMatchingAssetSet`은 run/sprint/start/remote start/stop/turn/jump/fall/landing PSD를 나눕니다.
- `UProject_JLocomotionProfile`은 movement, motion matching, optimization fallback을 제공합니다.
- `FProject_JAnimationBudgetSettings`는 Near/Mid/Far/Hidden budget 설정을 명시적으로 묶습니다.
- `FProject_JAnimOptimizationPolicy`는 현재 frame의 animation budget tier와 motion matching update interval을 표현합니다.
- `UProject_JPlayerInputBindingComponent`는 input binding만 담당하고 `DoMove`, `DoJumpStart`, `StartSprint` 같은 기존 gameplay method 호출 순서를 유지합니다.
- `UProject_JReplicatedAnimEventComponent`는 remote proxy가 소비하는 replicated animation event counter를 갱신하고 해석합니다.

## Near Term

### 1. PSD 데이터 정리

각 PSD family가 맡는 역할을 좁게 유지합니다.

- Cycle: 기본 loop locomotion
- Start: local start
- RemoteStart: simulated proxy start/reface
- Stop: stop transition
- TurnRedirect: 이동 중 방향 전환
- Jump/Fall/Landing: 공중/착지 상태

### 2. Blueprint override 확인

다음 값들은 C++ fallback보다 Blueprint/component/profile override가 우선될 수 있으므로 실제 에셋에서 확인합니다.

- `StartMinDuration`
- `StartMaxDuration`
- `StopMinDuration`
- `StopFallbackDuration`
- `StandLandingRequestDuration`
- `LandingRequestDuration`
- `MidMotionMatchingUpdateInterval`
- `FarMotionMatchingUpdateInterval`

### 3. PIE 네트워크 테스트

Listen server와 client에서 다음 상황을 반복 확인합니다.

- remote start
- stop 중 회전
- jump/falloff/landing
- sprint key를 누른 stand landing이 sprint landing PSD로 잘못 가지 않는지
- chooser row가 local/remote/far tier에서 의도대로 선택되는지

### 4. Debug command 사용

PIE에서 다음 command를 사용해 상태를 확인합니다.

- `DumpMMOState`
- `DumpAnimBudget`
- `DumpReplicationPolicy`
- `DumpCharacterComponents`
- `DumpCombatState`

입력 또는 replicated animation event를 리팩터링할 때는 위 command로 local/autonomous/simulated proxy의 상태가 기존과 같은 순서로 변하는지 확인합니다.

## Optimization Track

### 1. Significance tuning

Near/Mid/Far/Hidden tier를 거리와 화면 가시성 기준으로 조정합니다.

- Near: local quality 유지
- Mid: motion matching update interval 완화
- Far: far chooser row 또는 낮은 빈도 PSD 갱신
- Hidden: animation-only data update throttle

### 2. Animation budget policy 정리

`FProject_JAnimationBudgetSettings`를 기준으로 budget 설정을 읽고, 기존 legacy float fallback은 migration path로 유지합니다. 장기적으로는 LocomotionProfile의 개별 optimization float를 struct 중심으로 정리합니다.

### 3. Profiling 기반 결정

Unreal Insights에서 다음 비용을 먼저 확인합니다.

- AnimInstance update
- PoseSearch / Motion Matching node
- CharacterMovement
- skeletal mesh evaluation
- material/draw call
- actor/component tick

## Later

### LocomotionProfile 고도화

캐릭터 종류, 체형, 무기, 직업별 start/stop/landing duration을 profile로 이동합니다.

### Distance-based animation quality policy

플레이어, 파티원, 적, 군중 NPC마다 같은 거리라도 다른 quality tier를 줄 수 있도록 policy를 분리합니다.

### NPC non-MM locomotion path

NPCs should not use player-grade Motion Matching in the current architecture. Keep NPC animation on cheaper blendspace, sequence, cached pose, URO, and significance-driven update paths. For MMORPG scale, spend NPC optimization budget on AI tick cadence, replication relevance, skeletal mesh update frequency, combat significance, and server-authoritative state compression before improving animation fidelity.

### Mass/Crowd 검토

수백 단위 crowd가 실제 목표가 될 때 MassEntity, crowd LOD, server-side simulation 분리를 검토합니다.

## Do Not Do Yet

- AnimInstance나 UObject 접근을 임의 worker thread로 옮기지 않습니다.
- PSD 선택 로직을 여러 시스템에 흩뿌리지 않습니다.
- replicated animation event counter의 의미를 바꾸지 않습니다. 구조를 옮기더라도 remote start/stop/jump/fall/landing timing은 유지합니다.
- 측정 없이 Mass, VAT, impostor, custom animation worker를 먼저 만들지 않습니다.
