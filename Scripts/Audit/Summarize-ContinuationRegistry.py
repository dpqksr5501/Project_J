"""Normalize public registry display strings without querying UObject properties again."""
from pathlib import Path
from collections import Counter
import hashlib
import json
import re

root = Path(__file__).resolve().parents[2]
raw_path = root / 'Saved/Validation/Continuation_20261014/AssetRegistry1/registry.json'
data = json.loads(raw_path.read_text(encoding='utf-8-sig'))
classes = Counter()
for row in data['assets']:
    match = re.search(r'package_name: "([^"]+)", asset_name: "([^"]+)"', row['asset_class_path'])
    if not match:
        raise RuntimeError('Unrecognized public class display; do not invent a type.')
    row['canonical_class_path'] = match[1] + '.' + match[2]
    classes[row['canonical_class_path']] += 1
unregistered = []
for row in data['disk_not_in_registry']:
    original = row['path'].replace(' (1).uasset', '.uasset')
    sha = lambda p: hashlib.sha256((root / p).read_bytes()).hexdigest()
    unregistered.append({**row, 'sha256':sha(row['path']), 'similarly_named_path':original,
                         'similarly_named_sha256':sha(original),
                         'same_bytes':sha(row['path']) == sha(original),
                         'status_ko':'서로 다른 파일 내용 보존. 현재 registry 미등록·제작 역할/내용 미검토. 삭제/이동/이름 변경 없음.'})
data['public_registry_class_counts'] = dict(sorted(classes.items()))
data['unregistered_file_checks'] = unregistered
out = root / 'Docs/Review/Audits/ContinuationEvidence_2026-10-14'
(out / 'AssetRegistryReconciliation.json').write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'registry_packages':data['registry_package_count'], 'disk_inventory':data['inherited_disk_package_count'],
                  'unregistered':unregistered, 'classes':dict(classes), 'possible_cue_providers':data['possible_gameplay_cue_providers']},ensure_ascii=False))
