"""Observed callee attribution from retained traces; not new gameplay/performance runs."""
import csv
import hashlib
import json
from pathlib import Path

root = Path(__file__).resolve().parents[2]
base = root / 'Saved/Validation/AuthoredAnimation_20261005'
out = root / 'Saved/Validation/Continuation_20261014/R15_CPUCallTreeAnalysis1'
out.mkdir(exist_ok=False)
folders = {v: base / f'CA_CPU_Integrated_Trace1_{v}_trace/Count200/TraceExport_Callees_R15_1' for v in ('A', 'B')}
common = ('ProjectJ_AuthoredAnimation_WorldTick', 'Project_J_PlayerCharacterTick_LocomotionState',
          'Project_J_PlayerCharacterTick_Trajectory', 'ProjectJ_VisualAnimationBudget',
          'Project_J_AnimBuildOptimizationPolicy', 'Project_J_AnimBuildThreadSafeData')

def analyze(path):
    rows = list(csv.DictReader(path.open(encoding='utf-8-sig')))
    names = {r['TimerId']: r['TimerName'] for r in rows}
    demand = {}
    for row in rows:
        if row['TimerName'] != 'ProjectJ_VisualDemandResolve':
            continue
        parent = names.get(row['ParentId'], f'unknown:{row["ParentId"]}')
        group = demand.setdefault(parent, {'calls': 0, 'exclusive_thread_elapsed_seconds': 0.0})
        group['calls'] += int(row['Count'])
        group['exclusive_thread_elapsed_seconds'] += float(row['Exc.Time'])
    return {
        'demand_scope_observed': bool(demand), 'demand_by_immediate_parent': demand,
        'common_parent_totals': {name: {'calls': sum(int(r['Count']) for r in rows if r['TimerName'] == name),
                                     'inclusive_thread_elapsed_seconds': sum(float(r['Inc.Time']) for r in rows if r['TimerName'] == name),
                                     'exclusive_thread_elapsed_seconds': sum(float(r['Exc.Time']) for r in rows if r['TimerName'] == name)} for name in common},
    }

files = sorted(p.name for p in folders['B'].glob('WorldCallees*.csv'))
if not files or any(not (folders['A']/name).is_file() for name in files):
    raise RuntimeError('Incomplete matching exports')
result = {
    'source_capture': 'CA_CPU_Integrated_Trace1: RA27 A / CA01-13 B',
    'meaning_ko': '이전200actor/socket0 NullRHI 캡처의 관측 GT WorldTick callee tree를 추가 추출. full은 warmup+measured+dispatch의 world scopes이며 region은 기존 measured CPU regions. gameplay/binary 재실행 없음. A demand scope 미관측은 기존 로직/시간0이 아님. parent inclusive와 child exclusive를 더하지 않고 active CPU/critical path로 해석하지 않음. 현재 CA14/15 바이너리의 새 측정 아님.',
    'variants': {v: analyze(folders[v]/'WorldCallees.csv') for v in ('A', 'B')},
    'regions': [{'file': name, 'variants': {v: analyze(folders[v]/name) for v in ('A', 'B')}} for name in files if name != 'WorldCallees.csv'],
    'exports': [{'path': (folders[v]/name).relative_to(root).as_posix(), 'sha256': hashlib.sha256((folders[v]/name).read_bytes()).hexdigest()} for v in ('A', 'B') for name in files],
    'cpu_increase_resolved': False,
}
(out/'DemandCallerSummary.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
print(json.dumps({'regions': len(result['regions']), 'variants': result['variants'], 'cpu_increase_resolved': False}, ensure_ascii=False))
