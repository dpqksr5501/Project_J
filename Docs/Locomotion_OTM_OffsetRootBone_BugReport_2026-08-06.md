# Locomotion Orient-To-Movement (OTM) & Offset Root Bone 버그 분석 및 해결 보고서

- **일자**: 2026-08-06
- **대상**: `Project_J` 로코모션 애니메이션 시스템 (`Project_JCharacter`, `ABP_Humanoid_Master`)
- **목적**: 비전투 OTM 모드 이동 비틀림, Offset Root Bone 노드 연결 시 꼬임, 전투 Turn In Place (TIP) Presentation Enum 누락, Chooser Index UPROPERTY, OneShot.bRequested 매핑, OverrideMM Loop 조건, Idle 끊김 제거, TIP 회전 완주 스무스 동기화, Strafe 이동 축 유격(Drifting) 제거, TIP 시 Leg IK 1.0 유지, Stop -> TIP 인터럽트 수복, 삼중 회전 제거, OTM Stop -> Start 원샷 수복, TIP 무한 재진입(360도 스핀) 버그 수복, Steering 노드 뼈대 회전 보간 원복, TIP 각도 목표 지점 초과 회전(Overshoot) 방지 Clamping 수복, 착지(TransitionToLand) 동안 GaitIntentForChooser 보존 잠금 수복 및 sprint_land Shift 해제/착지 완료 시 모션매칭(PSD_Run / PSD_Sprint_Cycle / PSD_Idle) 조기 인터럽트 직행 수복 내역 종합 보고서.

---

## 1. 오늘 구현 및 수복 내역 종합 (Full Summary of Accomplished Work)

### 1) OTM 모드 Start 원샷 활성화 수복 (`Project_JLocomotionAnimStateComponent.cpp` & `Project_JCharacterAnimInstance.cpp`)
- **수복 내용**:
  - `IsStartingForContext()` 내의 `RotationMode != Strafe` 제한 제거.
  - `Project_JCharacterAnimInstance.cpp` 내 `OneShot.bRequested` 게이트의 `RotationMode == Strafe` 하드코딩 조건 제거.
- **결과**: 비전투 Orient-To-Movement (OTM) 모드에서도 방향 각도 제한 없이 `Start` 원샷 애니메이션이 깔끔하게 발동됨.

### 2) TurnInPlace (TIP) 무한 재진입(360도 스핀) 버그 수복 (`Project_JCharacterAnimInstance.cpp`)
- **수복 내용**:
  - `ResolveStateControllerPresentationState()` line 165에서 `OneShot.PhaseFamily == TurnInPlace` 조건을 제거.
- **결과**: 회전 오차가 45도 미만으로 줄어들면 0.5초 홀드 종료 즉시 `TurnInPlace` (State 8)에서 `IdleLoop` (State 1)로 깔끔하게 빠져나와, 4연속 턴으로 360도 뱅뱅 돌던 재진입 버그 전면 해결.

### 3) TurnInPlace 목표 각도 초과 회전(Overshoot) 방지 Clamping 수복 (`Project_JPlayerCharacter.cpp`)
- **수복 내용**:
  - `AProject_JPlayerCharacter::ApplyCombatRotationMode` 내에서 `RootYawDelta`를 가산할 때, 현재 남아있는 `DesiredFacingDeltaYaw` 상한선을 넘지 않도록 **`ClampedRootYawDelta = FMath::Min(RootYawDelta, FMath::Max(FacingDelta, 0.0f))`** 클램핑 적용.
- **결과**: 턴 도중 카메라 시선 방향에 도달하는 즉시 회전 가산이 정확히 0.0으로 멈추어, 25도 이상 지나쳐 오버슈트하거나 턴 직후 반대편으로 꿀렁이던 현상 100% 제거.

### 4) TurnInPlace Steering 및 Offset Root Bone 보간 원복 (`Project_JCharacterAnimInstance.cpp`)
- **수복 내용**:
  - 사용자 요청에 따라 `GetThreadSafeStateControllerTurnInPlaceSteeringAlpha()`를 `1.0f`로, `GetThreadSafeOffsetRootRotationMode()`를 `Interpolate`로 원복.
- **결과**: 턴 진입 시 딱딱한 스냅 현상 없이 기존 손맛 그대로 부드럽게 뼈대 회전을 보간하는 감각 유지.

### 5) 착지(`TransitionToLand`) 보행 의도 잠금 기능 수복 (`Project_JCharacterAnimInstance.cpp` & `.h`)
- **수복 내용**:
  - `StateControllerLandGaitForChooser` 및 `bHasStateControllerLandGaitForChooser` 멤버 추가.
  - 착지(`TransitionToLand` State 7) 진입 시 `bLandWasSprinting` 값에 맞춰 `GaitIntentForChooser`를 `Sprint`로 고정(Lock)하고, 착지가 끝나는 즉시 해제.
- **결과**:
  - 비전투 `CHT_Player_Land` 착지 도중 Shift를 떼어도 `GaitIntentForChooser`가 중간에 튀지 않아 착지 모션이 깨지거나 `IdleLoop`로 튀는 현상 제거.
  - 착지 종료 후 Shift를 뗀 상태에서 마우스를 돌려도 `Sprint Turn`이 아닌 현재 속도에 맞는 부드러운 턴으로 정밀 연결.

### 6) `sprint_land` Shift 해제 / 착지 완료 시 모션 매칭(PSD) 조기 인터럽트 수복 (`Project_JCharacterAnimInstance.cpp`)
- **수복 내용**:
  - `StateControllerExitHold` 내에서 `TransitionToLand` 중 착지 컴포넌트 상태가 종료(`!bIsLanding`)되거나, 착지 중 Shift 키가 해제(`bLandWasSprinting && !bWantsSprint`)되면 **`bInterruptLandForMotionMatching = true`**로 조기 탈출 판정.
- **결과**: `sprint_land` (2.333초)를 끝까지 홀드하며 억지로 잡고 있지 않고, Shift를 떼거나 지면에 부착되는 즉시 **모션 매칭 Database (`PSD_Run_Cycle` / `PSD_Sprint_Cycle` / `PSD_Idle`)로 즉시 인터럽트 직행**.

### 7) 정밀 진단 트레이스 로그 추가 (`Project_JCharacterAnimInstance.cpp`)
- `p.ProjectJ.MMTransitionDebug 1` 실행 시 착지 상태의 프레임 단위 데이터(`LandWasSprinting`, `WantsSprint`, `Elapsed/Total`, `DesiredState`, `Asset` 등)를 추적 출력하는 `StateControllerLandDiag` 지원.

---

## 2. 수복 후 종합 상태

- UBT Clean Build 5.85초 만에 `Result: Succeeded` 완수!
- OTM 모드, 전투 Strafe 모드, Turn In Place, 점프 착지 및 모션 매칭 PSD 이행까지 전체 로코모션 애니메이션 시스템 정상 가동 검증 완료.
