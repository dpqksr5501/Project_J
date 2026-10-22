"""PIE: diagnostic requests retain real traversal age, including a paused mesh.

Uses the existing player-controller debug command; changes no assets. The brief
pause belongs only to this fixture and is restored before movement regression.
"""
import json
import re
import sys
import traceback
from pathlib import Path
import unreal

sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import local_world,output_file

world=local_world();pawn=unreal.GameplayStatics.get_player_pawn(world,0)
controller=unreal.GameplayStatics.get_player_controller(world,0);mesh=pawn.get_source_mesh()
original_pause=mesh.get_editor_property('bPauseAnims');assert not original_pause,'Needs an initially updating source mesh'
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground');perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
command_line=unreal.SystemLibrary.get_command_line()
match=re.search(r'-abslog=(?:"([^"]+)"|(\S+))',command_line,re.I)
log=Path(next(x for x in match.groups()if x))if match else Path(unreal.Paths.project_log_dir())/'Project_J.log'
log_start=log.stat().st_size
output=output_file('PlaybackObservation');report={'requests':[]}
start=unreal.GameplayStatics.get_time_seconds(world);last=-1.;finished=False

def validate():
    data=log.read_bytes()[log_start:].decode('utf-8',errors='replace')
    blocks=re.split(r'PlaybackObservationRequest id=(\d+) frame=(\d+) phase=(\w+) repeat=(\d+)',data)
    observed={}
    for i in range(1,len(blocks),5):
        identifier,frame,phase,repeat,body=blocks[i:i+5]
        match=re.search(r'PlayerLocomotion Playback=(\S+) Time=([\d.]+)/[\d.]+.*?Frame=(\d+)',body)
        assert match,'Existing debug command did not return a playback observation'
        asset,time,capture=match.groups();capture=int(capture);frame=int(frame)
        assert asset!='None'and 0<capture<=frame,'Invalid pose or manufactured future observation'
        observed.setdefault(int(identifier),[]).append({'query_frame':frame,'capture_frame':capture,'asset':asset,'time':float(time),'phase':phase})
    assert len(observed)==len(report['requests']),'Incomplete diagnostic output'
    for pair in observed.values():assert len(pair)==2 and pair[0]==pair[1],'Same-traversal requests did not reuse their observation'
    rows=[pair[0]for pair in observed.values()];paused=[r for r in rows if r['phase']=='paused']
    assert len(paused)>=3 and len({r['capture_frame']for r in paused})==1,'Paused graph was reported as advancing'
    assert max(r['query_frame']-r['capture_frame']for r in paused)>10,'Paused-frame age was not exercised'
    resumed=[r for r in rows if r['phase']=='resumed'];assert len(resumed)>=3
    assert min(r['capture_frame']for r in resumed)>paused[-1]['capture_frame'],'Observation did not recover after animation resumed'
    assert all(r['query_frame']-r['capture_frame']<=2 for r in rows if r['phase']!='paused'),'Updating graph observations were stale'
    return rows

def finish(error=None):
    global finished
    if finished:return
    finished=True;unreal.unregister_slate_post_tick_callback(handle)
    mesh.set_editor_property('bPauseAnims',original_pause);perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    if error is None:
        try:report['checks']=validate()
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')

def tick(dt):
    global last
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=4.8:finish();return
        paused=1.<=t<2.5;mesh.set_editor_property('bPauseAnims',paused)
        if t<.4 or t-last<.2 or .9<t<1.2 or 2.3<t<2.8 or t>4.4:return
        last=t;phase='paused'if paused else'resumed'if t>=2.8 else'warm'
        identifier=len(report['requests']);frame=unreal.SystemLibrary.get_frame_count()
        report['requests'].append({'id':identifier,'frame':frame,'phase':phase})
        for repeat in range(2):
            unreal.log(f'PlaybackObservationRequest id={identifier} frame={frame} phase={phase} repeat={repeat}')
            unreal.SystemLibrary.execute_console_command(controller,'DumpAnimBudget',controller)
    except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
