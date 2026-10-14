"""Copy bounded audit evidence; keep full traces and binaries in Saved with hashes."""
from pathlib import Path
import hashlib, json, shutil
root=Path(__file__).resolve().parents[3]
saved=Path(__file__).resolve().parent
dest=root/'Docs/Review/Audits/ReauditEvidence_2026-10-14'
if dest.exists(): raise RuntimeError('Evidence is immutable. Do not overwrite a prior bundle.')
dest.mkdir()
def copy(source, relative):
    target=dest/relative
    target.parent.mkdir(parents=True, exist_ok=True)
    shutil.copyfile(source, target)
for source in sorted(saved.iterdir()):
    if source.is_file() and source.suffix.lower() in {'.json','.log','.txt','.patch','.py','.ps1','.csv'} and source.name not in {'ContinueState.json','BaselineTrackedText.patch','McpSession1.txt','McpSession3.txt','McpEquipment2.txt','McpEquipment.txt'}:
        copy(source,Path('Reaudit')/source.name)
for folder in sorted(saved.iterdir()):
    if not folder.is_dir() or folder.name in {'Baseline','BaselineBinaries','FinalSourceSnapshot','FinalEditorBinaries','RA19SourceSnapshot','RA19EditorBinaries','RA25SourceSnapshot','RA25EditorBinaries','RA27SourceSnapshot','RA27EditorBinaries','Packaged1','Packaged2','Packaged3','EmptyEquipmentAssetBackup'}: continue
    for source in folder.rglob('*'):
        if source.is_file() and source.suffix.lower() in {'.json','.log','.txt','.ps1','.csv','.png'} and not any(part in {'User','SourceSnapshot','DerivedDataCache'} for part in source.relative_to(folder).parts):
            copy(source,Path('Reaudit')/source.relative_to(saved))
changes=json.loads((saved/'RA27SourceChangeManifest.json').read_text(encoding='utf-8-sig'))
for record in changes:
    relative=Path(record['path'])
    copy(saved/'RA27SourceSnapshot'/relative,Path('ChangedSource')/relative)
    old=saved/'Baseline'/relative
    if old.exists():copy(old,Path('BeforeChangedSource')/relative)
# External runs must be explicitly selected. No recursive copy of the project Saved tree.
external=json.loads((saved/'ExternalEvidenceRuns.json').read_text(encoding='utf-8-sig'))
raw_large=[]
for source in saved.rglob('*.utrace'):
    parts=source.relative_to(saved).parts
    if any(p in {'SourceSnapshot','Baseline','BaselineBinaries','FinalSourceSnapshot','FinalEditorBinaries','RA19SourceSnapshot','RA19EditorBinaries','RA25SourceSnapshot','RA25EditorBinaries','RA27SourceSnapshot','RA27EditorBinaries','Packaged1','Packaged2','Packaged3'} for p in parts):continue
    if 'User' in parts and 'Profiling' not in parts:continue
    raw_large.append(dict(path=source.relative_to(root).as_posix(),bytes=source.stat().st_size,sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
for relative in external:
    folder=root/relative
    for source in folder.rglob('*'):
        if not source.is_file():continue
        parts=source.relative_to(folder).parts
        if any(p in {'SourceSnapshot','DerivedDataCache'} for p in parts):continue
        if 'User' in parts and 'Profiling' not in parts:continue
        if source.suffix.lower() in {'.json','.log','.txt','.csv','.png','.rsp'}:
            copy(source,Path('External')/source.relative_to(root/'Saved/Validation'))
        elif source.suffix.lower() == '.utrace':
            raw_large.append(dict(path=source.relative_to(root).as_posix(),bytes=source.stat().st_size,sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
(dest/'LargeLocalRawEvidence.json').write_text(json.dumps(raw_large,indent=2),encoding='utf-8')
records=[]
for source in sorted(dest.rglob('*')):
    if source.is_file():records.append(dict(path=source.relative_to(dest).as_posix(),bytes=source.stat().st_size,sha256=hashlib.sha256(source.read_bytes()).hexdigest()))
(dest/'Manifest.json').write_text(json.dumps(records,ensure_ascii=False,indent=2),encoding='utf-8')
bad=[r['path'] for r in records if hashlib.sha256((dest/r['path']).read_bytes()).hexdigest()!=r['sha256']]
print(json.dumps(dict(files=len(records),bytes=sum(r['bytes'] for r in records),hash_mismatches=bad,large_local_traces=len(raw_large))))
if bad:raise RuntimeError('Evidence SHA mismatch.')
