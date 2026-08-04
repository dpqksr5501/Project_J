# Combat Animation Composition

## Decision

Continuous weapon presentation is selected per `WeaponAnimProfile` through
`CombatPresentationMode`. This is presentation data only: movement, GAS state,
hit validation, and equipment ownership do not depend on it.

| Mode | Intended use | Lower body | Leg IK |
| --- | --- | --- | --- |
| `Upper-Body Overlay` | Default. A weapon has a reliable armed upper-body pose but no phase-compatible in-place combat locomotion set. | Shared Motion Matching | Remains enabled |
| `Full-Body Locomotion` | Opt-in only after validating a complete in-place idle/start/loop/stop/airborne weapon set. | Weapon layer | Suppressed by the full-body policy |

The default is `Upper-Body Overlay`. This is deliberate: an incomplete weapon
locomotion set must not replace stable shared locomotion merely because a weapon
is equipped.

## Animation Layer Contract

`ALI_HumanoidCombat` has two independent continuous-pose contracts. They must
not be merged into a weapon-specific `ABP` graph.

```text
CombatLocomotion() -> Pose
  Full-body replacement path.
  Master fallback: shared Motion Matching pose.
  Weapon implementation: only for Full-Body Locomotion profiles.

CombatUpperBody(BasePose: Pose) -> Pose
  Overlay path.
  Master fallback: BasePose unchanged.
  Weapon implementation: BasePose + armed upper-body pose through Layered Blend Per Bone.
```

The common master chooses the path with the thread-safe function
`GetThreadSafeUsesFullBodyCombatLocomotion`. This makes a newly added weapon
family data-driven: it supplies a linked layer class, then chooses composition
mode in its `WeaponAnimProfile`; it never forks `ABP_Humanoid_Master` or player
character code.

## Master AnimGraph Shape

```text
Shared Motion Matching
  -> Save Cached Pose: SharedLocomotion
  -> Branch (UsesFullBodyCombatLocomotion)
       true:  CombatLocomotion Linked Layer
       false: CombatUpperBody Linked Layer (BasePose = SharedLocomotion)
  -> Upper-body montage slot, where applicable
  -> DefaultSlot (full-body actions)
  -> Aim Offset
  -> Foot Placement
  -> Leg IK
  -> mount selection / Output Pose
```

Keep `DefaultSlot` as the full-body action path. Draw/sheath, attacks, dodge,
hit reactions, death, and committed root-motion actions use it. `CombatUpperBody`
is only for a persistent armed stance or a movement-compatible short overlay.

## Locomotion source contract (OTM and Combat Strafe)

The input called `Shared Motion Matching` above is a resolved locomotion pose,
not a promise that every lower-body clip comes from Motion Matching. Project_J
uses two lower-body modes:

```text
Non-combat / OTM
  Existing Motion Matching + OTM State Controller direct one-shots.

Combat / Strafe
  Combat Motion Matching for continuous Cycle and Turn Redirect,
  State Controller Blend Stack for Start, Stop, Pivot, Jump, Fall Off and Land.
```

The master graph selects the State Controller direct pose only while
`GetThreadSafeStateControllerShouldOverrideMotionMatching` is true. Otherwise
the active Motion Matching pose is used. Both paths then flow through the same
combat upper-body, slots, aim offset, foot placement, Leg IK and Pose History
chain. This is why combat strafe does not need a second master AnimGraph or a
separate full-body combat layer.

Combat Strafe's State Controller Blend Stack may contain local, per-one-shot
Orientation Warping. It must remain inside that direct path, between `Local To
Component` and `Component To Local`; do not apply it globally to the final
locomotion pose. See
[`CombatStrafe_Implementation_2026-08-04.md`](CombatStrafe_Implementation_2026-08-04.md)
for the pin and bone contract.

## Greatsword Now

Set `DA_Greatsword_WeaponProfile.CombatPresentationMode` to
`Upper-Body Overlay`.

In `ABP_GreatSword_Layers -> CombatUpperBody`:

```text
Input Pose (BasePose)
  + Sword_Idle_Seq / armed upper-body Blend Space
  -> Layered Blend Per Bone
       Base Pose: BasePose
       Blend Pose: armed pose
       Branch Filter: spine_01 (or the confirmed humanoid upper-body root)
  -> Output Pose
```

The initial alpha may be `1.0`, because this linked layer only exists during
combat presentation. Keep the lower body out of the blend filter. If an asset
still modifies hips or legs, use a per-bone blend mask or a cleaned upper-body
asset; do not disable shared lower-body IK to hide the issue.

Do not delete the greatsword combat Data Assets. Combo graphs, attack
definitions, weapon presentation, equipment effects, the ability set, draw
montage, and the linked layer are still all used. The only deferred assets are
the unvalidated full-body combat Blend Space and its cropped Start/Loop/Stop
sequences.

## Future Full-body Upgrade

A future weapon can switch its profile to `Full-Body Locomotion` only when all
of the following are true:

1. Its locomotion clips are in-place and share a common root/pelvis origin.
2. Start, loop, and stop are phase compatible; matching left/right foot sync
   markers are authored.
3. Draw blend-out, attack recovery, air, slope, and network proxy behavior are
   verified with the common full-body montage and IK policy.
4. Its `ABP_<Weapon>_Layers` implements `CombatLocomotion`; the master graph
   and runtime linker remain unchanged.

This allows incremental quality upgrades without a character-class fork or a
data migration.
