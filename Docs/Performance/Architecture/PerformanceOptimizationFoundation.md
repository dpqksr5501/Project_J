# Performance Optimization Foundation

## Purpose and scope

This document defines the performance foundations that exist before the project has
large-scale NPC combat, pooled gameplay actors, or a final rendering/content budget.
They are intentionally conservative: none of these foundations silently changes an
Actor's behaviour, starts gameplay work on a worker thread, or enables object reuse.

Project J is an UE 5.8 third-person action MMORPG prototype. Local players use the
high-quality locomotion path; **NPCs, bosses, and monsters do not use Motion
Matching**. They should use a low-cost blendspace/sequence/cached-pose animation
path and opt into simulation quality reductions independently.

## Measurement comes first

The source-level trace scopes added by Project J are annotations for **Unreal
Insights**. They are not a custom profiler and do not replace engine tooling.

Use the official Unreal workflow:

1. Capture a packaged Development build with Unreal Insights.
2. Use `stat unit` to classify Game, Draw, and GPU bottlenecks.
3. Use `stat game`, `stat anim`, and `stat gpu` to narrow the affected system.
4. Inspect the Project J CPU trace events, then compare the same scenario before
   and after a proposed change.
5. Record average frame time and hitch percentiles, not FPS alone.

Current Project J trace events:

- `Project_J_PlayerCharacterTick`
- `Project_J_AnimShouldSkipNativeUpdate`
- `Project_J_ServerSideRewindTick`

The server-side rewind history uses a fixed-capacity circular buffer. Recording or
expiring a transform does not move the rest of the history array or allocate memory;
historical hit lookup remains a binary search over logical oldest-to-newest indices.

`DumpMMOProfilingSnapshot` remains a lightweight context dump: it reports character
roles and animation budget tiers. It complements Insights but does not measure CPU
or GPU duration accurately enough to approve an optimization by itself.

### Initial repeatable baseline

For each target hardware tier, measure a fixed map and camera path with 10, 30, and
50 visible characters. Record:

- Game, Draw, and GPU frame time
- total and per-tier player/NPC counts
- AnimGraph, Motion Matching, and skeletal mesh cost
- network replication rate when running two-client PIE or dedicated server
- hitch count during initial equipment, skill, and VFX usage

Do not add parallel work, Mass representation, or pooling because an average FPS
number is low. Identify the limiting thread or GPU pass first.

## Existing runtime policy

### 2026-10-12 시각 애니메이션 예산

원본 메시의 ABA·필수 포즈 요구는 기존 서비스가 담당하고, `UProject_JAnimationUpdateCoordinatorComponent`가 리타깃 몸체의 평가와 의상 물리를 같은 주기로 관리한다. 안정 근거리는 원본 ABA 상태에 따라 매 프레임 또는 30Hz, 중거리 30Hz, 원거리 15Hz 평가와 사이 프레임 보간을 사용한다. Leader Pose 장비는 기존 포즈 공유를 유지한다.

로컬·전투·게임플레이 포즈 요구·몽타주·루트 모션·단발·공중·Turn/Reface는 매 프레임 평가를 보호한다. 화면 복귀는 즉시 평가와 이력 초기화를 수행한다. GT 정책 판정이 worker proxy 완료를 기다리지 않도록 하고, 실제 리타깃 평가의 원본 포즈 의존은 유지한다. 전역 복구는 `ProjectJ.VisualAnimationBudget.Enabled 0`이며 캐릭터·메시별 제외도 지원한다.

현재 비교 기준은 사용자가 지정한 200명이다. [구현·동일 바이너리 비교·보호와 복구·검증 한계](../../Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md)를 따른다. 앞의 10/30/50명 기준선은 초기 수집 제안이며, 최근 200명 CPU 측정이 실제 렌더 FPS·GPU·Cook 검증을 대신하지 않는다.

### Player characters

Player characters use Motion Matching. Remote visual locomotion already has
Near/Mid/Far/Hidden tiers, a reduced update cadence, and far-only chooser rows.
The local player must preserve full input responsiveness and is not globally
throttled by Significance.

### NPCs, bosses, and monsters

`AProject_JNPCCharacter` intentionally has no actor tick by default and applies:

- low network update frequency and net cull distance
- skeletal mesh Update Rate Optimization
- `OnlyTickPoseWhenRendered`
- a non-Motion-Matching animation expectation

`FProject_JNPCUpdateBudgetSettings` provides Near/Mid/Far/Hidden intervals.
The current `Project_JNPCDecisionSubsystem` consumes the recommended tier interval
for registered NPC decisions and schedules registered `NPCActionComponent` work.
This is an opt-in runtime path, not a global Actor tick throttle. Registration and
each consumer's policy determine which work is reduced. Never reduce server hit
validation simply because an NPC is distant.

For current UI demand, mesh update-request composition and locomotion cadence
contracts, see [Character component ownership](../../Architecture/Runtime/Reports/Character_Component_Ownership_2026-09-20.md).
Other future-looking sections below describe the original foundation; consult
the current system documents before treating an item as unimplemented.

## Future pooling contract

`UProject_JObjectPoolRegistrySubsystem` stores `FProject_JObjectPoolDefinition`
records only. It does not acquire, release, prewarm, spawn, or destroy any object.
This provides a stable vocabulary before actual needs exist:

- pool id
- actor, Niagara component, or widget kind
- future prewarm and retained-count limit
- whether authority owns the eventual pool

Before connecting a monster to a real pool, define all of the following:

1. Reset contract: gameplay tags, ASC state, movement, collision, attachments,
   timers, delegates, damage state, and replicated properties must be reset.
2. Network contract: the server owns acquire/release; clients never revive a stale
   replicated actor locally.
3. Lifetime contract: stale async loads and callbacks must carry a generation id.
4. Visibility contract: release must disable collision, rendering, ticking, and
   replication as appropriate before reuse.
5. Profiling proof: demonstrate Spawn/Destroy, GC, or hitch cost before enabling
   pooling for a class.

## Gameplay async contract

`FProject_JGameplayAsyncWorkContract`는 값 계산과 GT 상태 변경의 경계를 정의한다. 현재 타깃 점수 서비스는 `UE::Tasks`·`ParallelFor`, Mass 이동은 별도 청크 병렬 계산을 사용한다. 이 타입 자체가 모든 작업을 받는 공통 실행기는 아니다. [현재 멀티스레드 구조](../../../README.md#멀티스레드-구조)와 [기존 기반을 활용한 시각 예산](../../Animation/Planning/Crowd_Visual_Presentation_Budget_2026-10-12.md#기존-병렬-아키텍처와의-관계)을 함께 읽는다.

- Worker input must be copied, plain data only.
- A worker must never access `UObject`, `Actor`, `Component`, `UWorld`, or GAS state.
- The result is applied on the Game Thread only.
- Results need a request token/generation so obsolete results can be discarded.
- A concrete user must prove that dispatch overhead is smaller than the saved
  Game Thread cost before using `UE::Tasks`, `ParallelFor`, or a thread pool.

Good future candidates: large candidate filtering, crowd steering preparation,
path-request preprocessing, and bulk data conversion. Poor candidates: a small
per-NPC branch, direct Actor mutation, and server-authoritative state changes.

## Rendering and content policy

GPU-driven rendering, HISM, HLOD, Nanite, impostors, material simplification, and
Niagara scalability are mostly content and renderer decisions rather than generic
gameplay C++ features. Do not create a custom renderer abstraction yet.

Before using a representation technique, profile the target scene:

- Render Thread bound: inspect draw calls, component count, material state changes,
  and repeated meshes. Consider ISM/HISM or merging static environment content.
- GPU bound: inspect GPU Visualizer/Insights for shadows, translucency, Lumen,
  material cost, and post process cost.
- Skeletal crowd bound: first use animation URO, leader pose, visibility ticking,
  and NPC simulation LOD. Move to Mass representation/impostors only after a
  measured crowd target requires it.

The current renderer settings enable Lumen, Virtual Shadow Maps, ray tracing, and
Substrate. They are feature choices, not free optimizations; define scalability
tiers only after measuring target hardware.

## Deferred work checklist

- Actual Actor/Niagara/Widget pool implementation and monster integration
- AI cadence consumer, time-slicing scheduler, and spatial query service
- Worker-thread gameplay jobs
- Mass processors, representation LOD, and Actor/Mass handoff
- Iris/Replication Graph runtime adapter work
- HISM/HLOD/Nanite/impostor content audit
- automated performance regression capture/CI thresholds

Each item needs a measured bottleneck, a concrete owner, and a before/after
Unreal Insights capture before it is promoted from foundation to runtime behaviour.
