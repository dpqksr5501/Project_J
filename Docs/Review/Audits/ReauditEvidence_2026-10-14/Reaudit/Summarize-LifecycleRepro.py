from pathlib import Path
import json
out=Path(__file__).resolve().parent
records=[]
for name in ['LifecycleReproContracts1','LifecycleReproContracts2','LifecycleReproContracts3']:
    folder=out/name
    if not (folder/'index.json').exists():continue
    report=json.loads((folder/'index.json').read_text(encoding='utf-8-sig'))
    record={k:report[k] for k in ['succeeded','succeededWithWarnings','failed','notRun','inProcess','totalDuration']}
    record.update(run=name,process=json.loads((folder/'process.json').read_text(encoding='utf-8-sig')),tests=[])
    for test in report['tests']:
        if 'PlayReuse' not in test['fullTestPath']:continue
        events=[entry['event'] for entry in test.get('entries',[])]
        record['tests'].append(dict(name=test['fullTestPath'],state=test['state'],
            assertions=[event['message'] for event in events if event['type']=='Error' and event['message'].startswith('Expected ')],
            non_assertion_errors=[event['message'] for event in events if event['type']=='Error' and not event['message'].startswith('Expected ') and '[Callstack]' not in event['message']],
            warnings=[event['message'] for event in events if event['type']=='Warning']))
    records.append(record)
(out/'LifecycleReproSummary.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps(records,ensure_ascii=False))
