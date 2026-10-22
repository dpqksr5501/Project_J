"""PIE: production Run/Sprint speed, authored cadence and stance-foot observations.

Use a fresh flat-floor PIE starting at (-4500,-4500,260); the return path stays
inside the existing floor. World foot drift is a quality observation, not proof
of no slip from contact curves alone. No assets or levels are saved.
"""
import unreal,json,traceback,re,math,sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import local_world,local_inputs,output_file
world=local_world();pawn=unreal.GameplayStatics.get_player_pawn(world,0);pc=unreal.GameplayStatics.get_player_controller(world,0)
mesh=pawn.get_source_mesh();anim=mesh.get_anim_instance();inputs=local_inputs(world)
actions={n:unreal.load_asset('/Game/Input/Actions/IA_'+n)for n in ['Move','MoveIntent_Forward','Sprint','ToggleCombat']}
original_combat=pawn.is_combat_mode_active();original_rotation=pc.get_control_rotation()
output=output_file('SpeedCadence');r={'samples':[],'cases':[('OTM','Run',500.),('OTM','Sprint',750.),('Strafe','Run',500.),('Strafe','Sprint',750.)]}
start=unreal.GameplayStatics.get_time_seconds(world);handle=None

def vec(v):return [v.x,v.y,v.z]
def knee(side):
    a=mesh.get_socket_location('calf_'+side)-mesh.get_socket_location('thigh_'+side)
    b=mesh.get_socket_location('foot_'+side)-mesh.get_socket_location('calf_'+side)
    return math.degrees(math.acos(max(-1.,min(1.,a.dot(b)/(a.length()*b.length())))))
def validate():
    checks=[]
    for case,(mode,gait,speed)in enumerate(r['cases']):
        rows=[s for s in r['samples']if s['case']==case and 4.<s['u']<8.]
        assert len(rows)>30,(case,'Insufficient steady samples')
        assert all(abs(s['speed']-speed)<5. for s in rows),(case,'Requested movement speed was not reached')
        assert all(s['loop'] and not s['invalid'] for s in rows),(case,'Expected valid loop')
        assert all(s['strafe']==(mode=='Strafe')for s in rows),(case,'Expected rotation mode')
        for s in rows:
            players=re.findall(r'Asset=(\S+) Time=[0-9.]+ Length=[0-9.]+ Weight=([0-9.]+) Players=\d+ Rate=([0-9.]+)',s['played'])
            active=[(asset,float(rate))for asset,weight,rate in players if float(weight)>.25]
            assert active and all('/'+gait+'/'in asset and abs(rate-1.)<.001 for asset,rate in active),(case,active)
        drift={}
        for side in ['l','r']:
            speeds=[]
            for a,b in zip(rows,rows[1:]):
                dt=b['t']-a['t']
                if dt<.1 and min(a['contact_'+side],b['contact_'+side])>.9:
                    speeds.append(math.dist(a['foot_'+side][:2],b['foot_'+side][:2])/dt)
            speeds.sort();drift[side]={'samples':len(speeds),'median_cm_s':speeds[len(speeds)//2]if speeds else None,'p95_cm_s':speeds[min(len(speeds)-1,int(len(speeds)*.95))]if speeds else None}
        checks.append({'mode':mode,'gait':gait,'speed_min':min(s['speed']for s in rows),'speed_max':max(s['speed']for s in rows),'play_rate':1.,'knee_range_degrees':[min(s['knee_'+side]for s in rows for side in ['l','r']),max(s['knee_'+side]for s in rows for side in ['l','r'])],'stance_foot_world_speed_observation':drift})
    return checks

def finish(error=None):
    unreal.unregister_slate_post_tick_callback(handle)
    for a in actions.values():inputs.inject_input_vector_for_action(a,unreal.Vector(),[],[])
    pc.set_control_rotation(original_rotation)
    if pawn.is_combat_mode_active()!=original_combat:inputs.inject_input_vector_for_action(actions['ToggleCombat'],unreal.Vector(1,0,0),[],[])
    if error is None:
        try:r['checks']=validate()
        except Exception:error=traceback.format_exc()
    r['completed']=error is None
    if error:r['error']=error
    output.write_text(json.dumps(r,indent=2));unreal.log('Speed/cadence regression: '+str(output)+' Success='+str(r['completed']))

def tick(dt):
    try:
        t=unreal.GameplayStatics.get_time_seconds(world)-start
        if t>=40.:finish();return
        case=int(t/10.);u=t%10.;mode,gait,speed=r['cases'][case];moving=.5<u<8.5
        toggle=.1<u<.3 and pawn.is_combat_mode_active()!=(mode=='Strafe')
        pc.set_control_rotation(unreal.Rotator(pitch=0,yaw=0 if case<2 else 180,roll=0))
        for n,v in {'Move':unreal.Vector(0,float(moving),0),'MoveIntent_Forward':unreal.Vector(float(moving),0,0),'Sprint':unreal.Vector(float(moving and gait=='Sprint'),0,0),'ToggleCombat':unreal.Vector(float(toggle),0,0)}.items():inputs.inject_input_vector_for_action(actions[n],v,[],[])
        if r['samples']and t-r['samples'][-1]['t']<.03:return
        blend=anim.get_editor_property('PlayerBlendStackInputs')
        r['samples'].append({'t':t,'u':u,'case':case,'speed':pawn.get_velocity().length2d(),'strafe':pawn.is_combat_mode_active(),'loop':blend.get_editor_property('Loop'),'invalid':anim.get_editor_property('PlayerNoValidAnim'),'played':anim.inspect_foundation_playback(),**{'foot_'+s:vec(mesh.get_socket_location('foot_'+s))for s in ['l','r']},**{'knee_'+s:knee(s)for s in ['l','r']},**{'contact_'+s:anim.get_curve_value('contact_'+s)for s in ['l','r']}})
    except Exception:finish(traceback.format_exc())
handle=unreal.register_slate_post_tick_callback(tick)
