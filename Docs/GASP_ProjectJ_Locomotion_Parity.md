# GASP ↔ Project_J Locomotion / State Controller 대응표

> 작성일: 2026-08-02  
> 범위: 현재 검토한 GASP `SandboxCharacter_CMC_AnimBP` 자료와 Project_J의 비전투 OTM locomotion, Motion Matching, Experimental State Controller 경로.  
> 이 문서는 **구현 사실**과 **추가 확인이 필요한 GASP 세부 동작**을 분리한다. 에셋 연결값은 Unreal Editor에서 최종 확인한다.

## 1. 결론과 작업 경계

Project_J는 GASP를 Blueprint 그대로 복사하지 않는다. 게임플레이 및 locomotion 사실은 C++에서 계산하고, AnimInstance는 게임 스레드 snapshot을 proxy로 복사하며, ABP는 thread-safe getter로 포즈만 조립한다.

현재 OTM Run/Sprint Start는 State Controller의 direct Blend Stack 경로로 동작한다. 다음 대상인 **OTM Reface Start**는 GASP의 `Target Rotation Delta` 기반 asset 선택만 추가하며, Combat Strafe Pivot, rotation-break, Steering, TIP를 함께 켜지 않는다.

```text
GASP
Character Properties / Trajectory
  -> AnimBP Update_Logic
  -> State Controller logical state
  -> CHT_CMCCharacterAnimations
  -> SetBlendStackAnimFromChooser
  -> Blend Stack / final AnimGraph

Project_J
PlayerCharacter + LocomotionAnimStateComponent
  -> CharacterAnimInstance game-thread snapshot
  -> CharacterAnimInstanceProxy
  -> State Controller chooser evaluation (game thread)
  -> ABP_Humanoid_Master StateController + Blend Stack
  -> Two Way Blend + Blend Poses by Bool + Locomotion cache / final graph
```

## 2. 현재 GASP ABP 구조와 Project_J 구조

### GASP `SandboxCharacter_CMC_AnimBP` (검토된 범위)

```text
EventGraph
  - Event Blueprint Update Animation
  - Event Blueprint Post Evaluate Animation
  - SM Transition Debug Events

Blueprint Thread Safe Update Animation
  - Update_Logic
    -> Update Trajectory
    -> Update Essential Values
    -> Update States
    -> Update Movement Direction (필요할 때)
    -> Update Target Rotation

AnimGraph
  - State Controller (논리 상태 머신; pose 직접 출력 없음)
  - AnimationBlendStackGraph_0 (State Controller 선택 asset 재생)
  - AnimationBlendStackGraph_0 (MM 내부 Blend Stack graph)
  - Motion Matching / Foot Placement / Aim Offset 등 보조 레이어
```

### Project_J `ABP_Humanoid_Master` 및 native 경로

```text
AProject_JPlayerCharacter
  -> UProject_JLocomotionAnimStateComponent
     - 입력, 이동/공중/착지, trajectory, phase 및 OTM/Strafe 정책
  -> UProject_JCharacterAnimInstance::NativeUpdateAnimation
     - BuildThreadSafeData
     - State Controller Chooser game-thread evaluation
     - proxy publish
  -> FProject_JCharacterAnimInstanceProxy
     - native Motion Matching database / interrupt policy
  -> ABP_Humanoid_Master
     - regular Motion Matching fallback
     - logical StateController
     - direct Blend Stack one-shot
     - Two Way Blend + Blend Poses by Bool
     - Locomotion pose cache -> linked combat layer / slots / AO / IK / Pose History
```

`ABP_Player`/`BP_Player`는 테스트 경로다. 생산용 휴머노이드 기준은 `AProject_JPlayerCharacter -> 직업 native class -> BP_직업` 및 `ABP_Humanoid_Master + Linked Anim Layer`다.

## 3. 함수 및 그래프 대응표

상태는 다음 의미다.

- **구현됨**: Project_J에 동등한 책임의 실행 코드가 있다.
- **부분 구현**: 필요한 기반 또는 일부 정책은 있으나 GASP와 같은 전체 계약은 아직 아니다.
- **의도적 보류/제외**: 현재 OTM/no-root-motion/MMO 경계와 충돌하거나 범위 밖이다.
- **확인 필요**: 원본의 정확한 조건·파라미터를 아직 받지 못해 구현 여부를 단정하지 않는다.

| GASP 함수/그래프 | GASP의 검토된 역할 | Project_J 대응 위치 | 상태 | 비고 |
|---|---|---|---|---|
| `Update_PropertiesFromCharacter` | Character Properties를 ABP에 수집 | `UProject_JCharacterAnimInstance::BuildThreadSafeData` | 구현됨 | ABP가 Actor/Component를 직접 읽지 않고 native snapshot을 사용한다. |
| `Blueprint Thread Safe Update Animation` | worker-thread용 driver 값 갱신 | `FProject_JCharacterAnimInstanceProxy` 및 thread-safe getters | 구현됨 | proxy snapshot이 동일 책임을 수행한다. |
| `Update_Logic` | Trajectory, essential value, state, direction, target rotation orchestration | `UProject_JLocomotionAnimStateComponent` + `UProject_JCharacterAnimInstance` | 구현됨(분리형) | 단일 Blueprint 함수 대신 C++ 책임으로 분리했다. |
| `Update Trajectory` | 미래 trajectory 생성 | `UProject_JMotionMatchingTrajectoryComponent::UpdateTrajectoryState` | 구현됨 | AnimInstance가 primary mesh일 때 game thread에서 갱신한다. |
| `IsMoving` | 현재/미래 속도와 가속을 고려한 이동 의도 | `IsMotionMatchingMovingForContext` -> `bIsMotionMatchingMoving` | 구현됨 | broad gameplay movement와 MM presentation movement를 분리한다. |
| `IsStarting` | 미래 속도가 현재 속도보다 충분히 빠르고 Pivot DB가 아닐 때 Start | `IsStartingForContext` 및 `bIsStarting` | 구현됨/부분 | 미래 velocity 기반이다. GASP의 `Current Database Tags contains Pivots` 차단은 State Controller direct-asset 경로에는 그대로 존재하지 않으며, OTM에는 필요 없다. |
| `Get_TrajectoryTurnAngle` | GASP 화면상 Acceleration 방향과 Velocity 방향의 signed yaw delta | `MoveInputTurnAngle`, `VelocityToMoveInputAngle`, `FutureTrajectoryTurnAngle` | 부분 구현 | Project_J `FutureTrajectoryTurnAngle`은 velocity→future trajectory의 **절대값**이다. Reface 자산 좌/우 선택에는 직접 쓰지 않는다. |
| `Update States` | Movement Mode, State, Gait, Stance 등 결정 | `BuildLocomotionContext` / `FProject_JDerivedLocomotionContext` | 구현됨 | Gait, OTM/Strafe, phase, air/landing snapshot을 보유한다. |
| `Update Movement Direction` | GASP F/B/LL/LR/RL/RR 및 foot bias | `ResolveStateControllerStrafeDirection` | 구현됨 | Strafe에서 F/B/LL/LR/RL/RR와 static bias를 지원한다. OTM은 항상 Forward이며 OTM Reface에는 사용하지 않는다. |
| `Get_MovementDirectionThresholds` | direction hysteresis/foot-forward 결정 | Strafe sector boundaries (-45/45/-135/135) | 부분 구현 | 현재는 고정 경계다. GASP식 per-profile threshold/hysteresis는 Combat Strafe 저작 후 확장한다. |
| `Update Target Rotation` | target yaw, root/visual rotation 갱신 | `DesiredFacingDeltaYaw` 계산만 존재 | 부분 구현 | OTM Reface에 필요한 signed actor→입력 yaw는 이미 있다. GASP의 strafe target-rotation/steering은 보류다. |
| `IsPivoting` | moving Pivot 선별 | `IsPivotingForContext` | 부분 구현 | Combat Strafe 전용으로 제한된다. OTM Reface Start에 Pivot을 섞지 않는다. |
| rotation-break reselect | Start/Pivot 중 target yaw 변화로 재선택 | 없음 | 의도적 보류 | GASP는 Start/Pivot tag와 target rotation cache가 필요하다. OTM 첫 Reface 범위에는 불필요하다. |
| `ShouldTurnInPlace` | idle 상태 yaw mismatch의 TIP 조건 | `ShouldTurnInPlaceForContext` | 부분 구현/비활성 | TIP 표현/asset 경로는 아직 연결하지 않는다. |
| `ShouldSpinTransition` | 큰 yaw mismatch의 회전 transition | `ShouldSpinTransitionForContext` | 부분 구현/비활성 | no-root-motion 정책상 GASP graph를 그대로 활성화하지 않는다. |
| `Get_MMInterruptMode` | core locomotion 변화 때만 MM interrupt | `FProject_JCharacterAnimInstanceProxy::ResolveDatabaseChangeInterruptMode` | 구현됨 | 기본 DoNotInterrupt, core state 변화만 interrupt한다. |
| `SetBlendStackAnimFromChooser` | Chooser 결과/메타데이터를 Blend Stack에 설정하고 필요 시 1회 MM | `EvaluateStateControllerAnimationChooserOnGameThread` + ABP Blend Stack | 구현됨/차이 있음 | Project_J는 현재 최종 `UAnimationAsset` 하나를 선택해 entry Motion Match한다. GASP는 여러 valid asset 후보를 한 번의 MM에 넘길 수 있다. |
| `IsAnimationAlmostComplete` | non-loop asset 종료 전 transition->loop | `GetThreadSafeStateControllerSelectedAnimationAlmostComplete` | 구현됨 | 범용 0.75초 상수는 복사하지 않고 asset metadata/notify를 우선한다. |
| `BP_NotifyState_EarlyTransition` | authored transition 조기 이탈 창 | `UProject_JAnimNotifyState_LocomotionEarlyTransition` | 구현됨, 현재 미사용 | asset 끝에서 자연스럽게 loop로 넘어가는 현재 Start/Reface에는 필요 없다. transition을 의도적으로 일찍 끊어야 할 때만 저작한다. |
| `CHT_PoseSearchDatabases` | regular MM PSD array 선택 | Locomotion Profile/AssetSet 및 native MM DB binding | 구현됨 | direct State Controller table과 분리한다. |
| `CHT_CMCCharacterAnimations` | logical state 조건으로 AnimationAsset + `S_ChooserOutputs` 반환 | `CHT_Player_StateControllerAnimations` -> Rotation Mode별 하위 테이블 | 구현됨(OTM 일부) | OTM Idle/Start/Loop/Stop이 연결되어 있다. Strafe/Air/Land 하위 테이블도 최상위에서 분기한다. |
| `S_ChooserOutputs` | Start Time, Use MM, Cost, Blend, Profile, Tags | `FProject_JStateControllerChooserOutput` | 구현됨 | `StartTime`, `bUseMotionMatch`, `MotionMatchCostLimit`, `BlendTime`, `BlendProfile`, `Tags`. |
| Steering / Offset Root Bone | root-motion 중심 locomotion steering | 없음 | 의도적 제외 | `Root Motion from Everything` 금지와 충돌한다. |
| Traversal | traversal montage / trajectory handling | 없음 | 범위 제외 | 현 locomotion 작업 범위 밖이다. |

## 3A. GASP 함수 목록 전체 분류

아래 표는 제공받은 GASP 함수 제목을 빠짐없이 현재 Project_J와 대조한 작업용 목록이다. `이식 우선도`는 **현재 OTM Reface Start 작업 기준**이다.

### Movement Analysis

| GASP 함수 | Project_J 대응 | 상태 | 이식 우선도 / 판단 |
|---|---|---|---|
| `IsMoving` | `IsMovingForContext`, `IsMotionMatchingMovingForContext` | 구현됨 | 유지 |
| `IsStarting` | `IsStartingForContext` | 구현됨/부분 | 유지. GASP Pivot-tag 억제는 Combat Strafe에서만 검토 |
| `IsPivoting` | `IsPivotingForContext` | 부분 구현 | OTM에는 불필요, Combat Strafe 후속 |
| MM Pivot conditions | `IsPivotingForContext`, selection revision/reselect 정책 | 부분 구현 | Combat Strafe 전용 |
| SM Pivot Condition | `PhaseFamily::Pivot`, one-shot request | 부분 구현 | Combat Strafe direct Pivot row 저작 후 연결 |
| `ShouldTurnInPlace` | `ShouldTurnInPlaceForContext` | 부분 구현/비활성 | TIP 독립 트랙에서 구현 |
| `ShouldSpinTransition` | `ShouldSpinTransitionForContext` | 부분 구현/비활성 | ORB/rotation-owner 설계 전 보류 |
| `JustTraversed` | 없음 | 제외 | Traversal 범위 밖 |
| `JustLanded_Light` | `bIsLanding`, `bUseHeavyLand`, `LastFallSpeed` | 구현됨 | direct Land chooser 저작 시 사용 |
| `JustLanded_Heavy` | `bIsLanding`, `bUseHeavyLand`, `LandStartFallSpeed` | 구현됨 | direct Land chooser 저작 시 사용 |
| `Get_LandVelocity` | `LastFallSpeed`, `LandStartFallSpeed` snapshot | 구현됨 | 유지 |
| `Get_TrajectoryTurnAngle` | `MoveInputTurnAngle`, `VelocityToMoveInputAngle`, `FutureTrajectoryTurnAngle` | 부분 구현 | Reface 좌/우 선택은 `DesiredFacingDeltaYaw` 사용 |
| `PlayLand` | `HandleLanded`, landing phase/snapshot | 구현됨 | 유지 |
| `PlayMovingLand` | landing redirect/cancel 정책 | 부분 구현 | direct Land chooser 전에는 값/조건 확인 필요 |

### Root Offset / Orientation Warping

| GASP 함수 | Project_J 대응 | 상태 | 이식 우선도 / 판단 |
|---|---|---|---|
| `Get_OffsetRootRotationMode` | 없음 | 의도적 보류 | ORB의 visual-yaw owner가 정해진 뒤 Combat 전용으로 검토 |
| `Get_OffsetRootTranslationMode` | 없음 | 의도적 보류 | 동일 |
| `Get_OffsetRootTranslationHalfLife` | 없음 | 의도적 보류 | 동일 |
| `Get_OffsetRootTranslationRadius` | 없음 | 의도적 보류 | 동일 |
| `Get_OrientationWarpingWarpingSpace` | 기존 ABP Orientation Warping presentation | 부분 구현 | ORB 의존 GASP 조건은 이식하지 않음 |

### Aim Offset / Additive Lean / Foot Placement

| GASP 함수 | Project_J 대응 | 상태 | 이식 우선도 / 판단 |
|---|---|---|---|
| `Enable_AO` | `CalculateAimOffsetAlpha`, `GetThreadSafeAimOffsetAlpha` | 구현됨 | 유지 |
| `Get_AOValue` | `GetThreadSafeAimYaw`, `GetThreadSafeAimPitch` | 구현됨/부분 | yaw/pitch snapshot은 구현됨; GASP curve 세부 튜닝은 확인 필요 |
| `Get_AO_Yaw` | `GetThreadSafeAimYaw` | 구현됨 | 유지 |
| `CalculateRelativeAccelerationAmount` | `RelativeAccelerationAmount` 계산 | 구현됨 | 유지 |
| `Get_LeanAmount` | `LeanAmount` snapshot | 구현됨 | 유지 |
| `Get_FootPlacementPlantSettings` | `Get_FootPlacementPlantSettings` | 구현됨 | Stop/Default profile 설정 선택 |
| `Get_FootPlacementInterpolationSettings` | `Get_FootPlacementInterpolationSettings` | 구현됨 | Stop/Default profile 설정 선택 |

### Motion Matching

| GASP 함수 | Project_J 대응 | 상태 | 이식 우선도 / 판단 |
|---|---|---|---|
| `Update_MotionMatching` | Profile/AssetSet 선택 + proxy database binding | 구현됨 | 유지 |
| `Update_MotionMatching_PostSelection` | post-selection trace/cache | 부분 구현 | per-result Blend Profile override는 데이터 작업 필요 |
| `Get_MMBlendTime` | `FProject_JMotionMatchingSearchPolicy::ResolveBlendTime` | 구현됨 | 유지 |
| `Get_MMInterruptMode` | `ResolveDatabaseChangeInterruptMode` | 구현됨 | 유지 |
| `Get_MMNotifyRecencyTimeOut` | Profile `NotifyRecencyTimeOut` | 구현됨 | gait별 값은 footstep 검증 후 조정 |
| `Get_PoseHistoryReference` | ABP Pose History + native fallback diagnostics | 구현됨 | final Pose History는 ABP 하나만 소유 |

### Experimental State Machine

| GASP 함수/영역 | Project_J 대응 | 상태 | 이식 우선도 / 판단 |
|---|---|---|---|
| Movement Direction | `EProject_JStateControllerStrafeDirection` | 부분 구현 | Strafe 4방향만. OTM은 Forward 고정 |
| `Update_MovementDirection` | `ResolveStateControllerStrafeDirection` | 구현됨 | GASP 6방향/foot bias를 지원한다. Combat Strafe Chooser 행 저작은 별도다. |
| `Get_MovementDirectionThresholds` | -60/60/-120/120 Strafe sector 경계 | 부분 구현 | GASP는 OTM 또는 Sprint면 항상 F. Project_J도 OTM=Forward이고 Strafe 전용 sector만 사용한다. |
| Target Rotation | `DesiredFacingDeltaYaw` 및 Actor rotation | 부분 구현 | GASP와 동일한 값은 `TargetRotation - RootTransformRotation`이다. Project_J OTM Reface는 ORB 없이 입력 facing delta를 별도 selector로 사용해야 한다. |
| `Update_TargetRotation` | target rotation 갱신 없음 | 부분 구현 | GASP OTM은 Orientation Intent(캡슐 회전)를 Target으로 사용한다. Strafe yaw offset/Steering은 Combat 전용으로 보류한다. |
| `Get_StrafeYawRotationOffset` | 없음 | 의도적 보류 | ORB/rotation owner 결정 전 이식 금지 |
| Debug | MM CVar 및 dump commands | 구현됨 | `DumpMotionMatchingTrace`, `DumpMotionMatchingTransitionTrace`, `DumpMotionMatchingPivotTrace` |

### Anim Graph / State entry

| GASP 함수 | Project_J 대응 | 상태 | 이식 우선도 / 판단 |
|---|---|---|---|
| `SetBlendStackAnimFromChooser` | `EvaluateStateControllerAnimationChooserOnGameThread` + direct Blend Stack | 구현됨/차이 | 현재 최종 asset 하나만 1회 Motion Match; GASP의 multi-candidate MM은 후속 |
| `IsAnimationAlmostComplete` | `GetThreadSafeStateControllerSelectedAnimationAlmostComplete` | 구현됨 | ABP transition rule 연결 상태는 에디터 확인 필요 |
| `Get_DynamicPlayRate` | Min/Max play-rate profile 값만 존재 | 의도적 보류 | curve/warping 품질 작업; one-shot 종료 권한으로 사용 금지 |
| `OnStateEntry_IdleLoop` | logical state 및 idle chooser row | 부분 구현 | ABP state entry wiring 확인 필요 |
| `OnStateEntry_TransitionToIdle` | logical state 및 stop chooser row | 부분 구현 | ABP state entry wiring 확인 필요 |
| `OnStateEntry_LocomotionLoop` | logical state 및 loop chooser row | 부분 구현 | ABP state entry wiring 확인 필요 |
| `OnStateEntry_TransitionToLocomotion` | logical state 및 Start/Reface chooser row | 부분 구현 | GASP는 현재 Target Rotation을 저장하고 force blend한다. Project_J는 OTM Reface selector를 별도로 추가 |
| `OnUpdate_TransitionToLocomotion` | 없음 | 부분 구현/보류 | GASP는 저장 Target Rotation을 target으로 `RInterpTo(5.0)`하여 rotation-break 조건에 사용한다. OTM 1차에는 불필요, Combat Pivot 후속 |
| `OnStateEntry_InAirLoop` | logical state 존재, direct asset 행 없음 | 부분 구현 | regular MM fallback 유지 |
| `OnStateEntry_TransitionToInAir` | logical state 존재, direct asset 행 없음 | 부분 구현 | regular MM fallback 유지 |
| `OnStateEntry_IdleBreak` | profile opt-in/API만 존재 | 보류 | idle variation이 필요할 때만 |
| `OnStateEntry_SlideLoop` | 없음 | 제외 | slide gameplay/asset 범위 밖 |
| `OnStateEntry_TransitionToSlide` | 없음 | 제외 | slide gameplay/asset 범위 밖 |

### Steering / Event Graph

| GASP 함수 | Project_J 대응 | 상태 | 이식 우선도 / 판단 |
|---|---|---|---|
| `EnableSteering` | 없음 | 의도적 보류 | GASP root-motion steering은 Project_J의 no-root-motion locomotion 정책과 충돌 |
| `Get_DesiredFacing` | `DesiredFacingDeltaYaw`, future trajectory | 부분 구현 | Steering은 보류. OTM Reface에는 GASP의 root delta가 아닌 입력 facing delta를 사용 |
| `Blueprint Thread Safe Update Animation` | native snapshot/proxy update | 구현됨(구조 변경) | GASP EventGraph를 복사하지 않음 |
| `AllowFootPinning` | Foot Placement/Leg IK presentation path | 부분 구현 | GASP는 `OnGround && IsMoving`만 허용한다. Project_J의 현재 pin gate가 같은지는 ABP 에디터 확인 필요 |

## 3B. GASP AnimGraph 및 State Controller 구조

### GASP AnimGraph의 실제 역할 분리

GASP의 Experimental State Machine은 **포즈를 직접 출력하지 않는 논리 상태기계**다. 상태 진입 함수가 Chooser를 평가해 `Blend Stack Inputs`를 채우고, 실제 로코모션 포즈는 Blend Stack이 출력한다. 따라서 State Controller를 일반적인 "상태마다 애니메이션을 재생하는 State Machine"으로 옮기면 GASP의 설계 의도와 달라진다.

```text
GASP State Controller (논리)
  Grounded Conduit
    ├─ Transition to Idle -> Idle Loop -> (선택) Idle Break
    └─ Transition to Locomotion -> Locomotion Loop
         └─ Re-Enter: Start / Reface / Pivot 등을 다시 선택

  In Air
    Transition to In Air -> In Air Loop

각 OnStateEntry_* -> SetBlendStackAnimFromChooser
                         -> Chooser 결과 / Motion Match 결과를 Blend Stack에 입력
```

화면에 보인 GASP AnimGraph의 전체 포즈 흐름은 다음과 같다.

```text
일반 Motion Matching ─┐
                       ├─ 로코모션 소스 선택/전환 -> Additive Lean -> Aim Offset
State Controller       │                              -> Slot(DefaultSlot)
  + Blend Stack ───────┘                              -> Offset Root Bone
                                                      -> Remap Curves
                                                      -> Foot Placement -> Leg IK
                                                      -> Pose History -> Output Pose
```

### Project_J 대응 구조

```text
Native snapshot / locomotion state
  -> UProject_JCharacterAnimInstance (state-controller presentation state 결정)
  -> Chooser 평가 + 필요 시 entry-time Motion Match
  -> ABP_Humanoid_Master 논리 State Controller / Blend Stack
  -> 일반 Motion Matching과 Two Way Blend + Blend Poses by Bool
  -> Cached Locomotion Pose
  -> Upper-body/Slot, Aim Offset, Foot Placement, Leg IK
  -> Pose History -> Output Pose
```

Project_J는 일반 Motion Matching과 State Controller 출력을 먼저 `Two Way Blend`(현재 Alpha 1.0)로 합친 뒤, Direct Blend Stack의 유효 선택 여부로 `Blend Poses by Bool`을 사용한다. 현재 제공된 그래프에는 Inertialization 노드가 보이지 않으므로, 이를 현재 구조로 전제하지 않는다.

| 단계 | GASP | Project_J 상태 | Reface Start와의 관계 |
|---|---|---|---|
| 일반 Motion Matching | Motion Matching 노드 | 구현됨 | Direct State Controller가 못 고른 경우의 기본/복귀 경로 |
| 논리 State Machine | State Controller, 포즈 미출력 | 구현됨 | `TransitionToLocomotion`에 진입해 Reface를 선택할 기반 |
| Blend Stack | Chooser 결과를 실제 포즈로 출력 | 구현됨 | Reface 애셋을 재생하는 핵심 출력 경로 |
| Additive Lean / Aim Offset | BlendSpace + additive | 기반 구현됨 | Start 위에도 상체 보정을 적용할 수 있음 |
| Slot | Traversal/몽타주 주입 | 구현됨 | Reface 자체에는 필수 아님 |
| Offset Root Bone | GASP 실험적 root offset | 의도적으로 제외 | **Reface Start에 이식하지 않음**. Project_J 로코모션은 root motion/ORB 소유 구조가 아님 |
| Remap Curves | 커브 변환 | 확인 필요 | Reface 최초 이식의 선행 조건 아님 |
| Foot Placement / Leg IK | 지면 정렬/발 고정 | 구현됨 | Start 종료와 Loop 진입이 안정화된 뒤 품질 보강 역할 |
| Pose History | Trajectory와 과거 포즈 보관 | 구현됨 | entry-time MM 및 일반 MM의 공통 의존성 |

GASP 화면의 Offset Root Bone 설명에는 충돌 검사가 없어 지오메트리를 뚫을 수 있고 몽타주와 결합할 때 부작용이 있다는 주의가 있다. 현재 Project_J의 Reface Start는 루트 회전 보정이 아니라 Chooser의 방향 구간 선택과 일반 캐릭터 회전 정책으로 해결하는 편이 맞다.

### 상태 구조 차이와 해석

- GASP 화면상 Grounded는 `Transition to Idle`, `Idle Loop`, `Idle Break`, `Transition to Locomotion`, `Locomotion Loop`로 나뉜다. `Re-Enter`는 Start/Reface/Pivot처럼 같은 큰 상태 안에서 새 선택이 필요한 경우의 재평가 진입점이다.
- GASP 화면상 In Air는 `Transition to In Air -> In Air Loop`다.
- Project_J는 이 구조에 더해 `TransitionToLand`를 명시적으로 가진다. 이는 GASP와의 충돌이 아니라 Direct landing chooser 행을 지원하기 위한 Project_J 확장이다.
- Reface Start의 첫 구현 범위는 `Idle/Locomotion -> TransitionToLocomotion -> LocomotionLoop`만으로 충분하다. Idle Break, Pivot, Landing, In Air 재진입은 별도 단계다.

### 3C. Project_J 에디터 연결 확인값 (2026-08-02)

아래 항목은 제공된 Project_J 에디터 화면으로 확인했다. 이후 구현 판단은 이 값을 기준으로 한다.

#### 최상위 Chooser

`CHT_Player_StateControllerAnimations`는 다음처럼 분기한다.

| State Controller Presentation State | Rotation Mode | 하위 Chooser |
|---|---|---|
| `Transition to In Air`, `In Air Loop` | Any | `CHT_Player_InAir` |
| `Transition to Land` | Any | `CHT_Player_Land` |
| Any | `Orient to Movement` | `CHT_Player_OTM_Ground` |
| Any | `Strafe` | `CHT_Player_Strafe_Ground` |

따라서 OTM Reface Start의 실제 편집 대상은 `CHT_Player_OTM_Ground`다. C++ Profile의 `StateControllerAnimationChooserTable`도 이 최상위 Chooser를 가리켜야 한다.

#### 현재 OTM Ground 행

`CHT_Player_OTM_Ground`에는 다음 핵심 행이 이미 있다.

| Presentation State | Gait Intent | Animation Asset | 출력 |
|---|---|---|---|
| `Idle Loop` | Any | `M_Neutral_Stand_Idle_Loop` | `BlendTime=0.2` |
| `Transition to Locomotion` | Run | `M_Neutral_Run_Start_F_Lfoot` | `StartTime=0.1`, `UseMotionMatch=true` |
| `Transition to Locomotion` | Sprint | `M_Neutral_Sprint_Start_F_Lfoot` | `StartTime=0.1`, `UseMotionMatch=true` |
| `Transition to Idle` | Run | `M_Neutral_Run_Stop_F_Lfoot` | `StartTime=0.6`, `UseMotionMatch=true` |

Run/Sprint용 Pose Search Database에는 이미 다음 Reface Start 애셋들이 포함되어 있다.

```text
Run
  M_Neutral_Run_Reface_Start_F_L_090
  M_Neutral_Run_Reface_Start_F_L_180
  M_Neutral_Run_Reface_Start_F_R_090
  M_Neutral_Run_Reface_Start_F_R_180

Sprint
  M_Neutral_Sprint_Reface_Start_F_L_090
  M_Neutral_Sprint_Reface_Start_F_L_180
  M_Neutral_Sprint_Reface_Start_F_R_090
  M_Neutral_Sprint_Reface_Start_F_R_180
```

즉, 필요한 애셋과 PSD 등록은 완료되어 있다. Reface 구현에 남은 핵심은 **Chooser가 현재 Forward Start 대신 어느 Reface asset을 선택할지 판단할 selector/열을 추가하는 것**이다. C++ 입력값 제공은 2026-08-02에 완료했다.

`UProject_JCharacterAnimInstance`는 game-thread Chooser 컬럼으로 아래 reflected property를 제공한다.

```text
StateControllerInputFacingDeltaYawForChooser
  = DeltaAngle(CharacterActorYaw, MoveWorldDirectionYaw)
  negative: Left, positive: Right
```

이 값은 regular Motion Matching Chooser의 갱신 주기와 분리되어 매 native update마다 갱신된다. State Controller의 선택 캐시 키에는 의도적으로 넣지 않았다. 즉 `Transition to Locomotion` 진입 시에는 최신 입력으로 한 번 선택하고, 같은 Start 재생 중 매 프레임 Reface 애셋을 바꾸지 않는다.

Stop 행은 Start/Reface의 입력 방향값을 사용하지 않는다. C++는 `Transition to Idle` 진입 순간에 아래 두 game-thread Chooser property를 고정한다.

```text
StateControllerStopVelocityDeltaYawForChooser
  = DeltaAngle(CharacterActorYaw, HorizontalVelocityYaw at Stop entry)
  negative: Forward-Left, positive: Forward-Right

StateControllerStopFootForChooser
  = StateControllerOneShotFootForChooser (Stop-only compatibility mirror)
```

속도 방향은 Stop 재생 중 계속 감속해도 바뀌지 않도록 진입 순간에 latch한다. `StateControllerStopFootForChooser`는 이전 Stop-only table과의 호환용 미러다. 새 Chooser 열과 기존 OTM/InAir의 모든 직접 one-shot 행은 **반드시** `StateControllerOneShotFootForChooser`를 사용한다. Start/Jump/Fall/Land에서 StopFoot를 쓰면 해당 값은 갱신되지 않아 이전 Stop 값 또는 기본값으로 잘못 선택된다.

단발성 Start / Stop / Jump / Land는 `NativePostEvaluateAnimation`에서 이전 최종 포즈의
`contact_l`, `contact_r` curve를 읽고, 다음 update에서 상태 진입 시
`StateControllerOneShotFootForChooser`에 한 번만 latch한다. 값은 다음과 같다.

| 값 | 의미 | Chooser 행 용도 |
|---|---|---|
| `Left` | `contact_l`가 더 낮아 Left가 현재 swing/airborne foot으로 판정됨 | `_Lfoot` 에셋 행 |
| `Right` | `contact_r`가 더 낮아 Right가 현재 swing/airborne foot으로 판정됨 | `_Rfoot` 에셋 행 |
| `None` | 진단/명시적 opt-out 값 | 일반 에셋 fallback에는 사용하지 않음 |

`_Lfoot`/`_Rfoot`가 각각 해당 발부터 동작을 시작한다는 현재 네이밍을 전제로, 낮은 contact(이미 들린 발)를 선택한다. 이 매핑은 PIE에서 로그의 contact 값과 실제 재생을 함께 확인해야 한다. 반대로 보이면 resolver의 Left/Right 반환만 뒤집으면 된다. `StateControllerStopFootForChooser`는 기존 Stop child chooser와 호환되도록 같은 값을 미러링한다.

`p.ProjectJ.MMTransitionDebug 1` 상태에서는 one-shot 진입마다
`StateControllerFootLatch` 로그도 출력한다. `Reason` 값은 순서대로
`0=MissingContactCurve`, `1=BothFeetUnplanted`, `2=ContactsTooSimilar`,
`3=LeftFootLowerContact`, `4=RightFootLowerContact`,
`5=PhaseHistoryFallback`, `6=DefaultFootFallback`다.

Project_J는 매 final-pose evaluation에서 contact 차이가 충분히 큰 마지막
Left/Right 결과를 phase cache로 유지한다. Stop/Fall/Land 진입 시에는 0/0,
1/1 또는 curve 누락으로 live contact가 모호해도 이 cache를 사용한다. Idle
Loop에 도달하면 cache를 비운다. Idle에서 시작하는 새 Start/Jump에는 오래된
보행 발을 재사용하지 않고, `StateControllerNoPhaseFootFallback`(기본 `Left`)을
사용한다. 이 기본값은 ABP class defaults에서 변경할 수 있다.

발 구분이 없는 에셋은 `Foot=None` 행으로 만들지 않는다. Foot 열을 `Any`로
두고 L/R 행보다 아래의 마지막 fallback 행으로 둔다. 그러면 L/R이 판정된
경우에는 전용 행이 우선되고, 어떤 발 값이 와도 발 중립 에셋은 안전한 마지막
fallback이 된다. `StateControllerFootLatch` 로그의 `PhaseCache`,
`AllowPhaseCache`, `DefaultFoot`로 선택 근거를 확인한다.

`Reface_Start_F_L_090`, `Reface_Start_F_R_090` 등의 `L`/`R`은 foot variant가 아니라 입력/회전 방향이다. 이 행들은 이미 `StateControllerInputFacingDeltaYawForChooser`로 구분되므로 Foot 열을 `Any`로 둔다. 실제 이름에 `_Lfoot` 또는 `_Rfoot`가 있는 행만 `Left`/`Right`를 사용한다.

`TransitionToInAir`는 Jump Start와 Fall Off가 같은 presentation state를 공유한다. 따라서 `CHT_Player_InAir`에는 `bStateControllerFallOffForChooser` bool column이 필요하다. 일반 Jump Start 행은 `False`, `*_Off_*` Fall Off 행은 `True`로 둔다. 이 값은 state 진입 시 latch되어 Jump/Fall Off 행이 행 순서로 서로를 가로채지 않는다. 또한 CharacterMovement의 `Falling` 감지가 locomotion-state component의 `bIsFallOffStart` 게시보다 한 animation update 빠를 수 있으므로, native State Controller는 **직전 presentation state가 공중 상태가 아니고 Jump/Land도 아닌 첫 공중 프레임**을 Fall Off로 보정한다. 이 보정으로 `InAirLoop`의 `M_Neutral_Jump_Loop_Fall`이 한 프레임 먼저 섞였다가 `*_Off_*` one-shot으로 바뀌는 현상을 막는다.

Fall Off one-shot은 chooser의 `StartTime` 이후 남은 전체 길이를 무조건 hold하지 않는다. `ExperimentalFallOffMaxHoldTime`(기본 `0.65s`)까지만 재생하고 `InAirLoop`으로 넘긴다. `*_Off_*` 원본에 이미 `LocomotionEarlyTransition` NotifyState가 있으면 그 window가 더 이른 탈출을 허용한다. 이 값은 Profile에서 조정 가능하며, 다른 Start/Stop/Land one-shot에는 적용되지 않는다.

Start와 Land 진입 시에는 local player의 Control Yaw를 저장한다. 해당 one-shot 중 마우스 회전이 `15°` 이상이면 전용 Turn asset을 억지로 재생하지 않고 one-shot을 즉시 해제해 regular MM의 Run/Sprint Cycle PSD를 재검색한다. 현재 OTM Turn chooser/asset 계약이 없어서 Cycle 경로가 안정적이며, OTM Turn을 도입할 때에만 이 지점을 전용 Turn state로 교체한다.

Start Gait는 Stop Gait와 별도로 관리한다. Start 진입 뒤 `0.15s` 동안에는 Shift의 짧은 탭/해제를 반영해 Run 또는 Sprint Start를 정정할 수 있다. 그 뒤 Gait가 바뀌면 `Sprint_Start`를 `Run_Start`로 중간 교체하지 않고, one-shot을 즉시 해제해 현재 Gait의 trajectory-aware Cycle PSD로 전환한다. 즉 Sprint Start 재생 중 Shift를 놓으면 Run 속도에 Sprint 모션을 계속 적용하지 않는다.

추가로 `TransitionToIdle`에서는 이동 입력을 놓는 즉시 locomotion Gait가 `Walk`으로 되돌아갈 수 있다. Stop 애셋은 시작 Gait를 유지해야 하므로, State Controller Chooser는 Stop transition 동안 `bStopWasSprinting`으로 Run/Sprint Gait를 고정한다. 그렇지 않으면 Stop을 정상 선택한 직후 Walk 조건으로 재평가되어 `Asset=None`이 되는 문제가 발생한다.

OTM에서는 capsule이 수평 속도 방향을 빠르게 향하므로 `StateControllerStopVelocityDeltaYawForChooser`가 대부분 0° 근처가 되는 것이 정상이다. 이 경우 F Stop이 선택되고 FL/FR Stop 행은 거의 선택되지 않는다. OTM에서 의도적으로 FL/FR Stop까지 사용하려면, 현재 actor yaw가 아닌 **입력 해제 직전의 actor yaw/이동 방향**을 별도 latch하는 selector 설계가 필요하다. 이는 현재 Stop 안정화 범위와 분리된 후속 작업이다.

Start one-shot 재생 중 로컬 마우스(Control Rotation)가 시작 시점에서 15° 이상 변하면, Project_J는 Direct Start Blend Stack을 즉시 해제하고 regular Motion Matching의 presentation phase를 `Cycle`로 강제한다. 따라서 새 trajectory로 `PSD_Cycle` 검색을 같은 update에 수행한다. 이 정책은 local OTM 입력에만 적용하며, remote proxy의 보간된 회전은 Start를 취소하지 않는다.

GASP `MovementDirectionBias` 확인값: 런타임 Set 노드가 없는 `E Movement Direction Bias` enum이며, ABP 기본값은 `LeftFootForward`다. 이는 Strafe의 `LL/LR`, `RL/RR` movement-direction enum을 고르는 정적 선호값이다. OTM/Sprint는 GASP에서도 movement direction을 F로 고정하므로, 이 값을 Project_J OTM의 Lfoot/Rfoot one-shot selector로 그대로 이식하지 않는다.

GASP `Update_MovementDirection`의 확정 흐름은 다음과 같다.

```text
1. Movement Direction Last Frame = 현재 Movement Direction (캐시)
2. Movement State가 Moving일 때만 갱신
3. 미래 Trajectory Velocity와 capsule/Orientation Intent로 signed Direction 계산
4. 디버그용 Direction Thresholds 갱신
5. RotationMode=OrientToMovement 또는 Gait=Sprint -> Movement Direction = F
6. Strafe만 threshold quadrant를 사용
   - Forward range -> F
   - Left range -> MovementDirectionBias로 LL 또는 LR
   - Right range -> MovementDirectionBias로 RL 또는 RR
   - Back range -> B
```

따라서 GASP Bias의 정확한 이식 대상은 Project_J의 미래 **Strafe 6-direction animation set**이다. 현재 Project_J의 OTM one-shot 발 선택은 이 규칙과 별개로 유지한다.

에디터에서는 `CHT_Player_OTM_Ground`에 `StateControllerInputFacingDeltaYawForChooser` float column을 추가하고, 기존 Run/Sprint `Transition to Locomotion` Forward Start 행보다 앞에 아래 네 Reface 행을 둔다. 시작 구간 `-45~45`는 기존 Forward Start가 담당한다.

| Gait | 입력 yaw 구간 | Animation Asset |
|---|---:|---|
| Run | -180~-135 | `M_Neutral_Run_Reface_Start_F_L_180` |
| Run | -135~-45 | `M_Neutral_Run_Reface_Start_F_L_090` |
| Run | 45~135 | `M_Neutral_Run_Reface_Start_F_R_090` |
| Run | 135~180 | `M_Neutral_Run_Reface_Start_F_R_180` |
| Sprint | -180~-135 | `M_Neutral_Sprint_Reface_Start_F_L_180` |
| Sprint | -135~-45 | `M_Neutral_Sprint_Reface_Start_F_L_090` |
| Sprint | 45~135 | `M_Neutral_Sprint_Reface_Start_F_R_090` |
| Sprint | 135~180 | `M_Neutral_Sprint_Reface_Start_F_R_180` |

각 Reface 행의 output은 기존 해당 Gait의 Forward Start와 동일하게 `StartTime=0.1`, `UseMotionMatch=true`로 시작하는 것이 안전한 초기값이다. 필요하면 실제 동작 확인 후 asset별 StartTime/BlendTime만 조정한다.

#### 실제 ABP State Controller

현재 State Machine에는 아래 여섯 상태가 있고, Entry는 `Idle Loop`다.

```text
Idle Loop
Transition To Locomotion
Locomotion Loop
Transition To Idle
Transition To In Air
In Air Loop
```

`Transition To Locomotion` 상태는 포즈 노드 없이 `OnStateEntry_TransitionToLocomotion`만 연결된 논리 상태다. 이 점은 GASP의 logical State Controller 방식과 일치한다. 직접적인 Landing state는 이 State Machine 화면에는 없지만, Chooser 최상위에는 `Transition to Land -> CHT_Player_Land` 분기가 존재한다. 따라서 landing presentation state를 어느 상태에서 요청하는지는 별도로 유지되는 현재 프로젝트 설계다.

#### 실제 ABP AnimGraph의 로코모션 출력선

```text
StateController (logical) ─┐
                            ├─ Two Way Blend (Alpha 1.0) ─┐
regular Motion Matching ───┘                                ├─ Blend Poses by Bool
                                                             │  Active = Has Selected Animation
Direct Blend Stack <────────────────────────────────────────┘
  Selected Animation / StartTime / Loop / BlendTime / BlendProfile
  -> Locomotion cached pose
  -> CombatUpperBody linked layer -> ResolvedLocomotion cache
  -> Upper-body Slot + Layered Blend per Bone
  -> Aim Offset additive
  -> Local To Component -> Foot Placement -> Leg IK -> Component To Local
  -> Pose History (`PoseHistory`, Trajectory 연결)
  -> Blend Poses (Locomotion Mode: Default/Mounted) -> Output Pose
```

`PoseHistory` 태그/이름은 정확히 `PoseHistory`다. native entry-time Motion Match가 찾는 이름과 일치하므로, 이 부분의 별도 수정은 필요 없다.

## 4. 현재 State Controller Chooser 계약

현재 Profile의 `StateControllerAnimationChooserTable`은 game thread에서 `UProject_JCharacterAnimInstance`를 input object로 평가한다. 코드가 평가 전에 반영하는 핵심 selector는 다음과 같다.

```text
StateControllerPresentationStateForChooser
RotationModeForChooser
GaitIntentForChooser
StateControllerStanceForChooser
StateControllerStrafeDirectionForChooser
bCombatModeForChooser
```

직접 결과는 최종적으로 `UAnimationAsset`이어야 한다. `FProject_JStateControllerChooserOutput`은 asset이 아니라 그 asset의 재생 메타데이터다.

GASP의 `Target Rotation Delta`는 아래처럼 **visual root 기준** 값이다.

```text
TargetRotationDelta = DeltaRotator(TargetRotation, RootTransformRotation).Yaw
```

GASP OTM에서 `TargetRotation = OrientationIntent`이며, 이는 캡슐 회전과 같아야 한다. Strafe에서는 여기에 `Get_StrafeYawRotationOffset`을 더한다. `OnStateEntry_TransitionToLocomotion`은 이 Target Rotation을 저장하고 강제 Blend를 시작하며, `OnUpdate_TransitionToLocomotion`은 저장값을 현재 Target Rotation으로 `RInterpTo`(speed 5.0)해 rotation-start/pivot break 조건에 사용한다.

Project_J는 Offset Root Bone을 새 locomotion 회전 소유자로 쓰지 않으므로, 위 값과 동일한 `RootTransformRotation`을 만들지 않는다. 따라서 OTM Reface Start의 안전한 이식은 GASP 이름을 재사용하지 않고 다음 selector를 새로 정의하는 방식이다.

```text
StateControllerInputFacingDeltaYawForChooser
  = LocomotionState.KinematicContext.DesiredFacingDeltaYaw
```

이 값은 Reface asset을 고르기 위한 **Project_J OTM 전용 적응값**이다. 부호가 보존되므로 `-`는 Left, `+`는 Right로 사용한다. GASP의 정지/저속 Run Reface 범위는 다음과 같다.

| Speed 2D | Target Rotation Delta | GASP asset |
|---:|---:|---|
| 0~200 | -45~0 | `M_Neutral_Run_Start_F_Lfoot` |
| 0~200 | 0~45 | `M_Neutral_Run_Start_F_Rfoot` |
| 0~200 | -135~-45 | `M_Neutral_Run_Reface_Start_F_L_090` |
| 0~200 | -180~-135 | `M_Neutral_Run_Reface_Start_F_L_180` |
| 0~200 | 45~135 | `M_Neutral_Run_Reface_Start_F_R_090` |
| 0~200 | 135~180 | `M_Neutral_Run_Reface_Start_F_R_180` |

이 행들은 `Transition to Locomotion`, `IsPivoting=False`, `PlayMovingLand=False`, `JustTraversed=False`, `Tags=(Start)` 조건을 사용한다. 일반 Start의 `bUseMotionMatch=True`는 유지할 수 있으나, 현재 Project_J 구현은 하나의 최종 asset에 대해서만 entry Motion Match를 한다.

Sprint도 확인됐다. 저속 Start는 `Speed 2D 0~210`, `Target Rotation Delta -45~45`의 `M_Neutral_Sprint_Start_F_Lfoot`이며, Reface Start는 `Speed 2D 0~300`의 `-135~-45`, `-180~-135`, `45~135`, `135~180` 범위를 사용한다. 고속 Sprint Reface는 `Speed 2D 250~500` 및 `Movement Direction Last Frame` 조건이 추가된다.

## 5. OTM Reface Start 작업 전 확인이 필요한 GASP 자료

아래는 구현 유무를 확정하거나 범위를 정확히 맞추기 위해 필요한 캡처다. 번호 순서대로 받으면 된다.

다음 자료는 수신·확인했다.

- `Update_TargetRotation`: `TargetRotation - RootTransformRotation` delta, OTM/Strafe 분기.
- `OnStateEntry_TransitionToLocomotion`: Target Rotation cache, force blend.
- `OnUpdate_TransitionToLocomotion`: `RInterpTo` speed 5.0 cache update.
- `AllowFootPinning`: `OnGround && IsMoving`.
- Run/Sprint chooser의 저속·고속 Reface Start 행.

`Update_MovementDirection`도 확인됐다. GASP는 매 frame 직전 `Movement Direction`을 `Movement Direction Last Frame`으로 저장하되, 실제 방향 값은 Moving일 때만 갱신한다. OTM 또는 Sprint는 현재 방향을 항상 `F`로 강제하고, Strafe만 threshold 및 foot-forward bias로 F/B/LL/LR/RL/RR을 선택한다.

따라서 고속 Reface 행의 `Movement Direction Last Frame`은 **Strafe 방향 전환**을 위한 조건이다. Project_J의 현재 OTM Reface Start에는 새 last-direction 상태를 추가할 필요가 없다.

Reface Start asset의 Notify Track도 현재 작업의 필수 정보가 아니다. `UProject_JAnimNotifyState_LocomotionEarlyTransition`은 이미 구현되어 있으나, 현재 asset에 배치되지 않았고 배치하지 않아도 selected non-loop asset의 종료 시 `IsAnimationAlmostComplete` 경로로 loop로 넘어간다. `PoseSearchBranchIn` 역시 Project_J의 선택 asset 1개에 대한 entry Motion Match가 실패하면 해당 asset의 authored `StartTime`으로 재생하는 구조이므로 필수 조건이 아니다.

다음 자료는 이번 OTM Reface Start에는 받지 않아도 된다. Combat Strafe 작업을 시작할 때 요청한다.

- `IsPivoting`의 세부 노드와 Pivot retry
- rotation-break 및 target rotation interpolation
- Strafe F/B/LL/LR/RL/RR chooser rows
- Steering, Offset Root Bone, TIP graph

## 6. 권장 구현 순서

1. Run/Sprint Reface Start: `StateControllerInputFacingDeltaYawForChooser`를 Chooser 열로 사용한다. 이는 GASP Root Delta와 의도적으로 구분한다.
2. PIE에서 0°, 좌/우 90°, 좌/우 180°의 첫 입력을 검증한다. 소유 클라이언트와 simulated proxy를 각각 확인한다.
3. 같은 selector 계약을 Sprint 저속 Reface에 적용한다.
4. `Speed >= 200` 고속 reface/turn redirect는 Combat Strafe 전용 후속 작업으로 분리한다. OTM/Sprint의 GASP direction은 F로 고정된다.
5. Combat Strafe Pivot은 OTM Reface 완료 뒤 독립 트랙으로 다룬다.

## 7. 관련 Project_J 코드

- `Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h`
- `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp`
- `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstanceProxy.cpp`
- `Source/Project_JCharacter/Public/Project_JLocomotionAnimStateComponent.h`
- `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent.cpp`
- `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentGround.cpp`
- `Source/Project_JCharacter/Public/Animation/Project_JLocomotionProfile.h`
- `Docs/MotionMatching_StateController_Handoff_2026-08-01.md`
- `Docs/MotionMatching_Locomotion_Refactor.md`

### Native Strafe bias implementation

`StateControllerMovementDirectionBias` is the native equivalent of GASP's static
`Movement Direction Bias`. It defaults to `LeftFootForward`; its only effect is
the Strafe side-sector result exposed to the chooser as
`StateControllerStrafeDirectionForChooser`.

| Input direction relative to capsule | LeftFootForward | RightFootForward |
|---|---|---|
| -45 to 45 | `Forward` | `Forward` |
| -135 to -45 | `LeftLeftFootForward` (`LL`) | `LeftRightFootForward` (`LR`) |
| 45 to 135 | `RightLeftFootForward` (`RL`) | `RightRightFootForward` (`RR`) |
| remaining back sector | `Backward` | `Backward` |

The Strafe sub-chooser should use this single enum column. It does not need a
second foot selector. OTM remains deliberately separate and is unaffected by
the bias.

### Project_J integration decision

Do not replace `StateControllerInputFacingDeltaYawForChooser` with a GASP
`Target Rotation Delta` value at the current project stage. Project_J derives
the former from its locomotion state kinematic context and uses it directly for
OTM Start and Reface selection. GASP's value is visual-root based and is useful
when Root Offset, Steering, and the associated rotation pipeline are active;
those systems are not part of this OTM path yet.

Project_J already exposes landing snapshots for light/heavy and
stand/run/sprint chooser selection. No new Land C++ selector is required until
the direct `CHT_Player_Land` rows need a condition that the current snapshots
cannot express.

### GASP landing facts

The landing logic is independent from Movement Direction Bias and does not
supply a shared L/R foot selector. Light and heavy land are selected from
JustLanded plus the absolute vertical land velocity against Heavy Land Speed
Threshold. Play Land is the In Air to On Ground transition. Play Moving Land
adds an absolute trajectory-turn-angle limit of 120 degrees.

The In Air parent chooser selects Jumps F, B, LL-or-LR, or RL-or-RR by
Movement Direction. The Jumps F child then uses vertical velocity, Speed 2D,
and Time to Land. Its displayed Lfoot/Rfoot rows must additionally use the
native `StateControllerOneShotFootForChooser` column: `Left` for `_Lfoot`,
`Right` for `_Rfoot`. Foot-neutral assets use an `Any` row placed after the
specific L/R rows.
This avoids duplicate matches while deriving the choice from the preceding
evaluated locomotion pose rather than an alternating synthetic counter.

GASP Run Start F instead distinguishes Lfoot/Rfoot with Target Rotation Delta
from negative 45 to 0 versus 0 to positive 45. This is a state-specific
chooser condition, not gait phase data and not Movement Direction Bias.

### Current Project_J policy precedence

Where an earlier historical note in this document says to add a GASP
`Target Rotation Delta` selector to OTM Reface, the current decision above
takes precedence: use `StateControllerInputFacingDeltaYawForChooser` until
Project_J explicitly adopts Root Offset, Steering, and a visual-root rotation
owner. Likewise, any earlier four-direction Strafe description is historical;
the current native contract is F/B/LL/LR/RL/RR with static bias.
