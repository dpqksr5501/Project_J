from pathlib import Path
import hashlib
import json

root=Path(__file__).resolve().parents[2]
out=root/'Saved/Validation/Continuation_20261014/HistoricalComparisonVerification.json'
reaudit=root/'Saved/Validation/Reaudit_20261014'
checks=[]
for label,folder,manifest in [
    ('DS_source',reaudit/'Baseline',reaudit/'BaselineManifest.json'),
    ('RA13_source',reaudit/'FinalSourceSnapshot',reaudit/'FinalSourceHashes.json'),
    ('DS_editor',reaudit/'BaselineBinaries',root/'Saved/Validation/DeepAudit_20261013/FinalEditorBinaryHashes.json'),
    ('RA13_editor',reaudit/'FinalEditorBinaries',reaudit/'FinalEditorBinaryHashes.json')]:
    rows=json.loads(manifest.read_text(encoding='utf-8-sig')); mismatches=[]
    for row in rows:
        path=folder/row.get('path',row.get('file'))
        actual=hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None
        if actual != row['sha256'].lower(): mismatches.append({'path':str(path),'actual':actual,'expected':row['sha256']})
    checks.append({'label':label,'rows':len(rows),'mismatches':mismatches})
changes=json.loads((reaudit/'SourceChangeManifest.json').read_text(encoding='utf-8-sig'))
production=[r['path'] for r in changes if r['path'].startswith('Source/') and '/Tests/' not in r['path'] and not r['path'].endswith('Tests.cpp')]
result={'scope_ko':'역사적 DS01–18/RA01–13 frozen source+완전한9 binary 검증. 현재 asset과 실행 스크립트로 A/B 비교하며 복원은 실행 당시 최신9만 사용. 당시 asset/열 조건 동일을 주장하지 않음.',
        'checks':checks,'changed_production_paths':production,'valid':all(not c['mismatches'] for c in checks)}
out.write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'checks':[{'label':c['label'],'rows':c['rows'],'mismatches':len(c['mismatches'])} for c in checks], 'production':production},ensure_ascii=False))
assert result['valid']
