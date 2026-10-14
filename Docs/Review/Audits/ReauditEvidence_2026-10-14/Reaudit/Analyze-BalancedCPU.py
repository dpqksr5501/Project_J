from pathlib import Path
from collections import defaultdict
import json, csv, statistics
import sys
root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
prefix = sys.argv[1] if len(sys.argv)>1 else 'Reaudit_ABBA_Long'
measured_ticks = int(sys.argv[2]) if len(sys.argv)>2 else 600
warmup_ticks = int(sys.argv[3]) if len(sys.argv)>3 else 240
suffix = '' if prefix == 'Reaudit_ABBA_Long' else '_'+prefix
rows=[]
groups=defaultdict(list)
for phase in ['A1','B1','B2','A2']:
    p=root/f'Saved/Validation/AuthoredAnimation_20261005/{prefix}_{phase}/Count200/cpu.json'
    result=json.loads(p.read_text(encoding='utf-8-sig'))
    assert result['completed'] and result['fixture_version']==2 and len(result['rows'])==16
    for r in result['rows']:
        assert r['count']==200 and r['measured_ticks']==measured_ticks and r['warmup_ticks']==warmup_ticks
        groups[(r['scenario'],r['aba'],phase[0])].append((phase,r))
        rows.append(dict(phase=phase,scenario=r['scenario'],aba=r['aba'],repeat=r['repeat'],
            mean_world_tick_ms=r['mean_world_tick_ms'],p50_world_tick_ms=r['p50_world_tick_ms'],
            p95_world_tick_ms=r['p95_world_tick_ms'],max_world_tick_ms=r['max_world_tick_ms'],
            mean_observer_ms=r['mean_observer_ms'],measured_ticks=r['measured_ticks'],
            full_200_montage_ticks=sum(n==200 for n in r['attack_montage_actor_counts_after_tick'])))
summary=[]
for scenario,aba,_ in sorted({(s,a,'') for s,a,v in groups}):
    variants={}
    for variant in ['A','B']:
        data=groups[(scenario,aba,variant)]
        values=[r['mean_world_tick_ms'] for p,r in data]
        run_groups=defaultdict(list)
        for p,r in data: run_groups[p].append(r['mean_world_tick_ms'])
        variants[variant]=dict(mean_ms=statistics.mean(values),repeat_means_ms=values,
            run_means_ms={p:statistics.mean(v) for p,v in run_groups.items()},
            minimum_repeat_ms=min(values),maximum_repeat_ms=max(values))
    summary.append(dict(scenario=scenario,aba=aba,variants=variants,
        delta_percent=(variants['B']['mean_ms']/variants['A']['mean_ms']-1)*100))
with (out/('CPUComparisonRows'+suffix+'.csv')).open('w',encoding='utf-8-sig',newline='') as f:
    writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
(out/('CPUComparisonSummary'+suffix+'.json')).write_text(json.dumps(dict(
    order=['A1','B1','B2','A2'],rows=len(rows),source_baseline='after DS01-18, before RA01-13',
    scope=f'NullRHI world tick of 200 real authored BP actors; 8 conditions, 2 repeats/run, {warmup_ticks} warmup/{measured_ticks} measurement ticks. Observer time remains included. No trace overhead in this comparison. Run-to-run variation retained; not statistical proof of full performance equivalence.',
    comparisons=summary),ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(summary,ensure_ascii=False))
attack_windows=[]
for aba in [False,True]:
 for window in ['full_200_montages','after_montage']:
  variants={}
  for variant in ['A','B']:
   samples=[]
   for phase,r in groups[('AttackBurst',aba,variant)]:
    samples.extend(ms for ms,n in zip(r['world_tick_samples_ms'],r['attack_montage_actor_counts_after_tick'])
                   if (n==200 if window=='full_200_montages' else n==0))
   variants[variant]=dict(samples=len(samples),mean_ms=statistics.mean(samples))
  attack_windows.append(dict(aba=aba,window=window,variants=variants,
   delta_percent=(variants['B']['mean_ms']/variants['A']['mean_ms']-1)*100))
(out/('CPUAttackWindowSummary'+suffix+'.json')).write_text(json.dumps(dict(
 scope='Post-tick observed montage occupancy partitions; the final boundary tick includes work before occupancy changed. AttackBurst has one authored burst, not a continuously sustained attack workload. All A/B variants use the same occupancy sequence.',
 windows=attack_windows),ensure_ascii=False,indent=2),encoding='utf-8')
