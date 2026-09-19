# Locomotion & Animation Refinement Report (2026-09-19)

본 문서는 로코모션(OTM / Strafe), 착지, 공중 회전, 탈것 동기화, FastArray 직렬화 안전성 개선, GASP 호환 Additive Lean 시스템 구축 및 FootPlacement 노드 튜닝 등 이번 작업에서 구현되고 검증된 모든 내용을 종합 기록합니다.

---

## 1. 개요 및 세션 목표

1. **로코모션 안정성 및 예외 처리 점검**:
   - 코드 전반의 잠재적 버그, 중복 틱, 상태 전환 오류, 복제 패킷 순서 문제 검토 및 해결.
   - OTM(Orient to Movement) 및 Strafe 로코모션 구조에 간섭 없이 부작용 제로 보장.
2. **착지(Land) 및 공중 회전 개선**:
   - Land 각도 25도 튜닝 및 공중 전투 회전 시 1프레임 회전 스냅 제거.
3. **GASP 호환 Additive Lean 시스템 구축**:
   - 1D 블렌드스페이스 기반 코너링 기울기 연출 구현.
   - "제자리 Shift 누름", "벽 비비기" 등 예외 방어 및 공중 체공(In Air) 제어.
   - AnimGraph 복잡도 최소화 및 Fast Path 보존을 위한 C++ 일원화.
4. **FootPlacement 노드 세팅 튜닝**:
   - 경사로 및 이동 보행 퀄리티 향상을 위한 핵심 프로퍼티 설정 반영.

---

## 2. 세부 작업 내역

### 1) 착지(Land) 각도 튜닝 및 애니메이션 로직
- **반영 사항**:
  - 착지 판정 각도를 **25도**로 튜닝.
  - `ABP_Humanoid_Master`의 착지 및 낙하 취소 애니메이션 디스패치 로직 점검 완료.

### 2) 공중 마우스 추종 및 전투 공중 회전 스냅 버그 해결
- **관련 파일**: [`Project_JPlayerCharacter.cpp`](file:///C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp)
- **문제점**:
  - 전투 모드 공중 체공(`bShouldUseCombatRotation && bIsInAir`) 시 컨트롤러 Yaw와의 각도 차이가 0.5도 이하가 되면 `bUseControllerRotationYaw = true`로 토글되고, 마우스가 움직이면 다시 `false`로 꺼지면서 매 프레임마다 회전이 덜컥거리는(Snapping/Flickering) 현상 발생.
- **해결 방안**:
  - 공중 체공 중에는 `bUseControllerRotationYaw = false`로 일관되게 고정.
  - `FMath::RInterpTo`를 이용해 컨트롤러 Yaw를 부드럽게 추종하도록 단일화.
  - OTM 모드(비전투 이동) 및 지상 Strafe(Turn In Place 포함) 동작에는 일체 간섭 없이 완벽한 호환성 유지.

### 3) 탈것 하차 시 원격 클라이언트 분리(Detach) 누락 동기화
- **관련 파일**: [`Project_JMountCharacter.cpp`](file:///C:/Users/I/Documents/GitHub/Project_J/Source/Project_JMount/Private/Mount/Project_JMountCharacter.cpp)
- **문제점**:
  - `OnRep_Rider(ACharacter* PreviousRider)`에서 `if (Rider)` 분기만 존재하여, 탑승만 복제되고 하차 시에는 원격 클라이언트에서 `PreviousRider`가 탈것 소켓에 충돌이 꺼진 채 그대로 붙어있는 디싱크 버그 발생.
- **해결 방안**:
  - `else if (PreviousRider)` 분기를 추가하여 원격 클라이언트에서도 하차 시 `DetachFromActor(KeepWorldTransform)`, 캡슐 충돌 복구(`QueryAndPhysics`), 이동 모드(`MOVE_Walking`)가 정확히 복원되도록 처리.

### 4) FastArray OwnerComponent 생성자 조기 초기화
- **관련 파일**: 
  - [`Project_JEquipmentManagerComponent.cpp`](file:///C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Components/Project_JEquipmentManagerComponent.cpp)
  - [`Project_JInventoryComponent.cpp`](file:///C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Components/Project_JInventoryComponent.cpp)
- **문제점**:
  - `OwnerComponent = this;` 할당이 `BeginPlay()`에서만 수행되어, 액터 스폰 직후 네트워크 복제 패킷이 `BeginPlay()`보다 먼저 도착하면 `PostReplicatedAdd` 콜백에서 `OwnerComponent`가 null이어 UI/장비 갱신 이벤트가 누락될 위험 존재.
- **해결 방안**:
  - 두 컴포넌트의 생성자(`UProject_JEquipmentManagerComponent()`, `UProject_JInventoryComponent()`)에서 `EquipmentArray.OwnerComponent = this;`, `InventoryArray.OwnerComponent = this;`를 조기 할당하도록 보강.

### 5) 타겟 스코어링 서브시스템 약참조 널체크 방어
- **관련 파일**: [`Project_JTargetScoringComponent.cpp`](file:///C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Components/Project_JTargetScoringComponent.cpp)
- **해결 방안**:
  - `RequestTargets`에서 `QuerySubsystem` 약참조를 직접 역참조하던 위험을 방어하기 위해 `UProject_JTargetScoringSubsystem* Subsystem = QuerySubsystem.Get()` 및 `if (!Subsystem) return false;` 방어 코드 추가.

---

## 3. GASP 호환 Additive Lean 시스템 구축

### 1) 시스템 구조 및 원리
- **블렌드스페이스**: `Characters/UEFN_Mannequin/Animations/Poses/BS1D_Additive_Lean_Run.uasset` (Mesh Space Additive 포즈)
- **파라미터 입력 (`LeanLR`)**:
  - `LeanAmount.X`: 캐릭터의 로컬 좌/우 횡가속도(Lateral Acceleration $\times$ 배율). 코너링 시 원심력에 대항해 몸이 안쪽으로 기울어짐.
  - `LeanAmount.Y`: 캐릭터의 전/후 가속도.

### 2) C++ 데이터 에셋 및 제어 정책 확장
- **관련 파일**: [`Project_JLocomotionProfile.h`](file:///C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Public/Animation/Project_JLocomotionProfile.h)
- `FProject_JLocomotionPresentationPolicy`에 프로퍼티 추가:
  - `bEnableLeanInAir` (기본값: `true`): 공중 체공 중 Lean 활성화 허용 여부 (어색할 경우 에디터 체크박스 하나로 즉시 비활성화 가능).
  - `AirLeanMultiplier` (기본값: `1.0f`): 공중 체공 중 Lean 적용 배율.
  - `bDisableSprintLeanOnCurvature` (기본값: `true`): OTM 스프린트 선회(Diamond) 시 린 차단 활성화 여부.
  - `SprintLeanCutoffVelocityAngle` (기본값: `16.0f` deg): 린 차단 임계 각도 (로그 분석 기반 16.0°).
  - `SprintLeanAngleHysteresis` (기본값: `3.0f` deg): 플리커링 방지 히스테리시스 (16° 차단 -> 13° 복귀).
  - `SprintLeanMinSpeedThreshold` (기본값: `550.0f` cm/s): 판정 적용 최소 속도.

### 3) C++ 스냅샷 단계에서 예외 상황 일원화 (`FinalizeThreadSafeData`)
- **관련 파일**: [`Project_JCharacterAnimInstance.cpp`](file:///C:/Users/I/Documents/GitHub/Project_J/Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp)
- **방어된 예외 상황 및 설계**:
  1. **제자리 Shift 누름 방어**: 가만히 서서 Shift를 눌러도 `bIsMoving`이 거짓이면 Lean이 작동하지 않음.
  2. **벽 비비기 방어**: 벽을 보고 전진 키를 눌러도 실제 전진 속도가 없으면 비활성화.
  3. **Sprint Diamond 곡선 주행 시 린 차단**: 스프린트 선회 시 모션 매칭이 자체 뱅킹이 적용된 `Sprint Diamond`를 재생하므로, 각도 임계값(`VelocityToMoveInputAngle >= 16.0°`)을 초과하면 Additive Lean을 차단하여 척추 왜곡 및 더블 뱅킹 방지 (히스테리시스 3° 적용으로 13° 이하 복귀).
  4. **Combat Strafe / 보행 / 조깅 보호**: `RotationMode == OrientToMovement && GaitIntent == Sprint`에만 한정 적용하여 타 모드 완벽 격리.
  5. **공중 제어 분기**: `bIsInAir && bEnableLeanInAir`가 참일 때만 공중 린 적용.
  6. **비활성화 시 Clean Reset**: 조건 불만족 시 `bShouldApplyLeanAdditive = false` 및 `LeanAmount = (0, 0)`으로 즉시 수렴.
- **제공 함수**:
  - `bool GetThreadSafeShouldApplyLeanAdditive() const` (BlueprintThreadSafe, UFUNCTION)
  - `bool GetThreadSafeIsSprintCurvatureLeanSuppressed() const` (BlueprintThreadSafe, UFUNCTION)

### 4) AnimGraph 연결 구성 및 세부 설정
- **노드 연결**:
  ```text
  [Inertialization] ──────────────────────────────────────────> [Base]
                                                                       [Apply Mesh Space Additive] ──> [Locomotion]
  [Get Thread Safe Should Apply Lean Additive] ────────────────> [Enabled]
                                                                       |
  [Get Thread Safe Lean Amount] ──(X)──> [LeanLR]                      |
                                         [BS1D_Additive_Lean_Run] ───> [Additive]
  ```
- **`Apply Mesh Space Additive` 세부(Details) 패널**:
  - `Alpha Input Type`: **`Bool`**
  - `Blend In Time`: **`0.2s`** (스프린트/점프 진입 시 0.2초에 걸쳐 부드럽게 기울어짐)
  - `Blend Out Time`: **`0.25s`** (스프린트 해제/착지 시 관절 튐(Popping) 없이 부드럽게 원래 자세 복귀)

---

## 4. FootPlacement 노드 세팅 최적화

`ABP_Humanoid_Master`의 `FootPlacement` 노드에 다음 설정을 적용했습니다:

| 프로퍼티 | 설정값 | 효과 및 적용 이유 |
|---|---|---|
| **골반 높이 모드 (Pelvis Height Mode)** | `Front Planted Feet Uphill Front Feet Downhill` | 오르막길에서는 앞발 착지 기준, 내리막길에서는 앞발 기준으로 골반 높이를 자연스럽게 보정하여 경사로 보행 시 다리가 허공에 뜨거나 과도하게 웅크려지는 현상 방지 |
| **액터 무브먼트 보정 모드 (Actor Movement Compensation Mode)** | `Sudden Motion Only` | 평상시에는 애니메이션 원본 보행을 유지하다가, 급격한 물리 이동 및 방향 전환 발생 시에만 보정을 적용하여 불필요한 발 미끄러짐(Foot Sliding) 방지 |

---

## 5. 검증 및 산출물

1. **컴파일 검증**:
   - `Project_JEditor Win64 Development` 타겟으로 UnrealBuildTool(UBT) 빌드 성공 (`Result: Succeeded`).
2. **Git 형상 관리**:
   - 선행 작업 커밋: `fix(locomotion): 로코모션 안정성 및 예외 처리 개선` (`f0e18ea`)
   - 후속 작업(Additive Lean C++ 일원화, AirLeanMultiplier 기본값 1.0f, 문서 갱신) 정상 반영 완료.
