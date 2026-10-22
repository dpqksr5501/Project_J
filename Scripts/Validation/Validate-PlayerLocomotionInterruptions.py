"""PIE: start cancellation, rapid gait changes, stop/restart and jump interruption.

Runs in both rotation modes on a clear floor. No concurrent input/console calls.
Checks actual source/body knee motion; no assets or levels are saved.
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
source=pawn.get_source_mesh();anim=source.get_anim_instance();inputs=local_inputs(world)
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n)for n in ['Move','MoveIntent_Forward','Sprint','Jump','ToggleCombat']}
original_combat=pawn.is_combat_mode_active();original_rotation=controller.get_control_rotation()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
meshes=[]
for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
    if not mesh.get_anim_instance():continue
    bones=[b for b in ['calf_l','calf_r','Bip01-L-Calf','Bip01-R-Calf']if mesh.get_bone_index(b)>=0]
    assert bones,'Add an explicit knee observation for this fixture body'
    meshes.append((mesh,bones))
floors=[]
for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
    floor=next(a for a in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.StaticMeshActor)if a.get_actor_label()=='Floor')
    scale=floor.get_actor_scale3d();floors.append((floor,scale));floor.set_actor_scale3d(unreal.Vector(max(scale.x,120.),max(scale.y,120.),scale.z))
names=['StartCancel','RapidSprintRunSprint','StopRestart180','JumpDuringDownshift']
cases=[(mode,name)for mode in ['OTM','Strafe']for name in names]
report={'cases':cases,'samples':[]};output=output_file('TransitionInterruptions')
start=unreal.GameplayStatics.get_time_seconds(world);busy=False;finished=False
pawn.set_walk(False)

def pose(mesh,bones):
    result=[]
    for bone in bones:
        q=mesh.get_socket_transform(bone,unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE).rotation
        result.extend([q.x,q.y,q.z,q.w])
    return result

def validate():
    checks=[]
    for case,(mode,name)in enumerate(cases):
        rows=[s for s in report['samples']if s['case']==case and s['u']>2.8]
        assert rows and all(not s['invalid']and s['foot']for s in rows),'Invalid animation or globally disabled corrections'
        settled=[s for s in rows if 8.<s['u']<8.8];target=750. if name=='RapidSprintRunSprint'else 500.
        assert len(settled)>20 and all(abs(s['speed']-target)<5. for s in settled),'Movement did not recover after interruption'
        frozen={}
        for mesh,bones in meshes:
            last=None;since=None;maximum=0.
            for s in rows:
                if not s['moving']or s['speed']<50.:last=None;continue
                if last is None or max(abs(a-b)for a,b in zip(s['pose'][mesh.get_name()],last['pose'][mesh.get_name()]))>1.e-5:since=s['t']
                maximum=max(maximum,s['t']-since);last=s
            assert maximum<.5,'Moving pose froze after interruption: '+str((case,mesh.get_name(),maximum))
            frozen[mesh.get_name()]=maximum
        if name=='JumpDuringDownshift':
            air=[s for s in rows if 'Falling'in s['physical_mode']]
            assert air,'Jump interruption was not physically exercised'
            landing=next(s for s in rows if s['t']>air[0]['t']and 'Walking'in s['physical_mode'])
            assert all('Walking'in s['physical_mode']for s in rows if s['t']>=landing['t']),'Re-entered Falling after contact'
        checks.append({'mode':mode,'case':name,'settled_speed':settled[-1]['speed'],'longest_unchanged_knee_pose_seconds':frozen})
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
    for a in actions.values():inputs.inject_input_vector_for_action(a,unreal.Vector(),[],[])
    controller.set_control_rotation(original_rotation);pawn.set_walk(False)
    if pawn.is_combat_mode_active()!=original_combat:inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    for floor,scale in floors:floor.set_actor_scale3d(scale)
    perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    unreal.log('Transition interruption regression: '+str(output)+' success='+str(report['completed']))

def tick(dt):
    global busy
    if busy or finished:return
    busy=True
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=10.*len(cases):finish();return
        case=int(t/10.);u=t%10.;mode,name=cases[case]
        moving=2.8<=u<9.
        if name=='StartCancel':moving=2.8<=u<3. or 4.<=u<9.
        if name=='StopRestart180':moving=2.8<=u<3.9 or 4.05<=u<9.
        sprint=moving and (name=='RapidSprintRunSprint'and (u<3.9 or u>=4.02)or name=='JumpDuringDownshift'and u<3.9)
        jump=name=='JumpDuringDownshift'and 4.<=u<4.12
        yaw=180.*(case%2)+(180. if name=='StopRestart180'and u>=4.05 else 0.)
        controller.set_control_rotation(unreal.Rotator(pitch=0,yaw=yaw,roll=0))
        values={'Move':unreal.Vector(0,float(moving),0),'MoveIntent_Forward':unreal.Vector(float(moving),0,0),
                'Sprint':unreal.Vector(float(sprint),0,0),'Jump':unreal.Vector(float(jump),0,0),
                'ToggleCombat':unreal.Vector(float(.1<=u<2.6 and int(u*10.)%2==0 and pawn.is_combat_mode_active()!=(mode=='Strafe')),0,0)}
        for n,a in actions.items():inputs.inject_input_vector_for_action(a,values[n],[],[])
        if u>2.8:assert pawn.is_combat_mode_active()==(mode=='Strafe'),'Rotation mode was not established'
        if report['samples']and t-report['samples'][-1]['t']<.016:return
        report['samples'].append({'case':case,'u':u,'t':t,'moving':moving,'speed':pawn.get_velocity().length2d(),
            'state':str(anim.get_editor_property('PlayerStateName')),'physical_mode':str(pawn.get_mover().get_movement_mode_name()),
            'played':anim.inspect_foundation_playback(),'invalid':anim.get_editor_property('PlayerNoValidAnim'),
            'foot':anim.get_editor_property('PlayerFootPlacementEnable'),'pose':{m.get_name():pose(m,b)for m,b in meshes}})
    except Exception:finish(traceback.format_exc())
    finally:busy=False

handle=unreal.register_slate_post_tick_callback(tick)
