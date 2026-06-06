# Deferred MMORPG Systems

이 문서는 Project J에서 중요하지만 아직 본격 구현하지 않을 MMORPG 시스템을 정리합니다. 지금 목표는 대형 시스템을 성급히 만드는 것이 아니라, 나중에 붙일 수 있는 ID, 정책, 디버그, 책임 경계를 유지하는 것입니다.

## 지금 유지해야 할 최소 기반

- `AccountId`, `CharacterId`, `WorldId`, `ZoneId`, `InstanceId`, `ChannelId` 같은 ID 타입
- 서버 권위 원칙: 이동은 CMC/server validation, 전투 결과는 GAS/server path, 보상과 아이템 소유권은 backend가 결정
- 정의 데이터와 상태 데이터 분리: DataAsset/PrimaryDataAsset은 정의, Actor/Component/FastArray/backend row는 상태
- `PlayerState`는 공개 가능한 플레이어 메타데이터만 보관
- `GameState`는 모든 클라이언트가 알아야 할 얇은 월드/인스턴스 상태만 보관
- Debug dump, structured log, PIE network test를 우선 유지

## 지금 추가된 기반

### Replication policy decision

`FProject_JReplicationPolicyDecision`은 actor가 왜 복제되어야 하는지와 우선순위를 함께 표현합니다.

- `bShouldReplicate`
- `RelevanceReasonMask`
- `DistanceSquared`
- `PriorityMultiplier`

현재 relevance reason은 distance, owner, party, guild, combat, public event, always relevant를 표현할 수 있습니다. 아직 party/guild/event 시스템이 없어도 reason vocabulary를 먼저 정해두면 Iris 또는 RepGraph로 넘어갈 때 정책을 유지하기 쉽습니다.

### Distance filter

`UProject_JNetObjectFilter_Distance`는 viewer 위치와 target actor 위치를 기준으로 복제 여부를 계산합니다. 아직 실제 Iris `UNetObjectFilter` adapter는 아니지만, 정책 계산 로직은 독립되어 있습니다.

### Combat prioritizer

`UProject_JNetObjectPrioritizer_Combat`는 GAS gameplay tag를 기준으로 priority multiplier를 계산합니다.

- attacking/dodging/hit reacting: 높은 우선순위
- combat mode: 중간 우선순위
- dead: 낮은 우선순위

대규모 전투에서는 모든 actor를 같은 빈도로 복제할 수 없으므로, combat relevance를 먼저 명시해두는 것이 중요합니다.

### Handover manager

`UProject_JHandoverManager`는 아직 server meshing 구현은 아니지만, 다음 최소 방어를 갖습니다.

- authority가 없는 node에서 handover 시작 방지
- 같은 actor의 중복 handover 방지
- `IProject_JHandoverSerializable` 구현 여부 확인
- payload 크기와 target server node 기록

실제 구현은 backend server registry, transfer ticket, destination ghost spawn, authority switch, reconnect recovery가 준비된 뒤 진행합니다.

### Character responsibility split

`AProject_JPlayerCharacter`는 여전히 gameplay 흐름의 중심이지만, 다음 책임이 component로 분리되었습니다.

- `UProject_JCharacterUIBindingComponent`: attribute to MVVM ViewModel binding
- `UProject_JPlayerInputBindingComponent`: EnhancedInput binding
- `UProject_JReplicatedAnimEventComponent`: replicated animation event counter 의미와 remote 적용

중요한 점은 input component가 `DoMove`, `DoJumpStart`, `StartSprint`, `ToggleCombatMode` 같은 기존 method를 호출한다는 것입니다. 즉, motion matching과 server RPC timing은 유지하면서 소유권만 분리합니다.

## 아직 하지 말아야 할 것

### 진짜 megaserver/server meshing

여러 dedicated server process를 묶는 server meshing은 backend routing, node registry, transfer ticket, ghost replication, authority switch가 모두 필요합니다. 지금은 설계 이름과 interface만 유지합니다.

### Marketplace/economy engine

경매장과 경제 ledger는 Unreal actor 시스템이 아니라 backend service가 중심입니다. 지금은 item instance ID, transaction ID, idempotency key 같은 contract만 준비합니다.

### GM backoffice

GM 제재, 보상 회수, CS case 관리는 gameplay 코드보다 structured log와 audit event가 먼저 필요합니다. 지금은 remote log와 debug command를 유지합니다.

### Anti-cheat SDK

SDK 연동보다 서버 권위 경계가 먼저입니다. movement validation, hit validation, economy audit boundary가 명확해진 뒤 도입합니다.

### Mass/VAT/impostor 대체

수십 명 수준에서는 Significance, URO, animation budget tuning을 먼저 측정합니다. 수백 단위 crowd가 실제 목표로 확정되면 Mass/VAT/impostor를 검토합니다.

## 다음 우선순위

1. combat mode state와 sprint/jump/rotation policy를 별도 component 또는 policy object로 더 분리
2. party/guild/public event relevance를 replication policy에 연결
3. backend request/response contract와 idempotency key 타입 추가
4. PIE에서 10/30/50 character profile 측정
5. actor/component tick, AnimBP, skeletal mesh render cost를 Unreal Insights로 확인
