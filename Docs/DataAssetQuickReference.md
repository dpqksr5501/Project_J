# Project J 데이터 에셋 빠른 참조

## 전투/직업

- **CharacterClassDefinition**: 직업의 영구 정체성이다. 기본 전투 스타일, 기본 능력 세트, 성장 시작값을 지정한다.
- **CharacterAdvancementDefinition**: 전직 데이터다. 기본 직업을 전제로 추가 능력과 전직 전투 스타일 오버라이드를 지정한다.
- **CombatStyleDefinition**: 한 전투 스타일의 조립 루트다. 애니메이션, 콤보, 공격 목록, 커맨드, 전투 Ability Set을 한데 연결한다.
- **AbilitySet**: 함께 부여·회수되는 GA/GE 묶음이다. 대검 전투 Ability Set은 CombatStyle에 한 번만 연결한다.

## 공격/입력

- **AttackDefinition**: 게임플레이적으로 구분되는 공격 한 번이다. 몽타주, 이동 정책, 타격 판정, 서버 피해 GE를 소유한다.
- **AttackSet**: 해당 CombatStyle이 사용할 모든 AttackDefinition의 카탈로그다. Attack Tag는 세트 안에서 중복될 수 없다.
- **ComboDefinition**: 공격 순서와 입력 전이 그래프다. 노드는 AttackDefinition을 참조하며 자체 몽타주나 피해 데이터를 갖지 않는다.
- **CombatCommandSet**: `LMB → RMB → LMB` 같은 입력 시퀀스를 특정 GAS 입력 태그로 해석한다. 평타 콤보의 순서와는 별개다.

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
