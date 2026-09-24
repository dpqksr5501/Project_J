# Project J Runtime Retarget / Hand IK / SSR 후속 개선 프롬프트

## 역할

당신은 Unreal Engine 5.8 기반 대규모 액션 MMORPG의 **Senior Gameplay / Animation / Networking Engineer**입니다.

현재 프로젝트는 Motion Matching + Chooser + BlendStack 기반의 Leader Mesh와, 이종 스켈레톤 외형을 위한 Follower Mesh의 `Retarget Pose From Mesh` 구조를 사용하고 있습니다.

이번 작업의 목적은 기존 아키텍처를 뒤엎는 것이 아니라, 최근 외부 아키텍처 리뷰 반영 커밋 이후 남아 있는 **실제 코드 레벨의 결함과 확장성 문제를 보완**하는 것입니다.

---

# 0. 저장소 / 작업 기준

- Repository:
  - https://github.com/dpqksr5501/Project_J
- Branch:
  - `Character_Test`
- 현재 검토 기준 HEAD:
  - `de26358230a64d16acac81dad36d244767508b1c`
- Unreal Engine:
  - `5.8`
- 주요 기술:
  - Motion Matching
  - Chooser
  - BlendStack
  - GAS
  - Iris
  - Anim Budget Allocator
  - Significance Manager
  - Runtime IK Retargeting
  - Server Side Rewind

먼저 반드시 현재 브랜치의 실제 코드를 다시 읽고, 아래 내용이 여전히 유효한지 확인한 뒤 수정하세요.

**문서만 보고 구현하지 말고 반드시 현재 소스 코드 기준으로 판단하십시오.**

---

# 1. 현재 아키텍처

캐릭터 구조는 다음과 같습니다.

```text
CapsuleComponent
└─ Leader Mesh
   ├─ Motion Matching
   ├─ Chooser
   ├─ BlendStack
   ├─ Montage / Root Motion
   └─ Gameplay Animation Authority
      │
      └─ Follower Visual Mesh
         ├─ Retarget Pose From Mesh
         ├─ Post-Retarget Hand IK
         ├─ 향후 Foot IK
         └─ 최종 렌더링
```

Dedicated Server에서는 Follower visual animation과 visual weapon presentation을 실행하지 않는 구조를 지향합니다.

서버 판정은 Follower visual pose가 아니라 Leader / canonical gameplay trajectory를 기준으로 해야 합니다.

---

# 2. 이전 리뷰 반영 상태

최근 커밋에서 다음 항목들이 반영되었습니다.

## 완료 또는 1차 완료된 항목

### Hidden Leader + ABA

`UProject_JBudgetedSkeletalMeshComponent`

```cpp
SetShouldUseActorRenderedFlag(true);
bBudgetTickWhenNotRendered = true;
```

그리고 Player Leader Mesh:

```cpp
VisibilityBasedAnimTickOption =
    EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
```

가 적용되었습니다.

---

### Dedicated Server visual isolation

`UProject_JWeaponPresentationComponent::CanCreatePresentation()`

에서 Dedicated Server를 차단하고,

`UProject_JRetargetAnimInstance::NativeInitializeAnimation()`

에서 Dedicated Server일 경우 Follower Mesh tick을 끄도록 수정되었습니다.

---

### SSR attacker sweep history

`FProject_JAuthoritativeSweepRecord`

기반의 공격자 무기 스윕 history가 추가되었고,

```cpp
FindAuthoritativeTraceAtTime(ClientTimestamp, ...)
```

을 통해 SSR 요청 시 과거 공격자 trace를 찾도록 구현되었습니다.

---

### Hand IK 통합

`UProject_JRetargetAnimInstance`

가

`UProject_JWeaponPresentationComponent::GetWeaponGripTargets()`

를 읽고,

- RightGripLocation
- LeftGripLocation
- RightGripAlpha
- LeftGripAlpha

를 계산하도록 변경되었습니다.

---

# 3. 이번 작업의 핵심 목표

이번 작업은 아래 세 묶음으로 진행하세요.

우선순위:

```text
P0
Hand IK pipeline correctness

P0
SSR historical validation correctness

P1
Leader/Follower unified animation quality tier
```

각 작업 후 반드시 빌드하고, 가능하면 자동화 테스트 또는 최소한 재현 가능한 런타임 검증 포인트를 남기세요.

---

# 4. P0 — Hand IK 파이프라인 수정

현재 코드에는 다음 문제가 남아 있을 가능성이 높습니다.

## 4.1 GripTargets가 Independent Weapon Motion 중에만 유효한 문제

현재 `UProject_JWeaponPresentationComponent::UpdateGripTargets()`는 대략 다음 구조입니다.

```cpp
GripTargets = FProject_JWeaponGripTargets();

if (!bIndependentMotionActive)
{
    return;
}
```

이 경우 다음 상태에서는 GripTargets가 비어 있을 수 있습니다.

```text
Sheathed Idle
Combat Idle
Combat Locomotion
Independent Weapon Motion이 없는 공격 구간
일반 발도 상태
일반 납도 상태
```

그러나 `UProject_JRetargetAnimInstance`는 `GetWeaponGripTargets()`를 우선적으로 사용합니다.

동시에:

```cpp
bAutoDetectWeaponIfNull = false;
```

이므로 fallback도 기본적으로 꺼져 있습니다.

### 요구사항

`GripTargets`는 **Independent Weapon Motion 여부와 관계없이 안정적인 현재 무기 Grip Target을 제공**해야 합니다.

구조를 다음처럼 정리하세요.

```text
WeaponPresentationComponent
    │
    ├─ Stable Weapon State
    │   ├─ Sheathed
    │   └─ Drawn
    │
    ├─ Spawned Weapon
    │
    ├─ Primary Grip Socket
    ├─ Secondary Grip Socket
    │
    └─ Optional Independent Motion Override
            ↓
      FProject_JWeaponGripTargets
```

즉 Independent Motion은 Grip Target을 생성하는 유일한 경로가 아니라,

**기본 weapon socket 기반 grip transform 위에 덮어쓰는 optional presentation layer**

가 되어야 합니다.

---

## 4.2 Authored IK Alpha를 실제로 사용

현재 구조에서는 `FProject_JWeaponGripTargets`가 이미:

```cpp
float PrimaryIKAlpha;
float SecondaryIKAlpha;
```

를 갖고 있습니다.

하지만 `UProject_JRetargetAnimInstance`에서 이를 무시하고 다음처럼 강제로 1.0을 쓰는 코드가 있는지 확인하세요.

```cpp
TargetRightAlphaSnapshot =
    bIsCombatMode
    ? (bEnableCombatGripIK ? 1.0f : 0.0f)
    : 1.0f;
```

이 구조라면 수정해야 합니다.

### 요구사항

Presentation에서 전달된 alpha가 최종 authority가 되어야 합니다.

예:

```cpp
TargetRightAlphaSnapshot =
    bEnableCombatGripIK
    ? GripTargets.PrimaryIKAlpha
    : 0.0f;

TargetLeftAlphaSnapshot =
    bEnableCombatGripIK
    ? GripTargets.SecondaryIKAlpha
    : 0.0f;
```

단, Sheathed 상태의 back-grip IK가 별도 의미를 갖는다면 이를 임의로 삭제하지 말고 상태를 명확히 분리하세요.

예:

```text
DrawnPrimaryGripAlpha
DrawnSecondaryGripAlpha
SheathedGripAlpha
```

또는 presentation socket state 기준으로 결정해도 됩니다.

중요한 것은:

**artist-authored alpha를 코드가 1.0으로 덮어쓰지 않는 것**입니다.

---

# 4.3 매 프레임 snapshot 상태 초기화

현재 프레임에 Primary만 valid하고 이전 프레임의 Secondary가 valid했던 경우,

이전 `LeftGripLocation / bHasValidLeftSnapshot` 상태가 남을 가능성이 없는지 검사하세요.

`NativeUpdateAnimation()`에서 새 resolution을 시작하기 전에 반드시 현재 프레임용 state를 초기화하는 구조를 권장합니다.

예:

```cpp
bHasValidRightSnapshot = false;
bHasValidLeftSnapshot = false;

TargetRightAlphaSnapshot = 0.0f;
TargetLeftAlphaSnapshot = 0.0f;
```

그 후 현재 프레임에서 실제로 존재하는 target만 다시 채우세요.

stale transform이 유지되어 한 손이 이전 위치에 붙어 있는 현상을 방지해야 합니다.

---

# 4.4 UpdateWeaponTarget 이벤트 연결 검증

현재:

```cpp
void UpdateWeaponTarget(
    USceneComponent* InWeaponComponent,
    FName InSocketName);
```

가 존재하지만 실제 C++ equipment / presentation lifecycle에서 호출되는지 확인하세요.

다음 흐름을 검사하십시오.

```text
Equip
Unequip
Weapon spawned
Weapon destroyed
Weapon profile changed
Weapon actor replaced
Sheathe
Draw
Respawn
Possession
OnRep
```

`bAutoDetectWeaponIfNull = false`를 production default로 유지하려면,

**weapon lifecycle이 변경될 때 반드시 event-driven target refresh가 보장되어야 합니다.**

가능하면 다음처럼 단방향 ownership을 유지하세요.

```text
Equipment Runtime
       ↓
Weapon Presentation Component
       ↓
Retarget AnimInstance
```

AnimInstance가 world actor/component를 매 프레임 탐색하는 구조로 되돌아가면 안 됩니다.

---

# 4.5 Thread-safe snapshot 원칙 유지

다음 원칙을 깨지 마세요.

```text
Game Thread
UObject / Actor / Component 접근
        ↓
snapshot 저장
        ↓
Worker Thread
순수 수학 연산만 수행
```

`NativeThreadSafeUpdateAnimation()` 안에서는:

- FindComponent
- GetWorld
- GetActor
- socket UObject query
- gameplay object query

등을 하지 마세요.

필요하다면 향후 `FAnimInstanceProxy::PreUpdate()` 기반으로 옮길 수 있도록 구조를 정리해도 좋습니다.

---

# 5. P0 — SSR historical validation 강화

현재 attacker sweep history가 생겼지만, 단순한 "가장 가까운 timestamp sample" 방식만으로는 완성된 SSR이라고 보기 어렵습니다.

아래 사항을 개선하세요.

---

# 5.1 History 범위 밖 timestamp reject

현재 구현이 history에서 가장 가까운 sample을 무조건 선택한다면 다음 문제가 있습니다.

예:

```text
History:
100.50
100.52
100.54

Request:
99.90
```

이 경우 100.50을 쓰면 안 됩니다.

### 요구사항

최소한 다음 validation을 추가하세요.

```text
TargetTimestamp < OldestTimestamp
→ reject

TargetTimestamp > NewestTimestamp + small tolerance
→ reject
```

또는:

```cpp
MaxSweepTimestampError
```

를 설정해 일정 오차 이상이면 실패시키세요.

fallback으로 최신 trace를 사용하는 구조는 SSR validation에서는 제거하거나 매우 제한적으로 사용하세요.

---

# 5.2 Fixed ring buffer 구조로 변경

현재 다음처럼 되어 있다면:

```cpp
if (History.Num() >= 32)
{
    History.RemoveAt(0);
}

History.Add(NewRecord);
```

이를 고정 크기 ring buffer 방식으로 변경하는 것을 권장합니다.

기존:

`UProject_JServerSideRewindComponent`

의 history 구현 패턴을 재사용하세요.

예:

```text
Array
StartIndex
Count
```

불필요한 배열 shift를 없애세요.

---

# 5.3 entry 개수가 아니라 시간 길이 기준

`32 records`가 항상 `0.5~1.0 sec`를 의미하지 않습니다.

실제 sampling rate에 따라 달라집니다.

따라서:

```cpp
MaxSweepHistorySeconds
```

기준으로 관리하세요.

가능하면:

```text
MaxSweepHistorySeconds
>= HitValidationPolicy.MaxRequestAge
```

가 되도록 보장하세요.

예:

```cpp
MaxSweepHistorySeconds =
    HitValidationPolicy.MaxRequestAge + SafetyMargin;
```

---

# 5.4 trace interpolation

단순 nearest sample만 사용하면 서버 tick rate나 animation update cadence가 낮아졌을 때 오차가 커질 수 있습니다.

가능하다면 target timestamp를 감싸는 두 공격자 sweep sample을 찾고 보간하세요.

단순 선형 보간이 의미적으로 적절한 경우:

```cpp
Start = Lerp(A.Start, B.Start, Alpha);
End   = Lerp(A.End,   B.End,   Alpha);
```

를 사용할 수 있습니다.

그러나 서로 다른 공격 phase / node를 가로질러 보간하면 안 됩니다.

반드시 다음 identity가 같아야 합니다.

```text
AttackNode
PredictionKey
Attack Activation
```

필요하면 history record에 `PredictionKey`를 추가하세요.

---

# 5.5 Historical hit-window validation

현재 SSR 요청이 과거 시점의 공격인데도 다음 현재 상태를 검사하는지 확인하세요.

```cpp
if (!bHitWindowOpen)
{
    return HitWindowClosed;
}
```

이는 완전한 historical SSR이 아닙니다.

SSR에서는 가능하면:

```text
ClientTimestamp 당시
AttackNode가 무엇이었는가
PredictionKey가 무엇이었는가
HitWindow가 열려 있었는가
Weapon Sweep이 무엇이었는가
```

를 같은 시간축으로 검증해야 합니다.

권장 history record:

```cpp
struct FProject_JAuthoritativeSweepRecord
{
    float ServerTimestamp;

    FVector TraceStart;
    FVector TraceEnd;

    FGameplayTag AttackNodeTag;

    int32 PredictionKey;

    bool bHitWindowOpen;
};
```

또는 별도의 attack state history를 구성해도 됩니다.

최종적으로 다음 구조를 목표로 하세요.

```text
ClientTimestamp
      ↓
Historical Attacker State
├─ Attack Activation
├─ PredictionKey
├─ AttackNode
├─ HitWindow
└─ Weapon Sweep
      ↓
Historical Target Capsule
      ↓
Intersection
      ↓
Server Damage Authority
```

---

# 5.6 Client trace는 authority가 아님

다음 원칙을 유지하세요.

```text
Client Trace
→ candidate / request hint

Server Historical Sweep
+
Server Historical Target
→ final authority
```

클라이언트가 보낸 TraceStart / TraceEnd를 최종 공격 geometry로 신뢰하지 마세요.

기존 거리 / arc / rate limit / PredictionKey validation도 유지하십시오.

---

# 6. P1 — Unified Animation Quality Tier 구축

현재 Leader는 ABA 및 Significance Manager를 사용하지만,

Follower Retarget, Hand IK, 향후 Foot IK, Cloth 등이 동일한 quality decision을 공유하는 구조는 아직 완성되지 않았습니다.

대규모 MMORPG에서는 이 부분이 중요합니다.

---

# 6.1 목표 구조

캐릭터 단위의 단일 animation quality state를 만드세요.

예:

```cpp
enum class EProject_JCharacterAnimationQualityTier : uint8
{
    Full,
    Reduced,
    Cheap,
    Proxy
};
```

또는 기존 Significance tier와 통합해도 됩니다.

핵심은 다음 모든 시스템이 같은 결정값을 소비하는 것입니다.

```text
CharacterAnimationQualityTier
├─ Leader ABA
├─ Follower update cadence
├─ Runtime Retarget
├─ Retarget IK
├─ Hand IK
├─ Foot IK
├─ Cloth / Hair
└─ Weapon Presentation
```

---

# 6.2 예시 정책

기존 프로젝트 거리 값과 Significance 정책을 먼저 확인한 뒤 실제 값은 조정하세요.

예:

```text
Tier 0 — Full
근거리 / Local / Combat Critical

Leader
60 Hz

Follower
Full

Runtime Retarget
Full

Retarget IK
On

Hand IK
On

Foot IK
On

Cloth
On
```

```text
Tier 1 — Reduced

Leader
ABA reduced cadence

Follower
Leader cadence에 맞춰 update

Runtime Retarget
On

Retarget IK
On 또는 reduced

Hand IK
Off 또는 중요 캐릭터만

Foot IK
Reduced

Cloth
Reduced
```

```text
Tier 2 — Cheap

Leader
Low Hz

Follower
Low Hz

Runtime Retarget
FK 위주

Retarget IK
Off

Hand IK
Off

Foot IK
Off

Cloth
Off
```

```text
Tier 3 — Proxy

Runtime Retarget
Off

대체:
cached pose
cheap shared ABP
VAT
impostor
crowd proxy
```

---

# 6.3 Leader/Follower cadence coupling

다음 상태를 피하세요.

```text
Leader = 10 Hz
Follower = 60 Hz
```

이 경우 Follower가 오래된 Leader pose를 반복적으로 retarget할 수 있습니다.

Follower update policy는 Leader animation update policy와 연동되어야 합니다.

가능하면:

```text
Leader update occurred
→ Follower update request

Leader skipped
→ Follower도 동일 cadence 또는 interpolation policy
```

구조를 만드세요.

단, Unreal Engine 내부 dependency/tick ordering을 우회해서 위험한 manual tick을 만들지 마세요.

기존 엔진 animation scheduling과 ABA API를 우선 활용하세요.

---

# 6.4 Retarget Pose From Mesh LOD 활용

`Retarget Pose From Mesh`가 제공하는 LOD 관련 기능을 확인하고 적극 사용하세요.

특히:

```text
LODThreshold
LODThresholdForIK
```

가 현재 UE 5.8에서 사용 가능한지 실제 엔진 헤더로 확인하세요.

가능하다면 다음 단계로 사용하세요.

```text
Near
Full Retarget + IK

Mid
Retarget + IK Off

Far
Retarget node bypass
```

곧바로 VAT로 전환하기보다 중간 단계를 두는 것을 권장합니다.

---

# 7. ABA 정책 재검토

현재:

```cpp
SetShouldUseActorRenderedFlag(true);
bBudgetTickWhenNotRendered = true;
```

가 적용되어 있습니다.

이는 Hidden Leader freeze 문제 해결에는 안전하지만,

화면 밖 모든 remote character Leader가 계속 tick 대상이 되는 비용이 커질 가능성이 있습니다.

실제 엔진 동작과 Project J workload를 확인해 다음 두 정책을 비교 프로파일링하세요.

## Policy A

```cpp
SetShouldUseActorRenderedFlag(true);
bBudgetTickWhenNotRendered = true;
```

## Policy B

```cpp
SetShouldUseActorRenderedFlag(true);
bBudgetTickWhenNotRendered = false;
```

그리고 Gameplay Critical 상태에서만:

```text
Attack
Root Motion
Gameplay Notify
Dodge
Hit Reaction
```

`RequestAnimationUpdate(GameplayPose)`로 ABA/URO 제한을 해제하세요.

결과는 Unreal Insights에서 비교하십시오.

측정 항목:

```text
Animation GT
Parallel Animation Eval
Pose Search
Retarget
Skeletal Mesh Tick
ABA managed count
offscreen character count
combat critical count
```

---

# 8. Dedicated Server 검증

현재 DS visual isolation이 추가되어 있지만 다음 항목을 실제로 점검하세요.

```text
Follower component 자체는 존재하는가?
Follower tick은 확실히 disabled인가?
AnimGraph evaluation이 실행되지 않는가?
Weapon Presentation Actor가 spawn되지 않는가?
Weapon Motion client-only code가 server에서 실행되지 않는가?
Gameplay hit trace는 visual actor 없이 정상 동작하는가?
```

가능하면 trace scope 또는 stat counter로 검증하세요.

문서의 표현도:

```text
"Zero CPU"
```

처럼 절대적인 표현보다:

```text
"Dedicated Server에서 Follower animation evaluation 및 visual weapon presentation runtime 비용 제거"
```

처럼 정확하게 유지하세요.

---

# 9. 문서 동기화

수정 완료 후 반드시 다음 문서를 실제 코드와 맞게 업데이트하세요.

```text
Docs/Architecture/Animation/Runtime_Retarget_HandIK_Architecture.md
```

문서에서는 다음 과장 표현을 피하세요.

```text
100% 해결
0% desync
Zero CPU
완벽히 차단
```

대신 실제 보장 범위를 명시하세요.

예:

```text
Per-frame IK/bone transform replication은 발생하지 않는다.

동일한 presentation state가 확보된 이후,
손-무기 정합은 각 클라이언트 로컬 IK 평가로 유지한다.

Dedicated Server에서는 visual follower animation evaluation과
weapon presentation actor runtime을 비활성화한다.
```

---

# 10. 테스트 요구사항

코드 수정 후 단순 컴파일만 하고 끝내지 마세요.

최소한 다음 테스트 시나리오를 점검하세요.

## Hand IK

```text
1. 납도 Idle
2. 납도 Walk/Run
3. Draw montage
4. Combat Idle
5. Combat Walk/Run
6. 1H Weapon
7. 2H Weapon
8. Independent Weapon Motion
9. Motion 시작/종료
10. Weapon 교체
11. Unequip
12. Remote Player
```

확인 항목:

```text
손이 이전 weapon socket에 남지 않는가?
Left hand stale target이 남지 않는가?
Alpha가 artist-authored 값대로 들어가는가?
Draw/Sheathe transition에서 popping이 없는가?
```

---

## SSR

다음 네트워크 조건에서 테스트하세요.

```text
Ping 0 ms
Ping 50 ms
Ping 100 ms
Ping 150~200 ms
Packet loss
Jitter
```

확인:

```text
history range 외 request reject
PredictionKey mismatch reject
AttackNode mismatch reject
HitWindow historical validation
Target rewind와 attacker sweep timestamp 일치
duplicate hit 차단
```

---

## ABA / Crowd

최소:

```text
1
20
50
100
200
```

remote character를 기준으로 프로파일링 가능한 테스트를 준비하세요.

측정:

```text
Game Thread
Animation Thread
Pose Search
Runtime Retarget
Hand IK
Follower Mesh
ABA
Frame Time
```

---

# 11. 성능 원칙

다음 원칙을 지키세요.

```text
Gameplay authority ≠ Visual pose

Server
canonical gameplay geometry

Client
visual retarget / IK
```

그리고:

```text
중요하지 않은 캐릭터일수록
"더 싸게 계산"
해야지
"게임플레이 state 자체를 다르게 계산"
하면 안 됩니다.
```

---

# 12. 구현 시 금지사항

다음 방식은 사용하지 마세요.

```text
Follower bone transform replication

Per-frame hand target RPC

Server-side full visual Follower Retarget

Client visual weapon actor를 server hit authority로 사용

AnimInstance worker thread에서 UObject 직접 접근

매 프레임 GetComponents 전체 탐색을 production 기본 경로로 사용

모든 remote character를 영구 Always Full Tick

단순히 컴파일 성공만으로 완료 처리
```

---

# 13. 코드 품질

변경은 가능한 한 기존 Project J 구조를 유지하면서 수행하세요.

선호:

```text
Data-driven
Event-driven
Weak pointer
Explicit lifecycle
Server authority
Client cosmetic
Thread-safe snapshot
Fixed-size/ring history
Central quality policy
```

새로운 시스템을 추가할 때 기존 subsystem/component와 책임이 중복되지 않도록 하세요.

---

# 14. 최종 산출물

작업 완료 후 다음 형식으로 보고하세요.

## A. Source Audit

```text
현재 실제 문제
왜 발생하는지
어떤 경로에서 재현되는지
```

## B. 변경 파일

예:

```text
File
Function/Class
변경 이유
```

## C. Hand IK 결과

```text
Stable Grip Target
Authored Alpha
Left/Right Hand
Draw/Sheathe
Independent Motion
Event Wiring
```

## D. SSR 결과

```text
Sweep history
Timestamp bounds
Interpolation
PredictionKey
Historical Hit Window
Target rewind
```

## E. Animation Quality Tier

```text
Tier 정의
Leader policy
Follower policy
IK policy
Retarget policy
```

## F. Build

반드시 실제 UE 5.8 빌드 결과를 첨부하세요.

예:

```text
Project_JEditor Win64 Development
Result: Succeeded / Failed
```

## G. 테스트 결과

```text
Standalone
PIE Listen Server
Dedicated Server
2+ clients
Artificial latency
Crowd
```

## H. Remaining Risks

완료되지 않은 문제는 숨기지 말고 명확하게 남기세요.

---

# 15. 최종 목표 아키텍처

```text
                    SERVER / GAMEPLAY
                           │
                  AttackDefinition
                           │
             Historical Attack State
            ┌──────────────┼──────────────┐
            │              │              │
        Attack Node    Hit Window    Weapon Sweep
            │              │              │
            └──────────────┼──────────────┘
                           │
                  ClientTimestamp
                           │
          Historical Target Capsule
                           │
                    Hit Validation
                           │
                    Damage Authority


====================================================


                    CLIENT / VISUAL
                           │
                      Leader Mesh
                           │
                Motion Matching / Montage
                           │
                 Retarget Pose From Mesh
                           │
                    Follower Mesh
                           │
          ┌────────────────┴────────────────┐
          │                                 │
      Foot Correction              Weapon Presentation
                                             │
                                  Stable Grip Targets
                                             │
                                  Post-Retarget Hand IK
                                             │
                                        Final Render
```

그리고 전체 품질 정책:

```text
Significance / Character Quality Tier
                  │
       ┌──────────┼──────────┐
       │          │          │
     Leader    Follower     IK
       │          │          │
      ABA      Retarget   Hand/Foot
       │          │          │
       └──────────┼──────────┘
                  │
          Unified Budget Policy
```

이 구조를 기준으로 수정하십시오.

**기존 코드에서 이미 해결된 부분은 불필요하게 다시 작성하지 말고, 실제로 남아 있는 결함만 최소 변경으로 고치되, 확장성·멀티스레딩·네트워크 권위·MMORPG 대규모 군집 성능까지 고려해 구현하십시오.**
