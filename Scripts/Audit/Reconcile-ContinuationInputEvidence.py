"""Reconcile prior actual IMC reads; do not re-query or promote missing CDO/instance evidence."""
from pathlib import Path
import hashlib
import json
import subprocess

root=Path(__file__).resolve().parents[2]
base=root/'Docs/Review/Audits/DeepEvidence_2026-10-13/MCP/AssetFields2'
rows=[]
for label,filename in [('IMC_Default','Root38_Values.json'),('IMC_MouseLook','Root39_Values.json')]:
    path=base/filename
    rpc=json.loads(path.read_text(encoding='utf-8-sig'))
    values=json.loads(json.loads(rpc['result']['content'][0]['text'])['returnValue'])
    asset=root/f'Content/Input/{label}.uasset'
    rows.append({'asset':asset.relative_to(root).as_posix(),'current_sha256':hashlib.sha256(asset.read_bytes()).hexdigest(),
                 'inherited_rpc':path.relative_to(root).as_posix(),'rpc_sha256':hashlib.sha256(path.read_bytes()).hexdigest(),
                 'deprecated_mappings':values['mappings'],'default_key_mappings':values['defaultKeyMappings']['mappings'],
                 'profile_overrides':values['mappingProfileOverrides'],
                 'limits_ko':'기존 실제 reflected 조회를 계승. modifier subobject의 참조는 실제 numeric 값/모든 장치 품질 완료가 아님. 새 UE 조회를 수행하지 않음.'})
status=subprocess.check_output(['git','-c','core.fsmonitor=false','status','--porcelain','--','Content/Input','Content/ThirdPerson/Blueprints','Content/Character_BPs'],cwd=root,text=True).strip()
result={'scope_ko':'저장된 IMC 2개 실제 default mapping27행과 source GetMappings() 계약의 대응. UE5.8 GetMappings는 DefaultKeyMappings를 반환하므로 deprecated Mappings 빈 배열을 미연결로 해석하지 않는다.',
        'working_asset_changes':status,'contexts':rows,
        'current_source_contract_ko':'Controller Setup→mapping lease; Player Setup→ActionSet→binding, four semantic directions all-or-nothing and no game-key exception. movement gameplay is immediate; semantic stop/direction settles after EnhancedInput update. Existing mouse chord grace is authored input ambiguity policy, not a new animation symptom timer.',
        'unfinished_ko':['Controller BP DefaultMappingContexts/MobileExcluded arrays and Greatsword input CDO identity not present in these two receipts.',
                         'BP event/instance overrides, profile override selection, focus/device transitions and GC participants require distinct evidence.',
                         'Current Git clean asset paths show no working edit; this does not by itself prove all assets identical to every historical capture.']}
(root/'Docs/Review/Audits/ContinuationEvidence_2026-10-14/InputEvidenceReconciliation.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'contexts':len(rows),'default_mapping_rows':sum(len(r['default_key_mappings']) for r in rows),'working_asset_changes':status},ensure_ascii=False))
