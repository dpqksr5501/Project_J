"""Resolve a curated pack's real dependency closure through MCP, then stage bytes.

Unreal package paths stay unchanged during transfer. Never patch package bytes
or overwrite a conflicting existing package. Source project files stay intact.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import shutil

ROOT=Path(__file__).resolve().parents[2]
spec=importlib.util.spec_from_file_location('world_mcp',ROOT/'Scripts/Editor/World-MCP.py')
mcp=importlib.util.module_from_spec(spec);spec.loader.exec_module(mcp)
SEEDS=['/Game/MedCastle/Mesh/Houses_Towers/'+name for name in ['SM_Gate','SM_CastleGate_B','SM_MainTower2Doors','SM_CastleTowerTall','SM_SmallHouse','SM_MiddleHouse','SM_MainHouse','SM_BigHouse']]
SEEDS+=['/Game/MedCastle/Mesh/HouseParts/SM_CastleWall','/Game/MedCastle/Mesh/Bridges/SM_ArchedBridge']
SCRIPT='''import json
def dependencies(path):
    return execute_tool("editor_toolset.toolsets.asset.AssetTools.get_dependencies",json.dumps({"asset_path":path}))["returnValue"]

def exists(path):
    return execute_tool("editor_toolset.toolsets.asset.AssetTools.exists",json.dumps({"path":path}))["returnValue"]
def run():
    pending=SEEDS[:];found=[];external=[];missing=[];edges={}
    while pending:
        p=pending.pop()
        if p in found or p in missing:continue
        if not p.startswith("/Game/"):continue
        if not p.startswith(SCOPE):
            external.append(p);continue
        if not exists(p):missing.append(p);continue
        found.append(p);edges[p]=dependencies(p);pending.extend(edges[p])
    return {"seeds":SEEDS,"packages":sorted(found),"unresolved_external_game_dependencies":external,"missing_source_packages":missing,"dependency_edges":edges}
'''

if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--source-content',type=Path,required=True)
    parser.add_argument('--port',type=int,default=8001)
    parser.add_argument('--seed-file',type=Path)
    parser.add_argument('--scope',default='/Game/MedCastle/')
    parser.add_argument('--output',type=Path,default=ROOT/'Saved/Validation/StarterWorld/MedCastle-transfer.json')
    parser.add_argument('--local-packages',action='store_true',help='Explicit older-library copy; inspect through target MCP afterwards')
    parser.add_argument('--resolved-manifest',type=Path,help='Native source-editor registry export invoked through MCP')
    args=parser.parse_args()
    assert args.source_content.resolve().name=='Content'
    assert args.source_content.resolve().parent.name.lower() not in ['gasp','gameanimationsample']
    seeds=json.loads(args.seed_file.read_text(encoding='utf-8')) if args.seed_file else SEEDS
    script=SCRIPT.replace('SEEDS',repr(seeds)).replace('SCOPE',repr(args.scope))
    # Replace only the literal placeholders, leaving the function body fixed.
    if args.resolved_manifest:
        payload=json.loads(args.resolved_manifest.read_text(encoding='utf-8'))
        assert payload['source_mcp_checked'] and payload['packages']
    elif args.local_packages:
        assert args.seed_file and args.scope!='/Game/'
        assert all(p.startswith(args.scope) for p in seeds)
        payload=dict(seeds=seeds,packages=seeds,unresolved_external_game_dependencies=[],source_mcp_checked=False)
    else:
        result=mcp.invoke(args.port,'tools/call',{'name':'call_tool','arguments':{'toolset_name':'editor_toolset.toolsets.programmatic.ProgrammaticToolset','tool_name':'execute_tool_script','arguments':{'script':script}}})
        if result.get('error') or result.get('result',{}).get('isError'):raise RuntimeError(result)
        payload=json.loads(result['result']['content'][0]['text'])['returnValue']
        if isinstance(payload,str):payload=json.loads(payload)
        payload['source_mcp_checked']=True
    assert not payload['unresolved_external_game_dependencies'],payload
    assert not payload.get('missing_source_packages'),payload
    packages=set(payload['packages'])
    assert packages and set(seeds)<=packages,'Requested seed missing from resolved packages'
    assert all(isinstance(p,str) and p.startswith('/Game/') for p in packages),'Expected /Game package paths'
    if not args.local_packages or args.resolved_manifest:
        edges=payload['dependency_edges']
        assert packages==set(edges),'Incomplete dependency graph'
        assert all(dependency in packages for dependencies in edges.values() for dependency in dependencies
                   if dependency.startswith('/Game/')),'Missing /Game dependency in resolved closure'
    files=[]
    # Resolve and validate every source/destination before any copy occurs.
    for package in payload['packages']:
        relative=Path(package.removeprefix('/Game/'))
        primary='.umap' if (args.source_content/relative).with_suffix('.umap').exists() else '.uasset'
        for suffix in [primary,'.uexp','.ubulk','.uptnl']:
            source=(args.source_content/relative).with_suffix(suffix)
            if suffix!=primary and not source.exists():continue
            src=source.resolve(strict=True)
            dst=(ROOT/'Content'/relative).with_suffix(suffix).resolve()
            assert src.is_relative_to(args.source_content.resolve()),src
            assert dst.is_relative_to((ROOT/'Content').resolve()),dst
            if dst.exists():
                with src.open('rb') as a,dst.open('rb') as b:
                    assert hashlib.file_digest(a,'sha256').digest()==hashlib.file_digest(b,'sha256').digest(),'Conflicting existing asset: '+str(dst)
                continue
            files.append((src,dst))
    for src,dst in files:
        dst.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(src,dst)
        with src.open('rb') as a,dst.open('rb') as b:assert hashlib.file_digest(a,'sha256').digest()==hashlib.file_digest(b,'sha256').digest()
    payload['copied_bytes']=sum(src.stat().st_size for src,_ in files)
    payload['source_library_modified']=False
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_text(json.dumps(payload,indent=2),encoding='utf-8')
    print(json.dumps({'packages':len(payload['packages']),'files':len(files),'bytes':payload['copied_bytes']}))
