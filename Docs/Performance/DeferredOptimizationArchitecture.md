# Deferred Optimization Architecture

## Intent

This is a declaration-and-decision document, not an implementation plan to enable
every optimization feature. Project J should add a runtime optimization only after a
repeatable Unreal Insights capture demonstrates its bottleneck.

The following foundations exist today:

- `FProject_JGameplayAsyncWorkContract`: rules for future data-only worker tasks
- `FProject_JObjectPoolDefinition` and `UProject_JObjectPoolRegistrySubsystem`:
  pool vocabulary and definition registry only; no object reuse occurs
- `FProject_JNPCUpdateBudgetSettings`: AI quality vocabulary only; no NPC AI is
  currently throttled by it
- character Significance, animation URO, visibility-based animation ticking, and
  remote Motion Matching quality tiers: active character presentation optimizations
- server-side rewind fixed-capacity ring buffer: active server memory/churn
  optimization

NPCs, bosses, and monsters remain on a non-Motion-Matching animation path.

## Deferred feature contracts

| Feature | Declaration now | Do not implement until | First safe integration point |
| --- | --- | --- | --- |
| Actor pooling | Pool id, kind, prewarm/retention count, authority ownership | Spawn/Destroy or GC hitch is measured | Monster spawn lifecycle with an explicit full reset contract |
| Niagara/UI pooling | Same pool vocabulary | VFX/UI object churn is measured | Damage number or repeated combat VFX manager |
| Gameplay worker jobs | Work kind, request token, stale-result rule | A job costs enough to exceed dispatch overhead | AI target-candidate snapshot or crowd steering preprocessing |
| `ParallelFor` | Work contract marks a bulk data task | Hundreds/thousands of independent elements are measured | Plain position/stat array, never Actor array |
| Time slicing | NPC Near/Mid/Far/Hidden intervals | AI, perception, or navigation is present and measurable | AI coordinator with a per-frame work budget |
| Spatial hash/grid | Query category and world partition decision | Candidate scans dominate AI/skill profiling | Combat target-query subsystem |
| Mass representation | Existing Mass base types | Measured Actor/SkeletalMesh scale limit | Crowd-only NPC representation, not boss combat first |
| HISM/HLOD/impostor | Content representation policy | Render Thread/GPU scene analysis proves draw/mesh cost | Repeated static environment and distant crowd content |
| GPU compute/GPU-driven drawing | Renderer decision only | GPU profiler and target-platform support justify it | Engine/content pipeline work, not gameplay module code |
| PSO/shader warmup | Load/prewarm sequence policy | First-use shader/PSO hitch is captured | Map/skill/weapon preloading flow |
| Network relevance adapter | Existing policy calculation | Multiplayer replication scale needs it | Iris/Replication Graph adapter after a load test |

## Non-negotiable rules

### Worker threads

- Copy plain input data before dispatch.
- Never read or mutate `UObject`, `AActor`, `UActorComponent`, `UWorld`, or GAS
  state on a worker.
- Apply results on the Game Thread only.
- Give every request an epoch/token; discard a result if its owner changed state.
- Do not create one task per NPC. Batch independent work by system and budget it.

### Pooling

- Pool ownership is server-authoritative for replicated Actors.
- A release must reset timers, delegates, tags, ASC state, collision, movement,
  visibility, attachments, replicated state, and stale async callbacks.
- A pool is not an excuse to keep unlimited inactive objects in memory.
- Do not pool an object until Spawn/Destroy/GC is a demonstrated cost.

### NPC, boss, and monster policy

- No Motion Matching.
- Keep boss authority, hit validation, and phase changes at full correctness.
- Only independently safe work is degradable: target scans, perception refresh,
  path refresh, crowd steering, distant cosmetics, and VFX.
- Never use distance alone to suppress gameplay that can affect a nearby player.

### Rendering

- Use Unreal Insights/GPU Visualizer first: classify Render Thread vs GPU pressure.
- Prefer engine/content facilities—Nanite, HLOD, ISM/HISM, texture streaming,
  Niagara scalability, material simplification—before custom GPU-driven code.
- Validate both target hardware and packaged builds; Editor measurements are not
  shipping performance.

## Promotion checklist

A deferred feature can move to implementation only with all of these:

1. Reproducible scene and character count.
2. Before capture showing the affected thread/pass and milliseconds.
3. Correctness constraints, including authority and teardown.
4. A limited initial consumer rather than project-wide automatic activation.
5. After capture proving improvement without worse hitch, memory, or visual cost.
