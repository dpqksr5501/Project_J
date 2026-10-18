"""Add a dry-bank fishing village and supported landing. Never runs at startup."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'lake-village-build.json';assert not FILE.exists(),'Inspect previous native results before retrying'
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py')
P=next(p for p in json.loads((SOURCE/'StarterWorld.settlements.json').read_text(encoding='utf-8'))['settlements'] if p['target_id']=='S04');assert P['stable_id']=='S03'
cx,cy=next(p['center_m'][:2] for p in C.PLAN['places'] if p['id']=='S03');D=P['dock'];TAG='ProjectJ.Starter.V4.Lake.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S03/LakeBlockout')
assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
STONE=unreal.load_asset('/Game/World/Starter/Materials/M_StarterBridgeStone');WOOD=unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2');CUBE=C.mesh('/Engine/BasicShapes/Cube');assert STONE and WOOD
REPORT=dict(phase='QUEUED',target_id='S04',stable_id='S03',completed=[],houses=[],access=[],pending_access=[],entries=[],groups=[],height_sha256=hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest(),profile_sha256=hashlib.sha256((SOURCE/'StarterWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
def record(phase):
    REPORT.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
def tag(a):
    a.set_editor_property('tags',[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S03'),unreal.Name('ProjectJ.Target.S04')]);return a
def empty(label,x,y,z):
    a=tag(A.spawn('S03_Lake_'+label,'/Engine/BasicShapes/Cube',x,y,z));a.static_mesh_component.set_static_mesh(None);a.set_actor_location(A.point(x,y,z),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));return a
def instances(a,name,mesh,collision=False,material=None):
    c=C.component(a,unreal.HierarchicalInstancedStaticMeshComponent,name);c.set_static_mesh(mesh);c.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
    if material:c.set_material(0,A.instanced_material(material))
    else:
        for i,slot in enumerate(mesh.static_materials):
            if slot.material_interface:c.set_material(i,A.instanced_material(slot.material_interface))
    return c
def house(data):
    dx,dy=data['offset_m'];h=dict(data,x=cx+dx,y=cy+dy,yaw=math.degrees(math.atan2(-dx,dy)),roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile'])
    a,r=A.supported_house('S03_Lake_'+data['id'],h,C.height,C,STONE);tag(a);REPORT['houses'].append(r)
def access():
    actors={a.get_actor_label():a for a in A.subsystem.get_all_level_actors()}
    for h in REPORT['houses']:
        try:REPORT['access'].append(dict(label=h['label'],**A.add_access_stairs(actors[h['label']],h,C.height,C,STONE)))
        except AssertionError as error:
            if 'Exterior approach requires bespoke terrain-aware access' not in str(error):raise
            REPORT['pending_access'].append(dict(label=h['label'],reason=str(error)))
def pier():
    x,y,z=D['axis_x_m'],D['ramp_end_y_m'],D['deck_top_m'];a=empty('FishingLanding',x,y,z);groups={name:instances(a,name,CUBE,True,WOOD) for name in ['Planks','Piles','Rails','BankRamp']}
    def box(name,px,py,bottom,w,d,h):
        groups[name].add_instance(C.centered_transform(CUBE,unreal.Vector((px-x)*100,(py-y)*100,(bottom-z)*100),0,(w,d,h)),False)
    step=.25;length=y-D['head_y_m'];n=round(length/step)
    for i in range(n):box('Planks',x,y-(i+.5)*step,z-D['thickness_m'],D['width_m'],step-.01,D['thickness_m'])
    for i in range(round(D['head_depth_m']/step)):box('Planks',x,D['head_y_m']-D['head_depth_m']/2+(i+.5)*step,z-D['thickness_m'],D['head_width_m'],step-.01,D['thickness_m'])
    for py in [y-i*5 for i in range(int(length/5)+1)]+[D['head_y_m']-2]:
        for sign in [-1,1]:
            px=x+sign*(D['width_m']/2-.15);bottom=C.height(px,py)-.5;box('Piles',px,py,bottom,.25,.25,z+.8-bottom)
    for sign in [-1,1]:box('Rails',x+sign*(D['width_m']/2-.15),(y+D['head_y_m']+3)/2,z+.68,.12,length-3,.12)
    # A continuous shallow ramp joins the dry bank, rather than a floating deck.
    land=D['land_y_m'];start_z=C.height(x,land)+.08;horizontal=land-y;dz=z-start_z;angle=math.atan2(dz,horizontal);thick=.18
    transform=unreal.Transform(location=unreal.Vector(0,(land-y)/2*100,((start_z+z)/2-z-thick/(2*math.cos(angle)))*100),rotation=unreal.Rotator(pitch=math.degrees(angle),yaw=-90,roll=0),scale=unreal.Vector(math.hypot(horizontal,dz),D['width_m'],thick))
    groups['BankRamp'].add_instance(transform,False)
    REPORT['dock']=dict(label=a.get_actor_label(),deck_top_m=z,ramp_start_m=[x,land,start_z],ramp_end_m=[x,y,z],ramp_grade=abs(dz/horizontal),head_m=[x,D['head_y_m'],z],water_level_m=D['water_level_m'],components={name:c.get_instance_count() for name,c in groups.items()})
def fish_rack():
    x,y=cx+30,cy-115;z=C.height(x,y);a=empty('FishDryingCourt',x,y,z);frame=instances(a,'TimberFrame',CUBE,False,WOOD)
    for u in [-2,2]:frame.add_instance(C.centered_transform(CUBE,unreal.Vector(u*100,0,-15),0,(.12,.12,2.15)),False)
    frame.add_instance(C.centered_transform(CUBE,unreal.Vector(0,0,190),0,(4.3,.12,.12)),False)
    sets=[]
    for i,name in enumerate(['SM_FishDry01','SM_FishDry02']):
        mesh=C.mesh('/Game/Map_Resource/SkyTown/Meshes/'+name);comp=instances(a,'Fish'+str(i),mesh)
        for j in range(8):comp.add_instance(C.centered_transform(mesh,unreal.Vector((-1.7+j*.45)*100,(-.1+i*.2)*100,130),180,(1,1,1)),False)
        sets.append(dict(component='Fish'+str(i),instances=8))
    REPORT['groups'].append(dict(label=a.get_actor_label(),sets=[dict(component='TimberFrame',instances=3)]+sets))
def props():
    for i,sign in enumerate([-1,1]):
        tag(A.spawn('S03_Lake_MooredBoat'+str(i+1),'/Game/Map_Resource/SkyTown/Meshes/SM_Boat01',D['axis_x_m']+sign*7,D['head_y_m']+3,D['water_level_m']-.35,10*sign,collision=False))
    for i,(dx,dy) in enumerate([(25,-110),(28,-111),(24,-113),(32,-110)]):
        path='/Game/Map_Resource/SkyTown/Meshes/'+('SM_Barrel01' if i%2 else 'SM_FruitCrate01');tag(A.spawn('S03_Lake_FishingGoods'+str(i+1),path,cx+dx,cy+dy,C.height(cx+dx,cy+dy),i*28))
def entries():
    for name,q in [('West',P['streets'][0]['points_relative_m'][0]),('East',P['streets'][0]['points_relative_m'][-1])]:
        label='Entry_S03_'+name;assert label not in A.labels;x,y=cx+q[0],cy+q[1];a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,C.height(x,y)+.1));assert a
        a.set_actor_label(label);a.set_folder_path(A.folder);tag(a);a.tags=list(a.tags)+[unreal.Name('ProjectJ.Entry.S03.'+name)];a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);assert not a.get_editor_property('replicates')
        REPORT['entries'].append(dict(label=label,path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple())))
tasks=[(h['id'],lambda h=h:house(h)) for h in P['houses']]+[('Access',access),('Pier',pier),('FishRack',fish_rack),('Props',props),('Entries',entries)];index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):
            A.save();assert hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest()==REPORT['height_sha256'];record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];record('APPLYING_'+key);action();REPORT['completed'].append(key);index+=1
        if index%2==0 or index==len(tasks):A.save()
        record('READY')
    except Exception:REPORT['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(REPORT['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
