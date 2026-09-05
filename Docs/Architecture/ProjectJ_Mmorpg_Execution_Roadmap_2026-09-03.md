# Project_J MMORPG Execution Roadmap

**Date:** 2026-09-03  
**Scope:** 전체 MMORPG 아키텍처·성능·애니메이션 확장 로드맵. 이 문서는 구현 명령이 아니라 우선순위, 검증 기준, 보류 기준을 정한다.  
**Non-goal:** 이 문서 자체는 C++/Blueprint/에셋/프로젝트 설정을 변경하지 않는다.

## 1. 결정 요약

Project_J는 지금 **단일 캐릭터 전투·이동 vertical slice**로서 기반이 좋다. 다음 목표는 기능을 한꺼번에 추가하는 것이 아니라, 하나의 권위 있는 zone-server vertical slice를 측정 가능한 예산 안에서 증명하는 것이다.

우선순위는 아래와 같다.

| 우선순위 | 지금 해야 할 결과 | 아직 하지 않을 것 |
|---|---|---|
| P0 | 프로덕션 진입 금지 경계, 측정 기준, Iris 실제 구동 여부, 전용 서버 목표를 명확히 한다. | prototype GameMode GUID를 인터넷 서비스 identity로 사용, custom Iris filter/priority 구현, 대규모 Mass 전환 |
| P1 | 2~50 클라이언트 zone slice, replication/AOI 정책, animation thread-cost 감사, NPC tier를 검증한다. | profiler 없이 Game Thread 작업을 worker로 이동, GASP 전체 복제, 모든 NPC를 Mass로 대체 |
| P2 | 검증된 병목만 작은 단위로 개선하고, streaming/rendering/physics 예산과 확장 경계를 만든다. | server meshing, 광범위 object pooling, 복잡한 persistence/market/guild 구현 |
| P3 | multi-zone, backend durability, 대규모 군중과 운영 자동화를 실제 부하 증거 뒤에 확장한다. | 초기 단계에서의 분산 서버·cross-zone orchestration |

## 2. 목표 아키텍처

```text
Client
  Input / local prediction
    -> CharacterMovement + GAS
    -> semantic locomotion snapshot
    -> engine-supported parallel animation presentation

Authoritative zone server
  authenticated session / character load
    -> PlayerState-owned ASC, attributes, inventory/equipment
    -> authoritative movement, abilities, combat, NPC simulation
    -> relevance / frequency / representation policy
    -> replication budget per connection

Representation tiers
  Local / near combat       : full Character, gameplay-relevant animation
  Mid / far player          : throttled presentation, sparse cosmetic state
  Active combat NPC         : actor AI at assigned update tier
  Ambient population        : Mass / instanced representation, no full ASC/CMC

Data and services
  Primary assets + async visual loading
  -> persistence command boundary (future)
  -> authenticated backend / durable transaction store (future)
```

Game Thread는 authority, UObject/Actor access, CharacterMovement, GAS, RPC와 replication ownership을 유지한다. worker/task graph는 엔진이 지원하는 parallel animation과, 검증된 순수 데이터 계산에만 사용한다. `worker thread를 많이 쓰는 것`은 목표가 아니다.

## 1.1 현재 실행 상태 (2026-09-05)

| 영역 | 상태 | 실제로 확보한 증거 | 다음 게이트 |
|---|---|---|---|
| 로컬 CPU / animation | 부분 완료 | S0, S70, S100 visual-crowd CPU 기준선과 S100 parallel evaluation 경로 확인 | remote/dedicated-server role별 p95/p99 및 actual PoseSearch 비용 |
| GPU | 초기 확인 완료 | S100 `ProfileGPU` single-frame sample 약 3.99 ms | target camera/effects/population의 p95/p99 GPU capture |
| worker / Task Graph | 현 경로 검증 완료 | S100 `NativeThreadSafeUpdate` 표본의 약 99.99%가 parallel-evaluation 상태 | 측정 근거 없이 추가 worker 분리하지 않음 |
| 2-client networking | 규모 기준선 완료 | Legacy 기준선 보관, PIE dedicated-server + client 2 topology 및 Iris runtime activation 확인. N50 server-mover -> client 2 movement capture에서 50 mover/30 Hz의 실제 outbound packet·bit cost 확보 | 실제 inventory/equipment FastArray delta, AOI/relevance 정책, 50 real connections가 제품 목표가 될 때의 입력·connection 부하 |
| Iris adoption | 활성화 완료, 검증 진행 | server/client 모두 `IrisActive=1`, `ReplicationModel=Iris`; FastArray Iris module support 적용; N2 verbose packet baseline 확보 | FastArray delta smoke 및 connection별 relevance 정책 |
| dedicated-server binary | 보류 | PIE dedicated-server world는 동작 | installed UE 배포판이 Server target build를 지원하지 않음; source-built/server-capable environment에서 별도 검증 |
| remote TIP | 구현·수동 회귀 진행 | server-authoritative event와 simulated-proxy presentation 경로를 구현했고, 2-client에서 기본 presentation을 확인 | 빠른 연속 회전/방향 반전/거리 이탈·재진입의 수동 회귀 및 실제 gameplay polish |
| NPC / Mass / AOI / memory / assets / rendering / physics | 미착수 | 구조 감사·계획만 존재 | population/CPU/network evidence 후 우선순위 순서대로 착수 |

`프로파일링 완료`는 전체 프로젝트의 종료 상태가 아니다. 현재는 **로컬 animation CPU 기준선과 Iris 전환 전후의 runtime 기준선을 확보한 상태**다. 이후 모든 최적화는 동일 workload의 전후 trace와 correctness gate를 함께 통과해야 한다.

## 3. 단계별 실행 계획

### Stage 0 — 기준선·문서 정합성 (P0, 분석만)

**목표:** 현재 코드, 설정, 에셋의 실제 상태를 이후 모든 판단의 기준으로 만든다.

| 작업 | 산출물 / 검증 |
|---|---|
| 현 구조 감사 유지 | `ProjectJ_Architecture_Audit_2026-09-03.md`의 상태표를 코드·에디터 증거로만 갱신 |
| 문서 정합성 | 코드와 충돌하는 과거 문서에 현재 상태/보류 상태를 표시 |
| 애니메이션 기준 문서 | `Docs/Animation/ProjectJ_Animation_Architecture.md`가 현재 존재하지 않으므로, substantial locomotion 변경 전에 현 `ABP_Humanoid_Master`/State Controller/Chooser/MM/BlendStack 책임을 담은 기준 문서를 만들거나 기존 문서를 그 역할로 지정 |
| runtime 사실 확인 | default map, World Partition, Mass 배치, UI/Niagara, Iris runtime backend는 Editor/실행 증거로만 확정 |
| 프로파일 시나리오 정의 | hardware/build, local/remote/NPC 수, map, cvars, 목표 p50/p95/p99와 bytes/client 기록 양식 확정 |

**완료 조건:** “구현됨”, “기반만 존재”, “계획”을 구분하고, 수치 비교가 가능한 재현 시나리오가 있다.

### Stage 1 — Animation execution & Game Thread audit (P1, 분석·측정만)

**목표:** 현재의 `Gameplay/C++ -> thread-safe snapshot/proxy -> AnimGraph` 경로가 실제로 어느 thread에서 어떤 비용을 발생시키는지 증명한다. 이 단계는 애니메이션 구조를 교체하지 않는다.

| 감사 축 | 확인할 항목 | 판단 / 산출물 |
|---|---|---|
| Thread ownership map | CMC state 수집, semantic locomotion, trajectory, significance, `NativeUpdateAnimation`, thread-safe update, proxy publish/copy, Chooser, MM query/search/post-selection, State Controller, BlendStack, PoseHistory, IK, linked layers, notify/montage/curve, cloth/final pose | UE 5.8 source 기준으로 Game Thread / Parallel Update / Parallel Evaluation / worker / render-related / unknown 표 작성 |
| Game Thread hot path | getter/cast/UObject traversal, tag query, profile/asset lookup, Blueprint VM, delegate, debug string/log, temporary `TArray`/`TMap`/`FString`, layer switching | per-frame인지 revision-driven인지, 캐시 가치가 있는지 표시 |
| Snapshot audit | Actor/UObject 재접근, GT/Anim 중복 계산, snapshot 크기, container/UObject ref copy, hot/cold/debug 혼합 | 필요한 경우에만 Hot snapshot과 Cold/Debug snapshot 분리안을 제안 |
| Parallel eligibility / Fast Path | SkeletalMesh/AnimInstance 설정, AnimBP와 linked layer의 Blueprint call, dynamic cast, non-thread-safe function, property access | 실제 parallel fallback 가능성과 Fast Path 차단 노드를 에셋 증거와 함께 보고 |
| Chooser / MM | Chooser 평가 thread·빈도, PoseSearch DB 선택·query·search 비용/ownership, DB switch/reselect | 안전하지 않은 worker화 대신 event/revision, throttle, search stagger의 효용 판단 |
| sync / allocation | mutex, atomic, fences, forced completion/wait, candidate buffers, tag-container/temporary copy | Insights 상 GT stall, task starvation, allocation churn이 있을 때만 개선 후보 선정 |

**필수 측정 순서:** `1 local` → `1+10 remote` → `1+30 remote` → `1+50 remote` → `1+100 low-significance`.

각 시나리오에서 Game Thread, parallel update/evaluate, PoseSearch/MM, Chooser, BlendStack, IK, notifies, skeletal tick, memory, worst frame, p95/p99를 기록한다. 100명 테스트는 representation/test harness가 준비된 뒤 수행한다.

**완료 조건:** “Chooser가 매 프레임 GT라서 문제” 또는 “MM이 worker에 있으니 무료” 같은 추측이 아닌 trace/engine-source 근거로 병목과 안전한 변경 후보가 정리된다.

### Stage 2 — Authoritative multiplayer vertical slice (P0/P1, 구현·검증)

**목표:** 한 zone에서 최소 두 클라이언트와 dedicated server가 권위, 예측, 전투, inventory/equipment, remote presentation을 정확히 수행한다.

| 작업 | 범위 |
|---|---|
| Dedicated server | `Project_JServer.Target.cs`, headless build/cook, test map 및 서버 시작 절차 |
| identity/persistence boundary | prototype GameMode GUID를 production path에서 분리. 인증된 session → 서버 측 character load → idempotent persistence command 경계 설계 |
| authority verification | GAS ability activation, CMC movement, SSR, FastArray inventory/equipment, owner-only 데이터, combat validation의 multi-client test |
| remote presentation | movement와 semantic presentation event에서 재구성. pose asset/BlendStack 상태를 불필요하게 매번 복제하지 않음 |
| locomotion authority | C++ `LocomotionAnimStateComponent`가 semantic state authority, Chooser/MM/BlendStack/ABP는 pose selection·routing. one-shot 종료/interrupt 뒤 MM re-entry contract와 PoseHistory 정책을 문서화 |

**완료 조건:** dedicated server + 2 clients에서 authority denial, owner-only leakage, duplicate/replay, SSR bounds, remote one-shot/locomotion 전환을 자동/수동 테스트로 통과한다.

### Stage 3 — Replication, Iris and interest management (P1)

**목표:** actor class와 거리만이 아니라 connection별 relevance·bandwidth 예산에 따라 동기화한다.

1. Iris가 실제 NetDriver로 활성화되었는지 UE 5.8 공식 방식(`IrisNetDriverConfigs` 또는 runtime verification, target/module integration)을 먼저 증명한다.
2. Iris를 계속 사용할지 ReplicationGraph를 사용할지 하나를 선택한다. 현재 helper `UObject` policy는 live Iris adapter가 아니다.
3. Player, boss, active NPC, inactive NPC, loot, cosmetic actor별 relevance reason, update-rate bucket, dormancy, cull, priority, owner-only condition을 설계한다.
4. custom filter/prioritizer는 기본 relevancy/빈도 정책과 Network Insights 측정이 부족할 때만 등록·적용한다.
5. connection별 outbound bytes, actor/channel/object count, update latency, packet loss 상황을 다중 클라이언트 capture로 검증한다.

**완료 조건:** 10/30/50 remote 상황에서 동기화 정책이 실제 backend에 연결되어 있고, relevance와 bandwidth 결과를 재현할 수 있다.

### Stage 4 — NPC, AI and representation tiers (P1/P2)

**목표:** 모든 NPC를 full `ACharacter`로 유지하지 않는 server CPU/memory 정책을 만든다.

| Tier | 정책 |
|---|---|
| boss / active combat | full Actor, AI, collision, relevant ASC, 필요한 경우 고품질 animation |
| nearby normal NPC | actor AI와 animation budget tier, perception/query/update interval 소비자 연결 |
| distant/inactive NPC | 저빈도 simulation 또는 dormancy/비활성화; representation 전환 조건 명시 |
| ambient crowd | Mass/instanced representation, full ASC/CMC/individual replication 없음 |

작업에는 AI tick schedule, perception budget, Behavior Tree/StateTree 선택, Mass processors/representation/LOD/spawn-despawn bridge, combat promotion/demotion, server CPU soak test가 포함된다. 현재 Mass spawner/trait는 실험 기반일 뿐이므로, promotion 경계 없이 대규모 Mass 전환을 시작하지 않는다.

### Stage 5 — Evidence-led animation and CPU improvements (P1/P2)

**입력:** Stage 1 Insights 및 engine-source audit에서 확인된 병목만 선택한다.

| 후보 | 적용 조건 |
|---|---|
| revision/dirty-cache | weapon, gait, stance, rotation mode, mount, combat mode처럼 event-driven인 값이 매 프레임 재결정되는 경우 |
| Hot/Cold snapshot | proxy copy/캐시 locality가 측정상 문제이며 hot path가 희소 debug data에 오염된 경우 |
| remote search throttle/stagger | 30~100 remote MM update가 같은 frame에 집중되고 품질 저하 없이 분산 가능한 경우 |
| MM DB tuning | DB 크기/channel/pruning/PCA-KD tree/near-far DB가 실제 search cost를 초과할 때 |
| Anim Budget Allocator | full Character를 유지하는 near/mid/far tier에서 update/evaluate 비용이 예산 초과할 때 |
| Animation Sharing / Leader Pose | 동일 skeleton/animation을 쓰는 NPC/crowd가 반복 평가될 때 |
| One-shot ↔ MM observability | 동작 품질 문제가 아니라 debug surface가 분리되어 혼동될 때. StateController/BlendStack/MM를 합친 presentation trace를 추가 |

**보류:** GASP처럼 one-shot을 반드시 MM debug surface에 표시하도록 만들기 위해 runtime 구조를 바꾸지 않는다. Project_J 로그가 보인 것처럼 one-shot은 BlendStack trace, continuous locomotion은 MM trace로 분리될 수 있다.

### Stage 6 — Memory, assets and world streaming (P2)

**목표:** population과 콘텐츠 증가가 UObject/GC/asset resident memory/로딩 hitch로 바로 전환되지 않게 한다.

- `memreport`, Memory Insights, object count, GC time, component/mesh/material count의 기준선 작성
- `TArray`/`TMap` churn, per-character component creation, visual equipment mesh lifetime, SSR history memory를 측정
- Asset Manager/Primary Asset/soft reference/bundle/chunk 정책 수립. 현재 `AlwaysCook`을 streaming 전략으로 오해하지 않음
- 직업·장비·몬스터의 visual layer async load/unload, cancel/lifetime/burst policy 검증
- default map 및 World Partition/Data Layer/HLOD/streaming source를 Editor에서 확정한 뒤 client/server world streaming 정책 수립
- object pooling은 allocation/GC 프로파일이 증명할 때만 한정 적용

### Stage 7 — Rendering, VFX and physics budgets (P2)

**목표:** MMO 인구수에서 visual quality가 render/GPU/physics spike를 만들지 않도록 target hardware별 계약을 만든다.

| 분야 | 작업 |
|---|---|
| Rendering | skeletal mesh LOD, draw call/material/section, shadow, occlusion, HLOD, texture/streaming pool, Lumen/VSM/RT/Substrate device profile 및 scalability 정책 |
| Niagara | effect count, spawn rate, bounds, culling, pooling, combat burst budget |
| Physics/collision | channel matrix, overlap/trace/SSR query budget, capsule/crowd collision tier, Chaos actor and ragdoll policy |
| Validation | `stat unit`, GPU Visualizer, RHI/Render Thread, GPU frame, target population/effects/hardware p95/p99 |

### Stage 8 — Module, data and feature expansion boundaries (P2)

**목표:** 직업/무기/탈것/콘텐츠팀 확장 시 `Project_JCharacter` 단일 feature bucket이 병목이 되지 않게 한다.

- dependency rule: Core는 feature 의존 없음; GAS는 presentation 의존 없음; network interest는 server service; animation은 immutable snapshot 소비
- GameFeature/ModularGameplay 도입은 실제 plugin 단위 activation/content ownership 요구가 생길 때만
- data asset ownership/versioning/validation, GameplayTag namespace, class/weapon/mount/equipment contracts 정리
- Character는 orchestration, Component는 명확한 상태/서비스 책임, GAS는 ability/effect/attribute authority를 유지
- over-inheritance와 circular module dependency를 lint/review 규칙으로 차단

### Stage 9 — Backend, operations and multi-zone (P3)

**선행 조건:** Stage 2~4의 one-zone dedicated server가 예산과 correctness 목표를 통과했다.

- authenticated account/session, durable character/inventory/economy transactions, audit/retry/idempotency
- party/guild/chat/social presence가 zone-local replication을 우회하지 않는 service boundary
- zone ownership, transfer protocol, reconnect/version mismatch, backpressure, observability/redaction/rate limits
- multi-zone/meshing은 단일 zone population·CPU·bandwidth가 실제 한계에 도달한 뒤 결정

## 4. 전체 작업 항목과 착수 단계

| 분야 | 핵심 작업 | 주 시작 단계 |
|---|---|---|
| CPU / threading | GT hot path, thread ownership, Task Graph/parallel animation, allocation/lock/sync, tick | 1, 5 |
| Networking | authority, CMC, RPC, FastArray, Iris/RepGraph, relevance, bandwidth/update frequency | 2, 3 |
| Gameplay / GAS | ASC ownership, tags, combat/skill/state, Character/Component contract | 2, 8 |
| Animation | StateController authority, MM/Chooser/BlendStack, re-entry, linked layers, Fast Path, Anim Thread | 1, 2, 5 |
| AI / NPC | AI tick/perception, BT/StateTree, Mass, crowd, server CPU | 4 |
| Memory | UObject/GC, container churn, pools, asset lifetime | 1, 6 |
| Assets / streaming | async load, Primary Asset, soft reference, WP, equipment/monster/class data | 6 |
| Rendering | mesh/draw/material, Niagara, shadow/occlusion, Nanite/Lumen policy | 7 |
| Physics | collision, overlap/trace, Chaos, crowd collision | 7 |
| MMORPG-specific | population tiers, AOI, significance, player cap, social/inventory replication, persistence | 3, 4, 9 |
| Project structure | modules, GameFeature, DataAssets, scalable class/weapon/mount design | 8 |
| Profiling/testing | Unreal/Network/Memory Insights, automation, regression/load test | 0~9 지속 |

## 5. 애니메이션 locomotion 책임 계약

```text
Gameplay / CharacterMovement / GAS
  -> authoritative movement and gameplay facts
  -> UProject_JLocomotionAnimStateComponent
       -> semantic locomotion snapshot / revision
            -> AnimInstance proxy
                 -> Chooser: pose source / one-shot data selection
                 -> MM: continuous idle/cycle/turn redirect pose selection
                 -> BlendStack: start/stop/pivot/TIP/air/land authored one-shot
                 -> AnimGraph + linked layers: routing/composition
```

- `EProject_JLocomotionPoseSource` 같은 단일 presentation route는 Stage 2의 authority audit 결과가 실제 중복을 보일 때 도입한다. 현재의 단순한 debug surface 차이만으로 도입하지 않는다.
- one-shot 종료/interrupt 후에는 `MM re-entry contract`를 가진다: trajectory, current facing, PoseHistory, force-reselect/reset policy, interrupt reason을 명시한다.
- continuous MM, one-shot BlendStack, mounted/traversal/fallback의 ownership을 하나의 source가 결정하고 ABP가 gameplay semantic state를 재결정하지 않는다.
- local autonomous, simulated remote, AI/NPC의 search/update/IK quality는 동일할 필요가 없다.

## 6. 매 단계 공통 검증 게이트

1. 변경 전: scenario, population, build/hardware, baseline trace, correctness expectation을 기록한다.
2. 변경 후: compile/automation, two-client 또는 dedicated-server correctness, Unreal/Network/Memory Insights를 위험도에 맞춰 수행한다.
3. 결과: frame/server tick p50/p95/p99, memory/GC, bytes per client, actor/object count, search/update count, visual/correctness result를 전후 비교한다.
4. regression: 정해진 local/10/30/50/100 scenario를 반복할 수 있게 한다.
5. 실패 시: 최적화가 아니라 이전 안전한 경로를 유지하고 원인을 분리한다.

## 7. 즉시 실행 금지 목록

- 프로파일 없이 GAS/Actor/UObject/RPC/CMC를 임의 worker thread로 이전
- Iris plugin/cvar 존재만으로 custom Iris relevance가 live라고 가정
- asset graph를 C++ 문서만으로 추측하거나 `.uasset`를 텍스트로 수정
- GASP Full MM 또는 Experimental State Controller를 프로젝트에 통째로 이식
- one-shot이 MM debugger에 보이지 않는다는 이유만으로 selection/replication 구조 변경
- 대규모 object pooling, Mass 전환, server meshing, persistence/economy backend를 population evidence 전에 구현

## 8. 다음 착수 순서

1. **Stage 0:** Animation 기준 문서의 부재를 해소하고, profiling scenario/성능 예산표를 확정한다.
2. **Stage 1:** UE 5.8 source + `ABP_Humanoid_Master` 실제 graph를 근거로 Animation Thread Ownership / Fast Path / snapshot audit을 수행한다.
3. **Stage 2:** dedicated-server 2-client vertical slice를 완성하고 correctness gate를 만든다.
4. **Stage 3:** 측정된 actor/connection budget 위에서 Iris 또는 ReplicationGraph를 선택하고 AOI를 연결한다.

이 순서가 정해진 뒤에만 Stage 4~9의 규모 확장을 진행한다.
