# GASP BranchIn + TIP Handoff (2026-08-04)

> **2026-08-24 correction:** This is a historical BranchIn/direct-one-shot
> exploration, not the current moving Pivot authority. It predates confirmation
> that GASP has separate regular-MM and Experimental-State-Machine paths. In
> particular, its `UseMM=false` moving Pivot recommendation must not be used to
> activate Project_J's disabled Pivot chooser row. See
> [`GASP_Pivot_Architecture_Correction_2026-08-24.md`](GASP_Pivot_Architecture_Correction_2026-08-24.md).

> **Superseding design update (2026-08-05):** Project_J will keep all current
> State Controller one-shots on the direct path. `UseMM=false` is the intended
> policy for Start, Stop, Pivot, Jump, FallOff, Land, and InAirLoop. BranchIn
> is deferred rather than used as the next locomotion milestone.
>
> **TIP correction (2026-08-05, later decision):** Project_J will implement a
> dedicated Combat Strafe **Idle Turn In Place** family. It is separate from
> GASP's moving Start/Pivot rotation-break reference in section 10A. Idle camera
> rotation leads the body; a direct 90/180-degree TIP one-shot is selected once
> the yaw error crosses its threshold, and may reselect when its quantized
> left/right or 90/180 target changes.

## 1. Goal of the next task

Move Project_J from the current **Chooser-direct one-shot** policy toward a selective
GASP-style **PoseSearchBranchIn handoff** policy, then design and implement Turn In
Place (TIP).

Do not convert every animation at once. Stabilize one representative family first
(recommended: Run Start or Pivot), then expand only when its authored PSD, notify,
and runtime trace are proven correct.

## 2. Current locomotion architecture

Project_J has two presentation modes that must remain separate:

| Mode | Rotation | Continuous locomotion | One-shots |
|---|---|---|---|
| Non-combat | Orient To Movement (OTM) | regular Motion Matching from DA/PSD | State Controller + Chooser |
| Combat | Strafe | regular Motion Matching from DA/PSD, with directional strafe support | State Controller + hierarchical Chooser |

Combat strafe supports forward/back/left/right and diagonal directions. Sprint is
forward-only in gameplay intent (W, W+A, W+D). The one-shot foot (`Left`, `Right`,
or `None`) is resolved in native C++ from contact curves and exposed to Chooser.

### Ownership rule

```text
Cycle / Turn Redirect       -> standard Motion Matching node / active PSD
State Controller one-shots  -> State Controller Blend Stack
```

The one-shot set currently includes Start, Stop, Pivot, Jump, FallOff, Land, and
InAirLoop. `InAirLoop` is intentionally a direct looping State Controller pose,
not a regular ground Motion Matching selection.

## 3. AnimGraph topology already authored

The master ABP has:

1. a normal Motion Matching route driven by `Get Current Active Pose Search Database Thread Safe`;
2. a State Controller `Blend Stack` that receives the selected animation, start time,
   loop flag, blend time, and blend profile from C++;
3. an existing final `Blend Poses by Bool` that switches between those routes;
4. the normal downstream combat upper-body layer, slots, aim offset, foot placement,
   leg IK, Offset Root Bone, and Pose History chain.

**Do not swap the final bool node's True/False wires.** A previous experiment caused
a T-pose. The existing authored wiring must be treated as the baseline and inspected
in PIE rather than guessed from node labels.

### State Controller Blend Stack inner graph

The user added this combat strafe graph manually:

```text
Blend Stack Input
  -> Local To Component
  -> Orientation Warping
  -> Component To Local
  -> Output
```

It uses:

- `GetThreadSafeStateControllerCombatStrafeOrientationWarpingAlpha`
- `GetThreadSafeStateControllerCombatStrafeOrientationWarpingAngle`
- current Blend Stack animation asset/time
- the animation curve `enable_warping` as a per-asset alpha gate

This is for directional combat strafe one-shots; it is not a global GASP Steering,
Offset Root Bone, or TIP implementation. Do not add a second global Orientation
Warping node without first checking for double-warping.

## 4. Relevant C++ implementation

Primary file:

`Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp`

Key points:

- `EvaluateStateControllerAnimationChooserOnGameThread` begins around line 1610.
- It evaluates a parent Chooser recursively until it resolves an `UAnimationAsset`.
- The chosen asset plus `FProject_JStateControllerChooserOutput` are cached and fed
  to the State Controller Blend Stack.
- `OneShot.bShouldOverrideMotionMatching` is computed around lines 1790-1797.
  It is true when a State Controller direct transition has a selected animation,
  including `InAirLoop`.
- InAirLoop explicitly changes local `ChooserOutput.bUseMotionMatch` to `false`.
- C++ getters for combat Orientation Warping are around lines 960-972.

`FProject_JStateControllerChooserOutput` is the Chooser output contract. **Keep it.**
It carries Start Time, Blend Time, Blend Profile, tags, and other direct Blend Stack
data. Do not remove the entire struct merely because `bUseMotionMatch` may be
false for a given policy.

Other relevant files:

- `Source/Project_JCharacter/Public/Animation/Project_JCharacterAnimInstance.h`
- `Source/Project_JCharacter/Public/Animation/Project_JLocomotionProfile.h`
- `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent.cpp`
- `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentInput.cpp`
- `Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponentTransitions.cpp`
- `Source/Project_JCharacter/Public/Project_JLocomotionAnimStateComponent.h`

## 5. PoseSearchBranchIn: exact current behavior

`UseMM` on a State Controller Chooser row does **not** choose between the main
Motion Matching route and the State Controller Blend Stack route. It controls an
additional, one-asset `UPoseSearchLibrary::MotionMatch` query during State
Controller selection.

```text
Chooser selects a sequence
-> UseMM=true
-> C++ calls UPoseSearchLibrary::MotionMatch with that sequence
-> UE scans its PoseSearchBranchIn notify state(s)
-> a valid BranchIn Database can hand off to a pose search database
-> selected asset/time are returned to the State Controller Blend Stack
```

UE source behavior: `UPoseSearchLibrary::MotionMatch` inspects
`UAnimNotifyState_PoseSearchBranchIn` only during this explicit query. Ordinary
sequence playback does not itself invoke BranchIn.

### Diagnostic added in this task

`LogStateControllerPoseSearchBranchInDiagnostics` is at the top of
`Project_JCharacterAnimInstance.cpp`. With transition debug enabled it logs:

```text
StateControllerPoseSearchBranchIn
  State=<presentation state>
  Asset=<asset>
  UseMM=<bool>
  QueryWillInspectBranchIn=<bool>
  BranchInCount=<count>
  NullDatabaseCount=<count>
```

Observed real FallOff result:

```text
State=5 Asset=M_Neutral_Jump_F_Off_Run_Rfoot
UseMM=true QueryWillInspectBranchIn=true
BranchInCount=1 NullDatabaseCount=1
LogPoseSearch: Error: improperly setup UAnimNotifyState_PoseSearchBranchIn with null Database
```

This proves the notify was not harmless metadata: the explicit query reached a
BranchIn notify with no Database configured.

## 6. Current direct-policy recommendation (before BranchIn migration)

For the existing, stable Chooser-direct policy, use `UseMM=false` on direct
one-shot child-Chooser rows:

```text
Start / Stop / Pivot / Jump / FallOff / Land / InAirLoop = false
Cycle / Turn Redirect                              = main regular MM / PSD route
```

This did fix the previously broken InAirLoop: it now selects and plays
`M_Neutral_Jump_Loop_Fall` with `UseMM=false`.

This policy does **not** mean Motion Matching has been removed from the project.
The main continuous Cycle and Turn Redirect route remains MM.

## 7. Planned GASP-style BranchIn migration

> **2026-08-05 status:** deferred. This section is retained as future reference
> for a deliberately authored BranchIn experiment, not as the active plan.

The desired next architecture is selective, not blanket conversion.

### Good initial pilot

Use one of:

1. OTM Run Start / Reface Start;
2. Strafe Pivot.

These have the most potential benefit from a dynamic pose-search entry point.

### Keep direct initially

Keep Jump, FallOff, Land, Stop, and InAirLoop direct until their authored handoff
timing is stable. They already have explicit direction/foot/landing semantics and
will become harder to debug if BranchIn is introduced prematurely.

### Required setup for each BranchIn-enabled asset

1. Create or select an intentional PSD containing only compatible successor poses.
2. Open the source animation sequence.
3. Add/configure `AnimNotifyState_PoseSearchBranchIn`.
4. Set its **Database** to that PSD; it must never be None.
5. Put the notify at a meaningful handoff window, not blindly at frame zero.
6. Set only the matching child Chooser row's `UseMM=true`.
7. Preserve coherent Start Time, Blend Time, Blend Profile, and foot/direction
   selector values.
8. Test with the trace commands below.

### Design constraints

- The PSD must be authored for the same skeleton and compatible locomotion semantics.
- BranchIn is animation/cosmetic only; it must not become gameplay-authoritative.
- Do not allow a BranchIn PSD to contain unrelated jump/land/fall/upper-body poses.
- Avoid a handoff that changes root-motion ownership or applies Orientation Warping
  twice.
- Confirm Pose History is discoverable by `UPoseSearchLibrary::FindPoseHistoryNode`.
- Keep OTM and Strafe databases isolated unless a deliberate cross-mode handoff is
  designed and tested.
- Verify both left/right foot variations and network proxy behavior.

## 8. FallOff -> InAirLoop issue still open

After setting FallOff `UseMM=false`, InAirLoop appears correctly. There is still a
brief pause just before FallOff switches to InAirLoop.

Observed log:

```text
FallOff Asset Length=1.633, Start=0.500
StateControllerExitHold ... Elapsed=0.658
State=6 Asset=M_Neutral_Jump_Loop_Fall UseMM=false
```

Cause: `FProject_JLocomotionProfile::ExperimentalFallOffMaxHoldTime` defaults to
`0.65f` (`Project_JLocomotionProfile.h`, line ~178). The C++ hold code clamps the
remaining playable FallOff length to that global maximum. Its effective direct
playback is therefore terminated at ~0.65 seconds even though the selected source
clip has ~1.133 seconds after StartTime.

Do not blindly remove the cap. That can retain long FallOff recovery tails and delay
air-loop/landing response. Preferred future design: add a per-row/per-asset
`AirLoopHandoffTime` (or equivalent) to the State Controller Chooser output, with
zero meaning "use authored remainder". The value should specify the exact authored
time that can blend to the chosen air loop.

## 9. Landing policy currently implemented

Landing tracks post-touchdown input so a player who keeps moving briefly after
landing and then releases can be routed to Stop rather than abruptly Idle.

Relevant setting:

- `LandingExitStopInputHoldTime = 0.08f`

There is a known candidate logic issue to verify before changing it: an early
`LandingInputCancelGraceTime` return may run before the "post-touchdown release
should Stop" branch. If testing again shows release inside the grace window skips
Stop, restructure the condition so a confirmed post-touchdown release bypasses the
grace return while redirect/new input still uses the grace protection.

## 10. TIP scope

> **2026-08-05 final scope:** add an idle-only Combat Strafe Turn In Place
> State Controller family. GASP rotation-break remains a reference for the
> reselect/cooldown pattern only; it does not replace Idle TIP.

TIP is explicitly deferred. Current desired behavior:

- In combat Strafe idle, rotating the camera alone turns the head/aim offset, not
  the body.
- If the body faces forward but the camera faces behind, pressing W causes body
  movement/turning.

Idle TIP is a separate direct State Controller/Chooser family, not moving Pivot
logic and not BranchIn. It uses `UseMM=false`, 90/180 left/right assets, an entry
threshold (initially 65 degrees), lower exit threshold (initially 30 degrees),
and quantized reselects only when left/right or 90/180 classification changes.
During an active local Idle TIP, controller-yaw rotation is temporarily released
so the authored root yaw owns the body turn. Movement, air/land, attack, dodge,
and hit-react immediately preempt TIP.

## 10A. Combat Strafe moving reorientation / rotation-break (2026-08-05)

GASP's relevant behavior is not an idle TIP. Its logical `Transition to
Locomotion` state can re-enter while an authored Start or Pivot is still playing
when the requested rotation changes rapidly. This preserves the authored
one-shot model while allowing the player to change direction again during its
early portion.

### Confirmed GASP reference behavior

```text
OnStateEntry_TransitionToLocomotion
  -> TargetRotationOnTransitionStart = TargetRotation
  -> SetBlendStackAnimFromChooser(..., ForceBlend=true)

OnUpdate_TransitionToLocomotion
  -> TargetRotationOnTransitionStart = RInterpTo(
       TargetRotationOnTransitionStart, TargetRotation, DeltaSeconds, 5.0)

Rotation break
  -> abs(DeltaRotator(TargetRotation, TargetRotationOnTransitionStart).Yaw) > 90
  -> CurrentStateTime > 0 and < 0.5 seconds
  -> current Blend Stack output tags contain Start or Pivot
  -> re-enter Transition to Locomotion and select a fresh chooser result
```

The interpolated cache means a slowly changing target keeps the current asset,
while a quick large change (for example a 90-degree Pivot immediately becoming
a 180-degree request) reselects. GASP obtains `TargetRotation` from its own
Steering / Offset Root Bone visual-rotation system; that visual-root ownership
must not be copied into Project_J.

### Project_J translation

Project_J must retain its existing ownership boundaries:

```text
Idle + camera rotation only
  -> no body turn; Aim Offset remains responsible

Combat Strafe Start/Pivot selected
  -> latch the selected trajectory/Strafe direction and selection time

During the early direct one-shot window
  -> compare current future-trajectory Strafe direction with the latched value
  -> require a meaningful angular/sector change, a cooldown, and Start/Pivot tag
  -> request a fresh direct State Controller chooser selection
  -> choose the new asset from current direction + previous direction + foot
```

This supports both a continued turn in the same direction and a reversal, but
does not run a query every frame. The initial tuning reference may be a 90-degree
break threshold, a 0.5-second early window, and an explicit cooldown; final
values must be verified in PIE with the Project_J eight-direction assets.

Do not use GASP `TargetRotation - RootTransformRotation` as the Project_J
selector. Project_J has no global Offset Root Bone rotation owner, uses no
root-motion-everything locomotion policy, and already has local State Controller
Blend Stack Orientation Warping for supported combat one-shots. The reselect
criterion is trajectory intent, not visual-root delta.

### Asset and graph constraints

- `CHT_Player_Strafe_Run_Pivot` is the direct reselect leaf: previous Strafe
  direction -> current Strafe direction -> one-shot foot -> animation asset.
- Keep `UseMM=false` for the direct Pivot rows under the active policy.
- Continuous Cycle and Turn Redirect remain regular Motion Matching work.
- Do not add global Steering, Offset Root Bone, or a second global Orientation
  Warping node. The existing State Controller Blend Stack warper must remain
  the only combat one-shot directional warp.
- Traversal/Motion-Warping montages are a separate future action system. They
  must not become a locomotion rotation owner or be coupled to this reselect
  mechanism.

Suggested future TIP design:

```text
Idle + camera/body yaw exceeds threshold + no movement input
-> choose left/right TIP asset or BranchIn PSD
-> direct State Controller / selective BranchIn playback
-> return to Strafe idle
```

Before implementation decide:

- yaw thresholds, hysteresis, cooldown, and cancel rule;
- 90/180 degree coverage and mirroring policy;
- whether TIP assets require `TurnInPlace` tags/curves;
- interaction with combat aim offset, upper-body slots, montages, and Offset Root Bone;
- whether moving Pivot and idle TIP share data or stay completely separate.

## 11. Debugging workflow

In PIE:

```text
p.ProjectJ.MMTransitionDebug 1
DumpMotionMatchingTransitionTrace
```

Interpretation:

- `StateControllerPoseSearchBranchIn ... UseMM=true ... NullDatabaseCount=1`
  means a real invalid BranchIn query is being attempted.
- `UseMM=false QueryWillInspectBranchIn=false` means the direct row is not invoking
  BranchIn even if the asset still contains the notify.
- `DumpMotionMatchingTransitionTrace` currently captures the regular Motion Matching
  node's Blend Stack. It does **not** enumerate the State Controller direct Blend
  Stack player, so an Idle entry in that dump does not prove the direct one-shot
  failed to be selected.

Useful extra ABP evidence: PIE debug screenshot of the final `Blend Poses by Bool`,
including its Active Value and the value from
`GetThreadSafeStateControllerShouldOverrideMotionMatching` during the problematic
state. Do not swap True/False wires without observed runtime evidence.

## 12. Build notes

Last successful build used direct UBT:

```powershell
& 'C:\Program Files\Epic Games\UE_5.8\Engine\Binaries\DotNET\UnrealBuildTool\UnrealBuildTool.exe' `
  Project_JEditor Win64 Development `
  'C:\Users\I\Documents\GitHub\Project_J\Project_J.uproject' `
  -WaitMutex -NoHotReload
```

It compiled successfully after replacing a UE 5.8-invalid include
`Animation/AnimNotifyEvent.h` with `Animation/AnimTypes.h` in
`Project_JCharacterAnimInstance.cpp`.

Important: direct `UnrealBuildTool.exe` is still .NET-based; it is not a guaranteed
fix for all `dotnet.exe` exception dialogs. Never abort a running build or start a
second build while UBT/dotnet/Editor/Live Coding/MSBuild/ShaderCompileWorker is
running. Diagnose the first UBT/compiler error, not only the final dialog.

## 13. Existing documentation

Read these before changing architecture:

- `Docs/CombatStrafe_Implementation_2026-08-04.md`
- `Docs/CombatStrafe_Handoff_2026-08-03.md`
- `Docs/CombatAnimationComposition.md`
- `Docs/MotionMatching_StateController_Handoff_2026-08-01.md`
- `Docs/GASP_ProjectJ_Locomotion_Parity.md`

## 14. Workspace caution

The worktree is intentionally dirty. Many `.uasset` edits are user-authored,
including ABP and Chooser tables. Preserve them; do not reset, checkout, or bulk
overwrite assets. Do not use Unreal MCP unless the user explicitly asks for it.
