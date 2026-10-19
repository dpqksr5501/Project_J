"""Run in the task-owned AssetStaging project, never in the original asset library.
Engine dependency migration preserves package references; byte paths are never patched.
"""
import json
from pathlib import Path
import unreal

ROOT = Path(__file__).resolve().parents[2]
assert Path(unreal.Paths.project_dir()).resolve() == ROOT / 'Saved/WorldRegionA/AssetStaging'
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(True)
seeds = [
    '/Game/SkyTown/Meshes/SM_House01', '/Game/SkyTown/Meshes/SM_House02',
    '/Game/SkyTown/Meshes/SM_House03', '/Game/SkyTown/Meshes/SM_House04',
    '/Game/SkyTown/Meshes/SM_BellTower01', '/Game/SkyTown/Meshes/SM_RockRoad01',
    '/Game/SkyTown/Meshes/SM_RopeBridge01_40m', '/Game/SkyTown/Meshes/SM_Rock01',
    '/Game/SkyTown/Meshes/SM_AutumnTree01', '/Game/SkyTown/Meshes/SM_AutumnTree02',
    '/Game/ProceduralNaturePack/Meshes/trees_w_roots/large_tree_w_roots_a',
    '/Game/Map_Resource/Variants/ProjectExisting/ProceduralNaturePack/Meshes/rocks_boulders/rock_boulder_a',
    '/Game/ProceduralNaturePack/Meshes/rocks_boulders/rock_boulder_b1',
    '/Game/ProceduralNaturePack/Meshes/grass/grass_small_01',
    '/Game/AncientTempleRuins/Environment/Rock/Rock01/SM_Rock01',
    '/Game/AncientTempleRuins/Environment/Construction/Pillar/SM_Pillar_Cube_01_A',
    '/Game/AncientTempleRuins/Environment/Construction/Buildings/SM_Facade_Custom_01',
]
options = unreal.AssetRegistryDependencyOptions(include_soft_package_references=True, include_hard_package_references=True,
    include_searchable_names=False, include_soft_management_references=False, include_hard_management_references=False)
packages = set()
pending = list(seeds)
while pending:
    package = str(pending.pop())
    if package in packages or not package.startswith('/Game/'):
        continue
    assert unreal.EditorAssetLibrary.does_asset_exist(package), 'Missing dependency: '+package
    packages.add(package)
    pending.extend(str(x) for x in registry.get_dependencies(package, options))

# The scratch project's only game assets are copied environment packs. Retain pack/subfolder provenance.
assert all(x.split('/')[2] in {'SkyTown','ProceduralNaturePack','AncientTempleRuins'} for x in packages)
tools = unreal.AssetToolsHelpers.get_asset_tools()
renames = []
for package in sorted(packages):
    asset = unreal.load_asset(package)
    assert asset, package
    destination = package.replace('/Game/','/Game/Map_Resource/',1)
    assert not unreal.EditorAssetLibrary.does_asset_exist(destination), destination
    renames.append(unreal.AssetRenameData(asset, destination.rsplit('/',1)[0], destination.rsplit('/',1)[1]))
assert tools.rename_assets(renames), 'Engine rename failed'
assert unreal.EditorAssetLibrary.save_directory('/Game/Map_Resource', only_if_is_dirty=False, recursive=True)
migrated_seeds = [x.replace('/Game/','/Game/Map_Resource/',1) for x in seeds]
migration = unreal.MigrationOptions()
migration.set_editor_property('prompt', False)
migration.set_editor_property('ignore_dependencies', True)
migration.set_editor_property('asset_conflict', unreal.AssetMigrationConflict.SKIP)
# Registry notifications for renamed packages can be deferred until the next editor tick.
# Explicitly migrate the already-computed full closure instead of re-querying fresh names.
tools.migrate_packages([x.replace('/Game/','/Game/Map_Resource/',1) for x in sorted(packages)], str(ROOT/'Content'), migration)
result = {'seed_packages':migrated_seeds,'dependency_packages':[x.replace('/Game/','/Game/Map_Resource/',1) for x in sorted(packages)],
          'original_library_modified':False,'destination':'/Game/Map_Resource','method':'Engine AssetTools rename in scratch project, then dependency migration'}
(ROOT/'Saved/Validation/WorldRegionA/migration.json').write_text(json.dumps(result,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('PROJECT_J_WORLD_MIGRATION_COMPLETE '+str(len(packages)))
