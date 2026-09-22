# Project J Runtime Retarget 최종 남은 수정 프롬프트

## 역할

당신은 Unreal Engine 5.8 기반 대규모 액션 MMORPG의 Senior Animation / Gameplay / Performance Engineer입니다.

이번 작업은 `Character_Test` 브랜치의 현재 HEAD를 기준으로, 이전 리뷰와 수정 작업 이후에도 남아 있는 **마지막 실제 correctness / integration 문제만 수정**하는 작업입니다.

기존에 정상 동작하는 SSR, Hand IK, ABA, Dedicated Server 격리 구조는 불필요하게 다시 작성하지 마십시오.

---

# 0. 저장소 / 기준

- Repository:
  - https://github.com/dpqksr5501/Project_J
- Branch:
  - `Character_Test`
- 현재 기준 HEAD:
  - `a364e69d2820ec468248fe7acde7e79717463b8f`
- Unreal Engine:
  - `5.8`

현재 커밋에서 이미 구현된 것:

```text
SSR HitWindow transition boundary
SSR 128-slot ring buffer
SSR interpolation / timestamp bounds
Active Attack Context 범위 문서화
Hand IK stable grip target
Grip socket component cache
Authored Primary / Secondary Alpha
ABA Policy A/B runtime CVar
Follower Quality Tier flags
Hidden tier follower tick control 시도
StableGrip / SSR / QualityTier 테스트 보강
```

---

# 1. 현재 최우선 문제 — Hidden 판단이 Leader Mesh 렌더 상태만 사용

## 현재 코드

`UProject_JCharacterAnimInstanceBase::WasOwnerRecentlyRendered()`:

```cpp
bool UProject_JCharacterAnimInstanceBase::WasOwnerRecentlyRendered(float RecentlyRenderedTolerance) const
{
    if (!OwningCharacter)
    {
        return false;
    }

    const USkeletalMeshComponent* MeshComponent = OwningCharacter->GetMesh();
    return !MeshComponent || MeshComponent->WasRecentlyRendered(RecentlyRenderedTolerance);
}
```

그리고 `BuildOptimizationPolicy()`:

```cpp
const bool bRecentlyRendered =
    WasOwnerRecentlyRendered(RecentlyRenderedTolerance);

if (!bRecentlyRendered)
{
    Policy.Tier = EProject_JAnimBudgetTier::Hidden;
    Policy.bEnableFollowerRetarget = false;
    ...
}
```

## 문제

Project J의 구조는:

```text
Leader Mesh
- gameplay / Motion Matching source
- hidden visual source

Follower Mesh
- 실제 화면에 보이는 캐릭터
- Runtime Retarget
```

입니다.

따라서 Leader Mesh가 HiddenInGame / invisible source mesh라면:

```text
Follower는 화면에 보임
Leader는 렌더되지 않음
        ↓
LeaderMesh->WasRecentlyRendered() == false
        ↓
Hidden Tier
        ↓
Follower tick OFF
```

가 될 가능성이 있습니다.

이것은 Unified Quality Tier의 핵심 correctness 문제입니다.

---

# 2. P0 — Actor / Follower 기준 Render Visibility로 수정

`WasOwnerRecentlyRendered()`가 Leader 하나만 보지 않도록 수정하십시오.

## 권장 구조

우선순위:

```text
1. 실제 Visual Follower Mesh가 최근 렌더되었는가?
2. 그 외 캐릭터의 렌더 가능한 skeletal visual component가 최근 렌더되었는가?
3. Leader 자체가 visible renderer라면 Leader도 포함
```

가능한 helper:

```cpp
bool UProject_JCharacterAnimInstanceBase::WasOwnerVisualRecentlyRendered(
    float Tolerance) const;
```

예시 개념:

```cpp
if (!OwningCharacter)
{
    return false;
}

TInlineComponentArray<USkeletalMeshComponent*> Meshes;
OwningCharacter->GetComponents(Meshes);

for (const USkeletalMeshComponent* Mesh : Meshes)
{
    if (!Mesh || !Mesh->IsRegistered())
    {
        continue;
    }

    if (IsGameplayOnlyHiddenLeader(Mesh))
    {
        continue;
    }

    if (Mesh->WasRecentlyRendered(Tolerance))
    {
        return true;
    }
}

return false;
```

단, 매 프레임 전체 component scan을 새로 만드는 것은 피하십시오.

가능하면 Visual Follower reference를 캐싱하십시오.

---

# 3. Visual Follower를 명시적으로 식별

현재 구조에서 “Leader의 모든 child skeletal mesh”와 “Runtime Retarget Follower”를 동일하게 취급하면 안 됩니다.

다음 중 하나를 사용하십시오.

## 권장 A — 전용 Component Type

예:

```cpp
UProject_JRetargetVisualMeshComponent
```

또는 기존 Follower 전용 subclass가 있다면 그것을 활용.

## 권장 B — 명시적 reference

Player Character / Appearance Component 등에:

```cpp
TWeakObjectPtr<USkeletalMeshComponent> RuntimeRetargetFollowerMesh;
```

를 보관.

## 권장 C — AnimInstance type 기반

```cpp
Cast<UProject_JRetargetAnimInstance>(
    SkeletalMesh->GetAnimInstance())
```

를 이용해 Runtime Retarget Follower만 식별.

단, 이 방법도 매 프레임 탐색 대신 초기화/변경 시 cache하십시오.

---

# 4. P0 — ApplyOptimizationPolicy가 모든 Child SkeletalMesh를 끄는 문제

현재:

```cpp
for (USceneComponent* Child : LeaderMesh->GetAttachChildren())
{
    if (USkeletalMeshComponent* FollowerMesh =
        Cast<USkeletalMeshComponent>(Child))
    {
        FollowerMesh->SetComponentTickEnabled(
            NewPolicy.bEnableFollowerRetarget);
    }
}
```

## 문제

Project J에는 이미:

```text
UProject_JModularMeshComponent
```

가 존재합니다.

장비 시스템에서:

```cpp
UProject_JModularMeshComponent* NewMeshComp =
    NewObject<UProject_JModularMeshComponent>(OwnerCharacter);

NewMeshComp->AttachAndSetLeader(MainMesh);
```

또는 socket attach 형태로 Leader Mesh에 붙습니다.

따라서 현재 코드는:

```text
Runtime Retarget Follower
Armor
Helmet
Skirt
Socket-attached skeletal equipment
기타 child skeletal mesh
```

를 전부 같은 Follower로 간주해 tick을 끌 수 있습니다.

---

# 5. 요구사항 — Runtime Retarget Follower만 제어

`ApplyOptimizationPolicy()`는 반드시 **실제 Runtime Retarget visual follower만** 조절해야 합니다.

예:

```cpp
if (USkeletalMeshComponent* Follower =
    CachedRuntimeRetargetFollower.Get())
{
    Follower->SetComponentTickEnabled(
        NewPolicy.bEnableFollowerRetarget);
}
```

또는 AnimInstance type으로 필터:

```cpp
if (Cast<UProject_JRetargetAnimInstance>(
        Mesh->GetAnimInstance()))
{
    ...
}
```

## 금지

```text
GetAttachChildren()의 모든 USkeletalMeshComponent 일괄 제어
```

---

# 6. Hidden → Visible 복구 경로 검증

Follower tick을 끄는 것보다 중요한 것은 다시 켜지는 것입니다.

다음 시나리오를 반드시 검증하십시오.

```text
Remote character 화면 밖
→ Hidden Tier
→ Follower tick OFF

카메라 이동
→ 캐릭터 다시 화면 안

Leader / actor visibility detection
→ Near / Mid / Far 복귀
→ Follower tick ON
→ Retarget 즉시 정상 복구
```

## 반드시 확인

```text
Follower tick이 꺼져 있어도
visibility 판단 시스템이 다시 visible 상태를 감지할 수 있는가?
```

Follower animation tick이 꺼져 있어도 rendering 자체는 마지막 pose로 계속 가능할 수 있으나,
프로젝트의 실제 visibility detection이 이에 의존하지 않는지 확인하십시오.

특히 Leader Mesh만 `WasRecentlyRendered()`로 확인하면 복구가 불가능한 구조가 될 수 있습니다.

---

# 7. P1 — RetargetLODThreshold가 실제로 연결되지 않은 문제

`UProject_JRetargetAnimInstance`에:

```cpp
int32 RetargetIKLODThreshold;
int32 RetargetLODThreshold;
```

가 노출되어 있습니다.

하지만 현재 C++ 코드에서는 Leader Policy로부터:

```cpp
RetargetIKLODThreshold =
    Policy.RetargetIKLODThreshold;
```

만 복사하고,

`RetargetLODThreshold`는 실제 값이 설정되지 않는지 확인하십시오.

또한 변수만 노출하고 AnimGraph의 `Retarget Pose From Mesh` 노드에 연결하지 않았다면 실제 비용 감소가 없습니다.

---

# 8. Retarget Pose From Mesh 실제 연결

UE 5.8 엔진 헤더에서 실제:

```text
FAnimNode_RetargetPoseFromMesh
LODThreshold
LODThresholdForIK
```

API를 확인하십시오.

## 목표

```text
Local / Near
Full Retarget
Retarget IK ON

Mid
Retarget ON
Retarget IK OFF

Far
Retarget ON 또는 reduced
Retarget IK OFF

Hidden
Follower tick OFF
```

---

# 9. 정책 변수만 만들고 끝내지 말 것

다음 변수:

```cpp
bEnableFollowerRetarget
bTierAllowsHandIK
bTierAllowsFootIK
bTierAllowsRetargetIK
RetargetIKLODThreshold
RetargetLODThreshold
```

가 실제 AnimGraph evaluation에 연결됐는지 확인하십시오.

필요하면 Blueprint AnimBP 쪽 수동 설정이 필요하다는 점을 명확히 보고하십시오.

`.uasset`을 C++ 작업만으로 자동 수정할 수 없다면:

```text
Required Editor Wiring
```

섹션을 별도로 작성하고 정확한 노드/핀 연결을 설명하십시오.

---

# 10. Foot IK도 동일하게 검증

현재:

```cpp
bTierAllowsFootIK
```

가 C++ property로 존재합니다.

하지만 실제 Foot Placement node에 연결되지 않았다면 비용은 줄지 않습니다.

다음 정책을 실제 AnimGraph에 적용:

```text
Local / Near
Foot Placement ON

Mid
Foot Placement ON 또는 Reduced

Far / Hidden
Foot Placement OFF
```

---

# 11. QualityTier 테스트 개선

현재 QualityTier 테스트는 대부분:

```cpp
FProject_JAnimOptimizationPolicy Policy;
Policy.bEnableHandIK = false;
TestFalse(...);
```

형식으로 구조체 값 자체를 테스트합니다.

이것은 실제 시스템 연결 테스트가 아닙니다.

---

# 12. 실제 Quality Tier Integration Test 추가

다음을 검증하십시오.

## A. Visibility

```text
Follower rendered
Leader hidden
→ Hidden Tier가 아니어야 함
```

## B. Hidden

```text
Follower not rendered
→ Hidden
→ Follower tick disabled
```

## C. Restore

```text
Hidden
→ visible
→ Follower tick re-enabled
```

## D. Modular Mesh isolation

```text
Runtime Retarget Follower
Modular Armor Mesh
```

두 개가 Leader child로 존재할 때:

```text
Hidden Tier 적용
→ Follower tick OFF
→ Modular Armor를 잘못 일괄 제어하지 않음
```

을 확인.

---

# 13. ApplyOptimizationPolicy 호출 주기 검증

현재:

```cpp
PublishChooserProperties()
{
    const FProject_JAnimOptimizationPolicy OptimizationPolicy =
        BuildOptimizationPolicy();

    ApplyOptimizationPolicy(OptimizationPolicy);
}
```

형태입니다.

Hidden tier에서:

```cpp
Policy.bUpdateAnimationData = false;
```

일 때에도 `PublishChooserProperties()`가 visibility 변화 감지를 위해 충분히 호출되는지 확인하십시오.

만약 Hidden optimization 때문에 이 함수 자체가 장시간 호출되지 않는다면:

```text
Hidden → Visible
```

복구가 늦거나 불가능할 수 있습니다.

필요하다면 Tier visibility decision은 animation data update보다 더 상위의 lightweight GT policy pass로 이동하십시오.

---

# 14. ABA와 Follower visibility 상호작용 검증

현재 Leader:

```cpp
SetShouldUseActorRenderedFlag(true);
```

를 사용합니다.

ABA가 Actor rendered flag를 볼 때 실제 Follower rendering이 actor rendered 상태에 반영되는지 UE 5.8 소스 기준으로 확인하십시오.

목표:

```text
Leader hidden
Follower visible
→ Leader는 source pose 업데이트 가능
```

이 구조가 실제로 성립해야 합니다.

---

# 15. SSR — 현재 구현은 유지, 단 테스트 하나 추가

SSR은 이번 커밋에서:

```text
HitWindow transition boundary
128 slot history
close boundary semantics
Active Attack Context Option A
```

가 잘 정리된 방향입니다.

큰 구조 변경은 하지 마십시오.

다만 실제 production path를 테스트하십시오.

현재 테스트가 직접:

```cpp
AppendSweepHistoryRecord(...)
```

를 호출하는 것뿐 아니라,

실제:

```text
BeginAttackNode
SetHitWindowOpen(true)
RecordAuthoritativeTrace
SetHitWindowOpen(false)
```

경로로 history가 생성되는 테스트를 하나 추가하십시오.

---

# 16. Socket Cache — 현재 구조 유지

`CachedPrimaryGripComponent`
`CachedSecondaryGripComponent`

구조는 방향이 좋습니다.

다만 다음 lifecycle에서 cache invalidation이 보장되는지 확인하십시오.

```text
Weapon Destroy
Weapon Respawn
Presentation Profile Change
Weapon Actor Replacement
Grip Socket Name Change
```

정상이라면 추가 수정하지 마십시오.

---

# 17. ABA Runtime CVar — 현재 구조 유지

현재 Budget Subsystem `Refresh()`에서:

```cpp
Mesh->bBudgetTickWhenNotRendered =
    Project_J::Anim::GetBudgetTickWhenNotRendered();

Allocator->SetComponentSignificance(
    ...,
    Mesh->bBudgetTickWhenNotRendered,
    ...
);
```

가 매 policy refresh마다 적용되는 구조는 적절합니다.

이 부분은 불필요하게 다시 변경하지 마십시오.

---

# 18. 문서 표현 수정

현재 문서의:

```text
Hidden tier에서 Follower tick 비활성화하여 완전한 0 CPU 달성
```

같은 표현은 수정하십시오.

Component가 존재하고 rendering / visibility / state transition 비용 등이 남을 수 있으므로:

```text
Hidden tier에서 Follower animation component tick 및
Runtime Retarget / IK evaluation을 비활성화하여
visual animation evaluation 비용을 제거한다.
```

정도로 쓰십시오.

---

# 19. Build / Test

완료 후 반드시:

```text
Project_JEditor Win64 Development
```

빌드.

그리고 자동화 테스트:

```text
ProjectJ.Combat.SSRHistoricalSweepInterpolation
ProjectJ.Presentation.StableGripTargetsAndAuthoredAlpha
ProjectJ.Animation.QualityTierPolicy
```

기존 관련 regression tests도 실행.

---

# 20. 새로 반드시 추가할 테스트

```text
ProjectJ.Animation.HiddenLeaderVisibleFollower
ProjectJ.Animation.HiddenToVisibleFollowerRestore
ProjectJ.Animation.ModularMeshIsolation
ProjectJ.Combat.SSRHitWindowProductionTransition
```

이름은 변경 가능하나 의미는 유지하십시오.

---

# 21. 최종 보고 형식

## A. Hidden Visibility Audit

```text
Leader hidden
Follower visible
Tier 판단 결과
```

## B. Follower Identification

```text
어떤 component만 Runtime Retarget Follower로 제어하는지
```

## C. Modular Mesh Isolation

```text
장비 mesh에 영향 없는지
```

## D. Hidden → Visible Restore

```text
Follower tick OFF → ON 복구 경로
```

## E. Retarget LOD

```text
LODThreshold
LODThresholdForIK
실제 AnimGraph 연결 여부
```

## F. Foot IK

```text
Policy variable
실제 AnimGraph 연결 여부
```

## G. SSR

```text
production SetHitWindowOpen transition test
```

## H. Tests

실제 integration을 검증한 테스트인지 설명.

## I. Build

```text
UE 5.8
Project_JEditor Win64 Development
Result
```

## J. Remaining Manual Editor Work

Blueprint / AnimGraph `.uasset` 수동 wiring이 필요한 경우 정확히 명시.

---

# 22. 최종 완료 조건

다음이 모두 만족되면 이번 Runtime Retarget 아키텍처 리뷰 작업은 완료로 판단합니다.

```text
1. Hidden Leader 때문에 Visible Follower가 Hidden Tier로 오판되지 않음

2. Runtime Retarget Follower만 tick 제어
   Modular / Equipment SkeletalMesh에 부작용 없음

3. Hidden → Visible 시 Follower가 정상 복구

4. Hand IK / Foot IK / Retarget IK Quality Policy가 실제 AnimGraph에 연결

5. Retarget LOD policy가 실제 Retarget Pose From Mesh 평가에 연결

6. SSR transition boundary가 production 호출 경로 테스트로 검증

7. 관련 UE 5.8 build / automation test 성공
```

이 조건이 충족되면 추가 구조 개편은 하지 말고 완료 처리하십시오.
