"""Pure contour routing around native surveyed mesh bounds; no terrain edits."""
import functools,heapq,math
class HuntingRoutes:
    def __init__(self,terrain,boundary,obstacles,combat_plots,grade_limit=.18,step=2,width_m=3.2,water_ellipses=()):
        self.terrain,self.boundary,self.obstacles,self.plots=terrain,boundary,obstacles,combat_plots;self.grade_limit,self.step,self.width=grade_limit,step,width_m
        self.water_ellipses=water_ellipses
        self.offsets=[(i,j) for i in range(-3,4) for j in range(-3,4) if (i or j) and math.gcd(abs(i),abs(j))==1]
        self.cell_size=32;self.obstacle_cells={}
        for obstacle in obstacles:
            x,y=obstacle['center_m'][:2];radius=obstacle['radius_m']+self.width/2+.8
            for i in range(math.floor((x-radius)/self.cell_size),math.floor((x+radius)/self.cell_size)+1):
                for j in range(math.floor((y-radius)/self.cell_size),math.floor((y+radius)/self.cell_size)+1):self.obstacle_cells.setdefault((i,j),[]).append(obstacle)
        self.clear=functools.lru_cache(None)(self._clear);self.height=functools.lru_cache(None)(lambda q:self.terrain.height(*q))
    def _clear(self,x,y):
        inside=False;j=len(self.boundary)-1
        for i,(px,py) in enumerate(self.boundary):
            qx,qy=self.boundary[j]
            if ((py>y)!=(qy>y)) and x<(qx-px)*(y-py)/(qy-py)+px:inside=not inside
            j=i
        nearby=self.obstacle_cells.get((math.floor(x/self.cell_size),math.floor(y/self.cell_size)),[])
        dry=all(ellipse_clearance_lower_bound(x,y,e)>e.get('margin_m',0)+self.width/2+.8 for e in self.water_ellipses)
        return inside and dry and all(math.hypot(x-o['center_m'][0],y-o['center_m'][1])>o['radius_m']+self.width/2+.8 for o in nearby)
    def solve(self,start,goal,safe=False):
        start,goal=tuple(start),tuple(goal);queue=[(0,0,start)];scores={start:0};parent={}
        @functools.lru_cache(None)
        def edge(a,b):
            if not self.clear(*b):return None
            distance=math.dist(a,b);n=math.ceil(distance/2);last=self.height(a);maximum=0
            for i in range(1,n+1):
                x,y=a[0]+(b[0]-a[0])*i/n,a[1]+(b[1]-a[1])*i/n
                if not self.clear(x,y):return None
                if safe and any(math.dist([x,y],q['center_m'][:2])<q.get('safe_exclusion_radius_m',q['radius_m']+20) for q in self.plots):return None
                z=self.terrain.height(x,y);grade=abs(z-last)/(distance/n);last=z;maximum=max(maximum,grade)
                if grade>self.grade_limit:return None
            return distance*(1+maximum*3)
        while queue:
            _,g,a=heapq.heappop(queue)
            if g!=scores.get(a):continue
            if a==goal:
                path=[a]
                while path[-1]!=start:path.append(parent[path[-1]])
                return [list(v) for v in reversed(path)]
            for dx,dy in self.offsets:
                b=(a[0]+dx*self.step,a[1]+dy*self.step);value=edge(a,b)
                if value is None or g+value>=scores.get(b,1e30):continue
                scores[b]=g+value;parent[b]=a;heapq.heappush(queue,(g+value+math.dist(b,goal),g+value,b))
        raise AssertionError(('No contour route around native obstacles',start,goal))

def ellipse_clearance_lower_bound(x,y,ellipse):
    """Conservative exterior clearance, not an exact Euclidean shore distance.

    The scaled norm is 1/min(radius)-Lipschitz, so this lower bound cannot
    accept an exterior point closer to the ellipse than the required margin.
    Inside returns a negative value. Source radii must enclose the native mesh.
    """
    cx,cy=ellipse['center_m'][:2];rx,ry=ellipse['radius_m'];assert rx>0 and ry>0
    return (math.hypot((x-cx)/rx,(y-cy)/ry)-1)*min(rx,ry)
