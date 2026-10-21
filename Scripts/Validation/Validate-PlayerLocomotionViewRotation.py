"""Two-player PIE: camera-facing presentation must remain independent of body yaw.

Use a fresh flat-floor PIE session. No assets or levels are saved.
The test drives real input; it never writes animation or movement state.
"""
import json
import importlib
import math
from pathlib import Path
import re
import sys
import traceback
import unreal

sys.path.insert(0,str(Path(__file__).parent))
regression=importlib.reload(importlib.import_module('PlayerLocomotionRegression'))
output_file,check_view_rotation=regression.output_file,regression.check_view_rotation

output=output_file('ViewRotation')
worlds=unreal.EditorLevelLibrary.get_pie_worlds(False)
assert len(worlds)==2, 'Two-player PIE is required'
controls=[]
for inputs in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem):
    if inputs.get_path_name().startswith('/Script/'):continue
    world=next(w for w in worlds if unreal.GameplayStatics.get_game_instance(w)==unreal.GameplayStatics.get_game_instance(inputs))
    pawn=unreal.GameplayStatics.get_player_pawn(world,0)
    pc=unreal.GameplayStatics.get_player_controller(world,0)
    controls.append((inputs,pawn,pc,pawn.is_combat_mode_active(),pc.get_control_rotation()))
assert len(controls)==2
pawns=[p for w in worlds for p in unreal.GameplayStatics.get_all_actors_of_class(w,unreal.Project_JPlayerCharacter)]
assert len(pawns)==4
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n) for n in ['Move','MoveIntent_Forward','ToggleCombat']}
assert all(actions.values())
report={'samples':[],'actors':[{'path':p.get_path_name(),'role':str(p.get_local_role()),'local':p.is_locally_controlled(),'controller':bool(p.get_controller())} for p in pawns]}
start=unreal.GameplayStatics.get_time_seconds(worlds[0])
handle=None

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(handle)
    for inputs,p,pc,combat,rotation in controls:
        for action in actions.values():inputs.inject_input_vector_for_action(action,unreal.Vector(),[],[])
        pc.set_control_rotation(rotation)
        # GAS activation must run in the local world's input tick, especially in
        # multi-world PIE. Never invoke a client ability directly from editor Python.
        if p.is_combat_mode_active()!=combat:
            inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    if error is None:
        try:report['checks']=check_view_rotation(report)
        except Exception:error=traceback.format_exc()
    report['completed']=error is None
    if error:report['error']=error
    output.write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('PlayerLocomotion view regression completed: '+str(output)+' Success='+str(report['completed']))

def tick(dt):
    try:
        t=unreal.GameplayStatics.get_time_seconds(worlds[0])-start
        if t>=20.:finish();return
        moving=4.<=t<17.
        # Smooth camera sweeps cross both sides of the +/-180-degree seam.
        yaw=190.*math.sin((t-4.)*math.pi/2.) if moving else 0.
        for inputs,_,pc,combat,_ in controls:
            pc.set_control_rotation(unreal.Rotator(pitch=20. if moving else 0.,yaw=yaw,roll=0.))
            values={'Move':unreal.Vector(0,1,0) if moving else unreal.Vector(),
                'MoveIntent_Forward':unreal.Vector(1,0,0) if moving else unreal.Vector(),
                'ToggleCombat':unreal.Vector(1,0,0) if not combat and (.5<=t<.65 or 17.5<=t<17.65) else unreal.Vector()}
            for name,action in actions.items():inputs.inject_input_vector_for_action(action,values[name],[],[])
        if report['samples'] and t-report['samples'][-1]['t']<.03:return
        sample={'t':t,'moving':moving,'actors':[]}
        for p in pawns:
            a=p.get_source_mesh().get_anim_instance()
            properties=a.get_editor_property('PlayerCharacterProperties').export_text()
            match=re.search(r'AimingRotation=\(Pitch=([^,]+),Yaw=([^,]+),Roll=([^\)]+)\)',properties)
            assert match,'Missing authored aiming rotation'
            aim=[float(v) for v in match.groups()]
            smoothed=a.get_editor_property('PlayerSmoothedAimTarget')
            ao=a.get_editor_property('PlayerAO')
            sample['actors'].append({'path':p.get_path_name(),'aim':aim,'body_yaw':p.get_actor_rotation().yaw,
                'smoothed':[smoothed.pitch,smoothed.yaw,smoothed.roll],'ao':[ao.x,ao.y],
                'combat':p.is_combat_mode_active(),'invalid':bool(a.get_editor_property('PlayerNoValidAnim'))})
        report['samples'].append(sample)
    except Exception:finish(traceback.format_exc())

handle=unreal.register_slate_post_tick_callback(tick)
