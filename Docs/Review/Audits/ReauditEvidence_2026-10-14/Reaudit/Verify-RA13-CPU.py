from pathlib import Path
import hashlib, json

root = Path(__file__).resolve().parents[3]
out = Path(__file__).resolve().parent
source = json.loads((out/'FinalSourceHashes.json').read_text(encoding='utf-8-sig'))
binaries = json.loads((out/'FinalEditorBinaryHashes.json').read_text(encoding='utf-8-sig'))
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
source_mismatch = [r['path'] for r in source if sha(root/r['path']) != r['sha256']]
binary_mismatch = [r['file'] for r in binaries if sha(root/'Binaries/Win64'/r['file']) != r['sha256']]
phases=[]
for phase in ['A1', 'B1', 'B2', 'A2']:
    folder=root/f'Saved/Validation/AuthoredAnimation_20261005/Reaudit_ABBA_Long_{phase}'
    process=json.loads((folder/'Count200/process.json').read_text(encoding='utf-8-sig'))
    cpu=json.loads((folder/'Count200/cpu.json').read_text(encoding='utf-8-sig'))
    provenance=json.loads((folder/'comparison-provenance.json').read_text(encoding='utf-8-sig'))
    assert process['exitCode']==0 and process['hasExited'] and process['nativeHandleCapturedBeforeWait']
    assert cpu['completed'] and len(cpu['rows'])==16 and cpu['fixture_version']==2
    assert provenance['allEightModulesAndModulesFileSwitched'] and len(provenance['artifacts'])==9
    assert all(r['measured_ticks']==600 and r['warmup_ticks']==240 for r in cpu['rows'])
    compiled=out/('BaselineBinaries' if phase.startswith('A') else 'FinalEditorBinaries')
    assert all(sha(compiled/r['file']).lower()==r['sha256'].lower() for r in provenance['artifacts'])
    phases.append(dict(phase=phase,process=process,rows=len(cpu['rows']),artifacts=9))
payload=dict(source_version='RA01-13',source_files=len(source),editor_artifacts=len(binaries),
    source_mismatch=source_mismatch,restored_binary_mismatch=binary_mismatch,phases=phases,
    meaning='Verifies compiled-source and binary-set provenance; does not assert all code/assets manually audited or CPU regression resolved.')
(out/'CPUProvenanceVerification.json').write_text(json.dumps(payload,indent=2),encoding='utf-8')
print(json.dumps(payload))
assert not source_mismatch and not binary_mismatch
