"""Pure stair geometry shared by plot review and native static authoring."""
import math

def plan_mine_entry_steps(mine,height):
    cx,cy,floor=mine['center_m'];a=math.radians(mine['yaw_degrees']);co,si=math.cos(a),math.sin(a);steps=[]
    for i in range(20):
        u=-8-.36*(i+.5);top=floor-.16*i;heights=[height(cx+co*(u+du)-si*v,cy+si*(u+du)+co*v) for du in [-.18,.18] for v in [-.9,0,.9]]
        if top<=max(heights)+.06:break
        steps.append(dict(local_u_m=u,top_m=top,bottom_m=min(heights)-.2))
    return steps

def plan_path_guardrails(pieces,height):
    beams=[];previous=None
    for p in pieces[::8]:
        x,y=p['center_xy_m'];angle=math.radians(p['yaw_degrees']);dx,dy=math.cos(angle),math.sin(angle);left=height(x-dy*1.45,y+dx*1.45);right=height(x+dy*1.45,y-dx*1.45);sign=1 if left<right else -1;px,py=x-dy*1.37*sign,y+dx*1.37*sign
        if p['top_m']-min(left,right)<.85:previous=None;continue
        base=[px,py,p['top_m']-.1];head=[px,py,p['top_m']+1.05];beams.append(dict(start_m=base,end_m=head))
        if previous and previous[0]==sign and math.dist(head,previous[1])<3:beams.append(dict(start_m=[*previous[1][:2],previous[1][2]-.15],end_m=[*head[:2],head[2]-.15]))
        previous=(sign,head)
    return beams

def add_path_landings(pieces,controls,height):
    """Raise only the local turn treads into landing shelves; no terrain edits."""
    landings=[]
    for x,y in controls[1:-1]:
        top=max(height(x+u,y+v) for u in [-2,-1,0,1,2] for v in [-2,-1,0,1,2] if math.hypot(u,v)<2.1)+.12;landings.append(dict(center_xy_m=[x,y],top_m=top,radius_m=2,blend_radius_m=5))
    tops=[max([p['top_m']]+[p['top_m']+(q['top_m']-p['top_m'])*max(0,min(1,(5-math.dist(p['center_xy_m'],q['center_xy_m']))/3)) for q in landings]) for p in pieces]
    for i in range(1,len(tops)):tops[i]=max(tops[i],tops[i-1]-.36)
    for i in range(len(tops)-2,-1,-1):tops[i]=max(tops[i],tops[i+1]-.36)
    for p,top in zip(pieces,tops):p['top_m']=top
    return landings

class AuthoredPathSupport:
    """Highest top of overlapping, oriented path treads; metres in source space."""
    def __init__(self,pieces):
        self.cells={};self.rows=[];size=self.cell_size=8
        for p in pieces:
            x,y=p['center_xy_m'];angle=math.radians(p['yaw_degrees']);row=(p,math.cos(angle),math.sin(angle));self.rows.append(row);r=math.hypot(p['length_m'],p['width_m'])/2
            for i in range(math.floor((x-r)/size),math.floor((x+r)/size)+1):
                for j in range(math.floor((y-r)/size),math.floor((y+r)/size)+1):self.cells.setdefault((i,j),[]).append(row)
    def top(self,x,y):
        tops=[]
        for p,co,si in self.cells.get((math.floor(x/self.cell_size),math.floor(y/self.cell_size)),[]):
            dx,dy=x-p['center_xy_m'][0],y-p['center_xy_m'][1];u,v=dx*co+dy*si,-dx*si+dy*co
            if abs(u)<=p['length_m']/2+1e-7 and abs(v)<=p['width_m']/2+1e-7:tops.append(p['top_m'])
        return max(tops) if tops else None
def plan_access_stairs(placement,height):
    x,y,floor=placement['center_m'];a=math.radians(placement['yaw']);front=placement['depth']/2;run=.32;landing=.9
    def ground(distance):return height(x+math.sin(a)*distance,y-math.cos(a)*distance)
    candidates=[]
    for n in range(1,65):
        rise=(floor-ground(front+landing+(n-.5)*run))/n
        if .08<=rise<=.20 and all(floor-(i+1)*rise>=ground(front+landing+(i+.5)*run)-.03 for i in range(n)):candidates.append((n,rise))
    if not candidates:
        landing=.3
        for n in range(1,65):
            rise=(floor-ground(front+landing+(n-.5)*run))/n
            if .08<=rise<=.20 and all(floor-(i+1)*rise>=ground(front+landing+(i+.5)*run)-.03 for i in range(n)):candidates.append((n,rise))
    if not candidates and -.03<=floor-ground(front+.2)<=.10:candidates=[(0,0)];landing=.2
    assert candidates,(placement['id'],'Exterior approach requires bespoke terrain-aware access')
    n,rise=min(candidates);pieces=[]
    for distance,depth,top in [(front+landing/2,landing,floor)]+[(front+landing+(i+.5)*run,run+.015,floor-(i+1)*rise) for i in range(n)]:
        bottom=min(ground(distance-depth/2),ground(distance+depth/2),top-.12)-.20;pieces.append((distance,depth,top,bottom))
    return dict(instances=len(pieces),risers=n,rise_m=rise,tread_run_m=run,landing_depth_m=landing,width_m=1.8,exterior_only=True,pieces=pieces)
