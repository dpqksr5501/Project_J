"""Immutable integrated-checkpoint receipt. Historical filename retained for callers."""
import argparse
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess
from continuation_provenance import source_hashes, compare_sources, check_artifacts

root = Path(__file__).resolve().parents[2]
saved = root / 'Saved/Validation/Continuation_20261014'
parser = argparse.ArgumentParser(description=__doc__)
for key in ('name', 'version', 'regression', 'game', 'cook', 'package', 'combat', 'remote'):
    parser.add_argument('--' + key, required=True)
args = parser.parse_args()
if any(not value or any(c not in 'abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-' for c in value) for value in vars(args).values()):
    parser.error('Run names must be simple directory names')
out = saved / args.name
out.mkdir(exist_ok=False)
read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
git = lambda *a: subprocess.check_output(['git', '-c', 'core.fsmonitor=false', *a], cwd=root).decode('utf-8').strip()
frozen = saved / args.version
runtime = read(frozen / 'SourceHashes.json')
editor = read(frozen / 'EditorBinaryHashes.json')
preserved = read(frozen / 'Preservation.json')
report = read(saved / args.regression / 'index.json')
combat = root / 'Saved/Validation/EF_20260912' / args.combat
remote = root / 'Saved/Validation/GroupD_20260910' / args.remote
runtime_paths = {r['path'] for r in runtime}
actual_paths = {r['path'] for r in source_hashes(root)}
receipt = {
    'utc': datetime.now(timezone.utc).isoformat(), 'head': git('rev-parse', 'HEAD'),
    'branch': git('branch', '--show-current'), 'version': frozen.name,
    'runtime_count': len(runtime), 'runtime_mismatches': [r['path'] for r in compare_sources(root,runtime)],
    'runtime_path_difference': sorted(runtime_paths ^ actual_paths),
    'editor_count': len(editor), 'editor_mismatches': check_artifacts(root,[dict(path=r['path'] if 'path' in r else 'Binaries/Win64/'+r['file'],sha256=r['sha256']) for r in editor]),
    'preserved_count': len(preserved), 'preserved_mismatches': [r['path'] for r in preserved if sha(root / r['path']) != r['sha256']],
    'user_handoff_diff_preserved': git('diff', '--binary', '--', 'Docs/Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md') == (saved / 'InitialUserChanges.patch').read_text(encoding='utf-8').strip(),
    'content_git_diff_empty': not git('diff', '--name-only', '--', 'Content'),
    'whole_regression': {k: report[k] for k in ('succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'inProcess', 'totalDuration')},
    'whole_regression_run': args.regression,
    'whole_regression_process': read(saved / args.regression / 'process.json'),
    'inherited_warnings': read(saved / args.regression / 'WarningMultisetComparison.json'),
    'game_process': read(saved / args.game / 'process.json'),
    'game_identity': read(saved / args.game / 'GameVersion.json'),
    'cook_process': read(saved / args.cook / 'process.json'),
    'package_process': read(saved / args.package / 'process.json'),
    'package_identity': read(saved / args.package / 'PackageIdentity.json'),
    'cooked_combat': {'process': read(combat / 'process.json'), 'result': (combat / 'result.txt').read_text(encoding='utf-8-sig').strip()},
    'editor_remote': read(remote / 'verification.json'),
    'scope_ko': '지정 기준점의 현재 source·Editor·Game·configured Cook/package·actual combat fixture·Editor remote fixture. 실제 socket/NPC/lag/loss 수는 연결된 remote manifest의 값이며 200 NPC를 200 connections로 해석하지 않는다. 전체 소스/에셋 정밀 감사·CookAll·WAN·native Server target·close-up 품질 완료가 아님.',
    'whole_project_complete': False,
    'pending_ko': ['전체 시스템 소스/호출/소유권·에셋 상세 범위와 탑승 callback 경계', 'NPC 전체 확장 조합과 변경 후 전용 성능 비교', 'uninitialized progression와 migration adapter', '원래 CPU 증가·render 차이 및 hand/cloth 품질·실제 socket 확장'],
}
(out / 'IntegratedVerification.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
(out / 'GitStatus.txt').write_text(git('status', '--short')+'\n', encoding='utf-8')
print(json.dumps({k: receipt[k] for k in ('runtime_count', 'runtime_mismatches', 'editor_count', 'editor_mismatches', 'preserved_count', 'preserved_mismatches', 'user_handoff_diff_preserved', 'whole_regression', 'whole_project_complete')}, ensure_ascii=False))
if any(receipt[k] for k in ('runtime_mismatches', 'runtime_path_difference', 'editor_mismatches', 'preserved_mismatches')) or not receipt['user_handoff_diff_preserved']:
    raise RuntimeError('Current checkpoint mismatch; never restore automatically')
if any(receipt[k]['exitCode'] != 0 for k in ('whole_regression_process', 'game_process', 'cook_process', 'package_process')):
    raise RuntimeError('Checkpoint has a failed/incomplete process')
if report['failed'] or report['notRun'] or report['inProcess'] or not receipt['editor_remote']['passed'] or receipt['cooked_combat']['process']['exitCode'] != 0:
    raise RuntimeError('Checkpoint has incomplete/failed execution')
if receipt['game_identity']['editor_frozen_version'] != args.version or receipt['package_identity']['source_version'] != args.version or not receipt['package_identity']['matches_direct_ubt_game']:
    raise RuntimeError('Compiled/deployed checkpoint differs')
