# Combat VFX Architecture

## Runtime boundary

Combat gameplay remains `AttackDefinition`, montage, hit validation, and GameplayEffect driven. VFX never contributes damage geometry, hit timing, or server authority.

```text
AttackTag -> CombatPresentationSet -> AttackPresentationProfile -> Cue definition
Montage Notify State -> CombatPresentationComponent -> Niagara
```

`UProject_JCombatPresentationComponent` is non-ticking. Niagara components never replicate; the server sends only a compact active-attack/loop-cue recovery state plus unreliable cue events for responsive remote presentation. It guarantees cleanup when an attack changes, ends, is cancelled, or the owner is destroyed.

## Override order

For one cue of one `AttackTag`, the resolved definition is selected in this order:

1. `CombatStyleDefinition.CombatPresentationSet` (base style)
2. `CharacterAdvancementDefinition.CombatPresentationOverrideSet` (optional advancement override)
3. `WeaponPresentationProfile.CosmeticPresentationOverrideSet` (optional explicit skin override)

An override set changes only cues it contains. This permits Demon Greatsword and Angel Greatsword to reuse the same `Attack.Greatsword.*` damage/combo definitions while replacing their trail or impact presentation. A weapon skin should normally use its override only for intentional cosmetic replacement; it must not duplicate combat data.

## Authoring

1. Make an `AttackPresentationProfile` per visual attack family and assign Niagara systems to `PresentationCue.Combat.Trail`, `PresentationCue.Combat.Release`, or project-specific cue tags.
2. Add those profiles to a `CombatPresentationSet`, keyed by the stable `Attack.*` tag.
3. Assign the base set to the CombatStyle. Add only changed cues to advancement and skin override sets.
4. For a Notify State-bound trail, assign an internally looping Niagara system, check `Keep Alive Until Notify End`, and place `Project J Combat Presentation Cue` over the exact montage interval. The CueTag must match the profile. This component option tracks the spawned system; it does not make a one-shot Niagara system loop.
5. Select `Destroy Immediately On Notify End` only when no residual particle fade is wanted. Leave it off for a natural trail fade.
6. For a weapon trail, attach the Niagara system to the weapon's existing Base/Tip-authored socket contract. The Niagara asset owns endpoint sampling; gameplay trace sockets remain independent.
7. `Use Remote Spawn Budget` defaults to true, including existing saved cue definitions. Clear it only when the cue must retain its exact instant on remote views, such as a required warning. Verify that exception's cost and readability. Local-controlled/viewed owners retain immediate warm playback. This setting never changes gameplay hit timing.

## Confirmed impacts

The current server hit path confirms a target but does not preserve a confirmed impact point, normal, or physical material. Do not make an impact VFX authoritative yet. When material-aware impact work begins, extend the confirmed-hit context and emit a GameplayCue (or an equivalent confirmed cosmetic event) from that result. Local swing/trail cues remain montage-driven and predicted.

## Performance rules

- Reuse the engine Niagara pool (`ProjectJ.Combat.Presentation.Pool=1`); loops use explicit ManualRelease ownership and one-shots use AutoRelease. Value 0 preserves the unpooled comparison path. Notify cancellation, attack/style replacement and world shutdown must retire the exact existing ownership before callbacks; pooled retention is not active simulation or memory returned to the OS.
- Do not tick C++ per trail frame. Use Niagara, authored bounds, culling, and scalability tiers.
- Validate owner and simulated-proxy presentation under URO, cancellation, weapon swap, death, packet loss, and relevance changes.

### Load, creation and active cost

The shared visual-asset subsystem bounds soft loading and callback delivery. It does not grant permission to create every resident cue. Remote warm loops use the existing desired-cue records and world presentation FIFO, shared with remote equipment. Only the matching attack/path/revision can apply; a matching stop before apply removes the desire. Cold loop completion follows the same cue-specific policy. Missed cold one-shots are never replayed late.

Remote warm one-shots acquire a credit at their authored instant or skip it. Live queued work has priority, and the started-tag claim prevents recovery or a later frame from replaying a skipped instant. Queued applications and these immediate remote attempts share four attempts per world per actual engine frame. Repeated or reentrant Tick calls cannot replenish count, 128 visits, or accumulated queued soft time. The queued timer targets 1ms; the first visit and a single callback can exceed it. Immediate one-shot elapsed time is outside that queued timer. This is a conservative creation-attempt policy, not an FPS target or tuned capacity figure.

Local/viewed owners, authored timing exceptions and editor preview playback retain their immediate paths. A later real Play call can promote a pending cue after a view change; there is no new camera observer that flushes all pending work. These exceptions are outside the remote creation count.

Active Niagara instance/particle/GPU cost, pool retention and warning quality require separate effective System/EffectType policies and measurements. The selected native read on 2026-11-01 found the shared vendor EffectType present, but no effective instance/global-budget cull, and `fx.Budget.Enabled=false`. The existing trail's distance override does not impose an active GPU cap. Do not change a shared vendor EffectType or enable the global FX budget as a substitute for validating a project-owned policy's cull/resume and notify lifetime.

### Current persistent trail policy

The saved Spellblade presentation profile's Trail cue now selects `/Game/Effects/ProjectJ/Combat/NS_CombatTrail`, with the project-owned `NET_CombatTrail`. The original vendor System/EffectType remain unchanged. Emitter/material/readiness data, attachment, notify semantics, the existing 2500cm distance override and fixed bounds are preserved.

The active Spellblade Sword Cues profile lets both Trail and Release particles finish after emission stops. The shared ribbon uses a 0.16-second particle lifetime at 64 samples per second. The selected montage starts are authored from swing motion and stay separate from hit windows. An attack transition retires the old cue immediately and lets its existing particles finish through Niagara's completion-based pool return.

Spellblade ground light attack 3 uses `AM_SpellbladeKit_BasicCut03Ground`. Its saved Trail window is 0.420–0.850 seconds and its hit window is 0.600–0.850 seconds. The 0.420 start was verified by reloading the montage in a fresh Editor process. The earlier 0.483-second authoring pass was also sampled in PIE, where the third swing started its own ribbon after the second swing's tail.

Basic Greatsword resolves its Trail through `DA_CPS_Spellblade_Base` and the legacy-named `DA_AP_Spellblade`. That profile also lets the existing ribbon particles finish when the cue retires. Its montage ComboWindow still controls when buffered input is consumed. Keep that transition point tied to the attack motion and hit timing, and tune the visual tail in the presentation profile and Niagara system.

Rendered standalone PIE compared a single Basic Greatsword light attack with a buffered light 1 → 2 transition. After the profile change, the old Niagara component remained active at the start of light 2 and retired independently of the new cue. `ComboBlendOutWindows` and `ComboInputSubscription` passed. Rendered heavy/Q tail comparison and crowd cost at the new tuning remain separate validation.

The EffectType uses native distance significance, High updates (observed 0.25s), `DeactivateImmediateResume`, and an initial authored count of 128. Locally viewed effects remain protected by Niagara's existing linkage. The value is tunable data; eligible remote instances can exceed it between updates, local effects are exempt, and inactive/pool resources may remain allocated. It is not a measured player capacity, allocation/GPU hard cap or FPS guarantee. The same valid loop can resume when significant again; matching notify/attack retirement releases it from scalability so it cannot return later.

Apply this resume policy to explicitly owned persistent cosmetics. Finite one-shots must not acquire an unowned late-resume lifetime. `Use Remote Spawn Budget=false` exempts creation admission, not EffectType active culling: required warnings need their own appropriate payload/policy and readability validation. Attached effects use dynamic native local linkage; World-attached cues retain their creation-time force-local contract.

For repeatable offline authoring, [the native commandlet](../../../Source/Project_JCharacterEditor/Private/Authoring/Project_JNiagaraScalabilityCommandlet.cpp) accepts an absolute `-Recipe` JSON file. Its nine fields are `SourceSystem`, `DestinationSystem`, `DestinationEffectType`, `PresentationProfile`, `CueTag`, `MaxInstances`, `Frequency`, `CullReaction`, `KeepExistingDistance`. Dry-run is the default. Use the existing cue's exact source, new destination packages and an isolated unattended Editor commandlet; `-Apply` alone enables saving. Apply requires an invocation with `-SCCProvider=None`, preserves normal Git/LFS settings, saves new NET/NS before linking one profile field, and keeps a verified profile backup/partial receipt. Reports are under `Saved/Authoring/NiagaraScalability/<run id>`. Existing destinations are never overwritten and partial failures are not automatically deleted.

The 99 native checks verified actual serialized getters/consumer selection plus cull/resume, exact lease/attack end, local linkage and observer cleanup using a transient test limit of 2. This does not test 128 simultaneous active systems, GPU/pixel quality or Cook/Shipping inclusion. Preserve the earlier Basic weapon measurements and unfinished Spellblade grip/motion validation as separate evidence.


## Shared notify ownership and authoring lifetime

The shared combat-cue notify keeps one admission claim per mesh and notify instance ID before calling the presentation component. A recursive duplicate Begin cannot acquire a second token. After Niagara callbacks, only that exact claim and current source/token may publish; an End or replacement during acquisition causes the old return to release only its own token. End detaches ownership before calling into Niagara. The original Blueprint Super timing hooks remain in their original order.

Begin inspects only the current mesh's executions. Whole-map dead-mesh housekeeping follows the existing native PostGC lifecycle. While the shared notify remains alive, dead entries are detached before callbacks and their still-live component tokens are retired with pinned owners/worlds. BeginDestroy removes the delegate and weak storage without invoking Niagara during GC teardown. Normal active animation ownership and attack/owner teardown remain required; independent collection of a manually invoked notify while its owner survives is outside this added guarantee.

The existing Editor profile validation rejects untracked one-shot cues (`bLooping=false`) whose EffectType uses `DeactivateResume`, `DeactivateImmediateResume`, or `PauseResume`. One-shots may still have scalar cue tokens, but their AutoRelease Niagara component is not tracked for matching notify/attack retirement. Current-quality culling being disabled does not make a platform-wide resume reaction safe. Explicitly owned loops, non-resuming reactions and the existing no-EffectType contract remain unchanged. Validation reads saved policies; it does not assign a runtime default EffectType or modify vendor assets.

[126 implementation and focused evidence](../../Architecture/Planning/Project_J_MMO_Architecture_Work_2026-10-26.md#126차-공유-vfx-notify의-참가자별-수명과-제작-검증) covers real pooled activation reentry, exact End/replacement, shared-mesh full-GC retirement and authoring validation. These checks do not certify all GPU costs or warning readability.
