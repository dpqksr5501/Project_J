"""Contour-led capital composition. Reads height only; never flattens terrain.

Produces editable plots and streets. Native collision/navigation and art review
remain separate from this offline footprint/grade screening.
"""
import hashlib,heapq,json,math,random
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source'
DEST=SOURCE/'MainWorld.capital.json'
assert not DEST.exists(),'Preserve designer edits; explicitly revise the existing capital plan'
HFILE=SOURCE/'HM_MainWorld.r16';digest=hashlib.sha256(HFILE.read_bytes()).hexdigest()
H=(np.fromfile(HFILE,dtype='<u2').reshape(4033,4033).astype(float)-32768)*.0625
PLAN=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'))
PAL=json.loads((ROOT/'Saved/Validation/StarterWorld/Phase2/architecture-palette.json').read_text(encoding='utf-8'))
PALETTE={Path(r['path']).name:r for r in PAL if r['available']}
def height(x,y):
    u,v=x/2,y/2;i,j=int(u),int(v);s,t=u-i,v-j
    return float((H[j,i]*(1-s)+H[j,i+1]*s)*(1-t)+(H[j+1,i]*(1-s)+H[j+1,i+1]*s)*t)
def distance(x,y,points):
    best=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(dx*dx+dy*dy,1e-9)))
        best=min(best,math.hypot(x-a[0]-dx*t,y-a[1]-dy*t))
    return best
# Main gate links an offset market square, the artisan lane and a ridge keep.
nodes=dict(gate=[4124.08,4217.91],market=[4150,4280],west=[4005,4330],
           artisans=[4300,4330],ridge=[4320,4500],north=[4200,4530])
step=4;x0,y0=3780,4200;nx,ny=200,105
blocked=np.zeros((ny,nx),bool)
for j in range(ny):
    for i in range(nx):
        x,y=x0+i*step,y0+j*step
        blocked[j,i]=any(distance(x,y,r['points'])<r['width_m']/2+10 for r in PLAN['rivers'])
def route(start,end):
    def cell(p):return round((p[0]-x0)/step),round((p[1]-y0)/step)
    a,b=cell(start),cell(end);queue=[(0,a)];cost={a:0};previous={}
    while queue:
        _,p=heapq.heappop(queue)
        if p==b:break
        px,py=x0+p[0]*step,y0+p[1]*step;z=height(px,py)
        for di,dj in [(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)]:
            q=p[0]+di,p[1]+dj
            if not(0<=q[0]<nx and 0<=q[1]<ny) or blocked[q[1],q[0]]:continue
            d=step*math.hypot(di,dj);grade=abs(height(x0+q[0]*step,y0+q[1]*step)-z)/d
            if grade>.22:continue
            candidate=cost[p]+d*(1+grade*grade*35)
            if candidate>=cost.get(q,1e30):continue
            cost[q]=candidate;previous[q]=p
            heapq.heappush(queue,(candidate+step*math.dist(q,b),q))
    assert b in cost,('No unmodified-terrain street route',start,end)
    path=[b]
    while path[-1]!=a:path.append(previous[path[-1]])
    xy=[[x0+i*step,y0+j*step] for i,j in path[::-1]];xy[0]=start;xy[-1]=end
    return [[*p,height(*p)] for p in xy]
streets=[]
for a,b,w in [('gate','market',9),('market','west',6),('market','artisans',7),('artisans','ridge',7),('ridge','north',5),('north','west',5)]:
    pts=route(nodes[a],nodes[b]);grades=[abs(p[2]-q[2])/math.dist(p[:2],q[:2]) for p,q in zip(pts,pts[1:])]
    streets.append(dict(id=a+'_'+b,width_m=w,points=pts,max_grade=max(grades),status='TERRAIN_SCREENED_NAV_UNVERIFIED'))
rng=random.Random(103044);plots=[]
for street in streets:
    pts=street['points'];travel=0;next_lot=15
    for a,b in zip(pts,pts[1:]):
        d=math.dist(a[:2],b[:2]);travel+=d
        if travel<next_lot:continue
        next_lot=travel+rng.uniform(17,24);ux,uy=(b[0]-a[0])/d,(b[1]-a[1])/d
        for side in [-1,1]:
            kind=rng.choices(['SM_SmallHouse','SM_MiddleHouse','SM_BigHouse'],[5,4,1])[0]
            row=PALETTE[kind];w=row['extent_cm'][0]/50;depth=row['extent_cm'][1]/50
            x,y=b[0]-uy*side*(street['width_m']/2+depth/2+3),b[1]+ux*side*(street['width_m']/2+depth/2+3)
            yaw=math.degrees(math.atan2(uy,ux))+(180 if side==1 else 0)
            if any(math.dist([x,y],n)<24 for n in [nodes['market'],nodes['ridge']]):continue
            if math.dist([x,y],[4100,4200])>415:continue
            if any(distance(x,y,s['points'])<s['width_m']/2+depth/2+1 for s in streets):continue
            if any(math.dist([x,y],p['center_m'][:2])<(w+p['width_m'])/2+3 for p in plots):continue
            c,s=math.cos(math.radians(yaw)),math.sin(math.radians(yaw))
            samples=[height(x+c*u-s*v,y+s*u+c*v) for u in [-w/2,0,w/2] for v in [-depth/2,0,depth/2]]
            if max(samples)-min(samples)>2:continue
            if any(distance(x,y,r['points'])<r['width_m']/2+max(w,depth)/2+12 for r in PLAN['rivers']):continue
            plots.append(dict(id=f'S01_P{len(plots):03}',mesh=row['path'],center_m=[x,y,max(samples)+.06],yaw=yaw,
                              width_m=w,depth_m=depth,foundation_bottom_m=min(samples)-.25,street=street['id']))
assert len(plots)>=20,len(plots)
result=dict(schema=1,seed=103044,height_sha256=digest,height_modified=False,settlement_id='S01',
            design='Contour streets, offset market, artisan quarter, ridge keep; no terrain flattening.',
            nodes=nodes,streets=streets,plots=plots,art_status='FIRST_COMPOSITION_REQUIRES_EDITOR_REVIEW')
DEST.write_text(json.dumps(result,indent=2),encoding='utf-8')
assert hashlib.sha256(HFILE.read_bytes()).hexdigest()==digest
print(json.dumps(dict(plots=len(plots),streets=len(streets),maximum_street_grade=max(s['max_grade'] for s in streets),height_modified=False)))
