"""Whole-region contour routes with explicit existing/proposed bridge portals.

Reads preserved 2m heights. Every grid edge is screened at native 2m spacing;
proposed bridge use is reported as construction work, never as walkable ground.
No plan, DA, height or Unreal Actor is changed by this survey.
"""
import hashlib,heapq,json,math,time
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source'
OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'world-structure-routes.json'
survey=json.loads((OUT/'world-structure-sites.json').read_text(encoding='utf-8'))
height_file=SOURCE/'HM_MainWorld.r16';digest=hashlib.sha256(height_file.read_bytes()).hexdigest()
assert digest==survey['height_sha256']
full=(np.fromfile(height_file,dtype='<u2').reshape(4033,4033).astype('f4')-32768)*.0625
cache=np.load(OUT/'world-structure-grid.npz');h,slope,water=(cache[n] for n in ['height','slope','water'])
plan=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'))
step=8;size=h.shape[0];allowed=(h>2)&(water>8)&(slope<.75)
nodes={p['id']:p for p in survey['sites'] if 'center_m' in p}
assert len(nodes)==20,'Resolve missing sites before global routing'
# Actual city circulation is north of the preserved v3 identity anchor.
nodes['S01']=dict(nodes['S01'],center_m=[4150,4280,float(full[2140,2075])])
def cell(point):return round(point[1]/step),round(point[0]/step)
def xyz(c):return [c[1]*step,c[0]*step,float(h[c])]
def snap(point,radius=3):
    j,i=cell(point);candidates=[(jj,ii) for jj in range(max(1,j-radius),min(size-1,j+radius+1)) for ii in range(max(1,i-radius),min(size-1,i+radius+1)) if allowed[jj,ii]]
    if not candidates:return None
    return min(candidates,key=lambda p:math.hypot(p[1]*step-point[0],p[0]*step-point[1]))
def grade_grid(direction):
    fields=[]
    for k in range(5):
        if direction=='E':a=full[::4,k::4][:size,:size-1]
        elif direction=='N':a=full[k::4,::4][:size-1,:size]
        elif direction=='NE':a=full[k::4,k::4][:size-1,:size-1]
        else:a=full[k::4,4-k::4][:size-1,:size-1]
        fields.append(a)
    return np.max(np.abs(np.diff(np.stack(fields),axis=0)),axis=0)/(2*math.sqrt(2) if direction in ['NE','NW'] else 2)
grades={name:grade_grid(name) for name in ['E','N','NE','NW']}
def edge_grade(j,i,dj,di):
    if dj==0:return grades['E'][j,i if di==1 else i-1]
    if di==0:return grades['N'][j if dj==1 else j-1,i]
    if di==dj:return grades['NE'][j if dj==1 else j-1,i if di==1 else i-1]
    return grades['NW'][j if dj==1 else j-1,i-1 if di==-1 else i]
portals=[];links={}
def add_bridge(identifier,a,b,existing=False):
    ca,cb=snap(a),snap(b)
    if ca is None or cb is None or ca==cb:return
    aa,bb=xyz(ca),xyz(cb);length=math.dist(aa[:2],bb[:2]);rise=abs(aa[2]-bb[2])/length
    if not existing and (rise>.18 or length>155 or min(aa[2],bb[2])<4):return
    p=dict(id=identifier,a_m=aa,b_m=bb,length_m=length,bank_grade=rise,existing=existing,
           status='SAVED_BRIDGE_DECK_VERIFIED' if existing else 'PROPOSED_BRIDGE_REQUIRES_AUTHORING_AND_COLLISION')
    index=len(portals);portals.append(p)
    links.setdefault(ca,[]).append((cb,index));links.setdefault(cb,[]).append((ca,index))
bridge=json.loads((OUT.parent/'corridor-build.json').read_text(encoding='utf-8'))['bridge']
cx,cy=bridge['center_m'];angle=math.radians(bridge['heading']);half=bridge['span_m']/2
add_bridge('S01_LakeOut_Existing',[cx-math.cos(angle)*half,cy-math.sin(angle)*half],[cx+math.cos(angle)*half,cy+math.sin(angle)*half],True)
for river in plan['rivers']:
    traversed=0;next_crossing=180
    for a,b in zip(river['points'],river['points'][1:]):
        length=math.dist(a[:2],b[:2]);traversed+=length
        if traversed<next_crossing:continue
        next_crossing=traversed+320
        nx,ny=-(b[1]-a[1])/length,(b[0]-a[0])/length;half=(river['width_m']+64)/2
        add_bridge(river['id']+'_'+str(round(traversed)),[b[0]+nx*half,b[1]+ny*half],[b[0]-nx*half,b[1]-ny*half])
route_specs=[('S01','S00','main'),('S00','S02','main'),('S02','S03','main'),('S01','S03','main'),('S03','S05','mountain_stair'),
             ('S05','B01','mountain_stair'),('S01','S07','main'),('S07','S04','main'),('S04','H09','trail'),('H09','B02','trail'),
             ('S01','S08','main'),('S08','S06','main'),('S06','H07','main'),('S00','H01','trail'),('S02','H02','trail'),
             ('S07','H03','trail'),('S03','H04','trail'),('S04','H05','trail'),('S08','H06','trail'),('S05','H08','mountain_stair')]
directions=[(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)]
def solve(aid,bid,kind):
    a,b=snap(nodes[aid]['center_m']),snap(nodes[bid]['center_m'])
    stair=kind=='mountain_stair';route_allowed=((h>2)&(water>8)&(slope<1.25)) if stair else allowed
    result=dict(id=aid+'_'+bid,from_id=aid,to_id=bid,kind=kind,status='NO_ROUTE_WITHIN_CONSTRAINTS',max_ground_grade=.22 if kind=='main' else .60 if stair else .33,
                movement_intent='Foot traffic on constructed steps/landings; riding route not claimed' if stair else 'Terrain corridor; native movement/width validation pending')
    if a is None or b is None:return result
    best={a:0.};previous={};queue=[(math.dist(a,b)*step,0.,a)];expanded=0;found=False
    while queue and expanded<1000000:
        _,g,u=heapq.heappop(queue)
        if g>best.get(u,1e30)+1e-6:continue
        if u==b:found=True;break
        expanded+=1;j,i=u
        neighbors=[]
        for dj,di in directions:
            jj,ii=j+dj,i+di
            if not(0<=jj<size and 0<=ii<size) or not route_allowed[jj,ii]:continue
            if dj and di and (not route_allowed[j,ii] or not route_allowed[jj,i]):continue
            grade=float(edge_grade(j,i,dj,di))
            if grade>result['max_ground_grade']:continue
            length=step*math.hypot(dj,di)
            neighbors.append(((jj,ii),length*(1+12*grade*grade+.6*float(slope[jj,ii])**2+(2 if stair and grade>.33 else 0)),None))
        for v,p in links.get(u,[]):neighbors.append((v,portals[p]['length_m']*(1.1 if portals[p]['existing'] else 5),p))
        for v,weight,p in neighbors:
            ng=g+weight
            if ng>=best.get(v,1e30):continue
            best[v]=ng;previous[v]=(u,p);heapq.heappush(queue,(ng+math.dist(v,b)*step,ng,v))
    result['expanded_nodes']=expanded
    if not found:return result
    path=[b];used=[];edge_types=[]
    while path[-1]!=a:
        u,p=previous[path[-1]];edge_types.append(p);path.append(u)
        if p is not None:used.append(p)
    path.reverse();edge_types.reverse();points=[xyz(c) for c in path]
    ground=[];stair_segments=[]
    for u,v,p in zip(path,path[1:],edge_types):
        if p is None:
            grade=float(edge_grade(u[0],u[1],v[0]-u[0],v[1]-u[1]));ground.append(grade)
            if grade>.33:stair_segments.append(dict(from_m=xyz(u),to_m=xyz(v),maximum_grade=grade))
    result.update(status='GROUND_AND_BRIDGE_CORRIDOR_CANDIDATE',points_m=points,bridge_ids=[portals[p]['id'] for p in sorted(set(used))],
                  bridge_edges=[dict(segment_index=i,bridge_id=portals[p]['id']) for i,p in enumerate(edge_types) if p is not None],
                  length_m=sum(math.dist(p[:2],q[:2]) for p,q in zip(points,points[1:])),maximum_native_2m_ground_grade=max(ground,default=0),
                  navigation_verified=False,width_clearance_verified=False,endpoint_connection_verified=False)
    result.update(stair_segments=stair_segments,stair_construction_required=bool(stair_segments),stair_riser_limit_cm=18,stair_minimum_tread_cm=30,stair_width_m=2.4)
    return result
report=dict(height_sha256=digest,height_modified=False,grid_m=8,native_grade_sampling_m=2,routes=[],nodes=nodes,bridges=portals)
for a,b,kind in route_specs:
    r=solve(a,b,kind);report['routes'].append(r)
    FILE.write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(json.dumps({k:r[k] for k in ['id','status','expanded_nodes','length_m','bridge_ids'] if k in r}),flush=True)
assert hashlib.sha256(height_file.read_bytes()).hexdigest()==digest
report['survey_finished']=True;FILE.write_text(json.dumps(report,indent=2),encoding='utf-8')
