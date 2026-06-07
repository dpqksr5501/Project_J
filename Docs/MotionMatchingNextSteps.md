# Motion Matching Next Steps

Project J의 플레이어 캐릭터 Motion Matching은 C++ locomotion state, thread-safe AnimInstance snapshot, profile 기반 PSD 선택, animation budget policy를 중심으로 유지합니다. NPC는 현재 구조에서 player-grade Motion Matching을 사용하지 않는 것을 기본 가정으로 둡니다.

## Current Architecture

- `UProject_JLocomotionAnimStateComponent`가 ground/air/landing/start/stop state를 계산합니다.
- `UProject_JCharacterAnimInstance`가 game thread snapshot을 만들고 anim thread에서 안전하게 사용합니다.
- `UProject_JMotionMatchingAssetSet`이 run/sprint/start/remote start/stop/turn/jump/fall/landing PSD를 소유합니다.
- `UProject_JLocomotionProfile`이 movement speed, motion matching distance, update interval, optimization fallback을 제공합니다.
- `FProject_JAnimationBudgetSettings`와 `FProject_JAnimOptimizationPolicy`가 Near/Mid/Far/Hidden tier별 animation cost 정책을 공유합니다.
- `UProject_JReplicatedAnimEventComponent`가 remote proxy용 replicated animation event counter를 해석합니다.

## Animation Budget Source

`UProject_JCharacterAnimInstance`는 Near/Mid/Far/Hidden motion matching 값을 `FProject_JAnimationBudgetSettings`에서 해석합니다.

- `UProject_JLocomotionProfile::GetResolvedAnimationBudgetSettings()`는 legacy per-field float와 새 budget struct 사이의 migration bridge입니다.
- LocomotionProfile이 없으면 AnimInstance fallback 값을 사용합니다.
- Motion matching update interval, hidden remote interval, far distance, far-disable policy는 하나의 budget source를 공유합니다.
- Start/RemoteStart replicated event 의미는 유지합니다.

## Profile And PSD Validation

`Project_J::AnimationProfileValidation`과 `UProject_JMotionMatchingAssetSet::ValidateForProjectJLocomotion()`은 PIE 시작 시 다음 항목을 warning으로 알려줍니다.

- Run/Sprint: Cycle, Start, RemoteStart, Stop, TurnRedirect
- Jump/Fall/Landing: JumpStart, FallOffStart, JumpAir, Stand/Run/Sprint Land, Heavy Land
- LocomotionProfile: speed, sprint threshold, distance tier ordering
- Start timing override: resolved `MaxDuration < MinDuration` 같은 튜닝 실수
- Weapon/Combat profile: montage/play rate/socket/section 구성 실수

검증은 log-only입니다. 초기 개발을 막지 않으면서 C++ state-machine 문제와 에셋 세팅 문제를 빨리 분리하기 위한 장치입니다.

## Start Timing Overrides

Start timing은 공통값을 기본 경로로 유지하고, 필요할 때만 상황별 override를 사용합니다.

- `LocalRunStartTiming`
- `LocalSprintStartTiming`
- `RemoteRunStartTiming`
- `RemoteSprintStartTiming`

각 override field는 `-1`이면 공통값을 사용합니다. 먼저 `StartMinDuration`, `StartMaxDuration`, `StartResponsiveTurnExitMinTime`, `StartInputReleaseExitMinTime`, `StartAutoPromoteDelay`를 조정하고, local/remote 또는 run/sprint가 서로 다르게 보여야 할 때만 override를 채웁니다.

## PIE Profiling Snapshot

`DumpMMOProfilingSnapshot [MaxDetailedCharacters]`는 현재 PIE world에서 MMORPG-scale early snapshot을 캡처합니다.

- player character와 NPC character 수를 따로 집계합니다.
- authority, autonomous proxy, simulated proxy role 분포를 집계합니다.
- Local/Near/Mid/Far/Hidden animation budget tier를 집계합니다.
- animation data update, full chooser row, far-only chooser row 사용량을 기록합니다.
- local viewer 기준 replication relevance 샘플을 출력합니다.

Use this before changing budget values. 초기 목표는 완벽한 benchmark가 아니라 10/30/50 character 테스트 기준선을 만들고, simulated proxy가 너무 비싸거나 tier가 잘못 잡히는 상황을 빠르게 찾는 것입니다.

## Near Term Checks

- Remote start, stop 중 회전, jump/falloff/landing을 listen server와 client에서 반복 확인합니다.
- Sprint key가 stand landing을 sprint landing PSD로 오염시키지 않는지 확인합니다.
- Local/remote/far tier에서 chooser row가 의도대로 선택되는지 확인합니다.
- Profile validation warning이 실제 누락/튜닝 실수만 가리키는지 확인합니다.

## Optimization Track

### Significance Tuning

- Near: local quality 유지
- Mid: motion matching update interval 완화
- Far: far chooser row 또는 낮은 빈도의 PSD 갱신
- Hidden: animation-only data update throttle

### Profiling-Based Decisions

Unreal Insights에서 다음 비용을 먼저 봅니다.

- AnimInstance update
- PoseSearch / Motion Matching node
- CharacterMovement
- skeletal mesh evaluation
- material/draw call
- actor/component tick

### NPC Non-MM Path

NPC는 현재 구조에서 player-grade Motion Matching을 사용하지 않습니다. NPC 최적화는 blendspace/sequence/cached pose/URO/significance-driven update path, AI tick cadence, replication relevance, skeletal mesh update frequency, server-authoritative state compression을 우선합니다.

### Mass/Crowd Review

수백 단위 crowd가 실제 목표가 될 때 MassEntity, crowd LOD, server-side simulation 분리를 검토합니다. 측정 없이 Mass/VAT/impostor/custom animation worker를 먼저 만들지 않습니다.

## Do Not Do Yet

- AnimInstance에서 임의 worker thread로 UObject에 접근하지 않습니다.
- PSD 선택 로직을 여러 시스템에 흩뜨리지 않습니다.
- replicated animation event counter의 의미를 바꾸지 않습니다.
- 측정 없이 NPC 전체에 Motion Matching을 적용하지 않습니다.
