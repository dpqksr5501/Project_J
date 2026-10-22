from pathlib import Path
import hashlib
import json
import sys
import subprocess
from continuation_provenance import compare_sources, plugin_artifacts, content_worktree_hashes, check_artifacts

root=Path(__file__).resolve().parents[2]
name=sys.argv[1]; version=sys.argv[2]
out=root/'Saved/Validation/Continuation_20261014'/name
receipt=out/'GameVersion.json'
if receipt.exists(): raise RuntimeError('Preserve prior artifact receipt.')
frozen=json.loads((root/'Saved/Validation/Continuation_20261014'/version/'SourceHashes.json').read_text(encoding='utf-8-sig'))
sources=frozen
mismatches=compare_sources(root,frozen)
version_dir=root/'Saved/Validation/Continuation_20261014'/version
if subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode().strip() != (version_dir/'Head.txt').read_text(encoding='utf-8').strip():
    mismatches.append({'head':'Changed after frozen source snapshot'})
assets=json.loads((version_dir/'AssetHashes.json').read_text(encoding='utf-8-sig'))
mismatches += [{'asset':p} for p in check_artifacts(root,assets)]
if content_worktree_hashes(root) != json.loads((version_dir/'ContentWorktreeHashes.json').read_text(encoding='utf-8-sig')):
    mismatches.append({'content_worktree':'Changed after frozen asset snapshot'})
artifacts=plugin_artifacts(root)
for name in ['Project_J.exe','Project_J.pdb','Project_J.target']:
    path=root/'Binaries/Win64'/name
    if not path.is_file(): raise RuntimeError(f'Missing build artifact: {path}')
    artifacts.append({'path':path.relative_to(root).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
receipt.write_text(json.dumps({'editor_frozen_version':version,'actual_game_source_matches_frozen':not mismatches,
                              'checked_sources':len(sources),'mismatches':mismatches,'artifacts':artifacts,'assets':assets,
                              'scope_ko':'직접 UBT Game 성공 뒤 실제 exe/pdb/target SHA와 frozen Source/Config/descriptor 대응. Editor-only branch와 모든 Cook/runtime 품질 완료는 별도.'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'actual_game_source_matches_frozen':not mismatches,'checked_sources':len(sources),'game_artifacts':len(artifacts)}))
assert not mismatches
