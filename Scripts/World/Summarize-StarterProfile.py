"""Summarize native CSV profiling, honoring Unreal's final dynamic header."""
import argparse,csv,json,math
from pathlib import Path


def main():
    parser=argparse.ArgumentParser();parser.add_argument('csv',type=Path);args=parser.parse_args()
    rows=list(csv.reader(args.csv.open(newline='',encoding='utf-8-sig')))
    headers=[row for row in rows if row and row[0]=='EVENTS'];assert headers
    header=headers[-1]
    assert header[:len(headers[0])]==headers[0],'Non-append-only schema requires explicit CSV remapping'
    data=[row for row in rows if row and row[0]!='EVENTS' and not row[0].startswith('[')]
    result=dict(source=str(args.csv.resolve()),frames=len(data),scope='Stationary standalone PIE; editor overhead and current medium settings',metrics={},vram_measured=False,performance_approved=False)
    for name in ['FrameTime','GameThreadTime','RenderThreadTime','GPUTime','RHI/DrawCalls']:
        if name not in header:continue
        i=header.index(name);values=[]
        for row in data:
            if i<len(row) and row[i]:values.append(float(row[i]))
        if not values:continue
        values.sort()
        def pct(p):
            k=(len(values)-1)*p;a=math.floor(k);b=math.ceil(k)
            return values[a]*(b-k)+values[b]*(k-a) if a!=b else values[a]
        result['metrics'][name]=dict(samples=len(values),p50=pct(.5),p95=pct(.95),p99=pct(.99),max=max(values))
    footer=rows[-1]
    result['hardware']={footer[i]:footer[i+1] for i in range(0,len(footer)-1,2) if footer[i] in ['[cpu]','[gpu]','[rhi]','[platform]']}
    output=Path(__file__).resolve().parents[2]/'Saved/Validation/StarterWorld/Phase2/native-profile-summary.json'
    output.write_text(json.dumps(result,indent=2),encoding='utf-8');print(json.dumps(result))


if __name__=='__main__':main()
