"""Run in the documented flat-floor PIE fixture. No assets or levels are saved."""
import sys
import unreal,json,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import output_file,check_air
output=output_file('SprintAir')
world=next(w for w in unreal.EditorLevelLibrary.get_pie_worlds(False) if unreal.GameplayStatics.get_player_controller(w,0) and unreal.GameplayStatics.get_player_controller(w,0).is_local_controller())
pawn=unreal.GameplayStatics.get_player_pawn(world,0)
controller=unreal.GameplayStatics.get_player_controller(world,0)
original_combat=pawn.is_combat_mode_active()
original_rotation=controller.get_control_rotation()
if not pawn.is_combat_mode_active():pawn.toggle_combat_mode()
pawn.set_walk(False)
mesh=pawn.get_source_mesh();anim=mesh.get_anim_instance();mover=pawn.get_mover()
systems=[s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem) if not s.get_path_name().startswith('/Script/')]
assert len(systems)==1
inputs=systems[0]
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n) for n in ['Move','Jump','Sprint','ToggleCombat','MoveIntent_Forward','MoveIntent_Backward','MoveIntent_Left','MoveIntent_Right']}
def vec(v):return [v.x,v.y,v.z]
def val(o,n):
    try:
        v=o.get_editor_property(n)
        if isinstance(v,(bool,float,int,str)):return v
        if hasattr(v,'export_text'):return v.export_text()
        return str(v)
    except Exception as e:raise RuntimeError('Missing native locomotion observation: '+n) from e
props=['LastAirReselectionFromTime','LastAirReselectionToTime','PlayerStateName','PlayerStateMachineState','PlayerMovementState','PlayerMovementStateLastFrame','PlayerHasVelocity','PlayerHasAcceleration','PlayerSpeed2D','PlayerAcceleration','PlayerVelocityAcceleration','PlayerBlendStackInputs','AirReselectionCount','PlayerFootPlacementEnable','PlayerOffsetRootBoneEnabled','PlayerCharacterProperties','PlayerTrajectory','PlayerNoValidAnim']
report={'project':unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),'world':world.get_path_name(),'pawn':pawn.get_path_name(),'anim':anim.get_class().get_path_name(),'samples':[]}
start=unreal.GameplayStatics.get_time_seconds(world)
handle=None
def finish(error=None):
    for a in actions.values():inputs.inject_input_vector_for_action(a,unreal.Vector(),[],[])
    unreal.unregister_slate_post_tick_callback(handle)
    controller.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:pawn.toggle_combat_mode()
    if error is None:
        try:report['checks']=check_air(report)
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('PlayerLocomotion regression completed: '+str(output)+' Success='+str(report['completed']))
def tick(delta):
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start-3.
        if t<0.:return
        if t>=32.:finish();return
        case=int(t/4.);u=t%4.;strafe=True
        assert pawn.is_combat_mode_active(), "Strafe test requires combat mode"
        moving=True
        jump=1.5<=u<1.62
        directions=[(0.,1.),(-1.,1.),(1.,1.),(-1.,0.),(1.,0.),(-1.,-1.),(0.,-1.),(1.,-1.)]
        dx,dy=directions[case] if 1.7<=u<3. else (0.,1.)
        values={'Move':unreal.Vector(dx,dy,0.),'Sprint':unreal.Vector(1.,0.,0.),'Jump':unreal.Vector(1. if jump else 0.,0.,0.),'MoveIntent_Forward':unreal.Vector(1. if dy>0 else 0.,0.,0.),'MoveIntent_Backward':unreal.Vector(1. if dy<0 else 0.,0.,0.),'MoveIntent_Left':unreal.Vector(1. if dx<0 else 0.,0.,0.),'MoveIntent_Right':unreal.Vector(1. if dx>0 else 0.,0.,0.)}
        for n,a in actions.items():inputs.inject_input_vector_for_action(a,values.get(n,unreal.Vector()),[],[])
        controller.set_control_rotation(unreal.Rotator())
        report['samples'].append({'case':case,'u':u,'control_yaw':controller.get_control_rotation().yaw,'t':t,'dt':delta,'moving':moving,'jump':jump,'pos':vec(pawn.get_actor_location()),'vel':vec(pawn.get_velocity()),'mesh_pos':vec(mesh.get_world_location()),'actor_yaw':pawn.get_actor_rotation().yaw,'physical':pawn.inspect_physical_lifecycle(),'mode':str(mover.get_movement_mode_name()),'played':anim.inspect_foundation_playback(),**{n:val(anim,n)for n in props}})
    except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
