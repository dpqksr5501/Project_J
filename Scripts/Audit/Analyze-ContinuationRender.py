from pathlib import Path
import csv
import json
import statistics
import sys

root=Path(__file__).resolve().parents[2]
name=sys.argv[1]
out=root/'Saved/Validation/Continuation_20261014'/name
order=['A1','B1','B2','A2']
run_groups={}
def summary(rows):
    times=[float(r['frame_delta_ms']) for r in rows]
    gpu=[float(r['gpu_ms']) for r in rows]
    ordered=sorted(times)
    return {'frames':len(rows),'mean_frame_ms':statistics.mean(times),'median_frame_ms':statistics.median(times),
            'p95_frame_ms':ordered[min(len(ordered)-1,int(.95*(len(ordered)-1)))],
            'fps_from_mean_frame':1000/statistics.mean(times),'mean_gpu_ms':statistics.mean(gpu),
            'capture_requested_frames':sum(int(r['capture_requested']) for r in rows),
            'rendered_200_frames':sum(int(r['rendered_actors'])==200 for r in rows)}
for phase in order:
    folder=root/f'Saved/Validation/WholeProjectAudit_20261013/{name}_{phase}'
    with (folder/'frames.csv').open(encoding='utf-8-sig',newline='') as file:rows=list(csv.DictReader(file))
    full=[r for r in rows if int(r['phase'])==3 and int(r['montages'])==200]
    returned=[r for r in rows if int(r['phase'])==3 and int(r['montages'])==0]
    assert full and returned and all(int(r['actors'])==200 for r in rows)
    run_groups[phase]={'full_200_montages':summary(full),'after_montage':summary(returned)}
comparisons=[]
for window in ['full_200_montages','after_montage']:
    variants={}
    for variant in ['A','B']:
        runs=[run_groups[p][window] for p in order if p.startswith(variant)]
        variants[variant]={'mean_of_run_frame_means_ms':statistics.mean(r['mean_frame_ms'] for r in runs),
                           'mean_of_run_gpu_means_ms':statistics.mean(r['mean_gpu_ms'] for r in runs), 'runs':runs}
    comparisons.append({'window':window,'variants':variants,
                        'delta_frame_percent':(variants['B']['mean_of_run_frame_means_ms']/variants['A']['mean_of_run_frame_means_ms']-1)*100})
receipt=json.loads((out/'RestoreReceipt.json').read_text(encoding='utf-8-sig'));assert receipt['restored']
result={'scope_ko':'명시한 compiled manifest/전체9파일의 A 대 B Editor/game-mode D3D12 offscreen·1280×720·실제 body200·socket0. ABBA 동일 조건. 이전 packaged Render2/3과 실행 host가 달라 직접 회귀로 합산하지 않는다.',
        'limits_ko':'각 run의 one R burst와 복귀. capture/stall 포함; delayed GPU counters. 열·스케줄링·모든 손발/의상/장비 품질 보증 아님. run means를 동일 가중치로 비교하며 지속200 공격으로 승격하지 않음.',
        'order':order,'runs':run_groups,'comparisons':comparisons,'restored':receipt['restored']}
if (out/'RenderComparisonSummary.json').exists(): raise RuntimeError('Preserve prior render summary; add a version clarification companion instead.')
(out/'RenderComparisonSummary.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps([{'window':c['window'],'A_ms':c['variants']['A']['mean_of_run_frame_means_ms'],'B_ms':c['variants']['B']['mean_of_run_frame_means_ms'],'delta_percent':c['delta_frame_percent']} for c in comparisons]))
