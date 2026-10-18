"""Verify the full added south-coast band and each neighboring seam on reload."""
import hashlib,importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';B=json.loads((OUT/'coast-extension-build.json').read_text());FIRST=json.loads((OUT/'H07-coastal-reach-build.json').read_text());assert B['phase']==FIRST['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False)
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');C=module('contract','Starter-WorldDefinitionContract.py');profile=json.loads((SOURCE/T.manifest['coastal_surface_profile']).read_text());origin=T.grid['unreal_origin_cm'];units=T.grid['centimetres_per_source_metre']
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def intervals(geometry,x):
    segments=geometry['boundary_segments_m'];pieces=sorted([sorted([a[1],b[1]]) for a,b in segments if abs(a[0]-x)<1e-6 and abs(b[0]-x)<1e-6]);merged=[]
    for a,b in pieces:
        if merged and a<=merged[-1][1]+1e-6:merged[-1][1]=max(b,merged[-1][1])
        else:merged.append([a,b])
    return merged
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);R=dict(passed=False,read_only=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();paths={a.get_path_name():a for a in actors};world=unreal.EditorLevelLibrary.get_editor_world();definition=next(a for a in actors if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');assert [C.snapshot_place(v) for v in definition.get_editor_property('places')]==B['previous_places'];assert [C.snapshot_connection(v) for v in definition.get_editor_property('connections')]==B['previous_connections'];assert definition.get_editor_property('source_plan_hash')==B['source_plan_hash'];sea=paths[FIRST['base_sea_snapshot']['actor_path']];assert list(sea.get_actor_location().to_tuple())==FIRST['base_sea_snapshot']['location_cm'] and list(sea.get_actor_scale3d().to_tuple())==FIRST['base_sea_snapshot']['scale'];rows=[];geometry_by_id={};allwater=[paths[v['actor_path']] for v in B['reaches']]+[paths[FIRST['actors'][0]['path']],sea]
        for q in sorted(profile['reaches'],key=lambda v:v['clip_bounds_m'][0]):
            file=SOURCE/q['geometry_source'];assert hashlib.sha256(file.read_bytes()).hexdigest()==q['geometry_sha256'];g=json.loads(file.read_text());geometry_by_id[q['id']]=g;row=next((v for v in B['reaches'] if v['id']==q['id']),None);a=paths[row['actor_path']] if row else paths[FIRST['actors'][0]['path']];c=a.static_mesh_component;m=c.static_mesh;assert m.get_num_triangles(0)==len(g['triangles']);assert unreal.EditorAssetLibrary.get_metadata_tag(m,'ProjectJ.SourceHash')==q['geometry_sha256'];assert c.get_collision_enabled()==unreal.CollisionEnabled.QUERY_ONLY and c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)==unreal.CollisionResponseType.ECR_IGNORE;center,extent=a.get_actor_bounds(False);assert abs((center.y-extent.y-origin[1])/units-500)<.001 and abs(center.z-origin[2])<.001;errors=[];wet=0
            for first,last in g['shore_segments_m']:
                x,y=(first[0]+last[0])/2,(first[1]+last[1])/2;p=point(x,y,0);hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,150),p-unreal.Vector(0,0,150),unreal.TraceTypeQuery.ECC_VISIBILITY,True,allwater,unreal.DrawDebugTrace.NONE,True);v=hit.to_tuple() if hit else None;assert v and v[0],('Coast ground missing',q['id'],x,y);error=abs(v[4].z-p.z);assert error<10,('Coast shore mismatch',q['id'],x,y,error);errors.append(error)
            for indices in g['triangles'][::max(1,len(g['triangles'])//40)]:
                x,y=[sum(g['vertices_m'][i][axis] for i in indices)/3 for axis in [0,1]]
                if T.height(x,y)>-.3:continue
                p=point(x,y,0);hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,20),p-unreal.Vector(0,0,20),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);v=hit.to_tuple() if hit else None;assert v and v[0] and v[10]==c and abs(v[4].z-p.z)<.1,('Coast wet query missing',q['id'],x,y);wet+=1
            assert wet>20;rows.append(dict(id=q['id'],triangles=m.get_num_triangles(0),shore_samples=len(errors),maximum_shore_ground_error_cm=max(errors),wet_queries=wet))
        ordered=sorted(profile['reaches'],key=lambda v:v['clip_bounds_m'][0]);seams=[];assert ordered[0]['clip_bounds_m'][0]==0 and ordered[-1]['clip_bounds_m'][2]==8064
        for a,b in zip(ordered,ordered[1:]):
            x=a['clip_bounds_m'][2];assert x==b['clip_bounds_m'][0];left=intervals(geometry_by_id[a['id']],x);right=intervals(geometry_by_id[b['id']],x);assert len(left)==len(right) and all(abs(p-q)<1e-5 for v,w in zip(left,right) for p,q in zip(v,w)),('Sea seam footprint mismatch',a['id'],b['id'],left,right);seams.append(dict(x_m=x,wet_intervals_m=left,height_difference_cm=0))
        dirty=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()];assert not dirty;R.update(passed=True,saved_reload=True,reaches=rows,neighbor_seams=seams,base_sea_preserved=True,all20_places_7_connections_preserved=True,height_sha256=T.manifest['terrain_profile']['sha256'],dirty_maps=dirty,scope='Added ocean-connected south coastline only; river ribbons/shore art/waves/swimming/boats and gameplay unfinished')
    except Exception:R['error']=traceback.format_exc();unreal.log_error(R['error'])
    (OUT/'coast-extension-saved-verification.json').write_text(json.dumps(R,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
