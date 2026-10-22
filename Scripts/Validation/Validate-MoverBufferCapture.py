"""Check a read-only ProjectJ.CaptureMoverBuffers CSV without Unreal/Python bindings."""
import argparse
import csv
import json
import math
import re
from collections import defaultdict
from pathlib import Path


def validate_diagnostics(path):
    lines=Path(path).read_text(encoding='utf-8-sig',errors='replace').splitlines()
    start=next((i for i,line in enumerate(lines) if 'MoverBufferCapture Started ' in line),None)
    assert start is not None, 'No explicit post-readiness capture boundary in log'
    pattern=re.compile(r'LogNetworkPrediction: Warning:|LogNetworkPrediction.*Interpolation out of sync|NetworkPrediction.*SystemFault')
    startup=[line for i,line in enumerate(lines) if i<start and pattern.search(line)]
    measured=[line for i,line in enumerate(lines) if i>=start and pattern.search(line)]
    assert not measured, 'NetworkPrediction diagnostic during the captured stream: '+str(measured[:3])
    return dict(startup_diagnostics=startup,captured_diagnostics=measured,capture_start_line=start+1)


def validate(path, require_hitch_ms=0.0, min_receive_hz=1.0):
    worlds=defaultdict(list)
    with path.open(encoding='utf-8-sig',newline='') as stream:
        for source in csv.DictReader(stream):
            row={key:float(value) for key,value in source.items() if key!='World'}
            if row['NetMode']==3:
                assert all(math.isfinite(value) for value in row.values()), 'Non-finite client sample'
                worlds[source['World']].append(row)
    assert worlds, 'No initialized client interpolation stream was captured'
    results=[]
    for name,rows in worlds.items():
        all_rows=rows
        initialized=lambda row: row['LatestReceiveMS']>row['DesiredMS'] and row['InterpolationMS']>0
        first=next((index for index,row in enumerate(rows) if initialized(row)),None)
        assert first is not None, 'Client stream never initialized: '+name
        rows=rows[first:]
        assert all(initialized(row) for row in rows), 'Client stream reset or became invalid after initialization: '+name
        assert all(b['WallSeconds']>a['WallSeconds'] for a,b in zip(rows,rows[1:])), 'Capture clock did not advance: '+name
        assert len(rows)>=100, 'Insufficient client samples: '+name
        assert all(row['BufferedMS']>=-.01 for row in rows), 'Presentation exceeded received history: '+name
        assert all(row['InterpolationMS']<=row['LatestReceiveMS']+.01 for row in rows), 'Future state consumed: '+name
        assert all(b['InterpolationMS']>=a['InterpolationMS']-.01 for a,b in zip(rows,rows[1:])), 'Presentation rewound: '+name
        largest=max(row['WorldDeltaMS'] for row in rows)
        assert largest>=require_hitch_ms, 'Requested hitch stress was not exercised: '+name
        # Holding received history is safe but not proof of healthy buffering.
        # Require recovery in the final settled window as a separate condition.
        tail=[row for row in rows if row['WallSeconds']>=rows[-1]['WallSeconds']-3]
        tail_mean=sum(row['BufferedMS'] for row in tail)/len(tail)
        # A 15Hz snapshot stream naturally has lower troughs than a 120Hz PIE
        # stream. Measure recovery across the packet cycle, not only its trough.
        assert tail_mean>=tail[-1]['DesiredMS']*.5, 'Buffer did not recover across the receive cycle: '+name
        duration=tail[-1]['WallSeconds']-tail[0]['WallSeconds']
        receive_hz=sum(row['ReceiveAdvanceMS']>0 for row in tail)/max(.001,duration)
        assert receive_hz>=max(1.0,min_receive_hz), 'Snapshot supply stopped or is slower than the requested contract: '+name
        assert len({(row['DesiredMS'],row['MaxMS']) for row in rows})==1, 'Buffer settings changed during capture'
        results.append({'world':name,'frames':len(rows),'total_frames':len(all_rows),'warmup_frames':first,
                        'total_duration_seconds':all_rows[-1]['WallSeconds']-all_rows[0]['WallSeconds'],
                        'measured_duration_seconds':rows[-1]['WallSeconds']-rows[0]['WallSeconds'],
                        'min_buffer_ms':min(row['BufferedMS'] for row in rows),
                        'max_buffer_ms':max(row['BufferedMS'] for row in rows),'max_frame_ms':largest,
                        'tail_min_buffer_ms':min(row['BufferedMS'] for row in tail),'tail_mean_buffer_ms':tail_mean,
                        'tail_receive_hz':receive_hz,'future_frames':0,'rewinds':0})
    return {'passed':True,'capture':str(path),'worlds':results}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture',type=Path)
    parser.add_argument('--require-hitch-ms',type=float,default=0.)
    parser.add_argument('--min-receive-hz',type=float,default=1.)
    parser.add_argument('--output',type=Path)
    parser.add_argument('--log',type=Path,help='Require the explicit ready-stage capture marker and reject all later NP diagnostics')
    args=parser.parse_args()
    result=validate(args.capture,args.require_hitch_ms,args.min_receive_hz)
    if args.log:result['diagnostics']=validate_diagnostics(args.log)
    encoded=json.dumps(result,ensure_ascii=False,indent=2)
    if args.output:args.output.write_text(encoded,encoding='utf-8')
    print(encoded)
