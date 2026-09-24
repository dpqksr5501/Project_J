# 전체 코드 감사 후속 보완 요청

이 문서는 첫 감사 뒤 남은 런타임 문제를 좁혀 마무리하기 위한 당시의 요청 기록이다. 실제 수행 내용과 검증 결과는 [후속 감사 결과](Project_J_Final_Audit_Followup_Result_2026-09-23.md)를 확인한다.

## 대상

Repository:

```text
https://github.com/dpqksr5501/Project_J
```

Branch:

```text
Character_Test
```

현재 GitHub HEAD 기준으로 작업한다.

확인 당시 HEAD:

```text
d6f54675e4212330bcb88fc03605f7ff32d585b6
```

Unreal Engine:

```text
5.8
```

이번 작업은 이전 전체 코드 감사의 **마지막 후속 수정**이다.

이미 완료된 시스템을 다시 대규모 리팩토링하지 않는다.

목표는 아래 3개 영역만 확실히 마무리하는 것이다.

```text
1. Melee gameplay canonical weapon trajectory 확립
2. 이전 감사 커밋의 사소한 중복 코드 정리
3. Profiling / Debug 코드의 Production 경계 정리
```

---

# 1. 절대 원칙

다음은 유지한다.

```text
GAS authority
PlayerState ASC / Inventory / Equipment
FastArray
SSR sweep history
Input Binding → Router → Execution
NPC bounded async
Motion Matching trajectory 기본 구조
Leader → Follower runtime retarget
Hand IK calibration 구조
Mount async loading
WeaponPresentation socket cache
Animation quality tier
```

이번 작업에서 위 시스템을 다시 설계하거나 갈아엎지 않는다.

새로운 Manager / Subsystem / Thread / ECS / Pool을 추가하지 않는다.

---

# 2. P0 — Canonical Melee Weapon Trajectory

현재 `UProject_JAnimNotifyState_MeleeHit`는 presentation weapon의 `WeaponHit_Tip`을 사용하지 않고 Leader mesh의 `SocketName`을 사용한다.

현재 방향:

```text
Visual Weapon
Follower Retarget
Hand IK

→ Gameplay Hit Authority에서 제외
```

이 방향은 반드시 유지한다.

그러나 현재 기본 `SocketName`이:

```cpp
WeaponSocket_R
```

이고 이 소켓이 손/무기 장착 root에 해당한다면 대검의 실제 칼날 끝 궤적을 표현하지 못할 수 있다.

따라서 다음을 실제 코드/에셋 계약 기준으로 감사하고 수정한다.

## 2.1 목표 구조

최종 구조는 반드시:

```text
Attack / Leader Gameplay Pose
            +
Canonical Gameplay Weapon Geometry
            ↓
Canonical Weapon Sweep
            ↓
Authoritative Sweep History
            ↓
SSR
```

가 되어야 한다.

그리고 별도로:

```text
Presentation Weapon
Independent Weapon Motion
Follower Retarget
Hand IK
Trail / VFX
```

는 cosmetic presentation으로 유지한다.

즉:

**Gameplay hit trace가 visual weapon actor, Follower mesh, Hand IK 결과에 의존하면 안 된다.**

## 2.2 Canonical Gameplay Trace 방법 결정

현재 Project_J 구조에서 가장 단순하고 안정적인 방식을 선택한다.

가능한 방식 예:

### A. Leader skeleton canonical trace socket

Leader에 gameplay 판정 전용:

```text
weapon_trace_root
weapon_trace_tip
```

또는 실제 프로젝트의 기존 canonical socket을 사용한다.

이 경우:

```text
Previous Tip
→ Current Tip
```

만 사용할지,

```text
Blade Root + Blade Tip
```

을 이용한 shape sweep이 필요한지도 확인한다.

### B. Attack / Weapon data 기반 geometry

Leader skeleton에 무기 길이를 직접 표현할 수 없는 경우:

```text
Leader weapon root socket
+
Weapon gameplay length / offset
+
AttackDefinition or weapon gameplay profile
```

을 이용하여 canonical tip을 재구성할 수 있다.

예:

```text
RootTransform
    ↓
Local Blade Tip Offset
    ↓
Canonical World Tip
```

## 2.3 금지

다음으로 되돌아가지 않는다.

```text
PresentationActor->GetSocketTransform()
Follower Mesh socket
Post-Retarget hand position
Hand IK effector
Independent visual weapon transform
```

이들은 절대로 authoritative hit geometry가 아니다.

## 2.4 실제 현재 에셋 계약 확인

다음을 검색한다.

```text
WeaponSocket_R
weapon_r
WeaponHit_Tip
DrawnSocketName
MeleeHit Notify SocketName
Greatsword montages
WeaponPresentationProfile
AttackDefinition
```

특히 기존 Greatsword montage notify들이 어떤 `SocketName`을 직렬화해 가지고 있는지 확인 가능한 범위에서 확인한다.

Binary `.uasset` 내부를 안전하게 확인할 수 없다면 억지로 추정하지 않는다.

그 경우:

- 코드에서 올바른 canonical gameplay contract를 만든다.
- Data Validation을 추가한다.
- 에디터에서 사용자가 확인해야 할 정확한 asset field를 보고한다.

---

# 3. Hit Trace 구현 품질

현재 melee trace는 frame-to-frame sphere sweep을 한다.

다음을 확인한다.

```text
previous position
current position
TraceRadius
AttackDefinition HitSpec
hit window
PredictionKey
AttackNode
SSR history
```

Canonical gameplay weapon trajectory 변경 후에도 기존 SSR history 구조와 정확히 연결되어야 한다.

## 3.1 TraceDistance 의미 확인

현재 `FProject_JComboHitSpec`에는:

```cpp
TraceDistance
TraceRadius
```

가 존재한다.

실제 hit sweep에서 `TraceDistance`가 의미 있게 사용되고 있는지 확인한다.

사용되지 않거나 validation에만 사용된다면:

- 의도된 의미를 명확하게 한다.
- canonical blade geometry와 중복되는 값인지 판단한다.
- 필요하면 Data Validation을 추가한다.

단 기존 DataAsset 호환성을 깨면서 제거하지 않는다.

---

# 4. Canonical Melee Trace Test 보강

현재 테스트는 대략:

```text
visual weapon을 멀리 배치
→ gameplay trace가 visual weapon을 무시함
```

을 검증한다.

이것만으로는 부족하다.

새 테스트는 최소 다음을 검증해야 한다.

### Test A

```text
Presentation Weapon transform이 바뀌어도
Canonical gameplay trace가 변하지 않는다.
```

### Test B

```text
Leader gameplay pose/socket 또는 canonical weapon geometry가 움직이면
trace가 해당 경로를 정확히 따른다.
```

### Test C

```text
blade tip 또는 canonical endpoint가
root/hand socket보다 먼 위치에 존재할 때
실제 sweep endpoint가 canonical tip을 사용한다.
```

### Test D

가능하다면:

```text
same attack / same timestamp
→ authoritative sweep history
→ SSR query
```

까지 연결한다.

---

# 5. P1 — 감사 커밋의 중복 코드 정리

현재 HEAD에서 다음 중복을 제거한다.

## PlayerController

현재:

```cpp
#if !UE_BUILD_SHIPPING
#if !UE_BUILD_SHIPPING

...

#endif
#endif
```

형태의 중복 preprocessor guard가 존재한다.

단일 guard로 정리한다.

## MeleeHit header

현재:

```cpp
friend class FProjectJCanonicalMeleeTraceTest;
friend class FProjectJCanonicalMeleeTraceTest;
```

중복 선언이 존재한다.

하나만 유지한다.

---

# 6. P1 — Profiling / Debug Production Boundary

이전 수정으로 Shipping에서는:

```cpp
ProfilingCrowdComponent
```

가 생성되지 않도록 바뀌었다.

이 방향은 맞다.

이번에는 한 단계 더 감사한다.

현재 `AProject_JPlayerController` public/runtime class에 다음과 같은 profiling command/API가 다수 존재한다.

```text
StartProfilingVisualCrowd
StopProfilingVisualCrowd
DumpProfilingVisualCrowd

StartProfilingReplicatedMovementCrowd
StopProfilingReplicatedMovementCrowd
DumpProfilingReplicatedMovementCrowd

ServerStartProfilingReplicatedMovementCrowd
ServerStopProfilingReplicatedMovementCrowd
ServerDumpProfilingReplicatedMovementCrowd

DumpAnimationExecutionPolicy
DumpMMOProfilingSnapshot
각종 Dump...
```

## 6.1 목표

Production runtime gameplay와 development tooling의 경계를 명확하게 한다.

가능한 구조:

```text
PlayerController
    └─ 실제 gameplay/input/controller 책임

Development / Profiling
    ├─ CheatManager
    ├─ Development-only helper
    └─ ProfilingCrowdComponent
```

## 6.2 주의

단순히 "깨끗해 보이게" 하기 위해 대규모 파일 이동을 하지 않는다.

다음 중 가장 작은 안전한 수정으로 해결한다.

```text
#if !UE_BUILD_SHIPPING
WITH_EDITOR
WITH_DEV_AUTOMATION_TESTS
Development-only component
CheatManager
```

현재 Build/module 구조에서 CheatManager 분리가 지나친 변경이면:

```text
Shipping compile/runtime surface 최소화
```

만 확실히 하고 유지해도 된다.

## 6.3 확인할 것

다음을 구분한다.

```text
실제 게임 운영용 Exec
개발 디버깅용 Exec
성능 프로파일링용 Exec
Automation test용 helper
Editor-only 기능
```

모든 `Exec`를 무조건 제거하지 않는다.

---

# 7. Test / Profiling 코드 감사

다음 규칙을 적용한다.

### Automation Test

가능하면:

```cpp
#if WITH_DEV_AUTOMATION_TESTS
```

로 제한한다.

### Editor-only helper

가능하면:

```cpp
#if WITH_EDITOR
```

사용을 유지한다.

### Profiling runtime fixture

Shipping에서는:

```text
객체 생성 X
Tick X
RPC 실행 X
프로파일링 crowd 생성 X
```

가 보장되어야 한다.

---

# 8. Hand IK는 이번 작업에서 재설계하지 않는다

현재 추가된:

```text
Grip Position
Grip Rotation
Character-specific Hand Offset
Elbow Target
Two-Hand Grip Notify
```

구조는 유지한다.

이번 작업에서는 C++ 값을 다시 재설계하지 않는다.

단 명백한 correctness bug가 발견될 경우만 수정한다.

AnimBP 연결과 실제 visual quality는 사용자 에디터 작업 영역으로 남긴다.

---

# 9. Mount async loading도 다시 갈아엎지 않는다

현재 구현된:

```text
SoftClass
RequestAsyncLoad
WeakThis
Pending handle
Pending item id
EndPlay cancellation
완료 후 inventory / lock / mounted state 재검증
```

구조는 유지한다.

명백한 lifetime/race bug가 없는 한 변경하지 않는다.

---

# 10. Weapon socket cache도 유지한다

현재:

```text
positive socket cache
negative missing socket cache
profile socket 변경 시 invalidate
weapon destroy 시 invalidate
```

구조는 유지한다.

추가 micro optimization은 하지 않는다.

---

# 11. 코드 정리

이번 변경 중 발견되는 다음 정도의 명백한 정리만 수행한다.

```text
중복 #if
중복 friend
깨진 주석 encoding
stale comment
실제 동작과 맞지 않는 comment
```

하지만 범위를 넓혀 프로젝트 전체 style cleanup을 시작하지 않는다.

---

# 12. 검증

수정 후 다음을 수행한다.

## Build

```text
Project_JEditor Win64 Development
```

빌드.

## Automation

최소:

```text
Canonical Melee Trace
Combat SSR
Weapon Presentation / Grip
Animation
Mount
```

관련 테스트 실행.

## 추가 테스트

canonical gameplay weapon trajectory 변경에 대한 새 test를 추가한다.

단 테스트 개수 늘리기가 목표가 아니다.

실제 regression을 막는 테스트만 추가한다.

---

# 13. 완료 조건

아래가 모두 만족되면 작업을 종료한다.

```text
1. Gameplay melee hit geometry가 visual weapon/Follower/IK에 의존하지 않는다.

2. 대검처럼 긴 무기도 hand/root socket이 아니라
   명시적인 canonical gameplay blade trajectory를 사용할 수 있다.

3. SSR history가 같은 canonical trajectory를 기록한다.

4. Presentation weapon을 움직여도 gameplay hit path가 바뀌지 않는다.

5. 중복 #if 제거.

6. 중복 friend 제거.

7. Shipping에서 profiling crowd object/tick/RPC 실행 경로가 없다.

8. Profiling/debug API가 production 구조를 불필요하게 오염시키는 부분을
   현재 프로젝트 범위에서 안전하게 정리했다.

9. Build 성공.

10. 관련 Automation Test 성공.

11. 기존 GAS/MM/Locomotion/Input/NPC/Mount/Retarget 구조에 불필요한
    대규모 리팩토링을 하지 않았다.
```

---

# 14. 작업 종료 규칙

위 조건이 만족되면 **작업을 종료한다.**

다음을 이유로 추가 작업을 계속하지 않는다.

```text
CharacterAnimInstance가 크다
module dependency가 많다
더 예쁜 abstraction이 가능하다
MM smoothing 수식을 바꿀 수 있다
Foot IK를 더 개선할 수 있다
새로운 optimization 아이디어가 있다
```

이런 항목은 이번 작업 범위가 아니다.

새로운 실제 P0/P1 correctness 문제가 발견되지 않으면 더 이상 리팩토링하지 않는다.

---

# 15. 최종 보고

작업 완료 후 아래 형식으로 간단히 보고한다.

```text
# Final Follow-up Result

## Fixed
- Canonical melee trajectory:
- Duplicate cleanup:
- Profiling boundary:

## Canonical Hit Architecture

Before:
...

After:
...

## Tests
- Build:
- Canonical melee:
- SSR:
- Presentation:
- Animation:
- Mount:

## Editor-side Remaining Work
- 실제 Greatsword montage/socket 설정
- AnimBP Grip Rotation / Elbow 연결
- 최소 2체형 visual verification

## Remaining Code Work

없으면 정확히:

No material code follow-up is required for this audit scope.
```

이번 작업은 **이전 코드 감사의 마무리 수정**이다.

범위를 확장하지 말고 위 항목만 안전하게 완료하라.
