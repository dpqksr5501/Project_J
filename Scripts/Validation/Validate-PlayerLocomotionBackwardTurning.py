"""PIE backward Strafe turns with forward/OTM guards; observes evaluated poses.

Run in a fresh PIE on a large clear floor through the official editor console.
Input is camera-relative, matching held S/W plus rapid mouse movement.
PLAYER_BACKWARD_REPEAT_TURNS=True exercises four alternating turns per case.
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
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n)for n in ['Move','MoveIntent_Forward','ToggleCombat']}
cases=[('Strafe',-1.,a)for a in [-90.,90.,-135.,135.,-170.,170.,-190.,190.]]+[
    ('Strafe',1.,-170.),('Strafe',1.,170.),('OTM',1.,-170.),('OTM',1.,170.)]
repeat=bool(globals().get('PLAYER_BACKWARD_REPEAT_TURNS',False))
if repeat:cases=[('Strafe',-1.,-170.),('Strafe',-1.,170.)]
duration=12. if repeat else 8.
steady_start=10.2 if repeat else 6.
moving_end=11.3 if repeat else 7.3
original_combat=pawn.is_combat_mode_active();original_rotation=controller.get_control_rotation()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
meshes=[]
for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
    if not mesh.get_anim_instance():continue
    bones=[b for b in ['calf_l','calf_r','Bip01-L-Calf','Bip01-R-Calf']if mesh.get_bone_index(b)>=0]
    if bones:meshes.append((mesh,bones))
output=output_file('BackwardTurningRepeated'if repeat else 'BackwardTurning');report={'cases':cases,'repeated_turns':repeat,'samples':[]}
start=unreal.GameplayStatics.get_time_seconds(world);finished=False;running=False

def angle(a,b):return abs((a-b+180.)%360.-180.)
def value(name):
    v=anim.get_editor_property(name)
    return v.export_text()if hasattr(v,'export_text')else v if isinstance(v,(float,int,bool,str))else str(v)
def checkpoint():output.write_text(json.dumps(report,indent=2),encoding='utf-8')
def validate():
    checks=[]
    for case,(mode,direction,turn)in enumerate(cases):
        samples=[s for s in report['samples']if s['case']==case]
        steady=[s for s in samples if steady_start<s['u']<moving_end-.1]
        assert len(steady)>=10,'Insufficient steady frames '+str(case)
        assert not any(s['PlayerNoValidAnim']for s in samples),'Missing animation '+str(case)
        assert all(s['PlayerFootPlacementEnable']and s['PlayerOffsetRootBoneEnabled']for s in samples),'Procedural corrections were disabled'
        assert min(s['speed']for s in steady)>400.,'Movement did not recover '+str(case)
        assert max(angle(s['root'],s['yaw'])for s in steady)<20.,'Root facing did not recover '+str(case)
        if mode=='Strafe' and direction<0. and abs(turn)>=135.:
            for begin in ([4.,5.5,7.,8.5]if repeat else [4.]):
                redirects=[s for s in samples if begin+.05<s['u']<begin+.7 and 'Reface_Start_B_'in s['selected'] and 'Pivot'in s['tags']]
                assert redirects,'Backward turn fell through to a loop without a facing transition '+str((case,begin))
        frozen={}
        for mesh,bones in meshes:
            name=mesh.get_name();last=None;since=0.;longest=0.
            for s in [s for s in samples if 3.<s['u']<moving_end-.1 and s['speed']>100.]:
                if last is None or s['t']-last['t']>.15 or max(abs(a-b)for a,b in zip(s['knees'][name],last['knees'][name]))>1.e-5:since=s['t']
                longest=max(longest,s['t']-since);last=s
            assert longest<.5,'Frozen evaluated pose '+str((case,name,longest))
            frozen[name]=longest
        clips=sorted(set(asset.rsplit('/',1)[-1]for s in samples if 3.2<s['u']<6.
                         for asset,weight in re.findall(r'Asset=(\S+) Time=[0-9.]+ Length=[0-9.]+ Weight=([0-9.]+)',s['played'])if float(weight)>.25))
        checks.append({'mode':mode,'direction':direction,'turn':turn,'turn_clips':clips,'longest_frozen_pose':frozen})
    return checks
def finish(error=None):
    global finished
    if finished:return
    finished=True;unreal.unregister_slate_post_tick_callback(handle)
    for action in actions.values():inputs.inject_input_vector_for_action(action,unreal.Vector(),[],[])
    controller.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    if error is None:
        try:report['checks']=validate()
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    checkpoint()
def tick(dt):
    global running
    if running or finished:return
    running=True
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=duration*len(cases):finish();return
        case=int(t/duration);u=t%duration;mode,direction,turn=cases[case]
        toggle=.1<u<2.6 and int(u*10.)%2==0 and pawn.is_combat_mode_active()!=(mode=='Strafe')
        if u>2.8:assert pawn.is_combat_mode_active()==(mode=='Strafe'),'Mode not established'
        moving=2.8<=u<moving_end
        progress=lambda begin:max(0.,min(1.,(u-begin)/.18))
        yaw=turn*(progress(4.)-progress(5.5)+progress(7.)-progress(8.5))if repeat else turn*progress(4.)
        controller.set_control_rotation(unreal.Rotator(pitch=0.,yaw=yaw,roll=0.))
        for name,v in {'Move':unreal.Vector(0.,direction if moving else 0.,0.),'MoveIntent_Forward':unreal.Vector(direction if moving else 0.,0.,0.),'ToggleCombat':unreal.Vector(float(toggle),0.,0.)}.items():inputs.inject_input_vector_for_action(actions[name],v,[],[])
        if report['samples']and t-report['samples'][-1]['t']<.025:return
        knees={}
        for mesh,bones in meshes:
            qs=[mesh.get_socket_transform(b,unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE).rotation for b in bones]
            knees[mesh.get_name()]=[c for q in qs for c in [q.x,q.y,q.z,q.w]]
        blend=anim.get_editor_property('PlayerBlendStackInputs');selected=blend.get_editor_property('Anim')
        report['samples'].append({'t':t,'u':u,'case':case,'yaw':pawn.get_actor_rotation().yaw,'control':yaw,
            'selected':selected.get_path_name()if selected else '',
            'selected_start':blend.get_editor_property('StartTime'),'tags':str(blend.get_editor_property('Tags')),
            'root':anim.get_editor_property('PlayerRootTransform').rotation.rotator().yaw,
            'speed':pawn.get_velocity().length2d(),'knees':knees,'pivot':anim.evaluate_is_pivoting(),
            'played':anim.inspect_foundation_playback(),**{n:value(n)for n in ['PlayerStateName','PlayerMovementDirection','PlayerMovementDirectionRecent','PlayerFutureFacingDelta','PlayerTrajectoryTurnAngle','PlayerTrajectoryIsCircling','PlayerNoValidAnim','PlayerFootPlacementEnable','PlayerOffsetRootBoneEnabled']}})
    except Exception:finish(traceback.format_exc())
    finally:running=False
handle=unreal.register_slate_post_tick_callback(tick)
