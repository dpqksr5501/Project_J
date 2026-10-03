# Project_J MMORPG Architecture & Performance Audit

**Date:** 2026-09-03  
**Scope:** Phase 1, source/config/git-history audit only. No C++ code, assets, or project configuration was changed.  
**Evidence boundary:** C++ and `.ini` were inspected. Post-audit editor screenshots verified the `BP_GreatSword` → `ABP_Humanoid_Master` animation assignment, Master AnimGraph composition, selected Motion Matching Asset Sets, representative State Controller/Chooser tables, and the empty `BP_Project_JGameMode` Event Graph. Blueprint/asset coverage remains intentionally partial: map/world-partition, Niagara, Mass placement, Iris runtime state, full Event Graph and PoseSearch schema/index contents were not inspected. A plugin being enabled or a class existing is not treated as runtime integration.

## 1. Executive summary

Project_J is an early, promising single-character combat/locomotion vertical slice, not yet an MMORPG runtime. Its strongest implemented path is the player character: PlayerState-owned GAS, CharacterMovement, data-driven combat/equipment, native locomotion state, Motion Matching presentation, and sparse cosmetic replication. Editor evidence confirms that `BP_GreatSword` uses `ABP_Humanoid_Master` and `DA_Greatsword_AnimProfile`; the Master graph routes State Controller presentation through Motion Matching or authored Chooser/Blend Stack one-shots, then through upper-body composition, Offset Root Bone, Foot Placement/Leg IK, Pose History, and mounted locomotion blending. The code deliberately avoids most component ticks and has real FastArray, async visual-layer loading, significance measurement, remote-animation throttling, and server-side-rewind foundations.

The main architectural risk is the gap between **policy/prototype** and **connected runtime system**. The Iris plugin and intent cvars are present, but the UE 5.8 official runtime activation/verification path (`IrisNetDriverConfigs` or `-UseIrisReplication=1`) and `SetupIrisSupport(Target)` are absent; the custom distance filter/prioritizer are plain `UObject` policy helpers with their Iris adapter methods commented out and no registration path. Mass has one trait plus a test spawner, but no processors, representation bridge, AI, replication, or LOD policy. The handover, object-pool, backend, GameFeature, and async-work types are useful seams but are not production subsystems. The default map is still `Lvl_ThirdPerson`, while the repository also contains a world-partition-looking map; this must be verified in the editor before any streaming conclusion.

The recommended direction is not “put more work on threads.” First define an authoritative zone-server/interest-management boundary and prove actor classes against a population budget. Keep gameplay authority and UObject access on the Game Thread; use engine-supported parallel animation/async loading only for presentation. Build a tiered representation model: full `ACharacter` only for local, near players, bosses, and active combatants; throttled character presentation for mid/far actors; Mass/instanced representation for ambient populations. Do not integrate server meshing, broad pooling, custom Iris filters, or complex backend persistence until a vertical-slice load test establishes their need.

## 2. Runtime architecture map

```text
Enhanced Input (local player)
  -> PlayerInputBinding / SkillInputRouter
  -> SkillInputExecution RPC + GAS activation (PlayerState ASC)
  -> Character gameplay state / CharacterMovement / equipment FastArray
  -> PlayerCharacter Tick: trajectory + locomotion-state update
  -> AnimInstance game-thread snapshot -> AnimInstanceProxy
  -> engine parallel animation update/evaluation (when enabled by engine/mesh)
  -> PoseSearch / Chooser / BlendStack graph (asset wiring unverified)
  -> skeletal mesh -> render thread

Server authority
  -> CharacterMovement replication + ASC/attribute replication
  -> inventory/equipment FastArray, mount, combat presentation, sparse anim events
  -> simulated proxy applies semantic anim events and temporarily disables URO

GameMode -> GameState world-instance prototype
         -> PlayerState prototype identity/social state
         -> Gateway/Handover prototypes (not a production backend path)
```

`AProject_JPlayerCharacter` owns the local presentation/control components, while the player ASC, attributes, inventory, and equipment manager are deliberately hosted by `AProject_JPlayerState`. NPCs instead create local ASC/attributes/equipment. This is a sound ownership split, but its persistence/reconnect lifecycle is not yet implemented.

## 3. Implementation status matrix

| Subsystem | Status | Evidence and audit finding |
|---|---|---|
| Core module, tags, interfaces | Partially Implemented | `Project_JCore` is a useful low-level module; AssetManager only logs initialization and pool registry stores definitions only. |
| Module layering | Partially Implemented | `Core -> GAS`, `Core -> Character`, `GAS -> Character`, `Mount -> Character`; game module depends on all. `Character` is a large feature bucket (combat, animation, UI, Mass, network). |
| Player GAS / attributes | Implemented | PlayerState ASC uses Mixed replication; attributes replicate with notify; class/ability-set grants are server guarded. |
| Combat / equipment / inventory | Partially Implemented | Gameplay abilities, FastArrays, presentation profiles and SSR are present. Only prototype starting equipment and no persistence/transaction service are wired. |
| Character movement / prediction | Partially Implemented | Standard `UCharacterMovementComponent` is used, so UE client prediction/authority path exists; no custom saved moves, network prediction model, or population policy. |
| Motion Matching locomotion | Partially Implemented | Native state/trajectory/proxy and adaptive near/mid/far policy are substantial. Editor evidence verifies the Master AnimGraph's State Controller → Motion Matching/Chooser/Blend Stack composition and locomotion/combat Asset Set assignment; complete PSD schema/index and update-graph coverage remains unverified. |
| Animation parallelism | Partially Implemented | `FAnimInstanceProxy` transfers a game-thread snapshot to node update; engine controls parallel execution. No evidence all chosen AnimBP nodes/functions are thread-safe. |
| Remote animation presentation | Implemented | `ReplicatedAnimEventComponent`, jump state, event ordering, `COND_SkipOwner`, and brief URO override are connected. |
| NPC actor scalability | Infrastructure Only | NPC disables actor tick, uses URO/visibility tick and suggested AI interval; there is no scheduler consuming that interval. |
| Mass monsters | Experimental | One stats fragment/trait and a 100-count auto spawner; no processor, representation, spawn/despawn, AI, combat, or replication bridge. |
| Iris interest/prioritization | Infrastructure Only | Iris plugin and intent cvars are present, but UE 5.8 NetDriver runtime activation/verification and `SetupIrisSupport(Target)` are absent. Custom filter/prioritizer are unregistered helper UObjects, explicitly independent from future Iris glue. |
| Replication / FastArray | Partially Implemented | Inventory owner-only and equipment FastArrays are real. No ReplicationGraph/Iris filter assignment, dormancy policy, or measured replication budget. |
| Mount / flight | Partially Implemented | Replicated mount state and flight actor path exist; flying mount ticks every frame. Scale policy is absent. |
| Asset management / async loading | Partially Implemented | Custom AssetManager exists; combat, mounted layer, and equipment visual load paths are async. Primary assets are mostly `AlwaysCook`, not streaming bundles/chunks. |
| World partition / streaming | Planned | `Main_World_WP.umap` and HLOD assets exist, but default runtime map is `Lvl_ThirdPerson`; no code/config establishes streaming source, data layers, or server partition policy. |
| Rendering / VFX | Infrastructure Only | Lumen, VSM, RT, Substrate and DX12 SM6 are enabled. No project-level scalability/per-platform/device-profile policy observed; Niagara pool enum is not a VFX system. |
| Modular character | Partially Implemented | Runtime mesh component is created by equipment runtime. Merge strategy, slot limits, LOD/material budget, and crowd representation are unproven. |
| UI / MVVM | Partially Implemented | ViewModel and binding component exist; Blueprint widget binding/runtime usage needs editor verification. |
| Backend / persistence / handover | Infrastructure Only | HTTP gateway has request IDs/idempotency headers and disabled telemetry; GameMode generates prototype IDs; handover supports loopback/prototype snapshots. No authentication, durable store, queues, or zone orchestration. |
| Dedicated server | Planned | No `Project_JServer.Target.cs`, deployment config, headless asset policy, or server load test found. |
| Security boundary | Partially Implemented | Authority checks, owner-only IDs, input timestamp clamp, SSR validation exist. Identity is locally generated in GameMode and gateway sends arbitrary payload strings: not a secure trust boundary. |
| Profiling / observability | Partially Implemented | CPU trace scope, debug commands/CVars and remote-telemetry queue exist. No automated trace capture, metrics aggregation, budget gates, or production redaction proof. |
| Testing | Partially Implemented | C++ automation tests cover architecture policy/SSR/handover helpers. No multi-client PIE, soak, dedicated-server, replication scale, cook, or asset-graph tests. |

## 4. Threading map and Game Thread risks

| Domain | Observed responsibility | Assessment |
|---|---|---|
| Game Thread | Input, GAS/RPC, CharacterMovement, PlayerCharacter tick, significance callback, component delegates/timers, UObject creation/replication | Correct ownership. Local player tick is required, but it combines walk-speed/rotation, trajectory generation and locomotion update every frame. Profile it before splitting work. |
| Parallel animation update/evaluation | Proxy receives copied `FProject_JAnimThreadSafeData`; PoseSearch node policy is applied within proxy/node update | Good direction. Engine eligibility is asset/AnimBP dependent; Blueprint calls, UObject reads, or non-thread-safe graph nodes can collapse this back to GT. |
| Task Graph / worker | No project-owned TaskGraph/`UE::Tasks`/`Async` gameplay jobs found. HTTP callback and handover use async completion back to GT. | This is preferable to unsafe UObject threading at this stage. There is no demonstrated worker-thread gameplay bottleneck to “fix.” |
| Async loading | `FStreamableManager::RequestAsyncLoad` for visual equipment/combat/mount layers | Valid presentation path. Guard lifetime/cancellation and loading bursts; current usage is per character/equipment transition. |
| Physics | Chaos through CharacterMovement/capsule and teleport physics handover | No explicit collision channel/profile, query budget, or crowd collision scheme was found in source/config. |
| Render Thread | Engine-owned mesh/render pipeline | Project enables expensive desktop features globally; no project evidence of render-thread/RHI profiling or scalability tiers. |

Serial scaling hazards: each player `Tick` executes trajectory and state work; each base character registers a significance object whose calculation includes distance for every viewpoint; remote one-shots may disable URO concurrently; and full character/ASC/collision remains the default representation. The animation proxy uses reflection (`FindFProperty`) and iterates generated Motion Matching nodes; cached static reflection avoids repeated lookup, but node iteration/search cost must be measured at crowd scale. No project lock contention or forced task wait was found.

## 5. Networking map

```text
Autonomous proxy: input/GAS prediction -> Server RPCs / CharacterMovement saved-move path
Server: validates authority, timestamps and SSR -> mutates GAS, attributes, inventory/equipment,
        combat/mount state -> standard replication/Iris-enabled replication
Simulated proxy: receives movement plus replicated semantic animation/jump/combat presentation
        -> applies visual state, requests a short high-priority animation window
```

Strengths: PlayerState private account/character identifiers are `COND_OwnerOnly`; inventory is owner-only FastArray; equipment is FastArray; remote-only visual bools/events are skipped for owner; NPC ASC uses Minimal GE replication; SSR uses server pose history and finite/age checks.

Critical limitation: enabling Iris (`DefaultEngine.ini`) is **not** equivalent to deploying interest management. The classes named `NetObjectFilter` and `NetObjectPrioritizer` do not subclass/register Iris interfaces; their adapter overrides are comments and their live call sites are debug policy calculations in PlayerController. Consequently, current relevancy remains engine default actor replication plus per-NPC cull/update settings. No actor dormancy, replication graph, connection budget, frequency buckets, or replication condition mapping by actor class was found.

## 6. Strengths to retain

- PlayerState-owned persistent combat state is appropriate for respawn/pawn replacement and avoids duplicating a player ASC.
- FastArray is selected for mutable inventory/equipment rather than replicating whole arrays.
- Animation presentation deliberately separates sparse semantic events from movement replication and protects the owner from duplicate cosmetics.
- The proxy snapshot approach is safer than reading gameplay UObjects during parallel animation update.
- Most utility/gameplay components explicitly disable ticking; temporary presentation ticks turn themselves back off.
- Significance is used as a measurement rather than globally slowing actor ticks, avoiding a common correctness bug.
- Source comments accurately label several systems as prototype/future glue, and recent history shows active regression correction in locomotion rather than blindly retaining stale documents.

## 7. Architecture problems

### P0 — Server identity and persistence are prototype-only

`GameMode::AssignPrototypeIdentity` creates new GUIDs at login. There is no authenticated account/session, character-load ownership check, durable inventory/economy transaction, replay protection, or authoritative zone handoff. Never promote this path to an internet-facing dedicated server. Move identity issuance and character snapshot loading behind a server-to-backend service; clients must never supply account/character authority.

### P0 — Iris policy is not live

Do not describe the project as using custom Iris interest management. Establish an actual registered Iris filtering/prioritization adapter (or choose ReplicationGraph), configure it by actor class, and prove it through a multi-client replication capture before attaching gameplay expectations to it.

### P1 — “Character everywhere” has no representation architecture

NPC and Mass foundations do not select or swap representations. Without a zone population model, thousands of NPCs will mean actor replication, ASC, skeletal meshes, collision, animation and AI scale roughly with count. A distance check alone cannot solve server simulation or memory pressure.

### P1 — Animation state authority is distributed

Player tick/state component, AnimInstance, proxy, replicated semantic event component, combat state, and BP graph may all contribute locomotion decisions. The native code has safeguards, but asset graphs are uninspected. Define one authoritative gameplay locomotion snapshot and one presentation state machine; Chooser/PoseSearch should select assets, not re-author gameplay state. In particular, keep continuous locomotion MM separate from authored one-shots (start, stop, pivot, TIP, jump/fall/land and combat draw) and explicitly define re-entry rules/PoseHistory reset policy.

### P1 — Server/world topology is absent

World partition asset presence is not a zone architecture. There is no dedicated target, zone ownership, transfer protocol beyond a local snapshot prototype, AOI policy, cross-zone chat/party policy, or server-only world streaming/cooking strategy.

### P2 — Feature module boundaries will become expensive

`Project_JCharacter` currently owns gameplay, combat, animation, Mass, networking, UI, inventory and equipment. Split only when ownership teams/content cadence require it, but establish dependency rules now: Core has no game feature dependencies; GAS has no presentation dependency; network interest is a server service; animation presentation consumes immutable snapshots.

### P2 — Rendering target is unconstrained

RT + Lumen + VSM + Substrate at maximum desktop settings are reasonable visual experiments but not an MMO performance contract. No device profile, scalability buckets, shader/cook budget, mesh/material draw-call target, or VFX budget exists.

## 8. Performance risks by population tier

| Tier | Target representation | Current risk |
|---|---|---|
| Local player | Full CharacterMovement, ASC, full MM, camera/UI | Reasonable; profile player tick, PoseSearch, collision and ability prediction. |
| Nearby players / boss | Full actor, relevant ASC/combat, MM at budgeted rate, capsule/query collision | No live AOI budget; overlapping URO disable windows and full component sets can spike CPU. |
| Distant players | Movement + sparse cosmetic state; reduced animation/mesh update | Partial animation throttling exists, but no live replication filter nor verified asset graph. |
| Important NPC | Full actor only while relevant/active | NPC policy reports recommended AI interval but no consumer schedules it. |
| Generic monsters | Actor or Mass representation selected by combat relevance | No selector/bridge; Mass experimental. |
| Ambient crowd | Mass/instanced visuals, batched queries, no ASC/CharacterMovement/individual replication | Not implemented. |

Specific risks: all registered characters run significance calculations; full mesh component creation for modular equipment can create memory/draw-call churn; `FlyingMountCharacter::Tick` is always active; server rewind storage scales with tracked actors and attack volume; physics/collision and notify-tick hit checks need a query budget; and all custom primary assets are `AlwaysCook`, which is a safety choice rather than an async-streaming strategy.

## 9. Overengineering risks

- Do not build server meshing, cross-server handoff, broad custom object pools, or custom network prediction before one authoritative-zone dedicated-server slice has player/NPC load evidence.
- Do not replace every actor with Mass. Use Mass first for ambient/simplified populations; bosses and combat NPCs need an explicit promotion/demotion boundary.
- Do not create a custom task system or move GAS/UObject work to workers. First optimize visibility, representation, scheduling and asset work.
- Do not copy GASP wholesale. GASP full MM and its experimental State Machine/Blend Stack paths are reference patterns, not a networked MMO contract. Project_J should retain its single snapshot authority and only adopt asset-graph mechanisms proven by its content.
- Do not enable remote telemetry with arbitrary gameplay payloads until authentication, schema/versioning, redaction, rate limits and retention are specified.

## 10. Recommended target architecture

1. **Zone server vertical slice:** dedicated-server target; authenticated session -> server-owned character load -> PlayerState/ASC; a clear persistence command boundary with idempotency and audit IDs.
2. **Interest/representation service:** one live Iris-or-ReplicationGraph implementation with explicit connection budgets, relevance reasons, rate buckets and dormancy. Keep policy helpers; add the adapter only after choosing the engine path.
3. **Simulation tiers:** full player/boss actors; active combat NPC actors; low-frequency AI actors; Mass ambient entities; visual-only impostors. Promotion/demotion is event driven (combat, quest, proximity), not a permanent distance poll.
4. **Animation contract:** Game Thread produces an immutable locomotion/combat snapshot. Anim proxy/graph only consumes it. Continuous MM has staggered/budgeted searches; authored transitions have bounded one-shot lifecycle and deterministic re-entry to MM.
5. **Data/presentation:** AssetManager bundles by zone/class/equipment; async-load visuals with cancellation and fallback. Equipment data remains authoritative server content; clients receive only public presentation IDs/state.
6. **Operations:** Unreal Insights trace presets, network captures, server metrics and population test maps define an explicit performance envelope before content growth.

## 11. Priority backlog

| Priority | Action |
|---|---|
| P0 | Replace prototype login GUID assignment with server-authenticated identity/character ownership and a persistence command boundary. |
| P0 | Decide Iris adapter vs ReplicationGraph; do not claim live custom filtering until end-to-end registered and load-tested. |
| P1 | Create a dedicated-server target and a single-zone multi-client soak test. |
| P1 | Define actor/Mass promotion, AI cadence ownership, relevance/dormancy/update-frequency budgets by population tier. |
| P1 | Write and validate animation authority/re-entry contract against actual AnimBP/Chooser/PoseSearch assets. |
| P2 | Split Character module ownership boundaries and add data validation for public/private replicated fields and asset references. |
| P2 | Add device profiles/scalability budgets; measure Lumen/VSM/RT/Substrate before locking visual defaults. |
| P3 | Consolidate duplicate-looking content names, stale documents and prototype debug commands after runtime behavior is baselined. |
| P4 | Tune reflection/node iteration, individual tick intervals, pool sizes, compression and micro allocations only from Insights evidence. |

## 12. Migration roadmap

1. Freeze a representative combat map and capture baseline single/multi-client performance; no broad refactor yet.
2. Build the P0 identity/persistence boundary and dedicated server target; test reconnect, duplicate requests and invalid client input.
3. Make AOI real: adapter registration, actor class buckets, relevance/dormancy settings, network assertions and captures.
4. Introduce a small actor/Mass promotion prototype for ambient creatures while keeping boss/active-combat actors untouched.
5. Audit actual animation assets; then simplify state ownership and set measured MM search/update budgets per tier.
6. Add zone/world streaming and asset bundles only after the one-zone ownership and content load budgets are stable.
7. Expand services (party/guild/economy/handover) behind versioned backend contracts, not direct client HTTP.

## 13. Profiling and observability plan

Before each migration, capture a reproducible matrix: 1 local player; 16/64/128 nearby players; 50 active monsters; 500 ambient entities; plus boss-combat and teleport/load spikes. Run client and dedicated server separately.

- **CPU:** Unreal Insights trace with `cpu`, `task`, `animation`, `loadtime`, `assetloadtime`; compare Game Thread, animation worker time, pose-search cost, CharacterMovement, ability processing, collision and GC.
- **Network:** `stat net`, Network Insights/NetTrace, per-connection actor/channel/object counts, property/RPC bytes, replication time, relevancy churn and packet loss/latency scenarios.
- **Animation:** Animation Insights for update/evaluate rate, URO skips, PoseSearch search count/cost, BlendStack players, proxy eligibility and urgent one-shot windows.
- **Memory/loading:** `memreport -full`, `obj list`, `stat streaming`, AssetManager load handles, resident skeletal meshes/PSDs, GC duration and modular-mesh component counts.
- **Rendering:** `stat unit`, `stat gpu`, GPU Visualizer, draw calls, skeletal mesh cost, shadows, Niagara, Lumen/VSM/RT passes and RHI/Render thread time at target hardware tiers.
- **Server correctness:** automation plus soak tests for authority denial, replay/duplicate request, SSR timestamp bounds, owner-only data leakage, handover version mismatch and Mass/actor promotion.

Every change should state the scenario, hardware/build, population, p50/p95 frame and server tick time, memory, outbound bytes/client and correctness result. A single-editor-player result is not MMO evidence.

## 14. Required screenshots / editor information

The following binary-asset evidence is needed before finalizing runtime conclusions. Please provide these in one batch if possible.

1. **`BP_Player` / `ABP` assigned by its Mesh** — Event Graph plus Anim Graph, including Motion Matching, Pose History, Chooser, State Machine and Blend Stack nodes; **Details Panel: yes** for each MM/Pose History/Blend Stack node. Needed to verify the actual runtime graph and parallel-update safety.
2. **`DA_Player_Locomotion`, `DA_Player_Combat_Strafe`, all referenced PoseSearch DBs and Chooser tables** — database assets/roles, selection columns, query channels and search-throttle settings; **Details Panel: yes**. Needed to validate start/stop/pivot/TIP/jump/landing/combat-strife routing and database scale.
3. **`Main_World_WP` and `Lvl_ThirdPerson` World Settings** — World Partition panel, Data Layers, HLOD layers, streaming sources; **Details Panel: yes**. Needed to establish which world actually runs and whether partition/HLOD is configured.
4. **Project Settings: Networking and Iris/Replication** — relevant Iris config/class registration and replication driver settings; **Details Panel: yes**. Needed to confirm whether custom policy is connected despite no C++ adapter path.
5. **NPC/Mass actor and entity config** — Mass spawner traits/processors/representation/LOD in the level; **Details Panel: yes**. Needed to distinguish sample Mass content from active runtime population.
6. **`BP_Project_JGameMode`, widgets, and Niagara systems used in the test map** — parent class/event graph and assigned widgets/effects; **Details Panel: yes** where class/default asset assignment is relevant. Needed to verify blueprint VM, UI binding and VFX runtime paths.

## Evidence consulted

- Current HEAD `d4fce38a`; recent history gives precedence to the 2026-08/09 locomotion and `de1739f3` performance/SSR changes over older docs.
- `Project_J.uproject`, `Config/DefaultEngine.ini`, `Config/DefaultGame.ini`, `Config/DefaultMass.ini`.
- Module rules and implementation under `Source/Project_J`, `Project_JCore`, `Project_JGAS`, `Project_JCharacter`, and `Project_JMount`.
- No build was run: this is an audit-only documentation deliverable with no compiled-source modification.
