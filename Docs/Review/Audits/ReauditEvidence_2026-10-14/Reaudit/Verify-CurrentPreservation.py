from pathlib import Path
import hashlib,json
root=Path(__file__).resolve().parents[3]
saved=Path(__file__).resolve().parent
expected={
 'Content/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Woman_RunTIme.uasset':'27283D5F8A7F9F5B67FD493EF0D5CCBCCC4B75CE640E2264B3B02D739A995D9E',
 'Content/Animation_Logic/RTG_Greatsword_UE5.uasset':'74E3E616BC099FFBB1104B8EDB18FE79ED59569D8D4B324CD680C22D38FE9982',
 'Docs/Handoffs/WholeProjectAudit_2026-10-13/Project_J_Whole_Project_Audit_Handoff_2026-10-13.md':'DB19C60AE025D12FB2C5EBB2EDB084A113F543CD7D881AA4A81F2225BE174315',
}
attacks=json.loads((saved/'PreviousAttackAssetHashesFinal.json').read_text(encoding='utf-8-sig'))
expected.update({a['file']:a['after'] for a in attacks})
records=[]
for rel,sha in expected.items():
 actual=hashlib.sha256((root/rel).read_bytes()).hexdigest().upper()
 records.append(dict(path=rel,sha256=actual,matches=actual==sha.upper()))
source=json.loads((saved/'RA27SourceHashes.json').read_text(encoding='utf-8-sig'))
source_bad=[r['path'] for r in source if hashlib.sha256((root/r['path']).read_bytes()).hexdigest()!=r['sha256']]
binaries=json.loads((saved/'RA27EditorBinaryHashes.json').read_text(encoding='utf-8-sig'))
binary_bad=[r['file'] for r in binaries if hashlib.sha256((root/'Binaries/Win64'/r['file']).read_bytes()).hexdigest()!=r['sha256']]
result=dict(preserved=records,sourceFiles=len(source),sourceMismatches=source_bad,editorArtifacts=len(binaries),editorMismatches=binary_bad,
            meaning='Current frozen compiled provenance and original user/prior attack work preservation; not whole integrity review.')
(saved/'CurrentPreservation_RA27.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps(dict(preservedFiles=len(records),preservationMismatches=[r['path'] for r in records if not r['matches']],sourceMismatches=source_bad,editorMismatches=binary_bad)))
if source_bad or binary_bad or not all(r['matches'] for r in records):raise RuntimeError('Preservation mismatch; do not overwrite or restore blindly.')
