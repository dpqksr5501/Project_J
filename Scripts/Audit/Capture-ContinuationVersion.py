"""Freeze source/binary provenance after a normally completed build, without restoring anything."""
from pathlib import Path
import hashlib
import json
import re
import subprocess
import sys
from continuation_provenance import source_hashes, plugin_artifacts, content_worktree_hashes, digest

root = Path(__file__).resolve().parents[2]
name = sys.argv[1]
if not re.fullmatch(r'[A-Za-z0-9_-]+', name):
    raise SystemExit('Invalid version name')
out = root / 'Saved/Validation/Continuation_20261014' / name
out.mkdir(parents=True, exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
sources = source_hashes(root)
descriptor = json.loads((root / 'Project_J.uproject').read_text(encoding='utf-8-sig'))
names = ['UnrealEditor-' + m['Name'] + '.dll' for m in descriptor['Modules']] + ['UnrealEditor.modules']
binaries = [{'path':'Binaries/Win64/'+f, 'sha256':sha(root / 'Binaries/Win64' / f)} for f in names] + plugin_artifacts(root)
prior = json.loads((root / 'Docs/Review/Audits/ContinuationEvidence_2026-10-14/InitialHashChecks.json').read_text(encoding='utf-8-sig'))
historical = [{'path':r['path'], 'actual':digest(root/r['path']), 'historical_sha256':r['expected_sha256'],
               'matches_historical':digest(root/r['path'])==r['expected_sha256']}
              for r in prior if r['group']=='preserved']
# October 14 preservation decisions are history, not immutable roots after the
# Mover/Family retarget migration. Freeze current selected assets instead.
asset_paths={r['path'] for r in historical if (root/r['path']).is_file()}
asset_paths.update(('Content/Character_BPs/GreatSword/BP_GreatSword.uasset',
                   'Content/Animation_Logic/ABPs/ABP_Player_Master.uasset',
                   'Content/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Layers.uasset',
                   'Content/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme.uasset'))
assets=[{'path':p,'sha256':sha(root/p)} for p in sorted(asset_paths)]
content=content_worktree_hashes(root)
for file, data in [('SourceHashes.json',sources),('EditorBinaryHashes.json',binaries),
                   ('AssetHashes.json',assets),('Preservation.json',assets),('ContentWorktreeHashes.json',content),
                   ('HistoricalPreservation.json',historical)]:
    (out / file).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
patch = subprocess.check_output(['git','-c','core.fsmonitor=false','diff','--binary','--','Source','Scripts','Config','Plugins','Project_J.uproject'],cwd=root)
(out / 'TrackedSourceChanges.patch').write_bytes(patch)
(out/'Head.txt').write_text(subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode(),encoding='utf-8')
print(json.dumps({'version':name,'source_files':len(sources),'editor_artifacts':len(binaries),
                  'selected_current_assets':len(assets),'changed_content_files':len(content),
                  'scope':'Source/plugin/binary identity plus selected assets and Content worktree; not all asset bytes or build success'}))
