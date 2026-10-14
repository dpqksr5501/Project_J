from pathlib import Path
import hashlib, json, shutil, difflib

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
baseline = json.loads((out/'BaselineManifest.json').read_text(encoding='utf-8-sig'))
snapshot = out/'RA19SourceSnapshot'
binary = out/'RA19EditorBinaries'
if snapshot.exists() or binary.exists():
    raise RuntimeError('Final freeze is immutable; use a new explicitly named freeze if source changes.')
paths = {r['path'] for r in baseline}
paths.update(p.relative_to(root).as_posix() for p in (root/'Source').rglob('*Reaudit*Tests.cpp'))
manifest, changes, patch = [], [], []
for rel in sorted(paths):
    source = root/rel
    data = source.read_bytes()
    dest = snapshot/rel
    dest.parent.mkdir(parents=True, exist_ok=True)
    dest.write_bytes(data)
    sha = hashlib.sha256(data).hexdigest()
    manifest.append(dict(path=rel, sha256=sha, bytes=len(data)))
    old = out/'Baseline'/rel
    if not old.exists() or old.read_bytes() != data:
        changes.append(dict(path=rel, status='modified' if old.exists() else 'added', sha256=sha,
            baseline_sha256=hashlib.sha256(old.read_bytes()).hexdigest() if old.exists() else None))
        patch.extend(difflib.unified_diff(old.read_text(encoding='utf-8-sig').splitlines(True) if old.exists() else [],
            data.decode('utf-8-sig').splitlines(True), fromfile='a/'+rel if old.exists() else '/dev/null', tofile='b/'+rel))
(out/'RA19SourceHashes.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding='utf-8')
(out/'RA19SourceChangeManifest.json').write_text(json.dumps(changes, ensure_ascii=False, indent=2), encoding='utf-8')
(out/'RA19SourceChanges.patch').write_text(''.join(patch), encoding='utf-8')
modules = json.loads((root/'Project_J.uproject').read_text(encoding='utf-8-sig'))['Modules']
binary.mkdir()
binary_manifest = []
for name in [f"UnrealEditor-{m['Name']}.dll" for m in modules]+['UnrealEditor.modules']:
    source = root/'Binaries/Win64'/name
    shutil.copyfile(source, binary/name)
    binary_manifest.append(dict(file=name, sha256=hashlib.sha256(source.read_bytes()).hexdigest(), bytes=source.stat().st_size))
(out/'RA19EditorBinaryHashes.json').write_text(json.dumps(binary_manifest, indent=2), encoding='utf-8')
print(json.dumps(dict(text_files=len(manifest), source_changes=len(changes), editor_artifacts=len(binary_manifest),
    meaning='Frozen compiled source/provenance only; does not claim every function or asset reviewed.')))

