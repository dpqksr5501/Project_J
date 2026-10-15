"""Freeze the incoming worktree before the separate asset cleanup pass."""
import hashlib
import json
import subprocess
from datetime import datetime, timezone
from pathlib import Path

root = Path(__file__).resolve().parents[2]
out = root / 'Saved/Validation/AssetCleanup_20261015/Baseline'
out.mkdir(parents=True, exist_ok=False)

def git(*args):
    return subprocess.check_output(['git', '-c', 'core.fsmonitor=false', *args], cwd=root)

files = {}
for folder in ('Content', 'Source', 'Config', 'Plugins'):
    for path in (root / folder).rglob('*'):
        if path.is_file() and not any(p in ('Binaries', 'Intermediate', 'Saved') for p in path.parts):
            files[path.relative_to(root).as_posix()] = hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()
(out / 'Files.json').write_text(json.dumps(files, indent=2) + '\n', encoding='utf-8')
(out / 'Incoming.diff').write_bytes(git('diff', '--binary', '--no-ext-diff'))
(out / 'IncomingStatus.txt').write_bytes(git('status', '--short'))
(out / 'State.json').write_text(json.dumps({
    'utc': datetime.now(timezone.utc).isoformat(), 'head': git('rev-parse', 'HEAD').decode().strip(),
    'branch': git('branch', '--show-current').decode().strip(), 'files': len(files),
    'purpose_ko': '이전 수정/테스트/제작 에셋 보존 기준. 미사용 판정이나 전수 검토 완료 아님.'
}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'baseline_files': len(files), 'out': str(out)}))
