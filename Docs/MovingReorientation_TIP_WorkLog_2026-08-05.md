# Moving Reorientation (Rotation Break) & TIP 작업 기록 및 인수인계 문서 (2026-08-05)

> **이 문서는 다른 대화 세션에서도 이어서 작업을 진행할 수 있도록, 작업 배경, 기술 결정 사항, C++ 구현 내역 및 에디터 검증 절차를 상세히 기록하는 문서입니다.**

---

## 1. 작업 배경 및 핵심 정책 (Background & Policy)

### A. GASP ↔ Project_J 아키텍처 대응
* **GASP 회전 특성**: GASP는 정지(Idle) 상태에서 제자리 회전하는 Idle TIP(Turn In Place)가 기본적으로 포함되어 있지 않으며, 정지 중 마우스 회전 시 Aim Offset(상체/헤드)만 카메라를 추종합니다.
* **Rotation Break (Moving Reorientation)**: 정지 중 이동을 시작하거나(Start) 이동 중(Pivot) 마우스(카메라)를 빠르게 회전시킬 경우, 재생 중이던 원샷 애니메이션을 끊고 최신 목표 방향에 맞추어 Chooser 재선택(Reselect)을 수행합니다.
* **Project_J 적용 원칙**:
  * **비전투(OTM)**와 **전투(Combat Strafe)** 모드가 엄격히 구분됩니다.
  * Cycle / Turn Redirect는 기존 PSD 기반 연속 Motion Matching이 담당합니다.
  * Start / Stop / Pivot / Jump / FallOff / Land / InAirLoop / TurnInPlace는 **State Controller + Chooser + Blend Stack Direct 재생 (`UseMM=false`)** 정책을 유지합니다.

---

## 2. C++ 구현 상세 (Implementation Details)

### Primary Files
* `Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h`
* `Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp`

### A. 추가된 튜닝 UPROPERTY (State Controller Section)
```cpp
// Target Rotation Delta 60도 이상 시 Reselect (GASP 기준 수치)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State Controller", meta = (ClampMin = "0.0", UIMin = "0.0"))
float StateControllerRotationBreakAngleThreshold = 60.0f;

// Target Rotation 보간 속도 (GASP RInterpTo 기준 수치)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State Controller", meta = (ClampMin = "0.0", UIMin = "0.0"))
float StateControllerRotationBreakInterpSpeed = 5.0f;

// 원샷 진입 후 재선택 허용 타임 윈도우 (초)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State Controller", meta = (ClampMin = "0.0", UIMin = "0.0"))
float StateControllerRotationBreakMaxElapsedSeconds = 0.5f;

// 재선택 연속 발동 방지 쿨다운 (초)
UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Animation|State Controller", meta = (ClampMin = "0.0", UIMin = "0.0"))
float StateControllerRotationBreakCooldownSeconds = 0.15f;
```

### B. Rotation Break 연산 및 Reselect 알고리즘
`EvaluateStateControllerAnimationChooserOnGameThread()` 내에 반영:
1. 현재 State Controller Presentation State가 `Start` 또는 `Pivot` 원샷일 때, 진입 목표 방향(`StateControllerTargetRotationOnTransitionStart`)을 래칭.
2. 매 프레임 `FMath::RInterpTo`(속도 5.0f)로 최신 마우스/카메라 목표 방향(`StateControllerInputFacingDeltaYawForChooser`)을 향해 회전값 래핑 보간.
3. 원샷 시작 후 `StateElapsed <= 0.5s` 이내에 마우스가 급격히 회전하여 `YawDeltaFromStart >= 60.0f` 조건 성립 시 **`bRotationBreakReselectRequested = true`** 발동.
4. Chooser 조건 변수가 재평가되어 최신 8방향/디딤발(Foot)에 맞는 `Strafe Run Pivot` 또는 `Start` 원샷으로 Blend Stack 상에서 부드럽게 재선택 믹스.
5. TIP(`TransitionToTurnInPlace`) 재생 시에는 자체 루트 회전을 존중하도록 이동 원샷 전용 Orientation Warping(`bShouldEnableCombatStrafeOrientationWarping`)에서 예외 처리(`!= TransitionToTurnInPlace`)하여 이중 회전(Double-warping) 방지.

### C. Steering 및 Offset Root Bone 전용 ThreadSafe Pure Getter 함수
* `GetThreadSafeStateControllerTurnInPlaceSteeringAlpha()`: TIP 재생 중(`TransitionToTurnInPlace`)일 때 `1.0f`, 그 외 `0.0f` 반환.
* `GetThreadSafeStateControllerDesiredFacingRotator()`: `Steering` 노드의 `Target Orientation` 파란색 Rotator 핀에 연결할 목표 Facing Rotator (`FRotator(0.0f, DesiredFacingDeltaYaw, 0.0f)`) 반환.
* `GetThreadSafeOffsetRootRotationMode()`: `Offset Root Bone` 노드의 `Rotation Mode` 핀에 연결 (지상 시 `Interpolated` (2), 공중 시 `Off` (0) 반환).
* `GetThreadSafeOffsetRootTranslationMode()`: `Offset Root Bone` 노드의 `Translation Mode` 핀에 연결 (이동 시 `Interpolated` (2), 정지 시 `Release` (3) 반환).
* `GetThreadSafeOffsetRootTranslationHalfLife()`: `Offset Root Bone` 노드의 `Translation Halflife` 핀에 연결 (`0.1f` 반환).
* `GetThreadSafeOffsetRootTranslationRadius()`: `Offset Root Bone` 노드의 `Max Translation Radius` 핀에 연결 (`30.0f` 반환).

---

## 3. 검증 및 디버깅 (Verification & Debugging)

### 애니메이션 블루프린트 (ABP) 바인딩
* `State Controller State Machine` 내 `Turn In Place` 스테이트의 `On State Entry` 이벤트에 **`OnStateEntry_TurnInPlace`** 그래프 바인딩 완료.
* `AnimationBlendStackGraph_0` 내 TIP 전용 `Steering` 노드 알파/타겟 오리엔테이션 바인딩 준비 완료.

### 빌드 검증
* **Tool**: UnrealBuildTool (UBT direct invocation)
* **Target**: `Project_JEditor Win64 Development`
* **Status**: **`Result: Succeeded`** (컴파일 에러 0건)

### PIE 진단 및 제어 명령어
```text
p.ProjectJ.MMTransitionDebug 1
```
* 디버그 모드를 활성화하면 Output Log에 0.2초 간격으로 다음과 같이 쾌적하게 기록됩니다:
  ```text
  LogProjectJPlayer: Display: TIPSteeringTrace State=8 Alpha=1.00 RotMode=2 ControlYaw=82.7 ActorYaw=8.0 MeshYaw=-82.0 ControlVsActor=74.7 ControlVsMesh=164.7 SteeringRotYaw=74.4 DesiredDeltaYaw=74.6 OverrideMM=true SelectedAnim=M_Neutral_Stand_Turn_090_R
  ```
* 디버그 끄기: `p.ProjectJ.MMTransitionDebug 0`

---

## 4. 향후 작업 및 에디터 셋업 체크리스트 (Next Steps)

1. **PIE 인게임 튜닝**:
   * 마우스 감도 및 패드 조작 시 `UpdateTurnInPlaceActorRotation` 보간 속도(InterpSpeed = 10.0f) 반응성 테스트.
2. **Strafe Chooser 연결 확인**:
   * `CHT_Player_Strafe_Run_Pivot` 및 `CHT_Player_Strafe_Run_Start` Chooser 테이블 내 `UseMM=false` 직접 원샷 선택 행들 정상 배치 확인.
3. **Idle TIP 연계**:
   * 정지 중 camera-facing yaw delta 30°/65° 이상 시 발동되는 `bShouldTurnInPlace` 수명주기와 Idle 전용 TIP Chooser 행(`Turn 90 R/L`, `Turn 180 R/L`) 검증.
