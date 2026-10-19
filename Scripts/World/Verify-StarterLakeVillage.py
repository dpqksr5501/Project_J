"""Read-only saved dry-bank, pier, ramp, water and exterior access checks."""
import hashlib,importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';SOURCE=ROOT/'Content/World/MainWorld/Source';STATE=json.loads((OUT/'lake-village-build.json').read_text(encoding='utf-8'));assert STATE['phase']=='COMPLETE'
manifest=json.loads((SOURCE/'MainWorld.manifest.json').read_text(encoding='utf-8'));grid=manifest['world_grid'];origin=grid['unreal_origin_cm'];units=grid['centimetres_per_source_metre']
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);report=dict(passed=False,read_only=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();owned=[a for a in actors if 'ProjectJ.Starter.V4.Lake.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned}
        assert len(owned)==len(bylabel)==len(STATE['actors'])+len(STATE['entries'])==20
        for row in STATE['actors']+STATE['entries']:
            a=bylabel[row['label']];assert a.get_path_name()==row['path'] and (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor')
            assert {'ProjectJ.Location.S03','ProjectJ.Target.S04'}<={str(t) for t in a.tags}
        def trace(a,p,half=10):
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,half),p-unreal.Vector(0,0,half),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True)
            values=hit.to_tuple() if hit else None;assert values and values[0],(a.get_actor_label(),p);return values[4].z
        foundations=[];steps=[];access={h['label']:h for h in STATE['access']};assert not STATE['pending_access']
        for h in STATE['houses']:
            a=bylabel[h['label']];x,y,z=h['center_m'];p=point(x,y,z);foundations.append(abs(trace(a,p)-p.z))
            c=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');assert c.get_instance_count()==access[h['label']]['instances'];b=c.static_mesh.get_bounds()
            for i in range(c.get_instance_count()):
                top=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z+b.box_extent.z));steps.append(abs(trace(a,top,5)-top.z))
        dock=STATE['dock'];a=bylabel[dock['label']];components={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
        for name,n in dock['components'].items():assert components[name].get_instance_count()==n
        assert components['Underframe'].get_instance_count()==dock['structure']['instances']==15
        ramp=[];deck=[];x=dock['ramp_end_m'][0];start=dock['ramp_start_m'];end=dock['ramp_end_m']
        for i in range(1,10):
            t=i/10;y=start[1]+(end[1]-start[1])*t;z=start[2]+(end[2]-start[2])*t;p=point(x,y,z);ramp.append(abs(trace(a,p,30)-p.z))
        for y in [end[1]-i*8-.125 for i in range(5)]:
            p=point(x,y,dock['deck_top_m']);deck.append(abs(trace(a,p)-p.z))
        for dx in [-9,-4,4,9]:
            p=point(x+dx,dock['head_m'][1]+.125,dock['deck_top_m']);deck.append(abs(trace(a,p)-p.z))
        lake=next(a for a in actors if a.get_actor_label()=='MainWorld_LakePrototype');center,extent=lake.get_actor_bounds(False,False);assert abs(center.z+extent.z-(origin[2]+dock['water_level_m']*units))<.01
        for row in STATE['entries']:assert not bylabel[row['label']].get_editor_property('replicates')
        digest=hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest();assert digest==STATE['height_sha256']
        assert max(foundations+steps+deck+ramp)<.1,(max(foundations),max(steps),max(deck),max(ramp))
        report.update(passed=True,actors=20,buildings=10,exterior_accesses=len(access),stairs_threshold_instances=len(steps),deck_collision_samples=len(deck),ramp_collision_samples=len(ramp),ramp_grade=dock['ramp_grade'],maximum_foundation_collision_error_cm=max(foundations),maximum_stair_collision_error_cm=max(steps),maximum_deck_collision_error_cm=max(deck),maximum_ramp_collision_error_cm=max(ramp),water_surface_m=(center.z+extent.z)/100,height_sha256=digest,water_navigation_or_swimming_verified=False,art_complete=False,navigation_verified=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
    (OUT/'lake-village-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
