# Project_J Locomotion / Motion Matching Refactor

> Continuation summary and the State Controller / TIP roadmap are maintained in
> `docs/MotionMatching_StateController_Handoff_2026-08-01.md`.
> That handoff also contains the current GASP feature coverage matrix
> (`implemented` / `partial` / `deferred` / `excluded`).

## Why this refactor exists

Project_J keeps regular Motion Matching for responsive locomotion cycles, but
authored Start/Stop/Pivot/turn transitions require an explicit presentation
owner. The refactor moves gameplay facts, trajectory, network policy and asset
eligibility to C++ snapshots; ABP only assembles a pose from those facts. This
prevents continuous MM from becoming the owner of one-shot completion, keeps
worker-thread reads safe, preserves noncombat OTM, and creates the required
foundation for a later combat-Strafe-only TIP state machine.

Last updated: 2026-08-23
Scope: on-foot noncombat Orient-to-Movement (OTM) and combat Strafe locomotion.
Out of scope: TIP, Traversal, Root Motion from Everything.

## Design boundary

The reviewed GASP graphs are primarily **Strafe and root-motion-steering reference material**. Project_J must not copy their conditions into both modes.

| Mode | Facing rule | Motion Matching role | Must remain excluded |
|---|---|---|---|
| Noncombat | OTM: capsule rotates to WASD travel direction | Neutral Start/Cycle/Stop/Air/Land PSDs | Strafe yaw offsets, Steering, combat Pivot |
| Combat | Strafe: capsule faces control/camera direction while travel can be lateral/backward | Combat Start/Cycle/Stop/Air/Land PSDs; future Strafe Pivot | OTM rotation ownership |

Mesh relative yaw `-90°` is an asset-axis correction only. It does not create a second gameplay rotation owner. `Root Motion from Everything` remains disabled, so GASP's root-motion Steering node is deliberately not added.

## Ownership

| Layer | Owns | Does not own |
|---|---|---|
| `UProject_JLocomotionAnimStateComponent` | input, kinematics, OTM/Strafe, ground/air/landing, MM selection context | AnimGraph playback completion |
| `UProject_JCharacterAnimInstance` | game-thread snapshot and debug publication | worker-thread gameplay logic |
| `FProject_JCharacterAnimInstanceProxy` | thread-safe policy, PSD application, MM diagnostics | controller/component/world access |
| ABP / Linked Layer | pose assembly: MM, slot, AO, Foot Placement, IK, Pose History | gameplay state and direct actor lookup |
| Locomotion Profile / Asset Set | PSDs, thresholds, MM policy | hardcoded asset names |

```text
Character / CharacterMovement / Combat state (game thread)
  -> LocomotionAnimStateComponent
  -> AnimInstance snapshot
  -> AnimInstanceProxy policy + MM database
  -> ABP Motion Matching and pose composition
```

The active Pose History is the ABP node. The proxy declares `NativePoseHistoryNode` and `NativeMotionMatchingNode` as a fallback/diagnostic graph. With an AnimBlueprint class, UE evaluates the generated ABP graph, so these native nodes do not simultaneously output the final PIE pose.

## Current Project_J implementation

| Capability | Current implementation |
|---|---|
| Combat/noncombat and rotation mode | C++ state with thread-safe snapshot |
| Idle/Start/Cycle/Stop/JumpStart/Fall/Landing | C++ locomotion component owns phase |
| Velocity, acceleration, trajectory, braking and turn angle | game-thread kinematic snapshot |
| PSD selection | C++ Asset Set/Profile; no ABP asset-name branching |
| Light/heavy landing facts | C++ stores fall speed, movement/sprint intent and landing state |
| Worker-thread safety | ABP consumes getters only; no Character/Controller/Component cast |
| MMO cost policy | player/opt-in ownership, dedicated-server skip, visibility budget, proxy-aware throttling and diagnostic traces |
| Visual expression | ABP owns MM node, upper-body layer/slot, AO, Foot Placement, IK and Pose History |

### Future-trajectory velocity contract

Project_J **does generate and feed a multi-sample future trajectory into Pose Search**. That is the trajectory connected to the ABP Pose History node, and it is used by the Motion Matching search itself.

The gameplay/locomotion state machine now reconstructs GASP-equivalent future planar velocity on the game thread. It takes the trajectory sample nearest time zero and the positive sample nearest the configured short prediction horizon, then divides their world-space position delta by sample-time delta.

```text
has input:  predicted speed = ground speed + acceleration * prediction horizon
no input:   predicted speed = max(0, ground speed - braking deceleration * prediction horizon)
```

The reconstructed `FutureTrajectoryVelocity`, `FutureTrajectorySpeed`, and `FutureTrajectoryTurnAngle` are used for Start speed-gain prediction and combat-Strafe Pivot qualification. If trajectory samples are unavailable, the prior acceleration/braking formula remains a safe fallback. `MoveInputTurnAngle` remains an independent input-direction signal; that is intentional because OTM and Strafe do not share the same rotation semantics.

### Trajectory update ordering and sampling cost (2026-08-23)

`UProject_JMotionMatchingTrajectoryComponent` removes the engine example
component's unconditional `OnCharacterMovementUpdated` binding. PlayerCharacter
owns the single normal update entry after movement policy and rotation mode have
been applied and before `UProject_JLocomotionAnimStateComponent` derives the
current frame's semantic context. AnimInstance no longer generates or
post-processes trajectory; it only publishes an immutable copy to its proxy.

Generation eligibility is presentation policy, not gameplay authority:

- locally controlled players generate every eligible movement frame;
- non-local players generate only while recently rendered;
- hidden actors stop generating and re-seed history when they become visible;
- mounted players stop generating the on-foot trajectory and re-seed on dismount;
- dedicated servers never generate animation-only trajectory;
- ordinary NPC/field-monster classes do not own this player component. A special
  NPC or boss must opt into both the component and an explicit update call.

Past samples are immutable observations. A gait or `MaxWalkSpeed` change affects
the newly simulated future prediction only; it no longer scales already-recorded
history. Rotation-mode changes, acceleration-stop policy, visibility wake and
manual resets are recorded with a reset reason and revision.

The present and configured short-horizon sample indices are cached inside
`UProject_JMotionMatchingTrajectoryComponent`. Ordinary state updates therefore
reconstruct future planar velocity without rescanning the stable trajectory time
layout. A history reset invalidates this cache, and changing the configured
prediction horizon rebuilds it automatically.

The game-thread animation snapshot also publishes generation revision, reset
revision, snapshot age and generation eligibility. These are diagnostics and
thread-safety metadata only; trajectory arrays and raw client input are never
replicated.

## GASP mapping

### Motion Matching

| GASP function | GASP role | Project_J handling |
|---|---|---|
| `Update_MotionMatching` | Chooser returns searchable PSDs | AssetSet/Profile C++ selection is the equivalent responsibility. |
| `Update_MotionMatching_PostSelection` | cache selected anim/database/tags; override blend | Actual selected PSD, anim, time, rate, cost and continuing status are cached. Tag-specific blend profiles are deferred data work. |
| `Get_MMInterruptMode` | default no interrupt; interrupt core changes | Implemented in proxy. Normal PSD changes use `InterruptOnDatabaseChange`; forced reselect keeps force-interrupt. |
| `Get_MMBlendTime` | Ground .20, Land .50, upward Jump .15, Air .50 | Profile fields `DefaultBlendTime`, `LandingBlendTime`, `JumpBlendTime`, `AirBlendTime`. |
| `Get_MMNotifyRecencyTimeOut` | notify suppression | Profile policy exists; gait-specific values wait for footstep validation. |
| `Get_PoseHistoryReference` | feed Pose History into MM query | Existing ABP Pose History remains normal owner. |
| `Get_DynamicPlayRate` | MoveData Speed, Enable Warping, min/max curves | Future visual quality only; never Start/Stop completion authority. |

Start, Stop, Landing, JumpStart, and FallOff are transient families in the current policy. Their initial result is retained by default (`bSearch*EveryUpdate = false`), so normal Motion Matching does not keep stacking replacement poses inside the same transient PSD. Cycle remains searchable every update.

This reduces intra-PSD re-search, but it does **not** guarantee a complete authored Start/Stop animation. A regular Motion Matching node is still allowed to select a pose in the middle of a selected sequence and the gameplay phase can advance as braking/input changes. The trace has confirmed both effects. Full one-shot ownership requires the later GASP-style logical State Machine + Blend Stack path; it must not be approximated with playback percentage or a hardcoded duration.

### Movement / landing

| GASP function | GASP role | Project_J handling |
|---|---|---|
| `IsMoving` | current velocity + future trajectory velocity + acceleration | Current velocity, acceleration, and future trajectory speed are read on the game thread. `bIsMotionMatchingMoving` remains a separate C++ snapshot used solely by MM interrupt policy. |
| `IsStarting` | future speed exceeds current speed; false during Pivot DB | Future trajectory speed now drives speed-gain when samples are valid; acceleration/braking is fallback. Pivot tag protection remains a future combat Pivot asset-data task. |
| `PlayLand` | OnGround and previous frame InAir | Explicit replicated C++ landing state is stronger; retain ABP-free ownership. |
| `PlayMovingLand` | landing and abs trajectory turn <= 120 degrees | Existing redirect/cancel logic; profile this eligibility threshold. |
| `JustLanded_Light/Heavy` | just-landed plus stored fall velocity threshold | Existing C++ `LastFallSpeed`, `LandStartFallSpeed`, heavy/light state. |
| `Get_LandVelocity` | returns stored landing Z | Existing snapshot source; expose only through a thread-safe getter when needed. |

### Strafe-only GASP functions

| Function | Why it is Strafe-specific | Project_J decision |
|---|---|---|
| `Update_TargetRotation` | offsets target yaw for directional Strafe assets | OTM target equals actual capsule travel/facing. Add combat-only target snapshot only with explicit Steering/warping design. |
| `Get_StrafeYawRotationOffset` | uses authored direction curves/dummy sequence | No noncombat use. Requires authored combat curve contract first. |
| `EnableSteering` | steers active root-motion Blend Stack while moving/in-air | Do not add while Root Motion from Everything is disabled. |
| `Get_DesiredFacing` | trajectory facing at +0.5 seconds for Steering | Trajectory exists; use only with combat-only Steering. |
| `Get_MovementDirectionThresholds` | cardinal/diagonal hysteresis | Current combat uses continuous direction. Add only if a combat chooser requires discrete buckets. |

## MM interrupt contract

Gameplay movement and MM presentation movement are different. Gameplay can call a decelerating character moving; visually, Cycle -> Stop is a core transition.

```text
first update / core state change -> InterruptOnDatabaseChange
no core state change             -> DoNotInterrupt
explicit forced reselect         -> ForceInterruptAndInvalidateContinuingPose
```

Core changes: ground/air, MM presentation movement, combat/noncombat, OTM/Strafe, and gait change while visually non-moving. `GroundMotionMode::Stop` is intentionally non-moving for MM even when gameplay `bIsMoving` remains true.

The trace proved that Stop can be requested and selected, but still be visually too brief:

```text
RequestedPSD=PSD_Run_Stop
NativePSD=PSD_Run_Stop
AnimTime=0.700/4.067
... shortly afterwards ...
RequestedPSD=PSD_Idle
```

The interrupt correction allows Stop entry; it does not by itself hold a one-shot to authored completion. It deliberately does not add playback-percent or authored-duration state gates.

## Curves

| Curve | Intended use |
|---|---|
| `contact_l`, `contact_r` | Foot Placement / foot lock |
| `movedata_speed` | optional dynamic play-rate matching |
| `enable_warping` | optional warping/play-rate alpha |
| `phase` | pose/gait synchronization |
| `steeringtargettime` | authored steering time where applicable |

These are presentation data, not a universal Start/Stop completion contract. C++ phase entrance/exit uses input, kinematics, events and interrupt policy.

## Networking and threading

- Authority/autonomous proxy owns input intent and gameplay transitions.
- Simulated proxies use replicated movement/state and remote visual policy; never local controller yaw or input inference.
- C++ reads Character, Controller, CharacterMovement, combat and tags on the game thread, then copies a snapshot.
- ABP worker threads read snapshot/getters only: no direct Character cast, component lookup, world/timer access or gameplay-tag query.

## Experimental one-shot Blend Stack contract (wired, staged rollout)

The C++ side exposes an opt-in `OneShotPresentation` snapshot. The first
GASP-style State Controller and direct Blend Stack branch are now wired in
`ABP_Humanoid_Master`; the regular MM branch remains the fallback. The rollout
is intentionally partial: OTM Idle / Run Start / Run Loop / Run Stop rows are
authored first, while Strafe, air, land, Pivot and TIP rows remain regular-MM
until their direct-asset contracts are authored.

| C++ / ABP API | Meaning |
|---|---|
| `bEnableExperimentalOneShotPresentation` (Locomotion Profile) | Master opt-in. Defaults to `false`, preserving the current ABP exactly. |
| `GetThreadSafeExperimentalOneShotEnabled` | Read the master opt-in in a Blueprint Thread Safe state/entry function. |
| `GetThreadSafeOneShotRequested` | True only for Start, Stop, Landing, JumpStart, Fall, and combat-Strafe Pivot. Cycle/Idle stay on regular MM. |
| `GetThreadSafeOneShotPhase` | Semantic request category passed to the State Controller / Chooser. |
| `GetThreadSafeOneShotRequestRevision` | C++ locomotion/MM semantic-context revision; use it to detect a new entry request without reading Character state. |
| `GetThreadSafeOneShotUseMotionMatchOnEntry` | Allows a single Motion Match over the Chooser's one-shot candidates. It does not enable continuous re-search. |

The request deliberately contains no `UAnimSequence`, live component, world, controller or gameplay-tag reference. Chooser/DA owns asset choice, loop, start time, blend profile and asset-specific transition lead time. The State Controller owns its visual state until its non-looping Blend Stack asset is almost complete. This keeps the physical C++ phase responsive while preventing a Stop from disappearing merely because braking reached zero.

`Pivot` is emitted through this request only when rotation mode is combat `Strafe`; OTM never gets a combat Pivot request.

### Verified GASP State Controller contract

The reviewed GASP Experimental State Controller is a logical state machine: its State graphs output no pose. Each transition State calls `SetBlendStackAnimFromChooser` with `Force Blend=true`.

| GASP logical path | Enter condition | Blend Stack state passed to Chooser | Exit condition |
|---|---|---|---|
| Idle group -> Transition to Locomotion | `IsMoving` | `Transition to Locomotion Loop` | non-loop asset almost complete; no valid asset; or selected asset is looped; optional authored early-transition window |
| Locomotion group -> Transition to Idle | `NOT IsMoving` | `Transition to Idle Loop` | non-loop asset almost complete; no valid asset; or selected asset is looped; optional authored early-transition window |
| Any ground state -> Transition to In Air | `MovementMode == InAir` | In-Air chooser context | non-loop asset almost complete; no valid asset; or selected asset is looped |

Project_J mapping deliberately differs only at the source of facts:

| GASP state-machine input | Project_J thread-safe input | Reason |
|---|---|---|
| `IsMoving` Alias | `GetThreadSafeStateControllerWantsLocomotion` | Uses MM presentation movement, not broad gameplay movement that remains true while braking. |
| `NOT IsMoving` Alias | `GetThreadSafeStateControllerWantsIdle` | Gives Cycle -> Stop a visual transition even before gameplay is fully stationary. |
| `MovementMode == InAir` | `GetThreadSafeIsInAir` | C++ MovementMode snapshot owns ground/air state; it is higher priority than ground paths. |
| current State Machine state | `GetThreadSafeOneShotPhase` + `GetThreadSafeOneShotRequestRevision` | Chooser receives semantic phase/context without Actor access. |
| Notify transition-to-loop bool | `GetThreadSafeOneShotEarlyTransitionWindowOpen` | Set only by `UProject_JAnimNotifyState_LocomotionEarlyTransition`. |

`UProject_JAnimNotifyState_LocomotionEarlyTransition` is opt-in and local presentation only. It increments/decrements a game-thread depth counter to tolerate overlapping Blend Stack notifies, then the AnimInstance snapshots the resulting window for worker-thread State Controller conditions. It does not change CharacterMovement, gameplay state, replication, or Root Motion settings.

The profile field `ExperimentalOneShotFallbackLeadTime` defaults to zero. It is intentionally **not** a copied GASP 0.75-second constant: set an asset-specific lead time in the Chooser output when required, otherwise use an authored EarlyTransition Notify State or wait for the exact non-loop asset end.

### State Controller output contract (active only for authored chooser rows)

`FProject_JStateControllerChooserOutput` is the native equivalent of GASP's `S_ChooserOutputs`: `StartTime`, `bUseMotionMatch`, `MotionMatchCostLimit`, `BlendTime`, `BlendProfile`, and `Tags`. It deliberately contains no animation-asset field: the future Chooser/Blend Stack remains the sole asset owner, while C++ supplies only immutable gameplay and locomotion context.

The new `GetThreadSafeStateControllerPresentationState` resolves one of `IdleLoop`, `TransitionToLocomotion`, `LocomotionLoop`, `TransitionToIdle`, `TransitionToInAir`, or `InAirLoop` from the C++ snapshot. It is `Disabled` unless `bEnableExperimentalOneShotPresentation` is enabled. The AnimGraph's direct Blend Stack alpha is `HasSelectedAnimation`, so an un-authored chooser row cannot replace the regular Motion Matching pose.

`bEnableExperimentalIdleBreak` and `ExperimentalIdleBreakMinimumStateTime` are likewise an opt-in contract for GASP's Idle Loop -> Idle Break variation. C++ does not advance or inspect ABP state time; the future logical State Machine reads the threshold and owns the transition condition.

### Verified GASP re-entry contract

GASP uses four separate re-entry rules for `Transition to Locomotion`; they must not be collapsed into one unconditional MM re-search.

| Re-entry reason | GASP condition | Project_J policy |
|---|---|---|
| Semantic state changed | Movement Direction, Stance, or Gait differs from the preceding frame; current State time > 0 | `MotionMatchingSelectionRevision` increments only when the C++ selection context changes. The State Controller should use the revision as a single semantic-entry event. |
| Combat moving Pivot | `IsPivoting`; wait 0.5 s only when the current Blend Stack tags contain `Pivot` or `Start`, otherwise retry immediately if no pivot was selected | Combat-Strafe only. Do not apply to OTM. Current C++ uses input-turn reselect cooldown; replace/tune it only after Pivot tag metadata is authored in Project_J's chooser output. |
| Rotation break | target yaw differs by more than 60 degrees while a Start/Pivot is younger than 0.5 s and tags contain `Start` or `Pivot` | Future combat-only State Controller feature. It requires Blend Stack tags and a target-rotation snapshot; it is not a TIP implementation. |
| Authored early re-transition | `BP_NotifyState_EarlyTransition` window | `UProject_JAnimNotifyState_LocomotionEarlyTransition` publishes an equivalent thread-safe window. |

The GASP Pivot rule may retry selection while no Pivot animation is playing. That is a **combat moving Pivot** recovery policy, not the prohibited idle TIP design. Project_J must keep it isolated from TIP and from noncombat OTM.

### Verified idle re-entry and TIP reference (not implemented)

| GASP rule | Verified behavior | Project_J decision |
|---|---|---|
| Idle state changed | Stance changed and State time > 0; re-enter `Transition to Idle` to reselect Idle | Future stance/profile change may increment the existing semantic selection revision. Direction/gait changes alone must not restart Idle. |
| Idle TurnInPlace | `ShouldTurnInPlace`; enter immediately when current Blend Stack tags do not contain `TurnInPlace`; when already playing that tag, permit another turn only after 0.75 s | Reference only. Project_J TIP remains out of this MM refactor and must use Idle -> TurnInPlace -> Recovery with a data/asset-specific re-entry contract, not GASP's fixed 0.75 s. |

The observed GASP TIP rule confirms the architectural boundary: it is a logical idle-state transition whose chooser selects a turn asset. It is not an idle Motion Matching PSD that repeatedly searches the same database. Project_J must preserve this separation when TIP work begins.

### State Controller input contract implemented from the reviewed GASP rules

The following values are now snapshots generated on the game thread by `UProject_JCharacterAnimInstance` and are safe to call from an AnimGraph worker thread. They do not make the experimental State Controller active; that still requires the profile opt-in and the later ABP wiring.

| GASP rule/input | Project_J getter | Ownership / OTM policy |
|---|---|---|
| `-> Grounded` Conduit | `GetThreadSafeStateControllerIsGrounded` | C++ MovementMode snapshot; true only while the experimental presentation profile is enabled and the character is not in air. |
| `Conduit -> Transition to Locomotion` | `GetThreadSafeStateControllerWantsLocomotion` | MM presentation movement snapshot, not raw velocity. |
| `-> In Air` | `GetThreadSafeStateControllerIsInAir` | C++ MovementMode snapshot. It has priority over ground aliases. |
| `Loco - State Changed` | `GetThreadSafeStateControllerLocomotionSemanticStateChanged` | Gait / rotation-mode / Stand-Crouch changes for both modes, plus a quantized direction-sector change only in combat Strafe. ABP still adds `Current State Time > 0`. |
| `Idle - State Changed` | `GetThreadSafeStateControllerIdleSemanticStateChanged` | GASP-equivalent locomotion stance (`Stand` / `Crouch`) only. Project_J does not currently expose Crouch gameplay, so it remains `Stand`; combat, direction and gait never restart Idle. |
| GASP Movement Direction | `GetThreadSafeStateControllerStrafeDirection` | Six Strafe values (F/B/LL/LR/RL/RR), derived from movement direction plus static foot-forward bias. OTM always reports Forward and never re-enters from direction. |

The current direction sectors use -45, 45, -135, and 135 degrees to align with the authored directional Chooser ranges. They are not motion-matching search thresholds and they do not alter CharacterMovement, trajectory, PSD selection, or noncombat locomotion. Per-profile hysteresis remains future work.

`GetThreadSafeStateControllerStance` snapshots `ACharacter::bIsCrouched`, which is replicated by CharacterMovement. This prepares the same contract as GASP without inventing a combat-to-stance mapping or introducing a Crouch mechanic into Project_J.

### Verified GASP Chooser hierarchy and Blend Stack output contract

The reviewed GASP assets use two distinct chooser hierarchies. They must remain separate in Project_J.

```text
CHT_PoseSearchDatabases
  -> MMDatabaseLOD (Dense / Sparse / ExtremelySparse)
  -> MovementMode + Stance + MovementState + Gait
  -> PSD array for the regular Motion Matching node

CHT_CMCCharacterAnimations
  -> logical State Machine State + Stance + Gait
  -> optional Movement Direction / Speed / target-yaw / Pivot / Land conditions
  -> Animation Asset + S_ChooserOutputs for the Blend Stack
```

`S_ChooserOutputs` is the required metadata contract for the second hierarchy:

| GASP member | Type | Required Project_J meaning |
|---|---|---|
| `StartTime` | float | Authored entry time for the selected Blend Stack animation. |
| `UseMM` | bool | Permit exactly one State-Entry pose search among the Chooser candidates; never use this for continuous Idle TIP re-search. |
| `MMCostLimit` | float | Maximum accepted one-shot pose-search cost. |
| `BlendTime` | float | Selected-asset transition blend duration. |
| `BlendProfile` | Name | Data-selected Blend Profile identifier. |
| `Tags` | Name array | Semantic presentation tags such as `Start` and `Pivot`, used by re-entry and early-break rules. |

The `Stand Runs F` table verifies that GASP's State Controller returns direct animation assets and this metadata. Its rows include a looping run, starts/reface starts, run-to-sprint transitions, pivots and lands. This is why the current regular-MM node alone cannot guarantee full Start/Stop playback.

### Verified GASP movement-direction policy

GASP updates direction only while `MovementState == Moving`. It derives a -180..180 direction from **future trajectory velocity** and capsule/orientation-intent rotation. In `OrientToMovement`, or while using GASP's Sprint gait, it forces `F`. In Strafe it uses four thresholds (`FL`, `FR`, `BL`, `BR`) and a foot-forward bias to choose `F`, `B`, `LL`, `LR`, `RL` or `RR`.

Project_J exposes the GASP-style six-direction Strafe snapshot. `StateControllerMovementDirectionBias` defaults to `LeftFootForward` and resolves side sectors to LL/RL; changing the static default resolves them to LR/RR. It remains Strafe-only; Project_J OTM always resolves to Forward.

## Debug commands

## State Controller Chooser integration (current pass)

`CHT_Player_StateControllerAnimations` is the Project_J counterpart of the
GASP `CHT_CMCCharacterAnimations` hierarchy.  It is deliberately **not** a
Pose Search Database table: regular locomotion still uses a PSD array through
the normal Motion Matching node, whereas this table chooses one authored
Animation Asset for a logical presentation state.

The game-thread AnimInstance evaluates the configured chooser only when its
selection context changes and copies its immutable result into the animation
proxy.  The worker-thread AnimGraph consumes only these snapshot getters:

| Blend Stack input | Thread-safe getter |
|---|---|
| Animation Asset | `GetThreadSafeStateControllerSelectedAnimation` |
| Animation Time | `GetThreadSafeStateControllerSelectedAnimationStartTime` |
| Loop | `GetThreadSafeStateControllerSelectedAnimationShouldLoop` |
| Blend Time | `GetThreadSafeStateControllerSelectedAnimationBlendTime` |
| Blend Profile | `GetThreadSafeStateControllerSelectedAnimationBlendProfile` |

The selected `FProject_JStateControllerChooserOutput` supplies `StartTime`, `bUseMotionMatch`, `MotionMatchCostLimit`, `BlendTime`, `BlendProfile`, and `Tags`. When `bUseMotionMatch` is `true` (matching GASP's `Run Stops` and `Sprint Stops` defaults), C++ evaluates a 1-shot `UPoseSearchLibrary::MotionMatch` query on the state entry frame against `PoseHistory`. It dynamically calculates the best matching `StartTime` for the selected animation (skipping initial running stride frames) without performing continuous per-frame re-search. This preserves authored transition completion while ensuring the braking foot contact aligns immediately with the character's locomotion stride.

### Authored Start/Stop completion hold

The first implementation exposed the logical presentation state directly from
the C++ locomotion phase. That was insufficient: the physical locomotion phase
can change from `Start -> Cycle` or `Stop -> Idle` before the selected direct
Blend Stack asset has reached its authored end. GASP avoids that failure by
keeping its logical transition state until `IsAnimationAlmostComplete` observes
the current Blend Stack asset.

Project_J now implements the equivalent **presentation-only** hold on the game
thread. It starts when a direct State Controller transition asset is selected,
uses `UAnimationAsset::GetPlayLength() - S_ChooserOutputs::StartTime`, and
keeps `Transition to Locomotion`, `Transition to Idle`, or `Transition to In
Air` active until one of the following is true:

- the selected asset reaches its authored end (or the optional profile lead
  time);
- `UProject_JAnimNotifyState_LocomotionEarlyTransition` is currently open;
- the chooser has no valid direct asset, so the GASP `No Valid Anim` escape
  applies; or
- gameplay intent genuinely reverses, for example Start -> Stop, Stop -> Start,
  or ground -> air.

This is not a fixed duration or playback-percent state gate. It never drives
CharacterMovement, physics, root motion, replication, or regular Motion
Matching PSD selection. It only prevents the visual State Controller from
discarding an authored one-shot because C++ movement has already reached its
next physical phase.

The new worker-thread conditions are:

| State Controller rule | Thread-safe getter |
|---|---|
| GASP `IsAnimationAlmostComplete` equivalent | `GetThreadSafeStateControllerSelectedAnimationAlmostComplete` |
| diagnostics / remaining authored time | `GetThreadSafeStateControllerSelectedAnimationTimeRemaining` |

For each transition-state -> loop-state edge in ABP, use
`AlmostComplete AND Current State Time > 0`, with the existing `No Valid Anim`
or selected-loop escape. Do not exit merely because `OneShotRequested` becomes
false; that request reflects physical locomotion phase, not direct asset
playback.

The initial editor table is intentionally OTM-only:

- `Idle Loop` -> `M_Neutral_Stand_Idle_Loop`
- `Transition to Locomotion` -> the two forward Run Start foot variants
- `Locomotion Loop` -> `M_Neutral_Run_Loop_F`
- `Transition to Idle` -> the two forward Run Stop foot variants

Strafe, Jump/Fall, Landing, Pivot, steering, and TIP entries remain empty
until their authored asset/metadata contracts are authored.  This keeps the
existing regular Motion Matching branch as the fallback and prevents a new
experimental layer from changing noncombat OTM behavior merely because combat
entries are incomplete.

The profile switch is
`DA_Player_Profile -> Motion Matching Search Policy -> State Controller
Animation Chooser Table`.  Assign `CHT_Player_StateControllerAnimations` only
after the five Blend Stack pins above are bound.  Keep
`bEnableExperimentalOneShotPresentation` false until the first OTM smoke test;
that flag is the single safe rollout gate.

With `p.ProjectJ.MMTransitionDebug 1`, a context change also emits one
`StateControllerChooser` line.  It reports the selected presentation state,
rotation mode, gait, stance, Strafe direction, selected animation, asset length,
StartTime, Loop, BlendTime, `UseMM`, tag count, held elapsed/remaining time,
and completion state. This makes a missing chooser row
distinguishable from a Blend Stack binding issue without pausing movement.

Run these in the PIE game console.

```text
p.ProjectJ.MMTransitionDebug 1
DumpMotionMatchingTransitionTrace

p.ProjectJ.MMNetDebug 1
DumpMotionMatchingTrace
DumpMMOProfilingSnapshot
DumpLocomotionKinematics
```

| Trace field | Meaning |
|---|---|
| `RequestedPSD` | PSD requested by C++ policy |
| `NativePSD` | PSD selected by active generated MM node |
| `Interrupt=0/1` | 0 DoNotInterrupt; 1 InterruptOnDatabaseChange |
| `Continuing` | continuing-pose search persisted |
| `NewBlend` / `Stack[]` | Blend Stack insertion, playback and weight |
| `FutureSpeed` / `FutureTurn` / `FutureValid` | reconstructed trajectory velocity diagnostics; confirms whether Start/Pivot used real future samples or fallback prediction |

The transition trace retains 720 samples so one normal Start -> Cycle -> Stop
pass is not displaced by a later jump/landing transition before it is dumped.

## Editor state and remaining order

No new ABP node is needed for the **current regular-MM policy**. Keep the existing graph order.

```text
Motion Matching -> cached Locomotion -> UpperBody layer/slot -> Aim Offset
-> Foot Placement -> Leg IK -> Pose History
```

| Profile field | Default |
|---|---:|
| `DefaultBlendTime` | 0.20 s |
| `LandingBlendTime` | 0.50 s |
| `JumpBlendTime` | 0.15 s |
| `AirBlendTime` | 0.50 s |

1. Verify the wired direct State Controller branch in PIE. The State Machine is logical only; it must still be evaluated before its direct Blend Stack is blended over the regular-MM fallback.
2. For each direct transition state, use `SelectedAnimationAlmostComplete AND Current State Time > 0` to enter its loop state. Also retain the `No Valid Anim OR SelectedAnimationShouldLoop` escape. Do not exit merely because the physical C++ phase changed.
3. Confirm that an input reversal immediately replaces the current transition request: Start -> Stop and Stop -> Start are allowed. The C++ playback hold protects only natural continuation, not opposite gameplay intent.
4. Add Strafe, air and landing direct rows only after their individual asset / metadata contracts are reviewed. Un-authored rows must have no selected asset so the regular-MM fallback remains visible.
5. Profile Moving/Light/Heavy Land eligibility; leave timers only as safety fallbacks.
6. Decide separately on combat-only Steering and StrafeYawOffset authored data.
7. Design TIP separately: Idle -> TurnInPlace -> Recovery; no repeated MM PSD search and no noncombat OTM change.

### 2026-08-01 ABP wiring snapshot

The active `ABP_Humanoid_Master` architecture is intentionally a hybrid. It
does **not** replace the original Pose Search Motion Matching node.

```text
Regular PSD chooser -> Motion Matching -> cached Locomotion -----------------+
                                                                          fallback|
StateController (logical only) -> Inertialization ----------------------------+-- Two Way Blend
                                                                                |
CHT_Player_StateControllerAnimations -> C++ snapshot -> direct Blend Stack ---+-- alpha = HasSelectedAnimation
                                                                                |
                                                          cached Locomotion / downstream presentation
```

The direct Blend Stack inputs must be bound exclusively to the thread-safe
snapshot getters listed in the integration section above. The `Two Way Blend`
which exposes the direct stack uses `HasSelectedAnimation ? 1.0 : 0.0`; this
prevents an empty Strafe/Air row from overriding the known-good regular MM
pose. The output replaces the source that is saved as cached pose `Locomotion`,
then the pre-existing UpperBody, Slot, Aim Offset, Foot Placement, Leg IK and
the single final Pose History node continue unchanged.

`CHT_Player_StateControllerAnimations` returns a **single AnimationAsset**,
not a PSD. This is correct: it is the direct authored one-shot/loop selector
for the Blend Stack. `DA_Player_Locomotion` and the regular Motion Matching
Chooser remain the PSD-array selector. These two systems must not be merged.

Current authored direct rows are OTM / Stand only: Idle Loop, Run Start
(left/right foot), Run Loop, and Run Stop (left/right foot). Rows for Strafe,
air, landing, Pivot and TIP are intentionally empty and therefore fall back to
regular MM.

### Current Start / Stop diagnostic interpretation

`StateControllerChooser` trace lines report the C++ selector decision, not the
actual elapsed time/weight of the AnimGraph Blend Stack. The observed sequence
`Start -> Stop` on input release proves that the selector receives the correct
gameplay intent; it is not by itself evidence that the Stop asset was visually
played to completion. If visual playback is still short, inspect in this order:

1. direct Blend Stack is connected to the final cached-locomotion source and
   its `HasSelectedAnimation` alpha is one while the row is selected;
2. the logical transition-state -> loop-state rule uses
   `SelectedAnimationAlmostComplete`, not `WantsLocomotion` / `WantsIdle` or a
   physical phase change;
3. the direct asset's Loop input is false for Start/Stop and true only for the
   loop asset;
4. no Slot, montage or later pose path is replacing the cached locomotion pose.

The state-controller C++ hold is presentation-only and allows immediate
opposite intent replacement. It must never delay CharacterMovement, network
prediction, input responsiveness, or the regular MM policy.

## GASP import rule for the next pass

The next screenshots will be classified before implementation.

| GASP input/function type | Project_J import decision |
|---|---|
| Future trajectory velocity / facing / turn angle | Implemented: game-thread snapshot reconstructs real future velocity and exposes it read-only to ABP. |
| Chooser result, selected database/animation/tag | Keep C++ data selection and proxy diagnostics; ABP only applies the resulting pose. |
| Start/Stop one-shot Blend Stack functions | Import as a dedicated presentation layer; do not mix it into normal Cycle MM. |
| Root-motion Steering and Strafe yaw curves | Combat-only candidate, gated until authored data and movement ownership are defined. |
| Traversal or wall/obstacle functions | Excluded from this MMORPG locomotion scope. |
