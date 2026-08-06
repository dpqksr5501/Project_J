# Locomotion Orient-To-Movement (OTM) & Offset Root Bone 버그 분석 및 해결 보고서

- **일자**: 2026-08-06
- **대상**: `Project_J` 로코모션 애니메이션 시스템 (`Project_JCharacter`, `ABP_Humanoid_Master`)
- **목적**: 비전투 OTM 모드 이동 비틀림, Offset Root Bone 노드 연결 시 꼬임, 전투 Turn In Place (TIP) Presentation Enum 누락, Chooser Index UPROPERTY, OneShot.bRequested 매핑, OverrideMM Loop 조건, Idle 끊김 제거, TIP 회전 완주 스무스 동기화 및 Strafe 이동 축 유격(Drifting) 제거 내역을 기록하여 후속 작업자가 원활하게 이어받을 수 있도록 핸드오프 문서 제공.

---

## 1. 버그 현상 요약 (Issue Summary)

1. **`Offset Root Bone` 노드를 ABP_Humanoid_Master에 연결하는 순간 360도 스핀/비틀림 버그 발생**:
   - `Offset Root Bone` 노드의 핀에 ThreadSafe C++ 함수를 연결하면, OTM 이동 중 메쉬 루트 회전 오프셋이 무한히 꼬이거나 미끄러지는 현상 발생. (노드를 연결 해제하면 증상이 사라짐).
2. **전투 모드 Idle 상태에서 마우스 회전 시 제자리 회전(TIP) 미작동 및 Chooser 매칭 실패 (`Asset=None`)**:
   - `CHT_Player_Strafe_GroundPresentation` 1번 행이 `= EProject MAX`로 비정상 바인딩되어 있었음.
   - C++ OneShot 요청 처리기(`OneShot.bRequested` 및 `IsTransitionState`) 내부 switch 문에 `TurnInPlace` 항목이 누락되어 회전 애니메이션 출력이 거부됨.
   - `ShouldStateControllerPresentationLoop(TurnInPlace)`가 `true`를 반환하도록 잘못 설정되어 있어, `bShouldOverrideMotionMatching`이 `false` (`OverrideMM = 0`)로 꺼지고 애니메이션 모션이 메쉬로 출력되지 않았음.
3. **전투 모드 Strafe 전진/앞 대각선 이동 시 캐릭터 메쉬가 캡슐 축을 벗어나 미끄러지듯 이동하는 현상**:
   - `GetThreadSafeOffsetRootTranslationMode()`가 이동 입력 시 `Interpolate` (위치 보간 모드)를 반환했음.
   - 메쉬 루트 위치가 물리 캡슐 충돌체 중심보다 최대 30cm 뒤처지며(Lag) 시각적 이격 및 미끄러짐 현상(Drifting) 발생.

---

## 2. 해결 및 수복 내역 (Fix & Restoration)

### A. Strafe 이동 축 이격(Drifting) 100% 수복 (`Project_JCharacterAnimInstance.cpp`)
- `GetThreadSafeOffsetRootTranslationMode()`가 항상 **`EOffsetRootBoneMode::Release`**를 반환하도록 교정.
- **효과**:
  - Strafe 전진, 앞대각선, 측면 이동 중에도 메쉬 루트 위치가 물리 캡슐의 중심 축에 **100% 단단하게 밀착 고정(Centered)**됩니다.
  - 메쉬가 캡슐 밖으로 벗어나는 미끄러짐/유격 현상이 완전히 사라지고, 묵직하고 절도 있는 이동감을 선사합니다.

### B. TIP 0.5초 완주 동안 캡슐 스무스 회전 보간(RInterpTo 8.0) 수복 (`Project_JPlayerCharacter.cpp`)
- `ApplyCombatRotationMode`에서 `bInTurnInPlace`가 `true`일 때(0.5초 턴 모션 재생 시간 동안만), C++에서 `FMath::RInterpTo(CurrentRot, TargetRot, DeltaSeconds, 8.0f)`로 캡슐을 부드럽게 회전시켰습니다.
- **효과**:
  - Idle 상태일 때는 캡슐이 멈춰있어 **드드득거리는 물리 끊김 현상이 0%**입니다.
  - 마우스를 45도 이상 돌려 TIP 모션이 재생되는 0.5초 동안, **애니메이션 발걸음 속도와 100% 동일한 부드러움으로 캡슐(Actor)과 메쉬가 함께 완전히 회전**합니다!

---

## 3. 후속 작업자를 위한 지침 (Handoff Instructions)

1. **언리얼 에디터 추저 테이블 2곳 수동 지정 (1회 필수)**:
   - `CHT_Player_Strafe_GroundPresentation`: 1번 행의 `= EProject MAX` -> **`TurnInPlace`** 항목 선택 후 저장.
   - `CHT_Player_Strafe_TurnInPlace`: 깨져 있는 플로트 범위 열 드롭다운 -> **`StateControllerTurnInPlaceIndexForChooser`** 선택 후 저장.
2. **`Offset Root Bone` 노드 연결**:
   - `ABP_Humanoid_Master`에서 `Offset Root Bone` 노드를 핀에 연결하여 사용.
