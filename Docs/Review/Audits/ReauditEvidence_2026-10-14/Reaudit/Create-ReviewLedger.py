"""Explicit review record, never promotes inventory or tests to whole integrity."""
from pathlib import Path
import hashlib,json

root=Path(__file__).resolve().parents[3]
out=Path(__file__).resolve().parent
old=json.loads((out/'PreviousSourceReviewLedger.json').read_text(encoding='utf-8-sig'))
pending={}
for entry in old['files']:
    if entry['review']=='inventory_only' and entry['kind']=='production_or_tooling' and entry['path'].endswith('.cpp'):
        pending.setdefault(entry['module'],[]).append(entry['path'])
for paths in pending.values(): paths.sort()
# Set only after the relevant text was actually inspected, including rereading truncated ranges.
unread_character=set()
read={module:set(range(len(paths))) for module,paths in pending.items()}
read['Project_JCharacter']-=unread_character
scopes={
    'Project_JCharacterUIBindingComponent.cpp':'RA01: actual ASC/delegate lease, revision publication, native FieldNotify reentry/GC/retirement. WholeRA04 compiled behavior.',
    'Project_JCombatPresentationComponent.cpp':'RA02/10: instance retirement, detached/pinned cue maps and fresh BeginPlay admission. WholeFinal1 and RHICosmetics1 actual RHI pool passed.',
    'Project_JMinimapWidget.cpp':'RA03: actual TakeWidget reconstruction, definition/world rejection, icon request generation and exact timer-world release.',
    'Project_JInventoryViewModel.cpp':'RA06: actual widget projection now shares command SourceRevision admission. Stale compare/tooltip mismatches reproduced; WholeFinal1 passed.',
    'Project_JUILayoutSettings.cpp':'RA07: shutdown admission/private final flush and callback revision/liveness boundary; baseline late storage writes reproduced, fake storage protects user saves. WholeFinal1 passed.',
    'Project_JMountComponent.cpp':'RA05/10: public RequestMount -> TryMountRider -> SetMountedMount -> tag callbacks -> Dismount/new request. Baseline count2/leaked1; revision lease/retirement/play reuse implemented; WholeFinal1 passed.',
    'Project_JMountCharacter.cpp':'RA05 confirmed: old dismount restores captured movement over new mount. Restore now rechecks current rider relationship after callbacks and possession.',
    'Project_JCharacterAnimInstanceBase.cpp':'RA08: detached live follower cache reproduced, replaced duplicate tree/name scans with common attached-role resolver. MCP/public template and actual authored BP construction differ in evidentiary scope; WholeFinal1 passed.',
    'Project_JCharacterAnimInstance.SnapshotAccessors.cpp':'Worker-facing reads use proxy value/external snapshots; final BlendStack sample identity owns authored correction. UObject references require existing proxy GC ownership; no live actor/profile traversal in these getters.',
    'Project_JCharacterAnimInstance.UpperBodyFacing.cpp':'GT captures profile/movement and passes value-only inputs to upper/lower worker math; existing local/run/turn/action bypass ownership retained.',
    'Project_JAnimationUpdateCoordinatorComponent.cpp':'Wake/urgent requests already compose in RefreshSourceRequirement and releases guard the other lease. Initial duplicate-request candidate rejected; no change.',
    'Project_JMassRepresentationSubsystem.cpp':'Authority/GT ownership transitions, exact captured controls, copied route fragments, bounded transition cursor, worker join before applying actors. Reentrant deinitialize storage-release concern is potential; current public call evidence insufficient.',
    'Project_JCharacterMovementComponent.cpp':'SavedMove sprint bit, combine boundary, server legality, replay acceleration policy and common player publisher; valid physics behavior retained.',
    'Project_JNetObjectFilter_Distance.cpp':'Viewer/target social and owner relevance, configured distance decision. Native policy helper is not proof of active Iris filter registration or 200 connections.',
    'Project_JNetObjectPrioritizer_Combat.cpp':'Native GAS-state priority wrapper inspected; active network registration must be distinguished from helper availability.',
    'Project_JFoleySubsystem.cpp':'World-local listener snapshots, bounded shared async resources, queue admission/age/budget/trace and shutdown cancel. No project AudioComponent pool; audible quality remains separate.',
    'Project_JMotionMatchingTrajectoryComponent.cpp':'Single GT producer removes engine example duplicate binding; generation/history/postprocess frame gates, mount/server exclusion, age freshness, profile yaw filtering and remote history/facing preservation inspected. No broad history reset added.',
    'Project_JWeaponPresentationComponent.Diagnostics.cpp':'Optional GT diagnostics sample configuration and source/visible/target/pose/motion frame provenance; no search/input/physics mutation. Existing runtime vs diagnostic split retained; no claim all visual quality inspected.',
    'Project_JWeaponPresentationProfile.cpp':'RA11: NaN TraceLength accepted by native validator reproduced; shared enabled-contact settings contract and runtime endpoint admission implemented. WholeFinal1 passed; no pre-fix physics crash claimed.',
    'Project_JStateControllerRuntime.h':'Additional inline scope: full hold/consumed-command/pivot/turn-sequence policy read, callers in GT presentation/snapshot compared. Sole transition predicate retained by RA13; not proof of every generated BP branch.',
    'Project_JGeneralTurnPolicy.h':'Additional inline scope: full bounded geometry/candidate window and completed-selection feedback read; prospective database-family/cycle identity callers inspected. Preserved, not a new change.',
    'Project_JMovingTurnPolicy.h':'Additional inline scope: full running origin/preparation/admission/completion and copy-only diagnostics read. Physical geometry owner differs from State Controller hold; preserved.',
    'Project_JContinuousCurvePolicy.h':'Additional inline scope: full finite input/settings, bounded ring, signed physical/intent proof and routing grace read. .35/100/3/.85 unchanged.',
    'Project_JTurnEventSettings.h':'Additional inline scope: resolved finite bounded settings and dependent angle clamps read; preserved.',
    'Project_JAnimationPresentationHelpers.h':'RA13: complete helper body read; identical five-state transition lambda had three real GT callers. Removed duplicate predicate in favor of State Controller; sector/state-routing semantics preserved.',
    'Project_JHandoverManager.h':'Additional header scope: outgoing/incoming envelope, bounded replay/capacity/admission, transport/ticker owner and exposed APIs read. Related state callback/map re-query reviewed; live infrastructure remains unverified.',
    'Project_JOneShotInputResponse.h':'Additional inline scope: complete camera-frame key compatibility and steering error policy read, actual local presentation caller and raw profile cancel-angle inputs compared. Malformed entry/tuning numeric contract is a potential gap, not current asset failure.',
    'Project_JOneShotCurvePolicy.h':'Additional inline scope: delegates sustained history to ContinuousCurvePolicy, preserves bounded fast curve evidence and future sign agreement. Actual GT shot-owner/exit readiness caller read; preserved.',
    'Project_JBackwardRefacePolicy.h':'Additional inline scope: complete finite/fresh visual admission, bounded active window, completed-selection Cycle handoff and continuation read. Different candidate geometry from forward turns; preserved.',
    'Project_JPlaybackReturnPolicy.h':'Additional inline scope: complete value-only overlap/continuation predicates and inertial blend lifetime read. Physical landing/input is immutable, engine inertialization owns deficit; preserved.',
    'Project_JMotionMatchingSelectionPolicy.h':'Additional inline scope: complete semantic FKey publication and redirect reselect cooldown read. Finite tuning/clock assumptions remain caller-specific; no broad reset or timer added.',
    'Project_JTurnRequestSample.h':'Additional inline scope: full frame/time/mode/gait freshness and finite GT sample vs completed selection/visual feedback read. Evidence ages are not duplicate pose ownership; preserved.',
    'Project_JNPCActionComponent.h':'Additional header scope: complete server-only start/stop ownership, suspension/resume result, weak action context, path and ability handles, bounded retry timer fields read. Actual cancellation preserves noncancellable GAS ownership.',
    'Project_JNetObjectFilter_Distance.h':'Additional complete header scope: UObject policy helper, not UNetObjectFilter adapter. Future commented hooks are not registered Iris filtering. CPP decisions and Config/default Iris fixture distinguished; preserved.',
    'Project_JNetObjectPrioritizer_Combat.h':'Additional complete header scope: UObject combat multiplier helper, not active Iris prioritizer. CPP tag priority calculations preserved; interface presence is not active network implementation.',
    'Project_JCombatHitValidationComponent.h':'Additional complete header scope: attack/weapon revision, window tokens, bounded server sweep history, request sequence/rate, authoritative/predicted/SSR APIs read. Actual authority damage and current network input pass; full remote SSR remains unverified.',
    'Project_JServerSideRewindComponent.h':'Additional complete header scope: server sampled capsule geometry ring, discontinuity reset, rate/lifetime and public verify API read. Capsule rewind differs from weapon-sweep history; no duplicate owner merged.',
    'Project_JCombatHitValidation.h':'Additional complete header/inline scope: request and policy structs, failure mapping, accepted/rejected result constructors read. Native request validation contracts differ from current live remote testing.',
    'Project_JEquipmentRuntimeComponent.h':'Additional complete header scope: per-slot actual ASC grant handles, visual lease/revision/retry and provider binding ownership read. BeginPlay already reopens runtime admission; potential reuse gate candidate rejected.',
    'Project_JEquipmentManagerComponent.h':'Additional complete header/inline scope: PlayerState aggregate, FastArray callbacks, authority/owner RPCs, receipts/rate, slot commit lock and retirement read. BeginPlay already reopens aggregate and owner serializer; preserved.',
    'Project_JInventoryComponent.h':'Additional complete header/inline scope: sole replicated inventory, read-only UI value snapshot, observed-count/order bag requests, bounded receipts/rate/cooldown and lifecycle admission read. BeginPlay already reopens; preserved.',
    'Project_JAbilitySet.h':'Additional complete header scope: handle arrays vs keyed source generations, mutation revision and grant/take APIs read against existing DS10 source ownership. No second keyed handle owner introduced.',
    'Project_JCombatStyleDefinition.h':'Additional complete header scope: aggregation root, execution-change policy, inline entries vs derived catalog and transient duplicate cache/authoring invalidation read. Optional authoring reduces DA duplication; preserved.',
    'Project_JMassRepresentationSubsystem.h':'Additional complete header scope: explicit server movement handoff, world/generation token, private entity manager and joined processor, bStepping/admission fields read. Public Deinitialize/storage release vs active step callback is being prepared for native reproduction; not yet fixed.',
    'Project_JItemInstanceTypes.h':'Additional complete header/inline scope: authoritative stable item identity, stack/level/lock/equipped/order and minimum validity read. BagOrder is ordering, not ownership/capacity. Preserved.',
    'Project_JItemDefinition.h':'Additional complete header scope: immutable item identity/text/soft icon and max stack data read. Declaration presence is not every authored item validation; preserved.',
    'Project_JEquipmentItemDefinition.h':'Additional complete header scope: independent logical slot/visual mesh/socket/draw policy, actual ASC ability/effect policy, optional stat compatibility and combat vs visual profile read. Preserved.',
    'Project_JCombatCommandSet.h':'Additional complete header/inline scope: input alias sequence/result/conditions differs from active skill ComboDefinition and GAS costs/cancel ownership. Bounded history declarations and caller boundary read; preserved.',
    'Project_JCharacterClassDefinition.h':'Additional complete header scope: class/advancement graph requirements, exclusivity and grant policy, optional gameplay/style vs VFX override read. Allows cosmetics without duplicated combat styles; preserved.',
    'Project_JComboDefinition.cpp':'Exact-tag lookup, owner-tag transition/start selection and native graph definition validation. Duplicate transition inputs reject; source reading does not prove every authored branch/input timing.',
    'Project_JPresentationBudgetSubsystem.cpp':'GT FIFO admission token differs from component revision; coalescing/cancel/re-request cannot reclaim old slot. Bounded visits/applications/wall budget, remove before external component apply, lifecycle stop retained.',
    'Project_JCombatStyleDefinition.cpp':'Authored/derived catalog validation, class/advancement/equipment source rules, transient runtime cache invalidation. Source reading is not every asset graph verification.',
    'Project_JStrafeLowerBodyFacing.cpp':'Shared upper-body policy math, value-only local input/run/cycle/contact gates, two-bone solve preserves evaluated foot target. Defaults retained.',
    'Project_JStrafeUpperBodyFacing.cpp':'Finite quaternion/twist/yaw, turn ownership, profile-controlled correction/release and bounded history step. Not clip-specific exceptions.',
}
supplement=out/'AdditionalReviewScopes.json'
if supplement.exists():
    scopes.update(json.loads(supplement.read_text(encoding='utf-8-sig'))['scopes'])
records=[]
for entry in old['files']:
    record=dict(entry)
    path=root/entry['path']
    record['current_sha256']=hashlib.sha256(path.read_bytes()).hexdigest() if path.exists() else None
    record['previous_review']=entry['review']
    record['additional_body_read']=False
    module=entry['module']
    if entry['path'] in pending.get(module,[]):
        index=pending[module].index(entry['path'])
        record['additional_body_read']=index in read[module]
        record['bounded_reader_index']=index
    if path.name in scopes:
        record['reaudit_scope']=scopes[path.name]
    record['whole_file_manual_review_claimed']=False
    record['validation_is_whole_integrity_claim']=False
    records.append(record)
for path in root.joinpath('Source').rglob('*Reaudit*Tests.cpp'):
    records.append({'path':path.relative_to(root).as_posix(),'kind':'test_or_fixture',
                    'current_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                    'additional_body_read':True,'reaudit_scope':'Targeted native regression fixture only; compiled/executed version must be matched to its build record.',
                    'whole_file_manual_review_claimed':False,'validation_is_whole_integrity_claim':False})
payload={'meaning_ko':'파일 목록, 본문 읽기, 명명된 계약 대조, 실제 변경, 실행 검증은 서로 다르다. body_read는 전체 호출/헤더/에셋/모든 분기의 무결성 또는 전수 정밀 완료가 아니다.',
         'pending_implementation_total':sum(map(len,pending.values())),
         'additional_pending_implementation_bodies_read':sum(map(len,read.values())),
         'still_unread_pending_bodies':[paths[i] for module,paths in pending.items() for i in range(len(paths)) if i not in read[module]],
         'files':records}
(out/'SourceReviewLedger.json').write_text(json.dumps(payload,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in payload.items() if k!='files'},ensure_ascii=False))
