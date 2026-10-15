"""Summarize actual diagnostic timers and counter presence without imputing zero cost."""
from pathlib import Path
import csv
import hashlib
import json

root = Path(__file__).resolve().parents[2]
name = 'CA_CPU_NotifyTrace1'
export = root / f'Saved/Validation/AuthoredAnimation_20261005/{name}_B_trace/Count200/TraceExport'
with (export / 'AllTimers.csv').open(encoding='utf-8-sig', newline='') as file:
    timers = {row['Name']: row for row in csv.DictReader(file)}
counter_files = [p for p in export.glob('Counter-*.csv') if '-ProjectJ.' not in p.name]
counter_rows = {}
for path in sorted(counter_files):
    with path.open(encoding='utf-8-sig', newline='') as file:
        rows = list(csv.DictReader(file))
    counter_rows[path.name] = {'samples': len(rows), 'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
names = ['ProjectJ_VisualDemandResolve', 'ProjectJ_VisualFollowerResolve', 'ProjectJ_FollowerTickPolicy',
         'ProjectJ_WeaponMotionNotify_Begin', 'ProjectJ_WeaponMotionNotify_Tick', 'ProjectJ_WeaponMotionNotify_End',
         'ProjectJ_WeaponMotionNotify_WeakMeshPrune', 'ProjectJ_GroundContactNotify_Begin',
         'ProjectJ_GroundContactNotify_End', 'ProjectJ_GroundContactNotify_WeakMeshPrune',
         'ProjectJ_TwoHandIKNotify_Begin', 'ProjectJ_TwoHandIKNotify_End']
result = {
    'scope_ko': 'CA05 A/CA07 B의 동일 LMB intent burst·복귀 pair(실행 montage명 미수집), NullRHI body200·socket0. B 전체 trace의 setup/warmup/measurement 포함. OS active CPU/critical path 및 모든 notify workload 검증이 아님.',
    'timers': {key: {'observed': key in timers,
                     'calls': int(timers[key]['Count']) if key in timers else None,
                     'exclusive_thread_seconds': float(timers[key]['Excl']) if key in timers else None,
                     'inclusive_thread_seconds': float(timers[key]['Incl']) if key in timers else None} for key in names},
    'whole_counter_files': counter_rows,
    'interpretation_ko': 'TwoHandIK Begin/End가 실행됐지만 WeaponMotion/GroundContact timer와 방문 counter는 관찰되지 않았다. empty CSV는 이 조건의 미실행 증거이며 기능 전체의 비용0 또는 O(n²) 가설의 검증/해결 증거가 아니다. 역할 조회의 실제 호출/시간은 관찰했으나 원래 DS→RA13 증가의 인과로 확정하지 않는다.',
    'next_ko': '실제 WeaponMotion montage와 GroundContact 작성 여부를 확인해 동시50/100/200 시작·tick·종료/취소/GC를 별도 재현한다. 기존 LMB intent 조건과 구분하고 에셋·특정키/clip 예외로 제품 동작을 변경하지 않는다. 이미 저장한 옛 요약은 덮어쓰지 않고 현재 문서의 정정과 함께 읽는다.'
}
out = root / f'Saved/Validation/Continuation_20261014/{name}/NotifyObservationSummary.json'
if out.exists():
    raise RuntimeError('Preserve prior summary')
out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'summary': str(out), 'counter_files': len(counter_rows), 'counter_samples': sum(r['samples'] for r in counter_rows.values())}, ensure_ascii=False))
