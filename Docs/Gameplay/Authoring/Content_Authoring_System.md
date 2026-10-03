# 직업·전직·콘텐츠 데이터 제작 통합 가이드

정리일: 2026-10-03. 스탯·직업·전직·스킬·장비 확장, DA 역할과 제작 도구를 한 문서에서 찾는다. 기존 세 가이드의 코드 진입점·상세 순서·예외·태그·검수 내용을 유지한다.

## 작업 순서

1. 첫 절의 데이터 역할로 영구 진행, 스타일 조립, 공격 순서, 외형 설정의 소유자를 고른다.
2. 두 번째 절에서 추가할 콘텐츠 종류의 코드·DA·에디터 작업과 점검 절차를 따른다.
3. 반복 직업·전직 연결은 세 번째 절의 제작 도구로 미리보기·검증·생성한 뒤 저장·런타임 등록한다.

공유 DA 정의에 플레이 중 진행 상태·부여 핸들을 저장하지 않는다. 내부 능력과 자동 공격 카탈로그는 해당 계약의 transient 실행 데이터다. 데이터 옵션이 새로운 투사체·채널링·소환 실행기를 자동 구현하는 것은 아니다.

공격·콤보의 무기별 작성 예시는 [대검 작성](../../Combat/Authoring/GreatswordCombatAuthoringGuide.md), 현재 손 접촉·무기 부착은 [파지 통합](../../Animation/Authoring/Weapon_Hand_Contact_System.md), 스킬·탈것 실행 구조는 [게임플레이 목차](../README.md)를 함께 읽는다. 각 원문의 기준 시점과 구현·보류 범위를 유지한다.

---

<a id="data"></a>
## 데이터 역할·조립·태그 빠른 참조

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Gameplay/Authoring/DataAssetQuickReference.md)

<a id="data-project-j-데이터-에셋-빠른-참조"></a>
원제: **Project J 데이터 에셋 빠른 참조**

2026-09-19 기준. DA 구조를 유지하면서 전용 설정은 내부 작성, 공유 설정은 별도 에셋 참조로 구성한다. 새 직업·전직의 반복 연결은 [에디터 제작 도구](Content_Authoring_System.md#tool)를 사용할 수 있다. 생성·연결 후 저장과 runtime 목록 등록은 별도 단계다.

<a id="data-전투직업"></a>
### 전투/직업

- **CharacterClassDefinition**: 직업의 영구 정체성이다. 기본 전투 스타일, 기본 능력 세트, 성장 시작값을 지정한다.
- **CharacterAdvancementDefinition**: 기본 직업, 선행 전직 ID, 배타 분기, 레벨/태그 조건을 지정한다. 전직 능력은 Additive 또는 ReplacePreviousAdvancement 정책으로 부여하며, 전투 스타일을 덮어쓸 수 있다.
- **CombatStyleDefinition**: 한 전투 스타일의 조립 루트다. 애니메이션, 콤보, 공격 목록, 커맨드, 전투 Ability Set을 한데 연결한다.
- **AbilitySet**: 재사용할 GA/GE 묶음이다. 한 스타일에서만 사용하는 능력/효과는 CombatStyle의 `InlineAbilities`/`InlineEffects`에 직접 작성할 수 있다. 동일 능력을 공유 목록과 내부 목록에 중복 입력하지 않는다.

진행 상태와 실제 부여 핸들은 DA에 저장하지 않는다. 플레이어는 PlayerState의 ProgressionComponent/ASC, NPC는 캐릭터 측 소유자가 관리한다. 전직의 `bOverrideEquippedGameplay`를 켜면 전직의 게임플레이 스타일과 장비의 애니메이션 스타일을 조합한다. 이 옵션 자체가 모든 장비 능력을 회수하는 것은 아니다. [상세 계약](../../Architecture/Extensions/Extension_Foundation_2026-09-19.md)

<a id="data-공격입력"></a>
### 공격/입력

- **AttackDefinition**: 게임플레이적으로 구분되는 공격 한 번이다. 몽타주, 이동 정책, 타격 판정, 서버 피해 GE를 소유한다.
- **AttackSet**: CombatStyle이 사용할 공격 카탈로그다. 기존 별도 DA 참조를 유지할 수 있다. `bDeriveAttackCatalog`를 켜면 ComboDefinition과 `AdditionalAttacks`로 실행 목록을 생성하므로 별도 AttackSet은 지정하지 않는다. 서로 다른 공격이 같은 Attack Tag를 사용하면 검증에 실패한다.
- **ComboDefinition**: 공격 순서와 입력 전이 그래프다. 노드는 AttackDefinition을 참조하며 자체 몽타주나 피해 데이터를 갖지 않는다.
- **CombatCommandSet**: `LMB → RMB → LMB` 같은 입력 시퀀스를 특정 GAS 입력 태그로 해석한다. 평타 콤보의 순서와는 별개다.

콤보가 필요 없는 스타일은 `bUsesCombo=false`, 무기 애니메이션이 필요 없는 스타일은 `bRequiresWeaponAnimation=false`를 선택할 수 있다. Attack의 `bMontageDriven=false`는 데이터 작성 옵션이며, 새로운 투사체·채널링·소환 실행기를 자동 구현하지 않는다. 기존 근접 콤보 실행기는 여전히 몽타주를 요구한다.

내부 능력 묶음과 자동 공격 카탈로그는 공유 스타일에서 생성·재사용하는 transient 실행 데이터다. 저장할 DA를 추가로 만드는 기능이 아니며, 플레이 중 정의를 수정하지 않고 변경 후 PIE를 다시 시작한다.

<a id="data-애니메이션표시"></a>
### 애니메이션/표시

- **CharacterAnimProfile**: 휴머노이드 공통 Locomotion/Combat 애니메이션 설정이다. 무기나 직업 전투 데이터는 넣지 않는다.
- **LocomotionProfile**: 비전투 이동 속도, 회전, Motion Matching, 발 배치 관련 설정을 가진다.
- **WeaponAnimProfile**: 한 전투 스타일의 무기 자세, 발도 몽타주, Linked Anim Layer를 가진다. 외형·콤보·피해는 넣지 않는다.
- **WeaponPresentationProfile**: 전투 중 표시할 무기 Actor와 손 소켓을 가진다. 스킨 교체는 이 에셋만 바꿔도 된다.

<a id="data-장비"></a>
### 장비

- **EquipmentItemDefinition**: 실제 장착 아이템 하나다. 슬롯, 아이템 정보, 장비 스탯/GE, CombatStyle, WeaponPresentationProfile을 지정한다.
- **Equipment Profile/Starting Equipment DA**: 캐릭터 시작 장비 또는 프리셋 목록이다. Weapon 슬롯에 EquipmentItemDefinition을 지정해 실제 장착 상태를 만든다.

<a id="data-태그의-역할"></a>
### 태그의 역할

- **InputTag.***: 플레이어가 누른 입력의 의미다. 예: `InputTag.Weapon.LightAttack`.
- **Combo.***: ComboDefinition 내부 노드 주소다. 예: `Combo.Greatsword.Light.1`.
- **Attack.***: AttackDefinition의 공격 식별자다. 예: `Attack.Greatsword.Light.1`.
- **CombatStyle.***: 전투 스타일 식별자다. 예: `CombatStyle.Greatsword`.

---

<a id="expansion"></a>
## 스탯·직업·스킬·장비 확장 상세 절차

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Gameplay/Authoring/ContentExpansionGuide.md)

<a id="expansion-project-j-콘텐츠-확장-구현-가이드"></a>
원제: **Project J 콘텐츠 확장 구현 가이드**

직업·전직의 반복 DA 생성·연결은 에디터 **Tools → Project J → Create Class / Advancement Bundle**에서 시작할 수 있다. 기존 직업/스타일을 선택하고 미리보기 검증 후 미저장 에셋을 생성한다. 공격·애니메이션 등 공유 참조의 범위와 저장 후 runtime 등록 절차는 [제작 도구 가이드](Content_Authoring_System.md#tool)를 따른다.

이 문서는 Project J에 콘텐츠를 추가할 때 현재 C++ 구조를 깨뜨리지 않고 연결하는 방법을 정리한다.

대상 작업:

- 캐릭터 기본 스탯과 공격력 설정
- 새로운 스탯 종류 추가
- 직업과 전직 추가
- 직업·전직·장비 스킬 추가
- 장비 아이템 추가
- 모듈러 캐릭터/의상 에셋 및 본 물리(`RigidBody`) 세팅: [모듈러 캐릭터 및 의상 파이프라인 가이드](ModularCharacterEquipmentGuide.md)
- 인벤토리 지급·소비·장착 UI 연결

<a id="expansion-1-현재-데이터-흐름"></a>
### 1. 현재 데이터 흐름

<a id="expansion-플레이어-런타임-소유권"></a>
#### 플레이어 런타임 소유권

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

<a id="expansion-콘텐츠-조립-흐름"></a>
#### 콘텐츠 조립 흐름

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

<a id="expansion-2-캐릭터-기본-스탯-지정"></a>
### 2. 캐릭터 기본 스탯 지정

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

<a id="expansion-에디터-설정-순서"></a>
#### 에디터 설정 순서

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

<a id="expansion-주의-사항"></a>
#### 주의 사항

현재 초기화 코드는 기존 값이 0 이하일 때 기본값을 채우는 방식이다. 레벨업이나 전직 시 기존 Health를 강제로 최대치로 회복시키는 정책은 별도 게임 규칙으로 구현해야 한다.

<a id="expansion-레벨-변경-및-어트리뷰트-동기화-api"></a>
#### 레벨 변경 및 어트리뷰트 동기화 API

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

<a id="expansion-3-새로운-스탯-추가"></a>
### 3. 새로운 스탯 추가

예: `CriticalChance`를 추가할 경우 다음 위치를 함께 수정해야 한다.

<a id="expansion-31-attributeset"></a>
#### 3.1 AttributeSet

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

<a id="expansion-32-기본-스탯-dataasset"></a>
#### 3.2 기본 스탯 DataAsset

`Project_JDefaultAttributeSetData.h`에 기본값을 추가한다.

```cpp
UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Attributes",
	meta = (ClampMin = "0.0", ClampMax = "1.0"))
float CriticalChance = 0.05f;
```

그리고 `AProject_JBaseCharacter::InitializeDefaultAttributes()`에서 초기값을 설정한다.

<a id="expansion-33-장비-보너스"></a>
#### 3.3 장비 보너스

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

<a id="expansion-34-ui"></a>
#### 3.4 UI

AttributeSet delegate를 구독하는 UI ViewModel 또는 바인딩 컴포넌트에 새 속성을 연결한다. UI가 AttributeSet 값을 직접 Tick으로 읽는 구조는 피한다.

---

<a id="expansion-4-직업-추가"></a>
### 4. 직업 추가

직업은 `UProject_JCharacterClassDefinition` Primary DataAsset으로 구성한다.

관련 코드:

- `Source/Project_JCharacter/Public/CharacterClass/Project_JCharacterClassDefinition.h`
- `Source/Project_JCharacter/Public/AbilitySystem/Project_JAbilitySet.h`
- `Source/Project_JCharacter/Private/Project_JBaseCharacter.cpp`

<a id="expansion-예-전사-직업"></a>
#### 예: 전사 직업

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

<a id="expansion-character에-연결"></a>
#### Character에 연결

플레이어 Character Blueprint의 `Character Class Defaults > Character Class Definition`에 `DA_Class_Warrior`를 연결한다.

서버는 기존 `InitializeCharacterClassDefinition()` 또는 registry의 `InitializeCharacterClassById()`로 기본 직업을 최초 지정한다. 동일 직업의 재요청은 중복 부여하지 않으며 이미 지정된 직업을 다른 직업으로 바꾸는 API는 아니다.

직업·전직·레벨·능력 부여 수명은 `ProgressionComponent`가 소유한다. 플레이어는 PlayerState, NPC는 Character에 위치한다. BP 직업/전직 값은 최초 초기화용이며 리스폰의 기본값으로 지속 상태를 덮어쓰지 않는다. PlayerState의 공개 ClassId/Level은 진행 변경을 따라 갱신된다.

저장 어댑터에는 `CaptureSnapshot()`의 ID·레벨·전직 이력을 전달하고, 새 소유자 초기화 전에 `RestoreSnapshot()`으로 검증·복원한다. DB 저장과 handover 연결은 별도 구현 범위다. 자세한 사용 조건은 [확장 기반 문서](../../Architecture/Extensions/Extension_Foundation_2026-09-19.md)를 따른다.

Character Blueprint 변수에 클라이언트가 직접 직업 DataAsset을 쓰게 만들면 안 된다.

---

<a id="expansion-5-전직-추가"></a>
### 5. 전직 추가

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

<a id="expansion-전직-실행"></a>
#### 전직 실행

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

<a id="expansion-권장-서버-흐름"></a>
#### 권장 서버 흐름

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

<a id="expansion-6-스킬-추가-및-직업전직장비에-연결"></a>
### 6. 스킬 추가 및 직업·전직·장비에 연결

<a id="expansion-61-ability-생성"></a>
#### 6.1 Ability 생성

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

<a id="expansion-62-abilityset-또는-스타일-내부-작성"></a>
#### 6.2 AbilitySet 또는 스타일 내부 작성

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

<a id="expansion-63-연결-대상-선택"></a>
#### 6.3 연결 대상 선택

- 직업 기본 스킬: `CharacterClassDefinition.AbilitySets`
- 전직 스킬: `AdvancementDefinition.AdditionalAbilitySets`
- 무기/장비 전용 스킬: `EquipmentItemDefinition.AbilitySet`
- 짧은 버프나 상태: Ability에서 GameplayEffect 적용

같은 InputTag를 여러 Ability가 동시에 소유하지 않도록 한다.

---

<a id="expansion-7-장비-추가"></a>
### 7. 장비 추가

장비는 `UProject_JEquipmentItemDefinition` Primary DataAsset으로 만든다.

관련 코드:

- `Source/Project_JCharacter/Public/Equipment/Project_JEquipmentItemDefinition.h`
- `Source/Project_JCharacter/Public/Components/Project_JEquipmentManagerComponent.h`
- `Source/Project_JCharacter/Private/Components/Project_JEquipmentRuntimeComponent.cpp`

<a id="expansion-예-철검"></a>
#### 예: 철검

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

<a id="expansion-장비-스탯-적용-방식"></a>
#### 장비 스탯 적용 방식

<a id="expansion-gameplayeffect-권장"></a>
##### GameplayEffect 권장

`EquipmentEffects`에 무한 지속 GameplayEffect를 넣는다.

장점:

- 곱연산과 조건부 modifier 사용 가능
- 태그 기반 세트 효과 확장 가능
- GAS 디버거에서 추적 가능
- 장착 해제 시 ActiveEffectHandle로 제거 가능

<a id="expansion-statmodifiers"></a>
##### StatModifiers

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

<a id="expansion-무기-애니메이션"></a>
#### 무기 애니메이션

무기는 `WeaponAnimProfile`에 다음 내용을 연결한다.

- 공격 Montage
- 공격 InputTag
- 무기용 Motion Matching 또는 전투 애니메이션 설정

기본 공격은 `InputTag.Weapon.LightAttack` 경로를 우선 사용한다.

---

<a id="expansion-8-인벤토리-아이템-지급"></a>
### 8. 인벤토리 아이템 지급

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

<a id="expansion-현재-지원-연산"></a>
#### 현재 지원 연산

- `AddItemDefinition`
- `RemoveItemInstance`
- `SetItemStackCount`
- `AddItemStackCount`
- `ConsumeItemStack`
- `SetItemInstanceLocked`
- `FindItemInstance`

장착 중인 아이템은 잠기므로 제거·소비·이동할 수 없다.

---

<a id="expansion-9-장비-장착-ui-연결"></a>
### 9. 장비 장착 UI 연결

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

<a id="expansion-ui-갱신-이벤트"></a>
#### UI 갱신 이벤트

인벤토리:

- `OnItemAdded`
- `OnItemChanged`
- `OnItemRemoved`

장비:

- `OnEquipmentEquipped`
- `OnEquipmentUnequipped`

UI는 매 프레임 전체 목록을 다시 읽지 말고 위 이벤트에서 ViewModel 또는 슬롯 목록을 갱신한다.

---

<a id="expansion-10-소비-아이템의-간단한-구현-방향"></a>
### 10. 소비 아이템의 간단한 구현 방향

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

<a id="expansion-11-저장과-백엔드-연결-시-경계"></a>
### 11. 저장과 백엔드 연결 시 경계

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

<a id="expansion-12-기능별-완료-체크리스트"></a>
### 12. 기능별 완료 체크리스트

<a id="expansion-스탯"></a>
#### 스탯

- AttributeSet replication과 RepNotify가 있는가?
- 서버에서만 영구 값을 변경하는가?
- clamp 범위가 정의되어 있는가?
- 장비·버프·전직 중복 적용을 테스트했는가?
- UI가 delegate 기반으로 갱신되는가?

<a id="expansion-직업과-전직"></a>
#### 직업과 전직

- ClassId와 AdvancementId가 고유한가?
- AbilitySet InputTag가 중복되지 않는가?
- 서버가 요구 레벨과 태그를 검증하는가?
- 전직 후 PlayerState 공개 스냅샷을 갱신하는가?
- 재접속 시 동일 상태가 복원되는가?

<a id="expansion-장비"></a>
#### 장비

- 올바른 EquipmentSlot이 지정되었는가?
- GameplayEffect와 StatModifier가 중복되지 않는가?
- 무기 AbilitySet과 WeaponAnimProfile이 연결되었는가?
- 장착 중 Inventory instance가 잠기는가?
- 2-client PIE에서 스탯·외형·애니메이션이 일치하는가?

<a id="expansion-인벤토리"></a>
#### 인벤토리

- 지급·소비가 서버에서 실행되는가?
- UI 요청은 InstanceId만 보내는가?
- 장착 아이템의 제거·소비가 차단되는가?
- owner-only 상태가 다른 클라이언트에 노출되지 않는가?
- 실패한 operation에서 수량이나 장비 상태가 반쯤 변경되지 않는가?

---

<a id="expansion-13-권장-구현-순서"></a>
### 13. 권장 구현 순서

1. Damage Execution Calculation과 실제 Health 감소
2. 기본 직업 선택용 서버 API
3. 직업·전직 공개 스냅샷과 영속 저장 계약
4. 공통 ItemDefinition과 소비 아이템 분리
5. 인벤토리·장비 ViewModel 및 UI
6. 레벨업과 스탯 성장 규칙
7. 세트 장비, 랜덤 옵션, 강화

이 순서라면 현재 PlayerState 소유권, GAS, FastArray, EquipmentRuntime 구조를 유지하면서 기능을 확장할 수 있다.

---

<a id="tool"></a>
## 직업·전직 묶음 제작 도구

[보존한 전체 원문](../../Archive/SourceDocuments/2026-10-03/Gameplay/Authoring/Content_Bundle_Authoring_2026-09-19.md)

<a id="tool-직업전직-제작-도구"></a>
원제: **직업·전직 제작 도구**

<a id="tool-목적과-범위"></a>
### 목적과 범위

DA 기반 런타임은 유지하고, 기존 직업을 참고해 새 직업/전직의 루트·전투 스타일·콤보를 만드는 반복 작업을 줄인다. `Project_JCharacterEditor` 전용 도구이며 게임 모듈에는 에디터 의존성, Tick, 비동기 작업을 추가하지 않는다.

이 작업에서는 실제 프로젝트 에셋을 생성하거나 저장하지 않았다. 아래 기능은 사용자가 에디터에서 실행하는 제작 도구다.

<a id="tool-사용-방법"></a>
### 사용 방법

1. 에디터의 **Tools → Project J → Create Class / Advancement Bundle**을 연다.
2. `Kind`에서 직업 또는 전직을 선택한다. 기존 `Base Class`, 새 `Identifier`, `/Game/DataAssetSets` 아래 저장 폴더를 지정한다.
3. 필요하면 `Style Template`을 지정한다. 생략하면 기본 직업의 스타일을 사용한다. 전직은 선택적으로 같은 기본 직업에 속한 `Previous Advancement`를 지정할 수 있다.
4. **Preview & Validate**로 생성 예정 경로와 유효성을 확인한다. 설정 변경 시 미리보기 승인은 해제된다.
5. **Create Unsaved Assets**를 누르면 새 에셋들이 생성·연결되고 Content Browser에서 선택된다. 자동 저장하지 않는다.
6. 내용을 검토한 뒤 Unreal 표준 Save 명령으로 저장한다. 결과의 **Copy Registry Entry**를 사용해 표시된 줄을 `Config/DefaultGame.ini`의 `[/Script/Project_JCharacter.Project_JCharacterDataSubsystem]` 섹션에 추가한다.

직업/전직 registry 등록은 이 도구가 자동으로 수정·저장하지 않는다. 에셋 저장과 registry 등록을 마쳐야 ID 조회 경로로 사용할 수 있다. 게임 시작 시 registry를 채우므로 플레이 중 변경하지 않는다.

<a id="tool-생성-및-공유-경계"></a>
### 생성 및 공유 경계

```mermaid
flowchart LR
    Template[기존 직업과 스타일 선택] --> Preview[Preview: 메모리 초안 검증]
    Preview --> Create[Create: 새 루트·스타일·콤보 연결]
    Create --> Review[공유 참조와 전직 정책 검토]
    Review --> Save[사용자: 표준 Save]
    Save --> Register[사용자: 등록 줄을 DefaultGame.ini에 추가]
    Register --> Runtime[다음 게임 시작: ID로 조회]
```

| 구성 | 동작 |
|---|---|
| 직업 루트 | 선택한 기본 직업의 설정을 복제하고 새 ClassId와 새 스타일을 연결 |
| 전직 루트 | 새로운 전직을 생성하고 기존 기본 직업과 새 스타일을 연결 |
| 전투 스타일 | 별도 복제, 런타임 캐시 초기화, 공격 목록 자동 생성 모드 사용 |
| 콤보 | 원본에 있을 때만 별도 복제하여 새 스타일에 연결 |
| 공격·몽타주·능력 묶음·애니메이션 프로필·표현·능력치 | 원본 참조 공유, 자동 복제하지 않음 |

기존 AttackSet의 콤보 외 공격은 `AdditionalAttacks`로 보존한다. 콤보에 포함된 공격은 콤보에서 수집하므로 목록을 이중 유지하지 않는다. 새 콤보 노드를 수정해도 원본 콤보는 바뀌지 않지만, 공유 공격 DA 자체를 편집하면 그 공격을 사용하는 다른 콘텐츠에도 영향을 준다. 공격 동작을 독립적으로 변경할 때는 해당 공격만 별도 복제·연결한다.

새 전직에는 이전 전직의 배타 분기, 능력 교체 정책, 태그 요구사항을 자동 복사하지 않는다. 선택한 선행 전직 ID와 최소 레벨만 반영한다. `RequiredLevel`, 선행 전직 목록, `ExclusiveBranch`, 능력 부여 정책, `bOverrideEquippedGameplay`는 의도에 맞게 검토한다. 기존 스타일 태그와 입력/공격 태그는 재사용하며 GameplayTag 사전을 자동 변경하지 않는다.

<a id="tool-안전성과-한계"></a>
### 안전성과 한계

- 경로·이름·선행 전직 관계·스타일·콤보를 검사한다. 기존 파일, 로드된 패키지, AssetRegistry에 같은 목적지가 있으면 전체 생성을 거절한다. 해당 직업/전직 에셋 종류의 ID 중복도 검사한다.
- Preview는 메모리 초안만 만들고 버린다. 실제 생성에서도 검사를 다시 수행하며, 모든 초안이 검증된 후 패키지로 이동한다. 이동 실패 시 이번에 이동한 초안만 되돌리고 비어 있는 새 패키지를 폐기한다. 성공 후에만 AssetRegistry에 알린다.
- ID 속성이 검색용 메타데이터가 아니므로 ID 검사는 해당 종류의 직업 또는 전직 DA를 로드한다. 전체 콘텐츠/애니메이션 에셋 검색 도구는 아니다. 이 종류가 매우 커지면 검색 가능한 ID 메타데이터와 에셋 검증 규칙으로 옮기는 것이 다음 개선점이다.
- 자동 저장, 자동 source-control 작업, config 수정은 없다. 묶음 생성 전체를 Ctrl+Z로 되돌리는 기능은 제공하지 않는다. 생성 이후 속성 편집은 표준 Undo를 사용하며, 생성 자체를 취소하려면 Content Browser에서 이번에 생성한 에셋들을 삭제한다.
- 이 도구는 새로운 실행 알고리즘이나 스킬 로직을 생성하지 않는다. 이미 지원하는 실행 구조에 맞춘 데이터 제작을 돕는다.

<a id="tool-검증"></a>
### 검증

`ProjectJ.Authoring.Bundle.*` 3개 테스트는 경로/ID 검증, 메모리 초안의 원본 격리와 공유 경계, 콤보 연결과 카탈로그 변환, 전직의 기본 직업·선행 관계·정책 초기값을 검증했다. 이 결과는 [최종 81개 통합 회귀](../../Architecture/Runtime/Reports/Internal_Polish_Validation_2026-09-19.json)에 포함된다. 테스트는 `/Game` 에셋을 만들거나 저장하지 않는다. 메뉴 조작, 패키지 생성/삭제, Content Browser 표시와 표준 저장은 에디터 수동 확인 범위다.
