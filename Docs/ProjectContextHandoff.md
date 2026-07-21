# Project J 프로젝트 컨텍스트 · 작업 인수인계

이 문서는 새 Codex 채팅 또는 새 작업자가 **특정 기능 구현 전에 읽을 프로젝트 지도**다. 전투/애니메이션만이 아니라 런타임 소유권, 데이터, 네트워크, 탈것, UI와 향후 MMORPG 확장 경계를 함께 다룬다.

> 상세한 구현 규칙은 각 전문 문서가 기준이다. 이 문서는 현재 구조를 빠르게 파악하기 위한 진입점이며, 에셋의 정확한 할당값은 에디터에서 최종 확인한다.

## 1. 프로젝트 성격과 현재 범위

- Unreal Engine **5.8** 기반의 3인칭 액션 MMORPG 지향 프로젝트다.
- 핵심은 C++ 기반 캐릭터 이동, GAS, 데이터 기반 직업/장비/전투, 네트워크 권한, 그리고 Motion Matching 애니메이션이다.
- 현재 기본 맵은 `Lvl_ThirdPerson`, 기본 GameMode는 `BP_Project_JGameMode`다. Third Person 샘플 에셋이 남아 있지만 최종 게임 구조를 의미하지는 않는다.
- 대검은 첫 번째 실제 직업 수직 슬라이스다. `BP_Player`, `ABP_Player`는 테스트 자산이며 생산 직업 구조의 기준은 아니다.
- 아직 실제 백엔드/메가서버/거래소/길드/대규모 군중을 완성한 상태가 아니다. 관련 타입과 경계는 장래 확장을 위해 존재한다.

## 2. 모듈 경계

```text
Project_JCore
  ├─ 공용 Gameplay Tag, 인터페이스, MMO 식별자, Asset/GameData/Message Subsystem
  └─ 상호작용 공용 계약

Project_JGAS
  ├─ Project J AbilitySystemComponent, AttributeSet, AbilitySet 공용 타입
  └─ GAS 소유권 계약

Project_JMount
  └─ 지상/비행 탈것, 탈것 Attribute/Animation/Component

Project_JCharacter
  ├─ Character, Locomotion, Motion Matching, Animation
  ├─ Combat, Input, Hit Validation, Server-side rewind
  ├─ Inventory, Equipment, Weapon presentation, Character class
  ├─ UI binding, Networking, Mass/NPC, Validation
  └─ 실질적인 플레이어 게임플레이의 중심 모듈

Project_J
  ├─ GameMode / GameState / PlayerState / PlayerController
  ├─ Social, Gateway, Handover 등 게임·백엔드 경계
  └─ 전역 게임 흐름과 진입점

Project_JCharacterEditor
  └─ 에디터 전용 검증·도구 확장
```

의존성은 아래 방향을 유지한다. 하위 모듈이 Character나 Game 모듈을 역참조하지 않도록 한다.

```text
Project_JCore → Project_JGAS → Project_JCharacter → Project_J
                         ↘ Project_JMount (독립적인 탈것 런타임 경계)
```

## 3. 런타임 소유권: 가장 먼저 지켜야 할 규칙

### 플레이어와 NPC는 같은 방식으로 상태를 소유하지 않는다

- `AProject_JPlayerCharacter`는 Avatar다. 카메라, 입력, Locomotion, 전투 표현과 캐릭터 조정을 맡는다.
- 플레이어의 장기 상태(ASC, AttributeSet, Inventory, EquipmentManager)는 기본적으로 `AProject_JPlayerState`에 둔다. 재접속·Possess 변경을 고려한 구조다.
- `AProject_JBaseCharacter`는 플레이어/NPC 공통 기반이다. NPC는 PlayerState 없이 자신에게 로컬 ASC/장비 런타임을 둘 수 있다.
- 어떤 시스템이든 `GetAbilitySystemComponent()`, `GetInventoryComponent()`, `ResolveEquipmentManagerForRuntime()` 같은 해석 경로를 우회해 캐릭터에 직접 상태를 중복 생성하면 안 된다.

### 권한과 표현을 분리한다

- 서버: 장비 변경, Gameplay Effect, 능력 결과, 피해 판정, 이동의 최종 권한을 갖는다.
- 소유 클라이언트: 입력 반응과 GAS 예측을 담당한다.
- 모든 클라이언트: 복제된 상태를 바탕으로 무기 표시, 몽타주 등 시각 표현을 재구성한다.
- **게임플레이 상태**와 **애니메이션/무기 표현 상태**는 같은 값이 아니다. 예를 들어 CombatMode의 서버 권한과 Draw/Sheathe 시각 전환은 별도의 수명 주기를 가진다.

## 4. 핵심 실행 흐름

### 입력 → 게임플레이

```text
Enhanced Input (IA / IMC)
  → UProject_JPlayerInputBindingComponent
  → UProject_JSkillInputRouterComponent
  → InputTag.*
  → UProject_JSkillInputExecutionComponent
  → GAS Ability / Gameplay Event
  → 서버 권한 검증 · Gameplay Effect · 복제
```

- 키 자체가 아닌 `InputTag.*`가 게임플레이 의도다.
- 단일 스킬은 AbilitySet이 부여한 GA가 처리한다.
- 연속 입력 명령은 `CombatCommandSet`, 진행 중 공격 연계는 `ComboDefinition`이 담당한다. 둘을 혼합하지 않는다.

### 장비 → 직업 전투 표현

```text
EquipmentItemDefinition
  → EquipmentManager / EquipmentRuntime
  → CombatStyleDefinition + WeaponAnimProfile + WeaponPresentationProfile
  → AbilitySet / Effects / 장비 스탯
  → Combat animation layer / 무기 Actor 표현
```

- 아이템 정의는 소유·장착·시각·전투 스타일을 연결한다.
- `WeaponAnimProfile`은 전투 이동/발도/납도/링크 레이어다.
- `WeaponPresentationProfile`은 무기 Actor와 손·등 소켓 같은 시각 연결만 가진다.
- `AttackDefinition`은 한 공격의 몽타주, 이동 정책, 타격 사양, 피해 Effect다.
- 각 책임을 하나의 DA에 몰아넣지 않는다. 자세한 분리는 [DataAssetQuickReference](DataAssetQuickReference.md)를 따른다.

### 이동 → 애니메이션

```text
Player movement / replicated movement
  → UProject_JLocomotionAnimStateComponent
  → UProject_JCharacterAnimInstance snapshot (thread-safe)
  → Chooser / Motion Matching / Master ABP
  → Linked job animation layer / montage slots / Foot Placement / Leg IK
```

- Locomotion 상태 계산은 C++ 컴포넌트가 담당한다. 직업 ABP Event Graph에서 Pawn을 읽어 속도·방향을 다시 계산하지 않는다.
- Motion Matching은 비전투 기본 locomotion의 선택기다.
- 무기 전투 자세는 Master ABP의 Linked Anim Layer 경로로 합성한다.
- 공격·구르기·발도·피격처럼 하체까지 저작된 동작은 `DefaultSlot`의 full-body montage 우선순위를 따른다.

## 5. 현재 콘텐츠/에셋 작업의 기준

### 생산용 휴머노이드 직업

```text
AProject_JPlayerCharacter
  → AProject_JGreatswordCharacter
    → BP_GreatSword

ABP_Humanoid_Master
  + ALI_HumanoidCombat
  + ABP_Greatsword_Layers
```

- 새 휴머노이드 직업은 얇은 native 직업 클래스 + 해당 Blueprint + 직업 layer/DA 조합으로 확장한다.
- 공통 입력, GAS 소유권, 장비 시스템, SSR, Motion Matching graph를 직업 Blueprint에 복사하지 않는다.
- `ABP_Humanoid_Master`는 공통 Motion Matching, 슬롯, Aim, Foot Placement/Leg IK, Pose History, Mount 선택을 소유한다.
- 직업 layer는 무기 자세/상체 오버레이/직업 고유 전투 이동 등 **차이가 실제로 필요한 부분만** 구현한다.

### 태그와 데이터의 역할

- `InputTag.*`: 입력 의도
- `State.*`: 지속 상태(CombatMode, Sprinting, Attacking 등)
- `Event.*`: 몽타주 노티파이 등 일회성 Gameplay Event
- `Combo.*`: 콤보 그래프 노드 주소
- `Attack.*`: 개별 공격 정체성
- `Command.*`: 순차 입력 명령 정체성

공유/코드 태그는 `Source/Project_JCore/Public/Project_JGameplayTags.h`, 콘텐츠 태그는 `Config/DefaultGameplayTags.ini`를 먼저 확인한다. 같은 의미의 태그를 자산마다 새로 만들지 않는다.

## 6. 설정과 주요 진입점

| 범주 | 우선 확인 위치 |
| --- | --- |
| 엔진 기본 맵·GameMode | `Config/DefaultEngine.ini` |
| Input Action/Mapping Context 기본 설정 | `Config/DefaultInput.ini` 및 캐릭터의 Input 관련 DA/Blueprint |
| Gameplay Tag | `Config/DefaultGameplayTags.ini`, `Project_JGameplayTags.*` |
| GAS 능력/속성 | `Source/Project_JGAS`와 `Source/Project_JCharacter/Public/AbilitySystem` |
| 플레이어/GameMode/PlayerState | `Source/Project_J/Game/` |
| 캐릭터·이동·애니 | `Source/Project_JCharacter/Public/` 및 `Private/Animation/` |
| 장비·인벤토리 | `Source/Project_JCharacter/Public/Equipment`, `Inventory`, `Components` |
| 탈것 | `Source/Project_JMount` 및 `Docs/MountSystemArchitecture.md` |
| 데이터 검증 | `Source/Project_JCharacter/Public/Validation`, `Private/Tests` |

## 7. 새 작업 시작 전 점검 순서

1. 해당 기능이 어느 모듈의 책임인지 먼저 정한다.
2. 해당 시스템의 Data Asset, Gameplay Tag, AbilitySet이 기존에 있는지 `rg`로 확인한다.
3. 플레이어 전용인지, NPC도 공유하는지, 서버 권한이 필요한지 판단한다.
4. 기존 C++ API/컴포넌트를 확장할지, 새 컴포넌트가 정당한지 결정한다.
5. 에셋 연결이 필요한 경우에는 C++/설정 문서로 먼저 확인하고, 실제 Blueprint/DA 값만 에디터에서 최소 범위로 확인한다.
6. C++ 변경 뒤에는 `Project_JEditor Win64 Development` 빌드를 실행한다.
7. 네트워크·표현 변경은 최소 2인 PIE에서 소유자와 simulated proxy를 모두 확인한다.

## 8. 자주 발생하는 오해와 금지 사항

- `BP_Player`/`ABP_Player` 테스트 구현을 생산 직업에 직접 의존시키지 않는다.
- 아이템 장착 여부와 손에 든/등에 멘 무기 표현을 같은 bool 하나로 처리하지 않는다.
- InputTag를 몽타주 이름 또는 직업 이름으로 과도하게 세분화하지 않는다.
- 직업마다 PlayerCharacter, 공통 AnimGraph, GA 기반 로직을 복사하지 않는다.
- 일반 locomotion을 Root Motion으로 바꾸지 않는다. Root Motion은 커밋된 공격/회피/돌진에 한정한다.
- Client의 애니메이션 결과를 피해·장비·전투 상태의 권위로 사용하지 않는다.
- 현재 필요하지 않은 백엔드/대규모 MMO 시스템을 미리 완성하려 하지 않는다. 계약과 경계만 유지한다.

## 9. 검증 및 디버깅

유용한 PIE 콘솔 명령:

```text
DumpMMOState
DumpAnimBudget
DumpReplicationPolicy
DumpCharacterComponents
DumpCombatState
DumpMMOProfilingSnapshot [MaxDetailedCharacters]
```

Motion Matching 관련 CVar는 [README](README.md)와 [MotionMatchingNextSteps](MotionMatchingNextSteps.md)를 참고한다. 에셋 Validation 오류는 폴더 이름 변경 뒤 Redirector 또는 Soft Reference가 남은 경우도 있으므로, Content Browser의 Redirector 정리와 재저장을 우선 확인한다.

## 10. 이 문서 다음에 읽을 전문 문서

- 전체 방향/보류 범위: [ProjectOverview](ProjectOverview.md), [MMORPGArchitectureReview](MMORPGArchitectureReview.md), [DeferredMMORPGSystems](DeferredMMORPGSystems.md)
- GAS·AbilitySet·Input: [SkillSystemArchitecture](SkillSystemArchitecture.md)
- 장비·직업·DA: [DataAssetQuickReference](DataAssetQuickReference.md), [ContentExpansionGuide](ContentExpansionGuide.md)
- 전투·콤보·무기·애니메이션: [CombatLocomotionArchitecture](CombatLocomotionArchitecture.md), [CombatAnimationComposition](CombatAnimationComposition.md)
- Motion Matching·원격 프록시·예산: [MotionMatchingNextSteps](MotionMatchingNextSteps.md)
- 탈것: [MountSystemArchitecture](MountSystemArchitecture.md)

## 새 Codex 채팅에 붙여 넣을 최소 컨텍스트

```text
UE 5.8 C++ Project J 작업이다. 먼저 Docs/ProjectContextHandoff.md와 기능별 관련 문서를 읽고,
Source/Config를 좁게 분석한 뒤 작업해줘. BP_Player/ABP_Player는 테스트 자산이며,
생산 휴머노이드 기준은 AProject_JPlayerCharacter → 직업 native class → BP_직업,
ABP_Humanoid_Master + 직업 Linked Anim Layer 구조다. 플레이어의 장기 상태는 PlayerState 우선,
서버 권한과 클라이언트 표현을 분리한다. C++ 변경 후에는 Project_JEditor Win64 Development를 빌드해줘.
기존 사용자 에셋 변경은 건드리지 말고, Unreal MCP는 내가 명시적으로 요청할 때만 사용해줘.
```
