"""Read saved north camp collision and its shared edge identity."""
import hashlib,importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';B=json.loads((OUT/'north-camp-build.json').read_text());REG=json.loads((OUT/'north-camp-registration.json').read_text());assert B['phase']==REG['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False)
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');C=module('contract','Starter-WorldDefinitionContract.py');origin=T.grid['unreal_origin_cm'];units=T.grid['centimetres_per_source_metre']
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);r=dict(passed=False,read_only=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();owned=[a for a in actors if 'ProjectJ.Starter.V4.NorthCamp.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned};assert len(owned)==len(bylabel)==len(B['actors'])+len(B['entries'])+1
        for row in B['actors']+B['entries']+[B['port_marker']]:
            a=bylabel[row['label']];assert a.get_path_name()==row['path'] and (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor')
        def trace(a,p,half=10):
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,half),p-unreal.Vector(0,0,half),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0],(a.get_actor_label(),p);return values[4].z
        floors=[];stairs=[];access={h['label']:h for h in B['access']}
        for q in B['houses']+B['tents']+B['towers']:
            a=bylabel[q['label']];p=point(*q['center_m']);floors.append(abs(trace(a,p)-p.z))
            if q in B['towers']:continue
            c=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');assert c.get_instance_count()==access[q['label']]['instances'];bounds=c.static_mesh.get_bounds()
            for i in range(c.get_instance_count()):
                top=c.get_instance_transform(i,True).transform_location(unreal.Vector(bounds.origin.x,bounds.origin.y,bounds.origin.z+bounds.box_extent.z));stairs.append(abs(trace(a,top,5)-top.z))
        definition=next(a for a in actors if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');places=[C.snapshot_place(p) for p in definition.get_editor_property('places')];assert len(places)==20 and places[:19]==REG['previous_places'] and places[-1]['id']=='S08'
        connections=[C.snapshot_connection(v) for v in definition.get_editor_property('connections')];assert len(connections)==7
        assert [v for v in connections if v['id']!='E_N_Road']==[v for v in REG['previous_connections'] if v['id']!='E_N_Road'];connection=next(v for v in connections if v['id']=='E_N_Road');assert connection==REG['connection']
        plan=json.loads((SOURCE/'MainWorld.plan.json').read_text());edges=json.loads((SOURCE/'MainWorld.edges.json').read_text());port=next(v for v in plan['connections'] if v['id']=='E_N_Road');assert port==next(v for v in edges['connections'] if v['id']=='E_N_Road');assert list(point(*port['position_m']).to_tuple())==connection['location_cm']==B['port_marker']['location_cm']
        oldedges=json.loads((ROOT/'Saved/StarterWorld/BeforeNorthCamp/MainWorld.edges.json').read_text());assert edges['edges']==oldedges['edges']
        assert definition.get_editor_property('source_plan_hash')==hashlib.sha256((SOURCE/'MainWorld.plan.json').read_bytes()).hexdigest()
        assert all(any(a.get_path_name()==row['path'] for a in actors) for row in REG['review']);assert max(floors+stairs)<.1
        gate=B['gate'];x,y,z=gate['passage_center_m'];assert gate['lintel_bottom_m']-z>5
        # No gate mesh blocks the capsule corridor below the lintel.
        gateactors=[bylabel[v['label']] for v in B['towers']]+[bylabel[gate['label']]]
        for u in [-2,0,2]:
            a,b=point(x+u,y-6,T.height(x+u,y-6)+1),point(x+u,y+6,T.height(x+u,y+6)+1)
            hit=unreal.SystemLibrary.line_trace_single(world,a,b,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v not in gateactors],unreal.DrawDebugTrace.NONE,True);assert not hit or not hit.to_tuple()[0],'Blocked gateway passage'
        if 'sign' in B:
            a=bylabel[B['sign']['label']];assert len(a.get_components_by_class(unreal.TextRenderComponent))==1
        r.update(passed=True,actors=len(owned),buildings=6,towers=2,stairs_threshold_instances=len(stairs),maximum_collision_error_cm=max(floors+stairs),native_place_count=20,old_places_preserved=True,other_connections_preserved=True,port_sources_match=True,edge_height_arrays_preserved=True,height_sha256=T.manifest['terrain_profile']['sha256'],art_complete=False,navigation_verified=False,mount_verified=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:r['error']=traceback.format_exc();unreal.log_error(r['error'])
    (OUT/'north-camp-saved-verification.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
