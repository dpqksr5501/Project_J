# Project_J Scalability Profiling & Validation Plan

**Date:** 2026-09-03  
**Purpose:** MMORPG 기능·최적화의 판단을 평균 FPS가 아닌 재현 가능한 correctness, server tick, CPU/GPU, memory, replication 예산으로 수행한다.  
**Rule:** 수치가 없는 최적화는 제안 상태로 유지한다.

## 1. 공통 기록 규격

각 실행은 다음을 함께 기록한다.

| 항목 | 기록 |
|---|---|
| 환경 | commit, build config, hardware, driver, map, server/client mode |
| population | local/autonomous, simulated remote, active NPC, ambient Mass, effect 수 |
| workload | combat/locomotion state, camera distance, packet loss/latency 조건, duration |
| frame/server | frame time 및 server tick p50/p95/p99, worst frame, hitch count |
| CPU | Game Thread, render/RHI, parallel animation, worker/task graph, AI, replication |
| GPU | frame, skeletal mesh, shadow, Niagara, Lumen/VSM/RT relevant passes |
| network | bytes per client, relevant actor/object count, replication update frequency, packet/loss behavior |
| memory | resident memory, UObject/component count, GC time, streaming pool, asset/PSD/mesh residency |
| correctness | authority, prediction, SSR, owner-only condition, one-shot/MM presentation, visual regressions |

## 2. Population ladder

| ID | Scenario | 주 검증 |
|---|---|---|
| S0 | local player 1명 | baseline locomotion/combat, MM/BlendStack/IK, local prediction |
| S1 | local + remote 10명 | simulated-proxy animation, replication, initial GT/parallel slope |
| S2 | local + remote 30명 | AOI/frequency policy, animation budget/search staggering 후보 |
| S3 | local + remote 50명 | p95/p99 spike, bandwidth and relevance budget, server CPU |
| S4 | local + 100 low-significance actors | representation/animation/AI tier. 구현된 harness가 있을 때만 실행 |
| S5 | active combat NPC + ambient crowd | promotion/demotion, AI/perception, Mass/actor boundary |
| S6 | target effects/rendering load | mesh/material/shadow/Niagara/collision budget |

S4 이후는 Stage 3~4의 representation과 test harness가 준비되기 전에는 목표 시나리오이며 현재 성능 주장에 사용하지 않는다.

## 3. 도구별 질문

| 도구 | 답해야 할 질문 |
|---|---|
| Unreal Insights | GT는 무엇을 기다리는가? parallel animation/MM/AI/replication의 p95 spike는 어디서 생기는가? |
| Network Insights / net stats | connection별 relevance, bytes, update rate, RPC/FastArray 비용이 예산 안인가? |
| Memory Insights / `memreport` | UObject/mesh/component/SSR history/temporary allocation/GC 중 무엇이 population에 비례하는가? |
| `stat unit`, GPU Visualizer | rendering target이 GT가 아닌 GPU/RHI/render thread에 막히는가? |
| automation + multi-client PIE/dedicated server | authority, duplication, owner-only leak, reconnect/handover, remote presentation이 올바른가? |

## 4. 분야별 성능 게이트

| 분야 | 측정 전 금지 | 측정 후 선택 가능한 조치 |
|---|---|---|
| CPU/threading | GAS/Actor/UObject/RPC/CMC의 강제 worker 이동 | immutable snapshot, event revision, remote stagger, pure-data job |
| Motion Matching | 모든 PSD/DB/channel을 일괄 축소 | near/far DB, channel reduction, pruning, PCA/KDTree, throttle |
| Animation | GASP graph 전체 이식, quality 일괄 하향 | ABA, URO tier, Animation Sharing/Leader Pose, IK/procedural budget |
| Network | Iris helper를 live system으로 선언 | Iris/ReplicationGraph 선택, rate bucket, dormancy, relevancy/filter/priority |
| NPC/Mass | 모든 AI/NPC를 Mass로 대체 | active actor tier, AI schedule, Mass ambient, promotion/demotion |
| Memory | 전역 object pool 도입 | asset lifetime/bundle, constrained pools, component/mesh reuse |
| rendering/physics | global quality toggle만 변경 | device profile, LOD/material/effect/collision/query budget |

## 5. Regression gate

어떤 작은 변경도 아래를 충족해야 유지한다.

1. 변경 전후 같은 population/workload/trace 조건을 사용한다.
2. 개선 대상 수치와 악화될 수 있는 수치를 함께 비교한다.
3. p95/p99 또는 hitch가 나빠지면 평균 개선만으로 채택하지 않는다.
4. remote simulated proxy, local autonomous player, AI/NPC 각각의 correctness를 분리 검증한다.
5. networking 변경은 dedicated server 또는 최소 two-client authority test를 포함한다.
6. 결과를 roadmap/audit에 연결하고, 근거 없는 tuning은 되돌리거나 보류한다.

## 6. 초기 성공 기준

초기 수치 목표는 target hardware와 gameplay/player cap이 확정되기 전에는 고정하지 않는다. 우선 성공은 다음과 같다.

- 각 scenario가 재현 가능하고 trace를 비교할 수 있다.
- 1→10→30→50의 비용 기울기와 병목 thread가 알려져 있다.
- connection별 replication budget과 actor tier가 문서화되어 있다.
- one-shot/MM, GAS/CMC/SSR/FastArray가 다중 클라이언트에서 정확하다.
- CPU/GPU/memory/network 중 실제 우선 병목 하나를 증거로 선정할 수 있다.

