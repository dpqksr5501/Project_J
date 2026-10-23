"""Flat-floor jump/landing regression; no assets or levels are saved.

PLAYER_AIR_SPRINT_BEFORE_JUMP releases Shift in air. Setting
PLAYER_AIR_SPRINT_THROUGH_LANDING instead holds it through impact and adds a
straight-ahead case per rotation mode. Do not inject other input while running.
"""
import sys
import unreal,json,traceback,re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import output_file,check_air,local_inputs
sprint_before_jump=bool(globals().get('PLAYER_AIR_SPRINT_BEFORE_JUMP',False))
sprint_through_landing=bool(globals().get('PLAYER_AIR_SPRINT_THROUGH_LANDING',False))
sprint_before_jump |= sprint_through_landing
cases_per_mode=5 if sprint_through_landing else 4
output=output_file('AirSprintHeldLanding' if sprint_through_landing else 'AirSprintLanding' if sprint_before_jump else 'AirLanding')
world=next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(False) if unreal.GameplayStatics.get_player_controller(w,0) and unreal.GameplayStatics.get_player_controller(w,0).is_local_controller())
pawn=unreal.GameplayStatics.get_player_pawn(world,0)
controller=unreal.GameplayStatics.get_player_controller(world,0)
original_combat=pawn.is_combat_mode_active()
original_rotation=controller.get_control_rotation()
pawn.set_walk(False)
original_floor_scales=[]
if sprint_before_jump:
    # The faster carried momentum requires the same large-floor fixture as
    # sprint-air direction coverage. Change only the native Floor in PIE worlds.
    for fixture_world in unreal.EditorLevelLibrary.get_pie_worlds(True):
        floors=[a for a in unreal.GameplayStatics.get_all_actors_of_class(fixture_world,unreal.StaticMeshActor) if a.get_actor_label()=='Floor']
        assert len(floors)==1,'Sprint landing fixture requires one native Floor actor'
        floor=floors[0];scale=floor.get_actor_scale3d();original_floor_scales.append((floor,scale))
        floor.set_actor_scale3d(unreal.Vector(max(scale.x,40.),max(scale.y,40.),scale.z))
mesh=pawn.get_source_mesh();anim=mesh.get_anim_instance();mover=pawn.get_mover()
inputs=local_inputs(world)
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n) for n in ['Move','Jump','Sprint','ToggleCombat','MoveIntent_Forward','MoveIntent_Backward','MoveIntent_Left','MoveIntent_Right']}
def vec(v):return [v.x,v.y,v.z]
def val(o,n):
    try:
        v=o.get_editor_property(n)
        if isinstance(v,(bool,float,int,str)):return v
        if hasattr(v,'export_text'):return v.export_text()
        return str(v)
    except Exception as e:raise RuntimeError('Missing native locomotion observation: '+n) from e
props=['PlayerRotationMode','LastAirReselectionFromTime','LastAirReselectionToTime','PlayerStateName','PlayerStateMachineState','PlayerMovementState','PlayerMovementStateLastFrame','PlayerHasVelocity','PlayerSpeed2D','PlayerAcceleration','PlayerVelocityAcceleration','PlayerBlendStackInputs','AirReselectionCount','PlayerFootPlacementEnable','PlayerOffsetRootBoneEnabled','PlayerCharacterProperties','PlayerRootTransform','PlayerFutureFacingDelta','PlayerTrajectoryTurnAngle','PlayerMovementModeRecent','PlayerMovementMode','PlayerMovementDirection','PlayerTrajectoryFutureFacing','PlayerTrajectoryFutureVelocity','PlayerNoValidAnim']
report={'project':unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),'world':world.get_path_name(),'pawn':pawn.get_path_name(),'anim':anim.get_class().get_path_name(),'samples':[],'net_update':pawn.get_editor_property('NetUpdateFrequency'),'falling':{n:str(mover.get_editor_property('MovementModes')['Falling'].get_editor_property(n))for n in ['AirControlPercentage','FallingDeceleration','FallingLateralFriction']}}
report['rotation_modes_expected']=['OrientToMovement']*cases_per_mode+['Strafe']*cases_per_mode
report['sprint_before_jump']=sprint_before_jump
report['sprint_through_landing']=sprint_through_landing
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
start=unreal.GameplayStatics.get_time_seconds(world)
handle=None
landing_phases={}

def landing_phase(asset):
    if not asset:return None
    name=asset.get_path_name()
    if name not in landing_phases:
        impacts=[]
        if isinstance(asset,unreal.AnimSequence):
            for event in unreal.AnimationLibrary.get_animation_notify_events(asset):
                notify=event.get_editor_property('notify')
                if isinstance(notify,unreal.Project_JAnimNotify_FoleyEvent) and str(notify.get_editor_property('FoleyEvent').get_editor_property('Event').get_editor_property('TagName'))=='Foley.Event.Land':
                    impacts.append(unreal.AnimationLibrary.get_anim_notify_event_trigger_time(event))
        landing_phases[name]=min(impacts) if impacts else None
    return landing_phases[name]
def finish(error=None):
    for a in actions.values():inputs.inject_input_vector_for_action(a,unreal.Vector(),[],[])
    unreal.unregister_slate_post_tick_callback(handle)
    for floor,scale in original_floor_scales:floor.set_actor_scale3d(scale)
    perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    controller.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    if error is None:
        try:
            report['checks']=check_air(report)
            landing_checks=[]
            for case in range(cases_per_mode*2):
                rows=[s for s in report['samples']if s['case']==case]
                airborne=[s for s in rows if 'Falling' in s['mode'] and s['u']>2.]
                assert airborne,'Missing physical jump'
                land=next(s for s in rows if s['u']>airborne[0]['u'] and 'Walking' in s['mode'])
                grounded=[s for s in rows if s['u']>=land['u']]
                assert all('Walking'in s['mode']for s in grounded),'Mover re-entered Falling after contact without a new jump'
                after=[s for s in rows if land['u']<=s['u']<land['u']+.15]
                assert all(s['PlayerStateName']not in ['In Air Transition','In Air Loop']for s in after),'Air state re-entered after physical landing'
                observed=[s for s in after if s['impact_time'] is not None]
                # A large turn can legitimately choose a grounded pivot instead.
                # A landing player must start near impact, never in its fall-in prefix.
                assert all(s['asset_time']>=s['impact_time']-.1 for s in observed),'Grounded landing replayed its airborne approach'
                if sprint_through_landing and case%cases_per_mode==0:
                    assert observed and all('_Land_Sprint_'in s['asset']for s in observed),'Held straight Sprint did not select Sprint landing'
                    assert all(abs(s['PlayerSpeed2D']-750.)<5. for s in after),'Mover lost held Sprint speed at landing'
                landing_checks.append({'case':case,'physical_landing_time':land['u'],'landing_assets':sorted({s['asset']for s in observed}),'first_player_time':observed[0]['asset_time']if observed else None})
            report['checks']['landings']=landing_checks
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('PlayerLocomotion regression completed: '+str(output)+' Success='+str(report['completed']))
def tick(delta):
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=cases_per_mode*10.:finish();return
        case=int(t/5.);u=t%5.;strafe=case>=cases_per_mode
        moving=True
        jump=2.<=u<2.12
        kind=case%cases_per_mode-(1 if sprint_through_landing else 0)
        change=kind>=0 and u>=(3.0 if kind<2 else 3.20) and u<4.5
        angle=90. if kind%2==0 else 180.
        values={'Move':unreal.Vector(0.,1.,0.),'Jump':unreal.Vector(1. if jump else 0.,0.,0.),'Sprint':unreal.Vector(float(sprint_before_jump and .5<=u and (sprint_through_landing or u<2.5)),0.,0.),'MoveIntent_Forward':unreal.Vector(1.,0.,0.),
                'ToggleCombat':unreal.Vector(float(.2<=u<1.4 and pawn.is_combat_mode_active()!=strafe),0.,0.)}
        for n,a in actions.items():inputs.inject_input_vector_for_action(a,values.get(n,unreal.Vector()),[],[])
        controller.set_control_rotation(unreal.Rotator(pitch=0.,yaw=angle if change else 0.,roll=0.))
        played=anim.inspect_foundation_playback()
        blend=anim.get_editor_property('PlayerBlendStackInputs');asset=blend.get_editor_property('Anim')
        times=re.findall(r'Asset=\S+ Time=([0-9.]+)',played)
        report['samples'].append({'asset':asset.get_path_name()if asset else '', 'asset_time':float(times[0])if times else -1.,'impact_time':landing_phase(asset),'case':case,'u':u,'control_yaw':controller.get_control_rotation().yaw,'t':t,'dt':delta,'moving':moving,'jump':jump,'pos':vec(pawn.get_actor_location()),'vel':vec(pawn.get_velocity()),'mesh_pos':vec(mesh.get_world_location()),'actor_yaw':pawn.get_actor_rotation().yaw,'physical':pawn.inspect_physical_lifecycle(),'mode':str(mover.get_movement_mode_name()),'played':played,**{n:val(anim,n)for n in props}})
    except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
