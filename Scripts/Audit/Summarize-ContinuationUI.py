from pathlib import Path
import csv
import hashlib
import json
import statistics
import sys

root=Path(__file__).resolve().parents[2]
name=sys.argv[1] if len(sys.argv)>1 else 'CA01_05_CookedUI1'
base=root/f'Saved/Validation/{name}'
summary=[]
keys=['ProjectJUI/GameThread/InventoryFilter','ProjectJUI/GameThread/QuickSlotPresentation','ProjectJUI/GameThread/InventoryProjection']
for size in ['1280x720','1920x1080']:
    files=list((base/size/'User/Saved/Profiling/CSV').glob('*.csv')); assert len(files)==1
    path=files[0]; frames=[]; ignored=0
    with path.open(encoding='utf-8-sig',newline='') as file:
        for row in csv.DictReader(file):
            try: values={key:float(row[key]) for key in keys}
            except (ValueError,TypeError,KeyError): ignored+=1; continue
            frames.append(values)
    windows={}
    for label,data in [('full_capture',frames),('from_numeric_frame_120',frames[120:])]:
        metrics={}
        for key in keys:
            values=sorted(r[key] for r in data); assert values
            metrics[key]={'mean_ms':statistics.mean(values),'p95_ms':values[int(.95*(len(values)-1))],'max_ms':values[-1]}
        windows[label]={'numeric_frames':len(data),'metrics':metrics}
    summary.append({'resolution':size,'csv':path.relative_to(root).as_posix(),'csv_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                    'ignored_non_numeric_rows':ignored,'windows':windows})
output_name=f'{name}_Summary.json' if len(sys.argv)>1 else 'CA01_05_UI_Summary.json'
out=root/f'Saved/Validation/Continuation_20261014/{output_name}'
if out.exists(): raise RuntimeError('Preserve prior UI summary')
out.write_text(json.dumps({'scope_ko':'명시한 packaged exe/SHA의 cooked UI 동적1000행·24buff·4window·50단계·5Hz·t.MaxFPS60. 원 capture와 앞선 recipe의120행 이후를 함께 기록. 개별 inclusive scope라 임의 합산을 GT 전체 시간으로 표시하지 않는다. 이전 UI3과 A/B 비교가 아님; 모든 접근성/시각 품질 증거 아님.',
                          'runs':summary},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps([{'resolution':r['resolution'],'full_frames':r['windows']['full_capture']['numeric_frames'],
                  'metrics':r['windows']['from_numeric_frame_120']['metrics']} for r in summary]))
