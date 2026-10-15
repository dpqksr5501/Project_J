from pathlib import Path
import hashlib
import json
import sys

root = Path(__file__).resolve().parents[2]
stage_name, game_name, cook_name = sys.argv[1:4]
base = root / 'Saved/Validation/Continuation_20261014'
out = base / stage_name / 'PackageIdentity.json'
if out.exists():
    raise RuntimeError('Preserve prior package receipt')
exe = base / stage_name / 'Package/Windows/Project_J/Binaries/Win64/Project_J.exe'
game = json.loads((base / game_name / 'GameVersion.json').read_text(encoding='utf-8-sig'))
expected = next(r['sha256'] for r in game['artifacts'] if r['path'].endswith('/Project_J.exe'))
actual = hashlib.sha256(exe.read_bytes()).hexdigest()
result = {'game': game_name, 'cook': cook_name, 'stage': stage_name,
          'source_version': game['editor_frozen_version'], 'source_matches_frozen': game['actual_game_source_matches_frozen'],
          'packaged_game': exe.relative_to(root).as_posix(), 'sha256': actual, 'matches_direct_ubt_game': actual == expected,
          'scope_ko': '실제 packaged native exe와 direct UBT Game SHA 대조. bootstrap exe와 구분. 별도 configured Cook closure이며 CookAll·실제 실행/시각·원격·전체 감사 완료를 증명하지 않는다.'}
out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
if actual != expected or not result['source_matches_frozen']:
    raise RuntimeError('Package/source identity mismatch')
print(json.dumps(result, ensure_ascii=False))
