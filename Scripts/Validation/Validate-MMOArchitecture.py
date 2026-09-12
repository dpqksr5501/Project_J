"""Validate the extension catalog and project module DAG; --write refreshes catalog artifacts."""
import argparse
import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
parser = argparse.ArgumentParser()
parser.add_argument('--write', action='store_true')
args = parser.parse_args()
data = json.loads((ROOT / 'Docs/Architecture/MMO_Content_Catalog.json').read_text(encoding='utf-8'))
assert data['schemaVersion'] == 1
features = data['features']
by_id = {f['id']: f for f in features}
assert len(by_id) == len(features), 'Duplicate content ID'
assert len({f['id'].casefold() for f in features}) == len(features), 'FName IDs are case insensitive'
owners = {'Account', 'Character', 'Group', 'World', 'Service', 'Client'}
for f in features:
    assert re.fullmatch(r'[A-Za-z]+\.[A-Za-z]+', f['id']), f['id']
    assert f['domain'] == f['id'].split('.')[0] and f['stateOwner'] in owners, f['id']
    assert len(f['dependencies']) == len(set(f['dependencies'])), f['id']

def validate_dag(graph):
    active, done = set(), set()
    def visit(key):
        assert key in graph, f'Unknown dependency: {key}'
        assert key not in active, f'Dependency cycle: {key}'
        if key in done:
            return
        active.add(key)
        for dep in graph[key]:
            visit(dep)
        active.remove(key)
        done.add(key)
    for key in graph:
        visit(key)

validate_dag({f['id']: f['dependencies'] for f in features})
module_files = list((ROOT / 'Source').glob('*/*.Build.cs'))
modules = {p.stem.removesuffix('.Build'): p.read_text(encoding='utf-8-sig') for p in module_files}
module_graph = {name: sorted({s for s in re.findall(r'"(Project_J\w*)"', text)
                            if s in modules and s != name}) for name, text in modules.items()}
validate_dag(module_graph)
assert not module_graph['Project_JMMO'], 'Foundation must not depend on game modules'
assert 'PublicDependencyModuleNames.Add("Core")' in modules['Project_JMMO']
assert '"Engine"' not in modules['Project_JMMO'] and '"HTTP"' not in modules['Project_JMMO']
assert 'Project_JMMO' in module_graph['Project_J'], 'Composition root must link foundation'
for module, forbidden in {
    'Project_JCore': {'Project_J', 'Project_JCharacter', 'Project_JGAS', 'Project_JMount'},
    'Project_JGAS': {'Project_J', 'Project_JCharacter', 'Project_JMount'},
    'Project_JMount': {'Project_J', 'Project_JCharacter'},
    'Project_JCharacter': {'Project_J'},
}.items():
    assert not forbidden.intersection(module_graph[module]), f'Reverse module dependency: {module}'

inl = '// Extension catalog; generated/maintained with Docs/Architecture/MMO_Content_Catalog.json.\n'
for f in features:
    fields = [f['id'], f['label'], f['domain'], f['stateOwner'], ','.join(f['dependencies'])]
    inl += 'PROJECTJ_FEATURE(' + ', '.join(json.dumps(v, ensure_ascii=False) for v in fields) + ')\n'
md = '# MMORPG 콘텐츠 확장 카탈로그\n\n'
md += f'{len(features)}개 연결 항목. **구현 완료 목록이 아니다.** 공통 기반과 미래 콘텐츠의 의존성·주 상태 소유자를 정의한다.\n\n'
md += '기존 구현 여부, 연구 출처, 권한·저장·스레드 규칙은 [설계 보고서](MMO_Foundation_2026-09-12.md)를 따른다. '
md += '상태 소유자는 주 aggregate 범위이며, 거래처럼 여러 소유자가 참여할 수 있다. '
md += '모든 항목은 기본적으로 비활성 확장 계약이다. Foundation 항목도 실제 구현 범위는 설계 보고서에서 구분한다.\n\n'
for domain in dict.fromkeys(f['domain'] for f in features):
    md += f'## {domain}\n\n| ID | 기능 | 주 상태 소유자 | 필요 계약 |\n|---|---|---|---|\n'
    for f in features:
        if f['domain'] == domain:
            md += f"| `{f['id']}` | {f['label']} | {f['stateOwner']} | {', '.join(f['dependencies']) or '—'} |\n"
    md += '\n'
for relative, expected in {
    'Source/Project_JMMO/Private/FeatureCatalog.inl': inl,
    'Docs/Architecture/MMO_Content_Catalog.md': md,
}.items():
    path = ROOT / relative
    if args.write:
        path.write_text(expected, encoding='utf-8')
    else:
        assert path.read_text(encoding='utf-8') == expected, f'Generated catalog drift: {relative}; run --write'
print(json.dumps({'passed': True, 'features': len(features), 'domains': len({f['domain'] for f in features}),
                  'projectModuleDependencies': module_graph}, ensure_ascii=False, indent=2))
