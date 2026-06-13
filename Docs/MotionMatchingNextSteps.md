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

If this area is rewritten, validate with two-client PIE from the observing client and compare:

- straight run immediately after start,
- sustained turn,
- return to straight run after the turn,
- sprint,
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
