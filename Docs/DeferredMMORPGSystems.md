# Deferred MMORPG Systems

이 문서는 Project J에서 중요하지만 아직 본격 구현하지 않을 MMORPG 시스템과, 나중에 붙일 수 있도록 지금 유지해야 하는 최소 계약을 정리합니다. 현재 우선순위는 캐릭터 이동, remote proxy behavior, replication policy, NPC 비용 측정입니다.

## Backend Contract Baseline

Backend 호출은 marketplace, inventory, transfer, CS/audit 시스템이 완성되기 전부터 안정적인 request identity가 필요합니다.

- `FProject_JRequestId`: 하나의 gateway request를 log와 backend handler에서 추적합니다.
- `FProject_JIdempotencyKey`: retry 가능한 write가 보상, 구매, handover를 중복 처리하지 않도록 합니다.
- `FProject_JTransactionId`: economy, inventory, transfer transaction을 식별합니다.
- `FProject_JItemInstanceId`: static item definition이 아니라 실제 소유 item instance를 식별합니다.
- `FProject_JBackendRequestContext`: request, idempotency, transaction 값을 gateway header로 전달합니다.
- `FProject_JBackendResponseEnvelope`: success, HTTP status, failure kind, retryability, response payload, 원본 request context를 반환합니다.

현재 단계에서는 backend gameplay API를 더 넓히지 않습니다. Inventory, reward, marketplace, telemetry, audit system을 위한 boundary만 유지합니다.

## NPC Optimization Assumption

NPC는 player-grade Motion Matching을 사용하지 않는 것을 기본으로 합니다.

- AI tick cadence
- replication relevance
- skeletal mesh update rate
- combat significance
- server-authoritative state compression
- simple blendspace/sequence/cached pose LOD path

`AProject_JNPCCharacter`는 초기 저비용 정책으로 actor tick 비활성화, 낮은 net update frequency, 낮은 cull distance, slower significance tick interval, skeletal mesh URO, hidden mesh visibility tick policy를 적용합니다.

## Replication Policy Settings

`FProject_JReplicationPolicySettings`는 distance filter threshold를 reusable struct로 보관합니다. `UProject_JNetObjectFilter_Distance`는 default settings 또는 explicit settings에서 decision을 만들 수 있습니다.

현재는 Iris/RepGraph adapter 이전 단계입니다. 중요한 것은 policy calculation을 먼저 명확히 분리해, 나중에 transport layer를 바꿔도 relevance reason과 priority 계산 의미가 유지되도록 하는 것입니다.

## Replication Policy Decision

`FProject_JReplicationPolicyDecision`은 actor가 왜 복제되어야 하는지와 우선순위를 함께 표현합니다.

- `bShouldReplicate`
- `RelevanceReasonMask`
- `DistanceSquared`
- `PriorityMultiplier`

현재 reason vocabulary는 distance, owner, party, guild, combat, public event, always relevant를 표현할 수 있습니다. party/guild/event 시스템이 없어도 reason을 먼저 정해두면 Iris 또는 RepGraph로 넘어갈 때 정책 이름이 흔들리지 않습니다.

## Combat Prioritizer

`UProject_JNetObjectPrioritizer_Combat`는 GAS gameplay tag를 기준으로 replication priority multiplier를 계산합니다.

- attacking/dodging/hit reacting: 높은 우선순위
- combat mode: 중간 우선순위
- dead: 낮은 우선순위

대규모 전투에서는 모든 actor를 같은 빈도로 복제할 수 없으므로 combat relevance를 명시적으로 유지합니다.

## Handover Manager

`UProject_JHandoverManager`는 아직 server meshing 구현이 아니지만 다음 최소 방어를 갖습니다.

- authority가 없는 node의 handover 시작 방지
- 같은 actor의 중복 handover 방지
- `IProject_JHandoverSerializable` 구현 여부 확인
- payload size와 target server node 기록

실제 구현은 backend server registry, transfer ticket, destination ghost spawn, authority switch, reconnect recovery가 준비된 뒤 진행합니다.

## Character Responsibility Split

`AProject_JPlayerCharacter`는 아직 gameplay flow의 중심이지만 다음 책임은 component/policy로 분리되어 있습니다.

- `UProject_JCharacterUIBindingComponent`: attribute to MVVM ViewModel binding
- `UProject_JPlayerInputBindingComponent`: EnhancedInput binding
- `UProject_JReplicatedAnimEventComponent`: replicated animation event counter update/apply
- `Project_J::AnimationProfileValidation`: animation/profile/tuning validation
- `FProject_JCombatMovementPolicy`: combat state가 locomotion decision에 미치는 영향 계산

Input component는 기존 gameplay method를 호출하므로 motion matching과 server RPC timing은 유지됩니다.

## Do Not Build Yet

### Real Megaserver / Server Meshing

여러 dedicated server process를 묶는 server meshing은 backend routing, node registry, transfer ticket, ghost replication, authority switch가 모두 필요합니다. 지금은 interface와 boundary만 유지합니다.

### Marketplace / Economy Engine

경제 ledger와 marketplace는 Unreal actor 시스템이 아니라 backend service가 중심입니다. 지금은 item instance ID, transaction ID, idempotency key만 유지합니다.

### GM Backoffice

GM sanction, reward recovery, CS case management는 gameplay code보다 structured log와 audit event가 먼저 필요합니다. 지금은 remote log와 debug command를 유지합니다.

### Anti-Cheat SDK

SDK 연동보다 server authority boundary가 먼저입니다. Movement validation, hit validation, economy audit boundary가 명확해진 뒤 도입합니다.

### Mass / VAT / Impostor

수십 명 수준에서는 Significance, URO, animation budget tuning을 먼저 측정합니다. 수백 단위 crowd가 실제 목표로 확정되면 Mass/VAT/impostor를 검토합니다.

## Next Priorities

1. PIE에서 10/30/50 character profiling baseline 반복 측정
2. replication policy에 party/guild/public event relevance 연결
3. combat mode state와 ability activation boundary 추가 분리
4. backend request/response contract를 실제 gateway call에 지속 적용
5. Unreal Insights로 actor/component tick, AnimBP, skeletal mesh render cost 확인
