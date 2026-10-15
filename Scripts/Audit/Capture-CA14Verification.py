"""Bounded verification receipt; preserve immutable runs and separate pending integration."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[2]
saved = root / 'Saved/Validation/Continuation_20261014'
out = saved / 'R15_CA14Verification1'
out.mkdir(exist_ok=False)
read = lambda p: json.loads(p.read_text(encoding='utf-8-sig'))
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
git = lambda *args: subprocess.check_output(['git', '-c', 'core.fsmonitor=false', *args], cwd=root).decode('utf-8')
frozen = saved / 'R15_LevelStyleHandoverFix1'
sources = read(frozen / 'SourceHashes.json')
runtime = [r for r in sources if r['path'].startswith(('Source/', 'Config/', 'Plugins/ProjectJExperiments/Source/')) or r['path'] == 'Project_J.uproject']
editor = read(frozen / 'EditorBinaryHashes.json')
preserved = read(frozen / 'Preservation.json')
focused = read(saved / 'R15_LevelStyleHandoverFocused1/index.json')
user_patch = git('diff', '--binary', '--', 'Docs/Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md').strip()
result = {
    'utc': datetime.now(timezone.utc).isoformat(), 'head': git('rev-parse', 'HEAD').strip(),
    'branch': git('branch', '--show-current').strip(), 'unit': 'CA14',
    'version': frozen.name, 'full_editor_build': read(saved / 'R15_LevelHandoverFixBuild1/process.json'),
    'runtime_count': len(runtime),
    'runtime_mismatches': [r['path'] for r in runtime if sha(root / r['path']) != r['sha256']],
    'editor_count': len(editor),
    'editor_mismatches': [r['file'] for r in editor if sha(root / 'Binaries/Win64' / r['file']) != r['sha256']],
    'preserved_count': len(preserved),
    'preserved_mismatches': [r['path'] for r in preserved if sha(root / r['path']) != r['sha256']],
    'user_handoff_diff_preserved': user_patch == (saved / 'InitialUserChanges.patch').read_text(encoding='utf-8').strip(),
    'content_git_diff_empty': not git('diff', '--name-only', '--', 'Content').strip(),
    'focused': {k: focused[k] for k in ('succeeded', 'succeededWithWarnings', 'failed', 'notRun', 'inProcess', 'totalDuration')},
    'focused_process': read(saved / 'R15_LevelStyleHandoverFocused1/process.json'),
    'focused_tests': [{'name': t['fullTestPath'], 'state': t['state']} for t in focused['tests']],
    'asset_reads': ['R15_LevelAssetRead1/AssetConnections.json', 'R15_LevelAssetFieldRepair1/AssetConnections.json'],
    'asset_read_scope_ko': '지정 class/style DA + native PS/NPC·BP_NPC_Test CDO public authoring fields only. 최초 bool 속성명 오류는 field repair에서 교정; 접근 제한 우회 아님.',
    'pending_ko': ['전체 source/asset 상세 감사', 'uninitialized persistent owner와 legacy fallback', '실서비스 XP/inventory/cooldown snapshot adapter',
                   '새 기준점 전체 회귀·Game/Cook/package', '동일 조건 CPU 증가·render 차이 원인 및 실제 품질·socket 확장 검증'],
    'whole_project_complete': False,
}
(out / 'R15_SystemVerification.json').write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
(out / 'GitStatus.txt').write_text(git('status', '--short'), encoding='utf-8')
print(json.dumps({k: result[k] for k in ('runtime_count', 'runtime_mismatches', 'editor_count', 'editor_mismatches', 'preserved_count', 'preserved_mismatches', 'user_handoff_diff_preserved', 'focused', 'whole_project_complete')}, ensure_ascii=False))
if any(result[k] for k in ('runtime_mismatches', 'editor_mismatches', 'preserved_mismatches')) or not result['user_handoff_diff_preserved']:
    raise RuntimeError('Review differences; do not restore automatically')
