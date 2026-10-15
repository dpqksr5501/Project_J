# 전체 코드 감사와 개선 요청

이 문서는 `Character_Test`의 소스 구조, 성능, 애니메이션, 전투, 네트워크를 점검하고 필요한 부분을 개선하기 위해 작성한 당시의 요청 기록이다. 실제 수행 내용과 검증 결과는 [코드 감사 결과](Project_J_Code_Audit_Result_2026-09-23.md)를 확인한다.

## 0. 작업 대상

Repository:

```text
https://github.com/dpqksr5501/Project_J
```

Target branch:

```text
Character_Test
```

Engine:

```text
Unreal Engine 5.8
```

이 작업은 단순한 코드 정리나 스타일 리팩토링이 아니다.

**현재 구현된 기능과 게임플레이 동작을 최대한 보존하면서, Project_J 전체 소스의 구조·성능·애니메이션 품질·전투 품질·네트워크 안정성·확장성을 종합적으로 감사하고 실제로 필요한 부분만 개선하는 작업**이다.

특히 다음을 목표로 한다.

- 불필요하거나 중복된 구조 제거
- legacy / migration / prototype 경로 정리
- 책임이 과도하게 집중된 클래스 분리 또는 내부 구조 단순화
- 런타임 비용 감소
- 캐릭터 애니메이션 품질 개선
- Runtime Retarget / Hand IK / Foot IK 고도화
- Motion Matching / Trajectory 품질 개선
- 전투 반응성과 공격 데이터 구조 개선
- 서버 권위 / SSR / 네트워크 구조 보존 및 검증
- 동기 로딩 및 반복 탐색 제거
- Development / Profiling 코드와 Production 코드 분리
- Module dependency / include 구조 개선
- 불필요한 신규 시스템이나 과도한 추상화는 만들지 않는다.

---

# 1. 가장 중요한 작업 원칙

## 1.1 먼저 현재 코드를 다시 읽는다

문서만 보고 판단하지 말 것.

반드시 현재 `Character_Test` 브랜치의 실제 소스 코드를 기준으로 판단한다.

기존 `Docs/` 문서에는 과거 상태를 설명한 자료가 섞여 있을 수 있다.

따라서:

```text
Current Source Code > 최신 Docs > 과거 Audit Docs
```

순서로 신뢰한다.

문서에서 문제라고 적혀 있더라도 현재 코드에서 이미 해결되었다면 다시 수정하지 않는다.

---

## 1.2 기능을 보존한다

이번 작업의 기본 정책은:

```text
현재 기능 유지
→ 구조 개선
→ 성능 개선
→ 품질 개선
```

이다.

기능 삭제가 필요한 경우에는 반드시 실제로 사용되지 않는 legacy / migration / prototype 코드인지 먼저 확인한다.

Blueprint, DataAsset, Config, GameplayTag, AnimBP에서 참조될 가능성이 있는 public property/function을 코드만 보고 바로 삭제하지 않는다.

---

## 1.3 "최적화처럼 보이는 코드"를 추가하지 않는다

다음과 같은 작업을 이유 없이 하지 않는다.

- Tick을 무조건 제거
- UObject/Component를 더 추가
- Async Task를 무조건 추가
- Actor를 무조건 Mass로 변환
- custom thread pool 추가
- custom object pool 추가
- 새로운 Manager/Subsystem 추가
- 추상화 계층을 무조건 추가
- 모든 것을 interface로 변경
- micro optimization을 위해 가독성을 심하게 희생

실제 비용이나 구조 문제가 확인된 경우에만 수정한다.

---

## 1.4 변경사항을 다음 4가지로 분류한다

모든 주요 영역을 다음 중 하나로 판단한다.

```text
KEEP
현재 구조가 적절하므로 유지

REFACTOR
기능은 유지하되 구조/책임/성능을 개선

REMOVE
실제 미사용 legacy/prototype/migration 코드이므로 제거 가능

MEASURE
병목 가능성은 있으나 측정 없이 변경하면 안 됨
```

---

# 2. 작업 진행 방식

아래 순서대로 진행한다.

```text
1. Repository / Docs / Source 재감사
2. 현재 아키텍처와 실제 코드의 차이 확인
3. 문제를 P0 / P1 / P2 / P3로 분류
4. 실제 개선 가치가 높은 항목부터 수정
5. Build
6. 관련 Automation Test 실행
7. 정적 코드 재검토
8. 완료 조건 충족 시 작업 종료
```

중요:

**완료 조건을 충족한 뒤 사소한 취향 차이 때문에 계속 리팩토링하거나 테스트 루프를 반복하지 않는다.**

새로운 실질적 결함이 발견되지 않으면 작업을 종료한다.

---

# 3. 우선순위

## P0

다음과 같은 실제 correctness 문제가 있을 경우 최우선 수정한다.

- crash
- invalid lifetime
- thread safety 위반
- server/client authority 오류
- SSR 시간축 오류
- stale animation state
- replication correctness 문제
- Gameplay/Presentation authority 혼합
- 공격 판정이 visual follower/IK에 의존
- blocking load가 실제 gameplay 중 발생
- 잘못된 state ownership

---

## P1

현재 프로젝트에서 가장 중요한 개선 대상이다.

```text
CharacterAnimInstance
LocomotionAnimStateComponent
PlayerCharacter orchestration
Runtime Retarget
Hand IK / Two-Handed Grip
Weapon Presentation
Motion Matching
Trajectory
Combat flow
Animation state ownership
```

---

## P2

다음으로 개선한다.

```text
Equipment / Inventory
Animation Budget
Iris / Replication policy
Asset loading
Module dependencies
Dev / Profiling code
Mount
Foot IK
VFX / rendering integration
```

---

## P3

명확한 문제가 없다면 유지한다.

```text
bounded NPC async architecture
TargetScoring task model
Input Binding -> Router -> Execution 구조
FastArray 기반 Inventory / Equipment
현재 SSR sweep history 기본 구조
GAS authority 구조
```

---

# 4. CharacterAnimInstance 감사

현재 `UProject_JCharacterAnimInstance`는 매우 큰 클래스이며 다음과 같은 책임이 집중되어 있다.

- thread-safe animation snapshot
- Motion Matching
- Chooser
- State Controller
- locomotion presentation
- one-shot transition
- pivot
- Turn In Place
- montage suppression
- combat intro/outro
- procedural IK
- aim
- mount
- debug
- optimization policy

단순히 파일을 여러 `.cpp`로 나누는 것은 충분한 리팩토링이 아니다.

실제 runtime responsibility가 분리 가능한지 확인한다.

특히 다음 구조를 검토한다.

```text
Gameplay / GT semantic state
        ↓
Compact animation snapshot
        ↓
AnimInstance runtime policy
        ↓
Anim Proxy
        ↓
Worker-thread evaluation
```

AnimInstance 내부의 State Controller / playback selection처럼 UObject가 필요 없는 로직은 필요하다면 plain C++ runtime/value object로 분리할 수 있다.

예:

```cpp
FProject_JStateControllerRuntime
```

단, 새로운 구조가 실제 복잡도를 줄이지 않는다면 만들지 않는다.

---

# 5. Animation Snapshot 비용 감사

`FProject_JAnimThreadSafeData` 및 관련 snapshot에서 다음을 확인한다.

- `FTransformTrajectory`
- `TArray`
- `FGameplayTagContainer`
- DatabaseTags
- Selection Context
- 기타 dynamic container

특히 매 frame 다음과 같은 deep copy 또는 allocation이 발생하는지 확인한다.

```text
Trajectory copy
GameplayTagContainer copy
TArray growth
temporary container allocation
```

가능하다면:

- persistent capacity 사용
- compact snapshot 사용
- 실제 AnimGraph에서 필요한 scalar/value만 전달
- immutable snapshot 유지

등을 검토한다.

단:

**Unreal Insights나 코드상 명확한 반복 allocation 근거 없이 snapshot 구조를 무리하게 바꾸지 않는다.**

---

# 6. Runtime Retarget 구조

현재 목표 구조는 다음과 같다.

```text
Leader Mesh
 └─ Motion Matching
 └─ Chooser
 └─ BlendStack
 └─ Gameplay Montage
 └─ Locomotion / TIP / Foot Placement

        ↓

Follower Mesh

Retarget Pose From Mesh
        ↓
Post-Retarget correction
        ↓
Hand IK / optional Foot IK
        ↓
Final visual pose
```

큰 구조는 유지한다.

Follower는 presentation이다.

Gameplay authority가 되면 안 된다.

---

# 7. Hand IK 고도화

현재 `UProject_JRetargetAnimInstance`는 weapon grip의 `FTransform`을 snapshot하지만 최종적으로 주로 위치를 사용한다.

예:

```cpp
RightGripLocation =
    SnapshotOwningCompWorldTransform
    .InverseTransformPosition(
        SnapshotRightGripWorldTransform.GetLocation());
```

이 구조에서는:

```text
손 위치는 Grip에 맞음
손목 회전은 원래 애니메이션에 의존
손바닥 방향도 원래 애니메이션에 의존
팔꿈치 방향은 solver에 의존
```

하여 신체 비율이 다른 캐릭터에서 어색할 수 있다.

다음을 검토하고 가능한 범위에서 개선한다.

## 7.1 Grip Transform 전체 활용

단순 위치뿐 아니라 필요하다면:

```text
Grip Position
Grip Rotation
```

을 AnimGraph에 전달한다.

손목 rotation correction이 실제 visual quality를 개선한다면 적용한다.

---

## 7.2 Character-specific Grip Calibration

다양한 캐릭터 에셋을 지원하기 위한 profile/data 구조를 검토한다.

예:

```text
Character Retarget / Grip Calibration Profile

RightHandPositionOffset
RightHandRotationOffset

LeftHandPositionOffset
LeftHandRotationOffset

RightElbowHint
LeftElbowHint

MaxArmStretch
Optional Shoulder/Palm correction
```

핵심 책임:

```text
Weapon
= "손잡이가 어디인가"

Character
= "이 캐릭터 손이 손잡이를 어떤 orientation으로 잡아야 하는가"
```

무기와 캐릭터 calibration 책임을 분리한다.

---

## 7.3 Two-handed weapon

대검 등의 양손 무기는 양쪽 손을 동일한 강도로 world target에 고정하지 않는다.

우선적으로 다음 구조를 검토한다.

```text
Primary Hand
= weapon primary grip 기준

Secondary Hand
= weapon primary transform이 결정된 뒤
  secondary grip을 따라가는 보조 IK
```

캐릭터 팔 길이가 달라져도 어깨/팔꿈치가 비정상적으로 꼬이지 않도록 한다.

---

## 7.4 Elbow / Joint Target

Two Bone IK에서 effector position만 사용하는 경우 elbow flipping / unnatural bending 가능성을 검사한다.

필요하면:

```text
character-calibrated joint target
weapon-relative elbow hint
animation-derived elbow direction
```

중 적절한 방식을 사용한다.

---

## 7.5 IK Alpha

현재 단순 `FInterpTo()`만으로 모든 동작을 처리하고 있다면 공격 동작 중 hand IK가 뒤늦게 따라오는 느낌을 확인한다.

가능하다면:

```text
Combat Idle
Draw
Attack Startup
Attack Active
Attack Recovery
Guard
Sheathe
```

상태별로 authored IK weight를 사용할 수 있게 한다.

Animation Curve / AttackDefinition / Weapon Presentation Profile 등 기존 데이터 구조와 가장 잘 맞는 곳을 선택한다.

하드코딩을 늘리지 않는다.

---

# 8. Foot IK / Retarget correction

서로 다른 신장/다리 길이에서:

```text
Leader Foot Placement
→ Runtime Retarget
```

만으로 충분한지 확인한다.

가까운 캐릭터에서만 필요하다면:

```text
Runtime Retarget
→ lightweight follower final foot correction
```

을 검토한다.

단 모든 캐릭터에 이중 Foot IK를 돌리는 구조는 피한다.

Animation Quality Tier와 연동한다.

예:

```text
Local / Near
Full

Mid
Reduced

Far
Off

Hidden
Off
```

---

# 9. PlayerCharacter 책임 감사

`AProject_JPlayerCharacter`는 현재 많은 component를 소유하고 orchestration한다.

다음 책임을 확인한다.

- locomotion
- trajectory
- combat mode
- TIP
- animation event
- equipment presentation
- mount
- interaction
- handover
- movement speed
- rotation mode

PlayerCharacter를 무조건 작게 만들 필요는 없다.

하지만 component가 이미 source of truth인데 Character에도 같은 state가 존재하는 경우를 찾는다.

특히:

```text
bIsAttacking
bIsDodging
bIsHitReacting
bIsPlayingCombatIntro
bPendingCombatModeFromIntro
```

등 compatibility mirror가 실제로 필요한지 Blueprint/reference audit 후 판단한다.

가능하면 state owner를 하나로 만든다.

---

# 10. LocomotionAnimStateComponent

`UProject_JLocomotionAnimStateComponent`는 사실상 큰 native locomotion state machine이다.

다음을 구분할 수 있는지 확인한다.

```text
movement observation
semantic locomotion state
network reconstruction
transition policy
animation presentation state
timer/retry policy
```

특히 Gameplay/Movement semantics와 Animation presentation-specific state가 섞여 있다면 정리한다.

단 기존 동작이 복잡하므로 대규모 재작성보다는 안전한 내부 경계 개선을 우선한다.

---

# 11. Motion Matching

Motion Matching 관련 전체 구조를 감사한다.

대상:

```text
Pose Search Schema
Pose Search Database
Chooser
Blend Stack
State Controller
Continuing Pose
Reselect policy
Search frequency
Trajectory Channel
Pose Channel
Velocity / Heading weighting
Start
Cycle
Stop
Pivot
TIP
Jump
Fall
Land
Combat locomotion
```

다음 질문을 기준으로 본다.

- 실제로 사용되지 않는 GASP migration logic이 남아 있는가?
- 동일한 상태를 여러 곳에서 판단하는가?
- Search를 필요 이상 자주 하는가?
- Continuing Pose로 해결 가능한데 매번 search하는가?
- animation database가 목적별로 명확히 분리되어 있는가?
- Pivot/TIP/Start/Stop state ownership이 명확한가?

---

# 12. Motion Matching Trajectory

현재 trajectory component는 기본적으로 유지 가치가 높다.

이미 다음 기능이 존재한다.

- Dedicated Server skip
- local/remote generation 분리
- recently rendered gating
- same-frame duplicate protection
- reset revision
- remote smoothing
- remote facing repair
- future velocity query

이를 유지하면서 다음을 감사한다.

## 12.1 Context-specific trajectory

다음 movement context에서 동일한 trajectory policy가 항상 적합한지 확인한다.

```text
Normal OTM
Combat Strafe
Sprint
Dash
Root Motion Attack
Knockback
Mounted
Swimming
Flying
```

필요한 경우 movement context를 명시적으로 전달하되 과도하게 복잡한 strategy pattern은 만들지 않는다.

---

## 12.2 Remote proxy

현재 remote trajectory facing repair는 유용하지만 최종적으로는 잘못 만들어진 trajectory를 나중에 repair하는 것보다 remote input 부족을 감안한 generation을 개선할 수 있는지 검토한다.

사용 가능한 값:

```text
Replicated Velocity
Actor Rotation
Previous Velocity
Network Smoothing State
Combat Strafe State
Rotation Mode
```

단 remote와 local의 presentation 결과가 이미 안정적이면 불필요하게 수정하지 않는다.

---

## 12.3 Smoothing

현재 frame-dependent smoothing이 사용된다면:

\[
\alpha = Clamp(\Delta t \cdot k,0,1)
\]

과

\[
\alpha = 1-e^{-k\Delta t}
\]

형태의 frame-rate-independent exponential smoothing을 비교할 수 있다.

실제 MM query 품질이 개선될 때만 적용한다.

---

# 13. Combat Architecture

현재 GAS + ComboDefinition + AttackDefinition 구조는 유지한다.

다만 공격 lifecycle이 여러 곳에 분산되어 있다면 다음 semantic phase를 더 명확히 정의할 수 있는지 확인한다.

```text
Startup
Active
Recovery
Cancel Window
```

가능하면 같은 AttackDefinition을 기반으로 다음이 함께 움직이도록 한다.

```text
movement permission
rotation permission
root motion policy
motion warping
combo input window
cancel window
hit window
weapon trajectory
hand IK weight
trail / VFX
camera presentation
```

단 하나의 거대한 AttackDefinition에 모든 것을 강제로 넣지 않는다.

기존 구조와 책임 분리를 유지하면서 공유 가능한 semantic timing을 정리한다.

---

# 14. Combat Movement

다음을 검사한다.

- 공격 중 rotation ownership
- camera-facing attack
- root motion attack
- movement lock
- attack cancellation
- montage blend-out
- dodge/hit reaction priority
- combat intro/outro interrupt

공격 종료 시 locomotion state에 잘못된 landing/start transition을 만들지 않는지 확인한다.

---

# 15. Gameplay Weapon Trajectory와 Presentation 분리

매우 중요하다.

최종 목표:

```text
                AttackDefinition
                     │
          ┌──────────┴───────────┐
          ↓                      ↓

Gameplay Trajectory        Visual Presentation
          ↓                      ↓
Server authoritative       Weapon Mesh
Hit Sweep                  Trail
SSR                        Independent motion
                           Hand IK
```

다음 규칙을 지킨다.

**서버 공격 판정은 Follower Retarget / Hand IK / visual weapon actor에 의존하면 안 된다.**

Client visual은 gameplay canonical trajectory를 최대한 자연스럽게 표현한다.

---

# 16. SSR / Hit Validation

현재 authoritative sweep history가 존재한다.

다음을 보존한다.

```text
ServerTimestamp
PredictionKey
AttackNodeTag
TraceStart
TraceEnd
HitWindow state
```

현재 server-side rewind에서 attacker sweep과 target rewind가 동일 timestamp domain으로 처리되는지 재확인한다.

이미 제대로 되어 있다면 재작성하지 않는다.

검사 대상:

- request timestamp bounds
- future request
- old request
- hit window transition boundary
- prediction key mismatch
- attack node mismatch
- interpolation between different attacks
- replay/duplicate request
- range/arc validation
- line-of-sight / obstruction policy

---

# 17. WeaponPresentationComponent

현재 구조를 감사한다.

중요 항목:

```text
weapon actor lifecycle
drawn/sheathed attachment
Grip targets
Grip socket cache
Independent weapon motion
Ground correction
VFX attachment
IK integration
Animation budget integration
```

## 반복 component search

Grip socket은 cache가 존재하므로 유지한다.

다른 경로, 특히 VFX attachment 등에서 반복적으로:

```cpp
GetComponents(...)
DoesSocketExist(...)
```

scan이 발생하는지 확인한다.

무기 spawn/profile/socket 변경 시 cache를 갱신하고 frame path에서는 cache를 사용하는 방식이 적합하면 적용한다.

---

# 18. Draw / Sheathe

무기 socket을 즉시 변경하여 한 프레임 튀는 문제가 있는지 확인한다.

```text
Sheathe socket
→ notify
→ hand socket
```

전환에서 필요하다면:

- authored notify timing
- temporary attachment blending
- IK alpha coordination
- weapon motion interpolation

을 이용한다.

무기 gameplay state와 visual attachment state가 충돌하지 않게 한다.

---

# 19. Asset Loading

Gameplay path에서 다음을 찾는다.

```text
LoadSynchronous()
StaticLoadObject()
ConstructorHelpers outside constructor
blocking asset resolve
```

특히 mount/equipment/weapon/animation 관련 runtime sync load를 찾는다.

실제 cold-load hitch 가능성이 있다면:

```text
preload
StreamableManager
async request
soft reference warm-up
```

중 프로젝트에 가장 간단한 방식을 사용한다.

단 모든 SoftObjectReference를 무조건 async 시스템으로 감싸지 않는다.

---

# 20. Animation Budget / LOD

현재 Animation Budget Allocator와 animation optimization policy는 기본적으로 유지한다.

다음 구조를 검증한다.

```text
Local
Near
Mid
Far
Hidden
```

각 tier에서:

```text
Leader update frequency
Follower Retarget
Retarget IK
Hand IK
Foot IK
URO
Montage protection
Gameplay pose requirement
```

이 올바르게 조합되는지 본다.

Leader와 Follower를 서로 독립적으로 줄여 pose desync를 만드는 정책은 피한다.

---

# 21. Input

현재:

```text
Enhanced Input Binding
        ↓
SkillInputRouter
        ↓
SkillInputExecution / GAS
```

구조는 유지한다.

단 다음을 테스트한다.

- chord input
- grace window
- same-frame Completed/Canceled/Triggered reconciliation
- network authoritative re-resolution
- duplicate input
- sequence validation
- rate limit
- attack combo buffering

구조를 단순화한다는 이유만으로 3개 component를 합치지 않는다.

---

# 22. Equipment / Inventory

현재 PlayerState persistent ownership 및 FastArray 방향은 유지한다.

다음만 감사한다.

- replicated data duplication
- unnecessary owner/public replication
- PlayerCharacter에 남은 migration state
- prototype startup equipment
- presentation와 authoritative equipment state 중복
- equip revision / visual refresh race

다음과 같은 prototype/migration property가 실제 사용되지 않는다면 정리 후보이다.

```text
PrototypeStartingWeapon
direct LocomotionProfile fallback
direct MotionMatchingAssetSet fallback
```

단 실제 asset reference를 먼저 검색한다.

---

# 23. NPC / Async Architecture

현재 bounded async architecture는 좋은 구조이므로 기본적으로 유지한다.

예:

```text
NPCDecisionSubsystem
TargetScoringSubsystem
bounded query count
bounded outstanding tasks
bounded completion
world epoch
cancel token
```

다음이 확인되지 않는 한 custom thread system으로 바꾸지 않는다.

- 실제 GT bottleneck
- task scheduling bottleneck
- excessive memory churn
- starvation

---

# 24. Mass

Actor NPC를 무조건 Mass로 바꾸지 않는다.

Mass는 representation/simulation scale이 필요한 영역에만 적용한다.

Gameplay-rich nearby NPC는 Actor가 더 적절할 수 있다.

측정과 요구사항 없이 broad Actor → Mass migration 금지.

---

# 25. Mount

Mount 전체를 재작성하지 않는다.

다음을 우선 확인한다.

- synchronous loading
- duplicated state
- mounted/unmounted animation state
- locomotion trajectory disable/restore
- replication
- GAS health/state source of truth
- animation layer transition

---

# 26. Network / Iris

다음 원칙을 유지한다.

```text
Server authoritative gameplay
Minimal semantic replication
No per-frame IK transform replication
No per-frame bone replication
Remote animation reconstructed from replicated movement/state
```

확인 대상:

- relevancy
- dormancy
- owner-only data
- FastArray policy
- replicated anim event recovery
- jump event/state dual path
- packet-loss recovery
- unnecessary RPC
- unreliable multicast misuse

ReplicatedJumpState와 ReplicatedAnimEvent가 비슷해 보여도 의미가 다르면 억지로 합치지 않는다.

---

# 27. Development / Profiling 코드

Production runtime class에 profiling/debug 기능이 과도하게 들어가 있는지 확인한다.

특히 PlayerController에 존재하는:

```text
DumpMMOState
DumpAnimBudget
DumpLocomotionKinematics
Motion Matching traces
DumpReplicationPolicy
DumpCharacterComponents
DumpCombatState
profiling visual crowd
replicated movement crowd
```

등을 검토한다.

Shipping에서 compile/run되지 않더라도 production PlayerController가 profiling component를 항상 생성한다면:

```text
CheatManager
Development-only component
#if !UE_BUILD_SHIPPING
Editor/Developer module
```

등 더 적절한 위치로 이동할 수 있는지 검토한다.

---

# 28. Module Dependencies

모듈 구조를 감사한다.

특히 `Project_JCharacter`가 매우 큰 모듈이다.

그러나 곧바로 새 모듈을 만들지 않는다.

먼저:

```text
PublicDependency → PrivateDependency 가능 여부
Public header include 감소
forward declaration
module DAG
unused dependency
circular conceptual dependency
```

를 정리한다.

그 이후에도 명확한 독립 책임이 존재할 때만 모듈 분리를 고려한다.

---

# 29. Logging

high-frequency runtime path에서 `Log` 레벨 로그가 과도한지 확인한다.

예:

```text
combat state changes
combo events
animation state
network event
```

필요하면:

```text
Verbose
VeryVerbose
CVar gated logging
```

으로 변경한다.

Warning/Error는 실제 문제에만 사용한다.

---

# 30. Tick 감사

모든 Tick을 목록화한다.

각 Tick에 대해:

```text
왜 필요한가?
몇 Hz인가?
Local only인가?
Server에서도 필요한가?
recently rendered gating 가능한가?
event-driven으로 바꾸는 게 실제로 더 나은가?
```

를 판단한다.

하지만 다음은 현재 정당한 이유가 있을 수 있으므로 무조건 제거하지 않는다.

- Player movement/trajectory current-frame update
- TIP
- locomotion semantic state
- one-frame input reconciliation
- animation budget policy
- lease owner death cleanup
- bounded subsystem scheduler

---

# 31. Memory / Allocation 감사

hot path에서 다음을 찾는다.

```text
TArray temporary
GetComponents
GetAttachedActors
FGameplayTagContainer copy
trajectory deep copy
FString formatting
temporary FName/string conversion
dynamic delegate churn
frequent FindComponentByClass
```

실제 frame path에 있고 반복 호출된다면 cache 또는 persistent storage를 검토한다.

초기화/장비 변경처럼 드문 경로라면 과도하게 최적화하지 않는다.

---

# 32. Thread Safety

Animation worker thread 또는 UE::Tasks에서 다음을 금지한다.

- UObject unsafe access
- World access
- Actor/component mutation
- non-thread-safe gameplay state access

worker에는 pure value snapshot만 전달한다.

현재 bounded snapshot → worker → GT apply 패턴은 유지한다.

---

# 33. Character Asset Onboarding

새 캐릭터 에셋을 쉽게 추가할 수 있도록 현재 workflow를 감사한다.

목표는 가능하면 다음 정도로 단순화한다.

```text
Skeletal Mesh
IK Rig / Retargeter
Character Animation / Retarget Profile
```

캐릭터마다 C++ 수정이나 AnimBP duplicate가 과도하게 필요한 구조는 피한다.

가능하다면 profile 기반으로 다음을 구성한다.

```text
retarget asset
grip calibration
foot offset
IK limits
animation quality options
body proportion correction
```

---

# 34. 테스트

변경사항과 관련된 테스트만 실행한다.

필요한 경우 새로운 automation test를 추가한다.

테스트 대상 예:

```text
Combat State
Combo transition
Hit window
SSR timestamp/history
Locomotion transition
TIP
Trajectory reset
Remote trajectory
Equipment transition
Weapon grip target
Animation budget tier
Mount transition
```

모든 변경마다 전체 프로젝트의 모든 테스트를 무조건 반복 실행하지 않는다.

큰 milestone 또는 최종 검증에서 전체 relevant suite를 실행한다.

---

# 35. 성능 검증

가능한 경우 Unreal Insights 또는 existing profiling hooks 기준으로 다음을 확인한다.

```text
Game Thread
Animation Worker
Pose Search
AnimGraph
Follower Retarget
Hand IK
Foot IK
Weapon Presentation
Trajectory
NPC scheduler
Replication
Asset loading hitch
```

측정할 수 없는 경우:

```text
"성능 개선됨"
```

이라고 단정하지 않는다.

대신:

```text
"allocation 제거"
"component scan 제거"
"sync load 제거"
"copy 감소"
```

처럼 코드상 확인 가능한 사실만 기록한다.

---

# 36. 하지 말아야 할 것

다음은 금지한다.

```text
모든 코드를 새로 작성
모든 Component를 합침
모든 Component를 쪼갬
Tick 0개 만들기
Mass로 전부 이전
무조건 비동기화
custom ECS 추가
custom job system 추가
Gameplay 상태를 AnimInstance가 소유
visual Follower를 server hit authority로 사용
IK 위치를 replication
코드 스타일 때문에 public API 대량 파괴
Blueprint reference 확인 없이 UPROPERTY 제거
문서에 적혀 있다는 이유만으로 현재 해결된 문제 재수정
```

---

# 37. 예상 KEEP 영역

현재 코드 감사 시 문제가 없다면 다음은 우선 유지한다.

```text
GAS authority
PlayerState persistent ASC pattern
FastArray inventory/equipment
Input Binding → Router → Execution
bounded NPC decision scheduling
bounded UE::Tasks target scoring
authoritative SSR sweep history
animation quality tier concept
Leader → Follower runtime retarget concept
semantic replication instead of per-frame IK replication
```

---

# 38. 우선 REFACTOR 후보

현재 실제 코드와 사용처를 재검증한 뒤 다음을 우선 본다.

```text
UProject_JCharacterAnimInstance
UProject_JLocomotionAnimStateComponent
AProject_JPlayerCharacter orchestration
UProject_JRetargetAnimInstance
UProject_JWeaponPresentationComponent
Motion Matching state/control logic
Trajectory remote prediction
Combat phase/timing ownership
Runtime asset loading
Production PlayerController profiling responsibilities
Project_JCharacter public dependencies
```

---

# 39. REMOVE 후보

반드시 reference audit 후에만 제거한다.

```text
migration-only LocomotionProfile
migration-only MotionMatchingAssetSet
PrototypeStartingWeapon
legacy compatibility booleans
unused fallback weapon detection
stale debug fields
stale code/comments
unused test hooks
```

---

# 40. 최종 완료 조건

아래가 만족되면 작업을 종료한다.

- 프로젝트가 빌드된다.
- 관련 Automation Test가 통과한다.
- 기존 핵심 gameplay 기능이 유지된다.
- 신규 crash / compile warning / replication error가 없다.
- state ownership이 이전보다 명확하다.
- 불필요한 legacy/prototype path가 안전하게 정리되었다.
- Hand IK가 다양한 캐릭터 체형에서 더 자연스럽게 확장 가능한 구조다.
- Gameplay weapon trajectory와 visual weapon presentation 경계가 명확하다.
- Motion Matching / Trajectory에 불필요한 중복 계산이 없다.
- runtime sync load 또는 반복 component scan 중 명확한 문제를 제거했다.
- Dev/profiling 기능이 Production architecture를 오염시키지 않는다.
- 의미 없는 신규 subsystem/component/manager를 만들지 않았다.
- P0/P1 문제를 모두 해결하거나 명확한 이유로 KEEP/MEASURE 판정했다.

**위 조건을 충족했다면 더 이상 사소한 개선점을 찾아 무한히 작업을 이어가지 말 것.**

---

# 41. 최종 보고 형식

작업 완료 시 다음 형식으로 보고한다.

```text
# Project_J Audit Result

## 1. Overall
- 완료 여부
- build 결과
- test 결과

## 2. Fixed
### P0
- ...

### P1
- ...

### P2
- ...

## 3. Kept Intentionally
- 구조
- 유지 이유

## 4. Removed
- legacy/prototype code
- 제거 근거

## 5. Measured / Needs Profiling
- 코드상 병목 가능성
- 실제 측정 없이는 변경하지 않은 이유

## 6. Architecture Changes
Before:
...

After:
...

## 7. Gameplay / Animation Quality Improvements
- Runtime Retarget
- Hand IK
- Foot IK
- Motion Matching
- Trajectory
- Combat

## 8. Performance-related Changes
- allocation
- sync loading
- component scan
- tick
- replication
- animation evaluation

## 9. Remaining Work
정말 후속 작업이 필요한 항목만 작성.

없다면:

"No material follow-up work is required for this audit scope."

라고 명시한다.
```

---

# 42. 시작 지시

이제 `Character_Test` 브랜치의 현재 HEAD를 기준으로 작업을 시작한다.

먼저 Docs와 Source를 다시 읽고 현재 상태를 재구성한다.

과거 audit 결과를 그대로 믿지 말고 실제 코드에서 이미 해결된 문제를 제거한다.

그 후 전체 소스를 감사해:

```text
KEEP
REFACTOR
REMOVE
MEASURE
```

로 분류하고,

**실제 가치가 높은 P0/P1 문제부터 수정한다.**

특히 첫 번째 집중 영역은:

```text
Runtime Retarget
Hand IK
CharacterAnimInstance
LocomotionAnimStateComponent
PlayerCharacter
Weapon Presentation
Motion Matching
Trajectory
Combat
```

이다.

단 이 영역만 보고 끝내지 말고 이후 Equipment, Network, Asset Loading, Modules, Dev Code 등 전체 소스도 점검한다.

최종 목표는 단순히 코드가 예뻐지는 것이 아니라:

**캐릭터 에셋을 여러 개 적용해도 자연스럽고, 전투가 안정적이며, 네트워크 환경에서도 올바르게 동작하고, 대규모 MMORPG로 확장 가능한 Project_J 런타임 구조를 만드는 것**이다.
