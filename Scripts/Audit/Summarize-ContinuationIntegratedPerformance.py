from pathlib import Path
import json
import statistics
import sys

root = Path(__file__).resolve().parents[2]
name = sys.argv[1]
base = root / 'Saved/Validation/Continuation_20261014' / name
out = base / 'IntegratedPerformanceContext.json'
if out.exists(): raise RuntimeError('Preserve prior context')
manifest = json.loads((base / 'ComparisonManifest.json').read_text(encoding='utf-8-sig'))
result = {'name': name, 'compiled_variants': manifest,
          'scope_ko': '비교 variant는 실제 manifest/SHA 기준. 과거 분석기의 CA01–03 hardcoded label을 최신 variant로 오해하지 않는다. 원래 DS→RA13 CPU/packaged Render2→3 문제는 미해결.'}
if manifest['workload'] == 'CPU':
    rows = {}
    for phase in manifest['order']:
        folder = root / f'Saved/Validation/AuthoredAnimation_20261005/{name}_{phase}/Count200'
        rows[phase] = json.loads((folder / 'ProcessCpu.json').read_text(encoding='utf-8-sig'))
    result['whole_process_os_cpu'] = rows
    result['os_cpu_note_ko'] = '전체 setup/warm/measurement/종료의 OS user/kernel 누적 시간. 다른 measurement region·wait/thread elapsed 합계와 다름. 해당 전체 process work가 동일하다고 가정하지 않는다.'
else:
    report = json.loads((base / 'RenderComparisonSummary.json').read_text(encoding='utf-8-sig'))
    result['render_run_windows'] = report['runs']
    result['render_note_ko'] = '최신 B=CA01–13 Integrated1, A=RA27. one R burst/복귀·실제 render body200·socket0. A1/B1/B2/A2 run variance가 크므로 aggregate mean을 확정 개선으로 해석하지 않는다. GPU counter 지연/capture/stall 포함. 손/cloth/의상 품질은 별도.'
out.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps({'name': name, 'workload': manifest['workload'],
                  'summary': result.get('whole_process_os_cpu', result.get('render_run_windows'))}, ensure_ascii=False))
