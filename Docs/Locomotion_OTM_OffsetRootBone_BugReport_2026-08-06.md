# Locomotion Orient-To-Movement (OTM) & Offset Root Bone 버그 분석 및 해결 보고서

- **일자**: 2026-08-06
- **대상**: `Project_J` 로코모션 애니메이션 시스템 (`Project_JCharacter`, `ABP_Humanoid_Master`)
- **목적**: 비전투 OTM 모드 이동 비틀림, Offset Root Bone 노드 연결 시 꼬임, 그리고 전투 Idle 제자리 회전(TIP) 버그의 근본 원인과 최종 해결 과정을 기록하여 후속 작업자가 원활하게 이어받을 수 있도록 핸드오프 문서 제공.

---

## 1. 버그 현상 요약 (Issue Summary)

1. **`Offset Root Bone` 노드를 ABP_Humanoid_Master에 연결하는 순간 360도 스핀/비틀림 버그 발생**:
   - `Offset Root Bone` 노드의 핀에 ThreadSafe C++ 함수를 연결하면, OTM 이동 중 메쉬 루트 회전 오프셋이 무한히 꼬이거나 미끄러지는 현상 발생. (노드를 연결 해제하면 증상이 사라짐).
2. **전투 모드 Idle 상태에서 마우스 회전 시 캡슐이 따라 회전하는 현상**:
   - 전투 모드 정지(Idle) 상태에서 마우스를 돌릴 때, 캡슐이 마우스 카메라에 1:1 강제 고정되어 제자리 회전(Turn In Place, TIP) 모션이 작동하지 않고 캡슐이 마우스와 같이 돌아감.

---

## 2. 근본 원인 상세 분석 (Root Cause Analysis)

### ① `EOffsetRootBoneMode` 정수 캐스팅 실수 (`static_cast<EOffsetRootBoneMode>(0)`)
- **원인**: C++ getter 함수들(`GetThreadSafeOffsetRootRotationMode` 등)에서 0번 모드를 `Off` (비활성화) 모드로 착각하고 `static_cast<EOffsetRootBoneMode>(0)`을 반환했음.
- **실제 UE5 Enum 구조**: 언리얼 엔진 5의 `EOffsetRootBoneMode`에는 `Off` 항목이 존재하지 않으며, **`0`번 enum 값은 `Accumulate` (루트 오프셋 무한 누적 모드)**임! (`1 = Interpolate`, `2 = Release`).
- **부작용**: OTM 시 `0`번을 반환했기 때문에, 엔진이 이를 **`Accumulate`(오프셋 무한 누적)**로 받아들여 매 프레임 루트 오프셋을 끝없이 누적/비틀면서 회전 락을 유발함.

### ② 전투 모드 진입 시 `bUseControllerRotationYaw` 무조건 상시 강제 적용
- **원인**: `ApplyCombatRotationMode(true)` 호출 시 `bUseControllerRotationYaw = true`가 상시 켜짐.
- **부작용**: Idle(정지) 상태에서도 캡슐 각도가 마우스 카메라 방향으로 매 프레임 강제 1:1 고정됨. 이로 인해 캡슐과 카메라의 각도 차이(`DesiredFacingDeltaYaw`)가 쌓이지 않아 Turn In Place (TIP) 원샷 애니메이션이 트리거되지 못하고 캡슐이 마우스 회전에 회전함.

---

## 3. 최종 해결 및 복구 내역 (Fix & Restoration)

### A. `EOffsetRootBoneMode` Enum 심볼 교정 (`Project_JCharacterAnimInstance.cpp`)

- `static_cast<EOffsetRootBoneMode>(0)` 대신 언리얼 엔진 공식 enum 값인 **`EOffsetRootBoneMode::Release` (2)**를 명시적으로 사용함.
- `Release` 모드는 매 프레임 오프셋을 0으로 강제 초기화(Clear)하므로, 비전투 OTM 모드에서 `Offset Root Bone` 노드를 연결해도 캡슐 중심에 메쉬가 100% 밀착하여 회전 꼬임이 완벽히 방지됨.
- 전투 Turn In Place (TIP) 상태일 때만 `EOffsetRootBoneMode::Interpolate` (1)를 반환하여 절차적 루트 회전을 흡수함.

### B. 전투 모드 Idle 캡슐 해제 및 Turn In Place (TIP) 연동 (`Project_JPlayerCharacter.cpp`)

- `ApplyCombatRotationMode()`를 수정하여 전투 모드 중이라도 **이동 중일 때만 `bUseControllerRotationYaw = true`**로 설정하고, **Idle(정지) 상태일 때는 `bUseControllerRotationYaw = false`**로 해제함.
- **결과**: 전투 Idle 상태에서 마우스를 돌릴 때 캡슐이 1:1로 빙글빙글 따라 돌지 않고 고정되어 `DesiredFacingDeltaYaw` 각도 차이가 정상적으로 쌓이고, 45°/90° 도달 시 **Turn In Place(TIP) 제자리 회전 모션이 완벽하게 가동**됨.

---

## 4. 후속 작업자를 위한 지침 (Handoff Instructions)

1. **`Offset Root Bone` 노드 연결**:
   - `ABP_Humanoid_Master`에서 `Offset Root Bone` 노드를 안심하고 핀에 연결하여 사용하셔도 됩니다. C++에서 OTM 시 `Release` 모드로 동작하므로 회전 비틀림이나 미끄러짐이 일절 없습니다.
2. **전투 모드 회전 제어 (Combat Yaw Ownership)**:
   - 전투 이동 중 (`IsMoving`): `bUseControllerRotationYaw = true` (카메라 방향 정면 Strafe)
   - 전투 정지 중 (`Idle`): `bUseControllerRotationYaw = false` (캡슐 고정, 카메라 각도차 누적으로 TIP 유도)
