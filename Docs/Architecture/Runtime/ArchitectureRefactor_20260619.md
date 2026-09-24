# Architecture Refactor - 2026-06-19

이 문서는 `ProjectOverview.md`의 구조 원칙을 현재 C++에 적용한 첫 리팩터링 묶음을 기록한다.

## 적용된 변경

### 플레이어와 NPC 런타임 소유권

- `EProject_JRuntimeStateOwnership`을 추가했다.
- Base/NPC 캐릭터는 `CharacterLocal`을 기본으로 사용한다.
- PlayerCharacter는 `PlayerStatePreferred`를 사용한다.
- 플레이어의 character-local ASC와 EquipmentManager는 fallback 용도로만 남기고 독립 복제를 비활성화했다.

### 인벤토리와 장비

- 공통 아이템 정의 기본 클래스 `UProject_JItemDefinition`을 신규 생성하고, `UProject_JEquipmentItemDefinition`이 이를 상속받도록 리팩토링했다.
- 인벤토리 인스턴스 데이터 `FProject_JItemInstanceData::ItemDef`의 타입을 공통 `UProject_JItemDefinition`으로 일반화하여, 장비 외에 소비아이템 등 차후 추가될 모든 유형의 아이템을 수용할 수 있는 아키텍처를 구축했다.
- 장비 관리자(`UProject_JEquipmentManagerComponent`) 등 장비 사양 확인이 필요한 노드에서는 내부적으로 `Cast<UProject_JEquipmentItemDefinition>`을 통해 장착 슬롯과 속성을 식별하는 안전한 유효성 검사 코드를 적용했다.
- 네트워크 장착 요청은 전체 `FProject_JItemInstanceData` 대신 `InstanceId`만 전달한다.
- 서버가 InventoryComponent에서 authoritative item instance를 다시 조회한다.
- 장착 검증 결과를 `FProject_JEquipmentOperationResult`로 구조화했다.
- 새 아이템 잠금이 성공한 뒤 기존 슬롯을 교체하도록 순서를 변경했다.
- 클라이언트가 Item Definition만 전송해 임의 장착하는 경로를 폐기했다.

### 전투 입력과 판정

- CombatComponent의 기본 공격은 InputTag 활성화를 우선 사용한다.
- AbilityTag 활성화는 migration fallback으로 유지한다.
- 무기 공격 설정에 `InputTag`를 추가했다.
- 서버 hit request 검증을 `FProject_JCombatHitValidationPolicy`로 분리했다.
- 시간, 미래 timestamp, trace 길이, 대상 거리와 authority를 SSR 전에 검증한다.
- 검증되고 확인된 hit은 설정된 GameplayEffect를 통해 적용할 수 있다.

### 파생 애니메이션 상태

- `CurrentWeaponAnimProfile`의 독립 복제를 제거했다.
- 복제된 장비 상태를 소비하는 EquipmentRuntime이 각 인스턴스에서 프로필을 계산한다.

### Character 책임

- 이동 애니메이션 이벤트 5종의 RPC와 replicated counter를 `UProject_JReplicatedAnimEventComponent`로 이동했다.
- PlayerCharacter의 기존 dispatch 함수는 호환 wrapper로 유지한다.

### AnimInstance 책임

- `FProject_JCharacterAnimInstanceProxy`와 Motion Matching node 조작·BlendStack 진단을 별도 파일로 분리했다.
- `Project_JCharacterAnimInstance.cpp`는 snapshot, Chooser, profile 및 optimization 흐름에 집중한다.
- UE 5.8에서 필드는 남아 있지만 런타임 검색 경로가 더 이상 소비하지 않는 `bShouldSearch` 조작을 제거했다.
- 공중 검색 억제 시간은 `FProject_JMotionMatchingSearchPolicy`가 계산한다.

### 테스트

다음 자동화 테스트를 추가했다.

- `ProjectJ.Architecture.MotionMatching.SearchPolicy`
- `ProjectJ.Architecture.Combat.HitValidationPolicy`
- `ProjectJ.Architecture.Equipment.OperationResult`

## 유지된 호환성

- 기존 `EquipItem`, `EquipItemInstance`, `RequestEquipItemInstance` 함수는 유지한다.
- `RequestEquipItemInstance`는 내부적으로 InstanceId 요청으로 변환한다.
- PlayerCharacter의 기존 animation dispatch 함수 이름은 유지한다.
- AbilityTag 기반 활성화는 기존 에셋 마이그레이션을 위해 fallback으로 남긴다.
- Motion Matching PSD 선택, Chooser 변수, locomotion state와 장비 FastArray 데이터 형태는 유지한다.

## 후속 에디터 확인

- 기존 AbilitySet 에셋의 공격 ability에 `InputTag.Weapon.LightAttack` 또는 대응 InputTag가 지정되어 있는지 확인
- WeaponAnimProfile의 `PrimaryAttackSpec.InputTag` 채우기
- 장비 UI가 `RequestEquipItemInstanceById`를 사용하도록 전환
- 2-client PIE에서 장비 프로필, 원격 Start/Stop/Jump/Fall/Land 이벤트 확인
- 공중 Motion Matching BlendStack이 다시 누적되지 않는지 확인
