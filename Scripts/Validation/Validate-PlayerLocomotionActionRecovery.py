"""PIE regression for full-body action -> locomotion recovery ownership.

Use a fresh BP_GreatSword PIE on the native Floor at (-4500,-4500,260).
Exercises production animations, actual Mover root motion and GAS T, in both
rotation modes. Direct montage cases isolate presentation from GAS input locks.
No assets/settings are saved; temporary inputs/floor/throttle are restored.
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
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n) for n in
         ['Move','MoveIntent_Forward','ToggleCombat','Jump','Skill_T']}
montages={n:unreal.load_asset('/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_'+n)
          for n in ['LMB1','R','Equip']}
assert all(actions.values()) and all(montages.values())
assert not pawn.get_current_montage(),'Start after equipment initialization'
original_combat=pawn.is_combat_mode_active();original_rotation=controller.get_control_rotation()
perf=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle=perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground',False)
floors=[]
for w in unreal.EditorLevelLibrary.get_pie_worlds(True):
    for floor in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.StaticMeshActor):
        if floor.get_actor_label()=='Floor':
            scale=floor.get_actor_scale3d();floors.append((floor,scale))
            floor.set_actor_scale3d(unreal.Vector(max(1200.,scale.x),max(1200.,scale.y),scale.z))
assert floors,'Expected the documented flat native Floor'
names=['NormalStop','MoveRelease','StartThenAction','MoveDuringAction','StopThenAction','HoldThrough','StationaryRootMotion',
       'UpperBodyStop','CancelRootMotion','ActionFlightLand','JumpAfterAction']
# GAS flying attacks require combat mode. Test stationary and moving entry there.
cases=[('Strafe','ActionFlightLandMoving' if mode=='Strafe' else name) if name=='ActionFlightLand' else (mode,name)
       for mode in ['OTM','Strafe'] for name in names]
case_filter=globals().get('PLAYER_ACTION_RECOVERY_FILTER')
if case_filter:cases=[c for c in cases if c[1] in case_filter]
assert cases,'No requested action recovery cases'
report={'cases':cases,'samples':[],'events':[]};output=output_file('ActionRecovery')
start=unreal.GameplayStatics.get_time_seconds(world);fired=set();cancelled=set();busy=False;finished=False

def validate():
    checks=[]
    for case,(mode,name) in enumerate(cases):
        rows=[s for s in report['samples'] if s['case']==case]
        assert len(rows)>100,'Insufficient evaluated frames: '+str((case,len(rows)))
        assert all(not s['invalid'] and s['foot'] for s in rows if s['u']>1.4),'Missing selection or globally disabled foot correction'
        assert max(s['speed'] for s in rows if 3.<s['u']<4.5)>490. or name in ['StationaryRootMotion','ActionFlightLand','StartThenAction','MoveDuringAction'],'Move never reached the ground loop'
        if name in ['StartThenAction','MoveDuringAction']:
            resumed=[s for s in rows if 6.4<s['u']<8.8 and s['weight']<.01 and not s['montage']]
            assert resumed and max(s['speed'] for s in resumed)>490.,'Held input did not resume movement'
            assert not any('_Start_' in s['asset'] for s in resumed),'Action replayed its hidden start: '+str(case)
        if name=='StationaryRootMotion':
            assert not any('MOVING:' in s['moving_state'] for s in rows if s['action_active'] and not s['approved_input']),'Root motion created a locomotion episode'
        if name=='NormalStop':
            assert any('_Start_' in s['asset'] for s in rows if 1.7<s['u']<5.),'Independent start was suppressed'
        if name!='NormalStop':
            assert any(s['montage'] for s in rows if 5.<s['u']<6.),'Action did not play: '+str(case)
        if name in ['NormalStop','HoldThrough','UpperBodyStop','StartThenAction','MoveDuringAction']:
            release=9. if name in ['HoldThrough','StartThenAction','MoveDuringAction'] else 5.
            stops=[s for s in rows if s['u']>release and '_Stop_' in s['asset']]
            assert stops,'Independent ground stopping was suppressed: '+str(case)
            if name=='UpperBodyStop':assert max(s['weight'] for s in rows)<.01,'Equip must remain UpperBody'
        else:
            visible=[s for s in rows if s['u']>5.25 and s['weight']<.01 and not s['montage']]
            assert visible,'No post-action frames: '+str(case)
            assert all(s['base_weight']>.25 for s in visible),'No visible base pose: '+str(case)
            assert not any('_Start_' in s['asset'] for s in visible if not s['approved_input'] and 'Walking' in s['physical_mode']),'Uncommanded post-action start: '+str(case)
            assert not any('_Stop_' in s['asset'] for s in visible),'Action replayed its hidden stop: '+str(case)
            if name!='JumpAfterAction':
                assert not any('_Land_' in s['asset'] for s in visible),'Action-only motion minted a landing: '+str(case)
            else:
                assert any('Falling' in s['physical_mode'] and s['u']>7. for s in rows),'Physical jump not exercised'
                assert any('_Land_' in s['asset'] and s['u']>7. for s in rows),'Real post-action landing was suppressed'
            if name.startswith('ActionFlightLand'):
                assert any('Flying' in s['physical_mode'] for s in rows),'GAS forced-flight contract was not exercised'
                assert not any('Falling' in s['physical_mode'] for s in rows if s['u']>5.),'This case must isolate authored Land from a real fall'
        assert any(s['u']>11. and s['speed']<2. and s['state'] in ['Idle Loop','Idle Break'] for s in rows),'Failed to settle after action: '+str(case)
        checks.append({'case':case,'mode':mode,'name':name,'frames':len(rows),
                       'assets':sorted({s['asset'] for s in rows if s['u']>5.})})
    return checks

def finish(error=None):
    global finished
    if finished:return
    finished=True;unreal.unregister_slate_post_tick_callback(handle)
    if error is None:
        try:report['checks']=validate()
        except Exception:error=traceback.format_exc()
    for action in actions.values():inputs.inject_input_vector_for_action(action,unreal.Vector(),[],[])
    pawn.stop_anim_montage();controller.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:
        inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    for floor,scale in floors:floor.set_actor_scale3d(scale)
    perf.set_editor_property('bThrottleCPUWhenNotForeground',original_throttle)
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('Action recovery regression: '+str(output)+' success='+str(report['completed']))

def tick(dt):
    global busy
    if busy or finished:return
    busy=True
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=12.*len(cases):finish();return
        case=int(t/12.);u=t%12.;mode,name=cases[case]
        if report['samples'] and report['samples'][-1]['case']!=case:
            output.with_suffix('.progress.json').write_text(json.dumps({'finished_cases':case,'total_cases':len(cases),'samples':len(report['samples'])}))
        release=9. if name in ['HoldThrough','StartThenAction','MoveDuringAction'] else 4.65 if name=='StopThenAction' else 5.07 if name in ['MoveRelease','CancelRootMotion'] else 5.
        move_start=4.65 if name=='StartThenAction' else 5.3 if name=='MoveDuringAction' else 1.7
        moving=move_start<=u<release and name not in ['StationaryRootMotion','ActionFlightLand']
        controller.set_control_rotation(unreal.Rotator(pitch=0,yaw=180.*(case%2),roll=0))
        values={'Move':unreal.Vector(0,float(moving),0),'MoveIntent_Forward':unreal.Vector(float(moving),0,0),
                'ToggleCombat':unreal.Vector(float(.1<=u<2.6 and int(u*10.)%2==0 and pawn.is_combat_mode_active()!=(mode=='Strafe')),0,0),
                'Jump':unreal.Vector(float(name=='JumpAfterAction' and 7.<=u<7.12),0,0),
                'Skill_T':unreal.Vector(float(name.startswith('ActionFlightLand') and 5.<=u<5.12),0,0)}
        for n,action in actions.items():inputs.inject_input_vector_for_action(action,values[n],[],[])
        if 2.8<=u<5.:assert pawn.is_combat_mode_active()==(mode=='Strafe'),'Entry mode was not established'
        if u>=5. and case not in fired:
            fired.add(case)
            if name!='NormalStop' and not name.startswith('ActionFlightLand'):
                montage=montages['Equip' if name=='UpperBodyStop' else 'R' if name=='CancelRootMotion' else 'LMB1']
                duration=pawn.play_anim_montage(montage,1.,'')
                assert duration>0.,'Montage start failed'
                report['events'].append({'case':case,'u':u,'montage':montage.get_path_name(),'duration':duration})
        if name=='CancelRootMotion' and u>=5.9 and case not in cancelled:
            cancelled.add(case);pawn.stop_anim_montage();report['events'].append({'case':case,'u':u,'cancel':True})
        if report['samples'] and t-report['samples'][-1]['t']<.025:return
        played=anim.inspect_foundation_playback();active=pawn.get_current_montage()
        candidates=re.findall(r'Index=(\d+) Asset=(\S+) Time=([0-9.]+) Length=[0-9.]+ Weight=([0-9.]+)',played)
        candidates=[c for c in candidates if c[1]!='None']
        assert candidates,'No evaluated BlendStack'
        playback=max(candidates,key=lambda c:float(c[3]))
        approved=anim.get_editor_property('PlayerCharacterProperties').get_editor_property('InputAcceleration')
        full_body=bool(active and any(str(track.get_editor_property('SlotName'))=='DefaultSlot' for track in active.get_editor_property('SlotAnimTracks')))
        report['samples'].append({'case':case,'t':t,'u':u,'input':moving,'speed':pawn.get_velocity().length2d(),
            'moving_state':str(anim.get_editor_property('PlayerMovementState')),
            'physical_mode':str(pawn.get_mover().get_movement_mode_name()),'physical':pawn.inspect_physical_lifecycle(),
            'combat_mode':pawn.is_combat_mode_active(),'state':str(anim.get_editor_property('PlayerStateName')),'asset':playback[1],
            'time':float(playback[2]),'base_weight':float(playback[3]),'montage':active.get_name() if active else None,
            'weight':anim.blueprint_get_slot_montage_local_weight('DefaultSlot'),'invalid':anim.get_editor_property('PlayerNoValidAnim'),
            'foot':anim.get_editor_property('PlayerFootPlacementEnable'),
            'approved_input':approved.length2d()>1.e-4,
            'action_active':full_body or anim.blueprint_get_slot_montage_local_weight('DefaultSlot')>1.e-5})
    except Exception:finish(traceback.format_exc())
    finally:busy=False
handle=unreal.register_slate_post_tick_callback(tick)
unreal.log('Action recovery regression started: '+str(output))
