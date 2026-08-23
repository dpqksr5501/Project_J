# Motion Matching Notes

Project J uses a C++ locomotion state component, a thread-safe animation snapshot, and Pose Search databases selected from the character motion matching asset set. The player path is the primary Motion Matching path; NPC animation should stay on cheaper non-MM paths unless a later profiling pass proves otherwise.

## Current Architecture

Remote player Start/Stop/Land boundaries now follow the semantic snapshot contract in
[RemoteOneShotReplication.md](RemoteOneShotReplication.md). Keep trajectory and continuous
cycle selection locally derived; add replicated fields only for sparse authored one-shot
boundaries whose edge semantics cannot be reconstructed after network smoothing.

- `UProject_JLocomotionAnimStateComponent` reconstructs ground, air, landing, start, stop, gait, and phase context.
- `UProject_JCharacterAnimInstance` publishes a thread-safe snapshot to the native animation proxy.
- `UProject_JMotionMatchingAssetSet` owns run, sprint, start, remote start, stop, turn, jump, fall, and landing databases.
- `UProject_JMotionMatchingTrajectoryComponent` owns Motion Matching trajectory samples used by the Pose Search query.
- Animation budget settings throttle database selection and expensive update work. Eligible local/visible players publish a current trajectory; hidden actors suspend generation and re-seed on visibility wake instead of advancing stale history.

## Trajectory ownership and scope

- `AProject_JPlayerCharacter` is the default trajectory owner. Ordinary field
  monsters and NPCs remain on cheaper non-MM animation paths and do not construct
  this component.
- A special NPC or boss may opt in, but its class must explicitly own the
  component, call `UpdateTrajectoryState` after its movement policy is applied,
  and participate in the same visibility budget.
- The engine example component's unconditional movement delegate is removed.
  Character Tick is the normal single generation entry; AnimInstance only copies
  the completed trajectory into thread-safe proxy data.
- Dedicated servers skip generation. Trajectory is animation presentation data,
  is reconstructed from local/replicated CharacterMovement state, and is never
  replicated as an array.
- Hidden non-local actors suspend generation. Their history is reset on
  visibility wake so off-screen displacement cannot become a false query path.
- Mounted players suspend the on-foot generator and reset on dismount.
- Recorded history is never rescaled when gait or maximum speed changes. Current
  CharacterMovement limits are applied to the next future prediction only.
- Generation revision, reset revision, reset reason, eligibility and snapshot age
  are available in diagnostics. Keep these when changing URO or significance
  policy so stale-data problems remain distinguishable from bad asset selection.

### Required PIE checks after trajectory policy changes

- local OTM and combat Strafe start, stop, run and sprint keep a current
  generation revision;
- an observing client sees straight run, actual turns, stop and restart without
  persistent Arc/Prism selection;
- a hidden remote actor stops advancing generation revision, then receives a
  `PresentationWake` reset and a fresh trajectory when visible again;
- mount and dismount suspend and re-seed the on-foot trajectory;
- a dedicated server reports no generated samples after component BeginPlay;
- rotation-mode and acceleration-stop resets increment reset revision without
  rewriting the historical samples published before the reset;
- network correction or teleport behavior is inspected before adding an
  automatic distance-based reset policy.

## Locomotion-Only Policy

Motion Matching is currently a locomotion system only. Stylish MMORPG skills, including mouse button combinations and modifier inputs, should stay in the input, GAS ability, montage, slot, and gameplay tag layers rather than expanding the locomotion Pose Search query.

Keep this boundary unless there is a measured reason to change it:

- Locomotion Motion Matching chooses the base movement pose.
- Skills and attacks activate through ability input mapping and gameplay tags.
- Skill animation playback should use montages, slots, overlays, or ability-owned animation policy.
- Root-motion or forced-facing skills should temporarily own their montage/pose layer instead of changing the base run-cycle database.
- Future lock-on, strafe, or aim-offset movement should add explicit locomotion policy before it opts out of straight-running trajectory repair.

## Combat Strafe Pose Search Authoring

Combat presentation must not create a second Pose Search Schema merely because an
upper-body weapon layer is active. Start by sharing the on-foot locomotion schema
with dedicated `PSD_Combat_Strafe_*` databases. Create a separate combat schema
only if its lower-body pose features or trajectory sampling layout intentionally
differs from the shared locomotion query.

Recommended initial schema for the third-person combat overlay:

- Keep the same skeleton, pose-history source, and trajectory sample times as the
  native `UProject_JMotionMatchingTrajectoryComponent`. Do not add offsets in a
  PSD that the trajectory component does not produce.
- Use trajectory position and facing samples at `-0.4`, `-0.2`, `+0.2`, `+0.4`,
  and `+0.6` seconds as a starting point, with a relative channel weight of `1.5`.
  If the existing shared schema uses different sample times, preserve those times.
- Sample only lower-body locomotion bones (`thigh_l/r`, `calf_l/r`, `foot_l/r`)
  with position and velocity features, with a relative channel weight of `1.0`.
  Do not index spine, hands, or weapon bones while the upper body is owned by
  Aim Offset, linked layers, and montages.
- Author and assign separate idle, start, cycle, stop, pivot, moving-turn, and
  turn-in-place strafe PSDs. The cycle database needs forward, backward, left,
  right, diagonal, and direction-change clips before it can provide reliable
  camera-facing combat.
- Create a `UProject_JMotionMatchingAssetSet` DA such as
  `DA_Player_Combat_Strafe`, then assign it to
  `CharacterAnimProfile -> CombatAnimProfile -> Combat Strafe Motion Matching
  Asset Set`. Fill its visible Idle, Run, Sprint, Jump, Fall, and Landing
  fields with combat PSDs using the same layout as `DA_Player_Locomotion`.
  The combat asset set is a complete, readable replacement while Strafe is
  active; no per-phase `DatabaseEntries` array setup is required.
- `DatabaseEntries` remains an advanced override only for a future lock-on,
  weapon-specific, or special locomotion context that cannot use the standard
  family layout.
- Player startup validation warns for each missing Strafe phase while the combat
  profile enables camera-facing rotation. The temporary OrientToMovement fallback
  remains available only to keep incomplete data sets playable during authoring.
- To enable the authored combat sprint set `CombatAnimProfile.bAllowSprintInCombat`
  to true. With `bRequireForwardInputForSprintInCombat=true` and
  `CombatSprintForwardInputThreshold=0.1`, sprint is permitted only for `W`,
  `W+A`, or `W+D`; holding Shift while moving only left/right/back cancels the
  predicted Sprint ability and returns to normal combat movement.
- Use PCAKDTree and retain the engine/default pose-pruning threshold initially.
  Search remains unthrottled for the local/near combatant; retain the project
  mid/far intervals (`0.033` / `0.083` seconds) for remote actors.

`AProject_JPlayerCharacter::AllowsStraightRunningTrajectoryRepair()` is the
policy boundary for networked trajectory repair. It returns false while combat
owns character yaw, preventing the simulated-proxy straight-running correction
from rewriting intentional strafe, lock-on, or forced-facing trajectories. Future
facing modes must extend this policy instead of disabling the global repair CVar.

Aim Offset snapshots use `ACharacter::GetBaseAimRotation()`. This keeps the
owning client's full controller aim and allows simulated proxies to consume the
engine-replicated remote view pitch without replicating raw camera transforms.

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

Earlier diagnostic captures confirmed:

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

JumpStart uses the normal authored blend and should no longer be delayed by
skipped URO update frames.

## Useful CVars

- `p.ProjectJ.MM.RepairRemoteTrajectoryFacing` defaults to `1`.
- `p.ProjectJ.MM.RepairRemoteTrajectoryFacingMinSpeed` defaults to `80`.
- `p.ProjectJ.MM.RepairRemoteTrajectoryFacingMaxYawDelta` defaults to `35`.
- `p.ProjectJ.MM.DisableRemoteAccelReset` defaults to `1`.
- `p.ProjectJ.MM.SmoothRemoteTrajectoryPosition` defaults to `0`.
- `p.ProjectJ.MM.SmoothRemoteTrajectoryRotation` defaults to `0`.

## Combat Strafe reselect

The combat animation profile exposes `bForceReselectOnStrafeInputTurn` (default
`true`) and `StrafeInputTurnReselectAngle` (default `35`). They apply only to
camera-facing combat Strafe. When an already-held input turns at least that
amount—for example W to A/D or W+D to D—the Motion Matching node invalidates
its continuing pose and searches the current combat Cycle PSD again. This keeps
a forward running pose from surviving a lateral-direction transition without
changing ordinary locomotion or remote-proxy budgeting.

`TurnRedirect` remains part of the combat Strafe set: it is used once when an
already-held input changes direction by the locomotion turn threshold. While a
lateral input is held, the actor deliberately keeps camera-facing rotation, so
its desired-facing delta remains near +/-90 degrees. That persistent offset is
not a continuing turn; after the short turn hold, the selector returns to the
combat Cycle PSD, where Pose Search selects the sustained lateral pose.

## Landing recovery duration

`LandingRequestDuration` and `StandLandingRequestDuration` are the maximum
times that moving and standing landing PSDs own Motion Matching. Their defaults
are both `1.00` seconds, for combat and non-combat locomotion alike. Landing
search is suppressed after the initial database-change search
(`bSearchLandingEveryUpdate = false`), so the selected one-shot clip cannot be
restarted while that one-second recovery state is active. On leaving Landing,
the database changes to ground locomotion and permits an immediate new search.

Stop uses the same one-shot policy (`bSearchStopEveryUpdate = false`): the
initial stop result is held until the stop state resolves to Idle or a new input
starts movement. This prevents a combat Stop PSD from being restarted or lost
to a fresh search during deceleration.

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
