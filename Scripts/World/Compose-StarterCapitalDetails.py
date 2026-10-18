"""Add market life and the capital's two architectural anchors; preserve height."""
import array,hashlib,importlib.util,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source'
OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'capital-details.json'
assert not FILE.exists(),'Review completed/partial authoring before explicit recovery'
PLAN=json.loads((SOURCE/'StarterWorld.capital.json').read_text(encoding='utf-8'))
assert hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest()==PLAN['height_sha256']
raw=array.array('H')
with (SOURCE/'HM_StarterRegion.r16').open('rb') as stream:raw.fromfile(stream,4033**2)
def height(x,y):
    u,v=x/2,y/2;i,j=int(u),int(v);s,t=u-i,v-j
    return ((raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t-32768)*.0625
spec=importlib.util.spec_from_file_location('settlement',ROOT/'Scripts/World/Starter-SettlementAuthoring.py')
M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
A=M.SettlementAuthoring('ProjectJ.Starter.CapitalDetails.v1','StarterRegion/Settlements/S01/LandmarksAndMarket')
REPORT=dict(phase='READY',height_modified=False,completed=[])
def record(phase):
    REPORT.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
STONE=unreal.load_asset('/Game/World/Starter/Materials/M_StarterBridgeStone')
CASTLE='/Game/Map_Resource/MedCastle/Mesh/Houses_Towers/'
VILLAGE='/Game/Map_Resource/MedievalVillage/Meshes/'
tasks=[]
def anchor(label,name,x,y,yaw):
    mesh=unreal.load_asset(CASTLE+name);size=mesh.get_bounds().box_extent*2/100
    c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
    samples=[height(x+c*u-s*v,y+s*u+c*v) for u in [-size.x/2,0,size.x/2] for v in [-size.y/2,0,size.y/2]]
    z=max(samples)+.08;bottom=min(samples)-.3
    A.spawn(label+'_Foundation','/Engine/BasicShapes/Cube',x,y,bottom,yaw,(size.x+.3,size.y+.3,z-bottom),STONE)
    A.spawn(label,CASTLE+name,x,y,z,yaw)
tasks.append(('GuildHall',lambda:anchor('S01_GuildHall','SM_MainHouse',4150,4313,0)))
tasks.append(('RidgeKeep',lambda:anchor('S01_RidgeKeep','SM_MainTower2Doors',4320,4500,0)))
for index,(x,y,yaw) in enumerate([(4136,4270,210),(4133,4281,270),(4140,4294,315),(4160,4294,45),(4168,4281,90),(4162,4270,150)]):
    def stall(i=index,x=x,y=y,yaw=yaw):
        z=height(x,y)-.03
        frame=A.spawn(f'S01_MarketStall_{i}',VILLAGE+'Mesh_Components/SM_Stall_001',x,y,z,yaw)
        A.add_static_component(frame,VILLAGE+'Mesh_Components/SM_Stall_Cloth_001','Canvas')
        A.spawn(f'S01_MarketBarrel_{i}',VILLAGE+'Props/SM_Barrel_001',x+1.8,y+.8,height(x+1.8,y+.8),yaw)
    tasks.append((f'Stall_{index}',stall))
for index,(x,y,yaw) in enumerate([(4136,4260,0),(4163,4260,0),(4131,4294,90),(4170,4294,270)]):
    tasks.append((f'Bench_{index}',lambda i=index,x=x,y=y,yaw=yaw:A.spawn(f'S01_MarketBench_{i}',VILLAGE+'Props/SM_Bench_001',x,y,height(x,y),yaw)))
index=0;busy=False
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(tasks):
            A.save();record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];record('CREATING_'+key);action();REPORT['completed'].append(key);index+=1
        if index%3==0:A.save()
        record('READY')
    except Exception:
        REPORT['error']=traceback.format_exc();record('ERROR');unreal.unregister_slate_post_tick_callback(handle)
        unreal.log_error(REPORT['error'])
    finally:busy=False
record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
