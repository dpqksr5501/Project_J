from pathlib import Path
import csv
import json
import sys

root = Path(__file__).resolve().parents[2]
name = sys.argv[1]
stages = {
 'gt_preupdate':['ProjectJ_AnimProxy_PreUpdate_GameThread'],
 'gt_animation':['ProjectJ_AnimProxy_Update_GameThread','ProjectJ_AnimThreadSafeUpdate_GameThread'],
 'worker_animation':['ProjectJ_AnimProxy_Update_Worker','ProjectJ_AnimThreadSafeUpdate_Worker'],
 'snapshot':['ProjectJ_AnimProxy_ConsumeSnapshot'],
 'mm_playback':['ProjectJ_UnifiedLocomotion','ProjectJ_AnimProxy_ApplyDatabase','ProjectJ_AnimProxy_ApplySearchPolicy'],
 'post_selection':['ProjectJ_AnimProxy_CapturePostSelection','ProjectJ_AnimProxy_CompleteReselects'],
 'retarget':['ProjectJ_RetargetBody_Snapshot','ProjectJ_RetargetProfile_PreUpdate','ProjectJ_RetargetBody_ContactMath'],
 'aba_visual':['ProjectJ_VisualAnimationBudget','ProjectJ_CharacterABA_Policy'],
 'rigid_body':['FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread','RigidBody_PreUpdate'],
 'ability':['TickAbilityTasks','FindAbilitySpecFromHandle'],
 'weapon_spawn':['ProjectJ_WeaponPresentation_Refresh','ProjectJ_WeaponPresentation_Spawn','ProjectJ_WeaponPresentation_Destroy'],
 'combat_vfx':['ProjectJ_CombatPresentation_Spawn','ProjectJ_CombatVFX_StopCueLocal'],
 'worker_wait':['WaitForTasks'],
 'visual_role':['ProjectJ_VisualFollowerResolve','ProjectJ_VisualDemandResolve'],
 'follower_tick':['ProjectJ_FollowerTickPolicy'],
 'weapon_notify':['ProjectJ_WeaponMotionNotify_Begin','ProjectJ_WeaponMotionNotify_Tick','ProjectJ_WeaponMotionNotify_End'],
 'notify_weak_prune':['ProjectJ_WeaponMotionNotify_WeakMeshPrune','ProjectJ_GroundContactNotify_WeakMeshPrune'],
 'contact_notify':['ProjectJ_GroundContactNotify_Begin','ProjectJ_GroundContactNotify_End','ProjectJ_TwoHandIKNotify_Begin','ProjectJ_TwoHandIKNotify_End'],
}
query_names = ['PoseSearch PCA/KNN','STAT_PoseSearch_BuildQuery','STAT_PoseSearch_ContinuingPose','STAT_PoseSearch_UpdateAssetPlayer']
def read(path):
    with path.open(encoding='utf-8-sig',newline='') as file:
        return {r['Name']:r for r in csv.DictReader(file)}
def stats(rows):
    return {'stages':{stage:{'exclusive_thread_seconds':sum(float(rows[n]['Excl']) for n in names if n in rows),
                             'calls':sum(int(rows[n]['Count']) for n in names if n in rows)} for stage,names in stages.items()},
            'database_query_timers':{n:{'calls':int(rows[n]['Count']),'exclusive_thread_seconds':float(rows[n]['Excl'])} for n in query_names if n in rows},
            'world_tick':{k:rows['ProjectJ_AuthoredAnimation_WorldTick'][k] for k in ['Count','Incl','Excl']} if 'ProjectJ_AuthoredAnimation_WorldTick' in rows else None}
folders = {variant:root / f'Saved/Validation/AuthoredAnimation_20261005/{name}_{variant}_trace/Count200/TraceExport' for variant in ['A','B']}
result={'scope_ko':'별도 A/B trace. 200 actor·120 warmup/120 measured ticks·2반복. exclusive thread elapsed duration을 그룹화. CPU profiler timer는 OS 실제 CPU 사용 시간과 다르며 특히 WaitForTasks는 대기 경과시간이다. worker 누적/대기/inclusive 중첩을 world critical path로 더하지 않는다.',
        'timers':stages, 'query_meaning_ko':'PCA/KNN=실제 database 검색 경로, BuildQuery=실제 query 생성. MM result observations나 node update를 Search 호출로 계산하지 않는다. 개별 notify Begin/Tick/End 방문량은 기존 trace 범위에서 아직 분리되지 않음.',
        'whole_trace':{v:stats(read(p/'AllTimers.csv')) for v,p in folders.items()},'regions':[]}
for path in sorted(folders['A'].glob('Timers-ProjectJ.Authored*.csv')):
    other=folders['B']/path.name
    if not other.exists():
        raise RuntimeError('Missing comparable region')
    result['regions'].append({'region':path.stem,'variants':{v:stats(read(folders[v]/path.name)) for v in folders}})
out=root/'Saved/Validation/Continuation_20261014'/name/'StageTraceSummary.json'
out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'regions':len(result['regions']),'whole_trace':result['whole_trace']},ensure_ascii=False))
