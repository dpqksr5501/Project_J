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

`FProject_JNPCUpdateBudgetSettings` is a declaration-only AI policy. It provides
Near/Mid/Far/Hidden update intervals plus perception/path-refresh permissions.
It does **not** alter AI tick rate until a future AI component explicitly consumes
the policy. This prevents a distance policy from accidentally changing boss combat
or server authority behaviour.

When AI is added, use the policy for expensive, independently safe work such as
target scans, perception refresh, path refresh, and crowd steering. Never reduce
server hit validation or state transitions simply because an NPC is distant.

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

`FProject_JGameplayAsyncWorkContract` declares the rules for future worker tasks.
It intentionally provides no generic thread launcher while Project J has no measured
AI, crowd, or large target-query workload to consume it.

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
