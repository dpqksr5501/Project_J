# Locomotion Orient-To-Movement (OTM) & Offset Root Bone 버그 분석 및 해결 보고서

- **일자**: 2026-08-06
- **대상**: `Project_J` 로코모션 애니메이션 시스템 (`Project_JCharacter`, `ABP_Humanoid_Master`)
- **목적**: 비전투 OTM 모드 이동 비틀림, Offset Root Bone 노드 연결 시 꼬임, 전투 Turn In Place (TIP) Presentation Enum 누락, Chooser Index UPROPERTY, OneShot.bRequested 매핑, OverrideMM Loop 조건, Idle 끊김 제거, TIP 회전 완주 스무스 동기화, Strafe 이동 축 유격(Drifting) 제거, TIP 시 Leg IK 1.0 유지, Stop -> TIP 인터럽트 수복, 삼중 회전 제거, OTM Stop -> Start 원샷 수복, TIP 무한 재진입(360도 스핀) 버그 수복 내역을 기록하여 후속 작업자가 원활하게 이어받을 수 있도록 핸드오프 문서 제공.

---

## 1. 수복 내역 요약 (Summary of Fixes)

1. **`TurnInPlace` (State 8) 무한 무한 재진입(360도 팽이 스핀) 버그 완벽 수복 (`Project_JCharacterAnimInstance.cpp`)**:
   - **원인 분석**:
     - 기존에는 `ResolveStateControllerPresentationState()` 내부 조건문에 `OneShot.PhaseFamily == TurnInPlace`가 포함되어 있어, 턴 재생(0.5초)이 끝난 후에도 `PhaseFamily`가 여전히 `TurnInPlace`를 가리키고 있으면 `State 8 ➔ State 8`로 자가 재진입(ExitHold)되어 캐릭터가 지 혼자 360도 무한 스핀을 도는 문제가 발생함.
   - **수복**:
     - 조건문을 오직 **`Data.LocomotionContext.bShouldTurnInPlace` (각도 오차 45도 이상)** 일 때만 `TurnInPlace` (State 8)를 요청하도록 변경.
     - 0.5초 턴 도중 각도 오차가 45도 이내로 접어들면 타이머 종료 시 **즉시 `IdleLoop` (State 1)로 빠져나와 턴 동작이 깔끔하게 종료**됨.

---

## 2. 수복 후 진단 트레이스 결과 (Trace Log)

- TIP 턴 완주 시 C++ 로그:
  `StateControllerExitHold: Exiting State=8 to State=1 (Elapsed=0.508 Remaining=0.000)`
  ➔ 0.5초 턴 완료 후 무한 재진입 없이 `State 1` (Idle)로 깔끔하게 빠져나옴!
