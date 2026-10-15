"""Freeze source/binary provenance after a normally completed build, without restoring anything."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys

root = Path(__file__).resolve().parents[2]
name = sys.argv[1]
if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
    raise SystemExit('Invalid version name')
out = root / 'Saved/Validation/Continuation_20261014' / name
out.mkdir(parents=True, exist_ok=False)
paths = subprocess.check_output(['git', '-c', 'core.fsmonitor=false', 'ls-files', '--cached', '--others', '--exclude-standard', '--',
                                 'Source', 'Plugins/ProjectJExperiments/Source', 'Config', 'Scripts', 'Project_J.uproject'], cwd=root).decode().splitlines()
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
sources = [{'path':p, 'sha256':sha(root / p)} for p in sorted(set(paths)) if (root / p).is_file()]
descriptor = json.loads((root / 'Project_J.uproject').read_text(encoding='utf-8-sig'))
names = ['UnrealEditor-' + m['Name'] + '.dll' for m in descriptor['Modules']] + ['UnrealEditor.modules']
binaries = [{'file':f, 'sha256':sha(root / 'Binaries/Win64' / f)} for f in names]
prior = json.loads((root / 'Docs/Review/Audits/ContinuationEvidence_2026-10-14/InitialHashChecks.json').read_text(encoding='utf-8-sig'))
preservation = [{'path':r['path'], 'sha256':sha(root / r['path']), 'matches':sha(root / r['path']) == r['expected_sha256']}
                for r in prior if r['group'] == 'preserved']
for file, data in [('SourceHashes.json',sources),('EditorBinaryHashes.json',binaries),('Preservation.json',preservation)]:
    (out / file).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
patch = subprocess.check_output(['git','-c','core.fsmonitor=false','diff','--binary','--','Source','Scripts/Audit'],cwd=root)
(out / 'TrackedSourceChanges.patch').write_bytes(patch)
if not all(r['matches'] for r in preservation):
    raise SystemExit('Preserved content changed; investigate instead of restoring.')
print(json.dumps({'version':name,'source_files':len(sources),'editor_artifacts':len(binaries),'preserved':len(preservation),'mismatches':0}))
