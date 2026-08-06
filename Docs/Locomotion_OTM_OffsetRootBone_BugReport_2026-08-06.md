# Locomotion Orient-To-Movement (OTM) & Offset Root Bone 버그 분석 및 해결 보고서

- **일자**: 2026-08-06
- **대상**: `Project_J` 로코모션 애니메이션 시스템 (`Project_JCharacter`, `ABP_Humanoid_Master`)
- **목적**: 비전투 OTM 모드 이동 비틀림, Offset Root Bone 노드 연결 시 꼬임, 전투 Turn In Place (TIP) Presentation Enum 누락, Chooser Index UPROPERTY, OneShot.bRequested 매핑, OverrideMM Loop 조건, Idle 끊김 제거, TIP 회전 완주 스무스 동기화, Strafe 이동 축 유격(Drifting) 제거, TIP 시 Leg IK 1.0 유지 및 Stop -> TIP 인터럽트 수복 내역을 기록하여 후속 작업자가 원활하게 이어받을 수 있도록 핸드오프 문서 제공.

---

## 1. 수복 내역 요약 (Summary of Fixes)

1. **`Stop` (TransitionToIdle) ➔ `TurnInPlace` (TIP) 의도 즉시 교체 수복 (`Project_JCharacterAnimInstance.cpp`)**:
   - 기존에는 `TransitionToIdle` 모션이 재생 중일 때 `IsNaturalLoopContinuation(TransitionToIdle, TurnInPlace)`가 `true`를 반환하도록 되어 있어, 멈춤(Stop) 모션 2.6초가 완전히 끝날 때까지 TIP 회전 상태 진입이 강제로 잠겨 있었습니다.
   - `TransitionToIdle`의 자연 루프 전환 대상에서 `TurnInPlace`를 제외하여, **Stop 모션 중이라도 마우스를 돌리면 Intent Replacement(플레이어 의도 즉시 반영)가 발동하여 딜레이 없이 TIP 회전 모션으로 즉시 전환**됩니다.

2. **`TurnInPlace` (TIP) 시 Leg IK `1.0` 유지 수복 (`Project_JCharacterAnimInstance.cpp`)**:
   - TIP 모션 재생 시에도 Leg IK가 **`1.0` (100% 활성화)**을 유지하여 발 접지감 지속.

3. **Strafe 이동 축 이격(Drifting) 100% 수복 (`Project_JCharacterAnimInstance.cpp`)**:
   - `GetThreadSafeOffsetRootTranslationMode()`가 항상 **`EOffsetRootBoneMode::Release`**를 반환하도록 고정하여 메쉬 루트 위치가 캡슐 중심 축에 **100% 밀착 고정**.

4. **TIP 0.5초 완주 동안 캡슐 스무스 회전 보간(RInterpTo 8.0) 수복 (`Project_JPlayerCharacter.cpp`)**:
   - `ApplyCombatRotationMode`에서 `bInTurnInPlace`가 `true`일 때 C++ `FMath::RInterpTo`로 캡슐을 애니메이션 속도와 맞춰 부드럽게 동기화 회전.

---

## 2. 디버깅 트레이스 콘솔 명령어 (Debugging Guide)

- 콘솔 창(`~`)에서 아래 명령어를 입력하면 **Stop -> TIP 및 상태 제어 변환 트레이스 로그**가 실시간 출력됩니다:
  - **`p.ProjectJ.MMTransitionDebug 1`**
  - **`DumpMotionMatchingTransitionTrace`**
