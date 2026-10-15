# 캐릭터 런타임 책임 분리 요청

이 문서는 캐릭터 런타임에 집중된 책임을 나누기 위해 작성한 당시의 요청 기록이다. 실제 구조 변경과 검증 결과는 [책임 분리 결과](Project_J_Character_Runtime_Responsibility_Refactor_Result_2026-09-24.md)를 확인한다.

## 0. 대상

Repository:

```text
https://github.com/dpqksr5501/Project_J
```

Branch:

```text
Character_Test
```

작업 시작 시 반드시 현재 HEAD를 다시 확인한다.

확인 당시 HEAD:

```text
18fa75430e217107b636475aca9353f1a2c9d055
```

Engine:

```text
Unreal Engine 5.8
```

이번 작업은 이전 전체 코드 감사 이후 남아 있는 **캐릭터 런타임의 과도한 책임 집중**을 구조적으로 정리하는 작업이다.

핵심 대상은 다음 3개다.

```text
1. UProject_JCharacterAnimInstance
2. UProject_JLocomotionAnimStateComponent
3. AProject_JPlayerCharacter
```

목표는 단순히 파일을 나누는 것이 아니다.

**실제 state ownership, runtime policy, presentation policy, orchestration 책임을 명확히 분리하여 버그 가능성을 줄이고, 테스트 가능성·유지보수성·확장성·향후 성능 최적화 가능성을 높이는 것**이 목적이다.

---

# 1. 이번 작업의 최우선 원칙

## 1.1 파일 분할이 아니라 책임 분리

다음은 금지한다.

```text
CharacterAnimInstance.cpp
→ CharacterAnimInstance_StateController.cpp
→ CharacterAnimInstance_MM.cpp
```

처럼 동일 클래스의 구현 파일만 나누고 내부 state ownership을 그대로 두는 작업.

파일 분리는 결과일 수 있지만 목표가 아니다.

실제 목표는:

```text
State Controller runtime state
Motion Matching runtime state
Locomotion semantic state
Remote reconstruction
TIP runtime
Character orchestration
```

의 소유권과 경계를 명확히 하는 것이다.

## 1.2 UObject / Component / Tick을 늘리지 않는다

가능하면 새 구조는:

```cpp
struct / class FProject_J...
```

형태의 **plain C++ runtime/helper**로 만든다.

다음을 기본적으로 금지한다.

```text
새 ActorComponent
새 UObject
새 Subsystem
새 Manager
새 Tick
새 Async worker
새 custom thread pool
```

정말 UObject lifecycle이나 reflection이 필요한 경우만 예외적으로 고려한다.

## 1.3 기존 기능 보존

현재 동작하는 다음 기능을 깨지 않는다.

```text
Motion Matching
Pose Search
Chooser
Blend Stack
State Controller
Start / Stop
Pivot
Turn In Place
Jump / Fall / Land
Combat Strafe
Combat Intro / Outro
Attack / Dodge / Hit React
Runtime Retarget
Hand IK
Foot Placement
Aim Offset
Mount
Animation Budget
Remote proxy reconstruction
TIP replication
Replicated animation events
SSR / combat hit validation
Input Binding → Router → Execution
```

리팩토링 중 동작을 바꾸는 것이 목적이 아니다.

## 1.4 Blueprint / AnimBP / DataAsset 계약 보존

현재 public `UFUNCTION`, `UPROPERTY`, Chooser property, AnimGraph getter가 asset에서 사용될 수 있다.

따라서:

```text
기존 Blueprint/AnimBP API
기존 reflected properties
Chooser column property
DataAsset field
GameplayTag contract
Notify contract
```

를 코드 검색만으로 사용되지 않는다고 단정하여 삭제하지 않는다.

필요하면 내부 구현을 새 runtime으로 위임하고 외부 API는 adapter/facade로 유지한다.

---

# 2. 현재 구조에 대한 기본 판단

현재 구조에서 다음 ownership은 적절하다.

```text
Combat gameplay state
→ CombatStateComponent

Combat intro/outro
→ CombatIntroComponent

Weapon visual
→ WeaponPresentationComponent

Combat visual/VFX
→ CombatPresentationComponent

Hit / SSR
→ CombatHitValidationComponent

Trajectory
→ MotionMatchingTrajectoryComponent

Input binding
→ PlayerInputBindingComponent

Input interpretation
→ SkillInputRouterComponent

GAS execution
→ SkillInputExecutionComponent

Mount
→ MountComponent

UI
→ CharacterUIBindingComponent
```

이 영역을 다시 Character나 AnimInstance로 가져오지 않는다.

---

# 3. 최종 목표 구조

최종적으로 다음 방향을 목표로 한다.

```text
AProject_JPlayerCharacter
    │
    ├─ Gameplay / Component wiring
    ├─ Lifecycle orchestration
    ├─ authoritative high-level transitions
    └─ minimal façade API

UProject_JLocomotionAnimStateComponent
    │
    ├─ semantic locomotion Source of Truth
    ├─ owns state
    │
    ├── FProject_JLocomotionContextBuilder
    ├── FProject_JRemoteLocomotionRuntime
    ├── FProject_JTurnInPlaceRuntime
    └── FProject_JMotionMatchingSelectionPolicy
         (plain C++ helpers/runtime only)

UProject_JCharacterAnimInstance
    │
    ├─ snapshot adapter
    ├─ proxy publication
    ├─ AnimGraph / Chooser API façade
    │
    ├── FProject_JStateControllerRuntime
    └── FProject_JMotionMatchingRuntime
         (plain C++ runtime state)
```

---

# 4. Phase 0 — 현재 코드 재감사

작업 전 반드시 현재 HEAD의 실제 코드를 다시 읽는다.

최소 다음 파일을 확인한다.

```text
Project_JCharacterAnimInstance.h/.cpp
Project_JCharacterAnimInstanceProxy.*
Project_JLocomotionAnimStateComponent.h/.cpp
Project_JPlayerCharacter.h/.cpp

MotionMatchingTrajectoryComponent
ReplicatedAnimEventComponent
ReplicatedJumpStateComponent
AnimationUpdateCoordinatorComponent
CombatStateComponent
CombatIntroComponent
WeaponPresentationComponent
CombatHitValidationComponent
```

그리고 다음을 문서화한다.

```text
Source of Truth
Derived state
Presentation-only state
Network-reconstructed state
Compatibility mirror
Cached state
Debug-only state
```

---

# 5. CharacterAnimInstance — 가장 높은 우선순위

현재 `UProject_JCharacterAnimInstance`는 너무 많은 책임을 가진다.

현재 대표 책임:

```text
Thread-safe snapshot
State Controller
State playback hold
Chooser
Motion Matching
PoseSearch
MM search scheduling
MM reselect
Pivot presentation
TIP presentation
One-shot foot selection
Procedural IK
Aim
Mount snapshot
Optimization tier
Debug trace
```

이 중 AnimInstance에 반드시 있어야 하는 것과 그렇지 않은 것을 구분한다.

---

# 6. FProject_JStateControllerRuntime 분리

State Controller 관련 runtime state와 policy를 AnimInstance 본체에서 분리하는 것을 우선 검토하고 구현한다.

예:

```cpp
class FProject_JStateControllerRuntime
{
public:
    void Reset();
    void Update(...);
    void OnCombatPresentationStarted(...);
    void OnCombatPresentationEnded(...);
    void OnFullBodyMontageStarted(...);
    void OnFullBodyMontageEnded(...);

    FProject_JStateControllerRuntimeOutput BuildOutput(...) const;
};
```

실제 이름과 API는 코드에 맞게 결정한다.

## 6.1 StateControllerRuntime가 소유해야 할 후보

다음 계열을 우선 이동 검토한다.

```text
StateControllerPlaybackHoldState
StateControllerPlaybackHoldStartedAtSeconds

CachedStateControllerPresentationState
CachedStateControllerRotationMode
CachedStateControllerGaitIntent
CachedStateControllerStance
CachedStateControllerStrafeDirection
CachedStateControllerPreviousStrafeDirection

Pivot playback revision
Pivot move intent revision
Suppressed pivot revision

TIP sequence tracking
TIP force reselect
Turn index cache

One-shot foot selection
Foot phase history
Contact curve state

Start gait latch
Stop gait latch
Land gait latch

Remote Start reference
One-shot control yaw
One-shot move yaw

Cached selected StateController asset/output
Chooser selection revision
```

단 AnimBP/Chooser reflection을 위해 public mirrored UPROPERTY가 필요하면 mirror는 AnimInstance에 남겨도 된다.

중요:

```text
Runtime Source of Truth
≠
Chooser reflection mirror
```

이다.

## 6.2 StateControllerRuntime의 책임

다음 판단은 Runtime 내부로 모으는 것을 검토한다.

```text
Start/Stop/Pivot/TIP presentation state
Playback hold
direct one-shot 유지/종료
Pivot cancel
Pivot supersede
Pivot redirect
TIP restart/reselect
Landing presentation hold
Combat montage boundary reset
Full-body montage boundary reset
foot variant selection
```

## 6.3 AnimInstance 역할

분리 후 `UProject_JCharacterAnimInstance`는:

```text
snapshot 입력
→ StateControllerRuntime에 전달
→ output을 Chooser/AnimGraph mirror로 게시
```

하는 adapter 역할에 가까워져야 한다.

---

# 7. FProject_JMotionMatchingRuntime 분리

Motion Matching의 search/runtime bookkeeping을 AnimInstance 본체에서 분리한다.

후보:

```text
ShouldEvaluateMotionMatchingThisFrame
ShouldForceMotionMatchingContextRefresh
ShouldForceMotionMatchingReselect
CacheEvaluatedMotionMatchingContext
MotionMatchingSelectionSchedule

LastEvaluatedGroundMotionMode
LastEvaluatedGaitIntent
LastEvaluatedRotationMode
LastEvaluatedPhaseFamily

LastEvaluatedStartRequested
LastEvaluatedStartWasSprinting
LastEvaluatedMotionMatchingSelectionRevision

CurrentActivePoseSearchDatabase
MM debug trace history
```

## 7.1 목표

다음 형태를 목표로 한다.

```text
Locomotion semantic selection context
        ↓
FProject_JMotionMatchingRuntime
        ↓
Should Search?
Force Reselect?
Chooser / PSD selection
        ↓
selected database/result
        ↓
AnimInstance publishes result
```

## 7.2 주의

Pose Search / Chooser API가 UObject/AnimInstance/GameThread context를 요구하는 경우:

```text
policy/state → Runtime
actual engine API invocation → AnimInstance adapter
```

로 나눈다.

Engine UObject API를 억지로 plain helper 안에 넣지 않는다.

---

# 8. CharacterAnimInstance Snapshot 구조

현재 `BuildThreadSafeData()` 흐름은 기본적으로 좋은 방향이다.

```text
BuildThreadSafeData
 ├ FillMovement
 ├ FillLocomotionState
 ├ FillPlayer
 ├ FillMount
 ├ Finalize
 └ FillProceduralIK
```

이 구조는 유지한다.

다만 `FillLocomotionStateThreadSafeData()`가 단순 snapshot copy를 넘어 StateController runtime mutation을 수행하는 부분은 줄인다.

목표:

```text
Fill...
= snapshot adapter

Runtime policy mutation
= dedicated runtime
```

---

# 9. Snapshot / Worker thread 안전성

다음 경계를 더 명확하게 한다.

```text
Game Thread UObject state
       ↓
Compact value snapshot
       ↓
AnimInstance Proxy
       ↓
Worker thread
```

Worker-safe 데이터에는:

```text
Actor*
Component*
Controller*
UObject live lookup
World access
```

를 추가하지 않는다.

---

# 10. Snapshot 성능

구조 분리 과정에서 다음을 확인한다.

```text
FTransformTrajectory copy
GameplayTagContainer copy
TArray DatabaseTags
temporary arrays
temporary strings
```

단 이번 리팩토링의 주목적은 책임 분리다.

측정 근거 없이 대규모 POD 재설계를 하지 않는다.

명백하게 불필요한 복사만 정리한다.

---

# 11. Chooser mirror 정리

현재 AnimInstance에는 많은:

```text
...ForChooser
bChooser...
Chooser...
```

property가 존재한다.

Chooser asset reflection 계약상 필요한 값은 유지한다.

하지만 다음을 구분한다.

```text
Canonical runtime value
Chooser mirrored value
Debug-only value
Legacy duplicated value
```

가능하면:

```text
runtime output
→ one PublishChooserProperties()
```

경로를 통해 mirror를 갱신한다.

Chooser property 자체를 source of truth로 사용하지 않는다.

---

# 12. LocomotionAnimStateComponent — Source of Truth 유지

이 컴포넌트는 locomotion semantics의 Source of Truth로 유지한다.

다음 state ownership은 이동시키지 않는다.

```text
Start
Stop
Sprint
Jump
Fall
Land
Pivot request
TIP semantic request
Gait
Rotation Mode
Phase Family
Move Intent revision
Motion Matching selection context
```

AnimInstance가 이 state를 다시 계산하지 않게 한다.

---

# 13. LocomotionAnimStateComponent 내부 helper 분리

현재 컴포넌트는 큰 것이 어느 정도 정당하지만 내부 policy가 많다.

새 Component가 아니라 plain C++ helper로 분리 가능한지 검토한다.

## 13.1 FProject_JLocomotionContextBuilder

후보 책임:

```text
BuildAuthoritativeContext
BuildKinematicContext
BuildDerivedLocomotionContext

ResolveGaitIntent
ResolveRotationMode
ResolvePhaseFamily

IsMovingForContext
IsMotionMatchingMovingForContext
IsStartingForContext
IsPivotingForContext
ShouldTurnInPlaceForContext
ShouldSpinTransitionForContext
```

가능하면:

```text
Input values
→ pure-ish context calculation
→ output context
```

에 가깝게 만든다.

## 13.2 FProject_JRemoteLocomotionRuntime

후보:

```text
remote movement input reconstruction
remote grounded probe
remote landing timing
remote jump state
remote MoveStart / MoveStop suppression
hidden remote update timing
remote TIP presentation event state
```

다만 실제 collision/world probe는 Component adapter가 수행하고, pure state transition만 helper가 소유할 수 있다.

## 13.3 FProject_JTurnInPlaceRuntime

현재 TIP는:

```text
local target
direction bucket
target yaw
unwrapped control yaw
reversal release
sequence
replication request
remote TIP duration
remote target
```

등 상태가 많다.

이를 별도 runtime state object로 묶을 수 있는지 검토한다.

목표:

```text
Input:
actor yaw
control yaw
movement state
combat state
time

Output:
should TIP
bucket
target yaw
sequence
replication edge
abort/reversal state
```

## 13.4 FProject_JMotionMatchingSelectionPolicy

현재 Locomotion component는 MM selection context를 만들고 revision/reselect를 관리한다.

다음 policy를 helper로 분리 검토한다.

```text
selection revision
selection changed
force reselect
combat strafe settled cycle
published context comparison
```

단 실제 PoseSearch 실행은 여기로 옮기지 않는다.

---

# 14. Locomotion state data duplication 감사

현재 header에:

```text
bStartRequested
bUseStartDatabase
bGroundStartFinished
bUseGroundLocomotionDatabase
bStopRequested
bUseStopDatabase
bIsStopping
...
```

처럼 과거 compatibility 또는 presentation helper state가 다수 존재한다.

각 값을 다음으로 분류한다.

```text
Canonical
Derived
Compatibility
Presentation-only
Debug
```

그리고 다음 규칙을 적용한다.

### Canonical
유지.

### Derived
가능하면 context/runtime output에서 계산.

### Compatibility
AnimBP/DataAsset 참조 확인 전 삭제 금지.

### Presentation-only
가능하면 AnimInstance runtime으로 이동.

### Debug-only
WITH_EDITOR / debug-only 여부 검토.

---

# 15. PlayerCharacter — Facade / Orchestrator 정리

PlayerCharacter는 component composition root 역할은 유지한다.

다음은 Character에 있어도 된다.

```text
CreateDefaultSubobject
Possess / UnPossess
BeginPlay / EndPlay wiring
Replication entry
high-level gameplay orchestration
component accessors
```

하지만 실제 기능 구현을 다시 Character가 소유하지 않게 한다.

---

# 16. PlayerCharacter에서 정리할 후보

현재 직접 수행하는 다음 책임을 검토한다.

```text
Mount async summon
Combat Intro/Outro orchestration
Combat presentation replication
Sprint policy
Locomotion profile application
Animation event dispatch
TIP replication dispatch
Equipment configuration
Landing bridge
Handover serialization
Progression refresh
```

각 항목에 대해:

```text
Character가 orchestration만 하는가?
아니면 실제 state machine/policy를 소유하는가?
```

를 판단한다.

---

# 17. Combat Intro / Outro

현재 `CombatIntroComponent`가 Drawing/Sheathing lifecycle을 소유한다.

Character가 다시:

```text
montage state machine
revision
transition timer
```

를 중복 소유하지 않게 한다.

Character는:

```text
component event
→ GAS / weapon presentation / replication bridge
```

정도만 담당하는 방향을 유지한다.

---

# 18. Sprint

Sprint는 다음 계층을 구분한다.

```text
Raw input
→ Input Binding

Intent
→ Router / Character request

Gameplay authority
→ GAS

Locomotion presentation
→ LocomotionAnimState
```

Character에 중복된 sprint state가 있다면 source of truth를 확인한다.

---

# 19. TIP replication

TIP semantic state의 owner는 Locomotion component로 유지한다.

Character는:

```text
Consume replication request
→ ReplicatedAnimEventComponent dispatch
```

정도만 담당한다.

가능하다면 direct RPC orchestration과 ReplicatedAnimEvent 중복을 정리하되, 기존 네트워크 동작을 바꾸지 않는다.

---

# 20. Mount

현재 async mount class loading 구조는 이미 감사된 상태다.

이번 작업에서 재설계하지 않는다.

단 Character가:

```text
async load state machine
spawn
mount orchestration
```

까지 너무 많이 직접 소유하는 것이 명백하다면 기존 MountComponent와의 책임 경계를 정리할 수 있다.

대규모 이전은 하지 않는다.

---

# 21. Handover / Progression

Handover와 progression은 캐릭터의 cross-cutting lifecycle이므로 무조건 다른 Component로 옮기지 않는다.

다만 관련 로직이 Character 내부의 animation/combat state와 강하게 결합되어 있다면 호출 경계만 정리한다.

---

# 22. Runtime state reset contract

이번 리팩토링에서 가장 중요한 것 중 하나다.

각 runtime/helper는 명시적으로:

```cpp
Reset()
OnOwnerChanged()
OnProfileChanged()
OnCombatTransitionStarted()
OnCombatTransitionEnded()
OnMontageInterrupted()
```

중 필요한 lifecycle API를 가진다.

다음 상황에서 stale state가 남지 않아야 한다.

```text
Respawn
Possess
UnPossess
Character mesh swap
AnimInstance reinitialize
Combat enter/exit
Attack cancel
Dodge interrupt
Hit react
Montage blend out
Montage interruption
Mount enter/exit
Network correction
Hidden → visible
LOD/tier change
```

---

# 23. State transition priority

StateController/Locomotion 변경 시 기존 우선순위를 명시적으로 보존한다.

실제 프로젝트 우선순위는 현재 코드 기준으로 확정한다.

추정으로 새 priority를 만들지 않는다.

특히 full-body action과 locomotion presentation 간 우선순위를 기존 코드와 테스트에서 확인한다.

---

# 24. Full-body montage ownership

공격/회피/피격/발도/납도처럼 full-body pose를 소유하는 montage는:

```text
locomotion semantic state를 파괴하지 않고
presentation one-shot cache/hold만 적절히 reset
```

해야 한다.

현재 잘 동작하는 이 원칙을 유지한다.

---

# 25. Remote proxy

로컬 캐릭터와 remote simulated proxy의 상태 계산을 섞지 않는다.

Remote는:

```text
replicated velocity
replicated movement
animation event
jump state
ground probe
network smoothing
```

기반 reconstruction이다.

Local input 기반 판단을 remote에 강제로 사용하지 않는다.

---

# 26. Dedicated Server

Dedicated Server에서는 visual-only evaluation을 최소화한다.

하지만 gameplay hit validation에 필요한:

```text
Leader pose
Notify
canonical melee socket
root motion
```

갱신은 보존한다.

리팩토링 중 Animation Budget / GameplayPose request 구조를 깨지 않는다.

---

# 27. Animation quality tiers

다음 계층과의 compatibility를 유지한다.

```text
Local
Near
Mid
Far
Hidden
```

StateController/MM runtime 분리로 인해:

```text
hidden remote update throttle
far MM throttle
ABA/URO
Follower tick
Hand IK
Foot IK
```

정책이 달라지지 않게 한다.

---

# 28. Thread safety

plain C++ runtime으로 분리했다고 해서 worker thread에서 호출 가능한 것은 아니다.

각 helper에 대해:

```text
GameThread-only
Worker-safe
Pure value
```

를 명확히 한다.

필요하면 `check(IsInGameThread())`를 유지/추가한다.

---

# 29. Ownership naming

이름만 보고 책임이 명확해야 한다.

좋은 예:

```text
FProject_JStateControllerRuntime
FProject_JMotionMatchingRuntime
FProject_JRemoteLocomotionRuntime
FProject_JTurnInPlaceRuntime
FProject_JLocomotionContextBuilder
```

피해야 할 예:

```text
Helper
Manager
Util
Common
Misc
Processor
```

처럼 범위가 불분명한 이름.

---

# 30. Public API 최소화

새 helper의 public API는 작게 유지한다.

예:

```cpp
Input -> Update()
Output -> GetOutput()
Reset()
```

정도를 우선한다.

AnimInstance가 helper의 내부 필드를 직접 수십 개 수정하는 구조는 피한다.

---

# 31. 데이터 흐름 단방향화

목표:

```text
Gameplay / Movement
        ↓
Locomotion semantic state
        ↓
Thread-safe snapshot
        ↓
Presentation runtime
        ↓
Chooser / MM / AnimGraph
```

가능하면 presentation이 gameplay semantics를 역으로 수정하지 않는다.

---

# 32. 테스트 가능한 구조

plain runtime을 분리하는 가장 큰 이유 중 하나다.

가능하면 UObject 없이 다음을 테스트한다.

```text
Pivot request → Stop → Pivot cancel

Pivot A → opposite Pivot B → supersede

TIP 90 → continued camera rotation → same-direction extension

TIP reversal → release → opposite TIP

Landing → Combat Intro → old Land presentation does not resurrect

Full-body attack → Start/Stop one-shot suppressed

MM context revision → reselect exactly once

Remote MoveStop → stale velocity → no fake Start
```

---

# 33. 기존 Automation Test 보존

기존 테스트를 삭제하거나 약화하지 않는다.

최소 관련 테스트:

```text
Turn In Place
Combat Stop
State Controller
Motion Matching
Animation snapshot boundary
Combat transition
Landing
Remote locomotion
SSR
Weapon Presentation
Mount
```

를 재실행한다.

---

# 34. 신규 테스트

새 runtime/helper별 최소 핵심 regression test를 추가한다.

테스트 수를 많이 만드는 게 목적은 아니다.

특히:

```text
Reset
interruption
same-state reentry
revision handling
remote/local separation
```

을 검증한다.

---

# 35. 성능 관련 목표

이번 작업에서 FPS 향상을 과장하지 않는다.

그러나 구조 분리 후 다음 최적화를 안전하게 할 수 있는지 검토한다.

```text
StateController input unchanged → unnecessary reevaluation skip
MM context unchanged → search schedule 유지
Chooser mirror only when changed
debug trace only when enabled
temporary container reuse
```

명확한 의미상 중복 계산만 제거한다.

---

# 36. 하지 말아야 할 성능 작업

다음은 근거 없이 하지 않는다.

```text
Trajectory 구조 전면 교체
PoseSearch database 재설계
all Tick removal
new async animation worker
new object pooling
new Mass conversion
custom scheduler
AnimGraph rewrite
```

---

# 37. CharacterAnimInstance 완료 목표

리팩토링 후 AnimInstance는 개념적으로:

```text
Animation integration shell
```

에 가까워져야 한다.

즉:

```text
Owner reference
Snapshot build
Engine Chooser/PoseSearch invocation
Proxy publication
AnimGraph Blueprint API
Runtime helper orchestration
```

을 담당하고,

세부 상태 머신은 plain runtime이 소유한다.

---

# 38. LocomotionAnimStateComponent 완료 목표

컴포넌트는 계속:

```text
semantic locomotion Source of Truth
```

이다.

하지만 내부에는:

```text
Context calculation
Remote reconstruction
TIP policy
MM selection policy
```

가 의미 있는 runtime 단위로 분리되어 있어야 한다.

---

# 39. PlayerCharacter 완료 목표

Character는:

```text
composition root
lifecycle owner
network/gameplay bridge
high-level orchestrator
```

역할을 한다.

다음처럼 되면 안 된다.

```text
Character가 직접
TIP algorithm
MM search policy
combat montage state machine
weapon visual state
input chord logic
hit validation
```

을 소유.

---

# 40. Migration 전략

한 번에 전체를 옮겨서 깨뜨리지 않는다.

권장 순서:

```text
1. StateControllerRuntime 추출
2. tests/build
3. MotionMatchingRuntime 추출
4. tests/build
5. Locomotion context helper 추출
6. tests/build
7. TIP/remote runtime 추출
8. tests/build
9. PlayerCharacter orchestration 정리
10. final full relevant test
```

단 각 단계가 너무 작은 단순 파일 이동에 그치지 않게 한다.

---

# 41. API migration

기존 외부 호출을 깨야 할 필요가 없다면 facade를 유지한다.

예:

```cpp
AnimInstance->GetThreadSafe...
```

는 내부적으로 새 runtime/output을 읽도록 바꿀 수 있다.

Blueprint 호출자는 수정하지 않아도 된다.

---

# 42. 코드 삭제 규칙

새 runtime으로 책임을 옮겼으면 기존 duplicated state/path를 남겨두지 않는다.

하지만 삭제 전:

```text
C++ reference
Blueprint callable exposure
UPROPERTY reflected use
Chooser property
DataAsset reference
AnimBP getter
```

를 확인한다.

---

# 43. 주석/문서

리팩토링 후 다음을 문서화한다.

```text
State ownership
Data flow
GameThread / Worker boundary
StateController runtime
MM runtime
Locomotion semantic ownership
PlayerCharacter orchestration
```

기존 아키텍처 문서가 틀리면 갱신한다.

---

# 44. Debug / profiling

새 helper 안에 항상 켜지는 debug state를 만들지 않는다.

기존:

```text
CVar gated trace
WITH_EDITOR
WITH_DEV_AUTOMATION_TESTS
```

정책을 유지한다.

---

# 45. 코드 품질

다음을 우선한다.

```text
명확한 ownership
작은 public API
single responsibility
data-oriented snapshot
predictable lifecycle
explicit reset
```

다음을 피한다.

```text
복잡한 inheritance
template 과사용
generic framework
over-engineering
indirection만 증가하는 wrapper
```

---

# 46. 빌드

최종적으로 최소:

```text
Project_JEditor Win64 Development
Project_J Win64 Development
```

빌드한다.

가능하면 Shipping compile도 확인한다.

외부 파일 lock 등 비코드 문제로 최종 link가 실패하면 compile 결과와 원인을 정확히 구분해서 보고한다.

---

# 47. Automation

관련 테스트 전체를 실행한다.

최소 범위:

```text
Animation
Motion Matching
State Controller
Turn In Place
Locomotion
Remote movement
Combat transition
Weapon presentation
Combat SSR
Mount
```

기존 통과 테스트가 regression되지 않아야 한다.

---

# 48. 완료 조건

다음이 만족되면 이번 작업을 종료한다.

## CharacterAnimInstance

- StateController runtime state가 의미 있는 plain C++ runtime으로 분리됨.
- MM scheduling/reselect/cache state가 의미 있는 runtime으로 분리됨.
- AnimInstance는 snapshot/engine integration/facade 중심이 됨.
- Chooser reflection mirror가 source of truth가 아님.
- 기존 AnimBP public API가 보존됨.

## LocomotionAnimStateComponent

- semantic locomotion ownership은 유지됨.
- context calculation / TIP / remote / MM selection policy 중 실제 가치 있는 영역이 내부 helper로 분리됨.
- helper가 새 UObject/Tick을 만들지 않음.
- local/remote logic이 더 명확해짐.

## PlayerCharacter

- composition/lifecycle/orchestration 역할이 명확함.
- 이미 component가 소유하는 state machine을 중복 구현하지 않음.
- high-level bridge 외의 세부 policy가 감소함.

## 공통

- Build 성공.
- 관련 Automation Test 성공.
- Network/SSR/MM/Combat/Retarget 동작 회귀 없음.
- 신규 always-on Tick 없음.
- 신규 불필요 UObject/Subsystem 없음.
- worker thread UObject access 없음.
- Blueprint/AnimBP 계약을 불필요하게 깨지 않음.
- stale state reset contract가 명확함.

---

# 49. 작업 종료 규칙

이번 작업에서 가장 중요하다.

완료 조건을 만족한 뒤:

```text
더 작은 helper로 쪼갤 수 있다
파일이 아직 1000줄 넘는다
추상화를 더 만들 수 있다
모든 bool을 enum으로 바꿀 수 있다
더 generic한 framework를 만들 수 있다
```

같은 이유로 작업을 계속하지 않는다.

**파일 크기 자체가 완료 기준이 아니다.**

책임 경계와 state ownership이 명확해졌으면 종료한다.

---

# 50. 최종 보고 형식

작업 완료 시 다음 형식으로 보고한다.

```text
# Character Runtime Responsibility Refactor Result

## 1. HEAD / Build / Tests
- HEAD:
- Editor build:
- Game build:
- Tests:

## 2. CharacterAnimInstance

Before:
...

After:
...

Extracted runtime:
- StateController:
- MotionMatching:

Remaining responsibility:
...

## 3. LocomotionAnimStateComponent

Source of Truth:
...

Extracted helpers:
...

Remaining responsibility:
...

## 4. PlayerCharacter

Before:
...

After:
...

## 5. State Ownership Map

Combat:
Locomotion:
Trajectory:
State Controller:
Motion Matching:
Weapon:
Mount:
Input:
Replication:

## 6. Thread Boundary

GameThread:
Snapshot:
Worker:

## 7. Regression Protection
- tests added:
- stale state cases:
- remote/local cases:

## 8. Performance
Only code-provable changes:
- ...
Unmeasured:
- ...

## 9. Remaining Work

실제 material follow-up이 없다면:

No material code follow-up is required for this responsibility-refactor scope.
```

---

# 51. 시작 지시

현재 `Character_Test` HEAD에서 작업을 시작한다.

먼저 실제 코드 기준으로:

```text
UProject_JCharacterAnimInstance
UProject_JLocomotionAnimStateComponent
AProject_JPlayerCharacter
```

의 state ownership map을 작성한다.

그 다음:

```text
CharacterAnimInstance
→ StateControllerRuntime
→ MotionMatchingRuntime

LocomotionAnimState
→ Context Builder
→ Remote Runtime
→ TIP Runtime
→ MM Selection Policy

PlayerCharacter
→ orchestration cleanup
```

순서로 실제 가치가 있는 책임만 분리한다.

**새로운 시스템을 만드는 작업이 아니라, 현재 존재하는 시스템의 책임과 상태 소유권을 명확하게 만드는 작업이다.**

기존 동작을 유지하면서 버그 가능성, stale state, 수정 영향 범위, 테스트 난이도를 낮추는 것을 최우선으로 한다.
