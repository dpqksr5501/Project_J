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
  ├─ Mount ASC and MountAttributeSet
  ├─ Mount camera components
  └─ Common mount AnimInstance classes

Project_JCharacter
  ├─ Player input bridge and server interaction request
  ├─ Inventory mount-item use bridge
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

## Rider animation and hand IK

The player `UProject_JCharacterAnimInstance` now publishes a mount snapshot through its animation proxy.  It includes mounted state, mount speed, vertical speed, flying/gliding state, and optional left/right hand targets in the player mesh's component space.  The snapshot is collected on the game thread before worker-thread AnimGraph evaluation, so it is safe to use with the project's multi-threaded animation update.

While mounted, player Motion Matching trajectory updates and searches are disabled.  The player ABP should select a separate mounted locomotion layer, bypassing foot placement, leg IK, aim offsets, and pose history for that layer.  This avoids evaluating ground-only systems for an attached rider and also prevents attached-player velocity from driving an incorrect locomotion pose.

For a Wyvern Blueprint, create the following mesh sockets near the reins/neck handle and tune their position before enabling IK:

- `Reins_L`: left hand target
- `Reins_R`: right hand target

These are configurable as `Rider Left Hand Socket Name` and `Rider Right Hand Socket Name` on every mount Blueprint.  A mount without both sockets simply reports no hand targets, so its rider can still use the mounted pose without FABRIK.

### Current player ABP layout

`ABP_Player` currently has a Motion Matching based locomotion path.  Its high-level flow is:

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
  -> Output Pose
```

The lower path is intentionally left unchanged for an on-foot player.  It should not be reused directly for a rider: attached-player trajectory, foot planting, leg IK, and standing aim offsets all describe the wrong motion while seated on a mount.

### Required locomotion-layer selection in ABP_Player

Create separate `OnFootLocomotion` and `MountedLocomotion` Anim Layers.  `ABP_Player` remains the composition graph: select the full-body layer near the root with `Blend Poses by Enum`, using `GetThreadSafeLocomotionMode`.

```text
OnFootLocomotion Layer ---------------------- OnFoot
MountedLocomotion Layer --------------------- Mounted
Swimming / Vehicle / Transformed Layers ----- future entries
                                                  Blend Poses by Enum -> action slots -> Output Pose
Active Enum: GetThreadSafeLocomotionMode
```

The current Motion Matching / Aim Offset / Foot Placement / Leg IK / Pose History graph is the body of `OnFootLocomotion`.  It is not evaluated in `MountedLocomotion`.

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

`GetAnimationLocomotionMode` is a Blueprint-native extension point on the player.  Its default result is replicated mount state (`OnFoot` or `Mounted`).  Future swimming, vehicle, or transformation systems may override it in their player Blueprint or native subclass.  The current code intentionally exposes mounted speed and flight state now, so adding rider states later does not require player-input polling or new replication rules.

### Optional linked mounted AnimBP

The initial implementation can use `MountedLocomotion` as a self layer inside `ABP_Player`.  When rider logic grows, create a separate `ABP_Player_Mounted` and assign it to `Mounted Animation Layer Class` in the player Blueprint defaults.  The player character listens to the replicated `MountComponent.OnMountChanged` event and calls `LinkAnimClassLayers` only while mounted; it unlinks the class on dismount and end play.  This is event-driven rather than tick-driven.

For the external class to override the master layer, both `ABP_Player` and `ABP_Player_Mounted` must implement the same Animation Layer Interface and expose the layer selected by the master's `Linked Anim Layer` node.  If no class is assigned, or the layer cannot be linked yet, the self-layer implementation remains the safe fallback during setup.

## Validation checklist

1. Place `BP_Wyvern` in a level and confirm its mesh has `RiderSocket`.
2. In PIE, approach the mount and press F; verify the player possesses the Wyvern and the camera follows the mount.
3. Verify ground movement and camera rotation first.
4. Press Space to begin flight, hold Space to climb, and hold Left Control to descend.
5. Confirm the mount switches back to ground movement when it reaches the terrain.
6. Press the configured dismount interaction to return safely to the ground.

## Planned extensions

- Persist owned/summoned mounts through inventory and character data.
- Add cooldowns, summon/despawn rules, stable zones, and combat restrictions.
- Replace the temporary health path with GAS GameplayEffects and add stamina use for flight/sprint.
- Add body-part hit volumes, damage routing, and optimized far-distance collision policy.
- Implement interaction focus UI, prompts, hold progress, priority, and non-Pawn target scanning.
- Add prediction/smoothing policy and profiling tiers before supporting large numbers of visible mounts.
