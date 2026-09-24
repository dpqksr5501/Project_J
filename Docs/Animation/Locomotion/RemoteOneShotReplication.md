# Remote Locomotion One-Shot Replication

## Scope

Player Start, Stop, Fall Off, and Land are short presentation boundaries. Continuous
movement, direction, trajectory, and Motion Matching cycle selection remain locally
derived from CharacterMovement. NPCs do not opt into this player-only component unless
they explicitly use the player Motion Matching presentation stack.

## Semantic snapshot contract

`UProject_JReplicatedAnimEventComponent` owns a compact replicated recovery snapshot and
also sends the latest snapshot through an unreliable multicast for low visual latency.
The replicated property is the recovery path if the multicast is dropped.

Each movement or air boundary receives a monotonic server event order. A simulated proxy
applies coalesced boundaries in that order and ignores an older boundary received after a
newer one. This is important for landing: a delayed MoveStart which occurred before the
physical landing must not cancel the newer Land one-shot.

- MoveStart stores the gait at the input edge.
- MoveStop stores the gait immediately before input and Sprint are released.
- LandingStart is emitted by the server's physical `Landed` boundary and stores event
  time, impact speed, moving/standing, Run/Sprint, and light/heavy semantics.
- LandingCancel changes the current landing revision instead of acting as an unpaired
  fire-and-forget notification.
- The locomotion component increments a local `LandingPresentationRevision` at every
  physical landing boundary. The AnimInstance includes that epoch in its State Controller
  cache key and resets the Land playback hold for a new epoch. Consecutive Land events
  therefore restart the Blend Stack even when they select the same foot/asset.
- Direct State Controller chooser columns are published from the current immutable
  animation snapshot immediately before an event-driven chooser evaluation. They do not
  wait for the separately throttled regular PSD search; remote URO frames therefore cannot
  select a stale Heavy row or `None` from the preceding landing context.
- Land gait is resolved once from the replicated landing-edge facts: not moving -> Walk,
  moving/non-Sprint -> Run, moving/Sprint -> Sprint. The State Controller holds this value
  for the complete one-shot instead of following later input, sprint-tag, or velocity changes.
- `CHT_Player_Land` must not add a live `GetThreadSafeGroundSpeed` Float Range on top of that
  latched gait. A simulated proxy can briefly cross an authored 500 uu/s boundary because of
  movement replication and smoothing, producing an empty intersection such as `Gait=Run`
  with a Sprint-only speed row. The OTM table is authored from State + latched Gait + Foot +
  Heavy; the Strafe parent equivalently uses `LandWasMoving` + `LandWasSprinting`.
- Server event time lets a newly relevant proxy reject a landing older than its authored
  presentation duration.

The payload is cosmetic only. It never changes movement speed, collision, ability state,
or server gameplay authority.

## Start responsive exit

The local player can release a held Start immediately when control yaw or movement input
changes. A simulated proxy cannot read that local input. It compares replicated velocity
direction and Actor yaw against an immutable Start-entry reference instead. The AnimInstance
keeps this presentation reference for as long as the direct Start asset is held, even if the
remote locomotion component has already reached its maximum-speed `Locomotion` state. The
reference is not advanced every smoothing frame, so several small smoothed rotations still
produce the correct cumulative turn.

When the threshold is crossed, either the locomotion component's
`StartResponsiveExitRevision` or the held-Start presentation check releases the State
Controller playback hold and forces the regular trajectory-aware Cycle Motion Matching path.

## URO and distance budgets

Confirmed Start, Stop, Fall Off, and Land boundaries request a short URO bypass through
`UProject_JAnimationUpdateCoordinatorComponent`, the same coordinator used by confirmed
JumpStart. Replication components own only transport and semantic application; they do not
own skeletal-mesh optimization timers. The coordinator is non-replicated, non-ticking, and
restores the mesh URO state captured before the first overlapping urgent request. The
default window is 0.10 seconds. Do not permanently disable URO for remote players.

`UProject_JLocomotionProfile::IsDataValid` recursively follows the configured State
Controller table and its referenced Choosers. A Land table that binds a Float Range to
`GetThreadSafeGroundSpeed` is invalid: Land row selection must use the latched semantic
gait. This is an editor/commandlet validation only; it neither edits assets nor adds a
runtime animation tick.

Far-distance Start/Stop suppression and Land suppression are separate policies. Land is
rare and remains enabled by default. This prevents the old Start/Stop budget switch from
silently clearing all Land chooser rows.

## Diagnostics

Enable:

```text
p.ProjectJ.MMTransitionDebug 1
```

Relevant event-driven records:

- `RemoteAnimSemantic Actor=... Type=MoveStart|MoveStop`: actor, server order, movement sequence, edge gait,
  and event age.
- `RemoteAnimSemantic Actor=... Type=LandingStart|LandingCancel`: actor, server order, landing sequence and
  revision, moving/Sprint/heavy semantics, impact speed, and age.
- `StateControllerResponsiveStartExit`: responsive revision, held presentation state, and
  whether the held Start was cancelled.
- `StateControllerRemoteStartTurnExit`: cumulative Actor/velocity direction deltas used to
  release a Start that outlived the remote component's semantic Start state.
- `RemoteAnimSemanticDrop`: an out-of-order multicast/property recovery snapshot was
  rejected, including its server order and the last accepted order.
- `StateControllerChooser Actor=... LandEpoch=... ForceBlend=...`: selected asset, physical
  landing boundary and Blend Stack restart pulse together with the chooser context.

The former frame-by-frame `StateControllerLandDiag` record was removed. It duplicated
unchanged data every animation update and obscured the boundary which actually caused the
selection.

## Required network regression matrix

Use a two-client PIE session and observe the autonomous pawn from the other client.

1. Noncombat and combat Run/Sprint Start, then change WASD direction and camera yaw during
   Start. The observer must enter Cycle without waiting for the Start asset end.
2. Release movement and Sprint on the same frame and on adjacent frames. Sprint Stop must
   remain Sprint after replicated velocity falls below the Stop entry threshold.
3. Test standing, Run, and Sprint light/heavy landings while holding, releasing, and
   re-pressing movement in air. Include sustained maximum Run speed and speeds immediately
   below/above its old 500 uu/s chooser boundary; all must select the semantic Run/Sprint row.
4. Repeat with packet lag/loss, skeletal mesh URO enabled, and near/mid/far significance
   tiers.
5. Verify a newly relevant pawn does not replay an expired landing snapshot.
