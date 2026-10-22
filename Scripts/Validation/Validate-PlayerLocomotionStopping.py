"""PIE: recent-gait Stop selection, Idle return and contact-driven foot unlocking.

Start a fresh session at (-4500,-4500,260), with no manual/other injected input.
Optional PLAYER_STOP_SLOPE_PITCH=12 tilts only the PIE Floor in every network
world. No level or asset is saved. This checks solver state, not shoe/sole fit.
"""
import json
import re
import sys
import traceback
from pathlib import Path
import unreal

sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import local_world,local_inputs,output_file

world=local_world()
pawn=unreal.GameplayStatics.get_player_pawn(world,0)
controller=unreal.GameplayStatics.get_player_controller(world,0)
mesh=pawn.get_source_mesh()
anim=mesh.get_anim_instance()
inputs=local_inputs(world)
pitch=float(globals().get('PLAYER_STOP_SLOPE_PITCH',0.))
assert abs(pitch)<=15.,'This fixture covers walkable gentle slopes'
output=output_file('StoppingSlope' if pitch else 'StoppingFlat')
original_combat=pawn.is_combat_mode_active()
original_rotation=controller.get_control_rotation()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
floors=[]
for fixture_world in unreal.EditorLevelLibrary.get_pie_worlds(True):
    candidates=[a for a in unreal.GameplayStatics.get_all_actors_of_class(fixture_world,unreal.StaticMeshActor) if a.get_actor_label()=='Floor']
    assert len(candidates)==1,'Expected one native Floor in each PIE world'
    floor=candidates[0]
    floors.append((floor,floor.get_actor_transform()))
    scale=floor.get_actor_scale3d()
    floor.set_actor_scale3d(unreal.Vector(max(scale.x,40.),max(scale.y,40.),scale.z))
    if pitch:
        pivot=unreal.Vector(-4500.,-4500.,0.)
        rotation=unreal.Rotator(pitch=pitch,yaw=0.,roll=0.)
        floor.set_actor_location(pivot+rotation.quaternion().rotate_vector(floor.get_actor_location()-pivot),False,False)
        floor.set_actor_rotation(rotation,False)
rigs=[rig for rig in unreal.ObjectIterator(unreal.ControlRig)
      if rig.get_class().get_name()=='CR_ProjectJ_Manny_FootPlacement_C' and rig.get_outer()==mesh]
assert len(rigs)==1,'Observe the player source solver, not an idle NPC or preview'
rig=rigs[0]
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n)for n in ['Move','MoveIntent_Forward','Sprint','ToggleCombat']}
report={'slope_pitch':pitch,'rig':rig.get_path_name(),'samples':[]}
start=unreal.GameplayStatics.get_time_seconds(world)
last_case=-1
busy=False
finished=False

def validate():
    checks=[]
    for case in range(4):
        rows=[s for s in report['samples'] if s['case']==case]
        expected='Sprint' if case%2 else 'Run'
        assert all(not s['invalid'] and s['foot_enabled'] for s in rows if s['u']>1.5),'Invalid selection or disabled foot correction'
        steady=[s for s in rows if 3.5<s['u']<5.4]
        assert len(steady)>20 and all(abs(s['speed3d']-(750. if case%2 else 500.))<5. for s in steady),'Requested gait did not settle'
        stops=[s for s in rows if s['u']>5.5 and '_Stop_' in s['asset']]
        assert len(stops)>20 and all('/'+expected+'/' in s['asset'] for s in stops),'Wrong stop family'
        assert any(s['u']>8. and s['state']=='Idle Loop' and s['speed']<2. for s in rows),'Did not return to stationary Idle'
        if pitch:
            assert all(abs(s['normal'][0])>.15 and s['normal'][2]>.9 for s in stops),'Slope was not evaluated'
        conflicts={}
        releases={}
        for side,name in [('l','Left'),('r','Right')]:
            since=None
            maximum=0.
            unlocked=0
            for sample in stops:
                low=sample['contact_'+side]<.1
                pinned=sample['pins'][name+'FootPinned']
                if low and pinned:
                    if since is None:since=sample['t']
                    maximum=max(maximum,sample['t']-since)
                else:since=None
                if low and not pinned:unlocked+=1
            assert unlocked>0 and maximum<.1,'Low-contact foot stayed locked: '+str((case,side,maximum))
            conflicts[side]=maximum
            releases[side]=unlocked
        checks.append({'mode':'Strafe' if case>=2 else 'OTM','gait':expected,
                       'stop_assets':sorted({s['asset'] for s in stops}),
                       'low_contact_lock_seconds':conflicts,'unlocked_samples':releases})
    return checks

def finish(error=None):
    global finished
    if finished:return
    finished=True
    unreal.unregister_slate_post_tick_callback(handle)
    if error is None:
        try:report['checks']=validate()
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    for action in actions.values():inputs.inject_input_vector_for_action(action,unreal.Vector(),[],[])
    for floor,transform in floors:floor.set_actor_transform(transform,False,False)
    pawn.set_walk(False)
    controller.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:
        inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1.,0.,0.),[],[])
    perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    unreal.log('Stopping regression: '+str(output)+' success='+str(report['completed']))

def tick(dt):
    global last_case,busy
    if busy or finished:return
    busy=True
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=40.:finish();return
        case=int(t/10.)
        u=t%10.
        strafe=case>=2
        if last_case!=case:
            last_case=case
            pawn.set_walk(False)
        moving=1.7<=u<5.5
        controller.set_control_rotation(unreal.Rotator(pitch=0.,yaw=0. if case%2==0 else 180.,roll=0.))
        values={'Move':unreal.Vector(0.,float(moving),0.),'MoveIntent_Forward':unreal.Vector(float(moving),0.,0.),
                'Sprint':unreal.Vector(float(moving and case%2==1),0.,0.),
                'ToggleCombat':unreal.Vector(float(.2<=u<1.4 and int(u*10.)%2==0 and pawn.is_combat_mode_active()!=strafe),0.,0.)}
        for name,action in actions.items():inputs.inject_input_vector_for_action(action,values[name],[],[])
        if u>=1.5:assert pawn.is_combat_mode_active()==strafe,'Rotation mode was not established'
        if report['samples'] and t-report['samples'][-1]['t']<.03:return
        playback=anim.inspect_foundation_playback()
        match=re.search(r'Index=\d+ Asset=(\S+) Time=([0-9.]+)',playback)
        normal=anim.get_editor_property('PlayerCharacterProperties').get_editor_property('GroundNormal')
        report['samples'].append({'t':t,'u':u,'case':case,'speed':pawn.get_velocity().length2d(),'speed3d':pawn.get_velocity().length(),
            'asset':match.group(1) if match else '', 'asset_time':float(match.group(2)) if match else 0.,
            'state':str(anim.get_editor_property('PlayerStateName')),'normal':[normal.x,normal.y,normal.z],
            'invalid':anim.get_editor_property('PlayerNoValidAnim'),'foot_enabled':anim.get_editor_property('PlayerFootPlacementEnable'),
            'contact_l':anim.get_curve_value('contact_l'),'contact_r':anim.get_curve_value('contact_r'),
            'pins':{name:rig.get_editor_property(name) for name in ['LeftFootPinned','RightFootPinned','LeftPinWeight','RightPinWeight']}})
    except Exception:finish(traceback.format_exc())
    finally:busy=False

handle=unreal.register_slate_post_tick_callback(tick)
