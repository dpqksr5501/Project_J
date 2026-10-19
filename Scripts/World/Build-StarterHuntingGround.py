"""Save one data-authored hunting space without enabling a runtime spawn writer."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';target=globals().get('TARGET_ID','H01');FILE=OUT/(target+'-hunting-build.json');previous=json.loads(FILE.read_text()) if FILE.exists() else None
resume_water=bool(previous and "type object 'CollisionResponse' has no attribute 'IGNORE'" in previous.get('error',''))
if previous:assert previous['phase']=='ERROR' and not previous['completed'] and not previous['regions'] and not previous['candidates'] and (resume_water or 'set_generate_overlap_events' in previous['error'])
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
S=module('settlement','Starter-SettlementAuthoring.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');C=module('contract','Starter-WorldDefinitionContract.py');P=next(v for v in json.loads((SOURCE/T.manifest['hunting_profiles']).read_text())['hunting_grounds'] if v['target_id']==target);assert P['stage']=='PLANNED';TAG='ProjectJ.Starter.V4.Hunting.'+target+'.v'+str(P['generation_version']);A=S.SettlementAuthoring(TAG,'StarterRegion/Hunting/'+target+'/SpatialBlockout',T.manifest);owned=[a for a in A.subsystem.get_all_level_actors() if TAG in [str(t) for t in a.tags]]
if previous:
    expected_label=P['water_surfaces'][0]['id'].replace('.','_') if resume_water else P['clusters'][0]['id'].replace('.','_')+'_CombatBounds';assert len(owned)==1 and owned[0].get_actor_label()==expected_label
    archive=OUT/(target+('-hunting-before-water-response-api-fix.json' if resume_water else '-hunting-before-overlap-api-fix.json'));assert not archive.exists();archive.write_text(FILE.read_text(),encoding='utf-8')
    if resume_water:assert len(previous['actors'])==1 and not previous['water_surfaces'] and not previous['props'] and not previous['entries'];assert previous['profile_sha256']==hashlib.sha256((SOURCE/T.manifest['hunting_profiles']).read_bytes()).hexdigest()
else:assert not owned
definition=next(a for a in A.subsystem.get_all_level_actors() if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');R=dict(phase='QUEUED',target_id=target,stable_id=P['stable_id'],owned_tag=TAG,completed=[],regions=[],candidates=[],entries=[],props=[],previous_places=[C.snapshot_place(v) for v in definition.get_editor_property('places')],previous_connections=[C.snapshot_connection(v) for v in definition.get_editor_property('connections')],source_plan_hash=definition.get_editor_property('source_plan_hash'),profile_sha256=hashlib.sha256((SOURCE/T.manifest['hunting_profiles']).read_bytes()).hexdigest(),height_sha256=T.manifest['terrain_profile']['sha256'],runtime_spawn_enabled=False,art_complete=False,navigation_verified=False)
R.update(stockades=[],structures=[],access=[],instance_groups=[],water_surfaces=[])
if P.get('authored_surfaces'):
    module('surface','Starter-HuntingSurface.py').HuntingSurface(SOURCE,T,P['authored_surfaces'])
    existing={a.get_path_name():a for a in A.subsystem.get_all_level_actors()}
    for ref in P['authored_surfaces']:
        assert ref['actor_path'] in existing and ref['owner'] in [str(t) for t in existing[ref['actor_path']].tags],'Missing declared saved floor; do not recreate it'
    R['authored_surfaces']=P['authored_surfaces']
if resume_water:assert R['previous_places']==previous['previous_places'] and R['previous_connections']==previous['previous_connections'] and R['source_plan_hash']==previous['source_plan_hash']
if P.get('stockades') or P.get('structures') or P.get('instance_groups'):G=module('hunting_geometry','Build-StarterCorridor.py')
def record(phase):R.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def tag(a,kind,id):
    a.tags=[unreal.Name(TAG),unreal.Name('ProjectJ.Location.'+P['stable_id']),unreal.Name('ProjectJ.Hunting.'+target),unreal.Name('Candidate.'+kind),unreal.Name(id)];a.set_folder_path(A.folder);a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);assert not a.get_editor_property('replicates');return a
def marker(id,position,kind):
    label=id.replace('.','_');assert label not in A.labels;a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(*position));assert a;a.set_actor_label(label);tag(a,kind,id);A.labels.add(label);return dict(id=id,label=label,path=a.get_path_name(),position_m=position,location_cm=list(a.get_actor_location().to_tuple()),role=kind)
def cluster(q):
    x,y,z=q['center_m'];label=q['id'].replace('.','_')+'_CombatBounds'
    if label in A.labels:
        assert previous and q['id']==P['clusters'][0]['id'];a=owned[0];assert (a.get_actor_location()-A.point(x,y,z+2)).length()<.01
    else:a=A.subsystem.spawn_actor_from_class(unreal.TriggerBox,A.point(x,y,z+2));assert a;a.set_actor_label(label)
    tag(a,'CombatBounds.NotBound',q['id']);c=a.get_component_by_class(unreal.BoxComponent);assert c;c.modify();c.set_box_extent(unreal.Vector(q['radius_m']*100,q['radius_m']*100,600),False);c.set_collision_profile_name('NoCollision');c.set_editor_property('generate_overlap_events',False);R['regions'].append(dict(id=q['id'],label=label,path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple()),center_m=q['center_m'],radius_m=q['radius_m'],native_shape='EDITOR_BOUNDING_BOX_ONLY; Source circle is spawn/encounter candidate footprint',runtime_bound=False))
    for v in q['spawn_candidates']:R['candidates'].append(marker(v['id'],v['position_m'],'NPCSpawn.NotBound'))
    for i,point in enumerate(q['patrol_candidates_m'],1):R['candidates'].append(marker(q['id']+'.Patrol%02d'%i,point,'Patrol.NotBound'))
def prop(q):
    x,y=q['position_xy_m'];m=unreal.load_asset(q['mesh']);assert isinstance(m,unreal.StaticMesh);b=m.get_bounds();sx,sy,sz=q['scale'];half=max(b.box_extent.x*abs(sx),b.box_extent.y*abs(sy))/100;heights=[T.height(x+u,y+v) for u in [-half,0,half] for v in [-half,0,half]];z=min(heights)-(.12 if 'Rock' in q['mesh'] else 0)
    if q.get('vertical_anchor'):
        anchor=q['vertical_anchor'];assert anchor['kind']=='top';z=anchor['height_m']-b.box_extent.z*2*abs(sz)/100
    a=A.spawn(q['id'].replace('.','_'),q['mesh'],x,y,z,q['yaw_degrees'],tuple(q['scale']),collision=q['collision']);tag(a,'EnvironmentBlockout',q['id']);R['props'].append(dict(q,label=a.get_actor_label(),path=a.get_path_name(),bottom_source_m=z,footprint_relief_m=max(heights)-min(heights)))

def empty(id,center):
    a=A.spawn(id.replace('.','_'),'/Engine/BasicShapes/Cube',*center);a.static_mesh_component.set_static_mesh(None);a.set_actor_location(A.point(*center),False,False);tag(a,'EnvironmentBlockout',id);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));return a

def stockade(q):
    cx,cy,cz=q['center_m'];a=empty(q['id'],q['center_m']);m=unreal.load_asset(q['post_mesh']);assert isinstance(m,unreal.StaticMesh);b=m.get_bounds();scale=(q['post_diameter_m']/(max(b.box_extent.x,b.box_extent.y)*2/100),)*2+(q['height_m']/(b.box_extent.z*2/100),)
    posts=G.component(a,unreal.HierarchicalInstancedStaticMeshComponent,'StockadePosts');posts.set_static_mesh(m);posts.set_collision_profile_name('BlockAll')
    for i,s in enumerate(m.static_materials):
        if s.material_interface:posts.set_material(i,A.instanced_material(s.material_interface))
    for i,(x,y,z) in enumerate(q['posts_m']):posts.add_instance(G.centered_transform(m,unreal.Vector((x-cx)*100,(y-cy)*100,(z-cz)*100),i*37%360,scale),False)
    rails=G.component(a,unreal.HierarchicalInstancedStaticMeshComponent,'StockadeRails');rails.set_static_mesh(G.mesh('/Engine/BasicShapes/Cube'));rails.set_collision_profile_name('BlockAll');rails.set_material(0,A.instanced_material(unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2')))
    for i in range(0,len(q['posts_m'])-1,6):
        first,last=q['posts_m'][i],q['posts_m'][min(i+6,len(q['posts_m'])-1)];dx,dy,dz=[last[k]-first[k] for k in range(3)];horizontal=math.hypot(dx,dy)
        if horizontal>4:continue
        for rise in [.9,1.7]:
            t=unreal.Transform(location=unreal.Vector(((first[0]+last[0])/2-cx)*100,((first[1]+last[1])/2-cy)*100,((first[2]+last[2])/2+rise-cz)*100),rotation=unreal.Rotator(pitch=math.degrees(math.atan2(dz,horizontal)),yaw=math.degrees(math.atan2(dy,dx)),roll=0),scale=unreal.Vector(math.hypot(horizontal,dz),.12,.14));rails.add_instance(t,False)
    R['stockades'].append(dict(id=q['id'],label=a.get_actor_label(),path=a.get_path_name(),post_count=posts.get_instance_count(),rail_count=rails.get_instance_count(),gates=q['gates'],height_m=q['height_m'],post_mesh=q['post_mesh']))

def structure(q):
    assert q['kind']=='tent','Add an explicit authoring contract for another structure kind'
    a=empty(q['id'],q['center_m']);a.set_actor_rotation(unreal.Rotator(pitch=0,yaw=q['yaw_degrees'],roll=0),False);cube=G.mesh('/Engine/BasicShapes/Cube');stone=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldBridgeStone');c=G.component(a,unreal.StaticMeshComponent,'Foundation');c.set_static_mesh(cube);c.set_material(0,stone);c.set_collision_profile_name('BlockAll');depth=q['center_m'][2]-q['foundation_bottom_m'];c.set_relative_transform(G.centered_transform(cube,unreal.Vector(0,0,-depth*100),0,(q['width']+.24,q['depth']+.24,depth)),False,False)
    c=G.component(a,unreal.StaticMeshComponent,'Shelter');m=unreal.load_asset(q['mesh']);assert isinstance(m,unreal.StaticMesh);c.set_static_mesh(m);c.set_collision_profile_name('NoCollision');c.set_relative_transform(G.centered_transform(m,unreal.Vector(),0,(q['scale'],)*3),False,False)
    placement=dict(id=q['id'],label=a.get_actor_label(),center_m=q['center_m'],yaw=q['yaw_degrees'],width=q['width'],depth=q['depth']);access=A.add_access_stairs(a,placement,T.height,G,stone);R['structures'].append(dict(q,label=a.get_actor_label(),path=a.get_path_name()));R['access'].append(dict(label=a.get_actor_label(),**access))
def water_surface(q):
    file=SOURCE/q['geometry_source'];assert hashlib.sha256(file.read_bytes()).hexdigest()==q['geometry_sha256'];data=json.loads(file.read_text());surface=next(v for v in data['pools'] if v['id']==q['geometry_id']);cx,cy,cz=q['center_m']
    reuse=resume_water and q['id']==P['water_surfaces'][0]['id']
    if reuse:
        m=unreal.load_asset(q['mesh_asset']);assert m and unreal.EditorAssetLibrary.get_metadata_tag(m,'ProjectJ.Authoring')==TAG and unreal.EditorAssetLibrary.get_metadata_tag(m,'ProjectJ.SourceHash')==q['geometry_sha256'] and unreal.EditorAssetLibrary.get_metadata_tag(m,'ProjectJ.GeometryID')==q['geometry_id']
    else:
        W=module('water_authoring','Starter-WaterSurfaceAuthoring.py');m=W.create_owned_mesh(q,surface,TAG,A.units)
    if reuse:
        a=owned[0];assert a.get_path_name()==previous['actors'][0]['path'] and a.static_mesh_component.static_mesh==m and (a.get_actor_location()-A.point(cx,cy,cz)).length()<.01;a.modify();a.static_mesh_component.modify();A.created.append(previous['actors'][0])
    else:
        a=A.spawn(q['id'].replace('.','_'),q['mesh_asset'],cx,cy,cz,collision=False)
    a.set_actor_location(A.point(cx,cy,cz),False,False);A.created[-1].update(location_cm=list(a.get_actor_location().to_tuple()));tag(a,'WaterSurface.NotBound',q['id']);c=a.static_mesh_component;W=module('water_query','Starter-WaterSurfaceAuthoring.py');W.configure_surface_query(c)
    R['water_surfaces'].append(dict(id=q['id'],label=a.get_actor_label(),path=a.get_path_name(),mesh=m.get_path_name(),triangles=m.get_num_triangles(0),geometry_source=q['geometry_source'],geometry_sha256=q['geometry_sha256'],water_level_m=cz,maximum_depth_m=surface['maximum_depth_m'],area_m2=surface['area_m2'],collision_policy=q['collision_policy']))

def instance_group(q):
    a=empty(q['id'],q['center_m']);cx,cy,cz=q['center_m'];sets=[]
    for part in q['sets']:
        c=G.component(a,unreal.HierarchicalInstancedStaticMeshComponent,part['component']);m=unreal.load_asset(part['mesh']);assert isinstance(m,unreal.StaticMesh);c.set_static_mesh(m);c.set_collision_profile_name('BlockAll' if q['collision'] else 'NoCollision')
        for i,slot in enumerate(m.static_materials):
            if slot.material_interface:c.set_material(i,A.instanced_material(slot.material_interface))
        for v in part['instances']:
            x,y,z=v['position_m'];c.add_instance(G.centered_transform(m,unreal.Vector((x-cx)*100,(y-cy)*100,(z-cz)*100),v['yaw_degrees'],tuple(v['scale'])),False)
        sets.append(dict(component=part['component'],mesh=m.get_path_name(),count=c.get_instance_count()))
    R['instance_groups'].append(dict(id=q['id'],label=a.get_actor_label(),path=a.get_path_name(),sets=sets,role=q['role']))

def entries():
    for q in P['entries']:R['entries'].append(marker(q['id'],q['position_m'],'Entry.NotBound'))
tasks=[(q['id'],lambda q=q:water_surface(q)) for q in P.get('water_surfaces',[])]+[(q['id'],lambda q=q:cluster(q)) for q in P['clusters']]+[(q['id'],lambda q=q:stockade(q)) for q in P.get('stockades',[])]+[(q['id'],lambda q=q:structure(q)) for q in P.get('structures',[])]+[(q['id'],lambda q=q:prop(q)) for q in P['props']]+[(q['id'],lambda q=q:instance_group(q)) for q in P.get('instance_groups',[])]+[('Entries',entries)];index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):A.save();record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];record('APPLYING_'+key);action();R['completed'].append(key);index+=1
        if index%3==0 or index==len(tasks):A.save()
        record('READY')
    except Exception:R['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(R['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
