"""Author H01's three natural combat plots and non-destructive approach network."""
import functools,hashlib,heapq,importlib.util,json,math,shutil,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'H01-hunting-layout.json';replan='--replan-legacy-foliage' in sys.argv
if not replan:assert not FILE.exists()
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Starter-TerrainProfile.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);T=m.TerrainProfile(SOURCE/'MainWorld.manifest.json');plan=json.loads((SOURCE/'MainWorld.plan.json').read_text());place=next(v for v in plan['places'] if v['id']=='H01');survey=json.loads((OUT/'H01-hunting-site-survey.json').read_text());pre=json.loads((OUT/'H01-hunting-preflight.json').read_text());assert len(survey['selected'])==3
plots=[dict(q,id='H01.G%02d'%(i+1),role=role) for i,(q,role) in enumerate(zip(survey['selected'],['SouthMeadow','WestLowland','EastRise']))];trees=[v for v in pre['forest'] if 'BlackAlder' in v['mesh']]
def inside(x,y):
    points=place['boundary_m'];result=False;j=len(points)-1
    for i,(px,py) in enumerate(points):
        qx,qy=points[j]
        if ((py>y)!=(qy>y)) and x<(qx-px)*(y-py)/(qy-py)+px:result=not result
        j=i
    return result
@functools.lru_cache(None)
def ground(q):return T.height(*q)
@functools.lru_cache(None)
def clear(x,y):
    if not inside(x,y):return False
    return all(math.dist([x,y],v['position_m'][:2])>v['conservative_visual_radius_m']+2.6 for v in trees)
offsets=[(i,j) for i in range(-3,4) for j in range(-3,4) if (i or j) and math.gcd(abs(i),abs(j))==1]
def solve(start,goal,safe):
    start,goal=tuple(start),tuple(goal);queue=[(0,0,start)];scores={start:0};parent={}
    @functools.lru_cache(None)
    def cost(a,b):
        if not clear(*b):return None
        distance=math.dist(a,b);n=math.ceil(distance/2);last=ground(a);maximum=0
        for i in range(1,n+1):
            x,y=a[0]+(b[0]-a[0])*i/n,a[1]+(b[1]-a[1])*i/n
            if not clear(x,y):return None
            if safe and any(math.dist([x,y],q['center_m'][:2])<q['radius_m']+20 for q in plots):return None
            z=T.height(x,y);grade=abs(z-last)/(distance/n);maximum=max(maximum,grade);last=z
            if grade>.18:return None
        return distance*(1+maximum*3)
    while queue:
        _,g,a=heapq.heappop(queue)
        if g!=scores.get(a):continue
        if a==goal:
            path=[a]
            while path[-1]!=start:path.append(parent[path[-1]])
            return [list(v) for v in reversed(path)]
        for dx,dy in offsets:
            b=(a[0]+dx*2,a[1]+dy*2);value=cost(a,b)
            if value is None or g+value>=scores.get(b,1e30):continue
            scores[b]=g+value;parent[b]=a;heapq.heappush(queue,(g+value+math.dist(b,goal),g+value,b))
    raise AssertionError(('No preserved-terrain hunting route',start,goal))
entry=[3100,2892];exit=[2920,2800];safe=solve(entry,exit,True);trails=[dict(id='H01.SafePassage',role='safe_passage',width_m=3.2,points_m=safe)]
for q in plots:
    x,y=q['center_m'][:2];nearest=min(safe,key=lambda p:math.dist(p,[x,y]));path=solve(nearest,[x,y],False);trails.append(dict(id=q['id']+'.Approach',role='combat_branch',width_m=2.6,cluster_id=q['id'],points_m=path));q['approach_trail_id']=trails[-1]['id'];q['retreat_trail_id']=trails[-1]['id'];q['reset_radius_m']=q['radius_m']+10;q['spawn_candidates']=[]
    for index,(dx,dy) in enumerate([[-8,-5],[8,-5],[0,9]],1):
        px,py=x+dx,y+dy;assert clear(px,py);q['spawn_candidates'].append(dict(id=q['id']+'.Spawn%02d'%index,position_m=[px,py,T.height(px,py)],clearance_radius_m=.6,capsule_half_height_m=1,capsule_ground_gap_m=.15,terrain_validated=True,collision_validated=False,nav_validated=False))
    q['patrol_candidates_m']=[[x+dx,y+dy,T.height(x+dx,y+dy)] for dx,dy in [[-12,-8],[12,-8],[0,13]]];q['respawn_candidate_seconds']=90;q['respawn_policy_status']='AUTHORING_CANDIDATE_NOT_ACTIVE';q['max_concurrent_candidate']=3
walk=safe+list(reversed(safe[:-1]))
for trail in trails[1:]:
    branch=trail['points_m'];walk+=solve(walk[-1],branch[0],True)[1:]+branch[1:]+list(reversed(branch[:-1]))
# The native movement observer follows the full graph, including retreats.
p=dict(stable_id='H01',target_id='H01',region_id=plan['region_id'],display_name='Meadow Hills',archetype='entry_meadow_hills',seed=103201,generation_version=1,stage='PLANNED',center_source='MainWorld.plan.json#places/H01',boundary_source='MainWorld.plan.json#places/H01/boundary_m',levels=place['levels'],risk_tier='LOW_ENTRY',clusters=plots,trails=trails,entries=[dict(id='H01.Entry.Village',position_m=[*entry,T.height(*entry)],role='village_approach'),dict(id='H01.Exit.West',position_m=[*exit,T.height(*exit)],role='alternate_retreat')],safe_buffer_m=20,spawn_exclusion=dict(settlement_radius_margin_m=25,road_margin_m=15,water_margin_m=25,structures='Native capsule collision verification required',terrain_max_slope=.17),server_authority=dict(policy='SERVER_ONLY',npc_contract='/Script/Project_JCharacter.Project_JNPCCharacter',status='NOT_BOUND',mass_spawner_used=False,reward_writer_added=False),quest_hooks=[dict(id='H01.IntroWildlife',status='AUTHORING_HOOK_ONLY',cluster_ids=[q['id'] for q in plots]),dict(id='H01.WestRetreat',status='AUTHORING_HOOK_ONLY',entry_id='H01.Exit.West')],reset_policy=dict(status='CANDIDATE_ONLY',leash_scope='cluster reset_radius_m; safe passage excluded',ownership='existing server AI/gameplay contract integration pending'),surface_extent_m=380,functional_walk_points_m=walk,functional_walk_timeout_s=math.ceil(sum(math.dist(a,b) for a,b in zip(walk,walk[1:]))/2+60),terrain_policy='Preserve source heights and vegetation; select pre-existing natural clearings',art_complete=False,navigation_verified=False,gameplay_ready=False)
f=SOURCE/'MainWorld.hunting.json';data=json.loads(f.read_text()) if f.exists() else dict(schema=1,hunting_grounds=[])
if replan:
    old=next(v for v in data['hunting_grounds'] if v['stable_id']=='H01');assert old['stage']=='PLANNED' and json.loads((OUT/'H01-hunting-build.json').read_text())['phase']=='COMPLETE';data['hunting_grounds']=[v for v in data['hunting_grounds'] if v['stable_id']!='H01']
else:assert not any(q['stable_id']=='H01' for q in data['hunting_grounds'])
backup=ROOT/'Saved/StarterWorld'/('BeforeMeadowLegacyReplan' if replan else 'BeforeMeadowHunt');backup.mkdir(parents=True,exist_ok=True)
if replan:assert not (backup/FILE.name).exists();shutil.copy2(FILE,backup/FILE.name)
if f.exists():assert not (backup/f.name).exists();shutil.copy2(f,backup/f.name)
data['hunting_grounds'].append(p);f.write_text(json.dumps(data,indent=2),encoding='utf-8');FILE.write_text(json.dumps(dict(target_id='H01',clusters=plots,trails=[dict(id=q['id'],vertices=len(q['points_m']),length_m=sum(math.dist(a,b) for a,b in zip(q['points_m'],q['points_m'][1:]))) for q in trails],height_sha256=T.manifest['terrain_profile']['sha256'],height_modified=False,forest_modified=False,walk_points=len(walk)),indent=2),encoding='utf-8');print('H01 planned:',len(plots),'clusters,',len(trails),'trails,',len(walk),'walk points')
