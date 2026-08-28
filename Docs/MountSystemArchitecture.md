# Mount System Architecture

## Purpose and current scope

This document describes the reusable mount foundation currently added to Project J.  The first playable implementation is the Wyvern: it can be interacted with, mounted, moved on the ground, flown, glided, automatically landed, and dismounted.  The code is intentionally split so horses and other ground mounts can reuse the base class without inheriting flight-only behavior.

The system is a gameplay foundation, not a completed MMORPG mount feature.  In particular, mount health is networked and the GAS AttributeSet exists, but combat GameplayEffects, stamina consumption, persistence, and UI remain follow-up work.

## Module boundaries

```text
Project_JCore
  ├─ Interaction contract and InteractionTargetComponent
  └─ Gameplay tag: State.Mounted

Project_JMount
  ├─ Project_JMountCharacter / Project_JMountComponent
  ├─ Project_JFlyingMountCharacter
  ├─ RiderAnimationProfile (shared rider presentation policy)
  ├─ Mount ASC and MountAttributeSet
  ├─ Mount camera components
  └─ Common mount AnimInstance classes

Project_JCharacter
  ├─ Player input bridge and server interaction request
  ├─ Inventory mount-item use bridge
  ├─ MountedAnimationLayerComponent (client-side async layer linking)
  └─ Mount item definition
```

`Project_JCharacter` depends on `Project_JMount`; mount runtime code does not depend on player-character implementation details.  New mount types should normally be added in `Project_JMount` and selected with data or Blueprint subclasses.

## Class hierarchy

```text
AProject_JMountCharacter
  ├─ Ground mount Blueprint (horse, wolf, etc.)
  └─ AProject_JFlyingMountCharacter
       └─ BP_Wyvern

UProject_JMountAnimInstance
  └─ UProject_JFlyingMountAnimInstance
       └─ ABP_Wyvern
```

- `AProject_JMountCharacter` owns mounting, dismounting, rider attachment, replication, camera, interaction target, mount ASC, and common attributes.
- `AProject_JFlyingMountCharacter` adds takeoff, vertical flight movement, glide state, and automatic landing detection.
- `UProject_JMountComponent` lives on the player and keeps the currently mounted actor in replicated state.  It also applies the `State.Mounted` tag.

## Networking and authority

Mounting, dismounting, item-triggered summoning, flight start/stop, and damage are server-authoritative.  The mounted rider, mount state, glide state, and basic health values replicate.  The server validates rider state and interaction distance before accepting a mount request.

The player controller possesses the mount while riding.  The player actor is attached to the mesh `RiderSocket`, collision and movement are disabled during the ride, and are restored after a safe dismount location is found.

## Blueprint setup

### BP_Wyvern

1. Create `BP_Wyvern` from **Project J Flying Mount Character**.
2. Assign the Wyvern skeletal mesh and configure a `RiderSocket` on the skeleton.  Adjust socket position and rotation to seat the player.
3. Set mesh collision separately from the root capsule if wing/body hit detection is needed.  The root capsule is for navigation and blocking; skeletal Physics Assets or explicit hit volumes are better for detailed body parts.
4. Tune `FlightSpeed`, `GlideSpeed`, camera boom length, and camera relative position in the class defaults.

### ABP_Wyvern

1. Create `ABP_Wyvern` from **Project J Flying Mount Anim Instance** and assign it to the Wyvern mesh.
2. The parent class provides `Speed`, `VerticalSpeed`, `bIsFalling`, `bIsFlying`, and `bIsGliding`.
3. Recommended state mapping:
   - Idle: `Wyvern_Idle`
   - Walk: `Wyvern_Walk`
   - TakeOff: `Wyvern_TakeOff`
   - FlyStationary: `Wyvern_FlyStationary`
   - Fly: `Wyvern_Fly`
   - Glide: `Wyvern_Glide`
   - Landing: `Wyvern_FlyStationaryToLanding`
4. Keep the state-machine rules data-driven from the parent variables.  Avoid polling player input in the ABP.

## Input setup

Input keys are data assets, not hardcoded in C++.

- `IA_Interact`: map to **F** in `IMC_Default`, then assign it to the player's `Interact Action` property.
- `IA_MountAscend`: map to **Space Bar**, then assign it to the flying mount's `Ascend Action` property.  Pressing it starts flight; holding it moves upward while flying.
- `IA_MountDescend`: map to **Left Control**, then assign it to the flying mount's `Descend Action` property.  Holding it moves downward while flying.

When a descending flying mount reaches ground within the landing trace distance, it automatically returns to walking movement.  The initial version does not yet distinguish water, steep slopes, or restricted landing zones.

## Interaction foundation

All future F interactions share `IProject_JInteractable`; `UProject_JInteractionTargetComponent` stores editor-facing prompt, range, priority, and availability settings.  Mounts implement the interface directly and include the component by default.

The present target scan is the first playable server-side version: it searches nearby Pawn interactables in a fixed radius and asks the selected actor whether interaction is allowed.  NPCs and mount actors already fit this flow.  Pickups, chests, hold-to-interact UI, line-of-sight checks, and selecting by component priority/range should be added by evolving the scanner to consume `InteractionTargetComponent` data.  Do not duplicate F key bindings for individual actor Blueprints.

## GAS and attributes

Every mount owns a replicated `UProject_JAbilitySystemComponent` and `UProject_JMountAttributeSet` with Health, MaxHealth, Stamina, and MaxStamina.  This creates the correct ownership boundary for future mount-specific abilities and effects.

The current mount availability and damage path still uses replicated primitive health as a transitional gameplay path.  Before adding mount combat, move the authoritative health update fully to GameplayEffects/AttributeSet callbacks, then attach death, stagger, stamina drain, and cooldown logic to the ASC.

## Flight phases and automatic takeoff

Flying mounts use a replicated `EProject_JMountFlightState` instead of a single flying boolean:

```text
Grounded -> TakingOff -> AutoAscending -> Flying -> Landing -> Grounded
```

`TakingOff`, `AutoAscending`, and `Landing` lock movement, ascent, descent, and dismount input while preserving camera look. The server verifies upward capsule clearance, then automatically climbs to `AutoTakeOffHeight` at the authored wing-impact time. Assign the TakeOff sequence to `TakeOffCueAnimation` on the flying-mount Blueprint and add the reusable `Project J Mount Flight Cue` notify with `TakeOffImpulse` on its wing downbeat. Clients use the notify for the visual cue; a dedicated server scans that same notify time when the phase begins, so authority does not depend on animation evaluation and there is no Blueprint delay value to tune.

Normal flying uses moderated flying braking for a responsive hover without an abrupt stop. Glide uses a lower braking value to preserve momentum. Landing starts when a descending flight detects nearby ground with a valid surface normal; the configurable maximum landing slope rejects steep terrain. Assign the landing sequence to `LandingCueAnimation` and put the shared `LandingTouchdown` cue on its contact frame. The mount only returns to the grounded state after both server collision contact and that authored cue time have occurred; input stays locked until then.

The mount ASC receives replicated loose tags for the protected phases: `State.Mount.TakingOff`, `State.Mount.AutoAscending`, `State.Mount.Flying`, and `State.Mount.Landing`. A `LandingTouchdown` flight cue can be placed on a landing animation for VFX/SFX. Actual landing completion remains collision and server-state driven, so an early or delayed animation does not put the mount through terrain.

## Rider animation and hand IK

### Scalable MMORPG content boundary

Every concrete mount selects a small `UProject_JRiderAnimationProfile`. Multiple
mounts with the same rider behavior should share the same profile (for example,
horse, elk, and wolf can all use a ground-riding profile). The profile contains:

- a soft class reference to the AnimBP implementing `MountedLocomotion`;
- hierarchical gameplay tags used to select pose families/features inside that layer;
- hand-IK policy;
- the profile-specific transition blend duration.

`UProject_JMountedAnimationLayerComponent` is the only runtime owner of the
player's mounted linked layer. It reacts to `MountComponent.OnMountChanged`,
does no ticking, skips dedicated servers, discards stale async-load completions,
and keeps a one-entry preload cache for the player's active or summoned mount.
`SummonedMount` begins early preloading only for the locally controlled player;
simulated proxies wait for the actual replicated mount event so a crowded area
does not preload every remote player's idle summon. A missing or loading external layer safely leaves the
master AnimBP's `MountedLocomotion` passthrough implementation active.

Mounting and combat are mutually exclusive. `AProject_JMountCharacter` evaluates
a stable `EProject_JMountEligibilityFailure` on both interaction queries and the
authoritative server request. `State.CombatMode`, `State.Combat.Transition`,
attack, dodge, hit-react, and death tags reject mounting. The combat transition
tag closes the draw-montage window before the persistent combat GameplayEffect
has applied its tag. Conversely, the player's combat-toggle guard rejects combat
entry while `State.Mounted` is active. The server always repeats eligibility
checks, so a stale client interaction prompt cannot bypass the policy.

The profile is static content referenced by the replicated mount class, so its
selection does not add per-frame or per-character network state. Dynamic mount
state remains server-authoritative and already replicated by the mount actor.
The player animation proxy copies profile tags and scalar policy into its
game-thread snapshot before worker-thread evaluation; AnimGraph code must not
poll the mount actor or profile object directly.

Recommended content topology:

```text
ABP_Player_Master
  └─ one MountedLocomotion linked-layer slot
       ├─ generic ABP_Player_Mount shared by similar profiles
       └─ specialized rider layer only when graph logic truly differs

DA_RiderProfile_Ground
  ├─ Layer = ABP_Player_Mount
  └─ Tags = Animation.Mount.Pose.Ground

DA_RiderProfile_Flying
  ├─ Layer = ABP_Player_Mount (or a specialized flying rider layer)
  └─ Tags = Animation.Mount.Pose.Flying
```

Do not branch on concrete Blueprint classes in `ABP_Player_Mount`. Add or reuse
a profile/tag instead. This keeps the master graph fixed as the mount catalog
grows and prevents an enum entry per shop item or cosmetic variant.

The player `UProject_JCharacterAnimInstance` now publishes a mount snapshot through its animation proxy.  It includes mounted state, mount speed, vertical speed, flying/gliding state, and optional left/right hand targets in the player mesh's component space.  The snapshot is collected on the game thread before worker-thread AnimGraph evaluation, so it is safe to use with the project's multi-threaded animation update.

While mounted, player Motion Matching trajectory updates and searches are disabled. The master selects `OnFoot` or `Mounted` through `Blend Poses by EProject_JAnimationLocomotionMode`. The mounted branch contains one `MountedLocomotion` linked layer. A linked rider layer returns its authored full-body pose; the master implementation passes through `BasePose` whenever no rider layer is linked.

### Full-body locomotion contexts

`EProject_JAnimationLocomotionMode` is the stable, coarse full-body context
contract: `OnFoot`, `Mounted`, `Swimming`, `Vehicle`, and `Transformed`. It is
not a list of mount species or vehicle models. The player publishes it through
the thread-safe animation snapshot, allowing the master AnimGraph to use a
single top-level context router when those systems are authored.

The recommended long-term topology is:

```text
OnFoot       -> shared Motion Matching / standing aim / foot presentation
Mounted      -> MountedLocomotion Linked Layer -> RiderAnimationProfile -> rider AnimBP
Swimming     -> Swimming full-body layer
Vehicle      -> seat/vehicle full-body layer
Transformed  -> transformation full-body layer
```

Mount species, flying/gliding, rider pose families, and vehicle seat roles stay
inside the selected context's profile and AnimBP state machine. Combat, casting,
hit reactions, emotes, and short actions remain overlays or montages rather
than new locomotion contexts. The native animation snapshot now zeros shared
standing aim, Foot Placement, and Leg IK outside `OnFoot`; specialized layers
must author any equivalent procedural work themselves.

`Swimming`, `Vehicle`, and `Transformed` are reserved contracts, not partially
implemented features. Introduce one only when its gameplay system exists. That
work requires both C++ and editor content: an authoritative context-selection
condition in `GetAnimationLocomotionMode`, any replicated/source state the
presentation needs (for example water state, vehicle seat role, steering, or
transformation data), thread-safe snapshot getters, and a dedicated full-body
AnimBP/Linked Layer connected to the matching context pin. Do not add empty
master-graph branches or placeholder Blueprint casts before then.

For a Wyvern Blueprint, create the following mesh sockets near the reins/neck handle and tune their position before enabling IK:

- `Reins_L`: left hand target
- `Reins_R`: right hand target

These are configurable as `Rider Left Hand Socket Name` and `Rider Right Hand Socket Name` on every mount Blueprint.  A mount without both sockets simply reports no hand targets, so its rider can still use the mounted pose without FABRIK.

### Current player ABP layout

`ABP_Humanoid_Master` currently has a Motion Matching based locomotion path. Its high-level flow is:

```text
Pose Search Database
  -> Motion Matching
  -> Save Cached Pose: Locomotion

Use Cached Pose: Locomotion
  -> UpperBody Slot
  -> Layered Blend Per Bone
  -> Default Slot
  -> Mesh Space Aim Offset
  -> Local To Component
  -> Foot Placement
   -> Leg IK
   -> Component To Local
   -> Pose History
   -> Blend Poses by EProject_JAnimationLocomotionMode : Default Pose

Use Cached Pose: Locomotion
   -> MountedLocomotion Linked Anim Layer
   -> Blend Poses by EProject_JAnimationLocomotionMode : Mounted Pose

Get Thread Safe Locomotion Mode
   -> Blend Poses by EProject_JAnimationLocomotionMode : Active Enum Value

Blend Poses by EProject_JAnimationLocomotionMode
   -> Output Pose
```

The on-foot chain is the `Default Pose`; its cached locomotion pose is the mounted layer's `BasePose` fallback. A specialized rider layer may ignore that input and return its own full-body pose, as `ABP_Rider_Dragon` does. Use the enum only for coarse full-body contexts, never for a concrete mount Blueprint, species, or cosmetic variant.

`MountedLocomotion` for the current imported idle animation should be:

```text
Sequence Player: ANIM_Rider_Idle (Loop)
  -> Local To Component
  -> FABRIK Left Arm
  -> FABRIK Right Arm
  -> Component To Local
```

Configure the two FABRIK nodes as follows:

| Node | Root Bone | Tip Bone | Effector Transform Space | Location source | Alpha |
| --- | --- | --- | --- | --- | --- |
| Left arm | `clavicle_l` | `hand_l` | Component Space | `GetThreadSafeMountedLeftHandTargetComponentSpace` through `Make Transform` | `GetThreadSafeHasMountedHandIKTargets` |
| Right arm | `clavicle_r` | `hand_r` | Component Space | `GetThreadSafeMountedRightHandTargetComponentSpace` through `Make Transform` | `GetThreadSafeHasMountedHandIKTargets` |

If the project skeleton uses different bone names, use the matching clavicle/hand chain.  First tune `RiderSocket`, then the `Reins_L` and `Reins_R` sockets, and only then adjust FABRIK precision or alpha.  IK should correct a small difference, not pull the arm across a large distance.

### Planned rider states

Only `ANIM_Rider_Idle` is needed for the first test.  Do not make a second State Machine yet unless more rider sequences exist.  When additional assets are available, replace the single Sequence Player in `MountedLocomotion` with a small cached pose or state machine driven exclusively by these thread-safe getters:

| Future state | Suggested condition |
| --- | --- |
| MountedIdle | `MountedSpeed <= small threshold` and not flying |
| MountedGroundMove | `MountedSpeed > small threshold` and not flying |
| MountedFly | `MountedIsFlying` and not gliding |
| MountedGlide | `MountedIsGliding` |
| Mount/Dismount transition | Explicit replicated mount state or montage notification |

The current code intentionally exposes mounted speed and flight state now, so adding rider states later does not require player-input polling or new replication rules.

### Linked mounted AnimBP

Assign the rider layer on each shared `RiderAnimationProfile`, then assign that
profile on the concrete mount Blueprint. `Fallback Mounted Animation Layer
Class` remains on the player for the legacy/test mount path; production mount
content should configure the Rider Profile instead.

The external class and `ABP_Humanoid_Master` must implement the same Animation Layer
Interface and expose the layer selected by the master's `Linked Anim Layer`
node. Combat layers are always suppressed while mounted, so mounted and combat
classes never compete for linked interface functions. If no profile layer is
assigned or an async load has not finished, the master passthrough layer
remains the safe fallback.

## Validation checklist

1. Place `BP_Wyvern` in a level and confirm its mesh has `RiderSocket`.
2. Create or reuse a Rider Animation Profile, assign its linked layer/tags, and set it on `BP_Wyvern`.
3. In PIE, approach the mount and press F; verify the player possesses the Wyvern and the camera follows the mount.
4. Verify ground movement and camera rotation first.
5. Press Space to begin flight, hold Space to climb, and hold Left Control to descend.
6. Confirm the mount switches back to ground movement when it reaches the terrain.
7. Press the configured dismount interaction to return safely to the ground.

## Planned extensions

- Persist owned/summoned mounts through inventory and character data.
- Add cooldowns, summon/despawn rules, stable zones, and combat restrictions.
- Replace the temporary health path with GAS GameplayEffects and add stamina use for flight/sprint.
- Add body-part hit volumes, damage routing, and optimized far-distance collision policy.
- Implement interaction focus UI, prompts, hold progress, priority, and non-Pawn target scanning.
- Add prediction/smoothing policy and profiling tiers before supporting large numbers of visible mounts.
