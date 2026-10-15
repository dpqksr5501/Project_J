"""Record explicit read-only review scopes separately from defect execution status."""
from pathlib import Path
from datetime import datetime, timezone
import json
import subprocess

root = Path(__file__).resolve().parents[2]
out = root / 'Docs/Review/Audits/ContinuationEvidence_2026-10-14'
paths = subprocess.check_output(['rg', '--files', '--hidden', '--no-ignore', 'Source', 'Plugins/ProjectJExperiments/Source'], cwd=root).decode().splitlines()
paths = [p.replace('\\', '/') for p in paths]

def resolve(*names, prefix=None):
    """Map named, actually reviewed files to paths; grants no whole-file status."""
    return sorted(p for p in paths if (prefix and p.startswith(prefix)) or Path(p).name in names)

def stems(*names):
    return sorted(p for p in paths if Path(p).suffix in ('.h', '.cpp') and Path(p).stem in names)

def unit(id, files, scope, calls, owner, assets, failures, reason):
    return dict(id=id, status_ko='읽기 전용 명시 범위 대조·전체 시스템 미완료', files=files,
                scope_ko=scope, calls_ko=calls, owner_ko=owner, assets_ko=assets,
                failures_ko=failures, reason_ko=reason,
                whole_system_complete=False, asset_graph_review_complete=False)

units = [
    unit('CR13', resolve(prefix='Source/Project_JMount/'),
         '기존 Mount23개 runtime/header/inline·fixture/test·module/build의 전체 함수와 Character mounted layer/lease 및 Player summon/interaction/snapshot의 지정 caller. 새 TransitionTests는 주 작업자의 CA19 재현·수정 범위다. Character/Player 전체 모듈 완료 아님.',
         'request/RPC→eligibility→attach/CMC callback→rider relation/tag→possession; replicated rider→restore/capture; flight phase→GAS tags/cue/CMC/notify→animation.',
         'Mount actor는 이동/possession/server phase, rider component는 복제 관계와 원래 rider ASC tag lease. GT snapshot에서 worker로 값만 소비.',
         'BP_Wyvern/ABP_Wyvern, DA_RiderProfile_Dragon, 실제 linked rider ABP/interface, IMC와 실제 지정 IA, cue sequence/socket/retarget 최소 조회 대기.',
         'CA19 attach retirement·replicated A→B restore·phase tag supersession 실제3fail/미실행0. clone→새 session·protected phase 서버하차·native dead 공급·callback GC·authoring numeric은 별도 미검증.',
         'GAS health mirror/blocked exit rejection/forced lifecycle restore, dedicated cue fallback·landing collision+cue, commit 이후 K2 mounted 성공 의미를 유지.'),
    unit('CR14', resolve('Project_JGatewaySubsystem.cpp','Project_JGatewaySubsystem.h','Project_JBackendConnection.h',
                         'Project_JHandoverManager.cpp','Project_JHandoverManager.h','Project_JHandoverTransport.cpp','Project_JHandoverTransport.h',
                         'Project_JHandoverSerializable.cpp','Project_JHandoverSerializable.h','Coordination.cpp','Coordination.h',
                         'Repository.cpp','Repository.h','FeatureCatalog.cpp','FeatureCatalog.h','ServicePort.h',
                         'Project_JPlayerState.cpp','Project_JPlayerState.h','Project_JGameMode.cpp','Project_JGameMode.h'),
         'Gateway/connection/manager/loopback/interface/MMO foundation/PS/GameMode 전체 함수·header·inline. Base/Player level·handover 및 Inventory cooldown 소비는 지정 함수만. catalog inl 항목을 읽었으나 JSON205 항목별 상호대조 미완료.',
         'HTTP GT completion→tracker/current session→manager attempt/state→checked Player pose+level apply. GI composition→catalog→실제 service port 설치 여부.',
         'PS persistent owner와 avatar projection. manager state 이름은 운영 destination/source authority commit이 아님. Repository revision·receipt, Gate physical reservation, Tracker delivery 분리.',
         '실제 GameMode/PlayerStateClass·BP copy/init/transport 호출, class DA/cap override 대기. Backend ABP/Chooser/IA/IMC 직접 의존 없음.',
         '음수 log queue/비유한 timeout·flush, 큰 zero-delay retry 재귀, Revision0 owner 승인 우회는 source-only. 새 PS migration/DB/auth/fencing adapter 설치 증거 없음.',
         '기존 CA10/12/13 packet snapshot·GC pin·attempt/session·replay digest와 CA14 exact level/XP commit은 재사용하고 중복 수정하지 않음.'),
    unit('CR15', resolve('Project_JQuestComponent.cpp','Project_JQuestComponent.h','Project_JSocialSubsystem.cpp','Project_JSocialSubsystem.h',
                         'Project_JPlayerUIComponent.cpp','Project_JPlayerUIComponent.h','Project_JInventoryViewModel.cpp','Project_JInventoryViewModel.h',
                         'Project_JStatusEffects.cpp','Project_JStatusEffects.h','Project_JUIModels.cpp','Project_JUIModels.h',
                         'Project_JCharacterUIProfile.cpp','Project_JCharacterUIProfile.h','Project_JUILayoutSettings.cpp','Project_JUILayoutSettings.h',
                         'Project_JUIIconRequest.cpp','Project_JUIIconRequest.h','Project_JMinimapWidget.cpp','Project_JMinimapWidget.h'),
         'Quest/Social 및 지정 UI cpp/header 전체. PS/social relevance와 UI→Player→Execution→ASC release caller 대조. HUD는 Quest action/profile/CharacterModule 표시 함수만이며 layout/drag/drop/widgets 전체 미완료.',
         'trusted objective→Claim commit→reward→rollback/XP commit; social membership maps→PS projection; UI source/profile→skills/release/CanActivate BP→server receipt; map data→UV→paint.',
         'owner-only quest, social maps authority와 PS projection, UI pending/서버 확정·local preferences 분리. 원래 input tag/ASC lease 반환.',
         'DA_ProjectJFirstSteps/DA_ProjectJMap/DA_ProjectJUIProfiles와 실제 profile/widget/GE UI component 지정 연결 및 Cook 대기.',
         'ReleaseAll/ReleaseSkill reentry·new lease/GC, availability BP callback current성, Minimap failed UV 사용, retired social/invalid identity projection은 실행 미재현.',
         'CA12 reward rollback·CA14 지급commit, group revision/tombstone, UI next-tick coalescing/비재전송 timeout, 기존 UI GC/FieldNotify 수정은 유지.'),
    unit('CR16', resolve('Project_JNPCCharacter.cpp','Project_JNPCCharacter.h','Project_JNPCUpdateBudget.h',
                         'Project_JGameplayAbility_NPCAttack.cpp','Project_JGameplayAbility_NPCAttack.h',
                         'Project_JNPCActionComponent.cpp','Project_JNPCActionComponent.h','Project_JNPCPathSubsystem.cpp','Project_JNPCPathSubsystem.h',
                         'Project_JNPCDecisionSubsystem.cpp','Project_JNPCDecisionSubsystem.h','Project_JNPCDecisionExperiment.cpp','Project_JNPCDecisionExperiment.h',
                         'Project_JMassRepresentationSubsystem.cpp','Project_JMassRepresentationSubsystem.h','Project_JMassMovementProcessor.cpp','Project_JMassMovementProcessor.h',
                         'Project_JMassMonster_Trait.cpp','Project_JMassMonster_Trait.h','Project_JMassMonsterSpawner.cpp','Project_JMassMonsterSpawner.h',
                         'Project_JTargetScoringSubsystem.cpp','Project_JTargetScoringSubsystem.h','Project_JGameplayAsyncTypes.h',
                         'Project_JTargetScoring.cpp','Project_JTargetScoring.h','Project_JTargetSpatialSnapshot.cpp','Project_JTargetSpatialSnapshot.h','Project_JRetryDelay.h'),
         'NPC actor/policy/attack/Mass4쌍/Path/Core scoring 전체 함수·header·inline. Action resume/timer/suspend/owned callback, Decision capture/admission/queue/dispatch/shutdown 확장. NavigationLoad 전체 줄 검토는 아니며 큰 Base/Player 전체 아님.',
         'NPC policy→GT capture→copied worker→GT result→Action→engine path/owned MoveId→GAS attack/hit/end; Mass lease→joined value worker→GT apply/restore.',
         'logical cancel/delivery와 physical task/engine completion slot, registration/query/action/ability epoch 분리. Brain 실행 중 native action admission 거절로 이중 AI 권위 방지.',
         'BP_NPC_Test/GA_NPC_TestAttack의 실제 defaults/IsDead/BeginPlay/BT·ABP/attack montage·navmap 및 설치 Mass config 조회 대기. registry metadata 재사용.',
         'NPCAttack validation/presentation/hit callback에서 현재 activation 취소/재시작·GC, NPC numeric 후보. 실제 Mass bridge/Stats fragment gameplay 설치 미확인.',
         'Core physical2·module unload join, Path tombstone/GT dispatch/RequestMove, 기존 Mass reentrant retirement+GC와 suspension 실제 timer/foreign grant 증거 유지.'),
    unit('CR17', resolve('Project_JInventoryComponent.cpp','Project_JInventoryComponent.h','Project_JEquipmentManagerComponent.cpp','Project_JEquipmentManagerComponent.h',
                         'Project_JEquipmentRuntimeComponent.cpp','Project_JEquipmentRuntimeComponent.h','Project_JAbilitySet.cpp','Project_JAbilitySet.h',
                         'Project_JAbilitySystemComponent.cpp','Project_JAbilitySystemComponent.h','Project_JWeaponPresentationProfile.cpp','Project_JWeaponPresentationProfile.h',
                         'Project_JWeaponPresentationComponent.cpp','Project_JWeaponPresentationComponent.h','Project_JWeaponMotionTypes.h',
                         'Project_JEquipmentTypes.h','Project_JItemDefinition.cpp','Project_JItemDefinition.h','Project_JEquipmentItemDefinition.cpp','Project_JEquipmentItemDefinition.h',
                         'Project_JConsumableItemDefinition.cpp','Project_JConsumableItemDefinition.h','Project_JAnimationLayerLease.cpp','Project_JAnimationLayerLease.h',
                         'Project_JCombatAnimationLayerComponent.cpp','Project_JCombatAnimationLayerComponent.h','Project_JMountedAnimationLayerComponent.cpp','Project_JMountedAnimationLayerComponent.h'),
         'Inventory/EquipmentManager/Runtime/AbilitySet/ASC/definitions/types/WeaponPresentation main/profile/layer lease cpp/header 전체와 실제 Base/Player/PS callers. Diagnostics.cpp·Persona 전체·HUD 전체는 별도 미완료.',
         'RPC/receipt→inventory item lock→Manager commit/FastArray→avatar runtime grants/effects→weapon class/profile/mesh spawn→linked layer/GT grip snapshot.',
         'PS authoritative equipment, avatar runtime gameplay/visual leases, owner-only inventory/public equipment, generation/provider registry·unkeyed handles 분리.',
         '실제 Greatsword equip/AbilitySet/GE/style/presentation/weapon BP·master/retarget layer와 sockets/alpha/BeginPlay·Destroyed override 지정 조회 대기.',
         'consume/bag callback lifetime, incoming item 소실, unlock result, legacy RPC admission, default basename source, grant GC, weapon spawn identity, grip alpha, linked-layer lifecycle/ASC re-register 미재현.',
         '기존 CA04/05/07 keyed grant·removable Infinite GE·원래 ASC 반환, visual-only bounded retry·public/owner projection·schema 중복 필드 호환 유지.'),
]

units += [
    unit('CR18', resolve(prefix='Source/Project_JGAS/') + stems(
             'Project_JSkillInputExecutionComponent','Project_JSkillInputRouterComponent','Project_JGameplayAbility_Sprint',
             'Project_JGameplayAbility_CombatToggle','Project_JGameplayAbility_Melee','Project_JGameplayEffect_CombatMode',
             'Project_JCommandSet','Project_JComboDefinition','Project_JAttackDefinition','Project_JCombatTypes',
             'Project_JCombatMovementPolicy','Project_JCombatConfiguration','Project_JCombatHitValidationComponent',
             'Project_JCombatHitValidationPolicy','Project_JServerSideRewindComponent','Project_JCombatStateComponent',
             'Project_JCombatIntroComponent','Project_JCombatPresentationComponent','Project_JAnimNotifyState_MeleeHit',
             'Project_JAnimNotifyState_CombatPresentationCue','Project_JNetObjectPrioritizer_Combat'),
         'GAS12파일 전체·execution/router·Sprint/Toggle/Melee/CombatMode·실행데이터·hit/SSR·combat state/intro/presentation·두 notify 전체 cpp/header/inline. Player façade/authority/refresh/teleport caller는 지정 구간만. 모든 외부 task/BP/prediction 검증 아님.',
         'UI/raw input→alias/held lease→ASC spec/activation prediction→ability movement/presentation/tasks→canonical hit/SSR→end. notify→GAS callbacks/lease map.',
         'ASC spec storage lock과 lifetime/current avatar/activation epoch 분리. persistent CombatMode GE·combo 시작 style·canonical server hit와 cosmetic weapon 분리.',
         '실제 GA_Greatsword/CombatToggle·CombatMode/Sprint effect·style/command/combo/attack·slot/notify·Leader blade/SSR·IA/IMC 최소 조회 대기.',
         '기존 CR04 input activation·UI release/source epoch, Sprint applied handle 후게시, Melee/NPC callback transaction·notify map/batch, signed sequence와 prioritizer 합성 미재현.',
         'resource invariants·engine task/montage tick·owned window/subscription·server history·unreliable presentation recovery·기존 CA08/18 유지.'),
    unit('CR19', stems('Project_JLocomotionAnimStateComponent','Project_JLocomotionAnimStateComponentBase',
             'Project_JLocomotionAnimStateComponent.Input','Project_JLocomotionAnimStateComponent.Ground',
             'Project_JLocomotionAnimStateComponent.Air','Project_JLocomotionAnimStateComponent.Transitions','Project_JLocomotionAnimStateComponent.Support',
             'Project_JCharacterAnimInstance','Project_JCharacterAnimInstanceBase','Project_JCharacterAnimInstanceProxy',
             'Project_JCharacterAnimInstance.Snapshot','Project_JCharacterAnimInstance.SnapshotAccessors','Project_JCharacterAnimInstance.Search',
             'Project_JCharacterAnimInstance.Budget','Project_JCharacterAnimInstance.Continuity','Project_JCharacterAnimInstance.Presentation',
             'Project_JCharacterAnimInstance.PresentationUpdate','Project_JCharacterAnimInstance.UpperBodyFacing',
             'Project_JCharacterAnimInstance.FlowTrace','Project_JCharacterAnimInstance.ContinuityTrace','Project_JCharacterAnimInstance.SearchCostAudit',
             'Project_JMotionMatchingTrajectoryComponent','Project_JMotionMatchingAssetSet','Project_JAnimationUpdateCoordinatorComponent',
             'Project_JBudgetedSkeletalMeshComponent','Project_JCharacterAnimationBudgetSubsystem','Project_JRetargetAnimInstance',
             'Project_JRetargetAnimInstanceProxy','Project_JReplicatedAnimEventComponent','Project_JReplicatedJumpStateComponent'),
         '위 main/split cpp/header 전체와 context·remote/gait/turn·MM selection/boundary/engine adapter·unified/legacy node·snapshot policy·retarget/IK/facing 순수 helper 전체를 연결 대조. 큰 Player의 init/jump/edge/Landed만이며 모든 notify/모든 에셋 완료 아님.',
         'GT owner/data→value snapshot→proxy AddReferencedObjects→worker search/pose/IK→GT reselect completion. remote jump/landing semantic order·trajectory once/frame와 search/pose 구분.',
         'physical landing/presentation release/move/pivot/chooser/proxy/node revision은 각각 수명·완료조건이 달라 유지. outer linked traversal이 임시throttle/elapsed 복원 소유.',
         '실제 master/linked/follower ABP·Locomotion/Combat/Character/Weapon/Body/Family/HandGrip DA·Chooser 반환그래프·PSD/schema/normalization/mirror/clip/curve·palm/socket 대기.',
         'stream EndPlay timer lease·tag/BP 전환재진입·ordinary Cycle null·returned Chooser cycle 실행 미재현. no-op private sprint helper 정리후보와 BP entry 빈함수 유지 구분.',
         'OTM/Strafe·Run-Sprint·단발/MM·Land/측면·retarget 및 .35초/100cm/3도/85% 보존. cost audit은 사후재구성이며 원검색/CPU원인 확정자료 아님.'),
    unit('CR20', [p for p in paths if '/Tests/' not in p and p.startswith(('Source/Project_JCharacterEditor/','Source/Project_JAnimationNodes/'))],
         'ContentBundle/menu/Persona/좌표helper/WeaponMotion validator/Foley·Early migration/asset graph diagnostics 및 AnimationNodes wrapper/installer 전체 함수·header·inline. 회귀파일 전체 미검토. Experiments는 descriptor/startup/presentation audit/migration journal·receipt/output만이며 physics/GPU/audio/PCG 내부 미완료.',
         'draft validation→unsaved package/AssetCreated; explicit migration list→dirty/source preflight→per-package save; installer topology→rollback/compile; Config scan/registry→Cook closure.',
         'Editor/UncookedOnly/runtime 조건 의존과 disabled experiments. 새bundle Cook등록과 CharacterData ID선택 설치는 다른 권위.',
         '실제 Persona weapon root/child·notify montage·master/layer graph·class/style/combo·AbilitySet/registry 및 soft closure 지정 대조 대기.',
         'Escape cancel은 객체값 복원이 아님, preview/runtime transform owner, migration partial-save/canonical path, bundle 상세승인 범위 후보. 실제 editor/저장실패 재현 미완료.',
         'unsaved bundle 자동저장 안함·CA13 topology rollback·공식notify read/numeric·migration planned/original digest와 사용자 후속변경 거절 유지.'),
    unit('CR21', stems('Project_JObjectPoolRegistrySubsystem','Project_JObjectPoolTypes','Project_JInputLeaseSubsystem',
             'Project_JMessageSubsystem','Project_JAssetManager','Project_JGameDataSubsystem','Project_JVisualAssetSubsystem',
             'Project_JPlayerController','Project_JNetObjectFilter_Distance','Project_JNetObjectPrioritizer_Combat',
             'Project_JNetworkDiagnostics','Project_JIrisFastArrayDiagnostics','Project_JProfilingCrowd'),
         'registry/types·InputLease·Message·Core asset/data services·Controller·Distance/Prioritizer·diagnostics/crowd 전체와 Base significance 등록/해제/callback·설치manager·actual consumers. 전체Player/Base·runtime Iris/custom graph 설치 완료 아님.',
         'mapping lease→engine dynamic callback; message listener snapshot→payload operation pin; visual request→physical load→bounded GT apply; significance registration→실제 Update caller 검색; policy helper→actual caller/Config.',
         'policy map/기능catalog는 실제ActorPool/서비스 설치가 아님. actual Iris engine spatial과custom helper, displayed actor/replicated NPC/real sockets 분리.',
         'GameMode/Controller BP·Default/Mobile IMC/touch/menu IA, 정확한 service BPconsumer 지정 조회. currentsource significance updater/customIris 설치 못찾음.',
         'significance Update 미연결·near0/far3 역집계 및 worker rawUObject callback, Controller Acquire 실패미소비·message deinit admission·filter BP participant·crowd callback 후보 미재현.',
         'CA06 input/message callback/GC, physical load reservation·timer diagnostic/product 분리·default Iris actual remote 증거 재사용. global actor tick throttle 복원/worker UObject 접근 금지.'),
    unit('CR22', stems('Project_JPlayerHUDWidget','Project_JHUDWindow','Project_JInventoryWidgets','Project_JSkillButton',
             'Project_JInteractionQuery','Project_JInteractionPolicy','Project_JInteractionInterface','Project_JGameState',
             'Project_JRenderingWarmupSubsystem','Project_JDistortionPSOPrecache'),
         'HUD/window/inventory widgets/skillbutton 전체·cursor/presentation helper·Core interaction/Character query·GameState/main module/UI smoke·warmup/PSO 전체. 앞선 UI model 내부는 caller만 재사용. 실제 widget graph/physical focus/DPI 전체 미완료.',
         'server projection→native screen/profile/widget→press/drag origin→GUID request/layout identity; Player RPC→CanInteract BP→candidate/current interactor→Interact→Mount.',
         'row/drag/widget는 gameplay owner가 아님. item/quick drag frozen identity·modal focus·local layout·engine-owned PSO 요청 수명 분리.',
         'HUD/row/tile/skill/window BP names/Construct/visibility·HUDStyle/CharacterModule/map·interaction BP 최소조회 대기. physicaldrag/focus/DPI/여러LocalPlayer/렌더품질 미검증.',
         'Interactor retirement/authority, SkillButton press origin, window/menu drag source, HUD reinitialize subscription·동적widget callback current성 미재현.',
         '기존 item/quick identity·UI next-tick/debounce/표시timer·IconRequest GC 유지. UE5.8 monolithic rough-refraction 보완/Editor 원래path·NullRHI/render 경계 유지.'),
]

# Exact names reconciled against the reviewers' explicit path manifests. These
# links record source scope only; they do not grant runtime/asset completion.
extra_stems = {
    'CR18': 'Project_JCombatCommandSet Project_JCombatHitValidation Project_JCombatPresentationSet'.split(),
    'CR19': '''AnimNode_ProjectJLocomotion AnimNode_ProjectJOneShotHandoff AnimNode_ProjectJStrafeLowerBodyFacing
AnimNode_ProjectJStrafeUpperBodyFacing Project_JAnimationPresentationHelpers Project_JAnimNode_GuidedHandIK
Project_JCharacterAnimProfile Project_JCombatAnimProfile Project_JFootPlacementDefaults Project_JGuidedArmChain
Project_JGuidedArmSolver Project_JGuidedHandIKTrace Project_JHandContact Project_JHandGripProfile
Project_JMotionMatchingBoundaryPolicy Project_JMotionMatchingCVars Project_JMotionMatchingEngineAdapter
Project_JRetargetBodyProfile Project_JStrafeLowerBodyFacing Project_JStrafeUpperBodyFacing Project_JTrajectoryQuery
Project_JWeaponAnimProfile Project_JLocomotionAnimStateComponentAir Project_JLocomotionAnimStateComponentGround
Project_JLocomotionAnimStateComponentInput Project_JLocomotionAnimStateComponentSupport Project_JLocomotionAnimStateComponentTransitions
Project_JAnimationBudgetTypes Project_JAnimationClock Project_JAnimationFlowTrace Project_JAnimationLocomotionMode
Project_JAnimationProfileValidation Project_JAnimationSnapshot Project_JAnimationUpdateSchedule Project_JBackwardRefacePolicy
Project_JContinuousCurvePolicy Project_JGeneralTurnPolicy Project_JLocomotionContextBuilder Project_JMotionMatchingRuntime
Project_JMotionMatchingSelectionPolicy Project_JMovingTurnPolicy Project_JOneShotCurvePolicy Project_JPlaybackReturnPolicy
Project_JPredictionYawRateFilter Project_JRemoteLocomotionRuntime Project_JReplicatedAnimEventTypes Project_JReplicatedJumpState
Project_JStateControllerRuntime Project_JTurnEventSettings Project_JTurnInPlacePresentationRuntime Project_JTurnRequestSample
Project_JLocomotionAnimTypes Project_JAnimationQualityPolicy Project_JOneShotInputResponse
Project_JAnimNotify_FoleyEvent Project_JAnimNotify_SendGameplayEvent Project_JAnimNotify_SheatheWeapon
Project_JAnimNotify_WeaponPresentation Project_JAnimNotifyState_LocomotionEarlyTransition Project_JLocomotionDebugUtils
Project_JPlayerCharacter.Movement Project_JPlayerCharacter.MouseTurnTrace Project_JCharacterMovementComponent'''.split(),
    'CR21': '''Project_JProfilingCrowdComponent Project_JCore CombatDamageable Project_JCombatInterface
Project_JSocialStateInterface Project_JGameplayTags Project_JMMOTypes Project_JLogger Project_JMMO'''.split(),
    'CR22': '''Project_JCursorInput Project_JWidgetPresentationHelpers Project_JUIRuntimeSmoke
Project_JInteractionTargetComponent Project_JInteractable'''.split(),
    'CR17': '''Project_JWeaponPresentationDiagnostics Project_JWeaponPresentationActor Project_JConsumableDefinition
Project_JItemInstanceTypes'''.split(),
    'CR14': ['Project_JCharacterDataSubsystem'],
}
for review in units:
    review['files'] = sorted(set(review['files'] + stems(*extra_stems.get(review['id'], []))))
    if review['id'] == 'CR19':
        review['files'] = sorted(set(review['files'] + resolve('Project_JAnimationNodes.Build.cs','Project_JAnimationNodesModule.cpp')
                                    + stems('Project_JAnimNotifyState_ComboWindow','Project_JAnimNotifyState_TwoHandIK')))
        review['scope_ko'] += ' 추가: Movement/MouseTurnTrace·CMC cpp/header 및 관련 Build/module 전체, notify는 지정 exact-path manifest의 전체본문; ComboWindow/TwoHandIK cpp 기존 수정은 재사용하며 Super/BP·Melee/Cue 공통 callback은 미재현.'
    elif review['id'] == 'CR20':
        review['files'] = sorted(set(review['files'] + resolve('Project_J.Build.cs','Project_J.Target.cs','Project_JEditor.Target.cs','Project_JServer.Target.cs')))
        review['scope_ko'] += ' 추가: Project_J Build 및 Game/Editor/Server Target 4개 전체 읽기. Editor 조건 직접 의존성·root modules 대조.'
    elif review['id'] == 'CR21':
        review['files'] = sorted(set(review['files'] + resolve('Project_JCore.Build.cs','Project_JMMO.Build.cs')))
        review['scope_ko'] += ' 추가: 전체 GameplayTags 선언23/등록표·MMOTypes·Core/MMO module/Build와 Logger cpp 실제 본문 대조. tags 등록/사용 불일치 발견 없음.'

units += [
    unit('CR23', stems('Project_JBaseCharacter','Project_JPlayerCharacter','Project_JCameraComponent','Project_JFoleyComponent',
                      'Project_JCharacterUIBindingComponent','Project_JCharacterViewModel','Project_JWeaponPresentationComponent.Diagnostics',
                      'Project_JProgressionAvatarTests','Project_JGrantReentryTests','Project_JPlayerMountLifecycleTests',
                      'Project_JModularMeshComponent','Project_JGameFeatureReceiver','Project_JDefaultAttributeSetData',
                      'Project_JGreatswordCharacter','Project_JCharacter','Project_JDataValidation','Project_JMountItemDefinition'),
         'Base/Player main cpp/header 전체의 이전 지정함수 밖까지 전체 읽기. Camera/Foley component/UI binding·ViewModel/weapon Diagnostics 전체. FoleySubsystem은 Resolve/Prepare/Submit/Execute/Cancel·Stop 진입 지정 범위만.',
         'actor lifecycle→PS/local ASC/attributes/progression/equipment→InitActorInfo/OnProgression/GiveAbility; mount stream→inventory currentcheck→spawn BeginPlay→SummonedMount publication; Foley notify/dead gate→shared subsystem.',
         'PS persistent owner·NPC local owner·현재 avatar, async class request와동기spawn session분리. camera local ownership·UI fieldnotify demand·진단은presentation입력 아님.',
         '실제 파생 Player/NPC BP·defaultGA/class/progression/camera/Foley/profile·mount BeginPlay 및audio 지정연결 대기.',
         'Base 초기화 OnAvatarSet/Progression/OnGive의owner교체·GC, summon spawn 게시전재진입, Foley IsDead 뒤currentparticipant 미재현.',
         'CA14~18기존수정/회귀·원래avatar만cancel·remotecombat표현/ASC권위·profilelegacyfallback·leaderonlyFoley·진단시점 구분 유지.'),
    unit('CR24', resolve('AudioTests.cpp','GpuTests.cpp','PcgTests.cpp','PhysicsTests.cpp','SchedulingTests.cpp','TickTests.cpp',
                        'AnimationMigrationTests.cpp','ExperimentOutput.h','PresentationAudit.cpp','PresentationMigrationJournal.h',
                        'PresentationMigrationJournalTests.cpp','ProjectJExperimentsModule.cpp','ProjectJPresentationMigrationCommandlet.h',
                        'FeatureCatalog.inl'),
         '기존presentation migration 지정범위와여섯Experiments 전체본문·inline·실행/cleanup·worker/측정의미 대조. 추가 ValueCompute.usf/Build·Measure-Experiments scripts도 읽었으나 현재 source행과분리. 모든실험실행완료 아님.',
         'explicit automation/commandlet→bounded task/solver/PCG/audio/GPU epoch→GT/RT completion→CSV/cleanup. logger/MMO는등록·카테고리뿐, gameplayservice 아님.',
         'GPU shader timing과host_delivery·thread compute sum/wall·blocking wait/idleCPU·NullRHI/NonNullRHI 범위분리. shader source/결과원자료는별도validation.',
         'GPU/Audio device와실제render환경 미검증, plugin기본disabled. PCG fixture4는200캐릭터/운영규모 증거 아님.',
         'GT Ready 관측→RT readback reset→새poll enqueue race 및largefinite distance squared overflow 소스확인·실행미재현. audio completion deadline 환경도미검증.',
         'cancel epoch·bounded admission·worker join/drain·migration digest/preflight·timeout시자원보존 유지. ReadOnly agent는어떤실험도실행하지않음.'),
]

# Exact manifests supplied after actual body reads; never infer completion from
# directory membership, automation declarations, or an old successful roster.
additional_scopes = out / 'AdditionalReadScopes.json'
if additional_scopes.exists():
    units += json.loads(additional_scopes.read_text(encoding='utf-8'))

def finding(id, system, issue, source, validation, status='소스 후보·실행 미재현'):
    return dict(id=id, system=system, issue_ko=issue, source=source, status_ko=status,
                needed_validation_ko=validation, whole_system_complete=False)

findings = [
    finding('D19','탑승/비행','attach retirement·replicated A→B restore·GAS phase tag supersession',
            ['MountCharacter.TryMountRider/AttachRider/OnRep_Rider','FlyingMountCharacter.SetFlightState/ApplyFlightStateTags'],
            '원본3실패+controller 재배정1실패 재현. 관련24집중 23clean/기존warning1 통과. 후속 실제2rider 복제·중첩restore·GC·Game/Cook/시각은 별도.', '실제4실패·수정·집중24통과/통합 미완료'),
    finding('D20','탑승/비행','이전 input clone의 새session 실행·서버protected phase admission·native dead 공급',
            ['Mount/Flying input binding','Mount.ServerRequestDismount','MountAnimInstance.NativeUpdateAnimation'],
            'old clone/new session·phase별 authority/forced dismount·native dead/owner loss 및 실제 ABP 소비'),
    finding('D21','Backend','음수 queue 및 비유한 timeout/flush·큰 zero-delay retry',
            ['Gateway.TrimRemoteLogQueue/DispatchTrackedRequest/Initialize','Handover.ScheduleRetry/DispatchTransfer'],
            '잘못된 설정 승인/큐 재삽입·종료·bounded dispatch 깊이. 기본동작 유지'),
    finding('D22','지속 상태','Progression 존재하지만 Revision0에서 legacy admission·새PS 이관 미설치',
            ['Base.CanApplyCharacterLevelExactly/TrySetCharacterLevel','PlayerState/GameMode/Player handover'],
            'Revision0 cap100+packet101/초기화실패·진짜no-Progression 호환. 동일identity 새PS migration/service는 환경미검증'),
    finding('D23','입력/UI/GAS','release/availability callback의 map·newlease·avatar/prediction/current epoch',
            ['PlayerUI.ReleaseAllSkills/ReleaseSkill/UpdateSkills','SkillInputExecution release/refresh','ASC.AbilityInputTagPressed/Released'],
            'UI 단일/전체 release·실제 EndPlay callback 새 receipt 소실을 재현/수정하고 집중57 통과. availability BP·ASC activation/avatar/prediction·GC 조합은 미재현으로 유지.', 'UI receipt/종료 소유권 실제 정리·나머지 source 후보 미재현'),
    finding('D24','제작 숫자/UI','Minimap failed UV의 미초기화 결과 사용',
            ['Minimap.PositionOnMap/WorldToUV/NativePaint'],
            'NaN/Inf/invalid extent·region/marker 제외와 정상좌표 paint. 현재 authored 오류 주장 아님'),
    finding('D25','NPC/ability','NPCAttack validation/presentation/hit 뒤 old activation 계속 실행',
            ['NPCAttack.ActivateAbility/ValidateAttackContext/OnHit/EndAbility'],
            '기존 actual montage fixture에서 callback cancel/restart+GC·task/notify/hit/end의 원래 epoch만 소비'),
    finding('D26','Inventory/Equipment','commit 알림 뒤 participant 수명·incoming item ownership·unlock receipt·legacy RPC',
            ['Inventory.TryUseItem/TryBagChange','EquipmentManager equip lock/remove/RPC'],
            'use/split/merge/swap callback 종료/교체/GC·item제거·occupied slot 보존·양API 합산burst'),
    finding('D27','grants/layers','GAS callback participant·default source basename·linked lifecycle·ASC unregister/register',
            ['AbilitySet.GiveToAbilitySystem/TakeFromAbilitySystem','EquipmentRuntime grants','Base default grant','AnimationLayerLease'],
            '다른package 동일basename actual grants·OnGive/end/apply GC/newprovider·linked init replacement·engine re-register 정책'),
    finding('D28','무기/리타깃','spawn callback 중 profile identity 변경·기본 grip alpha 승인 공백',
            ['WeaponPresentation.RefreshPresentation/UpdateGripTargets','WeaponPresentationProfile.IsDataValid'],
            'World OnActorSpawned profile/class/body교체·unequip·GC, 기본alpha4 NaN/Inf/범위밖과 유효fractional'),
    finding('D29','locomotion','GAS tag callback 뒤 landing/jump transition·EndPlay timer lease',
            ['Locomotion.Transitions.StartLanding/HandleJumpStarted','Locomotion.EndPlay/OnComponentDestroyed/Support'],
            'tag callback 정상JumpStart·stream EndPlay UObject생존 후 oldtimer·reuse. 원래 authored timer와 physical/presentation revision 보존'),
    finding('D30','MM/Chooser','nullable 일반 Cycle의 무조건 접근·returned table cycle',
            ['AnimInstance.Continuity cycle diagnostic','AnimInstance.Presentation chooser loop','MotionMatchingAssetSet.FindTurnCycleCompanion'],
            '유효 Reface pair/optional ordinary Cycle·fresh native feedback와 returned chooser self/cycle/null/정상64·초과65를 수정 후 검증. 원본 AV/무한루프는 실행하지 않음. 실제 전수 graph/worker/pose 미검증.', 'native 참조/순회 경계 실제 수정·집중검증 통과'),
    finding('D31','제작/에디터','Escape transaction은 값복원이 아닌 기록제거·migration partial save',
            ['CharacterEditorModule.InputKey/FinishTransaction','Foley/EarlyTransition migration'],
            '실제drag/Escape 값·dirty·undo, 여러package 뒷항목실패 partial-save 명시. 이전완료migration 재실행 금지'),
    finding('D32','ability/presentation','Sprint effect 후 게시·Melee callback epoch·cue map reference·hit loop',
            ['Sprint.ActivateAbility/EndAbility','Melee.StartComboNode','CombatPresentationCue.NotifyBegin','MeleeHit.NotifyTick'],
            '실제GE/apply/movement/presentation/hit callback cancel/restart/GC·source change. forceEnd/reset·timer로 숨기지 않음'),
    finding('D33','Significance/운영 설치','거리 Update source미연결·near0/far3 다중view 역집계·worker rawUObject callback',
            ['Base significance registration/callback','USignificanceManager.Update/FManagedObjectInfo'],
            '실제observer/actor이동·다중viewer·없음/종료/GC. GT값snapshot→계산→GT apply 설계. 실제CPU원인/개선량은 미검증'),
    finding('D34','HUD/widget/interaction','press/windowdrag origin·HUD재초기화·dynamicwidget callback·interactorcurrent권위',
            ['SkillButton press/release/Configure','HUD Window/menu drag/LayoutIdentity','InteractionQuery.TryInteract'],
            '원래source의press/drag·newlease보존·실제dragoperation·focus/capture·CanInteract participant GC'),
    finding('D35','실험/숫자','GPU Ready/poll admission race·유한거리 제곱 overflow',
            ['Experiments.GpuTests poll','Project_JMMOTypes.GetMaxReplicationDistanceSquared'],
            '강제GT/RT교대 completion/poll·정상shaderCPU참조, largefinite boundary. 기본disabled 실험과runtimeIris 설치여부분리'),
    finding('D36','초기화/소환/오디오','Base owner/avatar 초기화 chain·summon spawn 게시전 재진입·Foley life gate',
            ['Base.InitializeAbilitySystem','Player.ServerRequestUseMountItem','FoleyComponent/Subsystem.IsDead/Submit/Execute'],
            'OnAvatarSet/OnGive/progression publication 교체+GC·spawn callback재요청/종료·FoleyStop/retire. 기존async inventory재검사 재사용'),
]

data = dict(recorded_utc=datetime.now(timezone.utc).isoformat(), whole_project_complete=False,
            active_scope_ko='최신 요청: 소스·아키텍처 정리. 전체 성능/운영/품질 전수 감사는 보류',
            current_checkpoint='R15_StructureCleanupVerification1/StructureCleanupVerification.json',
            source_review_track=[dict(unit=u['id'], scope_ko=u['scope_ko'],
                                      state_ko='명시 소스 범위 대조·관련 에셋/실행/환경 미완료') for u in units],
            defect_execution_track=findings,
            remaining_ko=['정확 manifest에 통합한 native 본문 공백 마감과 향후 변경/범위 밖의 동적 호출은 구분',
                          '모든 실제 BP/ABP/DA/DT/Chooser/IA/IMC graph 대조 및 제작/검증/배포 경계',
                          '원래 CPU 증가·렌더 차이·손/cloth/retarget 품질·200 real connections/live backend',
                          '기존 CoverageLedger의 미검토 행과 범위 밖 함수 전체'])
(out / 'ParallelReviewUnits.json').write_text(json.dumps(units, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
(out / 'ContinuationWorkQueues.json').write_text(json.dumps(data, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({'explicit_parallel_units':len(units), 'defect_groups':len(findings), 'whole_project_complete':False}))
