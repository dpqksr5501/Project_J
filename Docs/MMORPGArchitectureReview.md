# MMORPG Architecture Review

이 문서는 현재 Project J의 전체 C++ 코드를 기준으로, MMORPG 프로젝트로 확장할 때 취하면 좋은 구조, 최적화, 네트워킹, 시스템 경계, 모션 매칭 작업을 정리합니다. 현재 프로젝트는 캐릭터 이동과 C++ Motion Matching이 가장 많이 구현되어 있지만, 코드에는 이미 Core MMO 타입, GAS, 장비 FastArray, 백엔드 Gateway, handover, replication policy, UI/MVVM, Mass 초안까지 들어와 있습니다.

목표는 지금 단계에 맞는 구조를 유지하면서, 나중에 인벤토리, 장비, 전투, 파티, 길드, 월드 인스턴싱, 서버 이동, 대규모 전투가 붙어도 코드 경계가 무너지지 않게 하는 것입니다.

## Current Baseline

### Module Layout

현재 모듈 경계는 방향이 좋습니다.

```text
Project_J
- GameMode / GameState / PlayerState / PlayerController
- Gateway / Handover 같은 게임 레벨 subsystem

Project_JCore
- MMO ID 타입
- replication policy decision/settings
- gameplay tag bootstrap
- AssetManager / GameData / Message subsystem

Project_JGAS
- AbilitySystemComponent
- AttributeSet
- stat type

Project_JCharacter
- Player/NPC/Base character
- locomotion / motion matching / combat / equipment / network helper
- UI binding / ViewModel
- Mass monster 초안

Project_JCharacterEditor
- Character 전용 editor module 자리
```

이 구조는 `Core -> GAS -> Character -> Game` 방향으로 확장하기 좋습니다. 특히 MMO ID, replication policy, backend contract 같은 공통 타입이 `Project_JCore`에 있는 점은 좋습니다.

### Character And Animation

- `AProject_JPlayerCharacter`
  - 카메라, 이동 설정 적용, 프로필 조회, replicated animation event dispatch, combat/profile 연결의 중심입니다.
- `UProject_JLocomotionAnimStateComponent`
  - Idle, Start, Locomotion, Stop, JumpStart, FallOff, FallLoop, Landing 상태를 계산합니다.
- `UProject_JCharacterAnimInstance`
  - game thread 데이터를 thread-safe snapshot으로 만들고, Chooser/Motion Matching에 전달합니다.
- `UProject_JMotionMatchingAssetSet`
  - Run/Sprint/RemoteStart/Stop/Turn/Jump/Fall/Land PSD 선택 책임을 가집니다.
- `Project_J::AnimationProfileValidation`
  - PIE 시작 시 profile, PSD, timing, weapon/combat animation 설정 오류를 warning으로 잡습니다.

### Gameplay Ability And Attributes

- `UProject_JAbilitySystemComponent`
  - 현재는 가벼운 ASC wrapper입니다.
- `UProject_JAttributeSet`
  - Health, Mana, AttackPower, Defense를 replication notify와 clamp로 관리합니다.
- `UProject_JDefaultAttributeSetData`
  - 캐릭터 기본 attribute 초기값을 DataAsset으로 분리할 수 있는 기반입니다.

현재 GAS 구조는 초기 단계로 적절합니다. 아직 복잡한 ability lifecycle이나 attribute meta table을 만들 필요는 없습니다.

### Equipment And Inventory Foundation

- `FProject_JItemInstanceId`
  - static item definition과 실제 소유 아이템 instance를 분리하기 위한 ID입니다.
- `FProject_JItemInstanceData`
  - 인벤토리 구현 전에도 장비 instance 경계를 유지할 수 있습니다.
- `UProject_JItemDefinition`
  - 모든 인벤토리 아이템 유형(장비, 소비, 재료 등)이 공유하는 기본 공통 속성(ItemId, 이름, 설명, 아이콘, MaxStackCount)을 정의하는 추상 데이터 에셋입니다.
- `UProject_JEquipmentItemDefinition`
  - `UProject_JItemDefinition`을 상속받아 구현되며, 장비 슬롯, 장비 mesh, 부여 ability, stat modifier, weapon animation profile을 추가로 정의합니다.
- `UProject_JEquipmentManagerComponent`
  - `FFastArraySerializer` 기반으로 장비 목록을 복제합니다.
  - 장착/해제, mesh spawn, stat modifier, weapon animation profile 갱신 책임을 가집니다.
  - 클라이언트 요청용 `RequestEquipItem`, `RequestEquipItemInstance`, `RequestUnequipSlot` 경로와 서버 확정 함수를 분리합니다.
  - async mesh load 완료 전에 장비가 해제되는 race를 instance id와 visual request id로 방어합니다.
- `UProject_JInventoryComponent`
  - 서버 권위 item instance ownership을 `FFastArraySerializer`로 복제하는 최소 기반입니다.
  - 현재는 UI/컨테이너 시스템이 아니라, 장비/인벤토리/백엔드 persistence가 공유할 item instance 경계를 제공하는 역할입니다.
- `UProject_JModularMeshComponent`
  - 장비 mesh를 main mesh leader pose에 붙여 중복 animation evaluation을 줄이는 방향입니다.

인벤토리 UI가 없어도 이 구조는 문제가 없습니다. 오히려 지금처럼 장비 instance와 definition을 미리 분리한 것은 MMORPG에 맞습니다.

### Network And MMO Runtime

- `FProject_JReplicationPolicyDecision`
  - replicate 여부, reason mask, distance, priority multiplier를 한 구조체로 표현합니다.
- `FProject_JReplicationPolicySettings`
  - distance 기반 relevance 정책을 재사용할 수 있게 합니다.
- `UProject_JNetObjectFilter_Distance`
  - transport layer와 독립적으로 distance decision을 계산합니다.
- `UProject_JNetObjectPrioritizer_Combat`
  - combat tag 기반 priority multiplier를 계산합니다.
- `UProject_JReplicatedAnimEventComponent`
  - 원격 애니메이션 event counter를 캐릭터 본체에서 분리합니다.

Iris를 고려하여 policy 계산을 adapter와 분리해둔 방향은 좋습니다. 다만 UE 5.8에서도 Iris는 여전히 공식적으로 Experimental 상태(로컬 플러그인은 Beta)이며, 필터/우선순위화 API가 FInternalNetRefIndex 및 신규 index manager를 경유하도록 리팩터링되는 등의 API 정밀화가 진행 중입니다. 따라서 실제 Iris filter/prioritizer adapter에 완전 바인딩하는 것은 향후 선택적인 추가 작업으로 남겨두고, gameplay policy를 우선 분리해둔 현재 상태를 유지하는 것이 적합합니다.

### Backend And World Boundary

- `FProject_JRequestId`
- `FProject_JIdempotencyKey`
- `FProject_JTransactionId`
- `FProject_JBackendRequestContext`
- `FProject_JBackendResponseEnvelope`
- `UProject_JGatewaySubsystem`
- `UProject_JHandoverManager`
- `IProject_JHandoverSerializable`

이 코드는 아직 실제 megaserver 구현이 아니라, 나중에 backend/economy/audit/handover를 붙이기 위한 계약과 경계를 잡은 상태입니다. 초기 프로젝트에서 이 정도는 과하지 않습니다. 다만 실제 구현이 붙기 전에는 더 깊은 서버 이동 로직을 만들지 않는 것이 좋습니다.

### UI And Debugging

- `UProject_JCharacterUIBindingComponent`
  - GAS attribute를 MVVM ViewModel로 전달하는 책임을 캐릭터에서 분리합니다.
- `UProject_JCharacterViewModel`
  - HUD, party frame, target panel로 확장할 수 있는 기반입니다.
- `AProject_JPlayerController`
  - `DumpMMOState`, `DumpAnimBudget`, `DumpReplicationPolicy`, `DumpCharacterComponents`, `DumpCombatState`, `DumpMMOProfilingSnapshot` 같은 PIE debug command를 제공합니다.

현재처럼 debug command를 PlayerController에 모아둔 것은 초기 단계에서 적절합니다. 나중에 editor subsystem이나 cheat manager로 옮길 수 있습니다.

### Mass And NPC Foundation

- `AProject_JNPCCharacter`
  - 저비용 NPC tick/net/update 정책의 시작점입니다.
- `UProject_JMassMonster_Trait`
  - Mass entity에 monster stat fragment를 붙일 수 있는 초안입니다.
- `AProject_JMassMonsterSpawner`
  - Mass 기반 스폰 확장 자리입니다.

NPC에 player-grade Motion Matching을 쓰지 않는다는 현재 전제와 잘 맞습니다. Mass는 지금 당장 본격화하기보다, 실제 crowd 목표가 생긴 뒤 측정 기반으로 확장하는 것이 좋습니다.

## What Is Already Good

### 1. Template Code Cleanup Direction

Variant C++ 코드를 제거하고 모듈 의존성을 줄인 방향은 좋습니다. MMORPG 본편 코드와 UE template/sample 코드가 섞이면 나중에 include path, module dependency, content reference가 불분명해집니다.

남은 작업은 Content Browser에서 Variant asset과 redirector를 정리하고, 기본 맵/게임모드가 ThirdPerson에 묶여 있다면 새 프로젝트용 맵/게임모드로 교체하는 것입니다.

### 2. Common MMO Types Are In Core

Account, Character, Request, Transaction, ItemInstance, WorldInstance ID가 `Project_JCore`에 있는 것은 좋습니다. 인벤토리, 경제, 백엔드, 서버 이동, 로그 추적이 붙을 때 같은 ID 체계를 공유할 수 있습니다.

### 3. Equipment Uses FastArray

장비 복제를 `FFastArraySerializer`로 시작한 것은 MMORPG에 맞습니다. 장착 아이템은 전체 배열을 매번 복제하기보다 변경분 중심으로 보내는 구조가 좋습니다.

인벤토리도 같은 원칙으로 최소 FastArray 기반 ownership component를 갖게 되었으므로, 나중에 UI가 붙어도 소유 아이템과 장착 아이템을 같은 actor state에 섞지 않아도 됩니다.

### 4. UI Binding Is Componentized

캐릭터가 직접 HUD를 만지는 대신 UI binding component와 ViewModel을 둔 것은 좋습니다. 나중에 로컬 HUD, 파티 UI, 원격 타겟 UI를 분리하기 쉬워집니다.

### 5. Network Policy Is Transport-Independent

distance/combat priority 계산을 Iris adapter 자체에 바로 묻지 않고 UObject/policy 형태로 분리한 것은 좋은 방향입니다. 나중에 RepGraph 또는 Iris 설정이 바뀌어도 gameplay relevance 판단을 유지하기 쉽습니다.

### 6. Motion Matching Responsibility Is Separated

모션 매칭은 이 문서의 일부일 뿐이지만, 현재 구조에서 가장 많이 구현된 부분입니다. 상태 판단, PSD 선택, 원격 프록시, budget tier, validation이 분리되어 있어 이후 직업/무기/거리 LOD 확장에 유리합니다.

## Recommended Work Now

현재 프로젝트 단계에서 바로 해도 부담이 작고 효과가 큰 작업입니다.

### 1. Project Settings / Content Cleanup

현재 C++ Variant 제거는 진행됐지만, Content/Config는 더 정리할 수 있습니다.

작업 후보:

- `DefaultGame.ini`의 template project name은 `Project J`로 정리되었습니다.
- `/Game/Variant_Combat`
- `/Game/Variant_Platforming`
- `/Game/Variant_SideScrolling`
- `/Game/__ExternalActors__/Variant_*`
- `/Game/__ExternalObjects__/Variant_*`
- `DefaultGame.ini`의 template project name
- `DefaultEditor.ini`의 old ThirdPersonCPP path
- 기본 맵/게임모드가 ThirdPerson에 묶여 있는지 확인

주의:

- `/Game/ThirdPerson`은 기본 맵/게임모드가 아직 참조한다면 먼저 교체해야 합니다.
- `/Game/CombatMasterAnimBundle`은 template Variant가 아니라 animation asset pack으로 보이므로 삭제 대상이 아닙니다.

### 2. Global Validation System

현재 animation/profile validation과 더불어, 주요 데이터 에셋인 EquipmentItemDefinition, AbilitySet 등에는 C++ `IsDataValid()` 검증 함수가 구현되어 있습니다. 이를 자동화 테스트 및 빌드/에디터 실행 경로로 확대 적용하고 다른 시스템으로 넓히면 좋습니다.

추가 확장 후보:

- Attribute/default stat validation
  - MaxHealth, MaxMana가 0 이하인지
  - AttackPower, Defense가 음수인지
- Replication policy validation
  - MaxReplicationDistance가 0인지
  - priority multiplier가 0 또는 음수인지
- Backend config validation
  - GatewayUrl이 비어 있는지
  - log queue/flush interval 값이 비정상인지

초기에는 fatal error가 아니라 PIE warning이나 Data Validation Result(Warning/Error)로 유연하게 처리합니다.

### 3. Transition / Runtime Reason Telemetry

모션 매칭뿐 아니라 전체 gameplay runtime에도 reason 기록이 유용합니다.

후보:

```text
LocomotionTransitionReason
EquipmentChangeReason
ReplicationRelevanceReason
BackendRequestFailureReason
CombatStateChangeReason
```

장점:

- 디버깅이 빨라집니다.
- 로그/telemetry/audit로 확장하기 쉽습니다.
- MMORPG에서 “왜 이 상태가 됐는지”를 서버와 클라이언트 양쪽에서 추적할 수 있습니다.

### 4. PlayerCharacter Responsibility Split Continued

이미 input binding, UI binding, replicated anim event, locomotion component로 많이 분리됐습니다. 다음 후보는 실제 기능이 붙을 때 진행하는 것이 좋습니다.

후보:

- `UProject_JCombatAnimationComponent`
- `UProject_JCharacterProfileComponent`
- `UProject_JCharacterRuntimeDebugComponent`

지금 당장 새 컴포넌트를 만들기보다, `AProject_JPlayerCharacter`에 전투/장비/프로필 코드가 더 누적될 때 분리하는 것이 좋습니다.

### 5. Equipment Manager Server Authority Guardrails

장비 시스템은 FastArray 기반으로 방향이 좋습니다. 다음으로는 서버 권위와 inventory 연동 규칙을 문서화/검증하면 좋습니다.

현재 적용된 규칙:

- 클라이언트는 equip request만 보냅니다.
- 서버가 item definition, item instance id, slot conflict를 검증합니다.
- 장착 결과만 FastArray로 복제합니다.
- stat modifier와 granted ability는 서버 기준으로만 적용합니다.
- async mesh load가 끝나기 전에 unequip된 장비는 다시 붙지 않습니다.
- 장비 정의에 draw distance와 dynamic shadow 정책을 둘 수 있습니다.

남은 확장:

- 실제 inventory UI/획득/소비 흐름이 붙으면 inventory ownership 검증을 equip request에 강하게 연결합니다.
- cosmetic-only mesh는 owner/far LOD 정책을 따로 둘 수 있습니다.

### 6. Debug Commands Grouping

현재 debug command가 유용하지만, 프로젝트가 커지면 PlayerController가 너무 많은 dump command를 가질 수 있습니다.

다음 단계:

- 유지: 초기 개발 중에는 PlayerController command로 충분
- 확장: `UCheatManager` 또는 editor-only debug subsystem으로 이동
- 측정: `DumpMMOProfilingSnapshot` 결과를 파일 또는 CSV로 내보내는 기능 추가

## Recommended Work After More Gameplay Exists

### 1. Inventory System

장비 구조와 최소 inventory ownership component가 있으므로 다음은 실제 획득/소비/이동 규칙입니다.

권장 구조:

```text
InventoryComponent
-> FFastArray item instances
-> server-authoritative add/remove/move
-> EquipmentManager consumes item instance
-> backend persistence uses ItemInstanceId / TransactionId
```

주의:

- Unreal actor로 item instance를 표현하지 않습니다.
- inventory write는 idempotency key와 transaction id를 고려해야 합니다.
- marketplace/economy는 Unreal gameplay code보다 backend service가 중심이어야 합니다.

### 2. Ability Lifecycle And Combat

GAS는 초안이 있으므로 실제 전투가 붙으면 lifecycle 경계를 잡아야 합니다.

권장 구조:

- ability activation은 서버 권위 기준
- montage/event는 combat animation component 또는 event router가 담당
- locomotion 제한은 `FProject_JCombatMovementPolicy` 같은 policy struct를 유지
- hit validation은 SSR 또는 server authority trace로 분리
- cooldown/resource/cost는 GAS effect로 관리

### 3. Party / Guild / Public Event Relevance

replication reason enum은 이미 Party, Guild, PublicEvent를 표현할 수 있습니다. 실제 시스템이 붙으면 다음을 연결합니다.

- party member는 distance보다 높은 relevance
- guild는 근거리/social context에서만 relevance 증가
- public event actor는 event 참여자에게 priority 증가
- combat actor는 전투 중 priority 증가

### 4. Backend Persistence Boundary

Gateway/Handover/ID 타입이 있으므로 persistence가 붙을 때 다음 경계를 유지합니다.

- gameplay actor는 backend HTTP details를 직접 알지 않습니다.
- Gateway subsystem은 request context, retryability, envelope response를 관리합니다.
- inventory/economy write는 idempotency key를 사용합니다.
- audit log는 gameplay event와 backend transaction id를 같이 남깁니다.

### 5. Server Handover

`UProject_JHandoverManager`는 지금 interface boundary로 충분합니다. 실제 server meshing은 다음 준비가 된 뒤 진행합니다.

- server registry
- destination node reservation
- transfer ticket
- ghost spawn
- authority switch
- reconnect recovery
- handover payload versioning

지금 단계에서 더 깊게 구현하면 오버 엔지니어링이 될 가능성이 큽니다.

## Motion Matching Track

모션 매칭은 현재 가장 구현이 많은 영역입니다. 전체 구조 관점에서는 다음 정도가 적절합니다.

### Good Current Direction

- 상태 판단과 PSD 선택이 분리되어 있습니다.
- 원격 프록시 Start PSD와 local/remote timing override가 있습니다.
- animation budget tier로 비용을 줄일 수 있습니다.
- validation으로 설정 실수를 빠르게 찾습니다.
- `FProject_JRemoteVisualLocomotionPolicy`로 원격 forward-only start와 far distance start/stop row 사용 여부를 조절할 수 있습니다.

### Next Useful Work

- Remote visual policy 값을 에셋별로 튜닝
- 이동속도 버프/슬로우가 실제 gameplay에 붙은 뒤, Motion Matching PSD 속도 커버리지와 trajectory 품질을 먼저 확인
- MM이 아닌 fallback locomotion이 생길 때만 play rate policy를 별도로 도입
- transition reason debug 추가
- PoseSearch schema/PSD 설계 규칙 문서화
- 10/30/50 캐릭터 기준 profiling baseline 기록

### Do Not Do Yet

- NPC 전체에 Motion Matching 적용
- 실제 캐릭터 다양성이 생기기 전에 profile 계층 과도하게 세분화
- 모든 전환을 anim notify 중심으로 설계
- 원격 프록시에 로컬 입력 기반 pivot/start 판단을 그대로 적용

## Large Scale Optimization Direction

### Character Animation

- Local/Near: full Motion Matching, full trajectory, foot placement
- Mid: Motion Matching update interval 증가, trajectory refresh throttle
- Far: cached MM context, far-only chooser row, cheaper PSD
- Hidden: no MM search unless state changed, hidden remote update interval

### NPC

- player-grade Motion Matching 미사용
- blendspace/sequence/cached pose 중심
- AI tick cadence 조절
- skeletal mesh URO
- net update frequency/cull distance 조절
- crowd 목표가 확정된 뒤 Mass/VAT/impostor 검토

### Equipment / Modular Mesh

- modular mesh는 leader pose로 animation evaluation 중복을 줄입니다.
- 장비 부위가 늘어나면 material/draw call 비용도 같이 봐야 합니다.
- 가까운 캐릭터와 먼 캐릭터의 장비 mesh visibility/detail policy를 나눌 수 있습니다.

### Network

- distance-only relevance에서 owner/combat/party/event reason 기반으로 확장합니다.
- priority multiplier는 gameplay tag 또는 event state로 계산합니다.
- replication policy decision은 adapter와 분리된 상태를 유지합니다.

### Backend / Telemetry

- remote log는 queue size와 flush interval을 제한합니다.
- gameplay audit은 request id, transaction id, actor id를 같이 기록해야 합니다.
- debug log와 production telemetry를 나중에 분리해야 합니다.

## Edge Cases To Keep Testing

### Character / Animation

- Run/Sprint Start 중 WASD 급회전
- Run/Sprint Start 중 카메라 빠른 회전
- 짧게 입력 후 바로 정지
- 경사면에서 jump 후 즉시 landing
- falloff start 중 ledge/ground 근접
- listen server에서 서로의 Start/Stop/Fall/Land를 보는 경우
- dedicated server에서 simulated proxy만 보는 경우

### Equipment

- 같은 slot 장비 중복 장착
- 장착 직후 network join
- 장비 해제 중 actor destroy
- async mesh load 완료 전에 item unequip
- weapon animation profile이 없는 weapon 장착

### GAS / Combat

- Health/MaxHealth 변경 순서
- death state와 attribute replication
- 공격 montage 중 sprint/jump 제한
- lag compensation timestamp 범위 초과
- 클라이언트 hit request 위조 방지

### Backend / Network

- Gateway request 실패 후 retry 가능/불가 구분
- remote log queue overflow
- handover 중 actor destroy
- party/guild/event relevance가 distance cull과 충돌하는 경우

## Validation Roadmap

### Existing

- MotionMatchingAssetSet PSD 누락
- LocomotionProfile speed/distance 값 이상
- Start timing override 값 이상
- Weapon/Combat profile montage/socket/section 누락
- EquipmentItemDefinition validation (C++ `IsDataValid` 구현 완료)
- AbilitySet validation (C++ `IsDataValid` 구현 완료)
- DefaultAttributeSetData validation (finite value, positive max resource, current-resource overflow warning)

### Add Next

- ReplicationPolicySettings validation
- GatewaySubsystem config validation
- Handover payload/version validation
- Mass monster stat validation
- Project settings validation

## Do Not Overbuild Yet

현재 단계에서 피하는 것이 좋은 작업입니다.

- 실제 인벤토리 UI 없이 복잡한 item container hierarchy 구현
- marketplace/economy를 Unreal actor 시스템으로 구현
- server meshing을 실제로 구현
- NPC 전체에 Motion Matching 적용
- 측정 없이 Mass/VAT/impostor 도입
- 모든 debug command를 production telemetry로 승격
- 실제 직업/무기 데이터 없이 profile DataAsset을 과도하게 분리
- backend retry/transaction/audit을 실제 API 없이 깊게 구현

## Suggested Priority

1. Project settings와 Variant content 정리
2. AnimBP play rate 입력을 새 thread-safe getter에 연결
3. 전체 validation helper 확장
4. Transition/runtime reason telemetry 추가
5. `DumpMMOProfilingSnapshot`으로 10/30/50 캐릭터 baseline 기록
6. Inventory 획득/소비/이동 규칙은 실제 UI/게임플레이 필요 시점에 추가
7. Combat ability lifecycle은 실제 공격/회피/피격 기능이 붙을 때 정리
8. Handover/server meshing은 backend 준비 후 진행

현재 구조는 모션 매칭만을 위한 코드가 아니라, 초기 MMORPG 프로젝트의 핵심 경계를 여러 군데에 미리 잡아둔 상태입니다. 다만 가장 많이 구현된 영역이 캐릭터 이동과 Motion Matching이기 때문에, 지금 당장 작업 우선순위도 그쪽 안정화와 전체 validation/debug 구조 쪽이 가장 효과적입니다.
