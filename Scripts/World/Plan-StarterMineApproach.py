"""Terrain-preserving H08 mine stairs and ridge path, with bounded local fill."""
import importlib.util,json,math
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';FILE=SOURCE/'StarterWorld.H08.approach.json';assert not FILE.exists()
s=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Starter-TerrainProfile.py');m=importlib.util.module_from_spec(s);s.loader.exec_module(m);T=m.TerrainProfile(SOURCE/'StarterRegion.manifest.json')
mine=json.loads((SOURCE/'StarterWorld.H08.mine.json').read_text());control=[[2400,5900],[2400,5880],[2420,5840],[2400,5840],[2360,5870],[2320,5936],mine['outside_m'][:2]]
width=2.4;pieces=[];route=[];index=0
for a,b in zip(control,control[1:]):
    length=math.dist(a,b);count=math.ceil(length/.32);dx,dy=(b[0]-a[0])/length,(b[1]-a[1])/length;yaw=math.degrees(math.atan2(dy,dx));tread=length/count+.035
    for i in range(count):
        t=(i+.5)/count;x,y=a[0]+(b[0]-a[0])*t,a[1]+(b[1]-a[1])*t
        h=[T.height(x+dx*u-dy*v,y+dy*u+dx*v) for u in [-tread/2,0,tread/2] for v in [-width/2,0,width/2]]
        top=max(h)+.12;bottom=min(h)-.18
        assert top-bottom<4,('Excessive local retaining wall; revise route',x,y,top-bottom)
        if pieces:assert abs(top-pieces[-1]['top_m'])<.42,('Step exceeds existing Character max step',index,top-pieces[-1]['top_m'])
        pieces.append(dict(index=index,center_xy_m=[x,y],yaw_degrees=yaw,length_m=tread,width_m=width,top_m=top,bottom_m=bottom));route.append([x,y,top]);index+=1
s=importlib.util.spec_from_file_location('support',ROOT/'Scripts/World/Starter-SettlementSupport.py');support=importlib.util.module_from_spec(s);s.loader.exec_module(support);landings=support.add_path_landings(pieces,control,T.height);route=[[*p['center_xy_m'],p['top_m']] for p in pieces]
assert max(p['top_m']-p['bottom_m'] for p in pieces)<4.5,'Revise the route instead of adding a tall retaining wall'
# Boxes meet and overlap at landings; the highest overlapping top is the actual
# floor contract. Consumers must query the oriented footprint, not nearest point.
P=dict(id='H08.MineApproach',target_id='H08',owner='ProjectJ.Starter.H08.Approach.v1',stage='STRUCTURE_DRAFT',control_points_m=control,pieces=pieces,width_m=width,route_points_m=route,maximum_adjacent_riser_m=max(abs(a['top_m']-b['top_m']) for a,b in zip(pieces,pieces[1:])),length_m=sum(math.dist(a,b) for a,b in zip(control,control[1:])),height_sha256=T.manifest['terrain_profile']['sha256'],terrain_modified=False,art_complete=False,scope='Foot access/retreat stairs and ridge path only; regional wagon/mount road and full H08 hunting layout pending')
P.update(landings=landings,layout_revision=2,guardrail_beams=support.plan_path_guardrails(pieces,T.height));FILE.write_text(json.dumps(P,indent=2),encoding='utf-8');print('Approach',P['length_m'],'m;',len(pieces),'treads; maximum rise',P['maximum_adjacent_riser_m'])
