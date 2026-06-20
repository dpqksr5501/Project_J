# Motion Matching Notes

Project J uses a C++ locomotion state component, a thread-safe animation snapshot, and Pose Search databases selected from the character motion matching asset set. The player path is the primary Motion Matching path; NPC animation should stay on cheaper non-MM paths unless a later profiling pass proves otherwise.

## Current Architecture

- `UProject_JLocomotionAnimStateComponent` reconstructs ground, air, landing, start, stop, gait, and phase context.
- `UProject_JCharacterAnimInstance` publishes a thread-safe snapshot to the native animation proxy.
- `UProject_JMotionMatchingAssetSet` owns run, sprint, start, remote start, stop, turn, jump, fall, and landing databases.
- `UProject_JMotionMatchingTrajectoryComponent` owns Motion Matching trajectory samples used by the Pose Search query.
- Animation budget settings throttle database selection and expensive update work, but the Pose Search trajectory input must remain current every animation update.

## Locomotion-Only Policy

Motion Matching is currently a locomotion system only. Stylish MMORPG skills, including mouse button combinations and modifier inputs, should stay in the input, GAS ability, montage, slot, and gameplay tag layers rather than expanding the locomotion Pose Search query.

Keep this boundary unless there is a measured reason to change it:

- Locomotion Motion Matching chooses the base movement pose.
- Skills and attacks activate through ability input mapping and gameplay tags.
- Skill animation playback should use montages, slots, overlays, or ability-owned animation policy.
- Root-motion or forced-facing skills should temporarily own their montage/pose layer instead of changing the base run-cycle database.
- Future lock-on, strafe, or aim-offset movement should add explicit locomotion policy before it opts out of straight-running trajectory repair.

## Remote SimulatedProxy Fix

The remote running issue was not caused by bad Run_Arc, Prism, or Run_Loop assets. It was caused by the simulated proxy trajectory query:

- The visible debug path positions could look straight.
- After a turn, current velocity, actor yaw, phase, and selected database could all return to a straight run state.
- Future trajectory sample rotations could still encode the previous turn.
- Pose Search then saw an arc-like query and selected Prism/Arc candidates instead of `Run_Loop`.

The fix keeps remote running queries semantically straight after network-smoothed turns:

- `TransformTrajectory` is assigned to the native Pose Search history collector every animation update, independent of database selection throttling.
- Simulated proxies skip acceleration-stop trajectory resets by default because local input acceleration is not reliable for replicated characters.
- Simulated proxy trajectory smoothing is CVar-gated; rotation smoothing stays disabled by default.
- `RepairRemoteTrajectoryFacing` is enabled by default for simulated proxies when:
  - speed is above `p.ProjectJ.MM.RepairRemoteTrajectoryFacingMinSpeed`,
  - actor yaw and velocity yaw are within `p.ProjectJ.MM.RepairRemoteTrajectoryFacingMaxYawDelta`.
- The repair preserves the existing trajectory facing offset and only adjusts current/predicted samples. Historical samples are left intact so actual turns still match turn/arc candidates.

Expected behavior:

- Straight remote running selects `Run_Loop`.
- Actual turning can select Arc/Prism.
- Returning to straight remote running returns to `Run_Loop`.
- Sprint remains unchanged.
- Future strafe, lock-on, or aim-offset locomotion should add an explicit opt-out before it intentionally allows facing to differ from movement.

## Remote JumpStart Latency and URO Guard

Remote moving jumps have two separate latency hazards. Both must remain covered when refactoring animation optimization or Motion Matching.

### Observed failure

In two-client PIE, the locally controlled character jumped immediately, but the observing client briefly kept the remote character's Run/Sprint pose. Standing jumps and repeated jumps could look correct, making the issue easiest to reproduce on the first jump while walking or sprinting.

Diagnostic logging with `p.ProjectJ.MM.DebugJumpLatency 1` confirmed:

- replication and `HandleConfirmedRemoteJump` arrived without meaningful server-time age,
- `PSD_JumpStart` was selected correctly,
- the first JumpStart Motion Matching player initially blended against the previous Run/Sprint player,
- skeletal mesh Update Rate Optimization could defer the remote AnimGraph update by several render frames.

This was not caused by:

- handover retry, timeout, payload cleanup, or transport logic,
- sprint server-authority validation,
- the `GameState == nullptr` jump-age fallback when logs report `Age=0.000`.

### Required runtime behavior

- `UProject_JReplicatedJumpStateComponent` temporarily disables skeletal mesh URO for a visible simulated proxy when a confirmed jump arrives.
- The urgent update window is configured by `MotionMatchingSearchPolicy.RemoteJumpUrgentAnimationUpdateDuration` and defaults to `0.10` seconds, after which the previous URO setting is restored.
- Motion Matching BlendStack state is not modified for JumpStart. The same authored transition used by other locomotion changes remains responsible for visual continuity.
- Local, autonomous, and simulated-proxy characters therefore use the same Motion Matching blend behavior once their AnimGraph updates.

Do not permanently disable URO to solve this issue. URO remains part of the MMORPG animation budget; only latency-sensitive replicated transitions should receive a short event-driven exception.

### Refactor guardrails

When changing player mesh URO, significance tiers, animation budgets, replicated jump state, or Motion Matching BlendStack handling:

- preserve the short urgent animation update started by confirmed remote jumps,
- preserve restoration of the mesh's previous URO value,
- do not manipulate `FBlendStackAnimPlayer`, `AnimPlayers`, or JumpStart blend progress from project code,
- do not delete the previous player to force full weight; that produces an abrupt remote pose cut,
- let the authored Motion Matching transition handle pose continuity after URO allows the graph to update,
- do not move this exception into handover or sprint policy code; those systems do not own animation presentation timing.

### Mandatory two-client regression test

From the observing client, verify another client performing:

- standing jump,
- walking jump,
- sprinting jump,
- repeated jump after landing.

For a diagnostic run, enable:

```text
p.ProjectJ.MM.DebugJumpLatency 1
```

Expected sequence:

```text
MulticastReceive
UrgentAnimUpdateBegin
AnimStateEnter
UrgentAnimUpdateEnd
```

For deeper BlendStack inspection, enable `p.ProjectJ.MM.DebugInAirBlendStack 1` separately. JumpStart should use the normal authored blend and should no longer be delayed by skipped URO update frames.

## Useful CVars

- `p.ProjectJ.MM.RepairRemoteTrajectoryFacing` defaults to `1`.
- `p.ProjectJ.MM.RepairRemoteTrajectoryFacingMinSpeed` defaults to `80`.
- `p.ProjectJ.MM.RepairRemoteTrajectoryFacingMaxYawDelta` defaults to `35`.
- `p.ProjectJ.MM.DisableRemoteAccelReset` defaults to `1`.
- `p.ProjectJ.MM.SmoothRemoteTrajectoryPosition` defaults to `0`.
- `p.ProjectJ.MM.SmoothRemoteTrajectoryRotation` defaults to `0`.
- `p.ProjectJ.MM.DebugRemoteTrajectory` defaults to `0` and logs `PosYaw`, `FaceYaw`, and their delta for simulated proxy trajectory samples.

## Refactor Guardrails

Keep these invariants when refactoring the Motion Matching pipeline:

- Do not treat trajectory debug path positions as the whole query. Pose Search also consumes trajectory rotations/facing, pose history, continuing pose, phase, and selected database.
- Do not move `NativePoseHistoryNode.TransformTrajectory = ThreadSafeData.Movement.Trajectory` back under database throttling. Database selection may be throttled; query trajectory input must stay fresh every animation update.
- Do not replace `RepairRemoteTrajectoryFacing` with `Rotation = Velocity.Rotation()` or a hard-coded world offset. The project schema has a stable sample-facing convention; the repair must preserve the current present-sample `MoveYaw -> FaceYaw` offset.
- Do not repair historical trajectory samples. Past samples describe the turn that already happened and help Pose Search select turn/arc clips during actual turns.
- Do not silently reuse the straight-running repair for future locomotion modes that intentionally face away from movement. Add an explicit policy check first.
- Do not re-enable simulated proxy trajectory rotation smoothing by default. Local-space smoothing can delay or bend future sample rotations after network-smoothed turns.
- Do not use simulated proxy `GetCurrentAcceleration()` as a reliable local input stop signal. Remote characters can have valid replicated velocity while acceleration flickers.
- Do not remove the event-driven remote JumpStart URO exception merely because ordinary MM updates appear correct. URO scheduling and MM BlendStack convergence are separate latency layers.
- Do not permanently disable player mesh URO as a jump fix. Preserve the short exception and restore the previous setting.

If this area is rewritten, validate with two-client PIE from the observing client and compare:

- straight run immediately after start,
- sustained turn,
- return to straight run after the turn,
- sprint,
- standing, walking, sprinting, and repeated remote jumps,
- future strafe, lock-on, or aim-offset locomotion once those modes exist.

## Near Term Checks

- Test two-client PIE from the observing client:
  - idle to W-only straight run,
  - mouse/WASD turn,
  - return to straight run.
- Confirm `PSD_Run_Cycle` selects `Run_Loop` during straight segments and only uses Arc/Prism during actual turn-shaped queries.
- Repeat with sprint to confirm sprint databases remain stable.
- When future strafe, lock-on, or aim-offset locomotion exists, verify it opts out before intentional facing offsets are introduced.

## Do Not Do

- Do not remove Run_Arc or Prism clips to hide query problems.
- Do not force `Run_Loop` globally for simulated proxies.
- Do not apply the straight-running trajectory facing repair to future locomotion modes that intentionally decouple facing from movement.
- Do not throttle trajectory input updates together with database selection updates.
