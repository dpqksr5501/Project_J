"""Prepare explicitly versioned evidence helpers; do not modify the immutable RA13 freeze."""
from pathlib import Path
out=Path(__file__).resolve().parent
freeze=(out/'Freeze-Final.py').read_text(encoding='utf-8-sig')
for old,new in [
    ('FinalSourceSnapshot','RA14SourceSnapshot'),('FinalEditorBinaries','RA14EditorBinaries'),
    ('FinalSourceHashes.json','RA14SourceHashes.json'),('FinalEditorBinaryHashes.json','RA14EditorBinaryHashes.json'),
    ('SourceChangeManifest.json','RA14SourceChangeManifest.json'),('ReauditSourceChanges.patch','RA14SourceChanges.patch')]:
    freeze=freeze.replace(old,new)
target=out/'Freeze-RA14.py'
if target.exists():raise RuntimeError('Prepared helper exists; preserve it.')
target.write_text(freeze,encoding='utf-8')
print('Prepared Freeze-RA14.py; not executed. RA13 baseline/freeze remains unchanged.')
