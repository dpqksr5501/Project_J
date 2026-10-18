"""Explicit additive forestry village pass. Refuses an unknown/finished retry."""
import hashlib,importlib.util,json,math,random,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'forest-village-build.json'
assert not FILE.exists(),'Inspect existing native authoring; never duplicate a pass'
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py')
P=next(p for p in json.loads((SOURCE/'StarterWorld.settlements.json').read_text(encoding='utf-8'))['settlements'] if p['target_id']=='S03');assert P['stable_id']=='S04'
cx,cy=next(p['center_m'][:2] for p in C.PLAN['places'] if p['id']=='S04')
TAG='ProjectJ.Starter.V4.Forestry.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S04/ForestryBlockout')
assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
STONE=unreal.load_asset('/Game/World/Starter/Materials/M_StarterBridgeStone');assert STONE
REPORT=dict(phase='QUEUED',stable_id='S04',target_id='S03',houses=[],groups=[],entries=[],completed=[],height_sha256=hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest(),profile_sha256=hashlib.sha256((SOURCE/'StarterWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
def record(phase):
    REPORT.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
def tag(a):
    a.set_editor_property('tags',[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S04'),unreal.Name('ProjectJ.Target.S03')]);return a
def house(data):
    dx,dy=data['offset_m'];h=dict(data,x=cx+dx,y=cy+dy,yaw=math.degrees(math.atan2(-dx,dy)),roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile'])
    a,r=A.supported_house('S04_Forestry_'+data['id'],h,C.height,C,STONE);tag(a);REPORT['houses'].append(r)
def group(name,x,y,sets,collision=False):
    z=C.height(x,y);a=A.spawn('S04_Forestry_'+name,'/Engine/BasicShapes/Cube',x,y,z);a.static_mesh_component.set_static_mesh(None)
    a.set_actor_location(A.point(x,y,z),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));tag(a)
    records=[]
    for part,path,points in sets:
        mesh=C.mesh(path);comp=C.component(a,unreal.HierarchicalInstancedStaticMeshComponent,part);comp.set_static_mesh(mesh)
        for i,slot in enumerate(mesh.static_materials):
            if slot.material_interface:comp.set_material(i,A.instanced_material(slot.material_interface))
        comp.set_collision_profile_name('BlockAll' if collision else 'NoCollision')
        for px,py,yaw,scale in points:
            t=C.centered_transform(mesh,unreal.Vector((px-x)*100,(py-y)*100,(C.height(px,py)-z-.035)*100),yaw,(scale,scale,scale))
            comp.add_instance(t,False)
        records.append(dict(component=part,mesh=path,instances=len(points)))
    REPORT['groups'].append(dict(label=a.get_actor_label(),sets=records))
def yard(yard):
    x,y=cx+yard['offset_m'][0],cy+yard['offset_m'][1];rng=random.Random(P['seed']+len(yard['id']))
    logs=[(x-10+i*4,y-4+j*5,rng.uniform(-10,10),rng.uniform(.8,1.1)) for i in range(5) for j in range(2)]
    stumps=[(x-10+i*4,y+7,rng.uniform(0,360),rng.uniform(.7,1.05)) for i in range(5)]
    group(yard['id'],x,y,[('FirewoodStacks','/Game/Map_Resource/SkyTown/Meshes/SM_Firewood01_Item01',logs),('Stumps','/Game/Megascans/3D_Assets/TreeStumpA/SM_TreeStumpA_00',stumps)])
    tag(A.spawn('S04_Forestry_'+yard['id']+'_Cart','/Game/Map_Resource/SkyTown/Meshes/SM_Cart01',x+8,y-9,C.height(x+8,y-9),25))
def trees():
    rng=random.Random(P['seed']);points=[]
    for i in range(1500):
        x,y=rng.uniform(-182,182),rng.uniform(-182,182)
        if math.hypot(x,y)>182 or math.hypot(x,y)<14:continue
        if any(C.distance_path(x,y,r['points_relative_m'])<r['width_m']/2+6 for r in P['streets']):continue
        if any(math.dist([x,y],h['offset_m'])<math.hypot(h['width'],h['depth'])/2+9 for h in P['houses']):continue
        if any(abs(x-f['offset_m'][0])<f['width_m']/2+7 and abs(y-f['offset_m'][1])<f['depth_m']/2+7 for f in P['work_yards']):continue
        if any(math.hypot(x-p[0],y-p[1])<11 for p in points):continue
        points.append((x,y,rng.uniform(0,360),rng.uniform(.65,1.1)))
        if len(points)==120:break
    paths=['/Game/ElderboomVillage/Meshes/SM_BlackAlder_Forest_01','/Game/ElderboomVillage/Meshes/SM_BlackAlder_Forest_05_PP','/Game/ElderboomVillage/Meshes/SM_BlackAlder_Field_04']
    group('RetainedCanopy',cx,cy,[('Canopy'+str(i),path,[(cx+x,cy+y,yaw,scale) for x,y,yaw,scale in points[i::3]]) for i,path in enumerate(paths)],True)
def entries():
    for name,offset in [('West',P['streets'][0]['points_relative_m'][0]),('East',P['streets'][0]['points_relative_m'][-1])]:
        x,y=cx+offset[0],cy+offset[1];a=A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(x,y,C.height(x,y)+.1));assert a
        a.set_actor_label('Entry_S04_'+name);a.set_folder_path(A.folder);tag(a);a.tags=list(a.tags)+[unreal.Name('ProjectJ.Entry.S04.'+name)]
        a.set_editor_property('is_spatially_loaded',True);a.set_editor_property('is_editor_only_actor',False);assert not a.get_editor_property('replicates')
        REPORT['entries'].append(dict(label=a.get_actor_label(),path=a.get_path_name(),location_cm=list(a.get_actor_location().to_tuple())))
tasks=[(h['id'],lambda h=h:house(h)) for h in P['houses']]+[(f['id'],lambda f=f:yard(f)) for f in P['work_yards']]+[('Canopy',trees),('Entries',entries)]
index=0;busy=False
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
    except Exception:
        REPORT['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(REPORT['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
