from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import subprocess

root = Path(__file__).resolve().parents[2]
base = root / 'Saved/Validation/Continuation_20261014'
out = base / 'FinalCheckpoint1'
out.mkdir(exist_ok=False)
git = lambda *args: subprocess.check_output(['git','-c','core.fsmonitor=false',*args], cwd=root).decode('utf-8').strip()
head = git('rev-parse','HEAD')
frozen = json.loads((base / 'CA01_13_Integrated1/SourceHashes.json').read_text(encoding='utf-8-sig'))
rows = [r for r in frozen if r['path'].startswith(('Source/','Config/','Plugins/ProjectJExperiments/Source/')) or r['path']=='Project_J.uproject']
source_bad = [r['path'] for r in rows if hashlib.sha256((root/r['path']).read_bytes()).hexdigest()!=r['sha256']]
initial = json.loads((root/'Docs/Review/Audits/ContinuationEvidence_2026-10-14/InitialHashChecks.json').read_text(encoding='utf-8-sig'))
preserved = [{'path':r['path'],'matches':hashlib.sha256((root/r['path']).read_bytes()).hexdigest()==r['expected_sha256']}
             for r in initial if r['group']=='preserved']
editor = json.loads((base/'CA01_13_Integrated1/EditorBinaryHashes.json').read_text(encoding='utf-8-sig'))
editor_bad = [r['file'] for r in editor if hashlib.sha256((root/'Binaries/Win64'/r['file']).read_bytes()).hexdigest()!=r['sha256']]
result = {'utc':datetime.now(timezone.utc).isoformat(),'head':head,'branch':git('branch','--show-current'),
          'expected_handoff_commit_matches':head=='855a1c8e111c94a5dffdd08ad05c68bdea2accec',
          'source_version':'CA01_13_Integrated1','checked_game_source_files':len(rows),'source_mismatches':source_bad,
          'editor_artifacts':len(editor),'editor_mismatches':editor_bad,'preserved':preserved,
          'content_git_changes':git('status','--short','--','Content'),
          'whole_project_complete':False,
          'scope_ko':'현재 Git/실제 compiled source·Editor9 복원·보존13·Content diff 확인. Scripts/문서 변경은 compile된 runtime 코드 변경과 분리. 전체 감사 완료나 미확인 접근/운영 증거가 아님.'}
(out/'Checkpoint.json').write_text(json.dumps(result,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
(out/'GitStatus.txt').write_text(git('status','--short')+'\n',encoding='utf-8')
if source_bad or editor_bad or not all(r['matches'] for r in preserved) or not result['expected_handoff_commit_matches'] or result['content_git_changes']:
    raise RuntimeError('Checkpoint preservation/provenance mismatch')
print(json.dumps(result,ensure_ascii=False))
