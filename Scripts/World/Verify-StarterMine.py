"""Saved H08 adit: actual floor, wall/roof, headroom and entrance collision."""
import hashlib,importlib.util,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';P=json.loads((SOURCE/'StarterWorld.H08.mine.json').read_text());B=json.loads((OUT/'H08-mine-build.json').read_text());assert B['phase']=='COMPLETE'
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
C=module('contract','Starter-WorldDefinitionContract.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');grid=T.grid if hasattr(T,'grid') else T.manifest['world_grid'];origin=grid['unreal_origin_cm'];cx,cy,floor=P['center_m'];co,si=math.cos(math.radians(P['yaw_degrees'])),math.sin(math.radians(P['yaw_degrees']))
assert module('support','Starter-SettlementSupport.py').plan_mine_entry_steps(P,T.height)==B['stairs'],'Saved entry steps differ from the shared source contract'
def pt(u,v,z):return unreal.Vector(origin[0]+(cx+co*u-si*v)*100,origin[1]+(cy+si*u+co*v)*100,(floor+z)*100+origin[2])
def trace(world,a,b):
    h=unreal.SystemLibrary.line_trace_single(world,a,b,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);return h.to_tuple() if h else None
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);R=dict(passed=False,read_only=True,art_complete=False)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();a=next(a for a in actors if a.get_path_name()==B['actor_path']);world=a.get_world();assert a.static_mesh_component.static_mesh.get_path_name().split('.')[0]==P['mesh_asset'];assert unreal.EditorAssetLibrary.get_metadata_tag(a.static_mesh_component.static_mesh,'ProjectJ.SourceHash')==hashlib.sha256((SOURCE/'StarterWorld.H08.mine.json').read_bytes()).hexdigest()
        assert not a.get_editor_property('replicates') and a.get_editor_property('is_spatially_loaded');body=a.static_mesh_component.static_mesh.get_editor_property('body_setup');assert body.get_editor_property('collision_trace_flag')==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
        floors=[];capsules=[];walls=[];ceilings=[]
        for u in range(-7,18):
            for v in [-1,0,1]:
                h=trace(world,pt(u,v,.9),pt(u,v,-1.5));assert h and h[0] and h[9]==a,('Missing mine floor',u,v);error=abs(h[4].z-pt(u,v,0).z);assert error<1,('Floor mismatch',u,v,error);assert h[7].z>.99;floors.append(dict(u=u,v=v,error_cm=error))
            cp=pt(u,0,1.11);h=unreal.SystemLibrary.capsule_trace_single(world,cp,cp+unreal.Vector(0,0,.1),42,96,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);assert not h or not h.to_tuple()[0],('Blocked gallery capsule',u,str(h));capsules.append(u)
            h=trace(world,pt(u,0,1.2),pt(u,0,6));assert h and h[0] and h[9]==a;ceilings.append(dict(u=u,height_m=(h[4].z-origin[2])/100-floor))
            for sign in [-1,1]:
                h=trace(world,pt(u,0,1.2),pt(u,sign*8,1.2));assert h and h[0] and h[9]==a,('Missing interior side wall',u,sign,str(h));walls.append(dict(u=u,sign=sign))
        h=trace(world,pt(16.7,0,1.2),pt(20,0,1.2));assert h and h[0] and h[9]==a,'Missing closed gallery end'
        stairs=[]
        for step in B['stairs']:
            h=trace(world,pt(step['local_u_m'],0,step['top_m']-floor+.5),pt(step['local_u_m'],0,step['bottom_m']-floor-.1));assert h and h[0] and h[9]==a;assert abs((h[4].z-origin[2])/100-step['top_m'])<.01;stairs.append(step)
        definition=next(v for v in actors if isinstance(v,unreal.Project_JWorldState)).get_editor_property('definition');assert [C.snapshot_place(v) for v in definition.places]==B['previous_places'];assert [C.snapshot_connection(v) for v in definition.connections]==B['previous_connections'];assert definition.source_plan_hash==B['source_plan_hash'];assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        R.update(passed=True,floor_checks=floors,headroom_capsules=capsules,roof_checks=ceilings,wall_checks=walls,end_closed=True,entry_steps=stairs,places_preserved=20,connections_preserved=7,height_sha256=T.manifest['terrain_profile']['sha256'],scope='Saved geometry/collision only; player traversal and entire H08 routes not yet approved')
    except Exception:R['error']=traceback.format_exc();unreal.log_error(R['error'])
    (OUT/'H08-mine-verification.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
