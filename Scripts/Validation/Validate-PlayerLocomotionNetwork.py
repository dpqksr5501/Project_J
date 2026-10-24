"""Two-player listen-server PIE: movement, attacks and post-action Stop recovery.

Runs 38 game seconds; observes authority/autonomous/simulated source meshes.
No assets or levels are saved.
"""
import sys
import unreal,json,traceback,time,re
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import output_file,check_network
output=output_file('Network')
worlds=unreal.EditorLevelLibrary.get_pie_worlds(False)
assert len(worlds)==2
controls=[]
for inputs in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem):
 if inputs.get_path_name().startswith('/Script/'):continue
 world=next(w for w in worlds if unreal.GameplayStatics.get_game_instance(w)==unreal.GameplayStatics.get_game_instance(inputs))
 pawn=unreal.GameplayStatics.get_player_pawn(world,0)
 controls.append((inputs,world,pawn,unreal.GameplayStatics.get_player_controller(world,0)))
assert len(controls)==2
pawns=[p for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Project_JPlayerCharacter)]
assert len(pawns)==4
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n) for n in ['Move','Sprint','Jump','MoveIntent_Forward','MoveIntent_Left','MoveIntent_Right','MoveIntent_Backward','ToggleCombat','Attack_LMB','Skill_Q','Skill_R','Skill_T']}
assert all(actions.values())
report={'project':unreal.Paths.project_dir(),'began':time.time(),'actors':[],'samples':[]}
for p in pawns:
 report['actors'].append({'path':p.get_path_name(),'role':str(p.get_local_role()),'local':p.is_locally_controlled(),'CMC':bool(p.get_component_by_class(unreal.CharacterMovementComponent)),'anim':p.get_source_mesh().get_anim_instance().get_class().get_path_name()})
start=unreal.GameplayStatics.get_time_seconds(worlds[0])
handle=None
def finish(error=None):
 for inputs,_,_,_ in controls:
  for a in actions.values():inputs.inject_input_vector_for_action(a,unreal.Vector(),[],[])
 unreal.unregister_slate_post_tick_callback(handle)
 if error is None:
  try:
   report['checks']=check_network(report)
   assert len({a['role'] for a in report['actors']})==3,'Expected all three network roles'
   stop_checks=[]
   for actor in report['actors']:
    rows=[a for sample in report['samples'] if 33.1<sample['t']<37. for a in sample['actors'] if a['path']==actor['path']]
    stops=[a for a in rows if any('_Stop_' in asset and float(weight)>.25 for asset,weight in re.findall(r'Index=\d+ Asset=(\S+) Time=[0-9.]+ Length=[0-9.]+ Weight=([0-9.]+)',a['played']))]
    assert stops,'A real post-action movement did not restore Stop: '+actor['path']
    stop_checks.append({'path':actor['path'],'role':actor['role'],'stop_frames':len(stops)})
   report['post_action_stops']=stop_checks
  except Exception:error=traceback.format_exc()
 report['completed']=error is None
 if error:report['error']=error
 for w in worlds:unreal.SystemLibrary.execute_console_command(w,'ProjectJ.Foley.Status')
 output.write_text(json.dumps(report,ensure_ascii=False,indent=2),encoding='utf-8')
def tick(dt):
 try:
  t=unreal.GameplayStatics.get_time_seconds(worlds[0])-start
  if t>=38:finish();return
  for inputs,w,p,pc in controls:
   client=p.get_local_role()==unreal.NetRole.ROLE_AUTONOMOUS_PROXY
   move=((1<=t<8) if client else (9<=t<14)) or 28<=t<33
   jump=(2<=t<2.15 or 6<=t<6.15) if client else (10<=t<10.15)
   sprint=(3<=t<4 or 6<=t<7) if client else (11<=t<12)
   toggle=(4.5<=t<4.65) if client else (14.5<=t<14.65)
   attack=(12<=t<12.15 or 12.7<=t<12.85) if client else (16.5<=t<16.65)
   pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=90 if client else 0,roll=0))
   left=client and 5<=t<7
   vals={'Move':unreal.Vector(-1 if left else 0,1 if move and not left else 0,0),
         'MoveIntent_Forward':unreal.Vector(1 if move and not left else 0,0,0),
         'MoveIntent_Left':unreal.Vector(1 if left else 0,0,0),
         'Jump':unreal.Vector(1 if jump else 0,0,0),'Sprint':unreal.Vector(1 if sprint else 0,0,0),
         'ToggleCombat':unreal.Vector(1 if toggle else 0,0,0),'Attack_LMB':unreal.Vector(1 if attack else 0,0,0),
         'Skill_R':unreal.Vector(1 if client and 19<=t<19.15 else 0,0,0),
         'Skill_T':unreal.Vector(1 if not client and 24<=t<24.15 else 0,0,0),
         'Skill_Q':unreal.Vector(1 if client and (25<=t<25.15 or 25.7<=t<25.85) else 0,0,0)}
   for n,a in actions.items():inputs.inject_input_vector_for_action(a,vals.get(n,unreal.Vector()),[],[])
  if not report['samples'] or t-report['samples'][-1]['t']>=.05:
   frame={'t':t,'dt':dt,'wall':time.time(),'actors':[]}
   for p in pawns:
    loc=p.get_actor_location();v=p.get_velocity();anim=p.get_source_mesh().get_anim_instance();mont=p.get_current_montage()
    frame['actors'].append({'path':p.get_path_name(),'pos':[loc.x,loc.y,loc.z],'v':[v.x,v.y,v.z],'yaw':p.get_actor_rotation().yaw,
       'approved_input':str(anim.get_editor_property('PlayerCharacterProperties').get_editor_property('InputAcceleration')),'physical':p.inspect_physical_lifecycle(),'mode':str(p.get_mover().get_movement_mode_name()),'combat':p.is_combat_mode_active(),
       'montage':mont.get_path_name() if mont else None,'montage_time':anim.montage_get_position(mont) if mont else None,
       'played':anim.inspect_foundation_playback(),'foot':bool(anim.get_editor_property('PlayerFootPlacementEnable')),'root_offset':bool(anim.get_editor_property('PlayerOffsetRootBoneEnabled')),'walking_class':p.get_mover().get_editor_property('MovementModes')['Walking'].get_class().get_path_name()})
   report['samples'].append(frame)
 except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
