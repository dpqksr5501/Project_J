"""Preserve the original Cook; give UAT its official platform directory layout."""
from pathlib import Path
import hashlib
import json
import shutil
import sys

root=Path(__file__).resolve().parents[2]
base=root/'Saved/Validation/Continuation_20261014'
source=(base/sys.argv[1]/'Cooked').resolve()
output=(base/sys.argv[2]).resolve()
if not source.is_relative_to(base.resolve()) or not output.is_relative_to(base.resolve()): raise RuntimeError('Outside continuation evidence root.')
if output.exists(): raise RuntimeError('Preserve earlier output.')
target=output/'Windows'
# New Cook invocations already expand [Platform]. Do not wrap Windows twice.
platform_source=source/'Windows' if (source/'Windows').is_dir() else source
rows=[]
for path in platform_source.rglob('*'):
    if not path.is_file(): continue
    relative=path.relative_to(platform_source); dest=target/relative
    dest.parent.mkdir(parents=True,exist_ok=True)
    shutil.copyfile(path,dest)
    digest=hashlib.sha256(path.read_bytes()).hexdigest()
    if digest != hashlib.sha256(dest.read_bytes()).hexdigest(): raise RuntimeError(f'Copy hash mismatch: {relative}')
    rows.append({'path':relative.as_posix(),'sha256':digest,'bytes':path.stat().st_size})
(output/'LayoutReceipt.json').write_text(json.dumps({'original':source.relative_to(root).as_posix(), 'platform_source':platform_source.relative_to(root).as_posix(),
    'stage_input':target.relative_to(root).as_posix(),'all_copies_sha_match':True,'files':rows,
    'scope_ko':'원본 Cook 출력/로그 보존. 플랫폼 Windows 디렉터리를 가진 별도 Stage 입력 사본. ue.projectstore 내용/인증 정보는 공개하지 않음; Zen 원자료 수명은 shared cache와 구분.'},ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'copied_files':len(rows),'bytes':sum(r['bytes'] for r in rows),'all_copies_sha_match':True}))
