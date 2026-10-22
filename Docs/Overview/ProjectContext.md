# Project J 프로젝트 맥락

이 문서는 현재 구조·권위·상태 소유권과 작업 진입점을 설명한다. 현재 이동·애니메이션의 책임 분리는 [플레이어 로코모션 아키텍처](../Animation/Architecture/Player_Locomotion_Architecture.md), 에셋별 제작 규칙과 시점별 검증은 해당 전문 문서를 따른다.

공통 생산 경로는 Mover + `UProject_JPlayerAnimInstance` + `ABP_Player_Master`다. 상태·전환·Chooser 호출·핀 계산은 C++가 소유하고, 포즈 연결과 노드별 실행 시점은 ABP가 소유한다. OTM/Strafe, 전방 세 방향 Strafe Sprint, 공중 방향 재선택·playhead 연속성과 수평 관성을 유지한다. 구형 CMC·독립 MM/단발 경로는 현재 제작 기준이 아니다.

2026-10-21 원본 BP/ABP 실행 흐름 재검토에서 회전 Chooser 입력을 native 타입과 실제 table binding으로 이주하고, AnimInstance owner 교체의 source 유효성과 비표현 월드의 무기 콜백 경계를 보강했다. 후속 점검에서는 client-capable target의 Voice 모듈을 Iris 연결 전에 로드하고, UE 5.8.3 에디터의 영문 기대값 검사 3개의 언어 상태를 실행 경계에서 격리·복원한다. native 67개와 엔진 검사 3개, 총 70개가 경고·실패·미실행 없이 통과했다. 원본과 동일한 부분·프로젝트 고유 차이·PIE 검증 범위는 [애니메이션 흐름 검토](../Animation/Architecture/Player_Animation_Flow_Review_2026-10-21.md)를 따른다. 아래 61/62개는 각 시점의 과거 결과다.

2026-10-22 속도 튜닝은 Run 500/Sprint 750cm/s와 지상 재생률 1.0을 사용한다. 최근 gait 기반 정지 후보 분리와 접지 이후 착지 검색 범위 보정을 적용했고, 착지 수정 회귀 84개와 후속 전환 검색 점검 85개가 각각 실패·경고·미실행 없이 통과했다. 착지 시퀀스의 공중 접근 구간을 접지 뒤 다시 검색하지 않으며, 발 IK·접촉·잠금 해제·경사 보정과 공중 재선택·관성은 유지한다. 실제 PIE 조건과 남은 시각 검수 범위는 [라이브러리 검증 기록](../Animation/Authoring/Player_Animation_Library.md)을 따른다.

공통 Skeleton은 Manny `SK_Mannequin`이다. UEFN 원본에서 변환한 575개 시퀀스·BlendSpace 11개를 라이브러리로 관리한다. 현재 Run/Sprint·점프/착지는 Neutral, Idle/Walk 기본 동작은 Relaxed로 선택하며 원본 뱅크는 Content 밖에 압축 보관하고 최소 변환 kit만 유지한다. [추가 수입·중복 검사](../Animation/Authoring/Player_Animation_Library.md)는 원본 ID·실제 모션·재생 계약·recipe로 기존 Manny 결과를 재사용한다. UE5 호환 몸체는 직접 재생하고, 현재 Bip01 대검 몸체는 Family RTG와 몸체별 손 보정을 사용한다. 추가 망토 뼈의 처리는 [스켈레톤·추가 뼈 기준](../Animation/Authoring/HumanoidSkeletonRetargetingStandards.md)을 따른다. Quinn Simple은 같은 Skeleton의 직접 재생을 확인했지만, 체형별 접지·무기 접촉의 실제 플레이 검수는 별개다.

2026-10-21 마지막 IK 수정 기준 direct UBT와 집중 native 회귀 61개, 실제 지상·Sprint 회전·공중/착지·전신 몽타주 검증이 통과했다. 이전 빌드·성능·네트워크 수치를 이 결과에 합산하지 않는다. 팔 기준 포즈, 손가락 마디, Bip01 limb 시작점 누락의 원인·수정·실패 자료는 [전환·검증 기록](../Animation/Architecture/Player_Locomotion_Migration_2026-10-19.md)에 남겼다. 전체 클립 외형·새 군중 성능·Cook/Shipping 완료를 뜻하지 않는다.

원본 보관·퇴역 이후 별도 native 회귀 62개가 경고·실패 없이 통과했다. 생산 모션/그래프/리그 해시와 원본 없는 로드를 확인했고, 실제 입력 재실행 여부는 [보관·검증 결과](../Maintenance/Animation_Source_Archive_2026-10-21.md)에서 구분한다.

MMORPG의 운영 인증·영속 저장·실제 존 이전 등 구현/보류 경계는 [현재 잔여 작업 상태](../Review/Architecture/Project_J_MMO_Remaining_2026-10-19.md)를 따른다. 이전 인계 요청을 새 작업 지시로 자동 재개하지 않는다. 작업 종료 시 정리·검증·사용자 변경 보존은 [변경 마감 기준](../Maintenance/Change_Completion_Policy.md)을 따른다.

## 1. 프로젝트 성격과 현재 범위

- Unreal Engine **5.8** 기반의 3인칭 액션 MMORPG 지향 프로젝트다.
- 핵심은 C++ 기반 캐릭터 이동, GAS, 데이터 기반 직업/장비/전투, 네트워크 권한, 그리고 Mover·데이터 기반 로코모션 애니메이션이다.
- 현재 기본 맵은 `Lvl_ThirdPerson`, 기본 GameMode는 `BP_Project_JGameMode`다. 이 Blueprint는 `Project_JGameMode`를 상속하고, GameState/PlayerState는 C++ 클래스다. 실제 CDO의 Pawn은 `BP_GreatSword`, Controller는 `BP_ThirdPersonPlayerController`의 override다. Third Person 샘플 에셋이 남아 있지만 최종 게임 구조를 의미하지는 않는다.
- 대검은 첫 번째 실제 직업 수직 슬라이스다. 기존 테스트 `BP_Player`는 `BP_Player_Master`로 승격했고 `BP_GreatSword`는 그 자식이다. 공통 컴포넌트·입력·Manny source는 부모, 직업·장비·몸체 프로필은 자식이 공급한다. 테스트용 로컬 능력·속성 기본값과 의상 연결 BP 호출을 정리했다. 생산 대검·현재 테스트 NPC는 공통 `ABP_Player_Master`를 사용하며, NPC는 플레이어 BP를 상속하지 않는다. [마스터 제작 계약](../Architecture/Runtime/Player_Character_Master.md)을 따른다. 구형 `ABP_Player`는 퇴역했다.
- 아직 실제 백엔드/메가서버/거래소/길드/대규모 군중을 완성한 상태가 아니다. 관련 타입과 경계는 장래 확장을 위해 존재한다.
- NPC 타깃 후보는 중앙 스케줄러의 공간 질의를 사용하며, 장비 메시 스트리밍은 `VisualAssetSubsystem`의 요청·수명 관리를 사용한다. 전투/탑승 애니메이션 레이어에는 컴포넌트 로컬 `FStreamableHandle` 경로도 남아 있다. 이를 모든 전투 질의·표현 로딩의 단일 서비스로 일반화하지 않는다. 서버 간 이동은 handover envelope/상태 머신/transport 계약이며 실제 라이브 서버 연결 완료를 의미하지 않는다.

직업·전직·능력 부여 및 DA 작성의 최신 계약은 [2026-09-19 확장 기반](../Architecture/Extensions/Extension_Foundation_2026-09-19.md)을 참고한다. 같은 PlayerState 안의 아바타 교체를 지원한다. 후속 로컬 개발 저장은 `-ProjectJLocalProfile=<name>` Development 단독 실행에서 Progression/Quest와 실제 인벤토리·장비를 함께 다룬다. 기존 AssetManager 레지스트리가 안정적인 ItemId/version을 해석하고, 로컬 보상·소비·장착과 기존 직업·전직 변경은 worker의 lease+상태+receipt 커밋이 확정된 뒤 전체 상태를 설치하고 기존 권위 컴포넌트에 게시한다. 초기 로드도 첫 알림 전에 P/Q/소유물/슬롯을 함께 설치한다. 정상 레벨 상승은 XP 경로를 사용하고 동기 개발/hand-over override setter의 저장 우회는 거부한다.

프로젝트 전용 Windows rollback VFS와 전체 snapshot v2를 유지하며, DB container는 구형 writer의 오해석을 막는 version2다. 현재 세션의 경제 명령은 순번 창64개, checkpoint는 최신 결과1개를 보존하고 이전 세션/순번은 만료로 거부한다. 상태·receipt·순번/정리를 같은 transaction에 확정하므로 기존4096회 누적 한도로 정상 저장이 멈추지 않는다. 입장·큐·백업의 유한 한도와 실패 상태, 실측·재시작 검증은 [현재 잔여 작업 기록](../Review/Architecture/Project_J_MMO_Remaining_2026-10-19.md)을 따른다. `Project_JProfileMigration` commandlet은 원본 read-only 검사·dry-run·별도 결과/rollback 복사본을 지원한다. snapshot v1의 누락된 소유물을 추측하지 않고 명시적 입력으로만 이관한다. 실제 사용자 DB 전환과 운영 인증·네트워크/Shipping 저장·실제 handover는 별도 범위다.

## 2. 모듈 경계

```text
Project_JCore
  ├─ 공용 Gameplay Tag, 인터페이스, MMO 식별자, Asset/GameData/Message Subsystem
  └─ 상호작용 공용 계약

Project_JGAS
  ├─ Project J AbilitySystemComponent, AttributeSet, grant lease 공용 계약
  └─ GAS 소유권 계약

Project_JMount
  └─ 지상/비행 탈것, 탈것 Attribute/Animation/Component

Project_JCharacter
  ├─ Mover Pawn, Locomotion, 상태 선택/Pose Match, Animation
  ├─ Combat, Input, Hit Validation, Server-side rewind
  ├─ Inventory, Equipment, Weapon presentation, Character class
  ├─ UI binding, Networking, Mass/NPC, Validation
  └─ 실질적인 플레이어 게임플레이의 중심 모듈

Project_J
  ├─ GameMode / GameState / PlayerState / PlayerController
  ├─ Social, Gateway, Handover 등 게임·백엔드 경계
  └─ 전역 게임 흐름과 진입점

Project_JMMO
  └─ 값 기반 서비스 계약·CAS 메모리 repository·확장 카탈로그

Project_JCharacterEditor
  └─ 에디터 전용 검증·도구 확장

Project_JAnimationNodes
  └─ UncookedOnly 그래프 wrapper; 실행 노드는 Character 모듈
```

의존성은 아래 방향을 유지한다. 하위 모듈이 Character나 Game 모듈을 역참조하지 않도록 한다.

```text
Project_JCore → Project_JGAS → Project_JMount
       └──────────────────────→ Project_JCharacter → Project_J
Project_JMount ───────────────→ Project_JCharacter
엔진 Core → Project_JMMO → Project_J
Project_JCharacter → Project_JAnimationNodes → Project_JCharacterEditor
```

`Project_JMount`는 Core/GAS 공용 계약 위에 있는 탈것 런타임 모듈이며,
현재 `Project_JCharacter`가 이를 사용한다. 따라서 Character 또는 Game 모듈을
Mount가 역참조하지 않도록 한다.

## 3. 런타임 소유권: 가장 먼저 지켜야 할 규칙

### 플레이어와 NPC는 같은 방식으로 상태를 소유하지 않는다

- `AProject_JPlayerCharacter`는 Avatar다. 카메라, 입력, Locomotion, 전투 표현과 캐릭터 조정을 맡는다.
- 플레이어의 장기 상태(ASC, AttributeSet, Inventory, EquipmentManager, ProgressionComponent)는 기본적으로 `AProject_JPlayerState`에 둔다. 재접속·Possess 변경을 고려한 구조다.
- `AProject_JBaseCharacter`는 플레이어/NPC 공통 기반이다. NPC는 PlayerState 없이 자신에게 로컬 ASC/장비 런타임을 둘 수 있다.
- 어떤 시스템이든 `GetAbilitySystemComponent()`, `GetInventoryComponent()`, `ResolveEquipmentManagerForRuntime()` 같은 해석 경로를 우회해 캐릭터에 직접 상태를 중복 생성하면 안 된다.
- ASC 포인터가 살아 있어도 해당 Pawn이 현재 Avatar가 아니면 활성화·취소·상태 조회·입력 해제 권한이 없다. 후속 [전체 감사](../Review/Audits/Project_J_Whole_Project_Audit_2026-10-13.md)에서 실제 교체 시험과 검토 범위를 확인한다.

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
- 각 책임을 하나의 DA에 몰아넣지 않는다. 자세한 분리는 [DataAssetQuickReference](../Gameplay/Authoring/Content_Authoring_System.md#data)를 따른다.

### 이동 → 애니메이션

```text
Enhanced Input / NavMover → GAS 정책 검증 → Mover / Network Prediction
  → 물리 lifecycle·UMoverTrajectoryPredictor → GT 불변 snapshot / Proxy
  → State Controller / Pose Match Chooser / BlendStack (지상)
    + Project_J 값 타입 Chooser·실제 재생 시각 유지 (공중 재선택)
  → Warping / Root Offset / Lean / Aim / 공통 DefaultSlot / 발 접지
  → 전투·탈것 Linked Anim Layer / Pose History
  → 리타깃 몸체 / 손 접촉 / 의상 물리 / 장비 Leader Pose
```

- 지상 물리는 원본 SmoothWalking 엔진 solver에 native `Project_JGroundMovementLibrary` 계산을 연결한다. 모드 BP는 native 호출 adapter이며 튜닝은 Mover의 `GroundMovementSettings`다. 승인된 native 입력을 직접 소비하고 별도 BP 입력 투영을 만들지 않는다. native AnimInstance가 프레임 계산과 Chooser/재생 요청을 소유하고 포즈 그래프가 소비한다. worker에서 Pawn의 물리 상태를 다시 조회하지 않는다.
- OTM/Strafe facing과 jump/land episode는 물리 결과에서 가져온다. 공격 권위·판정·몽타주 선택은 GAS가 소유하고 root motion은 Mover에 연결한다.
- 리타깃·의상 표시 예산과 실제 전투/서버 포즈 요구는 기존 coordinator를 사용한다. 이번 이관을 과거 200명 성능 검증의 재통과로 보고하지 않는다.
- 옛 `Project Unified Locomotion`과 CMC TIP·곡선/MM 후보 정책은 이전 기반 자료다. 현재 계약과 남은 한계는 [Mover 이관](../Animation/Architecture/Player_Locomotion_Migration_2026-10-19.md)을 따른다.

## 5. 현재 콘텐츠/에셋 작업의 기준

### 생산용 휴머노이드 직업

```text
AProject_JPlayerCharacter
  → BP_Player_Master
    → BP_GreatSword

ABP_Player_Master
  + ALI_HumanoidCombat
  + ABP_Greatsword_Layers
```

- 새 플레이어 직업은 `BP_Player_Master` 자식 + 기존 직업 layer/DA로 확장한다. 값 차이만 있는 직업마다 native 클래스를 추가하지 않는다. `AProject_JGreatswordCharacter`는 기존 native 테스트 fixture용으로 유지한다. 사람형 NPC는 입력/PlayerState 계약까지 플레이어 부모에서 상속하지 않는다.
- 공통 입력, GAS 소유권, 장비 시스템, SSR, locomotion graph를 직업 Blueprint에 복사하지 않는다.
- `ABP_Player_Master`는 지상 상태 선택·재생/보정, Project_J 점프 재선택·원본 일반 추락, 슬롯·Aim·접지·Pose History·Mount 선택을 소유한다. 실제 BP_Player/GreatSword/NPC 연결을 저장했다. 직업/몸체 DA의 역할은 유지한다.
- State Controller/Chooser는 gameplay 권위를 갖지 않는다. 물리 입력과 lifecycle은 Mover, 전투·장비 상태는 기존 GAS/장비 컴포넌트가 소유한다.
- 직업 layer는 무기 자세/상체 오버레이/직업 고유 전투 이동 등 **차이가 실제로 필요한 부분만** 구현한다.
- 현재 대검의 망토/헤어/치마는 표시 몸체의 세 RigidBody 노드가 계산하고 의상은 LeaderPose를 공유한다. 실험용 embedded Chaos Cloth 3개는 mesh 원본 백업 후 제거했다. 공통 본 파츠의 Tick은 꺼지지만, 향후 독립 vertex cloth 장비에는 기존 modular component가 Tick 예외를 적용한다. 구성과 검증 범위는 [플레이어 마스터](../Architecture/Runtime/Player_Character_Master.md)를 따른다.

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
| 탈것 | `Source/Project_JMount` 및 `Docs/Gameplay/Architecture/MountSystemArchitecture.md` |
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

- 생산 휴머노이드는 현재 공통 `ABP_Player_Master`와 직업 Linked Anim Layer를 사용한다. `BP_Player`는 공통 아바타 제작 기반이고, 퇴역한 `ABP_Player`/`ABP_Humanoid_Master`를 새 제작 경로로 복원하지 않는다.
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

현재 로코모션의 회귀·제작·진단 경로는 [플레이어 로코모션 전환 기록](../Animation/Architecture/Player_Locomotion_Migration_2026-10-19.md)의 마지막 절을 따른다. 퇴역한 구형 MM CVar나 전용 노드를 복원하지 않는다. 에셋 Validation 오류는 실제 메시지와 참조를 먼저 확인하고, 폴더 변경 뒤 남은 Redirector/Soft Reference가 원인으로 확인되면 해당 연결을 정리한다.

## 10. 이 문서 다음에 읽을 전문 문서

- 전체 방향/보류 범위: [ProjectOverview](ProjectOverview.md), [MMORPGArchitectureReview](../Review/Architecture/MMORPGArchitectureReview.md), [DeferredMMORPGSystems](../Architecture/Planning/DeferredMMORPGSystems.md)
- GAS·AbilitySet·Input: [SkillSystemArchitecture](../Gameplay/Architecture/SkillSystemArchitecture.md)
- 장비·직업·DA: [DataAssetQuickReference](../Gameplay/Authoring/Content_Authoring_System.md#data), [ContentExpansionGuide](../Gameplay/Authoring/Content_Authoring_System.md#expansion)
- 전투·콤보·무기·애니메이션: [CombatLocomotionArchitecture](../Combat/Architecture/Combat_Animation_System.md#layers), [CombatAnimationComposition](../Combat/Architecture/Combat_Animation_System.md#composition)
- 현재 플레이어 로코모션·원격 표현·회귀: [플레이어 로코모션 전환 기록](../Animation/Architecture/Player_Locomotion_Migration_2026-10-19.md). [MotionMatchingNextSteps](../Animation/Architecture/MotionMatchingNextSteps.md)는 이전 MM 경로의 역사 자료다.
- 탈것: [MountSystemArchitecture](../Gameplay/Architecture/MountSystemArchitecture.md)

## 새 Codex 채팅에 붙여 넣을 최소 컨텍스트

```text
UE 5.8 C++ Project J 작업이다. 먼저 Docs/Overview/ProjectContext.md와 기능별 관련 문서를 읽고,
Source/Config를 좁게 분석한 뒤 작업해줘. BP_Player는 공통 아바타 제작 기반이며,
생산 휴머노이드 기준은 AProject_JPlayerCharacter → 직업 native class → BP_직업,
ABP_Player_Master + 직업 Linked Anim Layer 구조다. 이동은 Mover와 native 이동·애니메이션 계산,
포즈는 State Controller/Pose Match/BlendStack을 사용한다. OTM/Strafe와 현재 점프
공중 재선택·playhead 연속성을 유지하고, 퇴역한 Master/MM fallback을 복원하지 마.
플레이어의 장기 상태는 PlayerState 우선,
서버 권한과 클라이언트 표현을 분리한다. C++ 변경 후에는 Project_JEditor Win64 Development를 빌드해줘.
기존 사용자 코드·에셋 변경을 보존하고 AGENTS.md의 빌드 안전 규칙을 지켜줘.
작업에 필요하면 MCP·컴퓨터 사용을 별도 도구 사용 허락 없이 활용해줘.
현재 요청에 필요한 관련 변경·저장은 진행하되 작업 범위 밖으로 확대하거나 접근 제한을 우회하지 마.
```
