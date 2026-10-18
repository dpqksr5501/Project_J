"""Sequential official-MCP forest authoring in bounded spatial tiles.

Checkpoint every operation. Never retries an unknown spawn/generation outcome.
The native save helper validates height/core exclusions before scoped saving.
"""
import importlib.util,json,re,time
from pathlib import Path
from PIL import Image
import numpy as np
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
SPEC=importlib.util.spec_from_file_location('mcp',ROOT/'Scripts/Editor/World-MCP.py')
M=importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(M)
GRAPH={'refPath':'/Game/World/Starter/PCG/PCG_StarterForest.PCG_StarterForest'}
VOLUME_BOUNDS=json.loads((ROOT/'Content/World/Starter/Source/StarterWorld.forest-streaming.json').read_text())['tiles']
CHECKPOINT=OUT/'forest-rollout.json';SAVE=OUT/'forest-save.json'


def call(toolset,name,args):
    r=M.invoke(8000,'tools/call',{'name':'call_tool','arguments':{'toolset_name':toolset,'tool_name':name,'arguments':args}})
    assert not r.get('error') and not r.get('result',{}).get('isError'),str(r)[:600]
    return json.loads(r['result']['content'][0]['text'])['returnValue']


def main():
    assert not call('EditorToolset.EditorAppToolset','IsPIERunning',{})
    assert call('editor_toolset.toolsets.scene.SceneTools','get_current_level',{})=='/Game/World/Starter/Maps/L_StarterRegion'
    if CHECKPOINT.exists():
        state=json.loads(CHECKPOINT.read_text(encoding='utf-8'))
        assert state['phase'] not in ['ERROR','CREATE_UNKNOWN','EXECUTE_UNKNOWN'],'Inspect editor and checkpoint before resuming unknown mutations'
    else:
        saved=json.loads(SAVE.read_text(encoding='utf-8'));existing={r['label']:r for r in saved['tiles']}
        density=np.asarray(Image.open(ROOT/'Content/World/Starter/Source/T_StarterForestPlacement.png'))[:,:,0]/255.
        tiles=[]
        for j in range(16):
            for i in range(16):
                x0,y0=i*512,j*512;x1,y1=min(x0+512,8064),min(y0+512,8064)
                tile=density[round(y0/8064*2048):round(y1/8064*2048),round(x0/8064*2048):round(x1/8064*2048)]
                if float(tile.mean())<.025:continue
                label=f'PCG_Forest_{i}_{j}'
                item=dict(label=label,bounds_m=[x0,y0,x1,y1],mean_density=float(tile.mean()),status='planned')
                if label in existing:item.update(status='saved',path=existing[label]['path'])
                tiles.append(item)
        state=dict(phase='READY',tiles=tiles,height_modified=False,runtime_generation=False)
    def record(phase):
        state['phase']=phase;state['updated_at']=time.time();CHECKPOINT.write_text(json.dumps(state,indent=2),encoding='utf-8')
    def save_batch():
        before=SAVE.stat().st_mtime
        call('SlateInspectorToolset.SlateInspectorToolset','Windows',{'action':'select','index':0})
        call('SlateInspectorToolset.SlateInspectorToolset','Click',{'ref':'tb2'})
        assert call('SlateInspectorToolset.SlateInspectorToolset','Type',{'ref':'tb2','text':"py import runpy; runpy.run_path('C:/Users/I/Documents/GitHub/Project_J/Scripts/World/Save-StarterForests.py')",'submit':True})
        assert SAVE.stat().st_mtime>before,'Native save failed or still pending; inspect before continuing'
        saved={r['label']:r for r in json.loads(SAVE.read_text(encoding='utf-8'))['tiles']}
        for tile in state['tiles']:
            if tile['status']=='executed':
                assert tile['label'] in saved;tile['status']='saved'
        record('READY');print(json.dumps(dict(saved=sum(t['status']=='saved' for t in state['tiles']),total=len(state['tiles']))),flush=True)
    record('READY');batch=0
    try:
        for tile in state['tiles']:
            if tile['status']=='saved':continue
            if 'path' not in tile:
                x0,y0,x1,y1=tile['bounds_m']
                bounded=VOLUME_BOUNDS[tile['label']]
                transform=dict(location=dict(x=((x0+x1)/2-4032)*100,y=((y0+y1)/2-4032)*100,z=bounded['center_z_cm']),rotation=dict(pitch=0,yaw=0,roll=0),scale=dict(x=(x1-x0)/2,y=(y1-y0)/2,z=bounded['half_height_m']))
                record('CREATE_UNKNOWN')
                value=call('PCGToolset.PCGToolset','SpawnGraphInstance',{'graph':GRAPH,'name':tile['label'],'transform':transform,'jsonParams':'{}'})
                tile.update(path=value['actor']['refPath'],status='created');record('READY')
            record('EXECUTE_UNKNOWN')
            messages=call('PCGToolset.PCGToolset','ExecuteGraphInstance',{'pCGVolume':{'refPath':tile['path']}})
            assert not any(m.get('severity','').lower() in ['error','fatal'] for m in messages),messages
            tile.update(status='executed',messages=messages);record('READY');batch+=1
            if batch>=12:save_batch();batch=0
        if batch:save_batch()
        record('COMPLETE')
    except Exception as error:
        state['error']=str(error)
        # Preserve the exact unknown-operation phase so a timeout cannot cause a duplicate spawn.
        record(state['phase'] if state['phase'].endswith('UNKNOWN') else 'ERROR')
        raise
    print(json.dumps(dict(complete=True,tiles=len(state['tiles']))),flush=True)


if __name__=='__main__':main()
