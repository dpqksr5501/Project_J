"""Summarize an engine CSV, including its appended final header and late columns."""
import argparse,csv,json,math,statistics
from pathlib import Path
P=argparse.ArgumentParser(description=__doc__);P.add_argument('csv',type=Path);P.add_argument('--context',type=Path);P.add_argument('--output',required=True,type=Path);args=P.parse_args()
rows=list(csv.reader(args.csv.open(encoding='utf-8-sig')))
headers=[r for r in rows if r and r[0]=='EVENTS'];header=headers[-1]
assert all(header[:len(h)]==h for h in headers),'Unexpected changing CSV column order'
data=[r for r in rows if r and r[0]!='EVENTS' and len(r)>header.index('FrameTime') and not r[0].startswith('[')]
def stats(key):
    i=header.index(key);values=[]
    for r in data:
        if i>=len(r) or not r[i].strip():continue
        try:v=float(r[i])
        except ValueError:continue
        if math.isfinite(v):values.append(v)
    ordered=sorted(values)
    if not ordered:return dict(samples=0,missing=len(data),mean=None,median=None,p95=None,p99=None,maximum=None,minimum=None)
    return dict(samples=len(values),missing=len(data)-len(values),mean=statistics.mean(values),median=statistics.median(values),p95=ordered[round((len(ordered)-1)*.95)],p99=ordered[round((len(ordered)-1)*.99)],maximum=max(values),minimum=min(values))
keys=['FrameTime','GPUTime','GameThreadTime','RenderThreadTime_CriticalPath','TextureStreaming/StreamingPool','TextureStreaming/WantedMips','TextureStreaming/NonStreamingMips','GPUMem/LocalBudgetMB','GPUMem/LocalUsedMB']
result=dict(csv=str(args.csv),frames=len(data),measurements={k:stats(k) for k in keys if k in header},units='Frame/GPU/CPU: ms; memory: engine CSV MB (1024^2 bytes)',scope='Local PIE observation; no matched pre-authoring benchmark or minimum-spec guarantee')
frame_mean=result['measurements']['FrameTime']['mean']
result['fps_from_mean_frame_time']=1000/frame_mean if frame_mean and frame_mean>0 else None
if args.context:result['capture_context']=json.loads(args.context.read_text())
args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(result,indent=2)+'\n',encoding='utf-8');print(json.dumps(result,indent=2))
