"""Native registry batch, explicitly invoked in the source editor through MCP UI."""
import json,traceback
from pathlib import Path
import unreal
root=Path(__file__).resolve().parents[2];out=root/'Saved/Validation/StarterWorld'
try:
    assert Path(unreal.Paths.project_dir()).resolve().name=='MedievalVillageMegascansS'
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    roots=['/Game/Meshes','/Game/Megascans','/Game/ThirdParty']
    seeds=sorted({str(a.package_name) for folder in roots for a in registry.get_assets_by_path(folder,True) if str(a.asset_class_path.asset_name)=='StaticMesh'})
    options=unreal.AssetRegistryDependencyOptions(include_soft_package_references=True,include_hard_package_references=True,include_searchable_names=False,include_soft_management_references=False,include_hard_management_references=False)
    pending=seeds[:];seen=set();missing=[];edges={};external=set()
    while pending:
        package=pending.pop()
        if package in seen:continue
        seen.add(package)
        if not package.startswith('/Game/'):
            external.add(package);continue
        if not registry.get_assets_by_package_name(package):missing.append(package);continue
        deps=[str(p) for p in registry.get_dependencies(package,options)]
        edges[package]=deps;pending.extend(deps)
    report=dict(seeds=seeds,packages=sorted(edges),missing_source_packages=sorted(missing),dependency_edges=edges,external_packages=sorted(external),unresolved_external_game_dependencies=[],source_mcp_checked=True,registry_api='Native AssetRegistryDependencyOptions via explicit MCP Slate console')
    (out/'Megascans-dependencies-native.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('LIBRARY_DEPENDENCIES_EXPORTED '+str(len(edges))+' missing '+str(len(missing)))
except Exception:
    (out/'library-export-error.txt').write_text(traceback.format_exc(),encoding='utf-8');unreal.log_error(traceback.format_exc())
