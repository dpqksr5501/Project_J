"""Contour-aware mountain approach candidates, without grading the terrain.

Outputs review data only. Water avoidance, grade limits and endpoint search are
explicit; no automatic relaxation, place relocation, road replacement or Nav claim.
"""
import hashlib,heapq,importlib.util,json,math
from pathlib import Path
import numpy as np
from PIL import Image,ImageDraw
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
spec=importlib.util.spec_from_file_location('diagnosis',ROOT/'Scripts/World/Diagnose-StarterTerrain.py')
D=importlib.util.module_from_spec(spec);spec.loader.exec_module(D)


def solve(h,plan,road):
    p=np.asarray(road['points']);a,b=p[0,:2],p[-1,:2]
    step=8.;margin=640;radius=150.
    low=np.maximum(32,np.floor((np.minimum(a,b)-margin)/step)*step)
    high=np.minimum(8032,np.ceil((np.maximum(a,b)+margin)/step)*step)
    ys=np.arange(low[1],high[1]+1,step);xs=np.arange(low[0],high[0]+1,step);x,y=np.meshgrid(xs,ys)
    grid=D.sample(h,x,y,2);gy,gx=np.gradient(grid,step);side=np.hypot(gx,gy)
    allowed=side<.8
    for river in plan['rivers']:
        dist,_=D.T.distance_to_path(x,y,river['points']);allowed&=dist>river['width_m']/2+8
    lake=plan['lake'];cx,cy=lake['center_m'];rx,ry=lake['radius_m']
    allowed&=((x-cx)/rx)**2+((y-cy)/ry)**2>1.08**2
    goal=(np.hypot(x-b[0],y-b[1])<=radius)&allowed&(side<.38)
    start=(round((a[1]-low[1])/step),round((a[0]-low[0])/step))
    result=dict(id=road['id'],source_height_sha256=hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest(),
                grid_m=step,search_margin_m=margin,goal_approach_radius_m=radius,max_segment_grade_pct=25,
                status='NO_ROUTE_WITHIN_CONSTRAINTS',height_modified=False,navigation_verified=False)
    if not allowed[start] or not goal.any():return result
    def heuristic(j,i):return max(0,math.hypot(xs[i]-b[0],ys[j]-b[1])-radius)
    queue=[(heuristic(*start),0.,start)];cost={start:0.};parent={};end=None;expanded=0
    while queue and expanded<120000:
        _,g,u=heapq.heappop(queue)
        if g>cost[u]+1e-6:continue
        expanded+=1;j,i=u
        if goal[u]:end=u;break
        for dj,di in [(-1,0),(1,0),(0,-1),(0,1),(-1,-1),(-1,1),(1,-1),(1,1)]:
            jj,ii=j+dj,i+di
            if not (0<=jj<len(ys) and 0<=ii<len(xs)) or not allowed[jj,ii]:continue
            if dj and di and (not allowed[j,ii] or not allowed[jj,i]):continue
            length=step*math.hypot(dj,di);grade=abs(grid[jj,ii]-grid[j,i])/length
            if grade>.25:continue
            ng=g+length*(1+12*grade*grade+.8*side[jj,ii]**2);v=(jj,ii)
            if ng>=cost.get(v,float('inf')):continue
            cost[v]=ng;parent[v]=u;heapq.heappush(queue,(ng+heuristic(*v),ng,v))
    result['expanded_nodes']=expanded
    if end is None:return result
    route=[end]
    while route[-1]!=start:route.append(parent[route[-1]])
    route.reverse();points=[[float(xs[i]),float(ys[j]),float(grid[j,i])] for j,i in route]
    lengths=[];grades=[];rise=fall=0
    for a,b in zip(points,points[1:]):
        length=math.dist(a[:2],b[:2]);lengths.append(length)
        q=np.linspace(a[:2],b[:2],max(2,math.ceil(length/2)+1));z=D.sample(h,q[:,0],q[:,1],2)
        grades.extend(abs(np.diff(z))/(length/(len(q)-1))*100)
        rise+=max(0,b[2]-a[2]);fall+=max(0,a[2]-b[2])
    result.update(status='REVIEW_CANDIDATE' if max(grades)<=35 else 'REJECT_FINE_GRADE_EXCEEDS_35_PERCENT',
                  points_m=points,length_m=sum(lengths),net_height_change_m=points[-1][2]-points[0][2],ascent_m=rise,descent_m=fall,
                  fine_2m_grade_pct=D.summary(grades),width_clearance_verified=False,turn_radius_verified=False,
                  endpoint_role='Approach search candidate; original place center/ID remains unchanged')
    # Topographic analysis, not an Unreal screenshot.
    shade=np.clip((.85-.4*gx-.6*gy)/np.sqrt(1+gx*gx+gy*gy),.1,1)
    im=Image.fromarray(np.uint8(np.stack([shade*.8,shade*.86,shade*.73],axis=-1)*255)).resize((1000,800))
    draw=ImageDraw.Draw(im)
    def pixel(px,py):return ((px-low[0])/(high[0]-low[0])*999,(py-low[1])/(high[1]-low[1])*799)
    draw.line([pixel(*v[:2]) for v in road['points']],fill='#fa6666',width=3)
    draw.line([pixel(*v[:2]) for v in points],fill='#5affce',width=3)
    draw.text((10,10),'HEIGHT ANALYSIS / red: existing candidate / green: contour approach / north down',fill='white')
    im.save(OUT/(road['id']+'-contour-analysis.png'))
    return result


def main():
    file=SOURCE/'HM_MainWorld.r16';fingerprint=hashlib.sha256(file.read_bytes()).hexdigest()
    h=(np.fromfile(file,dtype='<u2').reshape(4033,4033).astype('f4')-32768)*.0625
    plan=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'))
    results=[solve(h,plan,r) for r in plan['roads'] if r['id'] in ['Road_Lake_Mountain','Road_Mountain_Boss']]
    assert hashlib.sha256(file.read_bytes()).hexdigest()==fingerprint
    (OUT/'mountain-trail-candidates.json').write_text(json.dumps(dict(height_unchanged=True,routes=results),indent=2),encoding='utf-8')
    print(json.dumps([{k:v for k,v in r.items() if k not in ['points_m','source_height_sha256']} for r in results]))


if __name__=='__main__':main()
