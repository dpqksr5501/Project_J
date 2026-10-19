"""Add the estuary trading harbor from its profile; no terrain/water regeneration."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'harbor-build.json';assert not FILE.exists(),'Inspect completed/unknown native results before retrying'
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json')
P=next(p for p in json.loads((SOURCE/'MainWorld.settlements.json').read_text(encoding='utf-8'))['settlements'] if p['target_id']=='S05');assert P['stable_id']=='S06';Q=P['quay'];cx,cy=next(p['center_m'][:2] for p in C.PLAN['places'] if p['id']=='S06')
TAG='ProjectJ.Starter.V4.Harbor.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S06/HarborBlockout');assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
CUBE=C.mesh('/Engine/BasicShapes/Cube');STONE=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldBridgeStone');WOOD=unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2');assert STONE and WOOD
R=dict(phase='QUEUED',target_id='S05',stable_id='S06',completed=[],houses=[],access=[],pending_access=[],entries=[],height_sha256=T.manifest['terrain_profile']['sha256'],profile_sha256=hashlib.sha256((SOURCE/'MainWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
def record(phase):R.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def tag(a):a.set_editor_property('tags',[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S06'),unreal.Name('ProjectJ.Target.S05')]);return a
def empty(label,x,y,z):
    a=tag(A.spawn('S06_Harbor_'+label,'/Engine/BasicShapes/Cube',x,y,z));a.static_mesh_component.set_static_mesh(None);a.set_actor_location(A.point(x,y,z),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));return a
def hism(a,name,mesh,material=None,collision=True):
    c=C.component(a,unreal.HierarchicalInstancedStaticMeshComponent,name);c.set_static_mesh(mesh);c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    if material:c.set_material(0,A.instanced_material(material))
    else:
        for i,s in enumerate(mesh.static_materials):
            if s.material_interface:c.set_material(i,A.instanced_material(s.material_interface))
    return c
def house(data):
    dx,dy=data['offset_m'];h=dict(data,x=cx+dx,y=cy+dy,yaw=math.degrees(math.atan2(-dx,dy)),roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile']);a,r=A.supported_house('S06_Harbor_'+data['id'],h,T.height,C,STONE);tag(a);R['houses'].append(r)
def access():
    actors={a.get_actor_label():a for a in A.subsystem.get_all_level_actors()}
    for h in R['houses']:
        try:R['access'].append(dict(label=h['label'],**A.add_access_stairs(actors[h['label']],h,T.height,C,STONE)))
        except AssertionError as e:
            if 'Exterior approach requires bespoke terrain-aware access' not in str(e):raise
            R['pending_access'].append(dict(label=h['label'],reason=str(e)))
def quay():
    first,last=Q['points_m'];x,y=first;z=Q['deck_top_m'];dx,dy=last[0]-x,last[1]-y;length=math.hypot(dx,dy);yaw=math.degrees(math.atan2(dy,dx));a=empty('TradingQuay',x,y,z);groups={n:hism(a,n,CUBE,WOOD) for n in ['Deck','Piles','Beams','Rails','CargoRamp']};normal=Q['normal_to_land']
    def box(name,px,py,bottom,w,d,h,angle=0):groups[name].add_instance(C.centered_transform(CUBE,unreal.Vector((px-x)*100,(py-y)*100,(bottom-z)*100),angle,(w,d,h)),False)
    n=math.ceil(length/.25);step=length/n
    for i in range(n):
        t=(i+.5)/n;box('Deck',x+dx*t,y+dy*t,z-Q['thickness_m'],step-.008,Q['width_m'],Q['thickness_m'],yaw)
    for i in range(math.ceil(length/5)+1):
        t=min(1,i*5/length)
        for sign in [-1,1]:
            px,py=x+dx*t+normal[0]*sign*2.65,y+dy*t+normal[1]*sign*2.65;bottom=T.height(px,py)-.5;top=z-Q['thickness_m'] if sign==1 and abs(t-.5)*length<3 else z+.8;box('Piles',px,py,bottom,.28,.28,top-bottom)
    for sign in [-1,1]:box('Beams',x+dx/2+normal[0]*sign*2.3,y+dy/2+normal[1]*sign*2.3,z-.5,length,.28,.28,yaw)
    # Leave the bank attachment open while keeping an outer-side safety rail.
    box('Rails',x+dx/2-normal[0]*2.65,y+dy/2-normal[1]*2.65,z+.7,length,.12,.12,yaw)
    start,end=Q['bank_point_m'],Q['ramp_attach_m'];rx,ry=end[0]-start[0],end[1]-start[1];horizontal=math.hypot(rx,ry);dz=end[2]-start[2];pitch=math.atan2(dz,horizontal);thick=.22
    transform=unreal.Transform(location=unreal.Vector(((start[0]+end[0])/2-x)*100,((start[1]+end[1])/2-y)*100,((start[2]+end[2])/2-z-thick/(2*math.cos(pitch)))*100),rotation=unreal.Rotator(pitch=math.degrees(pitch),yaw=math.degrees(math.atan2(ry,rx)),roll=0),scale=unreal.Vector(math.hypot(horizontal,dz),5,thick));groups['CargoRamp'].add_instance(transform,False)
    R['quay']=dict(label=a.get_actor_label(),points_m=Q['points_m'],deck_top_m=z,width_m=Q['width_m'],bank_point_m=start,ramp_attach_m=end,ramp_grade=Q['ramp_grade'],components={n:c.get_instance_count() for n,c in groups.items()})
def tower():
    p=P['watchtower'];x,y=cx+p['offset_m'][0],cy+p['offset_m'][1];w=p['foundation_width_m'];samples=[T.height(x+u,y+v) for u in [-w/2,0,w/2] for v in [-w/2,0,w/2]];floor=max(samples)+.06;bottom=min(samples)-.25
    a=tag(A.spawn('S06_Harbor_Lookout',p['mesh'],x,y,floor,0,(p['scale'],)*3));foundation=tag(A.spawn('S06_Harbor_LookoutFoundation','/Engine/BasicShapes/Cube',x,y,bottom,0,(w,w,floor-bottom),STONE));R['watchtower']=dict(label=a.get_actor_label(),foundation_label=foundation.get_actor_label(),center_m=[x,y,floor],scale=p['scale'],interior_verified=False)
def goods():
    x,y=cx-25,cy-30;z=T.height(x,y);a=empty('CargoCourt',x,y,z);counts={}
    for name,mesh,spacing,count in [('Crates','SM_FruitCrate01',1.4,24),('BarrelStacks','SM_Barrel03',3,6)]:
        obj=C.mesh('/Game/Map_Resource/SkyTown/Meshes/'+mesh);c=hism(a,name,obj,collision=False)
        for i in range(count):
            u=(i%6-2.5)*spacing;v=(i//6-1.5)*spacing+(11 if name=='BarrelStacks' else 0);ground=T.height(x+u,y+v)
            c.add_instance(C.centered_transform(obj,unreal.Vector(u*100,v*100,(ground-z)*100),(i*47)%360,(1,1,1)),False)
        counts[name]=count
    R['cargo']=dict(label=a.get_actor_label(),components=counts)
    first,last=Q['points_m'];normal=Q['normal_to_land'];yaw=math.degrees(math.atan2(last[1]-first[1],last[0]-first[0]))-90
    for i,t in enumerate([.22,.72]):
        x=first[0]*(1-t)+last[0]*t-normal[0]*6;y=first[1]*(1-t)+last[1]*t-normal[1]*6;water=Q['reference_water_levels_m'][0]*(1-t)+Q['reference_water_levels_m'][1]*t
        tag(A.spawn('S06_Harbor_MerchantBoat'+str(i+1),'/Game/Map_Resource/SkyTown/Meshes/SM_Boat01',x,y,water-.5,yaw,(1.7,1.7,1.7),collision=False))
def entries():
    for name,q in [('North',P['streets'][0]['points_relative_m'][0]),('East',P['streets'][1]['points_relative_m'][-1])]:
        label='Entry_S06_'+name;assert label not in A.labels;x,y=cx+q[0],cy+q[1];a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,T.height(x,y)+.1));assert a
        a.set_actor_label(label);a.set_folder_path(A.folder);tag(a);a.tags=list(a.tags)+[unreal.Name('ProjectJ.Entry.S06.'+name)];a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);assert not a.get_editor_property('replicates');R['entries'].append(dict(label=label,path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple())))
tasks=[(h['id'],lambda h=h:house(h)) for h in P['houses']]+[('Access',access),('Quay',quay),('Lookout',tower),('Cargo',goods),('Entries',entries)];index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):A.save();record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];record('APPLYING_'+key);action();R['completed'].append(key);index+=1
        if index%2==0 or index==len(tasks):A.save()
        record('READY')
    except Exception:R['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(R['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
