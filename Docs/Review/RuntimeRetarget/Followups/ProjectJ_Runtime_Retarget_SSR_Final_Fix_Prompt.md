# Project J Runtime Retarget / SSR / Animation Budget 최종 보완 프롬프트

## 역할

당신은 Unreal Engine 5.8 기반 대규모 액션 MMORPG의 **Senior Animation / Gameplay / Networking / Performance Engineer**입니다.

이번 작업은 기존 아키텍처를 다시 설계하는 것이 아니라, 현재 `Character_Test` 브랜치에 반영된 후속 개선 커밋 이후에도 남아 있는 **실제 correctness 문제, LOD wiring 누락, 테스트 신뢰성 문제, per-frame 비용 문제를 마무리하는 작업**입니다.

---

# 0. 저장소 / 기준

- Repository:
  - https://github.com/dpqksr5501/Project_J
- Branch:
  - `Character_Test`
- 현재 기준 HEAD:
  - `1d53702ec2351aef836d48c7b665b114c68a74fa`
- Unreal Engine:
  - `5.8`

현재 커밋에서 다음은 이미 구현되어 있습니다.

```text
Hand IK stable grip target
Authored Primary / Secondary IK Alpha
Per-frame stale snapshot reset
Event-driven UpdateWeaponTarget wiring
SSR fixed-size sweep ring buffer
SSR timestamp bound check
SSR sweep interpolation
PredictionKey / AttackNode matching
Historical HitWindow field
Unified Animation Quality Policy 구조
ABA Policy A/B CVar
Dedicated Server visual follower / weapon presentation 차단
```

따라서 위 항목을 불필요하게 다시 작성하지 마세요.

반드시 현재 브랜치의 실제 코드를 다시 읽은 뒤 작업하십시오.

---

# 1. 이번 작업의 우선순위

```text
P0
SSR HitWindow transition correctness

P0
Follower Retarget / IK 실제 Quality Tier wiring

P1
SSR history capacity / record cadence 정합성

P1
ABA Policy A/B CVar 실시간 반영

P1
Hand IK socket lookup 최적화

P1
자동화 테스트 신뢰성 보강

P2
문서의 보장 범위 정리
```

---

# 2. P0 — SSR HitWindow 전환 경계 기록

## 현재 문제

현재 `FProject_JAuthoritativeSweepRecord`에는:

```cpp
float ServerTimestamp;
FVector TraceStart;
FVector TraceEnd;
FGameplayTag AttackNodeTag;
int32 PredictionKey;
bool bHitWindowOpen;
```

가 기록됩니다.

그리고 `RecordAuthoritativeTrace()`에서 현재 `bHitWindowOpen` 값을 history에 저장합니다.

하지만 `SetHitWindowOpen(false)`는 현재 상태값만 바꾸고,

```text
HitWindow Close Timestamp
+
마지막 Authoritative Trace
+
bHitWindowOpen = false
```

상태를 history에 별도로 기록하지 않을 가능성이 있습니다.

이 경우:

```text
10.000 Open
10.016 Open
10.032 Open
10.040 HitWindow Close
```

상황에서도 history의 마지막 record가:

```text
10.032 Open
```

으로 남을 수 있습니다.

그리고 SSR lookup이 FutureTolerance 안에서 마지막 record를 사용하면 실제로는 닫힌 시점의 요청을 `Open` 상태로 오판할 가능성이 있습니다.

---

## 요구사항

`SetHitWindowOpen()`에서 상태가 변경될 때 **transition boundary record**를 남기십시오.

권장 형태:

```cpp
void UProject_JCombatHitValidationComponent::SetHitWindowOpen(bool bOpen)
{
    const bool bNewState = bOpen && ActiveAttackNodeTag.IsValid();

    if (bHitWindowOpen == bNewState)
    {
        return;
    }

    bHitWindowOpen = bNewState;

    if (GetOwner() &&
        GetOwner()->HasAuthority() &&
        bHasAuthoritativeTrace &&
        ActiveAttackNodeTag.IsValid())
    {
        AppendCurrentAuthoritativeSweepState();
    }
}
```

`AppendCurrentAuthoritativeSweepState()` 같은 helper를 만들어도 좋습니다.

중요한 것은:

```text
Open 시작 시점
Open 동안 Sweep
Close 시점
```

이 모두 history의 시간축에 남는 것입니다.

---

# 3. P0 — Historical HitWindow interpolation 정책 정리

## 현재 문제

두 bounding record 모두 `bHitWindowOpen=true`일 때만:

```cpp
OutHitWindowOpen =
    RecordA.bHitWindowOpen &&
    RecordB.bHitWindowOpen;
```

로 처리하는 구조는 보수적이지만,

transition boundary를 명확히 기록하지 않으면 의미가 불완전합니다.

---

## 요구사항

다음 규칙을 명확하게 만드세요.

```text
RecordA Open
RecordB Open
→ 구간 전체 Open

RecordA Open
RecordB Closed
→ RecordB timestamp 이전까지만 Open

RecordA Closed
RecordB Closed
→ Closed

RecordA Closed
RecordB Open
→ RecordB timestamp 이후 Open
```

단순 bool AND만으로 처리하지 말고,

**HitWindow state transition timestamp를 정확히 사용**하도록 구현하십시오.

가능한 방식:

```text
state sample
+
transition boundary sample
```

을 이용해서 target timestamp가 어느 상태 구간에 속하는지 판정.

---

# 4. P0 — SSR이 현재 Active Attack에 종속되는 범위 정리

## 현재 문제

현재 SSR 요청이:

```cpp
Request.PredictionKey == ActivePredictionKey
Request.AttackNodeTag == ActiveAttackNodeTag
```

를 요구합니다.

그리고 `EndAttack()`에서:

```cpp
ActivePredictionKey = 0;
ActiveAttackNodeTag = FGameplayTag();
ActiveAttackDefinition = nullptr;
```

로 정리합니다.

따라서 과거 시점의 유효한 요청이라도,

```text
Client hit
→ network delay
→ server combo node changed
→ SSR request arrives
```

혹은

```text
Client hit
→ attack ended
→ request arrives
```

상황에서는 history가 존재해도 현재 ActiveAttack 검증 단계에서 거절될 수 있습니다.

---

## 이번 작업에서 선택

다음 두 방향 중 하나를 선택하고 명확하게 구현하십시오.

### Option A — 현재 공격 context 내부 SSR 유지

이 경우 현재 구조를 유지하되 문서와 코드 주석에서 다음처럼 명시:

```text
SSR history는 현재 활성 공격 activation 내부의
weapon sweep / target rewind 정합성을 보장한다.

공격 activation이 이미 종료되었거나
다음 combo node로 넘어간 요청은 의도적으로 reject한다.
```

이 방식은 단순하고 안전합니다.

### Option B — Historical Attack Activation까지 확장

이 경우 별도 attack history를 구성:

```cpp
struct FProject_JHistoricalAttackState
{
    float StartTime;
    float EndTime;

    int32 PredictionKey;
    FGameplayTag AttackNodeTag;

    TObjectPtr<const UProject_JAttackDefinition> AttackDefinition;

    uint64 WeaponRevision;
};
```

또는 immutable lightweight snapshot 구조를 사용.

그 후:

```text
ClientTimestamp
→ Historical Attack Activation resolve
→ Historical PredictionKey / Node validate
→ Historical HitSpec
→ Historical Sweep
→ Historical Target
```

로 검증.

---

## 권장

이번 단계에서는 **Option A**를 기본으로 추천합니다.

학생 / 프로토타입 규모에서 SSR 전체 attack lifecycle history까지 확장하면 복잡도가 크게 증가합니다.

단, 코드와 문서에서 “완전한 historical attack replay”처럼 표현하면 안 됩니다.

---

# 5. P1 — Sweep History Capacity와 실제 Record Rate 정합성

## 현재 문제

현재:

```cpp
MaxSweepHistoryCapacity = 64;
MaxSweepHistorySeconds = 1.5f;
```

입니다.

그러나 64개 record는 항상 1.5초를 보장하지 않습니다.

예:

```text
60 Hz → 약 1.07초
120 Hz → 약 0.53초
```

입니다.

---

## 요구사항

둘 중 하나를 선택하세요.

### 방법 A — Record Rate 제한

Weapon sweep record를 일정 rate로 제한:

```cpp
SweepRecordRateHz = 30.0f;
```

또는 60Hz.

그러면:

```cpp
RequiredCapacity =
    CeilToInt(MaxSweepHistorySeconds * SweepRecordRateHz) + SafetyMargin;
```

로 계산.

### 방법 B — Capacity 증가

현재 animation / notify update 빈도를 고려해 worst-case capacity를 확보.

예:

```cpp
MaxSweepHistoryCapacity = 128;
```

그러나 단순히 숫자만 키우지 말고 실제 최대 record rate 근거를 남기세요.

---

## 권장

가능하면 `UProject_JServerSideRewindComponent`처럼:

```text
MaxRecordTime
RecordRateHz
Capacity
```

관계를 명시적으로 관리하세요.

---

# 6. P0 — Unified Quality Tier를 실제 Follower Retarget에 연결

## 현재 문제

현재 `FProject_JAnimOptimizationPolicy`에는:

```cpp
bool bEnableHandIK;
bool bEnableFootIK;
bool bEnableRetargetIK;
bool bEnableFollowerRetarget;
int32 RetargetIKLODThreshold;
```

가 존재합니다.

`UProject_JRetargetAnimInstance`도 Leader policy에서 일부 값을 읽습니다.

하지만 다음 값들이 실제 비용 절감까지 연결되어 있는지 확인해야 합니다.

```text
bEnableFollowerRetarget
bEnableRetargetIK
RetargetIKLODThreshold
bEnableFootIK
```

현재 C++ 변수만 존재하고 AnimGraph 또는 follower component evaluation에 연결되지 않았다면 **정책만 있고 비용 절감은 없는 상태**입니다.

---

## 요구사항

Follower AnimGraph 또는 런타임 제어에 실제 연결하세요.

최종 목표:

```text
Local / Near
Retarget Pose From Mesh = Full
Retarget IK = On
Hand IK = On
Foot IK = On

Mid
Retarget Pose From Mesh = On
Retarget IK = Off
Hand IK = Off
Foot IK = Reduced / optional

Far
Retarget Pose From Mesh = FK only 또는 reduced
Retarget IK = Off
Hand IK = Off
Foot IK = Off

Hidden
Follower animation evaluation = Off 또는 매우 저빈도
```

---

# 7. Retarget Pose From Mesh 실제 LOD 연결

UE 5.8의 `Retarget Pose From Mesh` 관련 실제 엔진 API를 확인하세요.

가능하다면 다음을 활용:

```text
LODThreshold
LODThresholdForIK
```

정확한 property 이름과 동작은 UE 5.8 엔진 헤더 기준으로 확인하십시오.

---

## 요구사항

`RetargetIKLODThreshold`를 단순 BlueprintReadOnly 변수로 남기지 말고 실제 AnimGraph 노드 입력 / property에 연결하십시오.

예:

```text
Follower AnimBP
Retarget Pose From Mesh
    ├─ LODThreshold
    └─ LODThresholdForIK
```

또는 Blueprint가 노드 property를 runtime variable로 받을 수 없다면,

```text
Blend by Bool
├─ Full Retarget
├─ FK-only Retarget
└─ Cheap Pose
```

구조를 사용해도 됩니다.

중요한 것은 **실제로 평가 비용이 줄어드는 것**입니다.

---

# 8. Hidden Tier에서 Follower Retarget 실제 비활성화

## 현재 문제

`Policy.bEnableFollowerRetarget = false`만 설정하고 실제 component / AnimGraph evaluation을 그대로 두면 비용은 계속 발생합니다.

---

## 요구사항

다음 중 안전한 방법을 선택하세요.

### 방법 A — AnimGraph branch

```text
bEnableFollowerRetarget
    true  → Retarget Pose From Mesh
    false → Cached / Ref / cheap pose
```

### 방법 B — Follower tick policy

Hidden 상태에서:

```cpp
FollowerMesh->SetComponentTickEnabled(false);
```

단, 다시 visible이 될 때 정확히 복구되어야 합니다.

### 방법 C — VisibilityBasedAnimTickOption / URO / LOD

엔진 기본 정책을 이용.

---

## 주의

다음은 피하세요.

```text
매 프레임 SetComponentTickEnabled 토글
manual animation tick
custom ticking that breaks engine animation scheduling
```

상태 변화 시에만 정책을 갱신하세요.

---

# 9. Foot IK policy 실제 연결

현재:

```cpp
Policy.bEnableFootIK
```

가 실제 Foot IK / Foot Placement 실행 여부와 연결되어 있는지 확인하십시오.

연결되어 있지 않다면 다음 구조를 사용하세요.

```text
Local / Near
Foot Placement = On

Mid
Foot Placement = Reduced / optional
또는 trace frequency 감소

Far / Hidden
Foot Placement = Off
```

World trace 기반 Foot Placement를 수백 명에게 Full Rate로 돌리지 마세요.

---

# 10. P1 — ABA Policy A / B CVar를 진짜 Runtime Toggle로 변경

## 현재 문제

현재:

```cpp
bBudgetTickWhenNotRendered =
    CVar.GetValueOnGameThread() != 0;
```

를 `BeginPlay()`에서만 읽는다면,

콘솔에서:

```text
Project_J.Anim.BudgetTickWhenNotRendered 0
```

으로 변경해도 이미 스폰된 캐릭터는 값이 갱신되지 않습니다.

---

## 요구사항

다음 중 하나를 구현하세요.

### Option A — Budget Subsystem Refresh에서 반영

`UProject_JCharacterAnimationBudgetSubsystem::Refresh()`에서:

```cpp
const bool bTickOffscreen =
    CVar.GetValueOnGameThread() != 0;
```

을 읽어 각 mesh policy에 반영.

### Option B — CVar delegate

CVar 변경 시 등록된 ProjectJ budget mesh들을 갱신.

---

## 목표

실제 플레이 중:

```text
Policy A
Project_J.Anim.BudgetTickWhenNotRendered 1

Policy B
Project_J.Anim.BudgetTickWhenNotRendered 0
```

를 바꾸고 Unreal Insights에서 즉시 비교 가능해야 합니다.

---

# 11. P1 — Hand IK socket component cache

## 현재 문제

현재 `FindWeaponSocketTransform()`가 매 호출마다:

```cpp
SpawnedWeapon->GetComponents(SceneComponents);
```

로 모든 scene component를 순회할 수 있습니다.

Primary / Secondary Grip 각각 호출되므로 캐릭터 수가 많아지면 불필요한 per-frame component scan 비용이 누적됩니다.

---

## 요구사항

무기 lifecycle 이벤트에서 socket owner component를 캐싱하세요.

예:

```cpp
TWeakObjectPtr<USceneComponent> CachedPrimaryGripComponent;
TWeakObjectPtr<USceneComponent> CachedSecondaryGripComponent;

FName CachedPrimaryGripSocket;
FName CachedSecondaryGripSocket;
```

갱신 시점:

```text
Weapon spawn
Weapon destroy
Presentation profile change
Weapon actor replacement
Socket configuration change
```

---

## 런타임

매 frame에는:

```cpp
if (CachedPrimaryGripComponent.IsValid())
{
    Transform =
        CachedPrimaryGripComponent->GetSocketTransform(...);
}
```

만 수행.

---

## fallback

캐시가 invalid한 경우에만 component scan.

그리고 성공하면 다시 캐싱.

---

# 12. P1 — StableGrip 자동화 테스트를 실제 기능 테스트로 변경

## 현재 문제

현재:

```text
ProjectJ.Presentation.StableGripTargetsAndAuthoredAlpha
```

테스트 이름과 달리 실제로는 quality tier 기본값 정도만 확인하고 있을 가능성이 있습니다.

---

## 요구사항

실제 테스트를 구성하세요.

최소 테스트 항목:

```text
Weapon actor spawn
PrimaryGrip socket 존재
SecondaryGrip socket 존재

Drawn State
DefaultDrawnPrimaryIKAlpha
DefaultDrawnSecondaryIKAlpha

Sheathed State
DefaultSheathedPrimaryIKAlpha
DefaultSheathedSecondaryIKAlpha

Independent Motion
ActivePrimaryGripIKAlpha
ActiveSecondaryGripIKAlpha

Independent Motion End
DefaultDrawn Alpha 복귀

Weapon Destroy / Unequip
GripTargets invalid
Alpha = 0

Primary valid / Secondary invalid
stale Secondary target 없음
```

---

## 테스트 결과

다음 값을 직접 `TestEqual / TestTrue / TestFalse` 하세요.

```text
GripTargets.bHasPrimaryGrip
GripTargets.bHasSecondaryGrip

PrimaryIKAlpha
SecondaryIKAlpha

PrimaryGripWorldTransform
SecondaryGripWorldTransform
```

---

# 13. SSR 자동화 테스트 보강

현재 테스트에 다음 케이스를 추가하세요.

```text
HitWindow Open → Close transition boundary
Close 직후 timestamp reject / false
PredictionKey가 다른 두 record 사이 interpolation reject
AttackNode가 다른 두 record 사이 interpolation reject
History oldest boundary
History newest boundary
Past tolerance boundary
Future tolerance boundary
Ring buffer wrap-around
Capacity overflow 후 chronological ordering 유지
```

---

# 14. Quality Tier 자동화 테스트 추가

최소한 다음 policy를 테스트하세요.

```text
Local
HandIK true
FootIK true
RetargetIK true
FollowerRetarget true

Near
HandIK true
FootIK true
RetargetIK true

Mid
HandIK false
RetargetIK false
FollowerRetarget true

Far
HandIK false
FootIK false
RetargetIK false

Hidden
HandIK false
FootIK false
RetargetIK false
FollowerRetarget false
```

그리고 실제 Follower AnimInstance에도 해당 값이 전달되는지 검증하세요.

가능하면 component tick / AnimGraph branch 상태도 확인하십시오.

---

# 15. Per-frame ownership 원칙

다음 원칙을 유지하세요.

```text
Game Thread
Actor / Component / UObject query
        ↓
Cached snapshot
        ↓
Worker Thread
pure math
```

`NativeThreadSafeUpdateAnimation()`에서는 UObject 접근을 추가하지 마세요.

---

# 16. Dedicated Server 원칙 유지

기존:

```text
Visual Weapon Presentation Actor
Follower Retarget Evaluation
Hand IK
Foot IK
```

은 DS에서 실행하지 않는 방향을 유지하세요.

다만 gameplay authority가 필요한:

```text
Leader pose
Gameplay notify
Attack sweep
Root motion
SSR
```

은 그대로 정상 동작해야 합니다.

---

# 17. 문서 수정

다음 문서를 실제 코드와 맞게 업데이트하세요.

```text
Docs/Architecture/Animation/Runtime_Retarget_HandIK_Architecture.md
```

---

## 표현 기준

다음 표현은 피하세요.

```text
100% 해결
Zero CPU
0% Desync
완전히 보장
```

대신 실제 보장 범위를 기술하세요.

예:

```text
현재 활성 공격 context 내부에서
attacker historical sweep과 target historical capsule을
동일 timestamp 기준으로 검증한다.

Follower visual animation은 Dedicated Server에서 평가하지 않는다.

Hand IK target transform은 network replication하지 않고
local presentation state에서 계산한다.
```

---

# 18. Build / Test

작업 완료 후 반드시 실제 UE 5.8 빌드를 수행하세요.

```text
Project_JEditor Win64 Development
```

결과:

```text
Result: Succeeded
```

를 확인.

---

# 19. Automation

다음 테스트들을 실제 실행:

```text
ProjectJ.Combat.SSRHistoricalSweepInterpolation
ProjectJ.Presentation.StableGripTargetsAndAuthoredAlpha
기존 WeaponAttackLifetime
기존 AuthorityHitWorldOrigin
기존 WeaponPresentationIdentity
새 QualityTier test
새 HitWindow transition test
```

---

# 20. 가능하면 추가 런타임 검증

자동화 테스트 외에 가능하면:

```text
Standalone
PIE Listen Server
Dedicated Server
2 clients
Artificial latency
```

검증을 수행.

---

# 21. Unreal Insights 프로파일링

가능하면 다음 workload:

```text
20 remote characters
50 remote characters
100 remote characters
200 remote characters
```

에서 비교.

---

## CVar 비교

```text
Project_J.Anim.BudgetTickWhenNotRendered 1
Project_J.Anim.BudgetTickWhenNotRendered 0
```

---

## 측정

```text
Game Thread
Animation Thread
Parallel Eval
Pose Search
Follower Retarget
Hand IK
Foot IK
Skeletal Mesh Tick
ABA Managed Count
Frame Time
```

---

# 22. 최종 보고 형식

작업 후 다음 형식으로 보고하세요.

## A. Source Audit

```text
남아 있던 실제 문제
원인
재현 경로
```

## B. 변경 파일

```text
File
Class / Function
변경 이유
```

## C. SSR 결과

```text
HitWindow transition record
Timestamp bounds
Sweep interpolation
PredictionKey
AttackNode
Ring buffer capacity / rate
Current-attack-only 여부
```

## D. Follower Quality Tier 결과

```text
Local
Near
Mid
Far
Hidden
```

각 tier에서 실제로:

```text
Follower evaluation
Retarget
Retarget IK
Hand IK
Foot IK
```

가 어떻게 동작하는지 명시.

## E. ABA

```text
Policy A / B
Runtime CVar 반영 여부
```

## F. Hand IK

```text
Socket component cache
Stable Grip Target
Authored Alpha
Stale Target
```

## G. Tests

```text
Test name
Pass / Fail
실제 검증 내용
```

## H. Build

```text
UE 5.8
Project_JEditor Win64 Development
Succeeded / Failed
```

## I. Remaining Risks

미완료 또는 에디터 수동 검증이 필요한 부분을 명확히 작성.

---

# 23. 최종 목표

최종 구조는 다음을 만족해야 합니다.

```text
Gameplay Authority
        │
        ├─ Leader Animation
        ├─ Root Motion
        ├─ Attack State
        ├─ Historical Weapon Sweep
        └─ SSR

Visual Presentation
        │
        ├─ Follower Runtime Retarget
        ├─ Retarget IK
        ├─ Hand IK
        ├─ Foot IK
        └─ Weapon Presentation
```

그리고:

```text
Significance / Quality Tier
        ↓
Leader ABA
Follower Retarget
Retarget IK
Hand IK
Foot IK
Weapon Presentation
```

모두 하나의 정책 체계 아래 실제 실행 비용까지 제어되어야 합니다.

---

# 24. 중요 원칙

이번 작업에서 가장 중요한 것은 다음입니다.

```text
정책 변수만 추가하고 끝내지 말 것.

실제 AnimGraph / Component evaluation 비용까지 줄어드는지 확인할 것.

테스트 이름과 테스트 내용이 일치해야 함.

SSR은 같은 시간축의 상태만 비교해야 함.

현재 공격 context만 지원한다면 그 제한을 숨기지 말 것.

문서보다 실제 소스 코드를 기준으로 판단할 것.
```

기존 구현 중 정상 동작하는 부분은 유지하고, 위에 명시된 남은 결함만 최소 변경으로 정리하십시오.
