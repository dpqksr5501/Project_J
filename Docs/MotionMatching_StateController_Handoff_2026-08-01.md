# Project_J locomotion handoff — 2026-08-01

## Purpose of this handoff

This document is the authoritative continuation context for the current
Project_J UE 5.8 locomotion refactor. The goal is to preserve the known-good
regular Motion Matching locomotion while adding a GASP-inspired, direct
AnimationAsset State Controller / Blend Stack presentation path for authored
Start and Stop. This is a staged rollout, not TIP work.

## Why this refactor exists

The original Project_J graph could look natural in simple play, but its
responsibilities were mixed: Blueprint/ABP selected gameplay-relevant state,
locomotion phase, rotation behavior, asset choice and final visual pose in the
same places. That makes four problems hard to solve safely:

1. **Authored transitions lose their intent.** Continuous Motion Matching is
   excellent for locomotion cycles, but it may reselect a Start or Stop at a
   middle timestamp, or replace it as soon as the physical movement phase
   changes. The result is visually short Starts/Stops despite valid assets.
2. **Thread safety is fragile.** ABP worker-thread code must not repeatedly
   inspect Character, Controller, Components, world state, timers or gameplay
   tags. Those facts must be collected once on the game thread.
3. **Network semantics become ambiguous.** Autonomous proxies own input
   intent; simulated proxies must use replicated movement/state rather than
   infer local controller yaw or input. Visual presentation must never become
   a second gameplay authority.
4. **Future features collide.** Combat Strafe Pivot, RootYawOffset/TIP,
   weapon profiles and performance tiers cannot be added safely when OTM and
   Strafe share unscoped rotation/asset selection logic.

The refactor therefore separates the system into four layers:

```text
C++ Character / Locomotion Component
  authoritative gameplay + locomotion state, trajectory, network policy,
  data/profile interpretation, debug trace

AnimInstance game-thread snapshot
  immutable worker-thread input only

ABP / Linked Layers
  pose assembly only: MM, BlendStack, layers, slots, AO, IK, Pose History

Data Assets / Choosers
  asset families, eligibility, thresholds, blend metadata and tags
```

This is why the direct Blend Stack is not a replacement for Motion Matching:
it gives authored transitions an explicit presentation owner, while regular MM
continues to solve dense, responsive locomotion matching.

## Non-negotiable project constraints

- Noncombat is third-person **Orient-to-Movement** (OTM). It must remain
  forward-oriented and must not receive combat Pivot logic.
- Combat is **Strafe**, facing camera/control direction. Combat-only direction,
  Pivot and future Steering remain isolated from OTM.
- Skeletal mesh relative yaw is `-90`. Do not add an arbitrary compensating
  `+90` node.
- `Root Motion from Everything` is prohibited because it caused floor sliding.
- TIP is explicitly deferred. Later it must be `Idle -> TurnInPlace ->
  Recovery`, with clear visual-root yaw ownership; do not implement TIP with
  repeated MM PSD searches or a continually accumulating Blend Stack.
- C++ owns gameplay, locomotion, trajectory, selection policy and networking.
  AnimInstance copies game-thread data into snapshots. Worker-thread ABP only
  reads getters and assembles poses.
- Do not use Unreal MCP unless the user explicitly requests it.

## Architecture after the current pass

There are two intentionally separate animation selectors:

```text
Regular locomotion MM
  C++ locomotion state -> PSD chooser / DA_Player_Locomotion -> PSD array
  -> Motion Matching node -> cached Locomotion fallback pose

GASP-style State Controller presentation
  C++ snapshot -> logical StateController -> Chooser single AnimationAsset
  -> direct Blend Stack -> selected-asset overlay/fallback blend
```

### Why direct State Controller uses one animation, not a PSD

The normal Pose Search Motion Matching node searches a PSD containing many
clips; this remains the correct system for regular locomotion. GASP's
`CHT_CMCCharacterAnimations` is different: it selects one authored
`AnimationAsset` plus per-entry metadata for a Blend Stack. This direct path
is how GASP preserves authored Starts, Stops, Pivots and transition clips
instead of allowing continuous MM to jump into their middle.

Project_J's direct table is `CHT_Player_StateControllerAnimations`; its result
class must be `AnimationAsset`, not `PoseSearchDatabase`. PSDs remain in
`DA_Player_Locomotion` / the existing regular-MM chooser.

## C++ work already completed

The current code has been compiled successfully after the latest change.

- Locomotion/animation state is owned in C++ and copied into thread-safe
  AnimInstance snapshots.
- Future trajectory velocity is available and used for intended movement,
  rather than relying only on current velocity.
- Snapshot state includes movement mode, OTM/Strafe rotation mode, gait intent,
  Stand/Crouch stance, combat-only Strafe direction, input intent,
  acceleration, in-air/landing data, phase, selected PSD and diagnostics.
- GASP `Stance` was correctly mapped to CharacterMovement `Stand/Crouch`, not
  combat state. Current project has no Crouch gameplay so it resolves Stand.
- State Controller snapshot APIs exist, including:
  - `GetThreadSafeStateControllerPresentationState`
  - `GetThreadSafeStateControllerWantsLocomotion`
  - `GetThreadSafeStateControllerWantsIdle`
  - `GetThreadSafeStateControllerIsGrounded`
  - `GetThreadSafeStateControllerIsInAir`
  - `GetThreadSafeStateControllerLocomotionSemanticStateChanged`
  - `GetThreadSafeStateControllerIdleSemanticStateChanged`
  - `GetThreadSafeStateControllerStance`
  - `GetThreadSafeStateControllerStrafeDirection`
  - `GetThreadSafeStateControllerSelectedAnimation`
  - `GetThreadSafeStateControllerSelectedAnimationStartTime`
  - `GetThreadSafeStateControllerSelectedAnimationShouldLoop`
  - `GetThreadSafeStateControllerSelectedAnimationBlendTime`
  - `GetThreadSafeStateControllerSelectedAnimationBlendProfile`
  - `GetThreadSafeStateControllerHasSelectedAnimation`
  - `GetThreadSafeStateControllerSelectedAnimationAlmostComplete`
  - `GetThreadSafeStateControllerSelectedAnimationTimeRemaining`
- `FProject_JStateControllerChooserOutput` maps GASP `S_ChooserOutputs`:
  `StartTime`, `bUseMotionMatch`, `MotionMatchCostLimit`, `BlendTime`,
  `BlendProfile`, `Tags`. The selected asset is the Chooser result, not a field
  in the metadata struct.
- **bUseMotionMatch (1-shot Pose Search on entry)**: Evaluates `UPoseSearchLibrary::MotionMatch`
  on the State Controller entry frame when `ChooserOutput.bUseMotionMatch` is true (matching GASP's `Run Stops` and `Sprint Stops` defaults). It dynamically matches current `PoseHistory` (using `"PoseHistory"` / `"PoseSearchHistoryCollector"` node tags) to candidate animation poses, updating `StartTime` to the exact plant/braking frame timestamp rather than blindly starting from 0.0s.
- **Hold Timer Initialization Fix**: Resolved the `HoldElapsed` 16,793,344s clock bug by ensuring `StateControllerPlaybackHoldStartedAtSeconds` is initialized to current `NowSeconds` on state transition entry.
- **TransitionToLand Enum Addition**: Added `TransitionToLand` to `EProject_JStateControllerPresentationState` and integrated it into `ResolveStateControllerPresentationState`, `IsTransitionState`, and `IsNaturalLoopContinuation` in C++, enabling explicit Landing transition routing in the Chooser Table.
- **Data Asset Collision Avoidance**: Documented that `DA_Player_Locomotion` (`Project_JMotionMatchingAssetSet`) must set `Start` and `Stop` database slots to `None` when using State Controller Chooser, leaving `Start`/`Stop` 1-shot animations strictly to the Chooser stack to prevent dual-track pose conflicts.
- **Nested Chooser Evaluation Fix**: Fixed C++ `EvaluateStateControllerAnimationChooserOnGameThread` to recursively unpack nested `UChooserTable` objects (when a Parent Chooser with `Result Class = ChooserTable` returns a Sub-Chooser Table), correctly resolving the final `UAnimationAsset` and populated `FProject_JStateControllerChooserOutput` struct parameters.
- **Enhanced StateController Real-time Debug Logs**: Added `StateControllerHold` and `StateControllerExitHold` detailed logs under `p.ProjectJ.MMTransitionDebug 1` for real-time tracking of hold elapsed times, playable lengths, and exit triggers.
- **ABP StateController Intent Reversal Topology**: Verified and documented the required direct transition edge `Transition To Locomotion` -> `Transition To Idle` using `GetThreadSafeStateControllerWantsIdle`. This eliminates state machine delay when input is released mid-Start, allowing immediate transition into Stop.

## ABP work already authored manually

Asset: `ABP_Humanoid_Master`.

1. Existing regular MM is still present:

   `Get Current Active Pose Search Database Thread Safe -> Motion Matching -> cached Locomotion`.

2. A logical-only `StateController` state machine exists with these states:

   - `Idle Loop`
   - `Transition to Locomotion`
   - `Locomotion Loop`
   - `Transition to Idle`
   - `Transition to In Air`
   - `In Air Loop`

   Its state graphs have no pose responsibility. It only produces update/state
   evaluation for the direct presentation branch.

3. A direct State Controller `Blend Stack` is wired to the selected-animation
   thread-safe getters. Its five input bindings are Animation Asset, Animation
   Time, Loop, Blend Time and Blend Profile.

4. The direct Blend Stack is blended over/falls back to regular locomotion with
   a `Two Way Blend`. Its alpha is `Select Float(HasSelectedAnimation, A=1,
   B=0)`; therefore un-authored Chooser rows remain regular MM.

5. The final chosen locomotion pose must be what is saved as cached pose
   `Locomotion`. The pre-existing downstream graph remains unchanged:

   `CombatUpperBody -> ResolvedLocomotion -> upper-body Slot/Layered blend ->
   Aim Offset -> Foot Placement -> Leg IK -> Pose History -> Output`.

6. Pose History has a single final ABP owner. Any native fallback reference is
   diagnostic/fallback only, not a second final pose-history path.

## Direct chooser table current content

Table: `CHT_Player_StateControllerAnimations`.

Parameters:

- Input object: `Project_JCharacterAnimInstance`
- output: `ChooserPlayerSettings`
- output: `Project_JStateControllerChooserOutput`

Columns added:

- State Controller Presentation State for Chooser
- Rotation Mode for Chooser
- Gait Intent for Chooser
- State Controller Stance for Chooser
- State Controller Strafe Direction for Chooser

Current authored OTM/Stand rows:

| Presentation state | Rotation | Gait | Asset |
|---|---|---|---|
| Idle Loop | OTM | Any | `M_Neutral_Stand_Idle_Loop` |
| Transition to Locomotion | OTM | Run | `M_Neutral_Run_Start_F_Lfoot`, `_Rfoot` |
| Locomotion Loop | OTM | Run | `M_Neutral_Run_Loop_F` |
| Transition to Idle | OTM | Run | `M_Neutral_Run_Stop_F_Lfoot`, `_Rfoot` |

Strafe, InAir, land, Pivot and TIP rows intentionally remain empty. The
regular MM path must take over in those cases.

## GASP facts that should guide the next work

- GASP's Experimental State Machine is logical only. Each state entry calls
  `SetBlendStackAnimFromChooser`; a Blend Stack carries the actual pose.
- Transition state -> loop state uses a manual equivalent of Automatic Rule:
  `IsAnimationAlmostComplete AND Current State Time > 0`.
- The GASP escape condition is `No Valid Anim OR selected asset is Loop`; this
  prevents a transition state from stalling if a chooser found no one-shot.
- GASP optionally uses a notify state (`BP_NotifyState_EarlyTransition`) to
  leave an authored transition early. Project_J's equivalent exists but is not
  yet authored on assets.
- GASP re-entry compares semantic state changes (direction/stance/gait). Do
  not turn it into unconditional re-search. Pivot/retry is Strafe-only.
- GASP forces Movement Direction Forward for OTM; direction quadrants are for
  Strafe, not noncombat OTM.
- GASP steering relies on root motion and Offset Root Bone; Project_J should
  not copy it now due its Root Motion policy.

## GASP coverage matrix — implemented, pending, and excluded

This matrix prevents two common mistakes: assuming a reviewed GASP screenshot
is already active in Project_J, and copying a GASP feature whose assumptions
conflict with the MMORPG / no-Root-Motion policy.

| GASP area / function | Project_J status | Project_J equivalent or decision | Notes |
|---|---|---|---|
| `Update Properties from Character` | Implemented, architecture differs | C++ game-thread locomotion snapshot -> AnimInstance proxy | Safer than ABP direct Actor access. |
| `Blueprint Thread Safe Update Animation` / driver values | Implemented | Native snapshot getters | ABP reads only snapshot data. |
| `Update Trajectory` | Implemented in C++ | Future trajectory velocity/sample reconstruction | No Blueprint world collision trace copy. |
| `IsMoving` | Implemented | MM presentation movement + input/future velocity | Intended movement, not only physical current speed. |
| `IsStarting` | Implemented / tuned | C++ phase policy and selection revision | OTM-safe; Pivot suppression is Strafe-only. |
| `PlayLand`, Light/Heavy land eligibility | Implemented at policy level | Ground phase / landing snapshots / DA families | Direct authored land StateController rows pending. |
| `Get_LandVelocity` | Implemented | landing velocity snapshot | Used for land policy/debug. |
| `Get_TrajectoryTurnAngle` | Implemented | future trajectory turn snapshot | Used for selection/reselect diagnostics. |
| `Update_MotionMatching` | Implemented | C++ profile/DA selection + MM node binding | Regular MM stays PSD-array based. |
| `Get_MMInterruptMode` | Implemented | C++ interrupt policy | Core semantic change only; avoids needless interrupt. |
| `Get_MMBlendTime`, notify recency | Implemented / data driven | profile/phase settings | Not a substitute for authored one-shot completion. |
| `Get_PoseHistoryReference` | Implemented | ABP final PoseHistory + native fallback diagnostics | Single final ABP owner. |
| `Update_MotionMatching_PostSelection` | Partially implemented | selected PSD/tag/trace diagnostics | Per-result Blend Profile override needs later data validation. |
| `Update States` | Implemented | C++ snapshots for mode, rotation, intent, gait, stance, last semantic state | Stance correctly means Stand/Crouch, not combat. |
| `Update Movement Direction` | Partial | combat Strafe coarse direction snapshot | OTM always Forward; GASP six-direction foot bias deferred. |
| `Get_MovementDirectionThresholds` | Partial / deferred | baseline Strafe hysteresis only | Full foot-forward quadrant policy needs authored Strafe data. |
| `Update Target Rotation` / Strafe yaw offset | Deferred | no active copied GASP target rotation layer | Required later for combat-only authored direction/Pivot. |
| `IsPivoting` / moving Pivot selection | Partial | C++ signal/cooldown and combat-only policy | Direct Strafe Pivot chooser rows/tags are not authored yet. |
| Rotation-break reselection | Deferred | none active | Requires selected tags + target rotation snapshot + Strafe assets. |
| `SetBlendStackAnimFromChooser` | Implemented in architecture | C++ chooser evaluation -> snapshot -> direct Blend Stack | Not a Blueprint function clone; same ownership outcome. |
| Logical State Controller states | Implemented in ABP | Idle/Transition/Loop/InAir state graphs | Rules and direct playback validation are ongoing. |
| `IsAnimationAlmostComplete` | Implemented C++ API; ABP rule validation pending | `SelectedAnimationAlmostComplete` | Must control transition -> loop exits. |
| `No Valid Anim` / selected-loop escape | API/architecture ready; ABP rule validation pending | `HasSelectedAnimation`, `ShouldLoop` | Empty rows deliberately fall back to MM. |
| `BP_NotifyState_EarlyTransition` | Implemented native equivalent; not authored | `UProject_JAnimNotifyState_LocomotionEarlyTransition` | Add only to reviewed direct one-shot assets. |
| `S_ChooserOutputs` | Implemented | `FProject_JStateControllerChooserOutput` | Asset comes from Chooser result; metadata comes from struct. |
| `CHT_PoseSearchDatabases` hierarchy | Implemented conceptually | existing DA/Profile/regular MM PSD selection | Current Project_J data layout differs from GASP tables. |
| `CHT_CMCCharacterAnimations` hierarchy | Implemented staged | `CHT_Player_StateControllerAnimations` | OTM first; Strafe/Air/Land empty by design. |
| Dynamic Play Rate from `MoveData_Speed` / curves | Deferred | no active direct BlendStack rate warp | Apply later only where curves are consistently authored. |
| Orientation Warping | Existing ABP presentation / not expanded | current graph handling | Do not import experimental GASP internal graph blindly. |
| Steering | Not used | explicitly deferred | GASP assumes root-motion/ORB behavior that conflicts with current policy. |
| Offset Root Bone root translation/rotation policy | Deferred | none newly introduced | Needed only after clear single-owner rotation design. |
| Foot Placement / Leg IK / Aim Offset / slots | Existing ABP retained | current downstream pose graph | Remain presentation-only. |
| Traversal, wall/obstacle collision trajectory | Excluded | none | Not in current MMORPG scope. |
| Mount/traversal special layers | Excluded for current locomotion task | existing mounted layer untouched | Do not let it alter core OTM/Strafe work. |
| TIP | Deferred deliberately | future dedicated state machine | See TIP roadmap; no MM PSD re-search. |

## Complete reviewed-GASP inventory

This is the exhaustive inventory of the GASP screenshots, annotations and
function graphs reviewed in this conversation. “Complete” means **everything
provided by the user in this task**, not an assertion that every function in
Epic's entire GASP project has been inspected. Anything not shown is labelled
unreviewed rather than inferred.

### A. Top-level ABP execution model

| GASP graph / node | Reviewed role | Project_J status / decision |
|---|---|---|
| `Update_PropertiesFromCharacter` EventGraph function | Gets character animation properties through a Blueprint interface and stores `Character Properties` | Replaced with native C++ game-thread collection and snapshot; ABP must not directly query Actor. |
| `Blueprint Thread Safe Update Animation` | Copies/update values for worker-thread-safe reads | Replaced by native AnimInstance snapshot/proxy getters. |
| `Update_Logic` | Calls Update Trajectory -> Update Essential Values -> Update States -> optionally Update Movement Direction -> Update Target Rotation | Split into C++ locomotion/AnimInstance update; no monolithic ABP gameplay logic. |
| `AnimationBlendStackGraph_0` | Internal graph instantiated per selected MM animation | Do not copy blindly; current direct Blend Stack is separate and regular MM stays as existing node. |
| `State Controller` State Machine | Pure logic state machine, no pose output | Authored in Project_J ABP; rules still being validated. |
| `State Machine Blend Stack` | Direct authored asset presentation pose | Authored/wired in Project_J with thread-safe selected-asset getters. |
| `Inertialization` | Smooths logical State Controller path | Authored in Project_J around the logical path. |
| `Two Way Blend` with Always Update Children | Evaluates logical State Controller while Blend Stack supplies pose | Project_J uses equivalent hybrid evaluation/fallback wiring. |
| `Motion Matching` node | Regular PSD-array continuous search | Existing Project_J regular MM retained. |
| `Pose History` | trajectory and prior-pose cache for MM | Final ABP node remains the only pose-history owner. |
| Apply Mesh Space Additive / AO | presentation additive | Existing Project_J retained. |
| Foot Placement / Leg IK | post-locomotion presentation | Existing Project_J retained. |
| UpperBody layer / Slot / Layered Blend Per Bone | gameplay montage presentation | Existing Project_J retained. |

### B. GASP data and state enums observed

| Item | Meaning in reviewed GASP | Project_J policy |
|---|---|---|
| Movement Mode | normalized CMC state, e.g. OnGround/InAir | Native movement-mode snapshot. |
| Rotation Mode | OrientToMovement / Strafe / Aim | Project_J OTM/Strafe snapshot; Aim not separately adopted yet. |
| Movement State | intended Moving vs Idle, computed from current/future trajectory rather than raw physical velocity | `bIsMotionMatchingMoving` / StateController WantsLocomotion/WantsIdle. |
| Gait | intended Walk/Run/Sprint input style, not current speed | `EProject_JLocomotionGaitIntent`. |
| Stance | Stand/Crouch, not weapon/combat | C++ `bIsCrouched` snapshot; currently Stand. |
| Movement Direction | F/B/LL/LR/RL/RR with foot-forward bias in GASP | Project_J OTM=Forward; coarse Strafe sectors only. |
| Target Rotation | desired visual rotation for Strafe state machine/steering | Deferred; no generic OTM use. |
| Target Rotation Delta | root/target yaw difference used to choose starts/turns | Deferred except existing safe diagnostics. |
| Character Transform | capsule/actor transform cache | native snapshot equivalent. |
| Root Transform | Offset Root Bone transform with +90 mesh-axis compensation in GASP | Do not add compensation blindly; Project_J mesh is already -90. |
| Velocity / Last Velocity / Speed2D | physical movement observation | native locomotion snapshot. |
| Acceleration / relative acceleration / last non-zero velocity | input/kinematic analysis | native snapshot/lean inputs available. |
| Trajectory past/current/future velocities | history and predicted intent | native C++ trajectory samples. |
| JustLanded / LandVelocity | land event and impact severity | native landing snapshot. |

### C. Movement-analysis functions reviewed

| GASP function | Exact reviewed behavior | Project_J status |
|---|---|---|
| `IsMoving` | Requires current velocity above tiny threshold, future trajectory velocity above threshold and acceleration condition; distinguishes intended stop/move | Implemented semantic equivalent. |
| `IsStarting` | `IsMoving` and future speed >= current speed + 100; returns false while current DB tags contain Pivot | Implemented policy equivalent; Pivot exclusion is Strafe-only. |
| `IsPivoting` | Uses current/future trajectory direction change; details partly shown under MM Pivot conditions | Partial; combat-only, direct Pivot assets pending. |
| `Get_TrajectoryTurnAngle` | yaw delta between current and future trajectory velocity | Implemented snapshot/diagnostic. |
| `ShouldTurnInPlace` | evaluates idle/root-control yaw mismatch and aiming/stopped conditions | Reviewed only as future TIP reference; deliberately not active. |
| `ShouldSpinTransition` | large root/capsule yaw mismatch for rotational transition | Deferred; no generic activation. |
| `JustTraversed` | traversal montage return condition | Excluded with traversal scope. |
| `JustLanded_Light` | just-landed and abs(Z land velocity) below heavy threshold | Implemented policy-level landing classification. |
| `JustLanded_Heavy` | just-landed and abs(Z land velocity) at/above heavy threshold | Implemented policy-level landing classification. |
| `Get_LandVelocity` | returns saved landing Z velocity | Implemented snapshot. |
| `PlayLand` | OnGround now and InAir previous frame | Implemented ground-phase event equivalent. |
| `PlayMovingLand` | PlayLand plus abs trajectory turn <= 120 degrees | Implemented/preserved landing eligibility intent; direct land presentation pending. |

### D. Root-offset, aim, lean and foot functions reviewed

| GASP function | Exact reviewed behavior | Project_J decision |
|---|---|---|
| `Get_OffsetRootRotationMode` | releases rotation under montage, otherwise accumulates | Deferred pending single rotation-owner plan. |
| `Get_OffsetRootTranslationMode` | montage/air/idle releases translation; moving interpolates | Deferred; no new ORB authority. |
| `Get_OffsetRootTranslationHalfLife` | chooses Idle/Moving translation recovery rate | Deferred with ORB. |
| `Get_OffsetRootTranslationRadius` | maximum translation offset radius | Deferred with ORB. |
| `Get_OrientationWarpingWarpingSpace` | changes warping space when Offset Root Bone is active | Current ABP presentation retained; no new ORB-dependent copy. |
| `Enable_AO` | enables AO according to Strafe/root-camera alignment/slot weight | Project_J has safe AO alpha snapshot; exact GASP criteria not fully ported. |
| `Get_AOValue` | converts root/camera aiming delta to AO X/Y and curve softens it | Project_J existing AimYaw/AimPitch snapshot; curve-specific parity deferred. |
| `Get_AO_Yaw` | chooser/Idle Break yaw source | Not separately needed until Idle Break/TIP data is authored. |
| `CalculateRelativeAccelerationAmount` | normalizes local acceleration/deceleration to -1..1 | Existing C++/ABP lean inputs preserve equivalent purpose. |
| `Get_LeanAmount` | scales relative acceleration by speed for additive lean | Existing presentation may retain it; no need for state-controller dependency. |
| `Get_FootPlacementPlantSettings` | chooses Stop vs normal plant settings | Existing Project_J getter/path retained. |
| `Get_FootPlacementInterpolationSettings` | chooses Stop vs normal interpolation settings | Existing Project_J getter/path retained. |

### E. Regular Motion Matching functions reviewed

| GASP function | Exact reviewed behavior | Project_J status |
|---|---|---|
| `Update_MotionMatching` | evaluates `CHT_PoseSearchDatabases`, writes PSD array to MM node, passes interrupt mode | Implemented equivalent through C++ data/profile policy. |
| `Get_MMInterruptMode` | database-change interrupt only when core movement/gait/stance/mode state changed | Implemented semantic interrupt policy. |
| `Update_MotionMatching_PostSelection` | caches selected animation/database/tags; applies selected blend settings | Partial: trace/selected PSD state exists; selected per-asset blend overrides are future data work. |
| `Get_MMBlendTime` | 0.5 when landing/normal air, 0.15 when upward jump, otherwise 0.5 | Project_J uses profile/phase blend settings; do not treat as one-shot-completion solution. |
| `Get_MMNotifyRecencyTimeOut` | gait-specific notify recency timeout | Implemented/available as policy setting. |
| `Get_PoseHistoryReference` | converts PoseHistory node reference and returns it to MM/Blueprint Motion Match | ABP final PoseHistory connection retained. |
| Motion Matching node blend settings | blend time, profile, search history, play-rate range, notify filtering, continuing-pose reset and search flags | Current Project_J regular MM node settings are retained/tuned separately; not assumed equal merely because GASP values exist. |
| Internal MM Blend Stack graph | per-selected-animation Orientation Warping, Reset Root Transform, Steering | Do not import now; it assumes GASP ORB/root-motion workflow. |

### F. Experimental State Controller / Blend Stack functions reviewed

| GASP function or rule | Exact reviewed behavior | Project_J status |
|---|---|---|
| `SetBlendStackAnimFromChooser` | state entry sets State Machine State, caches previous inputs, resets notify flags, evaluates animation chooser, optionally performs a single Motion Match, writes asset/loop/start/blend/profile/tags, optionally force blends | Implemented as C++ chooser snapshot + direct Blend Stack architecture; no per-frame continuous one-shot MM. |
| `IsAnimationAlmostComplete` | non-loop Blend Stack asset time remaining <= 0.75 and non-loop | Native remaining/almost-complete API exists; no universal .75 copy. |
| `OnStateEntry_TransitionToLocomotion` | caches target rotation and calls chooser with force blend | Logical state exists; target-rotation-specific logic deferred. |
| `OnUpdate_TransitionToLocomotion` | RInterp target rotation for rotational break detection | Deferred with rotation-break/Pivot data. |
| `OnStateEntry_TransitionToIdle` | calls chooser with force blend | Logical state/direct table exists. |
| `OnStateEntry_TransitionToInAir` | calls chooser with force blend | State exists; direct assets/rows pending. |
| `OnStateEntry_InAirLoop` | calls chooser without force blend | State exists; direct assets/rows pending. |
| `OnStateEntry_IdleLoop` | calls chooser without force blend | State exists; OTM idle row authored. |
| `OnStateEntry_IdleBreak` | calls chooser with force blend | Deferred; optional idle variation only. |
| Idle -> TransitionToLocomotion | `IsMoving` | Project_J StateController rule uses WantsLocomotion. |
| Locomotion -> TransitionToIdle | `NOT IsMoving` | Project_J rule uses WantsIdle. |
| any -> InAir | MovementMode InAir | Project_J rule uses IsInAir. |
| Transition -> Loop automatic rule | asset almost complete AND state time > 0 | APIs exist; ABP rule must be verified/used. |
| Transition -> Loop no-valid/loop escape | NoValidAnim OR BlendStackInputs.Loop | APIs available; ABP rule must be verified/used. |
| Early transition notify rule | notify bool from BP NotifyState EarlyTransition | Native notify counterpart exists; asset authoring pending. |
| Loco state-changed re-entry | direction OR stance OR gait changed and state time > 0 | Native semantic getter exists; ABP rule tuning pending. |
| Loco Pivot re-entry | IsPivoting; only delays retry while beginning Pivot/Start is playing | Deferred combat-Strafe-only. |
| rotation-break re-entry | target yaw delta > 60 while Start/Pivot younger than .5 and tagged | Deferred combat-Strafe-only. |
| Idle state-changed re-entry | stance change and state time > 0 | Native getter exists; currently inert because only Stand. |
| Idle Loop -> Idle Break | idle state time > 3 seconds | Optional/deferred; not a core locomotion requirement. |
| Idle TIP re-entry | ShouldTurnInPlace and tag/time rules | Deferred as dedicated TIP work. |

### G. GASP Choosers and tables reviewed

| GASP asset/table | Reviewed inputs / output | Project_J mapping |
|---|---|---|
| `CHT_PoseSearchDatabases` | picks Dense/Sparse/ExtremelySparse table according to MM DB LOD | Project_J profile/DA MM selection; no need to duplicate table names. |
| `CHT_PoseSearchDatabases_Dense` | MovementMode + Stance + MovementState + Gait -> PSD family | Project_J DA database families and phase selection. |
| Dense Stand Idles | Speed2D, JustLanded Light/Heavy, ShouldTurnInPlace -> specific PSD | landing/TIP direct split pending; regular PSD policy exists. |
| Dense Stand Runs | IsStarting, IsPivoting, JustTraversed, land flags, spin transition -> PSD families | Start/pivot/landing policy exists; direct authored branch staged. |
| Dense Stand Sprint | starts/loops/pivots/light/heavy lands | sprint data available but direct rows deferred. |
| Dense InAir | TimeToLand / traversal flag -> Jump PSDs | regular air PSD selection exists; traversal excluded. |
| `CHT_CMCCharacterAnimations` | State Machine State + Stance + Gait -> high-level animation chooser | `CHT_Player_StateControllerAnimations`. |
| Stand Stopped | Idle loops, transitions, TIP, lands, walk/run/sprint stops | Project_J OTM Idle/Run Stop first rows; others pending. |
| Stand Runs | direction child choosers F/B/LL/LR/RL/RR | Project_J OTM Forward first; Strafe directions pending. |
| Stand Sprint | starts/reface/pivot/lands and `S_ChooserOutputs` | Project_J direct sprint pending. |
| InAir animations | fall loops / directional jumps | Project_J direct air pending; regular MM fallback active. |
| `S_ChooserOutputs` | StartTime, UseMM, MMCostLimit, BlendTime, BlendProfile, Tags | Native `FProject_JStateControllerChooserOutput`. |

### H. Curves, notifies and asset annotations reviewed

| Asset data | GASP/reviewed use | Project_J decision |
|---|---|---|
| `contact_l`, `contact_r` | Foot Placement contact/plant behavior | Existing Foot Placement path; do not use as generic state exit. |
| `movedata_speed` / `MoveData_Speed` | dynamic play-rate scaling | Deferred until all target assets have coherent data. |
| `enable_warping` / `Enable_Warping` | gate Orientation Warping during straight-motion portions | Existing/possible presentation aid; do not make locomotion state gate. |
| `phase` | Pose History/MM phase channel | Project_J PSS/PSD quality work later. |
| `steeringtargettime` | timing for GASP Steering | Not used while Steering is deferred. |
| `MinDynamicPlayRate`, `MaxDynamicPlayRate` | clamp curve-driven dynamic play rate | Deferred. |
| `PoseSearchBlockTransition` notify/state | blocks premature pose-search transition sections | Asset-side authored behavior; do not assume Project_J parity without inspection. |
| `PoseSearchExcludeFromDatabase` notify/state | excludes clips/sections from searchable database | Asset/PSD authoring concern; no generic C++ replacement. |
| `BP_NotifyState_EarlyTransition` | controlled early State Controller exit | native Project_J equivalent exists, not authored yet. |
| Foley/footstep/scuff notifies | audio/gameplay presentation | outside current state-selection refactor. |
| Project_JLocomotionAnimNotify | older Project_J notify seen on assets | Do not treat it as the new early-transition contract unless its C++ implementation is verified. |

### I. Explicitly unreviewed GASP areas

The following were not supplied in enough detail and must not be claimed as
implemented: complete GASP Character BP interface implementation, every
Gameplay Tag source, every chooser child table, all traversal/mantle logic,
all camera rig behavior, exact Offset Root Bone tuning, complete foot-forward
bias source, all Motion Warping targets, and all animation-asset curves/notify
values. Request a screenshot/code sample before copying any of them.

## Current diagnostics and their interpretation

Commands in PIE:

```text
p.ProjectJ.MMTransitionDebug 1
DumpMotionMatchingTransitionTrace

p.ProjectJ.MMNetDebug 1
DumpMotionMatchingTrace
DumpMMOProfilingSnapshot
```

Recent trace correctly showed:

```text
State=2 ... MMMoving=true Input=true FutureSpeed=17.1 ... Run_Start
State=4 ... MMMoving=false Input=false FutureSpeed=0.0 ... Run_Stop
```

This proves the C++ selector gets Start then immediate Stop when input is
released. It does **not** prove visual Blend Stack elapsed time/weight. If
Start/Stop still looks short, diagnose ABP in this order:

1. Direct Blend Stack output actually feeds the cached `Locomotion` source.
2. Direct blend alpha is one while `HasSelectedAnimation` is true.
3. `Transition to Locomotion -> Locomotion Loop`, `Transition to Idle -> Idle
   Loop`, and `Transition to In Air -> In Air Loop` use:
   `SelectedAnimationAlmostComplete AND Current State Time > 0`.
4. Include the `No Valid Anim OR SelectedAnimationShouldLoop` escape.
5. Do not use `WantsLocomotion`, `WantsIdle`, raw phase, or broad duration as
   the transition-to-loop exit condition.
6. Ensure Start/Stop direct entries pass Loop=false; only loop entries pass
   Loop=true.
7. Ensure a later Slot/montage/layer path is not replacing the final cached
   locomotion pose.

The next desirable debug enhancement (only if still needed) is C++/ABP-side
diagnostics for the direct Blend Stack's actual current animation, elapsed
time, remaining time and blend weight. Do not mistake chooser selection trace
for that data.

## Next implementation order

1. Verify / correct the three logical transition-to-loop state-machine rules
   above and retest OTM Start -> Loop -> Stop -> Idle.
2. Add direct chooser metadata (`BlendTime`, tags, possibly asset-specific
   `StartTime`) per authored OTM asset; do not hardcode a universal percent or
   GASP 0.75 second constant.
3. If visual completion still fails, add direct Blend Stack playback diagnostic
   before changing movement policy.
4. Add combat Strafe direct rows only after its directional/start/stop assets
   and metadata are inventoried. Add Pivot only to Strafe and only after tags
   and selection conditions exist.
5. Add InAir and landing direct rows after asset contracts are selected.
6. Later, separately design TIP.

## Planned path from this work to TIP

TIP is intentionally postponed until the locomotion ownership boundary is
stable. The current work is a prerequisite, not a detour:

| Stage | Outcome | Why it is required before TIP |
|---|---|---|
| 1. Current regular-MM refactor | C++ owns phase, intent, rotation mode, trajectory and network snapshots | TIP decisions cannot compete with hidden ABP gameplay logic. |
| 2. Current direct State Controller rollout | Authored Start/Stop state has a stable visual owner and transition completion path | TIP must also play authored one-shot turns without continuous pose re-search. |
| 3. Combat Strafe data completion | Strafe-only direction/Pivot policy and assets are isolated from OTM | Prevents a combat turn feature from breaking noncombat Orient-to-Movement. |
| 4. Dedicated TIP implementation | `Idle -> TurnInPlace -> TurnInPlaceRecovery -> Idle/re-entry` | Establishes one visual-yaw owner and one clear interruption policy. |
| 5. Multiplayer validation | Autonomous/simulated proxy visual policies are verified | Ensures no local-control-yaw or root-offset divergence on remote clients. |

The intended TIP design, when started later:

- enters only during **combat Strafe idle**; OTM stays completely independent;
- uses a logical state machine/direct authored turn asset path, not a default
  Motion Matching node connected to a 90-degree-only TIP PSD;
- has one explicit visual rotation owner: RootYawOffset / Rotate Root Bone or
  an equivalent Offset Root Bone policy, never Steering + CharacterMovement +
  ORB at the same time;
- uses C++ game-thread snapshots for Control yaw, visual root yaw delta, idle
  eligibility, interruption priority and proxy policy;
- lets asset curves/notifies communicate authored turn progress, contact and
  safe re-entry timing; gameplay state remains C++-owned;
- starts with 90-degree L/R assets and data-driven thresholds, then expands to
  45/135/180 assets without changing the core ownership model.

Explicitly prohibited TIP approaches:

- TIP PSD continuously re-searched by normal Motion Matching;
- selecting an asset at a middle timestamp and repeatedly stacking blends;
- Root Motion from Everything;
- changing OTM behavior to make combat TIP work;
- multiple concurrent rotation owners.

## MMO/network/performance policy

- Locomotion C++ state is authoritative/predicted according to the existing
  CharacterMovement and gameplay-tag policy. ABP state never replicates.
- Autonomous proxy can use local input/control context on the game thread;
  simulated proxy uses replicated movement/combat snapshots and remote visual
  policy. It must never derive movement intent from a local Controller.
- Motion Matching database/chooser selection is narrowed by C++ semantic
  context, avoiding an unnecessary all-assets search every frame.
- Direct State Controller entries are selected only when their semantic context
  changes. They are not a per-frame Blend Stack accumulation mechanism.
- ABP worker threads consume snapshots only. No direct Actor cast, Controller,
  component, world, timer, montage state or Gameplay Tag query is permitted in
  a thread-safe function.
- Distant actor update/MM frequency and relevance tiers remain C++ policy;
  presentation chooser output must be deterministic from the snapshot at a
  given frame.

## Documents to read first in the next chat

- `docs/MotionMatching_Locomotion_Refactor.md` — full architecture and updated
  ABP wiring notes.
- `docs/MotionMatchingNextSteps.md` — existing staged roadmap.
- This handoff document.

## Safety / verification

- Never edit actual UE assets through tools; user performs editor actions.
- C++ changes must use `apply_patch` and then build with UnrealBuildTool.
- Keep unrelated user worktree changes untouched.
- Current latest C++ change (immediate opposite intent replacement) was built
  successfully before this handoff was written.
