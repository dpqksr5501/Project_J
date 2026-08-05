# Combat Strafe implementation status - 2026-08-04

> **Design update (2026-08-05, superseding prior TIP wording):** Combat Strafe
> gains a separate direct Idle TIP family. Camera yaw leads while idle; direct
> `UseMM=false` 90/180 left/right one-shots resolve the body yaw once the
> threshold is crossed. Start/Pivot re-selection during movement remains a
> separate future refinement. Regular Motion Matching continues to own only
> Cycle and Turn Redirect.

## Purpose and scope

## Combat Strafe Idle TIP integration (2026-08-05)

Native support is opt-in through
`UProject_JLocomotionAnimStateComponent::bEnableCombatStrafeTurnInPlace` (default
`false`). Once enabled, only a locally controlled, grounded Combat Strafe player
with no move input, low speed, and no attack/dodge/hit-react can request TIP.
The initial entry/exit yaw thresholds are 65/30 degrees. Attack, dodge, hit
react, movement, air, and landing immediately release the local controller-yaw
override and preempt TIP.

The editor integration is intentionally a narrow direct path:

1. Add `TransitionToTurnInPlace` rows under the Combat Strafe State Controller
   chooser path.
2. Use `StateControllerTurnInPlaceVariantForChooser`: `1=Left90`,
   `2=Left180`, `3=Right90`, `4=Right180`.
3. Map those four rows to `M_Neutral_Stand_Turn_090_L/R` and
   `M_Neutral_Stand_Turn_180_L/R` (or the project-approved equivalents).
4. Set each chooser output to `UseMM=false`, non-looping, and tag it
   `TurnInPlace`. Do not add it to a regular locomotion PSD.
5. Verify in PIE that the direct Blend Stack consumes the asset's authored root
   yaw. Do not enable global `Root Motion from Everything`, global Steering, or
   Offset Root Bone to make TIP work.

The native chooser cache only re-evaluates on a 90/180 or left/right bucket
change, not on every camera-yaw update. `p.ProjectJ.MMTransitionDebug 1` logs
the presentation state, input-facing yaw, and `TIPVariant` for verification.

This is the current source of truth for the locomotion work completed after
`CombatStrafe_Handoff_2026-08-03.md`.

Project_J intentionally has two lower-body locomotion modes:

| Mode | Rotation contract | Primary animation strategy |
| --- | --- | --- |
| Non-combat / OTM | The character turns toward its travel direction. | Existing OTM Motion Matching and OTM State Controller one-shots. |
| Combat / Strafe | The character may travel independently of its facing/camera aim. | Combat Strafe Motion Matching for continuous motion, plus directional State Controller one-shots. |

Combat Strafe is not a variation of OTM selected by an `Any` row. It has its
own Chooser branch, directional snapshot, and one-shot tables. OTM assets and
logic must remain valid when combat is disabled.

## Runtime ownership

```text
Character + UProject_JLocomotionAnimStateComponent
  -> gameplay movement, landing lifetime, sprint intent and replicated events
  -> UProject_JCharacterAnimInstance thread-safe snapshot
  -> State Controller / Chooser selects an AnimationAsset for direct one-shots
  -> State Controller Blend Stack or Motion Matching produces locomotion pose
  -> master AnimGraph adds combat upper body, montage slots, aim, feet and IK
```

### C++ owns decisions

The runtime publishes the data consumed by Choosers; Choosers only map that
data to assets. In particular, C++ owns:

- presentation phase/state, gait and rotation mode;
- Strafe direction and the previous direction snapshot;
- left/right contact-foot selection for one-shots;
- fall-off versus intentional jump;
- landing movement/sprint intent and landing-to-stop handoff;
- whether a direct State Controller animation overrides Motion Matching.

The published Strafe directions are:

```text
Forward, ForwardLeft, Left, BackwardLeft,
Backward, BackwardRight, Right, ForwardRight
```

They are derived from future trajectory velocity when available, otherwise
current velocity, relative to actor facing. Sector hysteresis prevents a row
from flickering at a boundary. At near-zero speed the last valid direction is
kept, which makes a stop choice deterministic.

### Editor assets own lookup, not policy

- A root State Controller Chooser routes OTM and Strafe separately by rotation
  mode and presentation state.
- Parent Choosers select the appropriate leaf table.
- Leaf Choosers select a sequence and `Project_JStateControllerChooserOutput`
  metadata such as start time, blend, loop, and Motion Matching override.
- Use an explicit condition for every branch. Do not rely on row order or use
  an OTM `Any` row as a fallback for Combat Strafe.

`None` is a literal value for a Chooser enum, not a wildcard. For an asset with
no per-foot variant, set the one-shot-foot column to `Any`. Use exact `Left` or
`Right` only when the selected source asset has that foot-specific version.

## Motion Matching and direct one-shots

The two systems have separate responsibilities.

| Family | Source | Owner |
| --- | --- | --- |
| Continuous cycle | Combat Motion Matching Asset Set / PSD | Motion Matching |
| Continuous turn redirect | Combat Motion Matching Asset Set / PSD | Motion Matching |
| Start, Stop, Pivot | Strafe State Controller leaf Choosers | Direct sequence |
| Jump Start, Fall Off, In-Air loop | Strafe In-Air parent and child Choosers | Direct sequence |
| Land | Strafe Land parent and child Choosers | Direct sequence |

Do **not** fill Combat Motion Matching Asset Set `Start`, `Stop`, `Pivot`,
`Jump`, `Fall Off`, or `Land` fields for these direct State Controller paths.
The active Combat data asset continues to supply only the continuous Cycle and
Turn Redirect PSDs. This prevents a direct one-shot and Motion Matching from
attempting to own the same transition.

## Chooser hierarchy currently used

```text
CHT_Player_StateControllerAnimations
  OTM Ground       -> CHT_Player_OTM_Ground
  Strafe Ground    -> CHT_Player_Strafe_Ground
  OTM In Air       -> CHT_Player_InAir
  Strafe In Air    -> CHT_Player_Strafe_InAir
  OTM Land         -> CHT_Player_Land
  Strafe Land      -> CHT_Player_Strafe_Land

CHT_Player_Strafe_Ground
  Transition to Locomotion -> Run Start / Sprint Start / Run Pivot leaf
  Transition to Idle       -> Run Stop / Sprint Stop leaf
  Locomotion Loop          -> Motion Matching, no direct Cycle row

CHT_Player_Strafe_InAir
  Transition to In Air + FallOff=true  -> CHT_Player_Strafe_FallOff
  Transition to In Air + FallOff=false -> CHT_Player_Strafe_Jump
  In Air Loop                         -> CHT_Player_Strafe_InAirLoop

CHT_Player_Strafe_Land
  WasMoving=false                 -> CHT_Player_Strafe_Land_Stand
  WasMoving=true, WasSprinting=false -> CHT_Player_Strafe_Land_Run
  WasMoving=true, WasSprinting=true  -> CHT_Player_Strafe_Land_Sprint
```

The exact rows are asset dependent. A direction with no authored diagonal
sequence may map to the nearest suitable cardinal sequence. It must still keep
the incoming Strafe-direction condition so the choice stays deterministic.

## Foot and direction rules

`StateController One Shot Foot` answers "which foot should lead this clip?".
It does not mean left/right travel direction. `Strafe Direction` answers the
travel direction relative to the actor.

Typical usage:

| Asset category | Direction column | Foot column |
| --- | --- | --- |
| F / FL / FR source with left/right variants | Exact direction | Exact Left or Right |
| B / BL / BR / L / R source without variants | Exact direction or documented cardinal fallback | `Any` |
| Loop selected by Motion Matching | Not a direct leaf row | Not used by direct one-shot selection |

This rule is important for Backward land/fall-off assets and any LL/RL source
that does not actually have `_Lfoot` / `_Rfoot` variants.

## ABP and Blend Stack contract

There are two different Blend Stacks.

1. **State Controller Blend Stack**: plays the single sequence selected by the
   State Controller. It is the direct-one-shot path.
2. **Motion Matching node internal Blend Stack**: processes each animation
   selected by Motion Matching before its blended pose is returned. It is the
   continuous PSD path.

The master AnimGraph chooses between the direct State Controller output and
Motion Matching with `GetThreadSafeStateControllerShouldOverrideMotionMatching`.
Direct transitions and the direct `InAirLoop` override Motion Matching; ground
Idle/Locomotion loops do not. It then continues through the existing composition path: cached locomotion,
combat upper-body linked layer/slots, aim offset, foot placement, leg IK, pose
history, and final locomotion-mode selection. This preserves the existing
non-combat and upper-body behavior.

### Combat Strafe Orientation Warping

Orientation Warping was added **inside the State Controller Blend Stack**, not
as a master-graph-wide warper:

```text
Blend Stack Input
  -> Local To Component
  -> Orientation Warping
  -> Component To Local
  -> Output Pose
```

The warper uses Graph mode and the following contract:

- `Locomotion Angle` <-
  `GetThreadSafeStateControllerCombatStrafeOrientationWarpingAngle`.
- `Alpha` <- `enable_warping` sequence curve multiplied by
  `GetThreadSafeStateControllerCombatStrafeOrientationWarpingAlpha`.
- `Current Anim Asset` and `Current Anim Asset Time` come from the State
  Machine Blend Stack Input reference.
- Target Time is `0`. `Locomotion Direction` remains unconnected/zero because
  the explicit local angle is used.
- Spine chain: `spine_01` through `spine_05`, `neck_01`, `neck_02`, `head`.
  IK root/feet: `ik_foot_root`, `ik_foot_l`, `ik_foot_r`.

The C++ alpha is nonzero only for a selected, direct, non-loop Combat Strafe
one-shot with a valid trajectory direction. Missing `enable_warping` means the
curve evaluates to zero, safely disabling warping for that sequence.

This provides directional adjustment for Start/Jump/Fall-Off/Land where a full
diagonal asset does not exist. It is not an implementation of Turn In Place.

### Intentionally not enabled globally

The project is **not** currently enabling GASP's global Steering or Offset Root
Bone nodes as part of this work. In Combat Strafe, those can conflict with
camera-facing/actor-facing authority. Turn In Place remains deferred: idle
camera rotation can stay upper-body/Aim-Offset-only until a dedicated TIP
one-shot design is added.

## Landing exit policy

Landing must not be immediately replaced by idle simply because the player
releases input on the touchdown frame. The locomotion component now tracks a
small post-touchdown input latch:

```text
LandingExitStopInputHoldTime = 0.08 seconds (default)
```

| Player behavior after touchdown | Result |
| --- | --- |
| Releases immediately / does not establish movement | Let the selected Land finish, then Idle. |
| Holds movement for at least the latch time, then releases | Cancel the Land into the matching directional Stop. Sprint is preserved for Sprint Stop. |
| Holds movement | Finish Land into locomotion. |
| Changes to another movement direction | Existing interrupt/reselection logic may take over; test this per asset. |

The same intent is applied to replicated movement-stop handling. The policy is
shared locomotion behavior, so it improves both OTM and Combat Strafe without
changing their distinct direction/asset Choosers.

## Current exclusions

- No Turn In Place implementation yet.
- No global Offset Root Bone or GASP Steering adoption yet.
- No requirement that every diagonal use a unique source sequence. Orientation
  Warping is a presentation aid, not new root motion or collision movement.

## Required editor verification

No additional State Controller states or C++ Blueprint functions are required
for the current implementation. Verify these authored assets after pulling the
code:

1. Root Chooser routes Strafe In-Air and Strafe Land rows by `RotationMode =
   Strafe`.
2. Strafe Ground contains only direct Start/Stop/Pivot rows; Cycle and Turn
   remain Motion Matching PSD work.
3. Strafe Jump, Fall Off, and each Land child use the Strafe Direction and
   one-shot-foot conditions appropriate to the actual asset variants.
4. Use `Any` instead of `None` where a source clip has no foot variants.
5. The State Controller Blend Stack Orientation Warping node has the asset,
   time, curve, alpha, and bone configuration described above.
6. Test OTM and Strafe independently: all eight direction sectors, Run/Sprint,
   jump, fall-off, each land family, release-on-touchdown, release-after-moving,
   combat entry/exit, and a simulated proxy.

`InAirLoop` is always a direct sequence in this project. Its chooser output
must set `bUseMotionMatch=false`; C++ also defensively forces it off so an old
row cannot issue an invalid one-asset Pose Search query.

## Relevant source files

```text
Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h
Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp
Source/Project_JCharacter/Public/Project_JLocomotionAnimStateComponent.h
Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent.cpp
Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentInput.cpp
Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentTransitions.cpp
```

The code was built successfully with the `Project_JEditor` Win64 Development
target after the landing-exit change.
