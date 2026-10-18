"""v4 Phase 1: actual farm settlement from the authored archetype/profile.

Adds owned actors only. Preserves S02's stable identity, current marker, native
WorldDefinition and every terrain sample. No runtime construction or new manager.
"""
import hashlib,importlib.util,json,math,random,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
FILE=OUT/'farm-village-build.json'
OLD=json.loads(FILE.read_text(encoding='utf-8')) if FILE.exists() else None
if OLD:assert OLD['phase']=='ERROR' and (OUT/'farm-plot-layout.json').exists(),'Inspect unknown or completed authoring before recovery'
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
C=module('corridor','Build-StarterCorridor.py');S=module('settlement','Starter-SettlementAuthoring.py')
profiles=json.loads((SOURCE/'StarterWorld.settlements.json').read_text(encoding='utf-8'))
P=next(p for p in profiles['settlements'] if p['target_id']=='S02')
registry=json.loads((SOURCE/'StarterWorld.locations.json').read_text(encoding='utf-8'))
assert next(p for p in registry['locations'] if p['target_id']=='S02')['stable_id']==P['stable_id']=='S02'
place=next(p for p in C.PLAN['places'] if p['id']==P['stable_id']);cx,cy=place['center_m'][:2]
TAG='ProjectJ.Starter.V4.Farm.v1';A=S.SettlementAuthoring(TAG,'StarterRegion/Settlements/S02/FarmBlockout')
owned={a.get_actor_label():a for a in A.subsystem.get_all_level_actors() if TAG in [str(t) for t in a.tags]}
if OLD:
    house_ids={h['id'] for h in P['houses']}
    expected={'S02_Farm_'+key if key in house_ids or key=='Well' else 'S02_Field_'+key for key in OLD['completed']}
    assert set(owned)==expected=={r['label'] for r in OLD['actors']},'Partial failed task or manual additions require inspection'
    for r in OLD['actors']:
        actor=owned[r['label']];assert actor.get_path_name()==r['path']
        assert (actor.get_actor_location()-unreal.Vector(*r['location_cm'])).length()<.01,'Preserve unexpected manual placement'
else:assert not owned
STONE=unreal.load_asset('/Game/World/Starter/Materials/M_StarterBridgeStone');CUBE=C.mesh('/Engine/BasicShapes/Cube')
WOOD=unreal.load_asset('/Game/Map_Resource/MedCastle/Materials/MI_WoodenPlanks2');assert WOOD
REPORT=dict(phase='QUEUED',stable_id='S02',target_id='S02',completed=[],actors=[],houses=[],fields=[],height_sha256=hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest(),
            profile_sha256=hashlib.sha256((SOURCE/'StarterWorld.settlements.json').read_bytes()).hexdigest(),art_complete=False,navigation_verified=False,performance_measured=False)
if OLD:
    REPORT=OLD;REPORT.setdefault('recovery_history',[]).append(dict(error=REPORT.pop('error'),previous_profile=REPORT['profile_sha256']))
    REPORT['profile_sha256']=hashlib.sha256((SOURCE/'StarterWorld.settlements.json').read_bytes()).hexdigest()
    A.created=REPORT['actors']
def record(phase):
    REPORT.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
def tagged(actor):
    actor.set_editor_property('tags',[unreal.Name(TAG),unreal.Name('ProjectJ.Location.S02'),unreal.Name('ProjectJ.Target.S02')]);return actor
def empty(label,x,y,z,yaw=0):
    actor=A.spawn(label,'/Engine/BasicShapes/Cube',x,y,z,yaw)
    actor.static_mesh_component.set_static_mesh(None)
    actor.set_actor_transform(unreal.Transform(location=C.point(x,y,z),rotation=unreal.Rotator(pitch=0,yaw=yaw,roll=0),scale=unreal.Vector(1,1,1)),False,False)
    A.created[-1].update(mesh=None,location_cm=list(actor.get_actor_location().to_tuple()))
    return tagged(actor)
def instances(owner,name,mesh):
    materials=[A.instanced_material(s.material_interface) if s.material_interface else None for s in mesh.static_materials]
    c=C.component(owner,unreal.HierarchicalInstancedStaticMeshComponent,name)
    for i,material in enumerate(materials):
        if material:c.set_material(i,material)
    c.set_static_mesh(mesh);c.set_collision_profile_name('NoCollision')
    return c
def build_house(data):
    x,y=cx+data['offset_m'][0],cy+data['offset_m'][1];yaw=math.degrees(math.atan2(cx-x,y-cy))
    w,d=data['width'],data['depth']
    h=dict(data,x=x,y=y,yaw=yaw,roof_style='intact_thatch_gable',roof_contact_profile=P['roof_contact_profile'])
    actor,placement=A.supported_house('S02_Farm_'+data['id'],h,C.height,C,STONE);tagged(actor);REPORT['houses'].append(placement)
    if data['id']=='Mill':
        rotor=instances(actor,'SlattedMillRotor',CUBE);rotor.set_material(0,A.instanced_material(WOOD));config=P['mill_rotor'];count=0
        def beam(r,t,length,width,angle):
            nonlocal count
            rotation=unreal.Rotator(pitch=angle,yaw=0,roll=0)
            transform=unreal.Transform(rotation=rotation,scale=unreal.Vector(length,.08,width))
            transform.translation=unreal.Vector((r*math.cos(math.radians(angle))-t*math.sin(math.radians(angle)))*100,-d*50-85,(config['hub_height_m']+r*math.sin(math.radians(angle))+t*math.cos(math.radians(angle)))*100)
            rotor.add_instance(transform,False);count+=1
        for angle in config['angles_degrees']:
            outer,inner=config['outer_radius_m'],config['inner_radius_m'];length=outer-inner
            beam(outer/2,0,outer,.10,angle)
            for sign in [-1,1]:beam((inner+outer)/2,sign*.6,length,.08,angle)
            for i in range(20):beam(inner+length*(i+.5)/20,0,.06,1.1,angle)
        REPORT['mill_rotor_instances']=count
def build_field(data):
    x,y=cx+data['offset_m'][0],cy+data['offset_m'][1];z=C.height(x,y);w,d=data['width_m'],data['depth_m']
    actor=empty('S02_Field_'+data['id'],x,y,z)
    wheat=C.mesh('/Game/Megascans/3D_Plants/WheatGrassB/SM_WheatGrass01_V1');c=instances(actor,'Wheat',wheat)
    rng=random.Random(P['seed']+sum(map(ord,data['id'])));count=0
    for i in range(int(w/1.5)):
        for j in range(int(d/1.5)):
            if rng.random()<.07:continue
            u=-w/2+(i+.5)*1.5+rng.uniform(-.18,.18);v=-d/2+(j+.5)*1.5+rng.uniform(-.18,.18)
            if C.distance_path(x+u,y+v,[[cx+q[0],cy+q[1]] for q in P['streets'][2]['points_relative_m']])<2.5:continue
            scale=rng.uniform(.72,.94);t=C.centered_transform(wheat,unreal.Vector(u*100,v*100,(C.height(x+u,y+v)-z-.035)*100),rng.uniform(0,360),(scale,scale,scale))
            c.add_instance(t,False);count+=1
    REPORT['fields'].append(dict(id=data['id'],label=actor.get_actor_label(),instances=count))
def build_well():
    actor=tagged(A.spawn('S02_Farm_Well','/Game/Meshes/Well/SM_MainWell',cx+8,cy-5,C.height(cx+8,cy-5),25))
    for name,path in [('StoneBase','/Game/Meshes/Well/WellBase/SM_WellBase'),('Shingles','/Game/Meshes/Well/SM_WellShingles'),('Crank','/Game/Meshes/Well/SM_WellDetailPieces')]:A.add_static_component(actor,path,name)
tasks=[(h['id'],lambda h=h:build_house(h)) for h in P['houses']]+[(f['id'],lambda f=f:build_field(f)) for f in P['fields']]+[('Well',build_well)]
tasks=[(key,action) for key,action in tasks if key not in REPORT['completed']]
index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):
            A.save();assert hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest()==REPORT['height_sha256']
            REPORT.update(height_modified=False,existing_actors_modified=False,provider_modified=False);record('COMPLETE')
            unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];record('APPLYING_'+key);action();REPORT['completed'].append(key);index+=1
        if index%2==0 or index==len(tasks):A.save()
        record('READY')
    except Exception:
        REPORT['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(REPORT['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
