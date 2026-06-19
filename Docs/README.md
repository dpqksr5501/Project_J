# Project J Docs

This folder tracks Project J architecture notes, animation decisions, and deferred MMORPG systems work.

## Documents

- [Project Overview](ProjectOverview.md)
  - 프로젝트의 게임 정체성, 현재 구현 범위, 모듈·런타임 소유권, 서버·전투·애니메이션 원칙을 한 문서로 정리한 기준 개요.
- [MMORPG Architecture Review](MMORPGArchitectureReview.md)
  - Module boundaries, character systems, GAS, services, networking, backend/UI, Mass, and Motion Matching priorities.
- [Motion Matching Notes](MotionMatchingNextSteps.md)
  - C++ Motion Matching architecture, remote simulated proxy trajectory repair, animation budget behavior, and validation checks.
- [Skill System Architecture](SkillSystemArchitecture.md)
  - Current GAS ownership, AbilitySet grants, InputTag activation foundation, and the future skill input router direction.
- [Deferred MMORPG Systems](DeferredMMORPGSystems.md)
  - Systems that should remain deferred until the core gameplay and networking path justifies them.
- [Combat Animation Architecture Notes](CombatAnimationArchitectureNotes.md)
  - Locomotion/combat ownership boundaries and future weapon, attack, dodge, and hit reaction animation paths.

## Current Structure Policy

- Player Motion Matching is focused on the local player and nearby remote players.
- NPC animation should default to cheaper non-MM paths until profiling proves player-grade Motion Matching is needed.
- Animation settings resolve through `CharacterAnimProfile -> LocomotionProfile -> MotionMatchingAssetSet`.
- `LocomotionAnimStateComponent` owns movement state; `CharacterAnimInstance` publishes the thread-safe snapshot to Chooser and Motion Matching.
- Remote simulated proxies must reconstruct query data from replicated movement and visual smoothing, not from local input acceleration.
- MMO-scale optimization should be measured through Near/Mid/Far/Hidden animation budget tiers before broad feature expansion.

## Useful PIE Commands

- `DumpMMOState`
- `DumpAnimBudget`
- `DumpReplicationPolicy`
- `DumpCharacterComponents`
- `DumpCombatState`
- `DumpMMOProfilingSnapshot [MaxDetailedCharacters]`

## Motion Matching Debug CVars

- `p.ProjectJ.MM.DebugRemoteTrajectory 1`
- `p.ProjectJ.MM.RepairRemoteTrajectoryFacing 0/1`
- `p.ProjectJ.MM.DisableRemoteAccelReset 0/1`
- `p.ProjectJ.MM.SmoothRemoteTrajectoryPosition 0/1`
- `p.ProjectJ.MM.SmoothRemoteTrajectoryRotation 0/1`
