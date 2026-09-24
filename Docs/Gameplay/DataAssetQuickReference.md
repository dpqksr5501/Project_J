# Project J 데이터 에셋 빠른 참조

2026-09-19 기준. DA 구조를 유지하면서 전용 설정은 내부 작성, 공유 설정은 별도 에셋 참조로 구성한다. 새 직업·전직의 반복 연결은 [에디터 제작 도구](../Architecture/Extensions/Content_Bundle_Authoring_2026-09-19.md)를 사용할 수 있다. 생성·연결 후 저장과 runtime 목록 등록은 별도 단계다.

## 전투/직업

- **CharacterClassDefinition**: 직업의 영구 정체성이다. 기본 전투 스타일, 기본 능력 세트, 성장 시작값을 지정한다.
- **CharacterAdvancementDefinition**: 기본 직업, 선행 전직 ID, 배타 분기, 레벨/태그 조건을 지정한다. 전직 능력은 Additive 또는 ReplacePreviousAdvancement 정책으로 부여하며, 전투 스타일을 덮어쓸 수 있다.
- **CombatStyleDefinition**: 한 전투 스타일의 조립 루트다. 애니메이션, 콤보, 공격 목록, 커맨드, 전투 Ability Set을 한데 연결한다.
- **AbilitySet**: 재사용할 GA/GE 묶음이다. 한 스타일에서만 사용하는 능력/효과는 CombatStyle의 `InlineAbilities`/`InlineEffects`에 직접 작성할 수 있다. 동일 능력을 공유 목록과 내부 목록에 중복 입력하지 않는다.

진행 상태와 실제 부여 핸들은 DA에 저장하지 않는다. 플레이어는 PlayerState의 ProgressionComponent/ASC, NPC는 캐릭터 측 소유자가 관리한다. 전직의 `bOverrideEquippedGameplay`를 켜면 전직의 게임플레이 스타일과 장비의 애니메이션 스타일을 조합한다. 이 옵션 자체가 모든 장비 능력을 회수하는 것은 아니다. [상세 계약](../Architecture/Extensions/Extension_Foundation_2026-09-19.md)

## 공격/입력

- **AttackDefinition**: 게임플레이적으로 구분되는 공격 한 번이다. 몽타주, 이동 정책, 타격 판정, 서버 피해 GE를 소유한다.
- **AttackSet**: CombatStyle이 사용할 공격 카탈로그다. 기존 별도 DA 참조를 유지할 수 있다. `bDeriveAttackCatalog`를 켜면 ComboDefinition과 `AdditionalAttacks`로 실행 목록을 생성하므로 별도 AttackSet은 지정하지 않는다. 서로 다른 공격이 같은 Attack Tag를 사용하면 검증에 실패한다.
- **ComboDefinition**: 공격 순서와 입력 전이 그래프다. 노드는 AttackDefinition을 참조하며 자체 몽타주나 피해 데이터를 갖지 않는다.
- **CombatCommandSet**: `LMB → RMB → LMB` 같은 입력 시퀀스를 특정 GAS 입력 태그로 해석한다. 평타 콤보의 순서와는 별개다.

콤보가 필요 없는 스타일은 `bUsesCombo=false`, 무기 애니메이션이 필요 없는 스타일은 `bRequiresWeaponAnimation=false`를 선택할 수 있다. Attack의 `bMontageDriven=false`는 데이터 작성 옵션이며, 새로운 투사체·채널링·소환 실행기를 자동 구현하지 않는다. 기존 근접 콤보 실행기는 여전히 몽타주를 요구한다.

내부 능력 묶음과 자동 공격 카탈로그는 공유 스타일에서 생성·재사용하는 transient 실행 데이터다. 저장할 DA를 추가로 만드는 기능이 아니며, 플레이 중 정의를 수정하지 않고 변경 후 PIE를 다시 시작한다.

## 애니메이션/표시

- **CharacterAnimProfile**: 휴머노이드 공통 Locomotion/Combat 애니메이션 설정이다. 무기나 직업 전투 데이터는 넣지 않는다.
- **LocomotionProfile**: 비전투 이동 속도, 회전, Motion Matching, 발 배치 관련 설정을 가진다.
- **WeaponAnimProfile**: 한 전투 스타일의 무기 자세, 발도 몽타주, Linked Anim Layer를 가진다. 외형·콤보·피해는 넣지 않는다.
- **WeaponPresentationProfile**: 전투 중 표시할 무기 Actor와 손 소켓을 가진다. 스킨 교체는 이 에셋만 바꿔도 된다.

## 장비

- **EquipmentItemDefinition**: 실제 장착 아이템 하나다. 슬롯, 아이템 정보, 장비 스탯/GE, CombatStyle, WeaponPresentationProfile을 지정한다.
- **Equipment Profile/Starting Equipment DA**: 캐릭터 시작 장비 또는 프리셋 목록이다. Weapon 슬롯에 EquipmentItemDefinition을 지정해 실제 장착 상태를 만든다.

## 태그의 역할

- **InputTag.***: 플레이어가 누른 입력의 의미다. 예: `InputTag.Weapon.LightAttack`.
- **Combo.***: ComboDefinition 내부 노드 주소다. 예: `Combo.Greatsword.Light.1`.
- **Attack.***: AttackDefinition의 공격 식별자다. 예: `Attack.Greatsword.Light.1`.
- **CombatStyle.***: 전투 스타일 식별자다. 예: `CombatStyle.Greatsword`.
