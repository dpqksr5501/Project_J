"""Fit the forestry settlement to preserved contours before native authoring."""
import array,hashlib,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
FILE=SOURCE/'StarterWorld.settlements.json';REPORT=OUT/'forest-village-layout.json'
assert not REPORT.exists(),'Inspect the existing forestry plan before replacing it'
data=json.loads(FILE.read_text(encoding='utf-8'));assert not any(p['target_id']=='S03' for p in data['settlements'])
place=next(p for p in json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'))['places'] if p['id']=='S04')
cx,cy=place['center_m'][:2];raw=array.array('H');raw.frombytes((SOURCE/'HM_StarterRegion.r16').read_bytes())
def height(x,y):
    u,v=x/2,y/2;i,j=int(u),int(v);s,t=u-i,v-j
    return ((raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t-32768)*.0625
def distance(x,y,points):
    best=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(dx*dx+dy*dy,1e-9)))
        best=min(best,math.hypot(x-a[0]-t*dx,y-a[1]-t*dy))
    return best
streets=[dict(id='ForesterApproach',width_m=4.5,points_relative_m=[[-175,-65],[-120,-48],[-75,-32],[-35,-15],[0,0],[35,15],[95,48],[170,85]]),
         dict(id='WoodlandLoop',width_m=3,points_relative_m=[[-75,-32],[-90,0],[-55,40],[-10,55],[45,50],[70,25],[35,15]]),
         dict(id='TimberYardLane',width_m=4,points_relative_m=[[0,0],[20,-30],[52,-52],[100,-60],[145,-40]])]
yards=[dict(id='TimberYard',offset_m=[82,-85],width_m=42,depth_m=30),dict(id='CarpenterCourt',offset_m=[-38,72],width_m=28,depth_m=24)]
houses=[];evidence=[]
specs=[('TimberStore',8,10,1,[100,-27]),('Carpenter',8,10,1,[-42,90]),('ForesterInn',8,10,2,[15,25])]
specs += [('Home%02d'%i,6+(i%2),8,1,q) for i,q in enumerate([[-88,-48],[-52,6],[-82,33],[-23,30],[27,70],[66,65],[83,8],[17,-55]],1)]
for name,w,d,storeys,preferred in specs:
    candidates=[];radius=math.hypot(w,d)/2
    for dx in range(-120,121,3):
        for dy in range(-120,121,3):
            x,y=preferred[0]+dx,preferred[1]+dy
            if not 22<math.hypot(x,y)<155:continue
            if any(math.dist([x,y],h['offset_m'])<radius+math.hypot(h['width'],h['depth'])/2+4 for h in houses):continue
            if any(distance(x,y,r['points_relative_m'])<radius+r['width_m']/2+1.5 for r in streets):continue
            if any(abs(x-f['offset_m'][0])<f['width_m']/2+radius+2 and abs(y-f['offset_m'][1])<f['depth_m']/2+radius+2 for f in yards):continue
            yaw=math.atan2(-x,y);co,si=math.cos(yaw),math.sin(yaw)
            samples=[height(cx+x+co*u-si*v,cy+y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]]
            relief=max(samples)-min(samples)
            if relief>2.5:continue
            candidates.append((math.hypot(dx,dy)+relief*9,x,y,relief))
    assert candidates,('No supported natural plot',name)
    _,x,y,relief=min(candidates);houses.append(dict(id=name,offset_m=[x,y],width=w,depth=d,storeys=storeys))
    evidence.append(dict(id=name,footprint_relief_m=relief,offset_m=[x,y]))
p=dict(stable_id='S04',target_id='S03',archetype='forestry_village',seed=103103,center_source='StarterWorld.plan.json#places/S04',stage='PLANNED',houses=houses,streets=streets,fields=[],work_yards=yards,
       roof_contact_profile=data['settlements'][0]['roof_contact_profile'],terrain_policy='No height edits; supported individual contour plots',
       purpose='Timber work yard separated from housing; loop woodland trail joins two through-road approaches')
backup=ROOT/'Saved/StarterWorld/BeforeForestVillage';backup.mkdir(parents=True,exist_ok=True);assert not (backup/FILE.name).exists();shutil.copy2(FILE,backup/FILE.name)
data['settlements'].append(p);FILE.write_text(json.dumps(data,indent=2),encoding='utf-8')
REPORT.write_text(json.dumps(dict(stable_id='S04',target_id='S03',houses=evidence,height_modified=False,height_sha256=hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest()),indent=2),encoding='utf-8')
print(json.dumps(evidence))
