"""Bake the registered lower-river hamlet and a supported timber crossing."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'river-hamlet-build.json';assert not FILE.exists()
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');assert json.loads((OUT/'river-hamlet-registration.json').read_text())['phase']=='COMPLETE' and json.loads((OUT/'river-forest-regeneration.json').read_text())['phase']=='COMPLETE'
P=next(p for p in json.loads((SOURCE/'MainWorld.settlements.json').read_text(encoding='utf-8'))['settlements'] if p['target_id']=='S07');B=P['bridge'];place=next(p for p in C.PLAN['places'] if p['id']=='S07');cx,cy=place['center_m'][:2];TAG='ProjectJ.Starter.V4.RiverHamlet.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S07/RiverHamletBlockout');assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
CUBE=C.mesh('/Engine/BasicShapes/Cube');STONE=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldBridgeStone');WOOD=unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2');assert STONE and WOOD
R=dict(phase='QUEUED',target_id='S07',stable_id='S07',completed=[],houses=[],access=[],entries=[],height_sha256=T.manifest['terrain_profile']['sha256'],profile_sha256=hashlib.sha256((SOURCE/'MainWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
def record(phase):R.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def tag(a):a.set_editor_property('tags',[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S07'),unreal.Name('ProjectJ.Target.S07')]);return a
def empty(label,x,y,z):
    a=tag(A.spawn('S07_River_'+label,'/Engine/BasicShapes/Cube',x,y,z));a.static_mesh_component.set_static_mesh(None);a.set_actor_location(A.point(x,y,z),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));return a
def hism(a,name,mesh,material=None,collision=True):
    c=C.component(a,unreal.HierarchicalInstancedStaticMeshComponent,name);c.set_static_mesh(mesh);c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    if material:c.set_material(0,A.instanced_material(material))
    else:
        for i,s in enumerate(mesh.static_materials):
            if s.material_interface:c.set_material(i,A.instanced_material(s.material_interface))
    return c
def house(data):
    dx,dy=data['offset_m'];h=dict(data,x=cx+dx,y=cy+dy,yaw=data['yaw_degrees'],roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile']);a,r=A.supported_house('S07_River_'+data['id'],h,T.height,C,STONE);tag(a);R['houses'].append(r);R['access'].append(dict(label=a.get_actor_label(),**A.add_access_stairs(a,r,T.height,C,STONE)))
def bridge():
    first,last=B['points_m'];x,y=first;z=B['deck_top_m'];dx,dy=last[0]-x,last[1]-y;length=math.hypot(dx,dy);yaw=math.degrees(math.atan2(dy,dx));normal=[-dy/length,dx/length];a=empty('TimberCrossing',x,y,z);groups={n:hism(a,n,CUBE,WOOD) for n in ['Deck','Piles','Beams','Rails','Ramps']}
    def box(name,px,py,bottom,w,d,h,angle=0):groups[name].add_instance(C.centered_transform(CUBE,unreal.Vector((px-x)*100,(py-y)*100,(bottom-z)*100),angle,(w,d,h)),False)
    n=math.ceil(length/.25);step=length/n
    for i in range(n):
        t=(i+.5)/n;box('Deck',x+dx*t,y+dy*t,z-B['thickness_m'],step-.008,B['width_m'],B['thickness_m'],yaw)
    for side in [-1,0,1]:box('Beams',x+dx/2+normal[0]*side*2,y+dy/2+normal[1]*side*2,z-.64,length,.32,.4,yaw)
    for i in range(math.ceil(length/8)+1):
        t=min(1,i*8/length)
        for side in [-1,1]:
            px,py=x+dx*t+normal[0]*side*2,y+dy*t+normal[1]*side*2;top=z-.64;bottom=min(T.height(px,py)-.4,top-.3);box('Piles',px,py,bottom,.38,.38,top-bottom)
    for side in [-1,1]:
        box('Rails',x+dx/2+normal[0]*side*2.35,y+dy/2+normal[1]*side*2.35,z+.95,length,.14,.14,yaw)
        for i in range(math.ceil(length/5-1e-9)+1):
            t=min(1,i*5/length);box('Rails',x+dx*t+normal[0]*side*2.35,y+dy*t+normal[1]*side*2.35,z,.15,.15,1.1)
    ramps=[]
    def segment(name,start,end,width,thickness):
        rx,ry=end[0]-start[0],end[1]-start[1];horizontal=math.hypot(rx,ry);dz=end[2]-start[2];pitch=math.atan2(dz,horizontal)
        t=unreal.Transform(location=unreal.Vector(((start[0]+end[0])/2-x)*100,((start[1]+end[1])/2-y)*100,((start[2]+end[2])/2-z-thickness/(2*math.cos(pitch)))*100),rotation=unreal.Rotator(pitch=math.degrees(pitch),yaw=math.degrees(math.atan2(ry,rx)),roll=0),scale=unreal.Vector(math.hypot(horizontal,dz),width,thickness));groups[name].add_instance(t,False)
    for index in [0,1]:
        start=B['bank_points_m'][index];end=[*B['points_m'][index],z];segment('Ramps',start,end,B['width_m'],.24);ramps.append(dict(start_m=start,end_m=end,grade=abs(start[2]-end[2])/math.dist(start[:2],end[:2])))
        for side in [-1,1]:
            rail_start=[start[0]+normal[0]*side*2.35,start[1]+normal[1]*side*2.35,start[2]+1.09];rail_end=[end[0]+normal[0]*side*2.35,end[1]+normal[1]*side*2.35,end[2]+1.09];segment('Rails',rail_start,rail_end,.14,.14)
    R['bridge']=dict(label=a.get_actor_label(),points_m=B['points_m'],bank_points_m=B['bank_points_m'],deck_top_m=z,width_m=B['width_m'],ramps=ramps,components={n:c.get_instance_count() for n,c in groups.items()})
def well():
    x,y=cx+8,cy-4;a=tag(A.spawn('S07_River_Well','/Game/Map_Resource/Meshes/Well/SM_MainWell',x,y,T.height(x,y),15))
    for name,path in [('StoneBase','/Game/Map_Resource/Meshes/Well/WellBase/SM_WellBase'),('Shingles','/Game/Map_Resource/Meshes/Well/SM_WellShingles'),('Crank','/Game/Map_Resource/Meshes/Well/SM_WellDetailPieces')]:A.add_static_component(a,path,name)
def goods():
    x,y=cx+16,cy-8;z=T.height(x,y);a=empty('MerchantGoods',x,y,z);counts={}
    for name,path,count in [('Barrels','/Game/Map_Resource/ElderboomVillage/Meshes/SM_Wooden_Barrel_uc2ichvqx_lod3',4),('Crates','/Game/Map_Resource/SkyTown/Meshes/SM_FruitCrate01',6)]:
        mesh=C.mesh(path);c=hism(a,name,mesh,collision=False)
        for i in range(count):
            u=(i%3-1)*1.3;v=(i//3)*1.3+(3 if name=='Crates' else 0);c.add_instance(C.centered_transform(mesh,unreal.Vector(u*100,v*100,(T.height(x+u,y+v)-z)*100),i*37),False)
        counts[name]=count
    R['goods']=dict(label=a.get_actor_label(),components=counts)
def entries():
    for name,q in [('North',[cx+70,cy+110]),('West',B['bank_points_m'][0][:2])]:
        label='Entry_S07_'+name;assert label not in A.labels;x,y=q;a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,T.height(x,y)+.1));assert a;a.set_actor_label(label);a.set_folder_path(A.folder);tag(a);a.tags=list(a.tags)+[unreal.Name('ProjectJ.Entry.S07.'+name)];a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);assert not a.get_editor_property('replicates');R['entries'].append(dict(label=label,path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple())))
tasks=[(h['id'],lambda h=h:house(h)) for h in P['houses']]+[('Bridge',bridge),('Well',well),('Goods',goods),('Entries',entries)];index=0;busy=False
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
