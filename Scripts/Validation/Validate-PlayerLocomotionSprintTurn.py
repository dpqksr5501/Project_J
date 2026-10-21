"""Live OTM/Strafe sprint mouse-turn matrix on both sides of the 180-degree seam."""
import sys
import unreal,json,traceback,math
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import output_file,check_sprint_turn
output=output_file("SprintTurn")
w=unreal.EditorLevelLibrary.get_pie_worlds(False)[0];p=unreal.GameplayStatics.get_player_pawn(w,0);pc=unreal.GameplayStatics.get_player_controller(w,0);m=p.get_source_mesh();a=m.get_anim_instance();i=next(s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem)if not s.get_path_name().startswith('/Script/'))
acts={n:unreal.load_asset('/Game/Input/Actions/IA_'+n)for n in ['Move','MoveIntent_Forward','Sprint']};r={'project':unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()),'samples':[]};start=unreal.GameplayStatics.get_time_seconds(w);h=None

original_combat=p.is_combat_mode_active();original_rotation=pc.get_control_rotation()

def val(n):
 x=a.get_editor_property(n);return x.export_text()if hasattr(x,'export_text')else x if isinstance(x,(bool,float,int,str))else str(x)
def finish(error=None):
 unreal.unregister_slate_post_tick_callback(h)
 for x in acts.values():i.inject_input_vector_for_action(x,unreal.Vector(),[],[])
 pc.set_control_rotation(original_rotation)
 if p.is_combat_mode_active()!=original_combat:p.toggle_combat_mode()
 if error is None:
  try:r['checks']=check_sprint_turn(r)
  except Exception:error=traceback.format_exc()
 r['completed']=error is None
 if error:r['error']=error
 output.write_text(json.dumps(r,indent=2),encoding='utf-8')
def tick(dt):
 try:
  t=unreal.GameplayStatics.get_time_seconds(w)-start
  if t>=48.:finish();return
  case=int(t/6.);u=t%6.;strafe=case>=4
  if p.is_combat_mode_active()!=strafe:p.toggle_combat_mode()
  angle=[170.,179.,181.,190.][case%4];yaw=angle*max(0.,min(1.,(u-2.5)/.18));pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=yaw,roll=0))
  moving=.3<=u<5.;values={'Move':unreal.Vector(0,1 if moving else 0,0),'MoveIntent_Forward':unreal.Vector(1 if moving else 0,0,0),'Sprint':unreal.Vector(1 if moving else 0,0,0)}
  for n,x in acts.items():i.inject_input_vector_for_action(x,values[n],[],[])
  v=p.get_velocity();r['samples'].append({'t':t,'u':u,'case':case,'yaw':p.get_actor_rotation().yaw,'control':pc.get_control_rotation().yaw,'speed':v.length2d(),'velocity':[v.x,v.y,v.z],'played':a.inspect_foundation_playback(),**{n:val(n)for n in ['PlayerStateName','PlayerGait','PlayerMovementDirection','PlayerMovementState','PlayerTrajectoryIsCircling','PlayerTrajectoryTurnAngle','PlayerFutureFacingDelta','PlayerRootTransform','PlayerTrajectoryCurrentAngularVelocity','PlayerNoValidAnim']}})
 except Exception:finish(traceback.format_exc())
h=unreal.register_slate_post_tick_callback(tick)
