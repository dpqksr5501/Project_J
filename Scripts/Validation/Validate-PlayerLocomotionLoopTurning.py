"""PIE: sustained forward input and gradual mouse-facing turns, including after Sprint -> Run.

Run in a fresh PIE on a large clear floor. This checks the displayed source root,
not only the capsule yaw; authored pivot turns can otherwise hide a stuck loop.
"""
import re
import sys
import traceback
from pathlib import Path
import json
import unreal

sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import output_file,local_inputs

world=next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(False)
           if unreal.GameplayStatics.get_player_pawn(w,0) and unreal.GameplayStatics.get_player_pawn(w,0).is_locally_controlled())
pawn=unreal.GameplayStatics.get_player_pawn(world,0)
controller=unreal.GameplayStatics.get_player_controller(world,0)
anim=pawn.get_source_mesh().get_anim_instance()
inputs=local_inputs(world)
# The actor and BlendStack clocks can advance while a retargeted body holds a
# cached pose. Observe knee rotations relative to their parents on both sides.
# This fixture supports the production Manny and Bip01 bodies; it does not
# infer a retarget mapping for arbitrary skeletons.
pose_meshes=[]
for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
    if not mesh.get_anim_instance():continue
    bones=[b for b in ['calf_l','calf_r','Bip01-L-Calf','Bip01-R-Calf'] if mesh.get_bone_index(b)>=0]
    assert bones,'Add explicit knee observations for this fixture body: '+mesh.get_name()
    pose_meshes.append((mesh,bones))
assert pose_meshes,'No evaluated skeletal meshes'

def knee_pose(mesh,bones):
    result=[]
    for bone in bones:
        q=mesh.get_socket_transform(bone,unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE).rotation
        result.extend([q.x,q.y,q.z,q.w])
    return result

def longest_frozen_pose(frames,name):
    last=None;since=None;maximum=0.
    for frame in frames:
        pose=frame['knee_pose'][name]
        if last is None or frame['t']-last['t']>.15 or max(abs(a-b)for a,b in zip(pose,last['knee_pose'][name]))>1.e-5:
            since=frame['t']
        maximum=max(maximum,frame['t']-since)
        last=frame
    return maximum
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n) for n in ['Move','MoveIntent_Forward','Sprint','ToggleCombat']}
cases=[('OTM','Walk'),('OTM','Run'),('OTM','Sprint'),('OTM','SprintToRun'),('Strafe','Run')]
original_combat=pawn.is_combat_mode_active()
original_rotation=controller.get_control_rotation()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
output=output_file('LoopTurning')
report={'cases':cases,'samples':[]}
start=unreal.GameplayStatics.get_time_seconds(world)
last_case=-1
running=False
finished=False

def checkpoint():
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')

checkpoint()

def angle(a,b):return abs((a-b+180.)%360.-180.)

def validate():
    results=[]
    for case,(mode,gait) in enumerate(cases):
        # Loop selection can lead the actual BlendStack player. Require the
        # evaluated gait too, so a transition clip cannot conceal a stuck loop.
        family='Sprint' if gait=='Sprint' else 'Walk' if gait=='Walk' else 'Run'
        expected='/'+family+'/' if family=='Sprint' else '/'+family+'/BS_'
        frames=[s for s in report['samples'] if s['case']==case and 5.5<s['u']<9.2 and s['loop'] and s['speed']>100. and s['moving']
                and any(expected in asset and float(weight)>.25 for asset,weight in re.findall(
                    r'Asset=(\S+) Time=[0-9.]+ Length=[0-9.]+ Weight=([0-9.]+)',s['played']))]
        assert len(frames)>=20,'Insufficient moving loop frames: '+str((mode,gait))
        assert max(s['control'] for s in frames)-min(s['control'] for s in frames)>60.,'Gradual turn must execute while remaining in the loop'
        maximum=max(angle(s['root'],s['yaw']) for s in frames)
        assert maximum<15.,'Displayed loop root froze or diverged from physical facing: '+str((mode,gait,maximum))
        frozen={mesh.get_name():longest_frozen_pose(frames,mesh.get_name()) for mesh,bones in pose_meshes}
        assert all(span<.5 for span in frozen.values()),'Moving loop held a frozen knee pose: '+str((mode,gait,frozen))
        assert not any(s['invalid'] for s in report['samples'] if s['case']==case),'Missing animation'
        results.append({'mode':mode,'gait':gait,'loop_frames':len(frames),'max_root_actor_error_degrees':maximum,
                        'longest_unchanged_knee_pose_seconds':frozen})
    return results

def finish(error=None):
    global finished
    if finished:return
    finished=True
    unreal.unregister_slate_post_tick_callback(handle)
    for action in actions.values():inputs.inject_input_vector_for_action(action,unreal.Vector(),[],[])
    controller.set_control_rotation(original_rotation)
    pawn.set_walk(False)
    # Local predicted abilities must execute in this world's input tick. Calling
    # them directly from editor Python can recurse through PIE RPC dispatch.
    if pawn.is_combat_mode_active()!=original_combat:
        inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    if error is None:
        try:report['checks']=validate()
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    checkpoint()
    unreal.log('Player locomotion loop-turn regression: '+str(output)+' success='+str(report['completed']))

def tick(dt):
    global last_case,running
    if running or finished:return
    running=True
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=10.*len(cases):finish();return
        case=int(t/10.);u=t%10.;mode,gait=cases[case]
        if case!=last_case:
            last_case=case
            checkpoint()
            pawn.set_walk(gait=='Walk')
        # One input pulse per case, never a synchronous GAS call from Slate.
        toggle=.2<=u<.35 and pawn.is_combat_mode_active()!=(mode=='Strafe')
        if u>=1.5:
            assert pawn.is_combat_mode_active()==(mode=='Strafe'),'Combat input failed to establish rotation mode'
        moving=1.6<=u<9.6
        sprint=moving and (gait=='Sprint' or gait=='SprintToRun' and u<3.3)
        # The turns are gradual enough to exercise continuous loop steering.
        base=case*120.
        yaw=base+120.*max(0.,min(1.,(u-4.)/5.))
        controller.set_control_rotation(unreal.Rotator(pitch=0.,yaw=yaw,roll=0.))
        for n,v in {'Move':unreal.Vector(0.,float(moving),0.),'MoveIntent_Forward':unreal.Vector(float(moving),0.,0.),'Sprint':unreal.Vector(float(sprint),0.,0.),'ToggleCombat':unreal.Vector(float(toggle),0.,0.)}.items():
            inputs.inject_input_vector_for_action(actions[n],v,[],[])
        if report['samples'] and t-report['samples'][-1]['t']<.03:return
        root=anim.get_editor_property('PlayerRootTransform').rotation.rotator().yaw
        blend=anim.get_editor_property('PlayerBlendStackInputs')
        report['samples'].append({'t':t,'u':u,'case':case,'control':yaw,'yaw':pawn.get_actor_rotation().yaw,'root':root,
            'knee_pose':{mesh.get_name():knee_pose(mesh,bones) for mesh,bones in pose_meshes},
            'speed':pawn.get_velocity().length2d(),'loop':blend.get_editor_property('Loop'),
            'moving':anim.evaluate_is_moving(),'invalid':anim.get_editor_property('PlayerNoValidAnim'),
            'state':str(anim.get_editor_property('PlayerStateName')),'played':anim.inspect_foundation_playback()})
    except Exception:finish(traceback.format_exc())
    finally:running=False

handle=unreal.register_slate_post_tick_callback(tick)
