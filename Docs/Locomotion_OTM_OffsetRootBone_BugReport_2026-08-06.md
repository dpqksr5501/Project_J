# Locomotion Orient-To-Movement (OTM) & Offset Root Bone 버그 분석 및 해결 보고서

- **일자**: 2026-08-06
- **대상**: `Project_J` 로코모션 애니메이션 시스템 (`Project_JCharacter`, `ABP_Humanoid_Master`)
- **목적**: 비전투 OTM 모드 이동 비틀림 버그의 원인 및 최종 해결 과정을 기록하여 **Codex GPT 5.6** 등 후속 작업자가 원활하게 이어받을 수 있도록 핸드오프 문서 제공.

---

## 1. 버그 현상 요약 (Issue Summary)

1. **비전투 OTM(Orient to Movement) 회전 이상**:
   - 비전투 상태(`Combat = false`, `bOrientRotationToMovement = true`)에서 WASD 이동 입력을 넣을 때, 캐릭터가 입력 방향으로 즉시 회전하지 않고 이상한 곳(정면 또는 반대 방향)을 바라본 채 미끄러지듯 이동함.
2. **180도 반대 방향(S키 등) 입력 시 비틀림**:
   - 뒤쪽으로 이동을 시도할 때 캐릭터 캡슐과 메쉬가 회전하지 못하고 제자리에서 꼬이거나 미끄러짐.

---

## 2. 근본 원인 분석 (Root Cause Analysis)

원래 정상 작동하던 구조에서 **"캡슐 회전 강제 조작 C++ 코드 추가"** 및 **"AnimGraph의 Offset Root Bone 노드 오프셋 락"**이 복합적으로 얽히면서 발생했습니다.

### ① 캡슐 회전 강제 코드 삽입 (`SetActorRotation` 등)
- **과거 시도**: Turn In Place(TIP) 회전을 맞추기 위해 C++ `Tick`이나 `UpdateTurnInPlaceActorRotation`에서 캡슐 회전(`SetActorRotation`)을 강제로 꺾는 코드가 추가되었음.
- **부작용**: 언리얼 엔진 표준 `CharacterMovementComponent`의 `bOrientRotationToMovement` 회전 업데이트 주기와 C++의 물리적 `SetActorRotation`이 충돌하여, 메쉬와 캡슐의 Relative Yaw가 비동기화됨.

### ② AnimGraph `Offset Root Bone` 노드의 루트 회전 오프셋 락 (Offset Hold)
- **원리**: `Offset Root Bone` 노드는 TIP 모션 등에서 메쉬 루트 회전을 절차적으로 오프셋(Hold/Interpolate)시키는 애니메이션 노드임.
- **부작용**: 비전투 OTM 모드에서도 `Offset Root Bone`이 활성화되어 메쉬 회전 오프셋을 계속 유지(`Interpolated` 모드)하는 바람에, `CharacterMovementComponent`가 캡슐을 회전시켜도 메쉬 포즈가 이전 오프셋으로 고정되어 캡슐 회전을 시각적으로 방해함.

### ③ Chooser 테이블의 비전투 Start / Reface 180 원샷 모션 간섭
- **원리**: C++의 `IsStartingForContext`가 비전투 OTM 이동 진입 시에도 `true`를 반환함.
- **부작용**:
  - 전진 입력 시: `M_Neutral_Run_Start_F_Lfoot` (정면 전진 스타트 원샷)
  - 180도 입력 시: `M_Neutral_Run_Reface_Start_F_R_180` (180도 리페이스 스타트 원샷)
  - 위 fixed-direction 원샷 애니메이션이 재생되는 1초 내외 동안 애니메이션 포즈의 루트 모션과 캡슐의 OTM 회전이 서로 싸우면서(fight) 캐릭터 회전이 잠김.

---

## 3. 최종 해결 및 복구 내역 (Fix & Restoration)

### A. C++ 코드 복구 및 교정 (`Source/Project_JCharacter/`)

1. **캡슐 강제 회전 전면 제거 (Capsule Untouched Rule)**:
   - C++ `Tick` 및 별도 함수에서 캡슐 회전(`SetActorRotation`)을 직접 조작하는 코드 전면 폐지.
   - 캡슐 이동/회전은 언리얼 표준 `CharacterMovementComponent` (`bOrientRotationToMovement`)에 100% 전담.

2. **9개 ThreadSafe Getters & Event 함수 완벽 복구 (`Project_JCharacterAnimInstance`)**:
   AnimGraph(`ABP_Humanoid_Master`) 및 State Controller의 Red `ERROR!` 노드를 해제하고 Offset Root Bone / Steering 연동을 지원하기 위한 9개 C++ 함수 명세 및 내부 동작:
   
   - `GetThreadSafeOffsetRootRotationMode()`: `PhaseFamily == TurnInPlace` 일 때만 `Interpolated`(2) 반환, 그 외 Locomotion / Idle / OTM 모드에서는 `Release`(3)를 즉시 반환하여 메쉬 오프셋 오버라이드 릴리즈.
   - `GetThreadSafeOffsetRootTranslationMode()`: 기본 `Release`(3) 반환.
   - `GetThreadSafeOffsetRootTranslationHalfLife()`: `0.1f` 반환.
   - `GetThreadSafeOffsetRootTranslationRadius()`: `0.0f` 반환.
   - `GetThreadSafeStateControllerTurnInPlaceSteeringAlpha()`: `PhaseFamily == TurnInPlace` 일 때 `1.0f`, 그 외 `0.0f` 반환.
   - `GetThreadSafeStateControllerDesiredFacingRotator()`: `FRotator(0.f, Data.LocomotionContext.DesiredFacingDeltaYaw, 0.f)` 반환.
   - `GetThreadSafeStateControllerShouldTurnInPlace()`: `Data.LocomotionContext.bShouldTurnInPlace` 반환.
   - `GetThreadSafeStateControllerShouldAbortTurnInPlace()`: `Data.LocomotionContext.bShouldAbortTurnInPlace` 반환.
   - `OnStateEntry_TurnInPlace()`: `PhaseFamily == TurnInPlace` 상태 진입 이벤트를 핸들링.

3. **비전투 OTM 모드 원샷 억제 (`Project_JLocomotionAnimStateComponent`)**:
   - `IsStartingForContext(AuthContext, KinematicContext)` 및 `IsPivotingForContext(AuthContext, KinematicContext)`에 `AuthContext.RotationMode != Strafe` 조건 반영.
   - 비전투 OTM 모드일 때는 정면 스타트(`M_Neutral_Run_Start_F_Lfoot`) 및 180도 리페이스 원샷(`M_Neutral_Run_Reface_Start_F_R_180`) 진입을 억제하여, WASD 입력 즉시 캡슐과 메쉬가 Motion Matching Cycle로 매끄럽게 회전하도록 보장.

### B. AnimGraph 블루프린트 교정 (`ABP_Humanoid_Master`)

- **Offset Root Bone 노드 연결 해제**:
  - AnimGraph 내 `Offset Root Bone` 노드의 연결을 끎/해제함.
  - 이 조치로 절차적 오프셋 락과 OTM 캡슐 회전 간의 모든 물리적/시각적 충돌이 원천 차단됨.

---

## 4. Codex GPT 5.6 후속 작업자를 위한 지침 (Handoff Instructions)

1. **캡슐 회전 조작 금지**:
   - C++ 코드에서 Character Capsule의 Rotation을 `SetActorRotation` 등으로 강제하지 마십시오. OTM 및 TIP 로직은 AnimGraph의 `Steering Alpha` 및 `Motion Matching` 샘플링으로 해결해야 합니다.
2. **Offset Root Bone 사용 주의**:
   - OTM 모드가 활성화된 상태에서 `Offset Root Bone` 노드를 다시 켜면 캡슐 회전과 오프셋 락이 다시 충돌할 수 있습니다. 켜야 하는 경우 반드시 `Combat Strafe` 모드 및 `TurnInPlace` Phase에서만 한정하여 동작하도록 보장하십시오.
3. **Chooser 원샷 조건 유효성**:
   - Orient-to-Movement 모드에서는 Direction-fixed (Start F / Reface 180) 원샷 모션을 억제해야 캡슐 회전 속도와 무관하게 자연스러운 360도 이동이 가능합니다.
