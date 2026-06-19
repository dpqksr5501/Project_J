# Project J 프로젝트 개요

## 한 줄 설명

Project J는 Unreal Engine 5.8 기반의 **3인칭 액션 MMORPG 프로토타입**이다.

현재 개발의 중심은 플레이어 캐릭터의 반응성 높은 이동과 전투 기반이며, 장기적으로 직업·무기·스킬·장비·다수 플레이어·NPC·월드 인스턴스·백엔드 서비스가 결합되는 MMORPG 구조를 목표로 한다.

이 문서는 현재 코드에서 확인되는 구현 상태와 기존 `Docs` 문서의 설계 결정을 합쳐, 프로젝트의 기준 방향을 짧게 설명한다. 세계관, 카메라 시점 외의 구체적인 전투 장르, 최종 동시접속자 수처럼 아직 코드와 문서로 확정되지 않은 내용은 정의하지 않는다.

## 현재 플레이 가능한 영역

현재 Project J에서 가장 구체적으로 구현된 영역은 다음과 같다.

- 3인칭 플레이어 캐릭터와 카메라
- C++ locomotion state 및 Motion Matching 기반 이동 애니메이션
- Start, Stop, Turn, Jump, Fall, Landing 등 이동 단계
- 원격 플레이어의 replicated movement를 고려한 애니메이션 처리
- GAS 기반 능력, 속성, 전투 상태 및 InputTag 입력 경로
- montage와 gameplay event 기반 근접 공격·콤보 기반
- 서버 권위 인벤토리와 장비 FastArray 복제
- 장비에 따른 AbilitySet, GameplayEffect, 능력치, 메시, 무기 애니메이션 프로필 적용
- PlayerState 기반 플레이어 ASC, 속성, 인벤토리 및 장비 소유
- 저비용 NPC 정책과 Mass 확장을 위한 초기 기반
- 월드 인스턴스 ID, 백엔드 요청 ID, 트랜잭션 ID와 같은 MMO 공통 타입
- 거리와 전투 상태를 고려하는 replication policy 기반

현재 기본 맵과 GameMode 설정에는 Unreal Third Person 템플릿 경로가 일부 남아 있다. 이는 프로젝트의 최종 게임 정체성이 Third Person 템플릿이라는 뜻이 아니라, 아직 콘텐츠와 프로젝트 설정 정리가 완료되지 않았다는 의미다.

## 목표 게임 구조

Project J가 지향하는 플레이 흐름은 다음과 같다.

```text
플레이어 입력
  -> InputTag 해석
  -> Gameplay Ability 활성화
  -> 서버 권위 gameplay 판정
  -> montage / gameplay event / effect
  -> 복제된 전투·장비·애니메이션 상태
```

이동과 전투 애니메이션은 서로 다른 책임으로 유지한다.

```text
Locomotion
  -> Motion Matching
  -> 기본 이동 pose 선택

Combat / Skill
  -> GAS Ability
  -> Montage / Slot / Gameplay Event
  -> 공격, 회피, 피격, 스킬 연출
```

Motion Matching이 공격이나 스킬을 선택하지 않으며, 전투 조건을 locomotion state machine 내부에 직접 누적하지 않는 것이 현재 핵심 정책이다.

## 모듈 구조

의존성 방향은 아래 순서를 유지한다.

```text
Project_JCore
  -> Project_JGAS
  -> Project_JCharacter
  -> Project_J
```

### Project_JCore

- Gameplay Tag
- Account, Character, ItemInstance, Request, Transaction, WorldInstance ID
- combat interface
- replication policy 공통 타입
- AssetManager 및 공통 subsystem 기반

구체적인 플레이어 캐릭터나 게임 모드에 의존하지 않는 공통 계약을 둔다.

### Project_JGAS

- Project J 전용 AbilitySystemComponent
- AttributeSet
- GAS 공통 능력치 타입
- InputTag 기반 ability 입력 처리

### Project_JCharacter

- Base, Player, NPC character
- locomotion 및 Motion Matching
- combat, skill input, equipment runtime
- inventory 및 equipment component
- UI ViewModel binding
- replication helper와 NPC/Mass 기반

### Project_J

- GameMode, GameState, PlayerState, PlayerController
- 월드·세션 수준 상태
- Gateway 및 handover 같은 게임 레벨 서비스 경계
- 디버그·프로파일링 명령

## 런타임 소유권

### 플레이어

플레이어의 지속성과 possession 변경을 고려하여 다음 상태는 `AProject_JPlayerState`가 우선 소유한다.

- AbilitySystemComponent
- AttributeSet
- InventoryComponent
- EquipmentManagerComponent
- 계정·캐릭터 식별자와 공개 캐릭터 정보

`AProject_JPlayerCharacter`는 Avatar 역할을 하며 카메라, 입력, 이동, 애니메이션, 전투 표현과 컴포넌트 조정을 담당한다.

### NPC

NPC는 PlayerState가 없을 수 있으므로 `AProject_JBaseCharacter`의 character-local ASC, AttributeSet, EquipmentManager를 사용할 수 있다.

현재 BaseCharacter의 local 컴포넌트는 플레이어에게는 fallback, NPC에게는 실제 소유 경로다. 따라서 이를 정리할 때 단순 삭제하지 않고 **플레이어 소유 전략과 NPC 소유 전략을 명시적으로 분리**해야 한다.

## 서버와 네트워크 원칙

- gameplay 결과는 서버가 권위 있게 확정한다.
- 클라이언트 예측은 입력 반응성과 능력 재생에 사용하되 최종 effect와 판정은 서버가 검증한다.
- 플레이어의 private 상태는 owner-only 복제 또는 backend 경계를 사용한다.
- 공용 PlayerState와 GameState에는 모든 플레이어가 알아야 하는 얇은 정보만 둔다.
- 인벤토리와 장비는 FastArray 변경분 복제를 사용한다.
- 대규모 환경에서는 모든 Actor를 같은 빈도로 복제하거나 갱신하지 않는다.
- 거리, owner, combat, party, guild, public event 같은 relevance 이유를 정책으로 표현한다.

현재 Gateway와 handover 코드는 실제 megaserver 구현이 아니라 향후 backend 및 서버 이동을 연결하기 위한 계약과 경계다.

## 애니메이션 원칙

- 플레이어와 가까운 원격 플레이어는 고품질 Motion Matching 경로를 사용한다.
- NPC는 측정 결과가 필요하다고 증명되기 전까지 저비용 애니메이션 경로를 기본으로 한다.
- 애니메이션 설정은 아래 순서로 해석한다.

```text
CharacterAnimProfile
  -> LocomotionProfile
  -> MotionMatchingAssetSet
```

- `LocomotionAnimStateComponent`가 이동 상태를 계산한다.
- `CharacterAnimInstance`가 thread-safe snapshot을 만들고 Chooser와 Motion Matching에 전달한다.
- 데이터베이스 선택 비용은 거리별로 줄일 수 있지만 Pose Search trajectory 입력은 필요한 갱신 주기를 유지한다.
- Local, Near, Mid, Far, Hidden budget tier를 사용해 비용을 단계적으로 조절한다.

## 전투와 스킬 원칙

- 신규 스킬 입력은 `InputTag.*` 경로를 사용한다.
- `Event.*`는 montage나 활성 ability 내부의 gameplay event에 사용한다.
- 직업, 전직, 장비는 AbilitySet을 통해 능력과 effect를 부여한다.
- 공격, 회피, 피격과 같은 action은 GAS ability와 명시적인 combat state로 표현한다.
- locomotion 제한은 `FProject_JCombatMovementPolicy`와 같은 정책 경계를 통해 계산한다.
- 실제 피해는 서버 검증과 GameplayEffect 경로로 확정하는 것을 목표로 한다.

## 인벤토리와 장비 원칙

```text
Inventory item instance
  -> EquipmentManager
  -> EquipmentRuntime
  -> AbilitySet / Effect / Stat / Mesh / WeaponAnimProfile
```

- static item definition과 실제 소유 item instance를 구분한다.
- 장착 요청은 서버가 item ownership과 slot 규칙을 검증한다.
- 장비 gameplay 적용과 제거는 서버에서 수행한다.
- 클라이언트는 복제된 장비 상태로 로컬 visual을 구성한다.
- 향후 persistence와 거래가 붙으면 inventory write는 RequestId, IdempotencyKey, TransactionId와 연결한다.

## 아직 본격 구현하지 않는 영역

다음 기능은 방향과 최소 계약만 유지하고, 실제 요구와 측정이 생기기 전까지 깊게 구현하지 않는다.

- 실제 megaserver 및 server meshing
- 경매장과 전역 경제 ledger
- 길드, 파티, public event의 전체 gameplay
- GM backoffice와 제재 시스템
- 본격적인 anti-cheat SDK 연동
- 수백 단위 Mass/VAT/impostor crowd
- 모든 NPC에 player-grade Motion Matching 적용
- 실제 backend가 없는 상태의 복잡한 retry·transaction orchestration

## 리팩터링 판단 기준

Project J의 리팩터링은 다음 기준을 따른다.

1. 기존 PlayerState, Character, GAS, inventory, equipment의 소유권을 보존한다.
2. 플레이어와 NPC가 서로 다른 런타임 소유 전략을 가질 수 있음을 고려한다.
3. 서버 권위와 클라이언트 표현을 분리한다.
4. 파생 상태를 여러 곳에서 중복 복제하지 않는다.
5. InputTag, GameplayEvent, AbilityTag의 의미를 섞지 않는다.
6. AnimInstance와 PlayerCharacter 같은 조정자가 개별 시스템의 세부 로직까지 소유하지 않도록 한다.
7. 아직 필요하지 않은 MMORPG 기능을 미리 과도하게 구현하지 않는다.
8. 대규모 최적화는 프로파일링 결과를 기준으로 진행한다.

## 현재 우선순위

현재 구조에 맞는 가까운 작업 순서는 다음과 같다.

1. 플레이어와 NPC의 ASC·Attribute·Equipment 소유 전략을 코드에서 더 명시적으로 표현
2. inventory와 equipment 교체 과정을 서버 단일 operation으로 구조화
3. 레거시 AbilityTag 입력을 InputTag 경로로 마이그레이션
4. `PlayerCharacter`와 `CharacterAnimInstance`의 조정·정책·디버그 책임 분리
5. inventory, equipment, AbilitySet, combat validation 자동화 테스트 추가
6. 실제 gameplay와 backend 요구가 생기는 시점에 Gateway와 persistence 계층 확장

## 관련 문서

- [Architecture Audit](Architecture_Audit_20260607.md)
- [MMORPG Architecture Review](MMORPGArchitectureReview.md)
- [Skill System Architecture](SkillSystemArchitecture.md)
- [Combat Animation Architecture Notes](CombatAnimationArchitectureNotes.md)
- [Motion Matching Notes](MotionMatchingNextSteps.md)
- [Deferred MMORPG Systems](DeferredMMORPGSystems.md)
