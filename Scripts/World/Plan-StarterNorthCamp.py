"""Survey a small boundary garrison without changing the source mountains."""
import functools,heapq,importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';REPORT=OUT/'north-camp-layout.json'
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');support=module('support','Starter-SettlementSupport.py')
plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'));profiles=json.loads((SOURCE/'StarterWorld.settlements.json').read_text(encoding='utf-8'));edges=json.loads((SOURCE/'StarterWorld.edges.json').read_text(encoding='utf-8'))
assert not REPORT.exists() and len(plan['places'])==19 and not any(p['id']=='S08' for p in plan['places'])
cx,cy=3440,8030;gate=[3352,8048];port=[3352,8064];start=[3500,8040];step=2
offsets=[(i,j) for i in range(-3,4) for j in range(-3,4) if (i or j) and math.gcd(abs(i),abs(j))==1]
@functools.lru_cache(None)
def h(q):return T.height(*q)
@functools.lru_cache(None)
def cost(a,b):
    if not (3300<=b[0]<=3540 and 7980<=b[1]<=8060):return None
    d=math.dist(a,b);dx,dy=b[0]-a[0],b[1]-a[1];n=math.ceil(d);last=h(a);maximum=0
    for i in range(1,n+1):
        x,y=a[0]+dx*i/n,a[1]+dy*i/n;z=T.height(x,y);grade=abs(z-last)/(d/n);last=z;maximum=max(maximum,grade)
        if grade>.22:return None
        if abs(T.height(x-dy/d*1.6,y+dx/d*1.6)-T.height(x+dy/d*1.6,y-dx/d*1.6))>1.6:return None
    return d*(1+maximum*2)
def solve(a,b):
    a,b=tuple(a),tuple(b);queue=[(0,0,a)];scores={a:0};parents={}
    while queue:
        _,g,q=heapq.heappop(queue)
        if g!=scores.get(q):continue
        if q==b:
            route=[q]
            while route[-1]!=a:route.append(parents[route[-1]])
            return [list(v) for v in reversed(route)]
        for dx,dy in offsets:
            v=(q[0]+dx*step,q[1]+dy*step);c=cost(q,v)
            if c is None or g+c>=scores.get(v,1e30):continue
            scores[v]=g+c;parents[v]=q;heapq.heappush(queue,(g+c+math.dist(v,b),g+c,v))
    raise AssertionError(('No natural camp approach',a,b))
route=solve(start,gate)+[port]
streets=[dict(id='NorthRidgeApproach',width_m=3.2,points_relative_m=[[x-cx,y-cy] for x,y in route])]
def closest(x,y):
    best=(1e9,None)
    for a,b in zip(route,route[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(dx*dx+dy*dy,1e-9)));p=[a[0]+dx*t,a[1]+dy*t];d=math.dist([x,y],p)
        if d<best[0]:best=(d,p)
    return best
specs=[('Quartermaster',6,8,1,[3400,8030],None,1),('GuardBarracks',6,8,1,[3430,8030],None,1),('CommandTent',6.395,6.232,1,[3470,8032],1,.6),('PatrolTentA',3.442,5.67,1,[3490,8044],3,.65),('PatrolTentB',3.149,4.106,1,[3450,8048],4,.8),('PatrolTentC',3.442,5.67,1,[3520,8046],3,.65)]
buildings=[];evidence=[]
for name,w,d,storeys,preferred,tent,scale in specs:
    candidates=[];r=math.hypot(w,d)/2
    for dx in range(-24,25,2):
        for dy in range(-18,19,2):
            x,y=preferred[0]+dx,preferred[1]+dy
            if not (3310<x<3540 and 8000<y<8056) or math.dist([x,y],gate)<r+17:continue
            if any(math.dist([x,y],q['center_m'][:2])<r+math.hypot(q['width'],q['depth'])/2+4 for q in buildings):continue
            distance,p=closest(x,y)
            if distance<r+2 or distance>30:continue
            yaw=math.atan2(p[0]-x,y-p[1]);co,si=math.cos(yaw),math.sin(yaw)
            samples=[]
            for u in [-w/2,0,w/2]:
                for v in [-d/2,0,d/2]:
                    px,py=x+co*u-si*v,y+si*u+co*v
                    if not (0<=px<=8064 and 0<=py<=8064):break
                    samples.append(T.height(px,py))
            if len(samples)!=9:continue
            relief=max(samples)-min(samples)
            if relief>2.3:continue
            try:access=support.plan_access_stairs(dict(id=name,center_m=[x,y,max(samples)+.06],yaw=math.degrees(yaw),depth=d),T.height)
            except AssertionError:continue
            candidates.append((math.hypot(dx,dy)+relief*5,x,y,relief,math.degrees(yaw),max(samples)+.06,min(samples)-.25))
    assert candidates,('No terrain supported plot',name)
    _,x,y,relief,yaw,floor,bottom=min(candidates)
    q=dict(id=name,offset_m=[x-cx,y-cy],center_m=[x,y,floor],width=w,depth=d,storeys=storeys,yaw_degrees=yaw,foundation_bottom_m=bottom)
    if tent:q.update(mesh='/Game/Map_Resource/SkyTown/Meshes/SM_Tents01_Item0'+str(tent),mesh_scale=scale)
    buildings.append(q);evidence.append(dict(id=name,footprint_relief_m=relief,center_m=q['center_m']))
towers=[]
for name,x in [('WestTower',3343),('EastTower',3361)]:
    y=gate[1];half=3.962
    samples=[T.height(x+u,y+v) for u in [-half,0,half] for v in [-half,0,half]];assert max(samples)-min(samples)<2.5
    towers.append(dict(id=name,center_m=[x,y,max(samples)+.06],width=half*2,depth=half*2,foundation_bottom_m=min(samples)-.25,scale=.5))
place=dict(id='S08',name='north_gateway_camp',kind='border_camp',levels=[25,30],center_m=[cx,cy,T.height(cx,cy)],radius_m=150,boundary_m=[[3290,7910],[3590,7910],[3590,8064],[3290,8064]])
profile=dict(stable_id='S08',target_id='S09',archetype='north_gateway_camp',seed=103109,stage='PLANNED',center_source='StarterWorld.plan.json#places/S08',houses=[q for q in buildings if 'mesh' not in q],tents=[q for q in buildings if 'mesh' in q],streets=streets,fields=[],gate=dict(center_m=[*gate,T.height(*gate)],towers=towers,clear_width_m=10.075,connection_id='E_N_Road'),walk_route_points_m=route[:-1]+[[port[0],8058]],functional_walk_timeout_s=120,roof_contact_profile=profiles['settlements'][0]['roof_contact_profile'],terrain_policy='Natural ridge, individual foundations, unchanged original height',regional_road_connection='E_N_Road; southern regional approach remains a review candidate')
previous=next(p for p in plan['connections'] if p['id']=='E_N_Road').copy();new_port=[*port,T.height(*port)]
for document in [plan,edges]:
    c=next(p for p in document['connections'] if p['id']=='E_N_Road');c['position_m']=new_port
road=next(p for p in plan['roads'] if p['id']=='Road_NorthExit');oldroad=json.loads(json.dumps(road));road['points']=[*road['points'][:-1],*[ [x,y,T.height(x,y)] for x,y in route]];road['status']='ROUTE_CANDIDATE_REGIONAL_SLOPE_UNVERIFIED_LOCAL_CAMP_SAMPLED'
profile['gate']['port_position_m']=new_port
plan['places'].append(place);profiles['settlements'].append(profile)
report=dict(place=place,buildings=evidence,gate=profile['gate'],route_points_m=route,local_centerline_grade_limit=.22,lateral_height_difference_limit_m=1.6,route_scope='source samples only; riding/Nav and regional southern approach unverified',previous_connection=previous,connection=next(p for p in plan['connections'] if p['id']=='E_N_Road'),previous_road=oldroad,height_sha256=T.manifest['terrain_profile']['sha256'],height_modified=False,native_registered=False)
backup=ROOT/'Saved/StarterWorld/BeforeNorthCamp';backup.mkdir(parents=True,exist_ok=True)
for name in ['StarterWorld.plan.json','StarterWorld.settlements.json','StarterWorld.edges.json']:
    assert not (backup/name).exists();shutil.copy2(SOURCE/name,backup/name)
for name,value in [('StarterWorld.plan.json',plan),('StarterWorld.settlements.json',profiles),('StarterWorld.edges.json',edges)]:
    (SOURCE/name).write_text(json.dumps(value,indent=2),encoding='utf-8')
REPORT.write_text(json.dumps(report,indent=2),encoding='utf-8');print('North camp planned:',len(buildings),'buildings;',len(route),'route points;',new_port)
