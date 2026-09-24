# Project J 문서 안내

문서는 **주제**를 먼저 찾고, 변경 이력은 **검토 → 구현 → 후속 보완** 순서로 읽는다. 날짜가 붙은 보고서와 검증 수치는 작성 당시의 기록이다. 현재 동작은 관련 최신 결과 문서와 `Source`·`Config`를 함께 확인한다.

## 주제별 위치

| 폴더 | 내용 |
| --- | --- |
| [프로젝트 개요](Overview/README.md) | 게임 소개, 모듈과 소유권의 현재 문맥 |
| [아키텍처](Architecture/README.md) | 런타임 책임, 확장 기반, 네트워크, 성능, 애니메이션 설계와 검증 |
| [애니메이션](Animation/README.md) | 이동 애니메이션, 모션 매칭, State Controller, TIP, 리타기팅 |
| [전투](Combat/README.md) | 전투 애니메이션·이동, Greatsword 작성, VFX |
| [게임플레이](Gameplay/README.md) | 직업·전직·장비 데이터, 탈것, 스킬, 콘텐츠 확장 |
| [성능 설계](Performance/README.md) | 성능 최적화의 기반과 보류된 설계 |
| [성능 측정](Benchmarks/SystemsModernization.md) | 측정 방법·원시 데이터·재계산과 E/F 실행 실험 |
| [감사와 후속 보완](Review/README.md) | 감사, 리팩터링 요청·결과, 부족한 부분의 후속 보완, TIP 진단 |
| [작업 인계](Handoffs/SystemsModernization_2026-09-09/인수인계.md) | 과거 작업 인계와 당시 브랜치·검증 상태 |
| [참고 메모](Reference/README.md) | 감사 후보를 정리한 원본 참고 자료 |

## 최근 변경을 읽는 순서

1. [현재 프로젝트 개요](Overview/ProjectOverview.md)와 [작업 문맥](Overview/ProjectContext.md)에서 모듈과 책임 경계를 확인한다.
2. [캐릭터 컴포넌트 소유권](Architecture/Runtime/Character_Component_Ownership_2026-09-20.md), [내부 갱신·수명 정리](Architecture/Runtime/Internal_Polish_2026-09-19.md), [확장 기반](Architecture/Extensions/Extension_Foundation_2026-09-19.md)에서 현재 구현 범위를 확인한다.
3. 리팩터링의 **요청과 결과**, 이후 발견된 부족한 부분은 [Review 작업 흐름](Review/README.md)에서 순서대로 확인한다. [캐릭터 런타임](Review/CharacterRuntime/README.md)과 [TIP 순간 튐 분석](Review/Animation/TIP/Project_J_TIP_Visual_Pop_Trace_2026-09-24.md)이 최근 사례다.
4. 성능 수치는 [측정 조건과 결과](Benchmarks/SystemsModernization.md) 및 [E/F 실행 실험](Benchmarks/ExecutionExperiments_2026-09-12/README.md)에서 원본 데이터와 함께 본다. 서로 다른 테스트 단계의 통과 개수나 측정값은 합산하지 않는다.

`Prompt`는 작업 요청이며 완료 증거가 아니다. `Result`도 해당 날짜의 결과이므로 후속 문서를 함께 확인한다. 이전 문서의 제안과 인계 지침보다 최신 구현·검증 기록을 우선한다.

문서 링크는 `node Scripts/Validation/Validate-DocLinks.mjs`로 검사한다. 과거 `Saved/Worktrees`에만 있던 로컬 실험 증거는 현재 저장소에 없을 수 있어 별도로 집계한다.

## 상세 문서 목록과 과거 기록

현재 구조는 **캐릭터 컴포넌트 실행·수명 → 내부 갱신·수명 정리 → 확장 기반 → 제작 도구** 순으로 확인한다. 후속 변경과 검증은 [2026-09-20 컴포넌트 정리](Architecture/Runtime/Character_Component_Ownership_2026-09-20.md)에 기록한다. [2026-09-19의 81개 테스트 및 Editor/Game 빌드 기록](Architecture/Runtime/Internal_Polish_Validation_2026-09-19.json)과 이전 문서의 54개·61개 등은 당시 단계별 결과이며 합산하지 않는다. 성능 비교는 별도의 A–E 벤치마크 조건을 따른다.

- [캐릭터 컴포넌트 실행·수명 정리 (2026-09-20)](Architecture/Runtime/Character_Component_Ownership_2026-09-20.md)
  - UI 소비자별 수명, 메시 갱신 요구 합성, 발도·납도 상태 소유권, 원격 locomotion의 시간 처리와 호환 경계.

- [내부 갱신·수명 정리와 제작 도구 (2026-09-19)](Architecture/Runtime/Internal_Polish_2026-09-19.md)
  - GAS 필요 기반 Tick, 서버 피격 기록 주기, 애니메이션 요청 합성, NPC 갱신 책임, 이동 코드 분리, 탈것·Handover 수명 점검.
- [직업·전직 묶음 제작 도구 사용법 (2026-09-19)](Architecture/Extensions/Content_Bundle_Authoring_2026-09-19.md)
  - 에디터에서 기존 DA를 참고해 미리보기·검증 후 새 직업/전직·스타일·콤보를 연결해 생성. 저장과 runtime 목록 등록은 명시적으로 진행.

- [직업·전직·전투 콘텐츠 확장 기반 (2026-09-19)](Architecture/Extensions/Extension_Foundation_2026-09-19.md)
  - PlayerState 진행 상태, 공유 능력 수명, 구성 영역별 선택, inline 능력 및 공격 목록 생성, 저장 어댑터 계약과 검증 범위.

- [콘텐츠 확장·DA 작성 구조 검토 (2026-09-13)](Architecture/Extensions/Extension_Architecture_Review_2026-09-13.md)
  - 9월 13일 당시 Master ABP, 직업·전직·스킬 조합과 콘텐츠 작성 방식의 진단·제안. 후속 구현 여부는 9월 19일 문서를 기준으로 확인한다.

- [런타임 내부 리팩터링 (2026-09-13)](Architecture/Runtime/Internal_Refinement_2026-09-13.md)
  - 궤적·AnimGraph 읽기 경계, 입력/커맨드 해제, 인벤토리·장비 재진입, 카메라 빙의, 서버 되감기와 상호작용 책임을 보강한 변경 및 검증 범위.

- [MMORPG Extension Foundation (2026-09-12)](Architecture/Extensions/MMO_Foundation_2026-09-12.md)
  - 기존 C++ 진단, Core-only 기반 모듈, 요청·저장·동시성 계약과 후속 구현 절차. [205개 콘텐츠·운영 확장 항목](Architecture/Extensions/MMO_Content_Catalog.md)은 구현 완료 목록과 구분한다. 신규 8개·기존 회귀 16개 검증 및 월드 정리 경고 해결 결과는 [검증 JSON](Architecture/Extensions/MMO_Foundation_Validation_2026-09-12.json)에 기록한다.

- [E Follow-up & F Execution Experiments (2026-09-12)](Benchmarks/ExecutionExperiments_2026-09-12/README.md)
  - 기본 조명 PSO 누락 보완, 승인된 ABP·Trail 적용, 실제 Tasks/Tick/FRunnable/Chaos/RDG/Audio/PCG 실험과 CSV. 실험 통과와 production 채택을 구분한다.

- [A–E Performance Measurements & Evidence](Benchmarks/SystemsModernization.md)
  - main에 통합한 멀티스레드 구조의 측정 조건, Mass·Niagara·네트워크·PSO 비교와 공개 CSV/JSON. Python 표준 라이브러리로 대표 수치를 재계산할 수 있다.

- [MMORPG Execution Roadmap (2026-09-03)](Architecture/Planning/ProjectJ_Mmorpg_Execution_Roadmap_2026-09-03.md)
  - 전체 MMORPG 확장 작업을 P0~P3와 Stage 0~9로 분리한 실행 로드맵. Animation threading/fast-path 감사, dedicated-server vertical slice, Iris/AOI, NPC tier, streaming·rendering·backend의 선행 조건과 검증 게이트를 정의한다.

- [Animation Execution & Threading Audit Plan (2026-09-03)](Architecture/Animation/ProjectJ_Animation_Execution_Threading_Audit_Plan_2026-09-03.md)
  - UE 5.8 source와 Insights로 Game Thread, parallel update/evaluate, proxy snapshot, Fast Path, Chooser/MM/BlendStack 실행 비용을 확인하는 분석 계획이다.

- [Scalability Profiling & Validation Plan (2026-09-03)](Architecture/Performance/ProjectJ_Scalability_Profiling_Validation_Plan_2026-09-03.md)
  - 1/10/30/50/100 population ladder와 CPU·GPU·memory·network·correctness의 공통 측정/회귀 기준을 정의한다.

- [Baseline Capture Guide (2026-09-03)](Architecture/Performance/ProjectJ_Baseline_Capture_Guide_2026-09-03.md)
  - Development build에서 Unreal Insights trace와 existing Exec dump를 수집하는 실제 실행 순서 및 전달 자료를 정리한다.

- [Profiling Baseline Results (2026-09-03)](Architecture/Performance/ProjectJ_Profiling_Baseline_Results_2026-09-03.md)
  - S0 단일 플레이어 anchor와 실제 이동 중인 70 local clone(S70) Unreal Insights CPU 결과, workload 조건, timer 원시값, 해석 한계와 후속 비교 규격을 보관한다.

- [Network Baseline Capture Guide (2026-09-04)](Architecture/Networking/ProjectJ_Network_Baseline_Capture_Guide_2026-09-04.md)
  - 2-client PIE dedicated-server 기준선, live NetDriver/Iris 확인, replication functional path 및 server/client trace 수집 절차를 정의한다.

- [Network Baseline Results (2026-09-04)](Architecture/Networking/ProjectJ_Network_Baseline_Results_2026-09-04.md)
  - N2 PIE에서 실제 dedicated-server/client topology와 Legacy `IpNetDriver` 사용, Iris 비활성 상태를 보관한다.

- [Project_J Architecture & Performance Audit (2026-09-03)](Architecture/Reviews/ProjectJ_Architecture_Audit_2026-09-03.md)
  - 현재 C++/Config와 최소 에디터 증거를 대조한 전체 구조 감사. 구현됨·기반만 존재함·계획 상태를 구분하고, Iris/Mass/backend/전용 서버의 실제 범위를 명시한다.

- [GASP Pivot Architecture Correction (2026-08-24)](Animation/Locomotion/GASP_Pivot_Architecture_Correction_2026-08-24.md)
  - GASP의 regular MM PSD와 Experimental State Machine 경로를 분리해 정리하고, `Pivots` PSD 태그·`Pivot` chooser 태그·Project_J Run Pivot의 현재 비활성 상태를 바로잡은 기준 문서.

- [Combat Strafe Turn In Place Implementation (2026-08-12)](Combat/CombatStrafe_TurnInPlace_Implementation_2026-08-12.md)
  - Combat Strafe Idle TIP의 C++/State Controller/Chooser/Blend Stack/Steering 구조, 연속 재생 Force Blend, MM fallback 문제와 해결, 에디터 저작 및 디버그 절차.
- [Combat Draw/Sheathe Stale One-shot Resolution (2026-08-12)](Combat/CombatIntro_StaleOneShot_Resolution_2026-08-12.md)
  - 장비 장착/해제 FullBody 몽타주 뒤에 이전 모드의 Land/Start/Stop direct Blend Stack 애셋이 재개되는 문제의 양방향 presentation-epoch 해결 구조.

- [Locomotion OTM & Offset Root Bone Bug Report (2026-08-06)](Animation/Locomotion/Locomotion_OTM_OffsetRootBone_BugReport_2026-08-06.md)
  - OTM 모드 Start 원샷, TurnInPlace 360도 스핀 및 초과 회전 Clamping, 착지 GaitIntent 잠금, sprint_land Shift 해제 시 모션매칭(PSD) 조기 인터럽트 수복 내역 종합 보고서.

- [Moving Reorientation & TIP WorkLog (2026-08-05)](Animation/Locomotion/MovingReorientation_TIP_WorkLog_2026-08-05.md)
  - Moving Reorientation (Rotation Break) C++ 알고리즘, Chooser Reselect 조건, UBT 빌드 검증 및 세션 간 연속성 가이드.

- [GASP ↔ Project_J Locomotion / State Controller 대응표](Animation/Locomotion/GASP_ProjectJ_Locomotion_Parity.md)
  - 검토한 GASP ABP/Chooser/함수와 Project_J native·ABP 경로의 구현 상태, 의도적 보류 항목, OTM Reface Start에 필요한 추가 확인 자료를 정리한 기준표.
- [캐릭터 런타임 책임 분리 결과 (2026-09-24)](Review/CharacterRuntime/Refactor/Project_J_Character_Runtime_Responsibility_Refactor_Result_2026-09-24.md)
  - 캐릭터·이동·애니메이션 인스턴스의 상태 소유권, 게임 스레드와 작업 스레드의 경계, 터미널 빌드 및 자동화 검증 결과.
- [StateController 런타임 책임 마무리 결과 (2026-09-24)](Review/CharacterRuntime/Followups/Project_J_StateController_Runtime_Finalization_Result_2026-09-24.md)
  - 이전 부분 분리의 이유, 표현 상태 전이의 최종 소유권, 회귀 테스트와 빌드 검증 결과.

- [Project Context](Overview/ProjectContext.md)
  - Current project context: module boundaries, runtime ownership, data flows, configuration entry points, and a new-task checklist.

- [Project Overview](Overview/ProjectOverview.md)
  - 프로젝트의 게임 정체성, 현재 구현 범위, 모듈·런타임 소유권, 서버·전투·애니메이션 원칙을 한 문서로 정리한 기준 개요.
- [Architecture Refactor 2026-06-19](Architecture/Runtime/ArchitectureRefactor_20260619.md)
  - 런타임 소유권, 장비 operation, 전투 판정, animation event와 AnimInstance 책임 분리 작업 기록.
- [Content Expansion Guide](Gameplay/ContentExpansionGuide.md)
  - 공격력과 신규 스탯, 직업·전직, 스킬, 장비, 인벤토리를 현재 코드와 에디터에 연결하는 구현 가이드.
- [MMORPG Architecture Review](Architecture/Reviews/MMORPGArchitectureReview.md)
  - Module boundaries, character systems, GAS, services, networking, backend/UI, Mass, and Motion Matching priorities.
- [Motion Matching Notes](Animation/Planning/MotionMatchingNextSteps.md)
  - C++ Motion Matching architecture, remote simulated proxy trajectory repair, animation budget behavior, and validation checks.
- [Skill System Architecture](Gameplay/SkillSystemArchitecture.md)
  - Current GAS ownership, AbilitySet grants, InputTag activation foundation, and the future skill input router direction.
- [Greatsword Combat Authoring Guide](Combat/GreatswordCombatAuthoringGuide.md)
  - Current data-authoring workflow for LMB/RMB, Q/R/T, modifiers, combo branches, airborne root-motion attacks, and inline independent weapon-motion/ground-contact authoring.
- [Mount System Architecture](Gameplay/MountSystemArchitecture.md)
  - Reusable ground/flying mount boundaries, interaction, camera, input, replication, and GAS foundation.
- [Deferred MMORPG Systems](Architecture/Planning/DeferredMMORPGSystems.md)
  - Systems that should remain deferred until the core gameplay and networking path justifies them.
- [Combat Animation Architecture Notes](Combat/CombatAnimationArchitectureNotes.md)
  - Locomotion/combat ownership boundaries and future weapon, attack, dodge, and hit reaction animation paths.
- [Combat Locomotion Architecture](Combat/CombatLocomotionArchitecture.md)
- [Remote Locomotion One-Shot Replication](Animation/Locomotion/RemoteOneShotReplication.md)
  - Reusable weapon-family linked layers, socket/IK rules, and combat animation priority.
- [Combat Animation Composition](Combat/CombatAnimationComposition.md)
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
- `ProjectJ.DumpNetworkRuntime`
- `DumpCharacterComponents`
- `DumpCombatState`
- `DumpMMOProfilingSnapshot [MaxDetailedCharacters]`

## Motion Matching Debug CVars

- `p.ProjectJ.MM.DebugRemoteTrajectory 1`
- `p.ProjectJ.MM.RepairRemoteTrajectoryFacing 0/1`
- `p.ProjectJ.MM.DisableRemoteAccelReset 0/1`
- `p.ProjectJ.MM.SmoothRemoteTrajectoryPosition 0/1`
- `p.ProjectJ.MM.SmoothRemoteTrajectoryRotation 0/1`
