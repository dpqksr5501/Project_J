"""Save an accessible timber watchtower and open roadside checkpoint."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'road-watch-build.json';assert not FILE.exists()
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');support=module('support','Starter-SettlementSupport.py');P=next(p for p in json.loads((SOURCE/'StarterWorld.settlements.json').read_text())['settlements'] if p['target_id']=='S08');TAG='ProjectJ.Starter.V4.RoadWatch.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S08/RoadWatchBlockout');assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
STONE=unreal.load_asset('/Game/World/Starter/Materials/M_StarterBridgeStone');WOOD=unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2');CUBE=C.mesh('/Engine/BasicShapes/Cube');assert STONE and WOOD;cx=cy=3100
R=dict(phase='QUEUED',target_id='S08',stable_id='S00',completed=[],houses=[],access=[],entries=[],height_sha256=T.manifest['terrain_profile']['sha256'],profile_sha256=hashlib.sha256((SOURCE/'StarterWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
def record(phase):R.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def tag(a):a.tags=[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S00'),unreal.Name('ProjectJ.Target.S08')];return a
def empty(label,x,y,z):
    a=tag(A.spawn('S08_Watch_'+label,'/Engine/BasicShapes/Cube',x,y,z));a.static_mesh_component.set_static_mesh(None);a.set_actor_location(A.point(x,y,z),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));return a
def hism(a,name,material=WOOD,collision=True):
    c=C.component(a,unreal.HierarchicalInstancedStaticMeshComponent,name);c.set_static_mesh(CUBE);c.set_material(0,A.instanced_material(material));c.set_collision_profile_name('BlockAll' if collision else 'NoCollision');return c
def house():
    q=P['houses'][0];h=dict(q,x=q['center_m'][0],y=q['center_m'][1],yaw=q['yaw_degrees'],roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile']);a,r=A.supported_house('S08_Watch_Barracks',h,T.height,C,STONE);tag(a);R['houses'].append(r);R['access'].append(dict(label=a.get_actor_label(),**A.add_access_stairs(a,r,T.height,C,STONE)))
def tower():
    q=P['lookout'];x,y=cx+q['offset_m'][0],cy+q['offset_m'][1];floor=q['deck_top_m'];a=empty('TimberTower',x,y,floor);parts={n:hism(a,n) for n in ['Deck','Posts','Beams','Rails','EntryStairs','StairStringers','Roof']}
    def box(name,u,v,bottom,w,d,h,yaw=0):parts[name].add_instance(C.centered_transform(CUBE,unreal.Vector(u*100,v*100,(bottom-floor)*100),yaw,(w,d,h)),False)
    for i in range(20):box('Deck',-2.5+(i+.5)*.25,0,floor-.22,.242,5,.22)
    for u in [-2.2,2.2]:
        for v in [-2.2,2.2]:
            bottom=T.height(x+u,y+v)-.25;box('Posts',u,v,bottom,.32,.32,floor+3.4-bottom)
    for v in [-2.2,2.2]:box('Beams',0,v,floor-.55,5.4,.25,.33);box('Beams',0,v,floor+3.15,5.5,.24,.24)
    for u in [-2.2,2.2]:box('Beams',u,0,floor-.75,.25,5.4,.3)
    for u in [-2.4,2.4]:box('Rails',u,0,floor+.95,.14,5,.14)
    box('Rails',0,2.4,floor+.95,5,.14,.14)
    for side in [-1,1]:box('Rails',side*1.75,-2.4,floor+.95,1.5,.14,.14)
    for u,v in [(-2.4,-2.4),(-.98,-2.4),(.98,-2.4),(2.4,-2.4),(-2.4,0),(2.4,0),(-2.4,2.4),(0,2.4),(2.4,2.4)]:box('Rails',u,v,floor,.14,.14,1.1)
    planned=support.plan_access_stairs(dict(id='TimberTower',center_m=[x,y,floor],yaw=0,depth=5),T.height)
    for distance,depth,top,bottom in planned['pieces']:box('EntryStairs',0,-distance,top-.12,1.8,depth,.12)
    foot=P['lookout']['approach_m'];length=y-2.5-foot[1];foot_z=T.height(*foot)
    # Continuous diagonal stringers/handrails support the actual thin timber treads.
    for side in [-1,1]:
        for name,rise,w,h in [('StairStringers',-.22,.16,.3),('Rails',1.0,.12,.12)]:
            start=[side*.99,foot[1]-y,foot_z+rise];end=[side*.99,-2.5,floor+rise];horizontal=end[1]-start[1];dz=end[2]-start[2];angle=math.degrees(math.atan2(dz,horizontal));t=unreal.Transform(location=unreal.Vector(side*.99*100,(start[1]+end[1])*50,((start[2]+end[2])/2-floor)*100),rotation=unreal.Rotator(pitch=0,yaw=0,roll=-angle),scale=unreal.Vector(w,math.hypot(horizontal,dz),h));parts[name].add_instance(t,False)
        for i in range(4):
            distance=2.5+length*i/3;top=floor-(floor-foot_z)*i/3-.25;bottom=T.height(x+side*.99,y-distance)-.2;box('Posts',side*.99,-distance,bottom,.18,.18,max(.2,top-bottom))
    for side in [-1,1]:
        t=unreal.Transform(location=unreal.Vector(0,side*150,375),rotation=unreal.Rotator(pitch=0,yaw=0,roll=side*math.degrees(math.atan(.5))),scale=unreal.Vector(6,math.hypot(3,1.5),.16));parts['Roof'].add_instance(t,False)
    R['lookout']=dict(label=a.get_actor_label(),center_m=[x,y,floor],width=5,depth=5,yaw=0,approach_m=foot,components={n:c.get_instance_count() for n,c in parts.items()});R['access'].append(dict(label=a.get_actor_label(),**{k:v for k,v in planned.items() if k!='pieces'}))
def checkpoint():
    q=P['checkpoint'];x,y,z=q['center_m'];a=empty('RoadsideRails',x,y,z);c=hism(a,'Rails');angle=math.radians(q['heading_degrees']);co,si=math.cos(angle),math.sin(angle)
    for side in [-1,1]:
        for t in [-4,0,4]:
            px,py=x+co*t-si*side*5,y+si*t+co*side*5;bottom=T.height(px,py);c.add_instance(C.centered_transform(CUBE,unreal.Vector((px-x)*100,(py-y)*100,(bottom-z)*100),0,(.2,.2,1.15)),False)
        for t in [-2,2]:
            px,py=x+co*t-si*side*5,y+si*t+co*side*5;bottom=T.height(px,py)+.75;c.add_instance(C.centered_transform(CUBE,unreal.Vector((px-x)*100,(py-y)*100,(bottom-z)*100),q['heading_degrees'],(4,.12,.16)),False)
    R['checkpoint']=dict(q,label=a.get_actor_label(),instances=c.get_instance_count())
def market():
    q=P['tents'][0];x,y,z=q['center_m'];a=empty('RoadsideStall',x,y,z);a.set_actor_rotation(unreal.Rotator(pitch=0,yaw=q['yaw_degrees'],roll=0),False);c=C.component(a,unreal.StaticMeshComponent,'Canopy');m=C.mesh(q['mesh']);c.set_static_mesh(m);c.set_collision_profile_name('NoCollision');c.set_relative_transform(C.centered_transform(m,unreal.Vector()),False,False)
    c=C.component(a,unreal.StaticMeshComponent,'Foundation');c.set_static_mesh(CUBE);c.set_material(0,STONE);c.set_collision_profile_name('BlockAll');d=z-q['foundation_bottom_m'];c.set_relative_transform(C.centered_transform(CUBE,unreal.Vector(0,0,-d*100),0,(q['width']+.24,q['depth']+.24,d)),False,False)
    r=dict(id=q['id'],label=a.get_actor_label(),center_m=q['center_m'],yaw=q['yaw_degrees'],width=q['width'],depth=q['depth'],foundation_bottom_m=q['foundation_bottom_m']);R['market']=r;R['access'].append(dict(label=a.get_actor_label(),**A.add_access_stairs(a,r,T.height,C,STONE)))
    for name,path,u,v,yaw in [('Table','SM_Furniture01_Table01',3181,3188,0),('Bench','SM_Furniture01_Bench01',3180,3184,90),('Cart','SM_Cart01',3176,3195,20),('Sacks','SM_Sacks01_Item01',3186,3192,45)]:tag(A.spawn('S08_Watch_'+name,'/Game/Map_Resource/SkyTown/Meshes/'+path,u,v,T.height(u,v),yaw,collision=False))
def entries():
    for name,point in [('Road',P['walk_route_points_m'][0]),('WatchDeck',[3160,3220])]:
        x,y=point;z=P['lookout']['deck_top_m'] if name=='WatchDeck' else T.height(x,y);a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,z+.1));assert a;a.set_actor_label('Entry_S08_'+name);a.set_folder_path(A.folder);tag(a);a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);R['entries'].append(dict(label=a.get_actor_label(),path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple())))
tasks=[('Barracks',house),('Tower',tower),('Checkpoint',checkpoint),('Market',market),('Entries',entries)];index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):A.save();record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];record('APPLYING_'+key);action();R['completed'].append(key);index+=1;A.save();record('READY')
    except Exception:R['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(R['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
