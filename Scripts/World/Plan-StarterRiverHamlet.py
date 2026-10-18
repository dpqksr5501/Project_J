"""Append one lower-river place and its small timber-crossing hamlet profile."""
import hashlib,importlib.util,json,math,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';PLAN=SOURCE/'StarterWorld.plan.json';PROFILES=SOURCE/'StarterWorld.settlements.json';REPORT=OUT/'river-hamlet-layout.json';assert not REPORT.exists()
def module(name,file):
    spec=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');support=module('support','Starter-SettlementSupport.py');plan=json.loads(PLAN.read_text(encoding='utf-8'));data=json.loads(PROFILES.read_text(encoding='utf-8'));assert len(plan['places'])==18 and not any(v['id']=='S07' for v in plan['places']) and not any(v['target_id']=='S07' for v in data['settlements'])
cx,cy=4900,2670;radius=170
for p in plan['places']:assert math.dist([cx,cy],p['center_m'][:2])>radius+p['radius_m']+60
river=next(v for v in plan['rivers'] if v['id']=='R_Main');i=min(range(1,len(river['points'])-1),key=lambda i:abs(river['points'][i][1]-2600));q=river['points'][i];a,b=river['points'][i-1],river['points'][i+1];dx,dy=b[0]-a[0],b[1]-a[1];length=math.hypot(dx,dy);normal=[-dy/length,dx/length]
ends=[[q[0]+normal[0]*d,q[1]+normal[1]*d] for d in [-55,55]];feet=[[q[0]+normal[0]*d,q[1]+normal[1]*d] for d in [-65,65]];deck=max(T.height(*p) for p in ends)+.3
assert all(math.dist([cx,cy],p)<radius-6 for p in feet)
bridge=dict(id='S07_TimberCrossing',river_id='R_Main',points_m=ends,bank_points_m=[[*p,T.height(*p)+.08] for p in feet],deck_top_m=deck,width_m=5,thickness_m=.24,water_reference_m=q[2],source_crossing_m=q[:2],span_m=110)
streets=[dict(id='VillageApproach',width_m=3.5,points_relative_m=[[70,110],[50,75],[25,40],[0,0]]),dict(id='BridgeLane',width_m=3.5,points_relative_m=[[0,0],[feet[1][0]-cx,feet[1][1]-cy]]),dict(id='MerchantLane',width_m=2.6,points_relative_m=[[-65,50],[-30,20],[0,0],[45,-15],[90,-10]])]
def closest(x,y):
    best=(1e9,None)
    for r in streets:
        for a,b in zip(r['points_relative_m'],r['points_relative_m'][1:]):
            dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/(dx*dx+dy*dy)));v=[a[0]+t*dx,a[1]+t*dy];distance=math.dist([x,y],v)
            if distance<best[0]:best=(distance,v)
    return best
specs=[('RiverStore',7,9,1,[-25,0]),('WayfarerInn',7,9,2,[25,10])]+[('Home%02d'%i,5.5,7,1,v) for i,v in enumerate([[-40,40],[-5,60],[65,40],[75,-35],[20,-40],[-50,0]],1)]
houses=[];evidence=[]
for name,w,d,storeys,preferred in specs:
    candidates=[];r=math.hypot(w,d)/2
    for dx in range(-35,36,2):
        for dy in range(-35,36,2):
            x,y=preferred[0]+dx,preferred[1]+dy
            if math.hypot(x,y)>125 or math.hypot(x,y)<15:continue
            if any(math.dist([x,y],h['offset_m'])<r+math.hypot(h['width'],h['depth'])/2+3 for h in houses):continue
            distance,v=closest(x,y)
            if distance<r+3:continue
            yaw=math.atan2(v[0]-x,y-v[1]);co,si=math.cos(yaw),math.sin(yaw);samples=[T.height(cx+x+co*u-si*v,cy+y+si*u+co*v) for u in [-w/2,0,w/2] for v in [-d/2,0,d/2]];relief=max(samples)-min(samples)
            if relief>=2.5 or min(samples)<q[2]+3:continue
            try:support.plan_access_stairs(dict(id=name,center_m=[cx+x,cy+y,max(samples)+.06],yaw=math.degrees(yaw),depth=d),T.height)
            except AssertionError:continue
            candidates.append((math.hypot(dx,dy)+relief*5,x,y,relief,math.degrees(yaw)))
    assert candidates,('No supported dry river-house plot',name)
    _,x,y,relief,yaw=min(candidates);houses.append(dict(id=name,offset_m=[x,y],width=w,depth=d,storeys=storeys,yaw_degrees=yaw));evidence.append(dict(id=name,offset_m=[x,y],relief_m=relief))
place=dict(id='S07',name='river_crossing_hamlet',kind='river_hamlet',levels=[12,22],center_m=[cx,cy,T.height(cx,cy)],radius_m=radius,boundary_m=[[cx+radius*math.cos(i*math.pi/4),cy+radius*math.sin(i*math.pi/4)] for i in range(8)])
profile=dict(stable_id='S07',target_id='S07',archetype='river_crossing_hamlet',seed=103107,stage='PLANNED',center_source='StarterWorld.plan.json#places/S07',houses=houses,streets=streets,fields=[],bridge=bridge,plaza=dict(offset_m=[0,0],radius_m=7,blend_m=2.5),functional_walk_timeout_s=90,roof_contact_profile=data['settlements'][0]['roof_contact_profile'],regional_road_connection='LOCAL_CROSSING_ONLY; global network joining is Phase 2',terrain_policy='Append place identity; preserve all prior places and original river/mountain heights')
backup=ROOT/'Saved/StarterWorld/BeforeRiverHamlet';backup.mkdir(parents=True,exist_ok=True)
for f in [PLAN,PROFILES]:assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name)
plan['places'].append(place);data['settlements'].append(profile);PLAN.write_text(json.dumps(plan,indent=2),encoding='utf-8');PROFILES.write_text(json.dumps(data,indent=2),encoding='utf-8');REPORT.write_text(json.dumps(dict(place=place,houses=evidence,bridge=bridge,height_sha256=T.manifest['terrain_profile']['sha256'],native_registered=False),indent=2),encoding='utf-8');print('Planned S07',len(houses),'buildings, bridge deck',deck,'water',q[2])
