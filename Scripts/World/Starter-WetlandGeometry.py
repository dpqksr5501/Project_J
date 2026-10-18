"""Clip level water to a connected basin in the preserved landscape grid."""
import collections,math

def coarsen_level_surface(surface,block_cells=8,allow_flowing=False):
    """Coarsen only fully wet grid blocks, retaining a conforming shoreline.

    Neighboring coarse blocks share long edges; a fine neighbor keeps every
    original edge sample. Unlike a T-junction mesh this also remains closed
    if the water shader later moves shared vertices.
    """
    step=surface['terrain_grid_m'];size=step*block_cells;level=surface['water_level_m']
    vertices=surface['vertices_m'];triangles=surface['triangles']
    assert allow_flowing or all(abs(v[2]-level)<1e-8 for v in vertices)
    lattice={(round(v[0]/step),round(v[1]/step)) for v in vertices
             if abs(v[2]-level)<1e-8 and abs(v[0]/step-round(v[0]/step))<1e-9 and abs(v[1]/step-round(v[1]/step))<1e-9}
    grouped={};areas={};flowing=set()
    for triangle in triangles:
        a,b,c=[vertices[k] for k in triangle]
        key=(math.floor((a[0]+b[0]+c[0])/(3*size)),math.floor((a[1]+b[1]+c[1])/(3*size)))
        grouped.setdefault(key,[]).append(triangle)
        if any(abs(p[2]-level)>=1e-8 for p in [a,b,c]):flowing.add(key)
        area=abs((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2
        areas[key]=areas.get(key,0)+area
    coarse=set()
    for key,area in areas.items():
        i,j=key[0]*block_cells,key[1]*block_cells
        if key not in flowing and abs(area-size*size)<1e-6 and all((i+u,j+v) in lattice for u in range(block_cells+1) for v in range(block_cells+1)):
            coarse.add(key)
    output=[];faces=[];indices={}
    def vertex(x,y,z=level):
        key=(round(x,8),round(y,8),round(z,8))
        if key not in indices:indices[key]=len(output);output.append([x,y,z])
        return indices[key]
    for key,old in grouped.items():
        if key not in coarse:
            for t in old:faces.append([vertex(*vertices[k]) for k in t])
            continue
        x,y=key[0]*size,key[1]*size;ring=[]
        for start,delta,neighbor in [((x,y),(step,0),(key[0],key[1]-1)),
                                     ((x+size,y),(0,step),(key[0]+1,key[1])),
                                     ((x+size,y+size),(-step,0),(key[0],key[1]+1)),
                                     ((x,y+size),(0,-step),(key[0]-1,key[1]))]:
            count=1 if neighbor in coarse else block_cells
            for k in range(count):
                factor=k*block_cells/count;ring.append(vertex(start[0]+delta[0]*factor,start[1]+delta[1]*factor))
        center=vertex(x+size/2,y+size/2)
        faces.extend([center,b,a] for a,b in zip(ring,ring[1:]+ring[:1]))
    result=dict(surface,vertices_m=output,triangles=faces)
    result['planar_coarsening']=dict(original_triangles=len(triangles),coarse_blocks=len(coarse),block_size_m=size,shoreline_changed=False,conforming_edges=True)
    return result

def surface_distance(x,y,surface):
    """Signed horizontal distance to contour segments, negative in wet rings."""
    distance=1e30;inside=False
    for a,b in surface.get('boundary_segments_m',surface['shore_segments_m']):
        dx,dy=b[0]-a[0],b[1]-a[1];length=dx*dx+dy*dy
        t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/length)) if length else 0
        distance=min(distance,math.hypot(x-a[0]-dx*t,y-a[1]-dy*t))
        if (a[1]>y)!=(b[1]>y) and x<(b[0]-a[0])*(y-a[1])/(b[1]-a[1])+a[0]:inside=not inside
    return -distance if inside else distance

def basin_surface(terrain,seed_xy,water_level_m,search_radius_m=120):
    step=terrain.spacing;assert step==2
    sx,sy=[round(v/step) for v in seed_xy];radius=math.ceil(search_radius_m/step);cache={}
    def height(q):
        if q not in cache:cache[q]=terrain.height(q[0]*step,q[1]*step)
        return cache[q]
    seed=(sx,sy);assert height(seed)<water_level_m
    wet={seed};queue=collections.deque([seed])
    while queue:
        q=queue.popleft();assert abs(q[0]-sx)<radius and abs(q[1]-sy)<radius,'Basin reaches survey boundary'
        # Native landscape diagonal verified from collision: 00 to11.
        for dx,dy in [(1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,-1)]:
            p=q[0]+dx,q[1]+dy
            if p not in wet and height(p)<water_level_m:wet.add(p);queue.append(p)
    return _surface_from_wet(terrain,wet,water_level_m,height)

def _surface_from_wet(terrain,wet,water_level_m,height,cell_bounds=None):
    step=terrain.spacing
    cells={(i+dx,j+dy) for i,j in wet for dx,dy in [(0,0),(-1,0),(0,-1),(-1,-1)]};vertices=[];triangles=[];indices={};shore=[];area=0
    if cell_bounds is not None:
        x0,y0,x1,y1=cell_bounds;cells={q for q in cells if x0<=q[0]<x1 and y0<=q[1]<y1}
    def vertex(q):
        key=(round(q[0],8),round(q[1],8))
        if key not in indices:indices[key]=len(vertices);vertices.append([q[0],q[1],water_level_m])
        return indices[key]
    for i,j in sorted(cells):
        for corners in [[(i,j),(i+1,j),(i+1,j+1)],[(i,j),(i+1,j+1),(i,j+1)]]:
            if not any(q in wet for q in corners):continue
            polygon=[];crossings=[]
            for a,b in zip(corners,corners[1:]+corners[:1]):
                za,zb=height(a),height(b)
                if za<water_level_m:polygon.append([a[0]*step,a[1]*step])
                if (za<water_level_m)!=(zb<water_level_m):
                    t=(water_level_m-za)/(zb-za);p=[(a[0]+t*(b[0]-a[0]))*step,(a[1]+t*(b[1]-a[1]))*step];polygon.append(p);crossings.append(p)
            if len(crossings)==2:shore.append(crossings)
            for k in range(1,len(polygon)-1):
                a,b,c=polygon[0],polygon[k],polygon[k+1];signed=((b[0]-a[0])*(c[1]-a[1])-(b[1]-a[1])*(c[0]-a[0]))/2
                # GeometryScript/Chaos front faces use the opposite winding
                # from the mathematical XY-positive convention used for area.
                if signed>1e-9:triangles.append([vertex(a),vertex(c),vertex(b)]);area+=signed
    assert triangles and shore
    return dict(vertices_m=vertices,triangles=triangles,shore_segments_m=shore,water_level_m=water_level_m,minimum_bed_m=min(height(q) for q in wet),maximum_depth_m=water_level_m-min(height(q) for q in wet),area_m2=area,bounds_m=[min(q[0] for q in vertices),min(q[1] for q in vertices),max(q[0] for q in vertices),max(q[1] for q in vertices)],terrain_grid_m=step,terrain_triangle_diagonal='00-11',source_height_modified=False)


def sea_connected_grid(terrain,bounds_m,water_level_m=0):
    """Flood only sea-connected vertices in a bounded reach abutting the ocean."""
    step=terrain.spacing;limits=[round(v/step) for v in bounds_m];x0,y0,x1,y1=limits;assert all(abs(a*step-b)<1e-6 for a,b in zip(limits,bounds_m));cache={}
    def height(q):
        if q not in cache:cache[q]=terrain.height(q[0]*step,q[1]*step)
        return cache[q]
    wet={(x,y0) for x in range(x0,x1+1) if height((x,y0))<water_level_m};assert wet;queue=collections.deque(sorted(wet))
    while queue:
        x,y=queue.popleft()
        for dx,dy in [(1,0),(-1,0),(0,1),(0,-1),(1,1),(-1,-1)]:
            q=x+dx,y+dy
            if x0<=q[0]<=x1 and y0<=q[1]<=y1 and q not in wet and height(q)<water_level_m:wet.add(q);queue.append(q)
    return wet,height,limits

def coastal_surface(terrain,bounds_m,water_level_m=0,connectivity=None):
    step=terrain.spacing;limits=[round(v/step) for v in bounds_m];wet,height,connected_bounds=connectivity or sea_connected_grid(terrain,bounds_m,water_level_m)
    result=_surface_from_wet(terrain,wet,water_level_m,height,limits);result.update(connected_to_south_edge=True,clip_bounds_m=bounds_m)
    edges={}
    for triangle in result['triangles']:
        for a,b in zip(triangle,triangle[1:]+triangle[:1]):
            key=tuple(sorted((a,b)));edges[key]=edges.get(key,0)+1
    result['boundary_segments_m']=[[result['vertices_m'][a][:2],result['vertices_m'][b][:2]] for (a,b),count in edges.items() if count==1]
    return result
