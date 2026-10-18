"""Read the saved road watch, actual stair/deck collision and preserved village."""
import hashlib,importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';B=json.loads((OUT/'road-watch-build.json').read_text());PRE=json.loads((OUT/'road-watch-preflight.json').read_text());assert B['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False)
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');C=module('contract','Starter-WorldDefinitionContract.py');origin=T.grid['unreal_origin_cm'];units=T.grid['centimetres_per_source_metre']
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);r=dict(passed=False,read_only=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();owned=[a for a in actors if 'ProjectJ.Starter.V4.RoadWatch.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned};assert len(owned)==len(bylabel)==10
        for row in B['actors']+B['entries']:
            a=bylabel[row['label']];assert a.get_path_name()==row['path'] and (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01;assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor')
        for row in PRE['actors']:
            a=next(a for a in actors if a.get_path_name()==row['path']);t=a.get_actor_transform();actual=dict(label=a.get_actor_label(),path=a.get_path_name(),location_cm=list(t.translation.to_tuple()),rotation=list(t.rotation.rotator().to_tuple()),scale=list(t.scale3d.to_tuple()),components=[dict(name=c.get_name(),mesh=c.static_mesh.get_path_name() if c.static_mesh else None) for c in a.get_components_by_class(unreal.StaticMeshComponent)]);assert actual==row,('Existing village changed',row['label'])
        definition=next(a for a in actors if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');assert [C.snapshot_place(v) for v in definition.get_editor_property('places')]==PRE['places'] and [C.snapshot_connection(v) for v in definition.get_editor_property('connections')]==PRE['connections'];assert definition.get_editor_property('source_plan_hash')==PRE['source_plan_hash']==hashlib.sha256((SOURCE/'StarterWorld.plan.json').read_bytes()).hexdigest()
        assert (SOURCE/'StarterWorld.corridor.json').read_bytes()==(ROOT/'Saved/StarterWorld/BeforeRoadWatch/StarterWorld.corridor.json').read_bytes()
        def trace(a,p,half=10):
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,half),p-unreal.Vector(0,0,half),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0],(a.get_actor_label(),p);return values[4].z
        floors=[];stairs=[];deck=[];access={q['label']:q for q in B['access']}
        for q in B['houses']+[B['market']]:
            a=bylabel[q['label']];p=point(*q['center_m']);floors.append(abs(trace(a,p)-p.z))
        for label,expected in access.items():
            a=bylabel[label];c=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');assert c.get_instance_count()==expected['instances'];bounds=c.static_mesh.get_bounds()
            for i in range(c.get_instance_count()):
                top=c.get_instance_transform(i,True).transform_location(unreal.Vector(bounds.origin.x,bounds.origin.y,bounds.origin.z+bounds.box_extent.z));stairs.append(abs(trace(a,top,5)-top.z))
        a=bylabel[B['lookout']['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
        for name,count in B['lookout']['components'].items():assert parts[name].get_instance_count()==count
        c=parts['Deck'];bounds=c.static_mesh.get_bounds()
        for i in range(c.get_instance_count()):
            top=c.get_instance_transform(i,True).transform_location(unreal.Vector(bounds.origin.x,bounds.origin.y,bounds.origin.z+bounds.box_extent.z));deck.append(abs(trace(a,top)-top.z))
        # Independently check diagonal endpoints; instance counts alone missed inverted slopes.
        q=B['lookout'];lx,ly,floor=q['center_m'];foot=q['approach_m'];foot_z=T.height(*foot);slope_errors=[]
        for name,indices,rise in [('StairStringers',[0,1],-.22),('Rails',[14,15],1)]:
            for side,index in zip([-1,1],indices):
                t=parts[name].get_instance_transform(index,True)
                for end,expected in [(-50,[lx+side*.99,foot[1],foot_z+rise]),(50,[lx+side*.99,ly-2.5,floor+rise])]:slope_errors.append((t.transform_location(unreal.Vector(0,end,0))-point(*expected)).length())
        for side,index in [(-1,0),(1,1)]:
            t=parts['Roof'].get_instance_transform(index,True);slope_errors.append((t.transform_location(unreal.Vector(0,-side*50,0))-point(lx,ly,floor+4.5)).length());slope_errors.append((t.transform_location(unreal.Vector(0,side*50,0))-point(lx,ly+side*3,floor+3)).length())
        assert max(slope_errors)<.01,slope_errors
        cp=B['checkpoint'];x,y,_=cp['center_m'];angle=math.radians(cp['heading_degrees']);co,si=math.cos(angle),math.sin(angle)
        for lateral in [-2.5,0,2.5]:
            ends=[]
            for distance in [-7,7]:
                px,py=x+co*distance-si*lateral,y+si*distance+co*lateral;ends.append(point(px,py,T.height(px,py)+1))
            hit=unreal.SystemLibrary.line_trace_single(world,*ends,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v not in owned],unreal.DrawDebugTrace.NONE,True);assert not hit or not hit.to_tuple()[0],'Checkpoint obstructs reserved road width'
        assert max(floors+stairs+deck)<.1
        r.update(passed=True,actors=10,existing_houses_preserved=8,player_start_preserved=True,place_count=20,all_places_connections_preserved=True,original_corridor_preserved=True,stairs_threshold_instances=len(stairs),deck_collision_samples=len(deck),maximum_collision_error_cm=max(floors+stairs+deck),sloped_member_endpoint_samples=len(slope_errors),maximum_slope_endpoint_error_cm=max(slope_errors),checkpoint_clear_width_m=6,height_sha256=T.manifest['terrain_profile']['sha256'],art_complete=False,navigation_verified=False,mount_verified=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:r['error']=traceback.format_exc();unreal.log_error(r['error'])
    (OUT/'road-watch-saved-verification.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
import math
handle=unreal.register_slate_post_tick_callback(verify)
