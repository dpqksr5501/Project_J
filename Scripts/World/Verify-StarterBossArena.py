"""Saved boss space: real floors, capsule room, safe separation and one server owner."""
import importlib.util,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';target=globals().get('TARGET_ID','B01');B=json.loads((OUT/(target+'-boss-build.json')).read_text());assert B['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False)
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');C=module('contract','Starter-WorldDefinitionContract.py');P=next(p for p in json.loads((SOURCE/T.manifest['boss_profiles']).read_text())['bosses'] if p['target_id']==target);origin=T.grid['unreal_origin_cm']
def point(x,y,z):return unreal.Vector(origin[0]+x*100,origin[1]+y*100,origin[2]+z*100)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);R=dict(passed=False,read_only=True,target_id=target,gameplay_ready=False,art_complete=False)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();by_path={a.get_path_name():a for a in actors};owned=[a for a in actors if B['owned_tag'] in [str(t) for t in a.tags]];assert len(owned)==len(B['actors'])+len(B['bounds'])+len(B['markers'])+1
        for row in B['actors']:assert (by_path[row['path']].get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01
        for row in B['props']:
            a=by_path[row['path']];c=a.static_mesh_component;assert c.static_mesh.get_path_name().split('.')[0]==row['mesh'];assert (a.get_actor_scale3d()-unreal.Vector(*row['scale'])).length()<.0001;assert abs((a.get_actor_rotation().yaw-row['yaw_degrees']+180)%360-180)<.001
            b=c.static_mesh.get_bounds();bottom=a.get_actor_transform().transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z-b.box_extent.z));assert abs(bottom.z-point(0,0,row['bottom_m']).z)<.01;assert not a.get_editor_property('replicates') and a.get_editor_property('is_spatially_loaded')
        for row in B['bounds']:
            a=by_path[row['path']];c=a.get_component_by_class(unreal.BoxComponent);assert c.get_collision_profile_name()=='NoCollision' and not c.get_editor_property('generate_overlap_events');assert (c.get_unscaled_box_extent()-unreal.Vector(row['radius_m']*100,row['radius_m']*100,600)).length()<.01
        for row in B['markers']:assert (by_path[row['path']].get_actor_location()-point(*row['position_m'])).length()<.01
        owners=[a for a in actors if isinstance(a,unreal.Project_JWorldEncounter) and str(a.get_editor_property('encounter_id'))==P['encounter']['id']];assert len(owners)==1;a=owners[0];assert a.get_path_name()==B['encounter']['path'];assert a.get_editor_property('replicates') and not a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('npc_class');assert (a.get_actor_location()-point(*P['arena']['center_m'])).length()<.01;assert a.get_editor_property('activation_radius')==P['encounter']['activation_radius_cm'] and a.get_editor_property('respawn_seconds')==P['encounter']['respawn_seconds']
        ground_errors=[];normals=[];capsules=[]
        forest_arena=target=='B02' and bool(P.get('combat_pockets'))
        floor_ignore=[a for a in actors if a.get_actor_label().startswith('PCG_Forest_') or B['owned_tag'] in [str(t) for t in a.tags]] if forest_arena else []
        def floor(x,y):
            z=T.height(x,y);h=unreal.SystemLibrary.line_trace_single(world,point(x,y,z+.25),point(x,y,z-1),unreal.TraceTypeQuery.ECC_VISIBILITY,True,floor_ignore,unreal.DrawDebugTrace.NONE,True);h=h.to_tuple() if h else None;assert h and h[0],('Missing ground',x,y);assert not forest_arena or 'Landscape' in h[9].get_class().get_name();error=abs(h[4].z-point(x,y,z).z);assert error<10,('Ground differs from original',x,y,error);ground_errors.append(error);normals.append(h[7].z);return z,h[7].z
        def capsule(x,y,radius,half):
            z,nz=floor(x,y);assert nz>math.cos(math.radians(44.7));lift=radius*(1/nz-1);cp=point(x,y,z+half+.15+lift)
            if forest_arena:h=unreal.SystemLibrary.capsule_trace_single_by_profile(world,cp,cp+unreal.Vector(0,0,.1),radius*100,half*100,'Pawn',False,[],unreal.DrawDebugTrace.NONE,True)
            else:h=unreal.SystemLibrary.capsule_trace_single(world,cp,cp+unreal.Vector(0,0,.1),radius*100,half*100,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True)
            assert not h or not h.to_tuple()[0],('Capsule blocked',x,y);capsules.append(dict(position_m=[x,y,z],radius_m=radius,half_height_m=half,query='Pawn.simple' if forest_arena else 'Visibility.complex'))
        cx,cy=P['arena']['center_m'][:2]
        for u in range(-32,33,4):
            for v in range(-32,33,4):
                if math.hypot(u,v)<=P['arena']['radius_m']:z,nz=floor(cx+u,cy+v);assert nz>math.cos(math.radians(44.7))
        if forest_arena:
            for pocket in P['combat_pockets']:
                x,y=pocket['center_m'][:2];r=int(pocket['radius_m'])
                for u in range(-r,r+1,2):
                    for v in range(-r,r+1,2):
                        if math.hypot(u,v)<=r:capsule(x+u,y+v,1.2,2)
        else:
            for u,v in [(0,0),(18,0),(-18,0),(0,18),(0,-18)]:capsule(cx+u,cy+v,1.2,2)
        for p in P['safe_staging']['points_m']:capsule(p[0],p[1],.6,1)
        for trail in P['trails']:
            for a,b in zip(trail['points_m'],trail['points_m'][1:]):
                count=max(1,math.ceil(math.dist(a,b)/2))
                for i in range(count):
                    boss_movement=forest_arena and trail['role'] in ['forest_boss_movement_candidate','player_and_boss_orbit_candidate'];capsule(a[0]+(b[0]-a[0])*i/count,a[1]+(b[1]-a[1])*i/count,1.2 if boss_movement else .6,2 if boss_movement else 1)
        bypass=next(p for p in P['trails'] if p['role']=='public_bypass_outside_candidate_reset');minimum=min(math.dist(p,[cx,cy])-P['arena']['reset_candidate_radius_m']-bypass['width_m']/2 for p in bypass['points_m']);assert minimum>=10 and P['safe_staging']['reset_candidate_edge_clearance_m']>=12
        definition=next(a for a in actors if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');assert [C.snapshot_place(v) for v in definition.places]==B['previous_places'];assert [C.snapshot_connection(v) for v in definition.connections]==B['previous_connections'];assert definition.source_plan_hash==B['source_plan_hash'];dirty=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()];assert not dirty
        R.update(passed=True,actors=len(owned),ground_samples=len(ground_errors),maximum_ground_error_cm=max(ground_errors),minimum_floor_normal_z=min(normals),capsule_checks=capsules,bypass_candidate_reset_edge_clearance_m=minimum,staging_candidate_reset_edge_clearance_m=P['safe_staging']['reset_candidate_edge_clearance_m'],server_owner=B['encounter'],places_preserved=20,connections_preserved=7,dirty_maps=dirty,height_sha256=T.manifest['terrain_profile']['sha256'],scope='Saved core/staging/paths and one unbound native encounter; outer rim hazards, actual boss AI/skills/reset/rewards/Nav and art remain pending')
    except Exception:R['error']=traceback.format_exc();unreal.log_error(R['error'])
    (OUT/(target+'-boss-saved-verification.json')).write_text(json.dumps(R,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
