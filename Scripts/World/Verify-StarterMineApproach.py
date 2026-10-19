"""Saved path treads, exact guardrail transforms and native character clearance."""
import hashlib,importlib.util,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';P=json.loads((SOURCE/'MainWorld.H08.approach.json').read_text());B=json.loads((OUT/'H08-mine-approach-build.json').read_text());assert B['phase']=='COMPLETE'
assert hashlib.sha256((SOURCE/'MainWorld.H08.approach.json').read_bytes()).hexdigest()==B['source_sha256'],'Native build is from a different authored path'
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
S=module('support','Starter-SettlementSupport.py').AuthoredPathSupport(P['pieces']);C=module('contract','Starter-WorldDefinitionContract.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');grid=T.manifest['world_grid'];origin=grid['unreal_origin_cm']
def point(x,y,z):return unreal.Vector(origin[0]+x*100,origin[1]+y*100,origin[2]+z*100)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);R=dict(passed=False,read_only=True,terrain_modified=False,art_complete=False)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();a=next(a for a in actors if a.get_path_name()==B['actor_path']);world=a.get_world();assert not a.get_editor_property('replicates') and a.get_editor_property('is_spatially_loaded');parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)};treads=parts['StoneTreads'];rails=parts['TimberGuardrails'];assert treads.get_instance_count()==len(P['pieces'])==B['treads'];assert rails.get_instance_count()==len(B['guardrail_beams'])
        floor_checks=[];capsules=[]
        for p in P['pieces']:
            angle=math.radians(p['yaw_degrees']);dx,dy=math.cos(angle),math.sin(angle)
            for v in [-.65,0,.65]:
                x,y=p['center_xy_m'][0]-dy*v,p['center_xy_m'][1]+dx*v;z=S.top(x,y);assert z is not None;h=unreal.SystemLibrary.line_trace_single(world,point(x,y,z+.08),point(x,y,z-.6),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);h=h.to_tuple() if h else None;assert h and h[0] and h[10]==treads,('Missing tread',p['index'],v,str(h));error=abs((h[4].z-origin[2])/100-z);assert error<.01 and h[7].z>.99;assert T.height(x,y)<z;floor_checks.append(dict(index=p['index'],offset_m=v,error_cm=error*100))
            if p['index']%3==0:
                x,y=p['center_xy_m'];z=max(S.top(x+u,y+v) or T.height(x+u,y+v) for u,v in [(0,0),(.42,0),(-.42,0),(0,.42),(0,-.42),(.3,.3),(-.3,.3),(.3,-.3),(-.3,-.3)])
                cp=point(x,y,z+1.11);h=unreal.SystemLibrary.capsule_trace_single(world,cp,cp+unreal.Vector(0,0,.1),42,96,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);assert not h or not h.to_tuple()[0],('Stair landing headroom blocked',p['index'],str(h));capsules.append(p['index'])
        bounds=rails.static_mesh.get_bounds();rail_errors=[]
        for i,b in enumerate(B['guardrail_beams']):
            tr=rails.get_instance_transform(i,True);ends=[tr.transform_location(bounds.origin+unreal.Vector(0,0,s*bounds.box_extent.z)) for s in [-1,1]];wanted=[point(*b[k]) for k in ['start_m','end_m']];error=min(max((ends[j]-wanted[j]).length() for j in [0,1]),max((ends[j]-wanted[1-j]).length() for j in [0,1]));assert error<1,('Guardrail end mismatch',i,error);rail_errors.append(error)
        definition=next(v for v in actors if isinstance(v,unreal.Project_JWorldState)).get_editor_property('definition');assert [C.snapshot_place(v) for v in definition.places]==B['previous_places'];assert [C.snapshot_connection(v) for v in definition.connections]==B['previous_connections'];assert definition.source_plan_hash==B['source_plan_hash'];assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
        R.update(passed=True,tread_floor_checks=floor_checks,maximum_floor_error_cm=max(v['error_cm'] for v in floor_checks),stair_landing_headroom_capsules=capsules,guardrail_endpoint_checks=len(rail_errors),maximum_guardrail_endpoint_error_cm=max(rail_errors,default=0),places_preserved=20,connections_preserved=7,height_sha256=T.manifest['terrain_profile']['sha256'],scope='Geometry and stair landing clearance; actual movement, full H08 combat/mount/Nav and art approval are separate')
    except Exception:R['error']=traceback.format_exc();unreal.log_error(R['error'])
    (OUT/'H08-mine-approach-verification.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
