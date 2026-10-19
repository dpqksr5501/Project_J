"""Independent saved mountain settlement collision, identity and height checks."""
import importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';STATE=json.loads((OUT/'mountain-village-build.json').read_text(encoding='utf-8'));assert STATE['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False)
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Starter-TerrainProfile.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);T=m.TerrainProfile(SOURCE/'MainWorld.manifest.json');origin=T.grid['unreal_origin_cm'];units=T.grid['centimetres_per_source_metre']
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);report=dict(passed=False,read_only=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();owned=[a for a in actors if 'ProjectJ.Starter.V4.Mountain.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned};assert len(owned)==len(bylabel)==len(STATE['actors'])+len(STATE['entries'])==17
        for row in STATE['actors']+STATE['entries']:
            a=bylabel[row['label']];assert a.get_path_name()==row['path'] and (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor') and {'ProjectJ.Location.S05','ProjectJ.Target.S06'}<={str(t) for t in a.tags}
        def trace(a,p,half=10):
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,half),p-unreal.Vector(0,0,half),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0],(a.get_actor_label(),p);return values[4].z
        foundations=[];steps=[];deck=[];ground=[];access={h['label']:h for h in STATE['access']}
        def check_stairs(a,record):
            c=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');assert c.get_instance_count()==record['instances'];b=c.static_mesh.get_bounds()
            for i in range(c.get_instance_count()):
                top=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z+b.box_extent.z));steps.append(abs(trace(a,top,5)-top.z))
        for h in STATE['houses']:
            a=bylabel[h['label']];p=point(*h['center_m']);foundations.append(abs(trace(a,p)-p.z));check_stairs(a,access[h['label']])
            parts={c.get_name():c for c in a.get_components_by_class(unreal.StaticMeshComponent)};assert {'HipRoof','GableNorth','GableSouth','Foundation'}<=parts.keys()
        p=STATE['lookout'];a=bylabel[p['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
        for name,count in p['components'].items():assert parts[name].get_instance_count()==count
        c=parts['Planks'];b=c.static_mesh.get_bounds()
        for i in range(c.get_instance_count()):
            top=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z+b.box_extent.z));deck.append(abs(trace(a,top)-top.z))
        check_stairs(a,p['access'])
        yard=STATE['workyard'];a=bylabel[yard['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
        for name,count in yard['components'].items():
            c=parts[name];assert c.get_instance_count()==count;b=c.static_mesh.get_bounds()
            for i in [0,count//2,count-1]:
                foot=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z-b.box_extent.z));ground.append(abs(foot.z-(origin[2]+T.height((foot.x-origin[0])/units,(foot.y-origin[1])/units)*units)))
        state=next(a for a in actors if isinstance(a,unreal.Project_JWorldState));places=state.get_editor_property('definition').get_editor_property('places');plan=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'));assert {str(p.get_editor_property('place_id')) for p in places}=={p['id'] for p in plan['places']};legacy=next(p for p in places if str(p.get_editor_property('place_id'))=='S05');assert (legacy.get_editor_property('location')-point(2600,6000,580.75)).length()<.01
        for row in STATE['entries']:assert not bylabel[row['label']].get_editor_property('replicates')
        assert max(foundations+steps+deck+ground)<.1
        floors=[h['center_m'][2] for h in STATE['houses']];report.update(passed=True,actors=17,buildings=11,lookouts=1,exterior_accesses=12,stairs_threshold_instances=len(steps),lookout_floor_m=p['center_m'][2],house_floor_range_m=[min(floors),max(floors)],lookout_deck_collision_samples=len(deck),maximum_collision_or_ground_error_cm=max(foundations+steps+deck+ground),height_sha256=T.manifest['terrain_profile']['sha256'],native_place_count=len(places),original_place_identity_preserved=True,art_complete=False,navigation_verified=False,mount_verified=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
    (OUT/'mountain-village-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
