"""Capture a new audit baseline without changing previous evidence or Git state."""
from pathlib import Path
import hashlib
import json
import subprocess
from datetime import datetime, timezone

ROOT = Path(__file__).resolve().parents[2]
OUT = ROOT / 'Saved/Validation/Continuation_20261014'
PORTABLE = ROOT / 'Docs/Review/Audits/ContinuationEvidence_2026-10-14'

def read(path):
    return json.loads((ROOT / path).read_text(encoding='utf-8-sig'))

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def git(*args):
    return subprocess.check_output(['git', '-c', 'core.fsmonitor=false', *args], cwd=ROOT).decode('utf-8')

def write(path, data):
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')

OUT.mkdir(parents=True, exist_ok=True)
PORTABLE.mkdir(parents=True, exist_ok=True)
if (OUT / 'InitialState.json').exists():
    raise SystemExit('Initial evidence already exists; preserve it.')
inventory = read('Docs/Handoffs/WholeProjectAudit_2026-10-14/ContinuationCoverageInventory.json')
previous = 'Saved/Validation/Reaudit_20261014/'
sources = read(previous + 'RA27SourceHashes.json')
binaries = read(previous + 'RA27EditorBinaryHashes.json')
preserved = read(previous + 'CurrentPreservation_RA27.json')['preserved']
checks = []
for group, rows, pathkey, prefix in [('source', sources, 'path', ''),
                                    ('editor', binaries, 'file', 'Binaries/Win64/'),
                                    ('preserved', preserved, 'path', '')]:
    for row in rows:
        relative = prefix + row[pathkey]
        file = ROOT / relative
        actual = sha(file) if file.exists() else None
        checks.append({'group': group, 'path': relative, 'expected_sha256': row['sha256'].lower(),
                       'actual_sha256': actual, 'matches': actual == row['sha256'].lower()})
state = {'captured_utc': datetime.now(timezone.utc).isoformat(),
         'head': git('rev-parse', 'HEAD').strip(), 'branch': git('branch', '--show-current').strip(),
         'initial_status': git('status', '--short'),
         'initial_user_diff_path': 'InitialUserChanges.patch',
         'process_check': {'Get-CimInstance': 'access_denied; no workaround',
                           'Get-Process': 'success; no Unreal/UBT/dotnet/LiveCoding/MSBuild/ShaderCompileWorker/Project_J'},
         'hash_counts': {group: sum(r['group'] == group for r in checks) for group in ['source', 'editor', 'preserved']},
         'hash_mismatches': [r for r in checks if not r['matches']],
         'meaning_ko': '이전 RA27와 시작 버전의 동일성 확인. 전체 상세 검토/에셋 무결성 판정이 아니다.'}
(OUT / 'InitialUserChanges.patch').write_text(git('diff', '--binary'), encoding='utf-8')
write(OUT / 'InitialState.json', state)
write(PORTABLE / 'InitialState.json', state)
write(PORTABLE / 'InitialHashChecks.json', checks)
write(PORTABLE / 'CoverageLedger.json', {
    'meaning_ko': '이전 범위를 계승하는 작업 장부. 상세 검토는 명시된 함수/호출/에셋 범위에만 적용한다.',
    'whole_project_complete': False,
    'source': [{**row, 'continuation_review_units': [], 'status_ko': '미완료 검토'} for row in inventory['source']],
    'assets': [{**row, 'continuation_review_units': [], 'status_ko': '미완료 검토'} for row in inventory['content_package_inventory']],
    'auxiliary': [{**row, 'continuation_review_units': [], 'status_ko': '미완료 검토'} for row in inventory['auxiliary_inventory']],
    'review_units': []})
print(json.dumps(state, ensure_ascii=False))
