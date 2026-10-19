"""Plan an estuary trading harbor inside the preserved S06 location reservation."""
import array,hashlib,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=SOURCE/'MainWorld.settlements.json';REPORT=OUT/'harbor-layout.json'
assert not REPORT.exists();data=json.loads(FILE.read_text(encoding='utf-8'));assert not any(p['target_id']=='S05' for p in data['settlements'])
plan=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'));place=next(p for p in plan['places'] if p['id']=='S06');cx,cy=place['center_m'][:2];river=next(r for r in plan['rivers'] if r['id']=='R_Main')
raw=array.array('H');raw.frombytes((SOURCE/'HM_MainWorld.r16').read_bytes())
def height(x,y):
    u,v=x/2,y/2;i,j=int(u),int(v);s,t=u-i,v-j
    return ((raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t-32768)*.0625
def distance(x,y,points):
    best=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(dx*dx+dy*dy,1e-9)))
        best=min(best,math.hypot(x-a[0]-t*dx,y-a[1]-t*dy))
    return best
flow=[p for p in river['points'] if 1120<p[1]<1220];assert len(flow)>=4
first,last=flow[0],flow[-1];dx,dy=last[0]-first[0],last[1]-first[1];length=math.hypot(dx,dy);normal=[-dy/length,dx/length]
quay_points=[[p[0]+normal[0]*12,p[1]+normal[1]*12] for p in [first,last]];attach=[sum(q[i] for q in quay_points)/2 for i in range(2)];bank=[6550,1230]
assert max(math.dist(q,[cx,cy]) for q in quay_points)<place['radius_m']-8
streets=[dict(id='TradeApproach',width_m=5,points_relative_m=[[0,165],[-15,110],[-30,55],[0,0],[-30,-10],[bank[0]-cx,bank[1]-cy]]),
 dict(id='MarketLane',width_m=3.5,points_relative_m=[[-100,30],[-65,25],[-30,-10],[25,-25],[80,-45],[135,-65]]),
 dict(id='WarehouseService',width_m=4,points_relative_m=[[-30,55],[25,55],[65,40],[100,10],[80,-45]])]
specs=[('BondedStore',8,12,2,[-65,0]),('TimberStore',8,12,1,[-80,55]),('TradeHall',9,12,2,[20,20]),('HarborInn',8,10,2,[50,-8])]
specs += [('Home%02d'%i,6+(i%2),8,1,q) for i,q in enumerate([[5,80],[55,75],[95,55],[130,20],[120,-30],[40,-70],[-20,-65],[-110,65]],1)]
houses=[];evidence=[]
for name,w,d,storeys,preferred in specs:
    candidates=[];radius=math.hypot(w,d)/2
    for dx in range(-60,61,3):
        for dy in range(-60,61,3):
            x,y=preferred[0]+dx,preferred[1]+dy
            if not 18<math.hypot(x,y)<165:continue
            if any(math.dist([x,y],h['offset_m'])<radius+math.hypot(h['width'],h['depth'])/2+4 for h in houses):continue
            if any(distance(x,y,r['points_relative_m'])<radius+r['width_m']/2+1 for r in streets):continue
            if distance(cx+x,cy+y,[bank,attach])<radius+5:continue
            yaw=math.atan2(-x,y);co,si=math.cos(yaw),math.sin(yaw)
            samples=[height(cx+x+co*u-si*v,cy+y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]];relief=max(samples)-min(samples)
            if relief>=2.5 or min(samples)<5:continue
            candidates.append((math.hypot(dx,dy)+relief*6,x,y,relief))
    assert candidates,('No supported dry harbor plot',name)
    _,x,y,relief=min(candidates);houses.append(dict(id=name,offset_m=[x,y],width=w,depth=d,storeys=storeys));evidence.append(dict(id=name,offset_m=[x,y],relief_m=relief))
deck=4.2;bank_top=height(*bank)+.08;grade=abs(bank_top-deck)/math.dist(bank,attach)
bed=[]
for i in range(25):
    t=i/24;q=[quay_points[0][k]*(1-t)+quay_points[1][k]*t for k in range(2)];water=first[2]*(1-t)+last[2]*t
    for lateral in [-3,0,3]:
        x,y=q[0]+normal[0]*lateral,q[1]+normal[1]*lateral;h=height(x,y);assert h<deck-.4;bed.append(dict(position_m=[x,y],bed_m=h,estimated_water_m=water,estimated_depth_m=water-h))
quay=dict(points_m=quay_points,deck_top_m=deck,width_m=6,thickness_m=.22,bank_point_m=[*bank,bank_top],ramp_attach_m=[*attach,deck],ramp_grade=grade,river_id='R_Main',reference_water_levels_m=[first[2],last[2]],normal_to_land=normal,boat_navigation_verified=False)
p=dict(stable_id='S06',target_id='S05',archetype='estuary_trading_harbor',seed=103105,stage='PLANNED',center_source='MainWorld.plan.json#places/S06',houses=houses,streets=streets,fields=[],quay=quay,roof_contact_profile=data['settlements'][0]['roof_contact_profile'],terrain_policy='Natural upper-bank plots and supported estuary quay; original heights/marker/river preserved')
backup=ROOT/'Saved/StarterWorld/BeforeHarbor';backup.mkdir(parents=True,exist_ok=True);assert not (backup/FILE.name).exists();shutil.copy2(FILE,backup/FILE.name);data['settlements'].append(p);FILE.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(houses=evidence,quay_bed_samples=bed,ramp_grade=grade,height_modified=False,height_sha256=hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest()),indent=2),encoding='utf-8');print('Harbor plots',len(houses),'quay length',length,'ramp grade',grade)
