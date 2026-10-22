"""PIE: all six directed Walk/Run/Sprint changes in OTM and Strafe.

Use a fresh flat-floor session, no manual/concurrent injected input. Records the
actual selected player, including brief transitions between looping poses.
No assets or levels are saved; all fixture input/settings are restored.
"""
import json
import re
import sys
import traceback
from pathlib import Path
import unreal

sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import local_world,local_inputs,output_file

world=local_world();pawn=unreal.GameplayStatics.get_player_pawn(world,0)
controller=unreal.GameplayStatics.get_player_controller(world,0)
anim=pawn.get_source_mesh().get_anim_instance();inputs=local_inputs(world)
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n)for n in ['Move','MoveIntent_Forward','Sprint','ToggleCombat']}
original_combat=pawn.is_combat_mode_active();original_rotation=controller.get_control_rotation()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
floors=[]
for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
    floor=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.StaticMeshActor)if a.get_actor_label()=='Floor')
    scale=floor.get_actor_scale3d();floors.append((floor,scale))
    floor.set_actor_scale3d(unreal.Vector(max(scale.x,120.),max(scale.y,120.),scale.z))
pairs=[('Walk','Run'),('Run','Walk'),('Run','Sprint'),('Sprint','Run'),('Walk','Sprint'),('Sprint','Walk')]
cases=[(mode,a,b)for mode in ['OTM','Strafe']for a,b in pairs]
speeds={'Walk':165.,'Run':500.,'Sprint':750.}
report={'cases':cases,'samples':[]};output=output_file('GaitTransitions')
start=unreal.GameplayStatics.get_time_seconds(world);finished=False;busy=False;last_walk=None

def validate():
    checks=[]
    for case,(mode,source,target)in enumerate(cases):
        rows=[s for s in report['samples']if s['case']==case]
        switch=3.4+.08*(case%6)
        steady=[s for s in rows if switch-.25<s['u']<switch-.05]
        settled=[s for s in rows if 6.2<s['u']<6.6]
        assert len(steady)>10 and all(abs(s['speed']-speeds[source])<5. for s in steady),'Initial gait did not settle: '+str(case)
        assert len(settled)>10 and all(abs(s['speed']-speeds[target])<5. for s in settled),'Destination gait did not settle: '+str(case)
        assert all(not s['invalid']and s['foot']for s in rows if s['u']>1.5),'Invalid animation or disabled foot correction'
        transitions=[]
        for i,s in enumerate(rows):
            if not switch<=s['u']<6.7:continue
            previous=rows[i-1]if i else None
            entered=not previous or previous['asset']!=s['asset']or s['asset_time']<previous['asset_time']-.001
            if not entered or target.upper()not in s['gait']:continue
            match=re.search(r'_Transition_(Walk|Run|Sprint)_to_(Walk|Run|Sprint)_',s['asset'])
            if match:transitions.append((match.group(1),match.group(2),s))
        wrong=[s for a,b,s in transitions if (a,b)!=(source,target)]
        assert not wrong,'Wrong gait transition family: '+str((mode,source,target,wrong[0]['asset']))
        checks.append({'mode':mode,'from':source,'to':target,'transition_assets':sorted({s['asset']for a,b,s in transitions}),
                       'final_speed':settled[-1]['speed']})
    return checks

def finish(error=None):
    global finished
    if finished:return
    finished=True;unreal.unregister_slate_post_tick_callback(handle)
    if error is None:
        try:report['checks']=validate()
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    for action in actions.values():inputs.inject_input_vector_for_action(action,unreal.Vector(),[],[])
    pawn.set_walk(False);controller.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    for floor,scale in floors:floor.set_actor_scale3d(scale)
    perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    unreal.log('Gait transition regression: '+str(output)+' success='+str(report['completed']))

def tick(dt):
    global busy,last_walk
    if busy or finished:return
    busy=True
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=len(cases)*8.:finish();return
        case=int(t/8.);u=t%8.;switch=3.4+.08*(case%6);mode,source,target=cases[case]
        gait=source if u<switch else target;moving=.5<=u<6.7
        walk=gait=='Walk'
        if walk!=last_walk:pawn.set_walk(walk);last_walk=walk
        controller.set_control_rotation(unreal.Rotator(pitch=0,yaw=180.*(case%2),roll=0))
        values={'Move':unreal.Vector(0,float(moving),0),'MoveIntent_Forward':unreal.Vector(float(moving),0,0),
                'Sprint':unreal.Vector(float(moving and gait=='Sprint'),0,0),
                'ToggleCombat':unreal.Vector(float(.1<=u<2.6 and int(u*10.)%2==0 and pawn.is_combat_mode_active()!=(mode=='Strafe')),0,0)}
        for n,a in actions.items():inputs.inject_input_vector_for_action(a,values[n],[],[])
        if u>2.8:assert pawn.is_combat_mode_active()==(mode=='Strafe'),'Rotation mode was not established'
        played=anim.inspect_foundation_playback();match=re.search(r'Index=\d+ Asset=(\S+) Time=([0-9.]+)',played)
        report['samples'].append({'case':case,'u':u,'t':t,'speed':pawn.get_velocity().length2d(),
            'gait':str(anim.get_editor_property('PlayerGait')),'recent_gait':str(anim.get_editor_property('PlayerGaitRecent')),
            'state':str(anim.get_editor_property('PlayerStateName')),'asset':match.group(1)if match else '',
            'asset_time':float(match.group(2))if match else -1.,'invalid':anim.get_editor_property('PlayerNoValidAnim'),
            'foot':anim.get_editor_property('PlayerFootPlacementEnable')})
    except Exception:finish(traceback.format_exc())
    finally:busy=False

handle=unreal.register_slate_post_tick_callback(tick)
