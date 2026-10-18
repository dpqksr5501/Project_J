"""Append only planned coastline reaches, one owned mesh actor per reach."""
import hashlib,importlib.util,json,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'coast-extension-build.json';assert not FILE.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
manifest=json.loads((SOURCE/'StarterRegion.manifest.json').read_text());profile=json.loads((SOURCE/manifest['coastal_surface_profile']).read_text());tasks=[q for q in profile['reaches'] if q['stage']=='PLANNED'];assert len(tasks)==8;C=module('contract','Starter-WorldDefinitionContract.py');W=module('water','Starter-WaterSurfaceAuthoring.py');S=module('settlement','Starter-SettlementAuthoring.py');TAG='ProjectJ.Starter.Coast.Extensions.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Hydrology/CoastalSurfaces',manifest);actors=A.subsystem.get_all_level_actors();assert not any(TAG in [str(t) for t in a.tags] for a in actors);definition=next(a for a in actors if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');R=dict(phase='QUEUED',owned_tag=TAG,previous_places=[C.snapshot_place(v) for v in definition.get_editor_property('places')],previous_connections=[C.snapshot_connection(v) for v in definition.get_editor_property('connections')],source_plan_hash=definition.get_editor_property('source_plan_hash'),reaches=[],art_complete=False,swimming_bound=False);index=0;busy=False
def record():R['actors']=A.created;FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def build(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):A.save();R['phase']='COMPLETE';record();unreal.unregister_slate_post_tick_callback(handle);return
        q=tasks[index];R['phase']='APPLYING_'+q['id'];record();file=SOURCE/q['geometry_source'];assert hashlib.sha256(file.read_bytes()).hexdigest()==q['geometry_sha256'];geo=json.loads(file.read_text());mesh=W.create_owned_mesh(dict(q,geometry_id=q['id']),geo,TAG,A.units);x,y,z=q['center_m'];a=A.spawn(q['id'].replace('.','_'),q['mesh_asset'],x,y,z,collision=False);a.set_actor_location(A.point(x,y,z),False,False);a.tags=[unreal.Name(TAG),unreal.Name(q['id']),unreal.Name('ProjectJ.Region.'+manifest['stable_region_id']),unreal.Name('Hydrology.SeaSurface')];a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);W.configure_surface_query(a.static_mesh_component);A.created[-1].update(location_cm=list(a.get_actor_location().to_tuple()));A.save();R['reaches'].append(dict(id=q['id'],actor_path=a.get_path_name(),mesh=mesh.get_path_name(),triangles=mesh.get_num_triangles(0),geometry_source=q['geometry_source'],geometry_sha256=q['geometry_sha256'],area_m2=geo['area_m2']));index+=1;R['phase']='READY';record()
    except Exception:R.update(phase='ERROR',error=traceback.format_exc());record();unreal.log_error(R['error']);unreal.unregister_slate_post_tick_callback(handle)
    finally:busy=False
record();handle=unreal.register_slate_post_tick_callback(build)
