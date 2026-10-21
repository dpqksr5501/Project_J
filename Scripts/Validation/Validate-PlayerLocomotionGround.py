"""Single PIE ground regression: OTM/Strafe, starts/stops, keyboard/camera 180, Walk/Sprint."""
import sys
import unreal,json,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import output_file,check_ground
output=output_file('Ground')
world=unreal.EditorLevelLibrary.get_pie_worlds(False)[0]
pawn=unreal.GameplayStatics.get_player_pawn(world,0);assert pawn and pawn.get_source_mesh()
mover=pawn.get_component_by_class(unreal.CharacterMoverComponent)
anim=pawn.get_source_mesh().get_anim_instance()
controller=unreal.GameplayStatics.get_player_controller(world,0)
inputs=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem)if not s.get_path_name().startswith('/Script/'))
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+('ToggleCombat'if n=='Strafe'else n))for n in ['Move','Strafe','Sprint','Walk','MoveIntent_Forward','MoveIntent_Backward','MoveIntent_Left','MoveIntent_Right']}
assert all(actions.values())
report={'project':unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),'samples':[]}
report['foot_rigs']=[rig.get_class().get_path_name()for rig in unreal.ObjectIterator(unreal.ControlRig)if rig.get_outer()==pawn.get_source_mesh()]
assert report['foot_rigs']==['/Game/Animation_Logic/Rigs/CR_ProjectJ_Manny_FootPlacement.CR_ProjectJ_Manny_FootPlacement_C'],report['foot_rigs']
original_combat=pawn.is_combat_mode_active()
original_rotation=controller.get_control_rotation()
if original_combat:pawn.toggle_combat_mode()
pawn.set_walk(False)
start=unreal.GameplayStatics.get_time_seconds(world);handle=None
def value(o,n):
    try:
        v=o.get_editor_property(n)
        return v.export_text()if hasattr(v,'export_text')else v if isinstance(v,(bool,int,float,str))else str(v)
    except Exception as e:raise RuntimeError('Missing native locomotion observation: '+n) from e
def finish(error=None):
    for a in actions.values():inputs.inject_input_vector_for_action(a,unreal.Vector(),[],[])
    unreal.unregister_slate_post_tick_callback(handle)
    controller.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:pawn.toggle_combat_mode()
    pawn.set_walk(False)
    if error is None:
        try:report['checks']=check_ground(report)
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('PlayerLocomotion ground regression completed: '+str(output)+' Success='+str(report['completed']))
def tick(dt):
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=40.:finish();return
        yaw=0 if t<11 or 26<=t else 180
        forward=1<=t<4 or 8<=t<14 or 16<=t<19 or 28<=t<31 or 33<=t<37
        back=4<=t<7 or 19<=t<22;left=22<=t<24;right=24<=t<26
        vals={'Move':unreal.Vector(-1 if left else 1 if right else 0,1 if forward else -1 if back else 0,0),
              'Strafe':unreal.Vector(1 if 15.<=t<15.12 else 0,0,0),'Walk':unreal.Vector(1 if 32.<=t<32.12 or 34.<=t<34.12 else 0,0,0),
              'Sprint':unreal.Vector(1 if 35.<=t<37 else 0,0,0)}
        vals.update({'MoveIntent_'+n:unreal.Vector(1 if v else 0,0,0)for n,v in [('Forward',forward),('Backward',back),('Left',left),('Right',right)]})
        for n,a in actions.items():inputs.inject_input_vector_for_action(a,vals[n],[],[])
        controller.set_control_rotation(unreal.Rotator(pitch=0,yaw=yaw,roll=0))
        if not report['samples']or t-report['samples'][-1]['t']>=.016:
            v=mover.get_velocity();p=pawn.get_actor_location()
            report['samples'].append({'t':t,'dt':dt,'yaw':pawn.get_actor_rotation().yaw,'control_yaw':controller.get_control_rotation().yaw,
                'pos':[p.x,p.y,p.z],'vel':[v.x,v.y,v.z],'mode':str(mover.get_movement_mode_name()),
                **{n:value(anim,n)for n in ['PlayerStateName','PlayerMovementState','PlayerMovementDirection','PlayerRotationMode','PlayerGait','PlayerTrajectoryFutureVelocity','PlayerTrajectoryTurnAngle','PlayerRootTransform','PlayerCharacterTransform','PlayerBlendStackInputs','PlayerHasAcceleration','PlayerOffsetRootBoneEnabled','PlayerFootPlacementEnable','PlayerNoValidAnim']},
                'custom':value(anim,'PlayerCharacterProperties'),'played':anim.inspect_foundation_playback(),'physical':pawn.inspect_physical_lifecycle(), 'walking_class':mover.get_editor_property('MovementModes')['Walking'].get_class().get_path_name()})
    except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
