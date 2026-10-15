from pathlib import Path
from collections import defaultdict
import csv
import json
import statistics
import sys

root = Path(__file__).resolve().parents[2]
name = sys.argv[1]
out = root / 'Saved/Validation/Continuation_20261014' / name
if (out/'CPUComparisonSummary.json').exists() or (out/'CPUComparisonRows.csv').exists():
    raise RuntimeError('Preserve prior comparison evidence')
manifest = json.loads((out / 'ComparisonManifest.json').read_text(encoding='utf-8-sig'))
groups = defaultdict(list)
rows = []
for phase in manifest['order']:
    result = json.loads((root / f'Saved/Validation/AuthoredAnimation_20261005/{name}_{phase}/Count200/cpu.json').read_text(encoding='utf-8-sig'))
    assert result['completed'] and result['fixture_version'] == 2 and len(result['rows']) == 16
    for row in result['rows']:
        assert row['count'] == 200 and row['measured_ticks'] == manifest['measuredTicks'] and row['warmup_ticks'] == manifest['warmupTicks']
        groups[(row['scenario'], row['aba'], phase[0])].append((phase, row))
        selected = {k:row[k] for k in ['scenario','aba','repeat','mean_world_tick_ms','p50_world_tick_ms','p95_world_tick_ms','mean_observer_ms',
                                      'bone_finalizations','mm_result_observations','visual_graph_update_observations',
                                      'visual_group_requested_actor_ticks','visual_group_managed_mesh_ticks','attack_dispatch_and_validation_ms']}
        selected['phase'] = phase
        selected['full_200_montage_ticks'] = sum(v == 200 for v in row['attack_montage_actor_counts_after_tick'])
        rows.append(selected)
comparisons = []
for scenario, aba in sorted(set((s,a) for s,a,v in groups)):
    variants = {}
    for variant in ['A','B']:
        data = groups[(scenario,aba,variant)]
        means = [row['mean_world_tick_ms'] for phase,row in data]
        variants[variant] = {'mean_ms':statistics.mean(means),'repeat_means_ms':means,
                             'run_means_ms':{phase:statistics.mean(row['mean_world_tick_ms'] for p,row in data if p == phase) for phase in sorted(set(p for p,row in data))},
                             'workload_observations':{k:[r[k] for p,r in data] for k in ['bone_finalizations','mm_result_observations','visual_graph_update_observations','visual_group_requested_actor_ticks','visual_group_managed_mesh_ticks']}}
    comparisons.append({'scenario':scenario,'aba':aba,'variants':variants,
                        'delta_percent':(variants['B']['mean_ms']/variants['A']['mean_ms']-1)*100})
receipt = json.loads((out / 'RestoreReceipt.json').read_text(encoding='utf-8-sig'))
assert receipt['restored']
summary = {'scope_ko':'명시한 compiled source와 전체9 산출물의 A/B, 200 authored BP actors·NullRHI·원격 socket0. 4run×16조건=64행. 실제 MM Search 호출은 관찰 field로 대체하지 않고 trace로 별도 검증한다.',
           'variant_artifact_manifest':manifest,
           'order':manifest['order'],'rows':len(rows),'trace':manifest['trace'],'restored':receipt['restored'],
           'meaning_ko':'반복/교대와 실제 수행량을 함께 기록. 모든 CPU 회귀 해결·통계적 동등성·실제 렌더 FPS/GPU의 증명이 아니다.',
           'comparisons':comparisons}
(out / 'CPUComparisonSummary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
with (out / 'CPUComparisonRows.csv').open('w',encoding='utf-8-sig',newline='') as file:
    writer = csv.DictWriter(file,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
print(json.dumps([{'scenario':c['scenario'],'aba':c['aba'],'A_ms':c['variants']['A']['mean_ms'],'B_ms':c['variants']['B']['mean_ms'],'delta_percent':c['delta_percent']} for c in comparisons]))
