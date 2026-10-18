"""Read-only saved corridor checks. Run after an independent map reload.

No authoring, lighting preparation, regeneration, weather changes or editor exit.
Loading WP actors here is an inspection scope, not runtime residency policy.
"""
import array,collections,datetime,hashlib,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld'
PLAN=json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'))
LAYOUT=json.loads((SOURCE/'StarterWorld.corridor.json').read_text(encoding='utf-8'))
BUILD=json.loads((OUT/'corridor-build.json').read_text(encoding='utf-8'))
BIOMES=json.loads((SOURCE/'StarterWorld.biomes.json').read_text(encoding='utf-8')) if (SOURCE/'StarterWorld.biomes.json').exists() else {}
RETIRED_GRASS_GROUPS=BIOMES.get('retired_legacy_grass_actors',0)
TAG='ProjectJ.Starter.Corridor.v1';MAP='/Game/World/Starter/Maps/L_StarterRegion'
world=unreal.EditorLevelLibrary.get_editor_world();assert world.get_path_name().startswith(MAP+'.')
subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
ordinary_loaded=subsystem.get_all_level_actors()
ordinary_labels=[a.get_actor_label() for a in ordinary_loaded]
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
raw=array.array('H')
with (SOURCE/'HM_StarterRegion.r16').open('rb') as stream:raw.fromfile(stream,4033**2)
def height(x,y):
    u=x/2;v=y/2;i,j=int(u),int(v);s,t=u-i,v-j
    q=(raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t
    return (q-32768)*6.25
started=time.monotonic()
unreal.EditorPythonScripting.set_keep_python_script_alive(True)


def verify(dt):
    if time.monotonic()-started<12:return
    unreal.unregister_slate_post_tick_callback(handle)
    report={'passed':False,'map':MAP,'verified_at_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'authoring_repairs_executed':False}
    try:
        actors=subsystem.get_all_level_actors();by_label={a.get_actor_label():a for a in actors}
        owned=[a for a in actors if TAG in [str(t) for t in a.get_editor_property('tags')]]
        assert len(owned)==390-RETIRED_GRASS_GROUPS,'Unexpected duplicate or missing corridor actors: '+str(len(owned))
        owned_labels=collections.Counter(a.get_actor_label() for a in owned)
        assert all(count==1 for count in owned_labels.values()),'Duplicate authored labels'
        states=[a for a in actors if isinstance(a,unreal.Project_JWorldState)]
        envs=[a for a in actors if isinstance(a,unreal.Project_JWorldEnvironment)]
        assert len(states)==len(envs)==1
        assert envs[0].get_editor_property('state')==states[0]
        sun=envs[0].get_editor_property('sun');lux=sun.get_component_by_class(unreal.DirectionalLightComponent).get_editor_property('intensity')
        assert lux==60000,lux
        definition=states[0].get_editor_property('definition')
        assert definition.get_editor_property('source_plan_hash')==hashlib.sha256((SOURCE/'StarterWorld.plan.json').read_bytes()).hexdigest()
        places=definition.get_editor_property('places');assert len(places)==len(PLAN['places'])
        actual={str(p.get_editor_property('place_id')):p for p in places};assert set(actual)=={p['id'] for p in PLAN['places']}
        for p in PLAN['places']:
            entry=actual[p['id']];x,y,z=p['center_m']
            assert (entry.get_editor_property('location')-unreal.Vector((x-4032)*100,(y-4032)*100,z*100)).length()<1
            assert entry.get_editor_property('recommended_min_level')==p['levels'][0]
            assert entry.get_editor_property('recommended_max_level')==p['levels'][1]
        assert len(definition.get_editor_property('connections'))==7
        assert unreal.Project_JWorldAuthoringLibrary.get_region_runtime_hash_name()=='WorldPartitionRuntimeHashSet'
        proxies=[a for a in actors if isinstance(a,unreal.LandscapeProxy)]
        component_count=sum(len(a.get_components_by_class(unreal.LandscapeComponent)) for a in proxies);assert component_count==1024
        ignore=[a for a in actors if not isinstance(a,unreal.LandscapeProxy)]
        sample_points=[(p['id'],*p['center_m'][:2]) for p in PLAN['places']]+[(h['id'],h['x'],h['y']) for h in LAYOUT['houses']]
        errors=[];missing=[]
        for label,x,y in sample_points:
            wx,wy=(x-4032)*100,(y-4032)*100
            hit=unreal.SystemLibrary.line_trace_single(world,unreal.Vector(wx,wy,200000),unreal.Vector(wx,wy,-15000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE,True)
            values=hit.to_tuple() if hit else None
            assert values and values[0],'Missing landscape collision '+label
            errors.append(abs(values[4].z-height(x,y)))
        assert max(errors)<100,errors
        starts=[a for a in actors if isinstance(a,unreal.PlayerStart)];assert len(starts)==1
        x,y,z=next(p for p in PLAN['places'] if p['id']=='S00')['center_m']
        assert (starts[0].get_actor_location()-unreal.Vector((x-4032)*100,(y-4032)*100,(z+1.3)*100)).length()<1
        assert not starts[0].get_editor_property('is_spatially_loaded')
        assert abs(starts[0].get_actor_rotation().yaw-LAYOUT.get('player_start_yaw',125))<.01
        ordinary_house_labels=[]
        for h in LAYOUT['houses']:
            label='S00_'+h['id'];a=by_label[label];r=a.get_actor_rotation()
            assert abs(r.pitch)<.01 and abs(r.roll)<.01,(label,str(r))
            center,extent=a.get_actor_bounds(False)
            assert math.hypot(center.x-(h['x']-4032)*100,center.y-(h['y']-4032)*100)<25,label
            # Actor bounds include the editor's 128 cm billboard below the root.
            # Evaluate the actual upright mesh geometry when checking grounding.
            mesh_components=[c for c in a.get_components_by_class(unreal.StaticMeshComponent) if c.static_mesh]
            lowest=min(c.get_world_location().z+(c.static_mesh.get_bounds().origin.z-c.static_mesh.get_bounds().box_extent.z)*c.get_world_transform().scale3d.z for c in mesh_components)
            assert abs(lowest-h['ground_m']*100)<55,(label,lowest,h['ground_m']*100)
            assert a.get_editor_property('is_spatially_loaded'),label
            if h.get('roof_style')=='intact_thatch_gable':
                parts={c.get_name():c for c in mesh_components}
                assert parts['HipRoof'].static_mesh.get_path_name().split('.')[0]=='/Game/Meshes/Houses/Roofs/House02_Roof/SM_House02_Roof'
                assert {'GableNorth','GableSouth'}<=parts.keys()
                assert all(parts[n].static_mesh.get_path_name().split('.')[0]=='/Game/ElderboomVillage/Meshes/SM_Medieval_Modular_Gable_udsreiudw_lod3_Var1' for n in ['GableNorth','GableSouth'])
            if label in ordinary_labels:ordinary_house_labels.append(label)
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                rr=c.get_world_rotation();assert abs(rr.pitch)<.01 and abs(rr.roll)<.01,(label,c.get_name(),str(rr))
                if c.get_name().startswith(('Front','Rear','Side','Upper','Gable')):
                    transform=c.get_world_transform();normal=transform.rotation.rotate_vector(unreal.Vector(0,1,0))
                    local=c.static_mesh.get_bounds().origin;center=c.get_world_location()+transform.rotation.rotate_vector(local*transform.scale3d)
                    outward=center-a.get_actor_location()
                    assert normal.dot(outward)>25,(label,c.get_name(),'Facade faces inward')
        assert len(ordinary_house_labels)==8,ordinary_house_labels
        counts=collections.Counter();groups=0;checked_instances=0
        for a in owned:
            for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent):
                n=c.get_instance_count();groups+=1
                kind=c.get_name().removesuffix('Instances');counts[kind]+=n
                assert a.get_editor_property('is_spatially_loaded')
                for i in sorted(set([0,n//2,n-1])):
                    transform=c.get_instance_transform(i,True);r=transform.rotation.rotator()
                    assert abs(r.pitch)<.01 and abs(r.roll)<.01,(a.get_actor_label(),str(r))
                    assert transform.translation.z>-1000
                    checked_instances+=1
        assert groups==BUILD['counts']['nature_spatial_groups']-RETIRED_GRASS_GROUPS,groups
        for kind in ['Tree','Rock','Grass','Wheat']:
            expected=0 if kind=='Grass' and RETIRED_GRASS_GROUPS else BUILD['counts'][kind]
            assert counts[kind]==expected,(kind,counts[kind])
        well=by_label['S00_PlazaWell']
        assert len(well.get_components_by_class(unreal.StaticMeshComponent))==4,'Incomplete well assembly'
        assert all(by_label['S01_'+n].get_editor_property('is_spatially_loaded') for n in ['CapitalGate','WestWatchtower','EastWatchtower','WestCurtain','EastCurtain'])
        bridge=by_label['S01_LakeOutStoneBridge'];assert bridge.get_editor_property('is_spatially_loaded')
        bridge_mesh=bridge.static_mesh_component.static_mesh
        assert bridge_mesh.get_num_triangles(0)==8856
        assert unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem).get_collision_complexity(bridge_mesh)==unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE
        bridge_data=BUILD['bridge'];bx,by=bridge_data['center_m'];a=math.radians(bridge_data['heading']);dx,dy=math.cos(a),math.sin(a)
        bridge_ignore=[v for v in actors if v!=bridge];bridge_errors=[]
        for sample in bridge_data['deck_samples']:
            t=sample['distance_m'];p=unreal.Vector((bx+dx*t-4032)*100,(by+dy*t-4032)*100,0)
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,200000),p-unreal.Vector(0,0,10000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,bridge_ignore,unreal.DrawDebugTrace.NONE,True)
            values=hit.to_tuple() if hit else None;assert values and values[0],'Saved bridge deck collision missing'
            bridge_errors.append(abs(values[4].z-sample['deck_m']*100))
        assert max(bridge_errors)<2,bridge_errors
        routes=[a for a in actors if str(a.get_folder_path())=='StarterRegion/Review/RouteCandidates']
        assert routes and all(a.get_editor_property('is_editor_only_actor') for a in routes)
        assert all(not c.get_editor_property('visible') and c.get_editor_property('hidden_in_game') for a in routes for c in a.get_components_by_class(unreal.SplineMeshComponent))
        # Check the saved Landscape consumer, not a preserved unused predecessor.
        material=proxies[0].get_editor_property('landscape_material');assert material
        assert all(p.get_editor_property('landscape_material')==material for p in proxies)
        material_path=material.get_path_name().split('.')[0]
        assert material_path in ['/Game/World/Starter/Materials/M_StarterTerrain','/Game/World/Starter/Materials/M_StarterTerrain_Biome']
        expressions=unreal.MaterialEditingLibrary.get_material_expressions(material)
        expected_nodes=(23 if RETIRED_GRASS_GROUPS else 20) if material_path.endswith('_Biome') else 14
        assert len(expressions)==expected_nodes,[(e.get_name(),e.get_class().get_name()) for e in expressions]
        color=unreal.MaterialEditingLibrary.get_material_property_input_node(material,unreal.MaterialProperty.MP_BASE_COLOR)
        assert isinstance(color,unreal.MaterialExpressionCustom) and 'Cobble' in color.get_editor_property('code')
        dirty_names={p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
        assert material_path not in dirty_names
        assert '/Game/World/Starter/Materials/M_StarterBridgeStone' not in dirty_names
        report.update(passed=True,world_state_actors=1,environment_actors=1,saved_sun_lux=lux,places=len(PLAN['places']),connections=7,landscape_components=component_count,terrain_collision_samples=len(errors),maximum_collision_error_cm=max(errors),player_starts=1,buildings=8,ordinary_editor_loaded_buildings=len(ordinary_house_labels),ordinary_editor_loaded_actors=len(ordinary_loaded),authored_actors=len(owned),duplicate_authored_labels=False,nature_counts=dict(counts),nature_spatial_groups=groups,instance_rotation_samples=checked_instances,well_mesh_components=4,capital_entrance_actors=6,bridge_collision_samples=len(bridge_errors),bridge_collision_reload_error_cm=max(bridge_errors),terrain_material_expression_count=len(expressions),obsolete_route_rendering_retired=True,weather_deferred=True,nav_verified=False,hlod_built=False,performance_measured=False,packaging_verified=False,visual_quality_approved=False)
    except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
    (OUT/'corridor-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('STARTER_CORRIDOR_SAVED_VERIFIED '+json.dumps(report))
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
handle=unreal.register_slate_post_tick_callback(verify)
