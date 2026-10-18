"""Fit lake-bank homes and a small pier to the existing water/height contract."""
import array,hashlib,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=SOURCE/'StarterWorld.settlements.json';REPORT=OUT/'lake-village-layout.json'
assert not REPORT.exists();data=json.loads(FILE.read_text(encoding='utf-8'));assert not any(p['target_id']=='S04' for p in data['settlements'])
plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'));cx,cy=next(p['center_m'][:2] for p in plan['places'] if p['id']=='S03');water=plan['lake']['water_level_m']
raw=array.array('H');raw.frombytes((SOURCE/'HM_StarterRegion.r16').read_bytes())
def height(x,y):
    u,v=x/2,y/2;i,j=int(u),int(v);s,t=u-i,v-j
    return ((raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t-32768)*.0625
def distance(x,y,points):
    best=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(dx*dx+dy*dy,1e-9)))
        best=min(best,math.hypot(x-a[0]-t*dx,y-a[1]-t*dy))
    return best
streets=[dict(id='BankThroughLane',width_m=4,points_relative_m=[[-140,-35],[-80,-45],[-20,-45],[30,-40],[100,-25],[170,10]]),
 dict(id='LandingLane',width_m=3,points_relative_m=[[-20,-45],[-5,-80],[0,-120],[0,-148]]),
 dict(id='ShoreWalk',width_m=2.4,points_relative_m=[[-115,-105],[-80,-120],[-35,-132],[0,-140],[45,-130],[95,-118],[150,-100]])]
specs=[('FishStore',8,10,1,[20,-98]),('LakesideInn',8,10,2,[35,-18])]
specs += [('Home%02d'%i,6+(i%2),8,1,q) for i,q in enumerate([[-100,-65],[-65,-15],[-30,-14],[70,-7],[115,-3],[-65,-95],[65,-90],[110,-70]],1)]
houses=[];evidence=[]
for name,w,d,storeys,preferred in specs:
    candidates=[];radius=math.hypot(w,d)/2
    for dx in range(-45,46,3):
        for dy in range(-45,46,3):
            x,y=preferred[0]+dx,preferred[1]+dy
            if math.hypot(x,y)>165 or math.hypot(x,y)<18:continue
            if any(math.dist([x,y],h['offset_m'])<radius+math.hypot(h['width'],h['depth'])/2+4 for h in houses):continue
            if any(distance(x,y,r['points_relative_m'])<radius+r['width_m']/2+1 for r in streets):continue
            yaw=math.atan2(-x,y);co,si=math.cos(yaw),math.sin(yaw)
            samples=[height(cx+x+co*u-si*v,cy+y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]];relief=max(samples)-min(samples)
            if relief>=2.5 or min(samples)<water+2:continue
            candidates.append((math.hypot(dx,dy)+relief*6,x,y,relief))
    assert candidates,('No natural dry plot',name)
    _,x,y,relief=min(candidates);houses.append(dict(id=name,offset_m=[x,y],width=w,depth=d,storeys=storeys));evidence.append(dict(id=name,offset_m=[x,y],relief_m=relief))
dock=dict(id='FishingLanding',axis_x_m=cx,land_y_m=cy-120,ramp_end_y_m=cy-148,head_y_m=cy-182,deck_top_m=water+1.25,width_m=4.4,head_width_m=22,head_depth_m=6,thickness_m=.18,water_level_m=water)
samples=[dict(x_m=cx,y_m=y,bed_m=height(cx,y),water_depth_m=water-height(cx,y)) for y in range(int(dock['head_y_m']-2),int(dock['ramp_end_y_m'])+1,4)]
assert all(s['water_depth_m']>2 for s in samples if s['y_m']<=cy-166)
assert math.hypot(dock['head_width_m']/2,182+3)<190
p=dict(stable_id='S03',target_id='S04',archetype='lake_fishing_village',seed=103104,stage='PLANNED',center_source='StarterWorld.plan.json#places/S03',houses=houses,streets=streets,fields=[],dock=dock,roof_contact_profile=data['settlements'][0]['roof_contact_profile'],terrain_policy='Dry natural plots; supported pier/ramp, no height or existing water edits')
backup=ROOT/'Saved/StarterWorld/BeforeLakeVillage';backup.mkdir(parents=True,exist_ok=True);assert not (backup/FILE.name).exists();shutil.copy2(FILE,backup/FILE.name)
data['settlements'].append(p);FILE.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(houses=evidence,dock_bed_samples=samples,height_modified=False,height_sha256=hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest()),indent=2),encoding='utf-8');print(json.dumps(evidence))
