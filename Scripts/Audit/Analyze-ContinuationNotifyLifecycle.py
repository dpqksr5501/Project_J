"""Count observed registry work; never equate thread elapsed time with total CPU/FPS."""
from pathlib import Path
import bisect
import csv
import hashlib
import json
import re
import statistics
import sys

root = Path(__file__).resolve().parents[2]
run = root / 'Saved/Validation/Continuation_20261014' / sys.argv[1]
export = run / (sys.argv[2] if len(sys.argv) > 2 else 'TraceExport')
output = run / 'NotifyStageSummary.json'
if output.exists():
    raise RuntimeError('Preserve prior summary')

def read(path):
    with path.open(encoding='utf-8-sig', newline='') as file:
        return list(csv.DictReader(file))

whole = {}
for path in export.glob('Counter-*.csv'):
    if '-ProjectJ.' not in path.name:
        rows = read(path)
        whole[path.stem.removeprefix('Counter-')] = (rows, [float(r['Time']) for r in rows])

regions = []
for path in sorted(export.glob('Timers-ProjectJ.NotifyLifecycle.*.csv')):
    match = re.search(r'Count(\d+)\.R(\d+)', path.name)
    if not match:
        continue
    rows = read(path)
    counters = {}
    suffix = path.stem.removeprefix('Timers-')
    for name, (all_rows, times) in whole.items():
        samples = read(export / f'Counter-{name}-{suffix}.csv')
        if samples:
            before = bisect.bisect_left(times, float(samples[0]['Time'])) - 1
            baseline = int(all_rows[before]['Value']) if before >= 0 else 0
            counters[name] = {'samples': len(samples), 'increments': int(samples[-1]['Value']) - baseline}
        else:
            counters[name] = {'samples': 0, 'increments': None}
    regions.append({'count': int(match[1]), 'repeat': int(match[2]), 'counters': counters,
                    'timers': {r['Name']: {'calls': int(r['Count']), 'exclusive_thread_seconds': float(r['Excl'])}
                               for r in rows if 'ProjectJ_' in r['Name']}})

measurement = json.loads((run / 'NotifyLifecycle.json').read_text(encoding='utf-8-sig'))
means = {str(count): {stage: statistics.mean(r[stage] for r in measurement['rows'] if r['count'] == count)
                     for stage in ['begin_ms', 'tick_ms', 'end_ms']} for count in [50, 100, 200]}
result = {'scope_ko': '실제 제작 BP·장비에서 native Motion 및 별도 synthetic Ground 수명 주기. 각 수 1warmup/4반복. 실제 full montage/GAS/world tick, OS active CPU, GPU/render FPS, 원격 socket·손/cloth 품질과 별도. 중첩 timer 합계를 critical path로 더하지 않는다.',
          'counter_note_ko': 'increments는 region 첫 sample 바로 전 whole counter 값을 뺀 실제 누적 변화. 빈 sample은 None이며 GC scope가 실행되지 않은 것과 기능 비용0을 구분한다.',
          'means': means, 'regions': regions,
          'raw_sha256': {p.relative_to(run).as_posix(): hashlib.sha256(p.read_bytes()).hexdigest()
                         for p in [run / 'NotifyLifecycle.json', run / 'CPU.utrace', *export.glob('*.csv')]}}
if len(regions) != 12:
    raise RuntimeError('Missing measured regions')
output.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'run': run.name, 'regions': len(regions), 'means': means,
                  'visits': [{'count': r['count'], 'repeat': r['repeat'],
                              'motion': r['counters']['WeaponMotion-WeakMeshVisits'],
                              'ground': r['counters']['GroundContact-WeakMeshVisits']} for r in regions]}, ensure_ascii=False))
