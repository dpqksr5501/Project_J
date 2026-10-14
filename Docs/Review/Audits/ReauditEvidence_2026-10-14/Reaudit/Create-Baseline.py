from pathlib import Path
import json,shutil,hashlib
root=Path(__file__).resolve().parents[3]
out=Path(__file__).resolve().parent
baseline=out/'Baseline'
if baseline.exists():raise RuntimeError('Existing baseline is immutable; inspect instead of replacing.')
old=root/'Saved/Validation/DeepAudit_20261013'
records=json.loads((old/'FinalSourceHashes.json').read_text(encoding='utf-8-sig'))
manifest=[]
for r in records:
    p=root/r['path'];target=baseline/r['path'];target.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(p,target)
    manifest.append(dict(path=r['path'],sha256=hashlib.sha256(p.read_bytes()).hexdigest(),bytes=p.stat().st_size))
(out/'BaselineManifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
binary=out/'BaselineBinaries';binary.mkdir()
for r in json.loads((old/'FinalEditorBinaryHashes.json').read_text(encoding='utf-8-sig')):
    shutil.copyfile(root/'Binaries/Win64'/r['file'],binary/r['file'])
for name in ['SourceReviewLedger.json','PreservationHashes.json','FinalDeliveryPreservation.json','AttackAssetHashesFinal.json']:
    shutil.copyfile(old/name,out/('Previous'+name))
print(json.dumps(dict(preserved_text_files=len(manifest),preserved_editor_artifacts=9),ensure_ascii=False))
