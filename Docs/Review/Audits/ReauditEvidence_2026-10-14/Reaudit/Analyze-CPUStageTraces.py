from pathlib import Path
import csv,json,re
root=Path(__file__).resolve().parents[3]
out=Path(__file__).resolve().parent
trace_root=root/'Saved/Validation/AuthoredAnimation_20261005'
stages={
 'gt_proxy_preupdate':['ProjectJ_AnimProxy_PreUpdate_GameThread'],
 'gt_anim_update':['ProjectJ_AnimProxy_Update_GameThread','ProjectJ_AnimThreadSafeUpdate_GameThread'],
 'worker_proxy_update':['ProjectJ_AnimProxy_Update_Worker','ProjectJ_AnimThreadSafeUpdate_Worker'],
 'snapshot_consumption':['ProjectJ_AnimProxy_ConsumeSnapshot'],
 'mm_and_playback':['ProjectJ_UnifiedLocomotion','ProjectJ_AnimProxy_ApplyDatabase','ProjectJ_AnimProxy_ApplySearchPolicy'],
 'post_selection':['ProjectJ_AnimProxy_CapturePostSelection','ProjectJ_AnimProxy_CompleteReselects'],
 'retarget_capture_and_math':['ProjectJ_RetargetBody_Snapshot','ProjectJ_RetargetProfile_PreUpdate','ProjectJ_RetargetBody_ContactMath'],
 'visual_budget':['ProjectJ_VisualAnimationBudget','ProjectJ_CharacterABA_Policy'],
 'rigid_body':['FAnimNode_RigidBody::EvaluateSkeletalControl_AnyThread','RigidBody_PreUpdate'],
}
def read(path):
 with path.open(encoding='utf-8-sig',newline='') as f:return {r['Name']:r for r in csv.DictReader(f)}
def stats(rows):
 return {s:dict(exclusive_cpu_seconds=sum(float(rows[n]['Excl']) for n in names if n in rows),
                   calls=sum(int(rows[n]['Count']) for n in names if n in rows)) for s,names in stages.items()}
summary={'scope':'A/B separate traced runs, 200 actors, 120 warmup/measurement ticks, 2 repeats. Timer totals are accumulated thread CPU; nested exclusive events are grouped once. Inclusive totals overlap and must not be summed. Worker CPU totals are not the world-tick critical-path wall time. Trace overhead prevents these runs from substituting for no-trace ABBA.',
 'timer_definitions':stages,'whole_trace':{},'regions':[]}
exports={}
for v in ['A','B']:
 folder=trace_root/f'Reaudit_Trace_{v}_trace/Count200/TraceExport'
 exports[v]=folder
 summary['whole_trace'][v]=stats(read(folder/'AllTimers.csv'))
pattern=re.compile(r'Timers-ProjectJ\.AuthoredCPU_200_(.+)_ABA([01])_R([01])\.csv$')
for p in sorted(exports['A'].glob('Timers-ProjectJ.AuthoredCPU_*.csv')):
 m=pattern.fullmatch(p.name)
 if not m:continue
 scenario,aba,repeat=m.groups()
 entry=dict(scenario=scenario,aba=aba=='1',repeat=int(repeat),variants={})
 for v in ['A','B']:
  rows=read(exports[v]/p.name)
  entry['variants'][v]=stats(rows)
  world=rows.get('ProjectJ_AuthoredAnimation_WorldTick')
  entry['variants'][v]['world_tick_wall_from_scope']=dict(calls=int(world['Count']),inclusive_seconds=float(world['Incl'])) if world else None
 summary['regions'].append(entry)
(out/'CPUStageTraceSummary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(dict(regions=len(summary['regions']),whole_trace=summary['whole_trace']),ensure_ascii=False))
