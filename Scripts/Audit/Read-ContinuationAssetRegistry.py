"""Public Asset Registry metadata only. No UObject property reflection or asset writes."""
import json
import re
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
out = root / 'Saved/Validation/Continuation_20261014/AssetRegistry1'
out.mkdir(parents=True, exist_ok=True)
receipt = out / 'registry.json'
if receipt.exists():
    raise RuntimeError('Preserve earlier asset evidence.')
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(synchronous_search=True)
options = unreal.AssetRegistryDependencyOptions(
    include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=False, include_soft_management_references=True,
    include_hard_management_references=True)
assets = registry.get_all_assets(include_only_on_disk_assets=True)
records = []
packages = {}
for asset in assets:
    package = str(asset.package_name)
    if not package.startswith('/Game/'):
        continue
    if package not in packages:
        packages[package] = sorted(str(value) for value in registry.get_dependencies(asset.package_name, options))
    tags = {}
    if hasattr(unreal.AssetRegistryHelpers, 'get_tag_value'):
        for tag in ('ParentClass', 'NativeParentClass', 'GeneratedClass'):
            tags[tag] = str(unreal.AssetRegistryHelpers.get_tag_value(asset, tag))
    records.append({'package':package, 'asset_name':str(asset.asset_name),
                    'asset_class_path':str(asset.asset_class_path), 'public_registry_tags':tags, 'dependencies':packages[package],
                    'status_ko':'공개 registry metadata 수집·상세 내용 검토 미완료'})
inventory = json.loads((root / 'Docs/Handoffs/WholeProjectAudit_2026-10-14/ContinuationCoverageInventory.json').read_text(encoding='utf-8-sig'))
disk_packages = {}
for row in inventory['content_package_inventory']:
    relative = row['path']
    path = Path(relative)
    package = '/Game/' + str(path.relative_to('Content').with_suffix('')).replace('\\', '/')
    disk_packages[package] = relative
literal_references = []
for row in inventory['source']:
    path = root / row['path']
    if not path.is_file():
        continue
    matches = sorted(set(re.findall(r'(/Game/[A-Za-z0-9_/-]+)(?:\.[A-Za-z0-9_]+)?', path.read_text(encoding='utf-8-sig', errors='replace'))))
    if matches:
        role = 'test_fixture' if row['kind'] == 'test_or_fixture' else 'authoring' if row['module'] in ('Project_JCharacterEditor','Project_JAnimationNodes','ProjectJExperiments') else 'production_literal_candidate'
        literal_references.append({'source':row['path'], 'role':role, 'packages':matches,
                                    'meaning_ko':'문자열 후보; 실제 호출/동적 생성/현재 도달성 증명 아님'})
receipt.write_text(json.dumps({
    'meaning_ko':'전체 저장 패키지의 공개 type/dependency 대응 목록. BP/ABP 그래프·CDO·runtime instance·품질 검토 완료가 아니다.',
    'asset_mutation':False, 'asset_save':False, 'public_property_reflection':False,
    'registry_asset_count':len(records), 'registry_package_count':len(packages),
    'inherited_disk_package_count':len(disk_packages),
    'disk_not_in_registry':[{'package':p,'path':path} for p,path in sorted(disk_packages.items()) if p not in packages],
    'registry_not_in_disk_inventory':sorted(p for p in packages if p not in disk_packages),
    'source_literal_references':literal_references,
    'possible_gameplay_cue_providers':[r['package'] for r in records if 'GameplayCueNotify' in str(r)],
    'assets':records}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.log('Continuation public registry metadata complete; detailed asset review remains open.')
