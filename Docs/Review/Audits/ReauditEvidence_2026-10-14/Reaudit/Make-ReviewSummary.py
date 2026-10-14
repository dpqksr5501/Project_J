from pathlib import Path
import json,collections
out=Path(__file__).resolve().parent
ledger=json.loads((out/'SourceReviewLedger.json').read_text(encoding='utf-8-sig'))
manifest=out/'RA27SourceChangeManifest.json'
if not manifest.exists(): manifest=out/'RA25SourceChangeManifest.json'
if not manifest.exists(): manifest=out/'RA19SourceChangeManifest.json'
changes=json.loads(manifest.read_text(encoding='utf-8-sig'))
changed={r['path'] for r in changes}
summary=dict(total_files=len(ledger['files']), kinds=dict(collections.Counter(r['kind'] for r in ledger['files'])),
    additional_implementation_bodies_read=ledger['additional_pending_implementation_bodies_read'],
    previous_named_contract_files=sum(r.get('previous_review')=='named_contract_scope_review' for r in ledger['files']),
    additional_named_scopes=[dict(path=r['path'],scope=r['reaudit_scope'],changed_since_reaudit_baseline=r['path'] in changed)
        for r in ledger['files'] if 'reaudit_scope' in r],
    whole_file_manual_review_claimed=False,
    limitation_ko='목록·구현 본문 읽기·명명된 계약·실제 수정·실행 검증을 합쳐 전수 정밀 완료 비율로 계산하지 않는다. 이전 named scope도 모든 함수 검토가 아니다.')
(out/'ReviewSummary.json').write_text(json.dumps(summary,ensure_ascii=False,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in summary.items() if k not in {'additional_named_scopes','limitation_ko'}}))
