"""Explicit first capital composition through the native editor Python API.

Owns only tagged additions. Preserves terrain, existing gate and every provider.
Checkpointed creation prevents an unknown MCP result from duplicating buildings.
"""
import hashlib,importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source'
OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';BASE='/Game/World/Starter'
PLAN=json.loads((SOURCE/'StarterWorld.capital.json').read_text(encoding='utf-8'))
TAG='ProjectJ.Starter.Capital.v1';FILE=OUT/'capital-build.json'
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith(BASE+'/Maps/L_StarterRegion.')
assert hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest()==PLAN['height_sha256']
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
STATE=json.loads(FILE.read_text(encoding='utf-8')) if FILE.exists() else dict(phase='READY',completed=[],actors=[])
assert STATE['phase']=='READY','Inspect existing result before explicit recovery: '+STATE['phase']
assert not any(TAG in [str(t) for t in a.tags] for a in ACTORS.get_all_level_actors()) or STATE['completed']
spec=importlib.util.spec_from_file_location('settlement',ROOT/'Scripts/World/Starter-SettlementAuthoring.py')
shared=importlib.util.module_from_spec(spec);spec.loader.exec_module(shared)
AUTHOR=shared.SettlementAuthoring(TAG,'StarterRegion/Settlements/S01/Capital')
STONE=unreal.load_asset(BASE+'/Materials/M_StarterBridgeStone');assert STONE
def record(phase):
    STATE.update(phase=phase,updated_at=time.time());FILE.write_text(json.dumps(STATE,indent=2),encoding='utf-8')
def save():
    AUTHOR.save()
def spawn(label,path,x,y,z,yaw=0,scale=(1,1,1),stone=False):
    actor=AUTHOR.spawn(label,path,x,y,z,yaw,scale,STONE if stone else None)
    STATE['actors'].append(AUTHOR.created[-1])
    return actor
def plot(p):
    x,y,z=p['center_m'];w,d=p['width_m'],p['depth_m'];bottom=p['foundation_bottom_m'];depth=z-bottom
    spawn(p['id']+'_Foundation','/Engine/BasicShapes/Cube',x,y,bottom,p['yaw'],(w+.24,d+.24,depth),True)
    spawn(p['id'],p['mesh'],x,y,z,p['yaw'])
later_ids={i for p in PLAN.get('urban_infill',[]) for i in p['plot_ids']}
original_plots=[p for p in PLAN['plots'] if p['id'] not in later_ids]
pending=[p for p in original_plots if p['id'] not in STATE['completed']]
index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(pending):
            save();STATE.update(height_modified=False,plots=len(original_plots),art_status='FIRST_COMPOSITION_REQUIRES_REVIEW');record('COMPLETE')
            unreal.unregister_slate_post_tick_callback(handle);return
        p=pending[index];record('CREATING_'+p['id']);plot(p);STATE['completed'].append(p['id']);index+=1
        if index%4==0 or index==len(pending):save()
        record('READY')
    except Exception:
        STATE['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle)
        unreal.log_error(STATE['error'])
    finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
