"""PIE: camera-fixed Strafe rear-diagonal starts with both keyboard edge orders.

Walk/Run, left/right rear diagonals, simultaneous/horizontal-first/vertical-first.
Each condition exercises a fresh start, a stopped restart and a rolling restart.
Fixture timings drive input only; production has no key wait or forced Start.
Run in a fresh PIE on a clear floor with no concurrent manual input. No save.
"""
import unreal,json,math,sys,traceback
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import local_world,local_inputs,output_file
world=local_world();pawn=unreal.GameplayStatics.get_player_pawn(world,0);controller=unreal.GameplayStatics.get_player_controller(world,0);anim=pawn.get_source_mesh().get_anim_instance();inputs=local_inputs(world)
move=unreal.load_asset('/Game/Input/Actions/IA_Move');combat=unreal.load_asset('/Game/Input/Actions/IA_ToggleCombat')
cases=[(g,a,p) for g in ['Run','Walk'] for a in [135,-135] for p in ['Together','HorizontalFirst','VerticalFirst']]
start=unreal.GameplayStatics.get_time_seconds(world);finished=False;busy=False
original_combat=pawn.is_combat_mode_active();original_rotation=controller.get_control_rotation()
report={'cases':cases,'samples':[]};output=output_file('StrafeStarts')
def validate():
 checks=[]
 for i,case in enumerate(cases):
  samples=[s for s in report['samples'] if s['case']==i]
  assert len(samples)>200, 'Insufficient actual samples: '+str(case)
  assert all(not s['invalid'] and s['foot'] and s['offset'] for s in samples), 'Invalid animation or disabled correction'
  for start in [2.5,4.4,5.7]:
   before=min(samples,key=lambda s:abs(s['u']-start))
   frames=[s for s in samples if start+.12<s['u']<start+.65]
   assert frames and all('STRAFE' in s['rotation_mode'] for s in frames), 'Expected Strafe was not exercised'
   assert all('BACKWARD' in s['direction'] for s in frames), 'Rear diagonal changed to a forward/side family: '+str(case)
   starts=[s for s in frames if 'Start' in s['tags']]
   if before['speed']<10.:
    assert starts, 'A stationary rear-diagonal start was skipped: '+str(case)
    assert any(s['selected'] in s['played'] for s in starts), 'Selected Start did not reach an evaluated BlendStack'
   settled=[s for s in samples if start+.7<s['u']<start+1.]
   assert settled and all(abs((s['yaw']+180.)%360.-180.)<90. for s in settled), 'Body turned away from the camera-facing hemisphere'
   assert all(abs((s['root_yaw']+180.)%360.-180.)<90. for s in settled), 'Displayed source root turned into forward travel'
   checks.append({'case':case,'input_at':start,'physical_speed_before':before['speed'],'start_was_required':before['speed']<10.,
     'start_clips':sorted({s['selected'] for s in starts}), 'max_body_yaw':max(abs(s['yaw']) for s in settled)})
 return checks

def finish(error=None):
 global finished
 if finished:return
 finished=True;unreal.unregister_slate_post_tick_callback(handle);inputs.inject_input_vector_for_action(move,unreal.Vector(),[],[])
 if error is None:
  try:report['checks']=validate()
  except Exception:error=traceback.format_exc()
 controller.set_control_rotation(original_rotation);pawn.set_walk(False)
 if pawn.is_combat_mode_active()!=original_combat:inputs.inject_input_vector_for_action(combat,unreal.Vector(1,0,0),[],[])
 report['completed']=error is None
 if error:report['error']=error
 output.write_text(json.dumps(report,indent=2),encoding='utf-8')
def tick(dt):
 global busy
 if busy or finished:return
 busy=True
 try:
  t=unreal.GameplayStatics.get_time_seconds(world)-start
  if t>=7.*len(cases):finish();return
  i=int(t/7.);u=t%7.;g,a,p=cases[i];pawn.set_walk(g=='Walk');controller.set_control_rotation(unreal.Rotator())
  inputs.inject_input_vector_for_action(combat,unreal.Vector(float(u<2. and int(u*10)%2==0 and not pawn.is_combat_mode_active()),0,0),[],[])
  x=y=0.
  if 2.5<=u<3.6 or 4.4<=u<5.5 or 5.7<=u<6.8:
   if u<3.6:phase=u-2.5
   elif u<5.5:phase=u-4.4
   else:phase=u-5.7
   a=math.radians(a);x=math.sin(a);y=math.cos(a)
   if phase<.05:
    if p=='HorizontalFirst':y=0.
    elif p=='VerticalFirst':x=0.
  inputs.inject_input_vector_for_action(move,unreal.Vector(x,y,0),[],[])
  blend=anim.get_editor_property('PlayerBlendStackInputs');asset=blend.get_editor_property('Anim')
  report['samples'].append({'t':t,'u':u,'case':i,'speed':pawn.get_velocity().length2d(),'keys':[x,y],
    'state':str(anim.get_editor_property('PlayerMovementState')),'state_time':anim.get_editor_property('PlayerMovementStateTime'),
    'rotation_mode':str(anim.get_editor_property('PlayerRotationMode')),'selected':asset.get_name() if asset else '',
    'start_time':blend.get_editor_property('StartTime'),'tags':str(blend.get_editor_property('Tags')),'invalid':anim.get_editor_property('PlayerNoValidAnim'),
    'direction':str(anim.get_editor_property('PlayerMovementDirection')),'recent':str(anim.get_editor_property('PlayerMovementDirectionRecent')),
    'turn':anim.get_editor_property('PlayerTrajectoryTurnAngle'),'facing':anim.get_editor_property('PlayerFutureFacingDelta'),
    'yaw':pawn.get_actor_rotation().yaw,'root_yaw':anim.get_editor_property('PlayerRootTransform').rotation.rotator().yaw,
    'foot':anim.get_editor_property('PlayerFootPlacementEnable'),'offset':anim.get_editor_property('PlayerOffsetRootBoneEnabled'),
    'played':anim.inspect_foundation_playback()})
 except Exception:finish(traceback.format_exc())
 finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
