"""Fit a terraced mining village and contour routes without editing the mountain."""
import functools,hashlib,heapq,importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=SOURCE/'StarterWorld.settlements.json';REPORT=OUT/'mountain-village-layout.json';assert not REPORT.exists()
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Starter-TerrainProfile.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);T=m.TerrainProfile(SOURCE/'StarterRegion.manifest.json')
data=json.loads(FILE.read_text(encoding='utf-8'));assert not any(p['target_id']=='S06' for p in data['settlements']);place=next(v for v in json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'))['places'] if v['id']=='S05');cx,cy=place['center_m'][:2]
step=4;radius=184;offsets=[(i,j) for i in range(-3,4) for j in range(-3,4) if (i or j) and math.gcd(abs(i),abs(j))==1]
def snap(p):return tuple(round((p[i]-[cx,cy][i])/step) for i in range(2))
def xy(node):return cx+node[0]*step,cy+node[1]*step
@functools.lru_cache(None)
def height(node):return T.height(*xy(node))
@functools.lru_cache(None)
def edge(a,b):
    ax,ay=xy(a);bx,by=xy(b);distance=math.hypot(bx-ax,by-ay)
    if math.hypot(bx-cx,by-cy)>radius:return None
    if abs(height(b)-height(a))/distance>.20:return None
    n=math.ceil(distance);previous=height(a);maximum=0
    for i in range(1,n+1):
        t=i/n;x,y=ax+(bx-ax)*t,ay+(by-ay)*t;z=T.height(x,y);grade=abs(z-previous)/(distance/n);maximum=max(maximum,grade);previous=z
        if grade>.20:return None
    return distance*(1+maximum*2)
def solve(start,goal):
    start,goal=snap(start),snap(goal);queue=[(0,0,start)];cost={start:0};parent={}
    while queue:
        _,g,a=heapq.heappop(queue)
        if g!=cost.get(a):continue
        if a==goal:
            nodes=[a]
            while nodes[-1]!=start:nodes.append(parent[nodes[-1]])
            nodes.reverse();return [list(xy(n)) for n in nodes]
        for dx,dy in offsets:
            b=(a[0]+dx,a[1]+dy);value=edge(a,b)
            if value is None:continue
            ng=g+value
            if ng>=cost.get(b,1e30):continue
            cost[b]=ng;parent[b]=a;heapq.heappush(queue,(ng+math.dist(xy(b),xy(goal)),ng,b))
    raise AssertionError(('No preserved-terrain contour route',xy(start),xy(goal)))
anchor=[2472,5968];west=[2420,6000];north=[2512,6156];workshop=[2496,6008];view_approach=[2516,5980]
definitions=[('WestContour',west,anchor,3.6),('NorthHairpins',north,anchor,3.2),('WorkshopLane',anchor,workshop,3.2),('LookoutPath',anchor,view_approach,2.6)]
streets=[];route_reports=[]
for name,a,b,width in definitions:
    points=solve(a,b);streets.append(dict(id=name,width_m=width,points_relative_m=[[x-cx,y-cy] for x,y in points]));length=sum(math.dist(a,b) for a,b in zip(points,points[1:]));route_reports.append(dict(id=name,length_m=length,vertices=len(points),sample_grade_limit=.20,grade_scope='1m centerline source samples; lateral width/riding unverified'));print(name,round(length,1),len(points),flush=True)
def closest(x,y):
    best=(1e9,None)
    for r in streets:
        points=r['points_relative_m']
        for a,b in zip(points,points[1:]):
            dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(dx*dx+dy*dy,1e-9)));q=[a[0]+t*dx,a[1]+t*dy];distance=math.dist([x,y],q)
            if distance<best[0]:best=(distance,q)
    return best
specs=[('OreStore',8,10,1,[-115,20]),('MountainInn',7,9,2,[-145,-25]),('Smithy',7,9,1,[-105,-65])]
specs += [('Home%02d'%i,5+(i%2)*.5,7,1,q) for i,q in enumerate([[-150,-60],[-120,-80],[-85,-40],[-90,30],[-150,50],[-105,70],[-60,65],[-40,90]],1)]
houses=[];evidence=[]
for name,w,d,storeys,preferred in specs:
    candidates=[];r=math.hypot(w,d)/2
    for dx in range(-50,51,2):
        for dy in range(-50,51,2):
            x,y=preferred[0]+dx,preferred[1]+dy
            if math.hypot(x,y)>172:continue
            if any(math.dist([x,y],h['offset_m'])<r+math.hypot(h['width'],h['depth'])/2+3 for h in houses):continue
            distance,q=closest(x,y)
            if distance<r+4 or distance>30:continue
            if math.dist([x,y],[view_approach[0]-cx,view_approach[1]-cy])<r+15:continue
            if math.dist([x,y],[anchor[0]-cx,anchor[1]-cy])<r+10:continue
            yaw=math.atan2(q[0]-x,y-q[1]);co,si=math.cos(yaw),math.sin(yaw);samples=[T.height(cx+x+co*u-si*v,cy+y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]];relief=max(samples)-min(samples)
            if relief>=2.5:continue
            candidates.append((math.hypot(dx,dy)+relief*6,x,y,relief,math.degrees(yaw)))
    assert candidates,('No natural supported mountain plot',name)
    _,x,y,relief,yaw=min(candidates);houses.append(dict(id=name,offset_m=[x,y],width=w,depth=d,storeys=storeys,yaw_degrees=yaw));evidence.append(dict(id=name,offset_m=[x,y],footprint_relief_m=relief,ground_m=T.height(cx+x,cy+y)))
walk=[[cx+q[0],cy+q[1]] for q in streets[0]['points_relative_m']]+[[cx+q[0],cy+q[1]] for q in streets[3]['points_relative_m'][1:]]+[[2534,5980]];walk_length=sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))
p=dict(stable_id='S05',target_id='S06',archetype='terraced_mining_village',seed=103106,stage='PLANNED',center_source='StarterWorld.plan.json#places/S05',houses=houses,streets=streets,fields=[],plaza=dict(offset_m=[anchor[0]-cx,anchor[1]-cy],radius_m=7,blend_m=2.5),work_yards=[dict(id='OreSorting',offset_m=[-115,2],width_m=15,depth_m=12)],walk_route_points_m=walk,functional_walk_timeout_s=max(60,math.ceil(walk_length/2+20)),lookout=dict(offset_m=[-66,-20],width_m=6,depth_m=6,yaw_degrees=-90,approach_m=view_approach),roof_contact_profile=data['settlements'][0]['roof_contact_profile'],terrain_policy='Preserve original ridges and height; contour routes and individual supported plots')
backup=ROOT/'Saved/StarterWorld/BeforeMountainVillage';backup.mkdir(parents=True,exist_ok=True);assert not (backup/FILE.name).exists();shutil.copy2(FILE,backup/FILE.name);data['settlements'].append(p);FILE.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(houses=evidence,routes=route_reports,walk_length_m=walk_length,height_modified=False,height_sha256=T.manifest['terrain_profile']['sha256']),indent=2),encoding='utf-8');print('Planned mountain buildings',len(houses),'walk m',round(walk_length,1))
