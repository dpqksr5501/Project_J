# Project J 프로젝트 개요

## 한 줄 설명

Project J는 Unreal Engine 5.8 기반의 **3인칭 액션 MMORPG 프로토타입**이다.

현재 개발의 중심은 플레이어 캐릭터의 반응성 높은 이동과 전투 기반이며, 장기적으로 직업·무기·스킬·장비·다수 플레이어·NPC·월드 인스턴스·백엔드 서비스가 결합되는 MMORPG 구조를 목표로 한다.

이 문서는 현재 코드에서 확인되는 구현 상태와 기존 `Docs` 문서의 설계 결정을 합쳐, 프로젝트의 기준 방향을 짧게 설명한다. 세계관, 카메라 시점 외의 구체적인 전투 장르, 최종 동시접속자 수처럼 아직 코드와 문서로 확정되지 않은 내용은 정의하지 않는다.

## 현재 플레이 가능한 영역

현재 Project J에서 가장 구체적으로 구현된 영역은 다음과 같다.

- 3인칭 플레이어 캐릭터와 카메라
- Mover 이동·C++ 상태/선택 계산과 Pose Match/BlendStack 기반 이동 애니메이션
- Start, Stop, Turn, Jump, Fall, Landing 등 이동 단계
- 원격 플레이어의 replicated movement를 고려한 애니메이션 처리
- GAS 기반 능력, 속성, 전투 상태 및 InputTag 입력 경로
- montage와 gameplay event 기반 근접 공격·콤보 기반
- 서버 권위 인벤토리와 장비 FastArray 복제
- 장비에 따른 AbilitySet, GameplayEffect, 능력치, 메시, 무기 애니메이션 프로필 적용
- PlayerState 기반 플레이어 ASC, 속성, 인벤토리 및 장비 소유
- 비모션매칭 NPC의 어그로·경로·공격과 Mass 군중 이동·표현 기반
- 월드 인스턴스 ID, 백엔드 요청 ID, 트랜잭션 ID와 같은 MMO 공통 타입
- 기본 Iris 복제, 플레이어·탈것의 공간 우선순위, 소유자 상세 FastArray와 공개 외형 분리, 채널별 접속 상한
- 로컬 Nakama·PostgreSQL을 사용하는 캐릭터 로그인과 가방·우편·장비·채집·제작, 별도 서버 사이 던전 입장·재입장·보상·복귀
- 저장된 테스트 맵의 필드 보스·던전·채집 노드·제작대·비행 탈것, 실제 HUD 그래픽 설정과 테스트 하네스
- 서버 권위 레벨 동기화, 핸드오버 캐릭터 직렬화, 설정 기반 소프트 클래스 로딩

로컬 검증에서는 실제 단발 공격의 서버 확정 피해를 Iris 필드 40인과 던전 24인에서 확인했다. Generic 합성 200접속, 로컬 200체 렌더, 실제 200인 보스전은 검증 항목이 다르다. 실제 200인 보스전은 완료 근거가 없다. [검증 기준과 원자료](../Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md#다시-검증하기-전에)

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
  -> C++ 상태·Chooser 후보 선택 / Pose Match / BlendStack
  -> 기본 이동 pose 선택

Combat / Skill
  -> GAS Ability
  -> Montage / Slot / Gameplay Event
  -> 공격, 회피, 피격, 스킬 연출
```

이동 포즈 검색이 공격이나 스킬을 선택하지 않으며, 전투 조건을 locomotion state machine 내부에 직접 누적하지 않는 것이 현재 핵심 정책이다.

## 모듈 구조

의존성 방향은 아래 순서를 유지한다.

```text
Project_JCore
  -> Project_JGAS
  -> Project_JMount
  -> Project_JCharacter
  -> Project_J
```

위 그림은 주요 실행 층이다. 실제 `.uproject`는 Runtime 6개(Core/GAS/Mount/Character/MMO/Project_J), Editor 1개(CharacterEditor), UncookedOnly 1개(AnimationNodes)다. MMO는 엔진 Core만 의존하는 값 계약이며 Project_JCore와 독립이고 Project_J가 소비한다. AnimationNodes는 Character 실행 노드의 제작 wrapper다. 전체 의존·실행 지도와 구현/계획 경계는 [2026-10-13 전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)에 기록한다.

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
- Mover locomotion 및 native 애니메이션 계산
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

Gateway와 handover는 서버 이동 계약을 제공한다. 실제 로컬 던전 경로에서는 두 Unreal 서버와 Nakama·PostgreSQL의 예약·워커 확인·입장 티켓·채널 임대를 사용한다. 채널 정원은 설정과 실제 NetDriver 한도를 함께 적용한다. 설치형 UE 5.8의 현행 Iris 드라이버에서 관측한 유효 슬롯은 127이었다. 운영 정원과 여러 서버가 공유하는 단일 월드의 수용량은 별도 검증 범위다. [네트워크 구현과 검증](../Networking/README.md)

## 애니메이션 원칙

공통 `ABP_Player_Master`와 `UProject_JPlayerAnimInstance`를 사용한다. Mover가 이동을 시뮬레이션하고 GT snapshot/AnimInstanceProxy가 worker에 값을 전달한다. 상태·전환·Chooser 실행·핀 계산은 C++가 담당하며 ABP는 포즈 연결·콜백 시점·직업/탈것 레이어를 담당한다.

지상은 상태 선택·Pose Match·BlendStack, 공중은 프로젝트의 방향 재선택과 playhead 연속성을 사용한다. 모션 라이브러리는 Manny로 공용화하고, UE5 호환 몸체 직접 재생과 비표준 골격의 Family/Body 리타깃을 구분한다. Local/Near/Mid/Far/Hidden 예산과 필수 전투 포즈 보호는 별도 정책이다. NPC가 공통 그래프를 사용한다는 사실을 같은 갱신 비용이나 모든 NPC의 동일 품질 정책으로 해석하지 않는다.

[현재 로코모션 구조](../Animation/Architecture/Player_Locomotion_Architecture.md)에서 실제 에셋 경로·제작·회귀를 확인한다. `MotionMatchingAssetSet`과 CMC 전용 이전 제작 경로를 새 콘텐츠 기준으로 복원하지 않는다.

## 전투와 스킬 원칙

- 신규 스킬 입력은 `InputTag.*` 경로를 사용한다.
- `Event.*`는 montage나 활성 ability 내부의 gameplay event에 사용한다.
- 직업, 전직, 장비는 AbilitySet을 통해 능력과 effect를 부여한다.
- 공격, 회피, 피격과 같은 action은 GAS ability와 명시적인 combat state로 표현한다.
- locomotion 제한은 `FProject_JCombatMovementPolicy`와 같은 정책 경계를 통해 계산한다.
- 실제 피해는 서버 검증과 GameplayEffect 경로로 확정한다.
- 라이브 소켓 근접 공격은 필요한 본 포즈를 갱신한다. 지원하는 베이크 공격은 전용 서버에서 몽타주 시각과 Mover 루트로 스윕을 만들며, 해당 경로의 본 갱신 생략은 집중 회귀에서 확인했다. [전투 제작과 판정 기준](../Combat/README.md)

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
- 온라인에서 연결한 가방·우편·장비 쓰기는 PostgreSQL 명령에서 세션 세대·revision·멱등 영수증을 확인한다. 소유자 FastArray는 커밋 결과를 전달한다. Standalone 개발 프로필은 SQLite 경로를 사용한다.

## 아직 본격 구현하지 않는 영역

다음은 구현 기반 위에서 운영 계약이나 현장 검증이 필요한 영역이다.

- 단일 월드의 megaserver 및 server meshing
- 운영 규모의 경매·경제·길드·공격대·전역 이벤트
- GM backoffice와 제재 시스템
- 본격적인 anti-cheat SDK 연동
- 실제 200인 동일 보스의 반복 교전, 별도 서버 호스트와 다중 호스트 검증
- MainWorld 전체의 고속 비행 전환 품질, HLOD·PSO·식생 전환과 운영 부하

NPC는 비모션매칭 정책을 유지한다.

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

다음 검증을 시작할 때는 [최신 구현과 확인 범위](../Architecture/Planning/Project_J_MMO_Audit_Followup_2026-11-24.md#다시-검증하기-전에)에서 실행별 선행 조건을 확인한다. 근접 공격·서버 틱, 던전·저장, 의상·그래픽, 탈것·월드의 검증은 실제 소비 경로에 맞춰 선택한다. 과거의 구현 순서와 지금 남은 검증은 [아키텍처 실행 이력](../Architecture/Planning/README.md)에서 확인한다.

## 관련 문서

- [Architecture Audit](../Review/Architecture/Architecture_Audit_20260607.md)
- [MMORPG Architecture Review](../Review/Architecture/MMORPGArchitectureReview.md)
- [Skill System Architecture](../Gameplay/Architecture/SkillSystemArchitecture.md)
- [Combat Animation Architecture Notes](../Combat/Architecture/Combat_Animation_System.md#boundary)
- [Motion Matching Notes](../Animation/Architecture/MotionMatchingNextSteps.md)
- [Deferred MMORPG Systems](../Architecture/Planning/DeferredMMORPGSystems.md)
- [Performance Optimization Foundation](../Performance/Architecture/PerformanceOptimizationFoundation.md)
- [Deferred Optimization Architecture](../Performance/Planning/DeferredOptimizationArchitecture.md)
