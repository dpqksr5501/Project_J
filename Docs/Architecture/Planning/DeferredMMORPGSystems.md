# Deferred MMORPG Systems

이 문서는 아직 도입하지 않은 MMORPG 기능과 도입 조건을 정리한다. 현재 구현/계약/미연결을 구분한 최신 판정은 [현재 잔여 작업 판정](../../Review/Architecture/Project_J_MMO_Remaining_2026-10-19.md)를 따른다. 메모리 파티·길드와 NPC 판단/이동 하이브리드는 실제 구현이다. 개발 단독 실행은 SQLite의 진행도·퀘스트·인벤토리·장비 절편과 보상/소비의 사전 영속 커밋을 연결한다. 운영 인증·네트워크/Shipping 저장·분산 경제·존 간 권한 이전은 별도 경계다.

## Backend Contract Baseline

Backend 호출은 marketplace, inventory, transfer, CS/audit 시스템이 완성되기 전부터 안정적인 request identity가 필요합니다.

- `FProject_JRequestId`: 하나의 gateway request를 log와 backend handler에서 추적합니다.
- `FProject_JIdempotencyKey`: retry 가능한 write가 보상, 구매, handover를 중복 처리하지 않도록 합니다.
- `FProject_JTransactionId`: economy, inventory, transfer transaction을 식별합니다.
- `FProject_JItemInstanceId`: static item definition이 아니라 실제 소유 item instance를 식별합니다.
- `FProject_JBackendRequestContext`: request, idempotency, transaction 값을 gateway header로 전달합니다.
- `FProject_JBackendResponseEnvelope`: success, HTTP status, failure kind, retryability, response payload, 원본 request context를 반환합니다.

현재 로컬 개발 경로의 Inventory·reward·class/advancement는 기존 consumer와 실제 SQLite 사전 커밋까지 연결돼 있다. 장기 저장·구버전 이관을 미래 기능으로 되돌리지 않는다. 운영 Gateway의 gameplay endpoint·인증/소유권, marketplace·telemetry·감사 서비스는 아직 미연결이며, 실제 서비스 계약이 정해질 때 확장한다. [현재 판정·검증 요약](../../Review/Architecture/Project_J_MMO_Remaining_2026-10-19.md)을 기준으로 내부 완료와 외부 차단을 구분한다.

## NPC Optimization Assumption

NPC는 player-grade Motion Matching을 사용하지 않는 것을 기본으로 합니다.

- AI tick cadence
- replication relevance
- skeletal mesh update rate
- combat significance
- server-authoritative state compression
- simple blendspace/sequence/cached pose LOD path

`AProject_JNPCCharacter`는 저비용 tick/net cull/URO 정책을 적용한다. NPCDecision과 NPCPath는 실제 batch/queue·수명·상한을 가진다. MassRepresentation은 Actor를 유지한 단일 이동 소유권 전환/경로 snapshot/GT 적용을 구현하지만 일반 필드 spawn에서 RegisterNPC/SetRoute까지 자동 연결하지 않는다. 실제 배회 NPC 콘텐츠를 선택한 뒤 전투 승격·종료 해제를 검증하며, 미연결 API를 전 NPC에 임의로 켜지 않는다.

NPC, boss, monster는 Motion Matching을 사용하지 않는다. AI tick cadence, perception/path refresh, pooling, Mass representation의 상세 계약과 Unreal Insights 측정 기준은 [Performance Optimization Foundation](../../Performance/Architecture/PerformanceOptimizationFoundation.md)를 따른다.

## Replication Policy Settings

`FProject_JReplicationPolicySettings`는 distance filter threshold를 reusable struct로 보관합니다. `UProject_JNetObjectFilter_Distance`는 default settings 또는 explicit settings에서 decision을 만들 수 있습니다.

기본 Iris는 `IrisNetDriverConfigs`·`net.Iris.UseIrisReplication`·registered subobject list와 Character의 `SetupIrisSupport(Target)`가 실제 연결되어 있다. 파티/길드 Distance filter와 Combat prioritizer UObject는 여전히 정책 계산기이며 custom Iris filter/prioritizer 등록·할당 구현과 다르다. 공간 Pawn AOI와 원거리 소셜 요약을 분리할 실제 소비자가 생길 때 adapter를 추가한다. 전체 길드의 Pawn을 거리 밖에서도 모두 복제하는 정책으로 바로 연결하지 않는다.

## Replication Policy Decision

`FProject_JReplicationPolicyDecision`은 actor가 왜 복제되어야 하는지와 우선순위를 함께 표현합니다.

- `bShouldReplicate`
- `RelevanceReasonMask`
- `DistanceSquared`
- `PriorityMultiplier`

현재 reason vocabulary는 distance, owner, party, guild, combat, public event, always relevant다. 파티·길드의 메모리 membership과 PS 투영은 이미 있고 정책 계산기가 이를 읽는다. 계산 결과가 실제 Iris 송신 대상/우선순위에 적용된다는 뜻은 아니다.

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

Actor를 유지하는 Mass 이동 하이브리드는 이미 있다. actorless representation·VAT/impostor는 별도 미래 단계다. 먼저 실제 필드 콘텐츠 연결과 GT eligibility/observer/worker join/transform 비용을 함께 측정하고, 병목이 확인될 때만 확장한다.

## Next Priorities

1. PIE에서 10/30/50 character profiling baseline 반복 측정
2. live Iris/RepGraph adapter를 도입한 뒤 replication policy에 party/guild/public event relevance 연결
3. combat mode state와 ability activation boundary 추가 분리
4. backend request/response contract를 실제 gateway call에 지속 적용
5. Unreal Insights로 actor/component tick, AnimBP, skeletal mesh render cost 확인
