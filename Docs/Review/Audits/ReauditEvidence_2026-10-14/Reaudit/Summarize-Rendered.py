from pathlib import Path
import csv,json,statistics
root=Path(__file__).resolve().parents[3]
out=Path(__file__).resolve().parent
folder=root/'Saved/Validation/WholeProjectAudit_20261013/Reaudit_Render1'
with (folder/'frames.csv').open(encoding='utf-8-sig',newline='') as f:rows=list(csv.DictReader(f))
groups={f'phase_{p}':[r for r in rows if r['phase']==str(p)] for p in [1,2,3]}
groups['attack_phase_full_200_montages']=[r for r in groups['phase_3'] if int(r['montages'])==200]
groups['attack_phase_no_montages']=[r for r in groups['phase_3'] if int(r['montages'])==0]
summary={}
for label,values in groups.items():
    times=[float(r['frame_delta_ms']) for r in values]
    gpu=[float(r['gpu_ms']) for r in values]
    summary[label]=dict(frames=len(values),mean_frame_ms=statistics.mean(times),fps_from_mean_frame=1000/statistics.mean(times),
        mean_gpu_ms=statistics.mean(gpu),full_200_montage_frames=sum(int(r['montages'])==200 for r in values),
        rendered_200_frames=sum(int(r['rendered_actors'])==200 for r in values))
(out/'RenderedSummary.json').write_text(json.dumps(dict(scope='Development, real D3D12/offscreen 1280x720, uncapped, VSync0, 200 actual authored BP/retarget bodies, 0 socket connections. Simplified camera/light scene. Phase3 contains one R burst and its return, not sustained full200 attacks. Capture/stalls included; GPU counters may be delayed. No pre-change matched comparison.',groups=summary),indent=2),encoding='utf-8')
print(json.dumps(summary))
