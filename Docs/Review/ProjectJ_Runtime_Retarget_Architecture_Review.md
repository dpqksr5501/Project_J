## 결론

`Character_Test` 브랜치의 실제 구현까지 확인해보면, **“단일 Leader에서 Motion Matching/게임플레이 애니메이션을 계산하고, 이종 외형은 Follower가 런타임 리타기팅하며 최종 IK를 수행한다”는 큰 방향은 적절합니다.** UE 5.8도 `Retarget Pose From Mesh + Use Attached Parent`를 공식적인 런타임 리타기팅 방식으로 제공하고 있습니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/runtime-ik-retargeting-in-unreal-engine?utm_source=chatgpt.com))

다만 현재 구조를 대규모 MMORPG용으로 확정하기 전에는 **3개는 반드시 수정**, 나머지 몇 개는 규모 테스트 전에 정리하는 것을 권합니다.

| 우선순위 | 핵심 이슈 | 판단 |
|---|---|---|
| **P0** | Hidden Leader + ABA의 `bTickEvenIfNotRendered=false` | 대규모 전투 전에 수정 권장 |
| **P0** | SSR이 `ClientTimestamp`의 Target을 rewind하면서 Attacker는 `LastAuthoritativeTrace`만 사용 | 시간축 불일치 가능성 |
| **P0** | DS early-out이 Follower Retarget AnimGraph 자체를 끄지는 않음 | “서버 시각 연산 0” 보장 안 됨 |
| **P1** | 현재 `GripIKAlpha`가 전투 시 `0` | 문서상 Hand IK 목적과 코드 의미 재확인 필요 |
| **P1** | Follower가 Leader ABA와 별도 스케줄 | crowd에서 stale source pose 위험 |
| **P1** | Independent Weapon Motion이 DS에서 비활성화 | 공격 판정 궤적과 시각 궤적 분리 필요 |
| **P2** | 무기 탐색 fallback의 반복 component scan | 이벤트 기반으로 완전 전환 |
| **P2** | “0% desync / 0 byte” 문서 표현 | 기술적으로 범위를 좁혀 표현 권장 |

검토 기준은 현재 `Character_Test` HEAD인 **`5a8552d3fe203b138a76d570eb367a375cdc455c`**입니다.  
[Character_Test 브랜치](https://github.com/dpqksr5501/Project_J/tree/Character_Test?utm_source=chatgpt.com)  
[Runtime Retarget / Hand IK 설계 문서](https://github.com/dpqksr5501/Project_J/blob/Character_Test/Docs/Architecture/Runtime_Retarget_HandIK_Architecture.md?utm_source=chatgpt.com)

---

# Q1. 대규모 동접 + Anim Budget Allocator

### 결론

**2 Skeletal Mesh 구조 자체는 사용할 수 있습니다. 하지만 수백 명에게 Leader + Runtime Retarget Follower를 전부 Full Rate로 돌리는 구조는 피해야 합니다.**

현재 Project J는 이미 상당히 좋은 ABA 기반을 구현했습니다.

실제 코드에서 `AProject_JBaseCharacter`의 기본 Mesh를

`UProject_JBudgetedSkeletalMeshComponent`

로 교체했고, 별도의 `UProject_JCharacterAnimationBudgetSubsystem`이 Significance를 ABA에 전달합니다. 또한 Montage/공격처럼 중요한 구간에는 ABA를 탈출하도록 되어 있습니다. Epic도 ABA를 “고정된 GT animation budget 안에서 SkeletalMesh tick rate, interpolation 등을 동적으로 줄이는 시스템”으로 정의합니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/animation-budget-allocator-in-unreal-engine?utm_source=chatgpt.com))

이 부분은 현재 설계에서 꽤 잘 되어 있습니다.

### 그런데 중요한 문제가 하나 있습니다.

현재:

```cpp id="bquumd"
bool bBudgetTickWhenNotRendered = false;
```

이고 이후:

```cpp id="jr5pbr"
Allocator->SetComponentSignificance(
    Mesh,
    1.0f / (1.0f + Tier),
    false,
    Mesh->bBudgetTickWhenNotRendered,
    false,
    false);
```

가 됩니다.

즉 문서대로 **Leader를 항상 Hidden 처리한다면**, ABA 입장에서 Leader는 “렌더링되지 않는 Mesh”가 될 가능성이 높습니다.

ABA에는 실제로 별도의 `bTickEvenIfNotRendered` 플래그가 있으며, 기본 ABA 설정은 offscreen 컴포넌트의 처리 수도 제한합니다. Epic 문서 기준 기본 `MaxTickedOffscreenComponents`는 4입니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/AnimationBudgetAllocator/IAnimationBudgetAllocator/SetComponentSign-?utm_source=chatgpt.com))

따라서 Leader가 정말 항상 Hidden인 구조라면 최소한:

```cpp id="e2y3py"
LeaderMesh->bBudgetTickWhenNotRendered = true;
```

에 해당하는 정책이 필요합니다.

또는 `USkeletalMeshComponentBudgeted::SetShouldUseActorRenderedFlag()` 등을 이용해 **Visible Follower/Actor의 visibility를 Leader significance 판단의 근거로 삼는 방식**도 고려할 수 있습니다. 해당 API가 UE 5.8에 존재합니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/AnimationBudgetAllocator/USkeletalMeshComponentBudgeted?utm_source=chatgpt.com))

이게 Q1에서 가장 먼저 확인할 사항입니다.

---

### Leader와 Follower tick은 독립적으로 줄이면 안 됩니다.

예를 들어:

```text id="740dr8"
Leader    10 Hz
Follower  60 Hz Retarget
```

이면 Follower가 60회 평가되더라도 대부분 **오래된 Leader pose를 다시 리타기팅**하게 됩니다.

ABA에는 interpolation이 있지만 “항상 알아서 매끄럽게 보간해준다”라고 가정하면 안 됩니다. Interpolation은 budget pressure 상황에 따라 적용되고 기본적으로 interpolated component 수에도 제한이 있습니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/animation-budget-allocator-in-unreal-engine?utm_source=chatgpt.com))

따라서 Project J에는 아예

```text id="4szz85"
CharacterAnimationQualityTier
```

를 하나 두고,

```text id="koq4sl"
Leader + Follower + HandIK + FootIK + Cloth
```

을 같은 tier가 통제하도록 만드는 것을 권합니다.

예를 들면:

```text id="ts3ut5"
Tier 0
Leader       Full
Retarget     Full
Retarget IK  Full
Hand IK      Full
Foot IK      Full

Tier 1
Leader       Reduced Rate
Follower     동일한 cadence
Retarget     Full
Hand IK      Off
Foot IK      Reduced

Tier 2
Leader       Low Rate
Follower     Low Rate
Retarget FK  유지
Retarget IK  Off
Hand/Foot IK Off

Tier 3
Runtime Retarget 자체 우회
Cheap ABP / baked subset / proxy / VAT / crowd representation
```

특히 UE 5.8 `Retarget Pose From Mesh` 자체에 이미:

- `LODThreshold`
- `LODThresholdForIK`

가 있습니다. 별도의 시스템을 만들기 전에 이것부터 적극 사용하는 게 좋습니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/IKRig/FAnimNode_RetargetPoseFromMesh?utm_source=chatgpt.com))

그래서 문서의 “LOD2부터 곧바로 VAT”보다는:

**Full Retarget → IK 없는 Retarget → Retarget 제거**

3단계가 더 실용적입니다.

---

# Q2. Dedicated Server Hitbox와 보이는 검 궤적

### 현재 Project J 방향은 상당 부분 맞습니다.

실제 코드를 보면 이미 단순히 “클라이언트 칼 위치를 서버가 믿는 구조”가 아닙니다.

`UProject_JCombatHitValidationComponent::BeginAttackNode()`에서 공격 중 Leader를:

```text id="jtifpq"
GameplayPose
→ AlwaysTickPoseAndRefreshBones
→ URO off
→ Notify dispatch on
```

으로 보호합니다.

그리고 `UProject_JAnimNotifyState_MeleeHit`에서 서버가 자체적으로 weapon sweep을 생성해:

```cpp id="13wgd4"
RecordAuthoritativeTrace(TraceStart, TraceLocation);
```

합니다.

클라이언트는 hit candidate를 보내지만 최종 SSR은 서버의 trace를 사용합니다.

방향은 정확합니다.

**Follower의 Two-Bone IK 위치는 gameplay authority가 되면 안 됩니다.**

최종 시각용 골격과 서버 공격 판정 골격을 완벽하게 일치시키려고 서버에서도 모든 Follower Retarget/IK를 실행하는 방식은 MMORPG에서 비용 대비 효과가 좋지 않습니다.

권장 관계는:

```text id="td63ge"
Server
Canonical attack trajectory
        ↓
Gameplay weapon sweep
        ↓
Hit validation

Client
Canonical trajectory
        ↓
Visible weapon
        ↓
Follower arm IK → weapon
```

입니다.

즉 **손이 검을 결정하는 게 아니라, 게임플레이상 검 궤적이 있고 손을 거기에 맞추는 구조**가 좋습니다.

---

## 그런데 현재 SSR에는 중요한 시간축 문제가 있습니다.

클라이언트 timestamp는 잘 처리되어 있습니다.

실제 코드는:

```cpp id="et2z9l"
GameState->GetServerWorldTimeSeconds()
```

를 사용하므로 클라이언트 로컬 clock이 아니라 server-time domain으로 맞춰져 있습니다.

이건 좋습니다.

하지만 target은:

```cpp id="yqava1"
ServerVerifyHit(ClientTimestamp, ...)
```

로 과거 capsule을 rewind하면서,

공격자의 검 궤적은:

```cpp id="lt2ua4"
LastAuthoritativeTraceStart
LastAuthoritativeTraceEnd
```

즉 **서버가 현재 보유한 가장 최근 trace 한 개**를 사용합니다.

따라서 개념적으로:

\[
T_{\text{target}} = T_{\text{client hit}}
\]

인데

\[
T_{\text{weapon}} = T_{\text{latest server attack}}
\]

가 될 수 있습니다.

이 둘을 같은 시간축으로 맞춰야 합니다.

### 권장 수정

Attacker도 작은 history를 유지하세요.

```cpp id="yci4di"
struct FWeaponSweepFrame
{
    float ServerTimestamp;
    int32 PredictionKey;
    FGameplayTag AttackNode;
    FVector Start;
    FVector End;
};
```

20~60Hz 정도의 짧은 ring buffer면 충분합니다.

SSR에서는:

```text id="fjs799"
ClientTimestamp
   ↓
AttackerSweepHistory에서 해당 timestamp의 sweep 복원
   ↓
Target capsule도 같은 timestamp로 rewind
   ↓
둘을 교차 검사
```

해야 합니다.

더 좋은 방법은 공격을 데이터 기반으로 완전히 만든 뒤:

```text id="diykqa"
AttackDefinition
+ PredictionKey
+ Montage normalized phase
→ deterministic canonical weapon sweep
```

으로 재구성하는 것입니다.

그러면 Server에서 실제 visual weapon actor가 없어도 됩니다.

---

## 또 하나: Independent Weapon Motion

`UProject_JWeaponPresentationComponent::BeginIndependentMotion()`에는:

```cpp id="h1f9wt"
OwnerCharacter->GetNetMode() == NM_DedicatedServer
```

면 return하는 코드가 있습니다.

즉 독립적인 검 궤적은 Client presentation에서만 실행됩니다.

이건 **시각 효과라면 정확합니다.**

하지만 `WeaponHit_Tip` 같은 그 위치를 공격 판정에도 사용한다면 문제가 됩니다.

따라서 반드시:

```text id="a4n3z0"
GameplayWeaponTrajectory
```

와

```text id="gpcdfo"
WeaponPresentation
```

을 분리하세요.

같은 AttackDefinition 데이터를 둘 다 읽을 수는 있지만, 서버 판정이 PresentationActor 존재 여부에 의존하면 안 됩니다.

---

# Q3. Foot Placement → Retarget → 다른 다리 길이

### IK Retargeter만으로 모든 지형 접지를 해결한다고 보면 안 됩니다.

IK Retargeter는 서로 다른 신체 비율에서도 IK contact를 유지하는 기능과 Speed Planting을 제공합니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/ik-rig-animation-retargeting-in-unreal-engine?utm_source=chatgpt.com))

하지만 이것은:

**“source foot target을 target skeleton에 잘 옮기는 문제”**

이고,

Foot Placement는:

**“현재 월드 지형에 실제 target 발이 닿도록 하는 문제”**

입니다.

둘은 다른 문제입니다.

특히 Bip01 여성형과 UE Mannequin처럼:

```text id="vx3mt8"
대퇴 길이
종아리 길이
pelvis height
발 크기
bind pose
```

가 다르면 Leader에서 지형에 정확히 맞춘 foot transform을 Retarget해도 최종 Follower에서는 약간 뜨거나 파묻힐 수 있습니다.

Epic의 Foot Placement 노드 자체에도 trace 설정, pelvis 설정, plant plane 계산 등이 별도로 존재합니다. 현재 UE 5.8 API에서는 이 노드가 여전히 `Experimental` 표시입니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/AnimationWarpingRuntime/FAnimNode_FootPlacement?utm_source=chatgpt.com))

### 제가 권하는 구조

근거리 캐릭터만:

```text id="vx5x6z"
Leader Base Locomotion
        ↓
Leader Foot Placement
        ↓
Runtime Retarget
        ↓
Retarget IK / Speed Plant
        ↓
Follower Residual Foot Correction
```

입니다.

단, 마지막 correction은 두 번째 강한 Foot Placement를 통째로 또 돌리는 식이면 pelvis가 이중 보정될 수 있습니다.

그래서 Follower 단계는:

```text id="q7kw6g"
foot penetration / hovering residual
+ small pelvis residual
```

정도로 제한하는 게 좋습니다.

또한 원거리 캐릭터는:

```text id="ea2lh5"
Follower Foot Placement = Off
```

로 두고 Retargeter의 IK/Speed Plant 정도만 유지하면 됩니다.

수백 명에게 World Trace 기반 Foot Placement를 두 번씩 수행하는 것은 피해야 합니다.

---

# Q4. Offline Retarget vs Runtime Retarget의 손익분기점

정확한 “몇 명부터”라는 숫자는 없습니다.

두 방식의 병목이 서로 다른 축이기 때문입니다.

외형 종류를 \(V\), 한 외형의 복제된 animation/PSD resident 비용을 \(M_a\)라고 하면 Offline 방식은 대략:

\[
M_{\text{offline}} \approx V M_a
\]

입니다.

반대로 현재 방식에서 동시에 runtime retarget되는 캐릭터 수를 \(N\), 캐릭터 하나의 Retarget + 후처리 CPU를 \(C_r\)라 하면:

\[
C_{\text{runtime}} \approx N C_r
\]

입니다.

그리고 실제 조건은:

\[
N C_r \leq B_{\text{retarget}}
\]

여야 합니다.

여기서 \(B_{\text{retarget}}\)은 여러분이 전체 animation frame budget 중 Retarget에 허용한 ms입니다.

예를 들어 **측정 결과** 한 Follower가 평균 \(0.04\,\mathrm{ms}\)라 가정하고 이 레이어에 \(2\,\mathrm{ms}\)를 허용했다면:

\[
N_{\max} \approx \frac{2}{0.04}=50
\]

입니다.

이 `0.04 ms`는 예시값이지 Project J 예상값이 아닙니다.

### 실무 계획용 기준으로는

대략 다음 정도로 생각하면 됩니다.

**20~30명 정도의 중요 캐릭터**는 Full Runtime Retarget 대상으로 설계할 수 있지만, **50~100명이 동시에 화면에 들어오기 시작하면 반드시 quality tier를 전제로 해야 하고, 100~200명 전체에 Motion Matching Leader + Runtime IK Retarget + Hand/Foot IK를 Full Rate로 돌리는 것을 목표로 잡지는 않는 것**이 좋습니다.

즉 Project J처럼 외형 종류가 계속 증가하는 MMORPG에서는:

**Offline 전면 복제보다 현재 Runtime + Distance Hybrid 방식이 더 적절합니다.**

---

# Q5. Root Motion과 Follower Foot Sliding

### 기본 구조는 맞습니다.

게임플레이 Root Motion 몽타주는 Leader에서만 재생하는 것이 맞습니다.

Epic의 네트워크 CharacterMovement 문서에서도 AnimMontage의 Root Motion은 `FSavedMove_Character`에 montage/track position 등의 정보와 함께 포함되며, CharacterMovement가 skeleton root 이동을 world movement로 소비합니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/understanding-networked-movement-in-the-character-movement-component-for-unreal-engine?utm_source=chatgpt.com))

따라서:

```text id="8vfep5"
Leader Montage
   ↓
Root Motion
   ↓
CharacterMovement
   ↓
Capsule movement
```

를 authority로 삼고,

```text id="l5yalj"
Leader Pose
   ↓
Runtime Retarget
   ↓
Follower Visual
```

이어야 합니다.

Follower에서 Root Motion을 또 적용하면 안 됩니다.

---

### 다만 Foot Sliding 가능성은 있습니다.

원인은 Root Motion 자체보다는:

1. Follower의 다리 비율
2. Retarget root translation 설정
3. Leader와 Follower update cadence 차이
4. ABA/URO에 의한 stale Leader pose
5. 네트워크 correction
6. Follower가 별도의 montage phase를 재생하는 경우

입니다.

특히 현재 공격 시작 시 `GameplayPose` 요청과 Montage 시작 시 ABA 탈출을 하는 것은 아주 좋은 선택입니다.

제가 추가할 것은:

```text id="6rwf92"
RootMotion / Attack / Dodge:
Leader = Never Skip
Follower = same-frame presentation priority
Hand/Foot relevant IK = same update window
```

입니다.

필요하면 ABA의:

```cpp id="zlr7al"
ForceNextTickThisFrame()
```

도 사용할 수 있습니다. UE 5.8 ABA에서 제공하는 API입니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/API/Plugins/AnimationBudgetAllocator/IAnimationBudgetAllocator?utm_source=chatgpt.com))

그리고 런타임에 반드시:

```text id="shllx0"
Leader evaluated frame
→ Follower Retarget evaluation
```

순서가 보장되는지 Animation Insights로 확인하세요.

Attached Parent 구조가 일반적인 runtime retarget 구성 자체는 맞지만, ABA를 별도로 적용하기 시작하면 **스케줄링까지 별개 문제**가 됩니다.

---

# Q6. Ragdoll lifecycle

여기는 현재 문서에는 설계가 있지만, 제가 확인한 C++ 소스에서는 Follower ragdoll lifecycle 전체 구현은 확인되지 않았습니다.

권장 흐름은 다음입니다.

```text id="jzps8m"
Alive
 ↓
Death gameplay state confirmed
 ↓
Cancel abilities / montage gameplay
 ↓
CharacterMovement Disable
 ↓
Capsule gameplay collision 변경
 ↓
Follower current pose 유지
 ↓
Follower PhysicsAsset simulation ON
 ↓
Leader animation update 중단
 ↓
Corpse significance에 따라 Physics 저감
 ↓
Sleep → pose freeze / cheap corpse
```

핵심은 **Follower physics가 시작된 다음에도 Hidden Leader를 계속 60Hz로 돌리지 않는 것**입니다.

죽은 순간부터 gameplay animation이 필요 없다면:

```cpp id="o355hb"
Leader->SetComponentTickEnabled(false);
```

에 해당하는 lifecycle을 적용할 수 있습니다.

단 부활 시스템이 있다면 Leader를 파괴하지 말고 component를 유지하는 편이 좋습니다.

부활 시:

```text id="929z19"
Follower Physics Off
→ pelvis/root 위치에서 capsule의 안전 위치 계산
→ mesh relative transform 복구
→ Leader AnimInstance 정상화
→ Follower Retarget 재활성
→ movement/collision 복구
```

순서가 좋습니다.

그리고 MMO에서 corpse ragdoll을 서버까지 full Chaos simulation할 필요는 보통 없습니다.

시체가 gameplay collision과 무관하다면:

```text id="w176mn"
Server = death root/capsule only
Client = cosmetic ragdoll
```

로 두고 시간이 지나면 physics도 꺼야 합니다.

시체 위치가 gameplay에 중요하면 pelvis/root 또는 단순 collision proxy만 서버 authoritative로 유지하는 쪽이 낫습니다.

---

# Q7. 현재 코드에서 보이는 추가 Blind Spot

## 1. Dedicated Server early-out은 Retarget AnimGraph 자체를 끄지 않습니다

이 부분은 문서를 수정해야 합니다.

현재:

```cpp id="h9uv5r"
NativeUpdateAnimation()
{
    if (NM_DedicatedServer)
        return;
}
```

한다고 해서 이후 AnimGraph의:

```text id="qzg4as"
Retarget Pose From Mesh
Two Bone IK
```

노드 자체가 자동으로 실행되지 않는 것은 아닙니다.

즉 DS에서 Follower component와 AnimInstance가 존재하고 tick/evaluate된다면 런타임 리타기팅 비용이 남을 수 있습니다.

진짜 Client-only Follower라면 Dedicated Server에서:

```text id="izc3b3"
Follower 생성 안 함
또는
Follower tick disabled
또는
AnimInstance 제거/비활성
```

까지 해야 합니다.

---

## 2. 서버에서도 Weapon Presentation Actor가 생성될 가능성이 있습니다

`WeaponPresentationComponent::CanCreatePresentation()`에는 Dedicated Server 차단 조건이 없습니다.

그리고 `RefreshPresentation()`은:

```cpp id="q6k3yu"
SpawnActor<AActor>(WeaponActorClass...)
```

을 수행합니다.

Independent Motion만 DS에서 차단되어 있습니다.

따라서 문서의:

> 비주얼 전용 서버 CPU 0

이라는 표현은 현재 코드 기준으로는 정확하지 않습니다.

게임플레이에 무기 Actor가 필요 없다면 DS에서는 PresentationActor 자체를 생성하지 않고 앞에서 말한 `GameplayWeaponTrajectory`를 별도 데이터로 유지하세요.

---

## 3. 현재 `GripIKAlpha` 의미가 문서와 다릅니다

실제 코드:

```cpp id="l455is"
TargetAlphaSnapshot =
    bIsCombatMode
    ? 0.0f
    : (bHasValidSocketSnapshot ? 1.0f : 0.0f);
```

그리고 주석도:

```text id="qwzwjq"
Combat stance → weapon drawn → back-grip IK OFF
Sheathed      → back-grip IK ON
```

입니다.

즉 `UProject_JRetargetAnimInstance`의 현재 `RightGripLocation / GripIKAlpha`는 **발도 상태의 손잡이 파지를 보정하는 시스템이라기보다는 납도 상태용 grip correction**으로 구현되어 있습니다.

반면 `WeaponPresentationComponent`에는 이미:

```text id="ddlvh4"
PrimaryGrip
SecondaryGrip
PrimaryIKAlpha
SecondaryIKAlpha
```

개념이 존재합니다.

따라서 저는 아예:

```text id="lbx7bu"
WeaponPresentationComponent
        ↓
FWeaponGripSnapshot
        ↓
Retarget AnimInstance Proxy
        ↓
Right/Left Hand IK
```

로 통합하는 것을 권합니다.

---

## 4. `bAutoDetectWeaponIfNull`는 MMORPG에서는 제거에 가깝게

현재 Weapon reference가 없으면 매 프레임:

```text id="jv4t5y"
Owner GetComponents
→ 모든 component socket 검색
→ AttachedActor 검색
→ AttachedActor components 검색
```

까지 갈 수 있습니다.

장비 시스템에서 이미 lifecycle을 알고 있으므로:

```cpp id="1tcnh1"
UpdateWeaponTarget()
```

를 Equip/Unequip 이벤트에서 반드시 호출하고 production에서는:

```cpp id="wzv7n6"
bAutoDetectWeaponIfNull = false;
```

가 더 좋습니다.

fallback은 debug/legacy migration용만 유지하세요.

---

## 5. Thread-safe 구조는 방향이 맞지만 Proxy로 한 단계 정리 가능

현재:

```text id="9ieo5z"
NativeUpdateAnimation (GT)
→ Transform snapshot
NativeThreadSafeUpdateAnimation (Worker)
→ pure math
```

는 UE 권장 방향과 일치합니다. Epic도 `NativeUpdateAnimation`에서는 데이터 수집을 하고 bulk work는 `NativeThreadSafeUpdateAnimation`에서 수행하는 것을 권장합니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/UAnimInstance/NativeUpdateAnimation?utm_source=chatgpt.com))

그래서 현재 구현을 “잘못된 멀티스레드 코드”라고 볼 이유는 없습니다.

다만 프로젝트 규모를 생각하면 `FAnimInstanceProxy::PreUpdate()`에서 GT snapshot을 만들고 proxy 데이터만 worker가 소비하도록 변경하면 ownership이 더 명확해집니다. Epic도 `FAnimInstanceProxy::PreUpdate`를 update 전에 필요한 데이터를 복사하는 지점으로 제공합니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/FAnimInstanceProxy?utm_source=chatgpt.com))

Blueprint 쪽은 Property Access / Thread Safe 경로를 유지하면 좋습니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/animation-optimization-in-unreal-engine?utm_source=chatgpt.com))

---

## 6. “0% Desync”, “0-byte network” 표현은 수정 권장

정확한 표현은:

> **IK/본 트랜스폼에 대한 추가적인 per-frame 네트워크 replication은 0 bytes이다.**

가 맞습니다.

CombatMode, Equipment, Montage/ability state 등은 당연히 네트워크 상태를 전달합니다.

또한 손 IK를 local solve한다고 해서 모든 종류의 desync가 0%가 되는 것은 아닙니다.

예를 들어 remote client가 아직:

```text id="58ckfx"
Draw state
Weapon attach event
Montage phase
Equipment revision
```

을 받지 못했다면 로컬 IK가 정확해도 잘못된 상태에 정확히 맞출 뿐입니다.

따라서 문서에는:

> 동일한 로컬 presentation state가 확보된 이후에는 Hand IK target 자체를 네트워크로 복제할 필요가 없으며, 손-무기 간 추가적인 좌표 replication 오차를 발생시키지 않는다.

정도가 기술적으로 정확합니다.

---

# 최종 권장 아키텍처

Project J는 결국 이 구조로 정리하는 것을 권합니다.

```text id="nejvus"
                    SERVER / GAMEPLAY
                           │
                  AttackDefinition
                           │
            Canonical Weapon Trajectory
                           │
            ┌──────────────┴───────────────┐
            │                              │
     Server Hit Sweep               Root Motion
            │                              │
        SSR History                  CharacterMovement
            │                              │
      Damage Authority                   Capsule

=====================================================

                    CLIENT / VISUAL
                           │
                     Leader Mesh
                           │
       Motion Matching / Montage / Foot Base
                           │
                 Retarget Pose From Mesh
                           │
                    Follower Mesh
                           │
          ┌────────────────┴───────────────┐
          │                                │
     Residual Foot IK             Weapon Grip Snapshot
                                           │
                                  Post-Retarget Hand IK
                                           │
                                      Final Render
```

그리고 Significance는:

```text id="vnh252"
Character
   ↓
Unified Animation Quality Tier
   ├─ Leader ABA
   ├─ Follower Retarget LOD
   ├─ Retarget IK LOD
   ├─ Hand IK
   ├─ Foot IK
   ├─ Cloth/Hair
   └─ Weapon Presentation
```

**한 군데에서 통제하는 구조**가 가장 중요합니다. Significance Manager 자체는 성능을 자동으로 개선하는 기능이 아니라 프로젝트가 각 시스템의 품질을 결정하기 위한 프레임워크라는 Epic 설명과도 맞습니다. ([dev.epicgames.com](https://dev.epicgames.com/documentation/unreal-engine/significance-manager-in-unreal-engine?utm_source=chatgpt.com))

종합하면 **현재 Leader/Follower Runtime Retarget 방향은 유지해도 됩니다.** 오히려 외형 종류가 계속 늘어나는 MMORPG에는 합리적인 선택입니다. 다만 출시 규모를 생각한다면 우선 **Hidden Leader ABA 처리 → SSR attacker sweep history → DS Follower/Presentation 제거 → Grip pipeline 단일화 → Leader/Follower coupled LOD** 순으로 손보는 것이 좋습니다. 이 다섯 개를 반영하면 지금 구조는 훨씬 안정적인 대규모 MMORPG용 파이프라인이 됩니다.
