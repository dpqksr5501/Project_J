"""Read saved hunting bounds, ground, capsule candidates and preserved world data."""
import hashlib,importlib.util,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';target=globals().get('TARGET_ID','H01');B=json.loads((OUT/(target+'-hunting-build.json')).read_text());PRE=json.loads((OUT/(target+'-hunting-preflight.json')).read_text());manifest=json.loads((SOURCE/'StarterRegion.manifest.json').read_text());P=next(p for p in json.loads((SOURCE/manifest['hunting_profiles']).read_text())['hunting_grounds'] if p['target_id']==target);assert B['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False)
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');C=module('contract','Starter-WorldDefinitionContract.py');origin=T.grid['unreal_origin_cm'];units=T.grid['centimetres_per_source_metre']
SURFACE=module('surface','Starter-HuntingSurface.py').HuntingSurface(SOURCE,T,P['authored_surfaces']) if P.get('authored_surfaces') else None
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);r=dict(passed=False,read_only=True,target_id=target)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();owned=[a for a in actors if B['owned_tag'] in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned};rows=B['actors']+B['regions']+B['candidates']+B['entries'];assert len(owned)==len(bylabel)==len(rows)
        authored_checks=[]
        for ref in P.get('authored_surfaces',[]):
            a=next(a for a in actors if a.get_path_name()==ref['actor_path']);assert ref['owner'] in [str(t) for t in a.tags];assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('replicates');assert hashlib.sha256((SOURCE/ref['source']).read_bytes()).hexdigest()==ref['sha256'];authored_checks.append(dict(ref,existing_native_owner_verified=True))
        for row in rows:
            a=bylabel[row['label']];assert a.get_path_name()==row['path'] and (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01;assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor') and not a.get_editor_property('replicates')
        for row in B['regions']:
            a=bylabel[row['label']];c=a.get_component_by_class(unreal.BoxComponent);assert str(c.get_collision_profile_name())=='NoCollision' and not c.get_editor_property('generate_overlap_events');assert (c.get_unscaled_box_extent()-unreal.Vector(row['radius_m']*100,row['radius_m']*100,600)).length()<.01
        errors=[];capsules=[]
        def ground(q):
            x,y=q[:2];support=SURFACE.support(x,y) if SURFACE else None;expected=point(x,y,support['height_m'] if support else T.height(x,y));above,below=(25,100) if SURFACE or P.get('ground_query_mode')=='forest_floor_with_headroom' else (500,500);hit=unreal.SystemLibrary.line_trace_single(world,expected+unreal.Vector(0,0,above),expected-unreal.Vector(0,0,below),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);v=hit.to_tuple() if hit else None;assert v and v[0],('Ground missing',q);error=abs(v[4].z-expected.z);assert error<10,('Source/actual ground mismatch',q,error)
            if support and support['actor_path']:assert v[9].get_path_name()==support['actor_path'],('Wrong authored support owner',q)
            errors.append(error);return v
        for cluster in P['clusters']:
            x,y,z=cluster['center_m']
            sample_step=1 if cluster['radius_m']<5 else (2 if cluster['radius_m']<10 else 5)
            for dx in range(-20,21,sample_step):
                for dy in range(-20,21,sample_step):
                    if math.hypot(dx,dy)<=cluster['radius_m']:ground([x+dx,y+dy])
            for candidate in cluster['spawn_candidates']:
                ground(candidate['position_m']);x,y,z=candidate['position_m'];p=point(x,y,z+candidate['capsule_half_height_m']+candidate['capsule_ground_gap_m'])
                for complex_trace in [False,True]:
                    hit=unreal.SystemLibrary.capsule_trace_single(world,p,p+unreal.Vector(0,0,.1),candidate['clearance_radius_m']*100,candidate['capsule_half_height_m']*100,unreal.TraceTypeQuery.ECC_VISIBILITY,complex_trace,[],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert not values or not values[0],('Spawn capsule blocked',candidate['id'],complex_trace);capsules.append(dict(id=candidate['id'],complex_trace=complex_trace,clear=True))
        headroom_checks=0;cover_checks=[];floor_checks=[]
        for trail in P['trails']:
            for q in trail['points_m']:
                surface=ground(q)
                if P.get('trail_headroom_check'):
                    normal_z=surface[7].z;limit=P.get('trail_walkable_floor_angle_degrees',45);assert normal_z>=math.cos(math.radians(limit)),('Trail floor exceeds candidate walkable angle',trail['id'],q,normal_z)
                    # A vertical capsule on an inclined plane contacts its lower
                    # hemisphere above the center ground height. For radius r,
                    # the required lift is r*(1/normal.z-1); retain 15cm query
                    # clearance above that support. This is an airspace check,
                    # followed by real CharacterMovement traversal, not Nav.
                    lift=.6*(1/normal_z-1);base=SURFACE.stance_height(q[0],q[1],.6) if SURFACE else T.height(*q);p=point(q[0],q[1],base+1.15+lift);hit=unreal.SystemLibrary.capsule_trace_single(world,p,p+unreal.Vector(0,0,.1),60,100,unreal.TraceTypeQuery.ECC_VISIBILITY,False,[],unreal.DrawDebugTrace.NONE,True);assert not hit or not hit.to_tuple()[0],('Trail capsule blocked',trail['id'],q);headroom_checks+=1;floor_checks.append(dict(position_m=q,normal_z=normal_z,capsule_support_lift_m=lift,declared_floor_height_m=base))
        actor_labels={a.get_actor_label():a for a in actors}
        pond_checks=[]
        if P.get('water_surfaces'):
            W=module('wetland_geometry','Starter-WetlandGeometry.py');water_actors=[bylabel[q['label']] for q in B['water_surfaces']]
            for q in P['water_surfaces']:
                row=next(v for v in B['water_surfaces'] if v['id']==q['id']);a=bylabel[row['label']];c=a.static_mesh_component;file=SOURCE/q['geometry_source'];assert hashlib.sha256(file.read_bytes()).hexdigest()==q['geometry_sha256'];surface=next(v for v in json.loads(file.read_text())['pools'] if v['id']==q['geometry_id']);assert c.static_mesh.get_path_name()==row['mesh'];assert c.static_mesh.get_num_triangles(0)==len(surface['triangles'])==row['triangles'];assert unreal.EditorAssetLibrary.get_metadata_tag(c.static_mesh,'ProjectJ.SourceHash')==q['geometry_sha256'];assert unreal.EditorAssetLibrary.get_metadata_tag(c.static_mesh,'ProjectJ.GeometryID')==q['geometry_id'];assert c.get_collision_enabled()==unreal.CollisionEnabled.QUERY_ONLY and c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_PAWN)==unreal.CollisionResponseType.ECR_IGNORE and c.get_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY)==unreal.CollisionResponseType.ECR_BLOCK
                assert (a.get_actor_location()-point(*q['center_m'])).length()<.01;shore_errors=[];depths=[];wet_queries=0;level=q['water_level_m']
                for first,last in surface['shore_segments_m']:
                    x,y=(first[0]+last[0])/2,(first[1]+last[1])/2;p=point(x,y,level);hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,100),p-unreal.Vector(0,0,100),unreal.TraceTypeQuery.ECC_VISIBILITY,True,water_actors,unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0],('Pond shore ground absent',q['id'],x,y);error=abs(values[4].z-p.z);assert error<10,('Pond shore floats or cuts terrain',q['id'],x,y,error);shore_errors.append(error)
                triangles=surface['triangles'];vertices=surface['vertices_m']
                for indices in triangles[::max(1,len(triangles)//32)]:
                    x,y=[sum(vertices[i][axis] for i in indices)/3 for axis in [0,1]]
                    if level-T.height(x,y)<.2:continue
                    p=point(x,y,level);hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,15),p-unreal.Vector(0,0,15),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0] and values[10]==c and abs(values[4].z-p.z)<1,('Native wet surface not found',q['id'],x,y)
                    hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,15),p-unreal.Vector(0,0,200),unreal.TraceTypeQuery.ECC_VISIBILITY,True,water_actors,unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0];depth=(p.z-values[4].z)/100;assert 0<depth<=1.1;depths.append(depth);wet_queries+=1
                assert wet_queries>=10
                trail_clearance=min(W.surface_distance(*v,surface)-trail['width_m']/2 for trail in P['trails'] for v in trail['points_m']);combat_clearance=min(W.surface_distance(*v['center_m'][:2],surface)-v['radius_m'] for v in P['clusters']);assert trail_clearance>=12 and combat_clearance>=25
                pond_checks.append(dict(id=q['id'],triangles=row['triangles'],wet_queries=wet_queries,shore_samples=len(shore_errors),maximum_shore_ground_error_cm=max(shore_errors),maximum_sampled_native_depth_m=max(depths),trail_edge_clearance_m=trail_clearance,combat_circle_clearance_m=combat_clearance,pawn_response='IGNORE',source_height_unchanged=True,swimming_bound=False,scope='Static shallow surface and natural terrain intersection; no flowing channel/swimming/final water art approval'))
        group_checks=[];ruin_cover_checks=[]
        if P.get('instance_groups'):
            G=module('hunting_geometry','Build-StarterCorridor.py')
            for q in P['instance_groups']:
                row=next(v for v in B['instance_groups'] if v['id']==q['id']);a=bylabel[row['label']];cx,cy,cz=q['center_m'];parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)};assert len(parts)==len(q['sets']);count=0;maxerror=0;ground_error=0
                for part in q['sets']:
                    c=parts[part['component']];assert c.static_mesh.get_path_name().split('.')[0]==part['mesh'];assert c.get_instance_count()==len(part['instances']);assert str(c.get_collision_profile_name())==('BlockAll' if q['collision'] else 'NoCollision')
                    for i,v in enumerate(part['instances']):
                        x,y,z=v['position_m'];expected=G.centered_transform(c.static_mesh,unreal.Vector((x-cx)*100,(y-cy)*100,(z-cz)*100),v['yaw_degrees'],tuple(v['scale']));actual=c.get_instance_transform(i,False)
                        for axis in [unreal.Vector(),unreal.Vector(100,0,0),unreal.Vector(0,100,0),unreal.Vector(0,0,100)]:maxerror=max(maxerror,(actual.transform_location(axis)-expected.transform_location(axis)).length())
                        assert maxerror<.01,('Saved instance transform mismatch',q['id'],part['component'],i);assert abs(z-(T.height(x,y)-.035))<.001
                        p=point(x,y,T.height(x,y));hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,25),p-unreal.Vector(0,0,100),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0],('No ground beneath authored instance',q['id'],i);ground_error=max(ground_error,abs(values[4].z-p.z));assert ground_error<10,('Authored instance ground mismatch',q['id'],i,ground_error);count+=1
                group_checks.append(dict(id=q['id'],sets=len(q['sets']),instances=count,maximum_transform_error_cm=maxerror,maximum_native_ground_error_cm=ground_error,source_ground_bottom_verified=True))
        for q in P['props']:
            if q.get('role')!='broken_ruin_fragment':continue
            row=next(v for v in B['props'] if v['id']==q['id']);a=bylabel[row['label']];c=a.static_mesh_component;assert c.static_mesh.get_path_name().split('.')[0]==q['mesh'];assert str(c.get_collision_profile_name())=='BlockAll';center,extent=a.get_actor_bounds(False);normal=a.get_actor_right_vector();hit=unreal.SystemLibrary.line_trace_single(world,center-normal*500,center+normal*500,unreal.TraceTypeQuery.ECC_VISIBILITY,False,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0] and values[10]==c,('Ruin fragment simple collision missing',q['id']);ruin_cover_checks.append(dict(id=q['id'],simple_visibility_blocked=True,scope='Fragment collider only, no AI/projectile profile approval'))
        water_checks=None
        if P.get('water_separation'):
            separation=P['water_separation'];inspection=json.loads((OUT/separation['native_inspection']).read_text());water=actor_labels['StarterRegion_LakePrototype'];c=water.static_mesh_component;E=module('hunting_routes','Starter-HuntingRoutes.py')
            assert water.get_path_name()==inspection['actor_path'] and c.static_mesh.get_path_name()==inspection['mesh']
            assert (water.get_actor_location()-unreal.Vector(*inspection['location_cm'])).length()<.01 and (water.get_actor_scale3d()-unreal.Vector(*inspection['scale'])).length()<.01
            assert all(abs(a-b)<.001 for a,b in zip(water.get_actor_rotation().to_tuple(),inspection['rotation']))
            assert str(c.get_collision_profile_name())==inspection['collision_profile'] and str(c.get_collision_enabled())==inspection['collision_enabled']
            ellipse=separation['ellipse'];assert ellipse==json.loads((SOURCE/'StarterWorld.plan.json').read_text())['lake']==inspection['ellipse']
            trail_clearances=[E.ellipse_clearance_lower_bound(*q,ellipse)-trail['width_m']/2 for trail in P['trails'] for q in trail['points_m']]
            combat_clearances=[E.ellipse_clearance_lower_bound(*q['center_m'][:2],ellipse)-q['radius_m'] for q in P['clusters']]
            assert min(trail_clearances)>=separation['trail_minimum_clearance_m'] and min(combat_clearances)>=separation['combat_minimum_clearance_m']
            for sample in inspection['wet_samples']:
                x,y,z=sample['position_m'];hit=unreal.SystemLibrary.line_trace_single(world,point(x,y,z+10),point(x,y,z-20),unreal.TraceTypeQuery.ECC_VISIBILITY,False,[],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0] and values[10]==c and abs(values[4].z-point(x,y,z).z)<1
            water_checks=dict(native_water_preserved=True,wet_queries=len(inspection['wet_samples']),minimum_trail_edge_clearance_lower_bound_m=min(trail_clearances),minimum_combat_circle_clearance_lower_bound_m=min(combat_clearances),natural_shore_complete=False,water_complete=False,scope='Dry candidate circles and trail edges outside enclosing ellipse; prototype wet query only. Natural shoreline and swimming unfinished.')
        for cluster in P['clusters']:
            for cover in cluster.get('cover_references',[]):
                a=actor_labels[cover['actor_label']];c=next(c for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent) if c.get_name()==cover['component']);assert c.static_mesh.get_path_name()==cover['mesh'];t=c.get_instance_transform(cover['instance'],True);assert (t.translation-point(*cover['position_m'])).length()<.01
                p=t.translation+unreal.Vector(0,0,140);dx,dy=cover['position_m'][0]-cluster['center_m'][0],cover['position_m'][1]-cluster['center_m'][1];length=math.hypot(dx,dy);normal=unreal.Vector(dx/length,dy/length,0);hit=unreal.SystemLibrary.line_trace_single(world,p-normal*300,p+normal*300,unreal.TraceTypeQuery.ECC_VISIBILITY,False,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0] and values[10]==c and values[13]==cover['instance'],('Referenced tree has no verified simple cover collision',cover['id']);cover_checks.append(dict(id=cover['id'],actor_path=a.get_path_name(),component=c.get_name(),instance=cover['instance'],simple_visibility_line_blocked=True,scope='Existing instance collider only; AI perception/projectile profile unverified'))
        structural_errors=[];stockade_wall_checks=0;stockade_gate_checks=0;structure_stair_samples=0
        for stockade in B.get('stockades',[]):
            q=next(v for v in P['stockades'] if v['id']==stockade['id']);a=bylabel[stockade['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)};posts=parts['StockadePosts'];assert posts.get_instance_count()==stockade['post_count']==len(q['posts_m']);assert parts['StockadeRails'].get_instance_count()==stockade['rail_count'];bounds=posts.static_mesh.get_bounds();ignore=[v for v in actors if v!=a]
            for i,expected in enumerate(q['posts_m']):
                bottom=posts.get_instance_transform(i,True).transform_location(unreal.Vector(bounds.origin.x,bounds.origin.y,bounds.origin.z-bounds.box_extent.z));structural_errors.append((bottom-point(*expected)).length())
            for i in range(0,posts.get_instance_count(),max(1,posts.get_instance_count()//24)):
                t=posts.get_instance_transform(i,True);p=t.transform_location(bounds.origin);dx,dy=p.x-a.get_actor_location().x,p.y-a.get_actor_location().y;length=math.hypot(dx,dy);normal=unreal.Vector(dx/length,dy/length,0);hit=unreal.SystemLibrary.capsule_trace_single(world,p-normal*200,p+normal*200,35,70,unreal.TraceTypeQuery.ECC_VISIBILITY,False,ignore,unreal.DrawDebugTrace.NONE,True);assert hit and hit.to_tuple()[0],('Stockade wall lacks simple collision',i);stockade_wall_checks+=1
            for gate in q['gates']:
                x,y,z=gate['position_m'];nx,ny=(x-q['center_m'][0])/q['radius_m'],(y-q['center_m'][1])/q['radius_m']
                for lateral in [-2.5,0,2.5]:
                    start=[]
                    for along in [-4,4]:
                        px,py=x+nx*along-ny*lateral,y+ny*along+nx*lateral;start.append(point(px,py,T.height(px,py)+1.15))
                    hit=unreal.SystemLibrary.capsule_trace_single(world,*start,60,100,unreal.TraceTypeQuery.ECC_VISIBILITY,False,ignore,unreal.DrawDebugTrace.NONE,True);assert not hit or not hit.to_tuple()[0],('Stockade gate obstructed',gate['id'],lateral);stockade_gate_checks+=1
        for structure in B.get('structures',[]):
            a=bylabel[structure['label']];p=point(*structure['center_m']);ignore=[v for v in actors if v!=a];hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,5),p-unreal.Vector(0,0,5),unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE,True);assert hit and hit.to_tuple()[0];structural_errors.append(abs(hit.to_tuple()[4].z-p.z));c=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');access=next(v for v in B['access'] if v['label']==structure['label']);assert c.get_instance_count()==access['instances'];bounds=c.static_mesh.get_bounds()
            for i in range(c.get_instance_count()):
                top=c.get_instance_transform(i,True).transform_location(unreal.Vector(bounds.origin.x,bounds.origin.y,bounds.origin.z+bounds.box_extent.z));hit=unreal.SystemLibrary.line_trace_single(world,top+unreal.Vector(0,0,5),top-unreal.Vector(0,0,5),unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE,True);assert hit and hit.to_tuple()[0];structural_errors.append(abs(hit.to_tuple()[4].z-top.z));structure_stair_samples+=1
        if structural_errors:assert max(structural_errors)<.1
        state=next(a for a in actors if isinstance(a,unreal.Project_JWorldState));definition=state.get_editor_property('definition');assert [C.snapshot_place(v) for v in definition.get_editor_property('places')]==B['previous_places'] and [C.snapshot_connection(v) for v in definition.get_editor_property('connections')]==B['previous_connections'];assert definition.get_editor_property('source_plan_hash')==B['source_plan_hash']
        cx,cy=PRE['place']['center_m'][:2];radius=PRE['place']['radius_m']+100;current=[]
        for a in actors:
            if a in owned:continue
            if not a.get_components_by_class(unreal.InstancedStaticMeshComponent):continue
            centre,extent=a.get_actor_bounds(False);x,y=(centre.x-origin[0])/units,(centre.y-origin[1])/units
            if abs(x-cx)>radius+extent.x/units or abs(y-cy)>radius+extent.y/units:continue
            for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                if not c.static_mesh or not any(v in c.static_mesh.get_path_name() for v in ['BlackAlder','Bush']):continue
                for i in range(c.get_instance_count()):
                    q=c.get_instance_transform(i,True).translation;x,y=(q.x-origin[0])/units,(q.y-origin[1])/units
                    if math.hypot(x-cx,y-cy)<=radius:current.append((a.get_actor_label(),c.get_name(),i,c.static_mesh.get_path_name(),round(x,4),round(y,4),round((q.z-origin[2])/units,4)))
        before=[(v['actor'],v['component'],v['index'],v['mesh'],*[round(q,4) for q in v['position_m']]) for v in PRE['forest']];assert sorted(current)==sorted(before),'Existing local vegetation positions changed'
        r.update(passed=True,actors=len(owned),combat_regions=len(B['regions']),spawn_candidates=sum(len(q['spawn_candidates']) for q in P['clusters']),patrol_candidates=sum(len(q['patrol_candidates_m']) for q in P['clusters']),ground_samples=len(errors),maximum_ground_error_cm=max(errors),capsule_checks=capsules,capsule_query_scope='Visibility simple/complex world geometry; final NPC collision channel/Nav and runtime spawn unverified',stockade_wall_collision_checks=stockade_wall_checks,stockade_gate_clearance_checks=stockade_gate_checks,structure_stair_samples=structure_stair_samples,maximum_structural_error_cm=max(structural_errors) if structural_errors else 0,preserved_local_vegetation_positions=len(current),old_places_connections_preserved=True,height_sha256=T.manifest['terrain_profile']['sha256'],runtime_spawn_enabled=False,gameplay_ready=False,navigation_verified=False,art_complete=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
        r.update(trail_headroom_checks=headroom_checks,trail_floor_checks=floor_checks,tree_cover_checks=cover_checks,ground_query_mode=P.get('ground_query_mode','Full scene +/-5m surface query'),water_separation_checks=water_checks,instance_group_checks=group_checks,ruin_cover_checks=ruin_cover_checks,pond_checks=pond_checks)
        r['authored_surface_checks']=authored_checks
    except Exception:r['error']=traceback.format_exc();unreal.log_error(r['error'])
    (OUT/(target+'-hunting-saved-verification.json')).write_text(json.dumps(r,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
