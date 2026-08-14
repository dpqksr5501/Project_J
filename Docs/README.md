# Project J Docs

This folder tracks Project J architecture notes, animation decisions, and deferred MMORPG systems work.

## Documents

- [Combat Strafe Turn In Place Implementation (2026-08-12)](CombatStrafe_TurnInPlace_Implementation_2026-08-12.md)
  - Combat Strafe Idle TIP의 C++/State Controller/Chooser/Blend Stack/Steering 구조, 연속 재생 Force Blend, MM fallback 문제와 해결, 에디터 저작 및 디버그 절차.
- [Combat Draw/Sheathe Stale One-shot Resolution (2026-08-12)](CombatIntro_StaleOneShot_Resolution_2026-08-12.md)
  - 장비 장착/해제 FullBody 몽타주 뒤에 이전 모드의 Land/Start/Stop direct Blend Stack 애셋이 재개되는 문제의 양방향 presentation-epoch 해결 구조.

- [Locomotion OTM & Offset Root Bone Bug Report (2026-08-06)](Locomotion_OTM_OffsetRootBone_BugReport_2026-08-06.md)
  - OTM 모드 Start 원샷, TurnInPlace 360도 스핀 및 초과 회전 Clamping, 착지 GaitIntent 잠금, sprint_land Shift 해제 시 모션매칭(PSD) 조기 인터럽트 수복 내역 종합 보고서.

- [Moving Reorientation & TIP WorkLog (2026-08-05)](MovingReorientation_TIP_WorkLog_2026-08-05.md)
  - Moving Reorientation (Rotation Break) C++ 알고리즘, Chooser Reselect 조건, UBT 빌드 검증 및 세션 간 연속성 가이드.

- [GASP ↔ Project_J Locomotion / State Controller 대응표](GASP_ProjectJ_Locomotion_Parity.md)
  - 검토한 GASP ABP/Chooser/함수와 Project_J native·ABP 경로의 구현 상태, 의도적 보류 항목, OTM Reface Start에 필요한 추가 확인 자료를 정리한 기준표.

- [Project Context](ProjectContext.md)
  - Current project context: module boundaries, runtime ownership, data flows, configuration entry points, and a new-task checklist.

- [Project Overview](ProjectOverview.md)
  - 프로젝트의 게임 정체성, 현재 구현 범위, 모듈·런타임 소유권, 서버·전투·애니메이션 원칙을 한 문서로 정리한 기준 개요.
- [Architecture Refactor 2026-06-19](ArchitectureRefactor_20260619.md)
  - 런타임 소유권, 장비 operation, 전투 판정, animation event와 AnimInstance 책임 분리 작업 기록.
- [Content Expansion Guide](ContentExpansionGuide.md)
  - 공격력과 신규 스탯, 직업·전직, 스킬, 장비, 인벤토리를 현재 코드와 에디터에 연결하는 구현 가이드.
- [MMORPG Architecture Review](MMORPGArchitectureReview.md)
  - Module boundaries, character systems, GAS, services, networking, backend/UI, Mass, and Motion Matching priorities.
- [Motion Matching Notes](MotionMatchingNextSteps.md)
  - C++ Motion Matching architecture, remote simulated proxy trajectory repair, animation budget behavior, and validation checks.
- [Skill System Architecture](SkillSystemArchitecture.md)
  - Current GAS ownership, AbilitySet grants, InputTag activation foundation, and the future skill input router direction.
- [Greatsword Combat Authoring Guide](GreatswordCombatAuthoringGuide.md)
  - Current data-authoring workflow for LMB/RMB, Q/R/T, modifiers, combo branches, debugging, and airborne root-motion attacks.
- [Mount System Architecture](MountSystemArchitecture.md)
  - Reusable ground/flying mount boundaries, interaction, camera, input, replication, and GAS foundation.
- [Deferred MMORPG Systems](DeferredMMORPGSystems.md)
  - Systems that should remain deferred until the core gameplay and networking path justifies them.
- [Combat Animation Architecture Notes](CombatAnimationArchitectureNotes.md)
  - Locomotion/combat ownership boundaries and future weapon, attack, dodge, and hit reaction animation paths.
- [Combat Locomotion Architecture](CombatLocomotionArchitecture.md)
  - Reusable weapon-family linked layers, socket/IK rules, and combat animation priority.
- [Combat Animation Composition](CombatAnimationComposition.md)
  - Data-driven upper-body overlay versus validated full-body weapon locomotion, including the master-layer contract.

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
