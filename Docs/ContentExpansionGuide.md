# Project J 콘텐츠 확장 구현 가이드

직업·전직의 반복 DA 생성·연결은 에디터 **Tools → Project J → Create Class / Advancement Bundle**에서 시작할 수 있다. 기존 직업/스타일을 선택하고 미리보기 검증 후 미저장 에셋을 생성한다. 공격·애니메이션 등 공유 참조의 범위와 저장 후 runtime 등록 절차는 [제작 도구 가이드](Architecture/Content_Bundle_Authoring_2026-09-19.md)를 따른다.

이 문서는 Project J에 콘텐츠를 추가할 때 현재 C++ 구조를 깨뜨리지 않고 연결하는 방법을 정리한다.

대상 작업:

- 캐릭터 기본 스탯과 공격력 설정
- 새로운 스탯 종류 추가
- 직업과 전직 추가
- 직업·전직·장비 스킬 추가
- 장비 아이템 추가
- 인벤토리 지급·소비·장착 UI 연결

## 1. 현재 데이터 흐름

### 플레이어 런타임 소유권

플레이어의 장기 런타임 상태는 `AProject_JPlayerState`가 소유한다.

```text
PlayerState
  ├─ AbilitySystemComponent
  ├─ AttributeSet
  ├─ InventoryComponent
  └─ EquipmentManagerComponent

PlayerCharacter
  ├─ 입력과 이동
  ├─ EquipmentRuntimeComponent
  └─ PlayerState의 ASC·장비 상태를 Avatar에서 사용
```

NPC는 PlayerState가 없을 수 있으므로 `AProject_JBaseCharacter`의 character-local ASC와 장비 관리자를 사용한다.

### 콘텐츠 조립 흐름

```text
직업 또는 전직
  -> DefaultAttributeSetData
  -> AbilitySet
  -> GameplayAbility / GameplayEffect

Inventory ItemInstance
  -> EquipmentManager
  -> EquipmentRuntime
  -> AbilitySet / GameplayEffect / StatModifier / Mesh / WeaponAnimProfile
```

새 콘텐츠는 가능한 한 C++ 분기문을 늘리지 말고 DataAsset, GameplayEffect, AbilitySet 조합으로 만든다.

---

## 2. 캐릭터 기본 스탯 지정

현재 기본 속성:

- `Health`
- `MaxHealth`
- `Mana`
- `MaxMana`
- `AttackPower`
- `Defense`

관련 코드:

- `Source/Project_JGAS/Public/Project_JAttributeSet.h`
- `Source/Project_JCharacter/Public/Project_JDefaultAttributeSetData.h`
- `Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp`

### 에디터 설정 순서

1. 콘텐츠 브라우저에서 `Miscellaneous > Data Asset`을 선택한다.
2. 클래스는 `Project_JDefaultAttributeSetData`를 선택한다.
3. 예: `DA_Attributes_Warrior`를 생성한다.
4. 다음과 같이 기본값을 입력한다.

```text
MaxHealth   = 300
Health      = 300
MaxMana     = 80
Mana        = 80
AttackPower = 25
Defense     = 12
```

5. 이 DataAsset을 직업 정의의 `Default Attribute Data`에 연결한다.

전직 정의의 `Override Attribute Data`가 설정되어 있으면 전직 데이터가 직업 기본 데이터보다 우선한다.

```text
Advancement.OverrideAttributeData
  > CharacterClass.DefaultAttributeData
  > Character.DefaultAttributeData
  > C++ fallback
```

### 주의 사항

현재 초기화 코드는 기존 값이 0 이하일 때 기본값을 채우는 방식이다. 레벨업이나 전직 시 기존 Health를 강제로 최대치로 회복시키는 정책은 별도 게임 규칙으로 구현해야 한다.

### 레벨 변경 및 어트리뷰트 동기화 API

캐릭터의 레벨을 갱신할 때 `CharacterLevel` 멤버변수에 값을 직접 대입하면 레벨 변화에 따른 GAS Attribute와 UI의 갱신이 누락될 수 있습니다. 반드시 아래 정식 API를 사용해야 합니다.

- **C++ 사용법**:
  ```cpp
  Character->SetCharacterLevel(NewLevel);
  ```
  서버 권한에서 호출한다. 진행 상태 초기화 후에는 ProgressionComponent가 레벨을 최소 1 및 직업의 StartingLevel 이상으로 보정하고 변경 이벤트를 게시한다. 실제 레벨이 바뀌면 `InitializeDefaultAttributes(true)`로 속성을 동기화하며, 플레이어의 UI와 ASC 연결도 갱신한다. 같은 PlayerState를 유지하는 리스폰에서는 캐릭터 기본값으로 진행 레벨을 덮어쓰지 않는다.

`AttackPower`와 `Defense`는 복제되고 장비 보너스도 적용되지만, 현재 `AttackPower`를 최종 피해량으로 변환하는 Damage Execution Calculation은 아직 없다. 실제 전투 공식은 아래 구조로 추가하는 것이 적합하다.

```text
서버 hit 확인
  -> ConfirmedHitGameplayEffect
  -> GameplayEffectExecutionCalculation
  -> Source AttackPower와 Target Defense 계산
  -> Target Health 감소
```

권장 예시 공식:

```text
FinalDamage = max(1, BaseDamage + AttackPower * AttackCoefficient - Defense * DefenseCoefficient)
```

공식은 GameplayAbility나 Character에 하드코딩하지 말고 `UGameplayEffectExecutionCalculation` 파생 클래스에 둔다.

---

## 3. 새로운 스탯 추가

예: `CriticalChance`를 추가할 경우 다음 위치를 함께 수정해야 한다.

### 3.1 AttributeSet

`Project_JAttributeSet.h`에 속성과 RepNotify를 추가한다.

```cpp
UPROPERTY(BlueprintReadOnly, Category = "Attributes", ReplicatedUsing = OnRep_CriticalChance)
FGameplayAttributeData CriticalChance;
ATTRIBUTE_ACCESSORS(UProject_JAttributeSet, CriticalChance)

UFUNCTION()
void OnRep_CriticalChance(const FGameplayAttributeData& OldCriticalChance);
```

`Project_JAttributeSet.cpp`에는 다음 항목이 필요하다.

- `DOREPLIFETIME_CONDITION_NOTIFY`
- `PreAttributeChange` clamp 정책
- `PostGameplayEffectExecute` clamp 정책
- `OnRep_CriticalChance`

### 3.2 기본 스탯 DataAsset

`Project_JDefaultAttributeSetData.h`에 기본값을 추가한다.

```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributes",
	meta = (ClampMin = "0.0", ClampMax = "1.0"))
float CriticalChance = 0.05f;
```

그리고 `AProject_JBaseCharacter::InitializeDefaultAttributes()`에서 초기값을 설정한다.

### 3.3 장비 보너스

장비의 고정 보너스로도 사용할 경우:

1. `EProject_JEquipmentStat`에 항목을 추가한다.
2. `UProject_JEquipmentRuntimeComponent::ApplyEquipmentStatModifiers()`에 attribute 매핑을 추가한다.

```cpp
case EProject_JEquipmentStat::CriticalChance:
	ASC->ApplyModToAttribute(
		UProject_JAttributeSet::GetCriticalChanceAttribute(),
		EGameplayModOp::Additive,
		SignedValue);
	break;
```

복잡한 곱연산, 태그 조건, 세트 효과는 `StatModifiers`보다 GameplayEffect를 사용한다.

### 3.4 UI

AttributeSet delegate를 구독하는 UI ViewModel 또는 바인딩 컴포넌트에 새 속성을 연결한다. UI가 AttributeSet 값을 직접 Tick으로 읽는 구조는 피한다.

---

## 4. 직업 추가

직업은 `UProject_JCharacterClassDefinition` Primary DataAsset으로 구성한다.

관련 코드:

- `Source/Project_JCharacter/Public/CharacterClass/Project_JCharacterClassDefinition.h`
- `Source/Project_JCharacter/Public/AbilitySystem/Project_JAbilitySet.h`
- `Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp`

### 예: 전사 직업

필요 에셋:

```text
DA_Attributes_Warrior
AS_Warrior_Base
DA_Class_Warrior
```

`DA_Class_Warrior` 설정:

```text
ClassId              = Warrior
StartingLevel         = 1
DefaultAttributeData  = DA_Attributes_Warrior
AbilitySets           = [AS_Warrior_Base]
```

`AS_Warrior_Base`에는 직업이 기본으로 소유할 Ability와 Effect를 넣는다.

```text
GrantedAbilityEntries
  - GA_Warrior_LightAttack / InputTag.Weapon.LightAttack
  - GA_Warrior_HeavyAttack / InputTag.Weapon.HeavyAttack
  - GA_Warrior_Dash        / InputTag.Skill.Dash

GrantedEffectEntries
  - GE_Warrior_BasePassive
```

> Current rule: use only `GrantedAbilityEntries` and `GrantedEffectEntries`.
> `GrantedGameplayAbilities` and `GrantedGameplayEffects` no longer exist.

신규 스킬 입력은 반드시 `GrantedAbilityEntries.InputTag`를 사용한다. `GrantedGameplayAbilities`는 InputTag를 붙일 수 없는 레거시 호환 배열이므로 새 콘텐츠에는 가급적 사용하지 않는다.

### Character에 연결

플레이어 Character Blueprint의 `Character Class Defaults > Character Class Definition`에 `DA_Class_Warrior`를 연결한다.

서버는 기존 `InitializeCharacterClassDefinition()` 또는 registry의 `InitializeCharacterClassById()`로 기본 직업을 최초 지정한다. 동일 직업의 재요청은 중복 부여하지 않으며 이미 지정된 직업을 다른 직업으로 바꾸는 API는 아니다.

직업·전직·레벨·능력 부여 수명은 `ProgressionComponent`가 소유한다. 플레이어는 PlayerState, NPC는 Character에 위치한다. BP 직업/전직 값은 최초 초기화용이며 리스폰의 기본값으로 지속 상태를 덮어쓰지 않는다. PlayerState의 공개 ClassId/Level은 진행 변경을 따라 갱신된다.

저장 어댑터에는 `CaptureSnapshot()`의 ID·레벨·전직 이력을 전달하고, 새 소유자 초기화 전에 `RestoreSnapshot()`으로 검증·복원한다. DB 저장과 handover 연결은 별도 구현 범위다. 자세한 사용 조건은 [확장 기반 문서](Architecture/Extension_Foundation_2026-09-19.md)를 따른다.

Character Blueprint 변수에 클라이언트가 직접 직업 DataAsset을 쓰게 만들면 안 된다.

---

## 5. 전직 추가

전직은 `UProject_JCharacterAdvancementDefinition` Primary DataAsset으로 구성한다.

예:

```text
DA_Advancement_Berserker
```

설정:

```text
AdvancementId        = Berserker
BaseClass             = DA_Class_Warrior
RequiredLevel         = 20
RequiredAdvancementIds = 모두 취득해야 하는 선행 전직 ID
ExclusiveBranch       = 동시에 활성화할 수 없는 분기의 공통 키 (선택)
RequiredTags          = 요구 조건 태그
BlockedTags           = 금지 조건 태그
OverrideAttributeData = DA_Attributes_Berserker
AdditionalAbilitySets = [AS_Berserker]
AbilityGrantPolicy    = Additive 또는 ReplacePreviousAdvancement
```

### 전직 실행

서버에서 Character의 다음 함수를 호출한다.

```cpp
if (Character->CanApplyAdvancementDefinition(AdvancementDefinition))
{
	Character->ApplyAdvancementDefinition(AdvancementDefinition);
}
```

`ApplyAdvancementDefinition()`은 다음을 처리한다.

- 레벨 확인
- BaseClass 호환 확인
- RequiredTags와 BlockedTags 확인
- 선행 전직 취득 이력, 배타 분기, 반복 취득 및 변경 중 재진입 검사
- Additive는 누적 부여, ReplacePreviousAdvancement는 전직 부여 묶음 전체 회수 후 새 묶음 부여
- 직업·장비가 공유하는 스타일 능력은 마지막 제공자가 해제할 때 회수
- 현재 전직·취득 이력·revision 게시

`OverrideAttributeData`는 이후 명시적인 속성 초기화가 참조한다. 전직 적용 순간 체력/마나를 재충전하는 동작은 자동 수행하지 않는다.

### 권장 서버 흐름

```text
클라이언트 전직 요청
  -> 서버가 NPC/퀘스트/아이템/레벨 조건 조회
  -> 서버가 AdvancementDefinition 결정
  -> CanApplyAdvancementDefinition
  -> ApplyAdvancementDefinition
  -> PlayerState 공개 ClassId/Level 갱신
  -> backend 또는 SaveGame 저장
```

클라이언트가 임의의 DataAsset 경로를 전송하게 하지 말고 `AdvancementId`만 요청한 뒤 서버 테이블에서 정의를 찾는 방식이 안전하다.

현재 직업·전직·레벨·revision은 ProgressionComponent의 public 상태로 복제하고, 전체 전직 취득 이력은 owner-only로 복제한다. 공개 ClassId/Level도 PlayerState에서 자동 갱신한다. 서버가 정의를 변경하는 도중 다른 변경 요청은 거절한다.

---

## 6. 스킬 추가 및 직업·전직·장비에 연결

### 6.1 Ability 생성

1. `UGameplayAbility` 기반 Blueprint 또는 C++ Ability를 만든다.
2. 활성 조건, 비용, 쿨다운, GameplayTag를 설정한다.
3. 입력이 필요한 Ability는 사용할 `InputTag`를 결정한다.

태그 의미:

```text
InputTag.*  = 플레이어 입력 의도
Event.*     = Montage/Ability 내부 gameplay event
Ability.*   = Ability 종류와 상태 식별
State.*     = 현재 캐릭터 상태
```

### 6.2 AbilitySet 또는 스타일 내부 작성

한 스타일에만 속하는 능력/효과는 `CombatStyle.InlineAbilities` / `InlineEffects`에서 직접 작성할 수 있다. 다른 콘텐츠와 공유할 묶음은 기존 AbilitySet을 사용한다. 두 경로의 입력 태그 중복은 검증 오류다. 콤보 없는 스타일은 `bUsesCombo=false`, 무기 애니메이션이 필요 없으면 `bRequiresWeaponAnimation=false`를 선택한다.

`bDeriveAttackCatalog=true`이면 ComboDefinition과 AdditionalAttacks에서 공격 목록을 생성하므로 별도 AttackSet 연결은 비운다. 플레이 중 정의 변경은 지원하지 않으며 에디터 수정 후 PIE를 다시 시작한다.

공유 AbilitySet을 작성하는 경우:

`Project_JAbilitySet` DataAsset을 만들고 `GrantedAbilityEntries`에 추가한다.

```text
Ability      = GA_Berserker_Whirlwind
AbilityLevel = 1
InputTag     = InputTag.Skill.Whirlwind
InputID      = -1
```

### 6.3 연결 대상 선택

- 직업 기본 스킬: `CharacterClassDefinition.AbilitySets`
- 전직 스킬: `AdvancementDefinition.AdditionalAbilitySets`
- 무기/장비 전용 스킬: `EquipmentItemDefinition.AbilitySet`
- 짧은 버프나 상태: Ability에서 GameplayEffect 적용

같은 InputTag를 여러 Ability가 동시에 소유하지 않도록 한다.

---

## 7. 장비 추가

장비는 `UProject_JEquipmentItemDefinition` Primary DataAsset으로 만든다.

관련 코드:

- `Source/Project_JCharacter/Public/Equipment/Project_JEquipmentItemDefinition.h`
- `Source/Project_JCharacter/Public/Components/Project_JEquipmentManagerComponent.h`
- `Source/Project_JCharacter/Private/Components/Project_JEquipmentRuntimeComponent.cpp`

### 예: 철검

```text
DA_Item_IronSword
```

설정 예:

```text
EquipmentSlot       = Weapon
EquipmentMesh       = SK_IronSword
AttachSocketName    = weapon_r
MaxDrawDistance     = 필요 시 지정
bCastDynamicShadow  = true
AbilitySet          = AS_IronSword
EquipmentEffects    = [GE_IronSword_Stats]
StatApplicationPolicy = GameplayEffectsOnly
WeaponAnimProfile   = WAP_OneHandSword
```

### 장비 스탯 적용 방식

#### GameplayEffect 권장

`EquipmentEffects`에 무한 지속 GameplayEffect를 넣는다.

장점:

- 곱연산과 조건부 modifier 사용 가능
- 태그 기반 세트 효과 확장 가능
- GAS 디버거에서 추적 가능
- 장착 해제 시 ActiveEffectHandle로 제거 가능

#### StatModifiers

단순 고정 합산값에 적합하다.

```text
StatModifiers
  - AttackPower +10
  - Defense +3
```

`StatApplicationPolicy`:

- `GameplayEffectsThenStatModifiers`: Effect가 있으면 Effect를 사용하고, 없으면 StatModifier fallback
- `GameplayEffectsOnly`: GameplayEffect만 사용
- `StatModifiersOnly`: 고정 StatModifier만 사용

같은 보너스를 Effect와 StatModifier 양쪽에 중복 입력하지 않는다.

### 무기 애니메이션

무기는 `WeaponAnimProfile`에 다음 내용을 연결한다.

- 공격 Montage
- 공격 InputTag
- 무기용 Motion Matching 또는 전투 애니메이션 설정

기본 공격은 `InputTag.Weapon.LightAttack` 경로를 우선 사용한다.

---

## 8. 인벤토리 아이템 지급

플레이어 인벤토리는 PlayerState의 `UProject_JInventoryComponent`가 소유하며 owner-only FastArray로 복제된다.

아이템 지급은 서버에서만 수행한다.

```cpp
AProject_JPlayerState* ProjectJPS = PlayerController->GetPlayerState<AProject_JPlayerState>();
if (!ProjectJPS || !ProjectJPS->HasAuthority())
{
	return;
}

UProject_JInventoryComponent* Inventory = ProjectJPS->GetInventoryComponent();
if (Inventory)
{
	const FProject_JItemInstanceData NewItem =
		Inventory->AddItemDefinition(ItemDefinition, 1, ItemLevel);
}
```

Blueprint에서는 서버 권한 이벤트에서:

```text
Get PlayerState
  -> Get Inventory Component
  -> Add Item Definition
```

인벤토리 아이템은 공통 기본 클래스인 `UProject_JItemDefinition`을 상속하여 확장합니다. 장비 아이템(`UProject_JEquipmentItemDefinition`)은 이를 상속받아 구현되어 있으며, 소비 아이템, 재료, 퀘스트 아이템 등의 새로운 아이템 분류가 필요할 경우 역시 `UProject_JItemDefinition`을 상속받는 새로운 데이터 에셋 정의를 만들어 쉽게 추가할 수 있습니다.

### 현재 지원 연산

- `AddItemDefinition`
- `RemoveItemInstance`
- `SetItemStackCount`
- `AddItemStackCount`
- `ConsumeItemStack`
- `SetItemInstanceLocked`
- `FindItemInstance`

장착 중인 아이템은 잠기므로 제거·소비·이동할 수 없다.

---

## 9. 장비 장착 UI 연결

UI는 Item Definition 전체를 서버로 보내지 않고 `InstanceId`만 요청한다.

```text
인벤토리 슬롯 클릭
  -> 슬롯이 가진 ItemInstance.InstanceId
  -> EquipmentManager.RequestEquipItemInstanceById
  -> 서버가 Inventory에서 authoritative instance 재조회
  -> 소유권, 잠금, 슬롯 검증
  -> 장착 상태 FastArray 복제
```

Blueprint 권장 호출:

```text
Get PlayerState
  -> Get Equipment Manager Component
  -> Request Equip Item Instance By Id(InstanceId)
```

장착 해제:

```text
Request Unequip Slot(EquipmentSlot)
```

사용하지 말아야 할 클라이언트 경로:

- `RequestEquipItem(ItemDef)` — deprecated
- 클라이언트에서 `EquipItem()` 직접 호출
- 클라이언트가 만든 `FProject_JItemInstanceData`를 신뢰하는 서버 로직

### UI 갱신 이벤트

인벤토리:

- `OnItemAdded`
- `OnItemChanged`
- `OnItemRemoved`

장비:

- `OnEquipmentEquipped`
- `OnEquipmentUnequipped`

UI는 매 프레임 전체 목록을 다시 읽지 말고 위 이벤트에서 ViewModel 또는 슬롯 목록을 갱신한다.

---

## 10. 소비 아이템의 간단한 구현 방향

현재 인벤토리는 장비 정의 중심이다. 임시 테스트라면 서버에서 `ConsumeItemStack()` 후 GameplayEffect를 적용할 수 있지만, 본 구현은 다음 구조가 적합하다.

```text
UProject_JItemDefinition
  ├─ ItemId
  ├─ DisplayName / Icon
  ├─ MaxStack
  └─ ItemType

UProject_JEquipmentItemDefinition : UProject_JItemDefinition
UProject_JConsumableItemDefinition : UProject_JItemDefinition
  └─ UseEffect
```

소비 요청:

```text
Client: RequestUseItem(InstanceId)
Server:
  1. authoritative instance 조회
  2. 잠금/수량/쿨다운/사용 조건 검증
  3. UseEffect 적용
  4. 성공한 경우에만 ConsumeItemStack
```

Effect 적용 실패 전에 수량부터 줄이지 않는다.

---

## 11. 저장과 백엔드 연결 시 경계

현재 FastArray는 접속 중 복제 상태이며 영속 저장소가 아니다.

저장 대상:

- CharacterId
- ClassId와 AdvancementId
- Level과 경험치
- 기본/성장 스탯
- Inventory ItemInstance 목록
- 장착 InstanceId와 Slot

권장 저장 흐름:

```text
게임플레이 서버 operation 성공
  -> 메모리 authoritative 상태 변경
  -> TransactionId / RequestId와 함께 persistence 요청
  -> 재접속 시 저장 데이터를 PlayerState 컴포넌트로 복원
```

클라이언트 UI 이벤트를 저장 트리거로 사용하지 않는다.

---

## 12. 기능별 완료 체크리스트

### 스탯

- AttributeSet replication과 RepNotify가 있는가?
- 서버에서만 영구 값을 변경하는가?
- clamp 범위가 정의되어 있는가?
- 장비·버프·전직 중복 적용을 테스트했는가?
- UI가 delegate 기반으로 갱신되는가?

### 직업과 전직

- ClassId와 AdvancementId가 고유한가?
- AbilitySet InputTag가 중복되지 않는가?
- 서버가 요구 레벨과 태그를 검증하는가?
- 전직 후 PlayerState 공개 스냅샷을 갱신하는가?
- 재접속 시 동일 상태가 복원되는가?

### 장비

- 올바른 EquipmentSlot이 지정되었는가?
- GameplayEffect와 StatModifier가 중복되지 않는가?
- 무기 AbilitySet과 WeaponAnimProfile이 연결되었는가?
- 장착 중 Inventory instance가 잠기는가?
- 2-client PIE에서 스탯·외형·애니메이션이 일치하는가?

### 인벤토리

- 지급·소비가 서버에서 실행되는가?
- UI 요청은 InstanceId만 보내는가?
- 장착 아이템의 제거·소비가 차단되는가?
- owner-only 상태가 다른 클라이언트에 노출되지 않는가?
- 실패한 operation에서 수량이나 장비 상태가 반쯤 변경되지 않는가?

---

## 13. 권장 구현 순서

1. Damage Execution Calculation과 실제 Health 감소
2. 기본 직업 선택용 서버 API
3. 직업·전직 공개 스냅샷과 영속 저장 계약
4. 공통 ItemDefinition과 소비 아이템 분리
5. 인벤토리·장비 ViewModel 및 UI
6. 레벨업과 스탯 성장 규칙
7. 세트 장비, 랜덤 옵션, 강화

이 순서라면 현재 PlayerState 소유권, GAS, FastArray, EquipmentRuntime 구조를 유지하면서 기능을 확장할 수 있다.
