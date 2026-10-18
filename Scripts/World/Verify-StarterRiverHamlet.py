"""Check saved river settlement, timber crossing and preserved native identities."""
import hashlib,importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';STATE=json.loads((OUT/'river-hamlet-build.json').read_text());REG=json.loads((OUT/'river-hamlet-registration.json').read_text());assert STATE['phase']==REG['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False)
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');snapshot=module('contract','Starter-WorldDefinitionContract.py').snapshot_place;origin=T.grid['unreal_origin_cm'];units=T.grid['centimetres_per_source_metre']
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);report=dict(passed=False,read_only=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();owned=[a for a in actors if 'ProjectJ.Starter.V4.RiverHamlet.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned};assert len(owned)==len(bylabel)==len(STATE['actors'])+len(STATE['entries'])==13
        for row in STATE['actors']+STATE['entries']:
            a=bylabel[row['label']];assert a.get_path_name()==row['path'] and (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor') and 'ProjectJ.Location.S07' in [str(t) for t in a.tags]
        def trace(a,p,half=10):
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,half),p-unreal.Vector(0,0,half),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0],(a.get_actor_label(),p);return values[4].z
        foundations=[];stairs=[];deck=[];ramps=[];access={h['label']:h for h in STATE['access']}
        for h in STATE['houses']:
            a=bylabel[h['label']];p=point(*h['center_m']);foundations.append(abs(trace(a,p)-p.z));c=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');assert c.get_instance_count()==access[h['label']]['instances'];b=c.static_mesh.get_bounds()
            for i in range(c.get_instance_count()):
                top=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z+b.box_extent.z));stairs.append(abs(trace(a,top,5)-top.z))
        bridge=STATE['bridge'];a=bylabel[bridge['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
        for name,n in bridge['components'].items():assert parts[name].get_instance_count()==n,(name,parts[name].get_instance_count(),n)
        c=parts['Deck'];b=c.static_mesh.get_bounds();n=c.get_instance_count()
        for i in [1,n//4,n//2,n*3//4,n-2]:
            for v in [-30,0,30]:
                top=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y+v,b.origin.z+b.box_extent.z));deck.append(abs(trace(a,top)-top.z))
        for ramp in bridge['ramps']:
            for i in range(1,6):
                t=i/6;p=point(*[ramp['start_m'][k]*(1-t)+ramp['end_m'][k]*t for k in range(3)]);ramps.append(abs(trace(a,p,20)-p.z))
        state=next(a for a in actors if isinstance(a,unreal.Project_JWorldState));definition=state.get_editor_property('definition');places=[snapshot(p) for p in definition.get_editor_property('places')];assert len(places)==19 and places[:18]==REG['previous_places'];new=places[-1];assert new['id']=='S07' and new['radius']==17000
        assert definition.get_editor_property('source_plan_hash')==hashlib.sha256((SOURCE/'StarterWorld.plan.json').read_bytes()).hexdigest()
        marker=next(a for a in actors if a.get_path_name()==REG['marker']['path']);assert (marker.get_actor_location()-unreal.Vector(*REG['marker']['location_cm'])).length()<.01 and not marker.get_editor_property('is_editor_only_actor')
        for row in STATE['entries']:assert not bylabel[row['label']].get_editor_property('replicates')
        assert len(bylabel['S07_River_Well'].get_components_by_class(unreal.StaticMeshComponent))==4
        assert max(foundations+stairs+deck+ramps)<.1
        report.update(passed=True,actors=13,buildings=8,bridge_span_m=110,bridge_deck_m=bridge['deck_top_m'],stairs_threshold_instances=len(stairs),bridge_collision_samples=len(deck),ramp_collision_samples=len(ramps),maximum_collision_error_cm=max(foundations+stairs+deck+ramps),native_place_count=19,old_places_preserved=True,marker_saved=True,height_sha256=T.manifest['terrain_profile']['sha256'],art_complete=False,navigation_verified=False,mount_verified=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
    (OUT/'river-hamlet-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
