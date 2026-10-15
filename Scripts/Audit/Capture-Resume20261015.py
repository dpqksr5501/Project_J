from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[2]
out = root/'Saved/Validation/Continuation_20261014/Resume_20261015_1'
out.mkdir(exist_ok=False)
sha = lambda p: hashlib.sha256(p.read_bytes()).hexdigest()
frozen = root/'Saved/Validation/Continuation_20261014/CA01_13_Integrated1'
sources = json.loads((frozen/'SourceHashes.json').read_text(encoding='utf-8-sig'))
runtime = [r for r in sources if r['path'].startswith(('Source/','Config/','Plugins/ProjectJExperiments/Source/')) or r['path']=='Project_J.uproject']
editor = json.loads((frozen/'EditorBinaryHashes.json').read_text(encoding='utf-8-sig'))
initial = json.loads((root/'Docs/Review/Audits/ContinuationEvidence_2026-10-14/InitialHashChecks.json').read_text(encoding='utf-8-sig'))
git = lambda *args: subprocess.check_output(['git','-c','core.fsmonitor=false',*args],cwd=root).decode('utf-8')
result = {'utc':datetime.now(timezone.utc).isoformat(),'client_date':'2026-10-15','head':git('rev-parse','HEAD').strip(),
          'branch':git('branch','--show-current').strip(),'runtime_count':len(runtime),
          'runtime_mismatches':[r['path'] for r in runtime if sha(root/r['path'])!=r['sha256']],
          'editor_mismatches':[r['file'] for r in editor if sha(root/'Binaries/Win64'/r['file'])!=r['sha256']],
          'preserved_mismatches':[r['path'] for r in initial if r['group']=='preserved' and sha(root/r['path'])!=r['expected_sha256']],
          'user_handoff_diff_preserved':git('diff','--binary','--','Docs/Handoffs/WholeProjectAudit_2026-10-14/Project_J_Complete_Audit_Continuation_2026-10-14.md').strip()==(root/'Saved/Validation/Continuation_20261014/InitialUserChanges.patch').read_text(encoding='utf-8').strip(),
          'scope_ko':'재개 당시 실제 기준점·runtime636·Editor9·보존13/user diff 대조. 별도 AdaptiveEcosystem 작업은 이 저장소 변경으로 추정하지 않으며 실제 Git/source에 나타난 영향만 대조. 전체 감사 미완료.'}
(out/'ResumeIdentity.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'StartingGitStatus.txt').write_text(git('status','--short'),encoding='utf-8')
print(json.dumps(result,ensure_ascii=False))
if any(result[k] for k in ['runtime_mismatches','editor_mismatches','preserved_mismatches']) or not result['user_handoff_diff_preserved']:
    raise RuntimeError('Review differences before changing anything')
