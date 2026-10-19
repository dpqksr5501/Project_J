"""Deterministic offline terrain/plan authoring for the complete starter region.

Requires numpy and Pillow. Does not import Unreal, edit maps, or touch libraries.
Coordinates: source JSON km east/north from SW -> Unreal cm, centred at origin.
The image rows follow +Y; no implicit image/UE north flip is allowed.
"""
import argparse
import hashlib
import heapq
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Docs/World/MMORPG_Starter_8x8km_Lv1-30_Codex_Pack_v3/ZONE_LAYOUT_LV1_30.json'
OUT = ROOT / 'Content/World/MainWorld/Source'
SEED = 103017
EXTENT = 8064.0
RESOLUTION = 4033
Z_SCALE = 800.0  # -2048 .. +2047.9375m, 6.25cm height quantization.


def smooth(value):
    value = np.clip(value, 0, 1)
    return value * value * (3 - 2 * value)


def blur(field, sigma=1.6):
    radius = math.ceil(sigma*3)
    weights = np.exp(-np.arange(-radius,radius+1,dtype='f4')**2/(2*sigma*sigma))
    weights /= weights.sum()
    result=field
    for axis in (0,1):
        padding=[(0,0),(0,0)];padding[axis]=(radius,radius)
        padded=np.pad(result,padding,mode='edge')
        result=np.zeros_like(field)
        for index,weight in enumerate(weights):
            section=[slice(None),slice(None)];section[axis]=slice(index,index+field.shape[axis])
            result+=padded[tuple(section)]*weight
    return result


def curve(points, spacing=32):
    result=[]
    for index,(a,b) in enumerate(zip(points,points[1:])):
        p=np.array(points[max(index-1,0)],dtype=float);q=np.array(a,dtype=float)
        r=np.array(b,dtype=float);s=np.array(points[min(index+2,len(points)-1)],dtype=float)
        for t in np.linspace(0,1,max(2,math.ceil(math.dist(a[:2],b[:2])/spacing)),endpoint=False):
            xy=.5*((2*q)+(r-p)*t+(2*p-5*q+4*r-s)*t*t+(-p+3*q-3*r+s)*t*t*t)
            if len(a)>2:xy[2]=a[2]+(b[2]-a[2])*t
            result.append(xy.tolist())
    return result+[points[-1]]


def noise(x, y, spacing, seed):
    rng = np.random.default_rng(seed)
    grid = rng.uniform(-1, 1, (int(EXTENT / spacing) + 10,) * 2).astype('f4')
    a, b = x / spacing + 3, y / spacing + 3
    ix, iy = np.floor(a).astype('i4'), np.floor(b).astype('i4')
    fx, fy = smooth(a - ix), smooth(b - iy)
    return ((grid[iy, ix] * (1-fx) + grid[iy, ix+1] * fx) * (1-fy)
            + (grid[iy+1, ix] * (1-fx) + grid[iy+1, ix+1] * fx) * fy)


def distance_to_path(x, y, points):
    best = np.full(x.shape, np.inf, dtype='f4')
    along_z = np.zeros_like(best)
    for a, b in zip(points, points[1:]):
        dx, dy = b[0]-a[0], b[1]-a[1]
        t = np.clip(((x-a[0])*dx + (y-a[1])*dy) / (dx*dx+dy*dy), 0, 1)
        d = np.hypot(x-a[0]-t*dx, y-a[1]-t*dy)
        hit = d < best
        if len(a) > 2:
            along_z = np.where(hit, a[2]+t*(b[2]-a[2]), along_z)
        best = np.minimum(best, d)
    return best, along_z


def drainage_erosion(height, spacing):
    """Priority-flood drainage + stream-power incision + bounded talus relaxation.

    This is an offline landform approximation, not a fluid simulation. Flooding
    only defines drainage; it does not fill the visible landscape with plateaus.
    """
    n = height.shape[0]
    flooded = height.copy()
    seen = np.zeros((n, n), dtype=bool)
    receiver = np.full(n*n, -1, dtype='i4')
    queue = []
    order = []
    for j, i in [(j, i) for j in range(n) for i in (0, n-1)] + [(j, i) for j in (0, n-1) for i in range(1, n-1)]:
        seen[j, i] = True
        heapq.heappush(queue, (float(flooded[j, i]), j*n+i))
    while queue:
        z, index = heapq.heappop(queue)
        order.append(index)
        j, i = divmod(index, n)
        for jj, ii in [(j-1, i), (j+1, i), (j, i-1), (j, i+1)]:
            if jj < 0 or ii < 0 or jj >= n or ii >= n or seen[jj, ii]:
                continue
            seen[jj, ii] = True
            receiver[jj*n+ii] = index
            h = max(float(height[jj, ii]), z + .0001)
            flooded[jj, ii] = h
            heapq.heappush(queue, (h, jj*n+ii))
    accumulation = np.ones(n*n, dtype='f4')
    for index in reversed(order):
        if receiver[index] >= 0:
            accumulation[receiver[index]] += accumulation[index]
    flow = accumulation.reshape(n, n)
    gy, gx = np.gradient(height, spacing)
    incision = np.minimum(32, np.maximum(0, np.log2(flow)-2) * np.clip(np.hypot(gx, gy), 0, 1.4) * 2.6)
    h = height - blur(incision,1.4)
    # Conservative, mass-preserving thermal flux. No wrapping at world boundaries.
    for _ in range(36):
        change = np.zeros_like(h)
        for axis in (0, 1):
            a = (slice(None, -1), slice(None)) if axis == 0 else (slice(None), slice(None, -1))
            b = (slice(1, None), slice(None)) if axis == 0 else (slice(None), slice(1, None))
            difference = h[a]-h[b]
            flux = np.sign(difference)*np.maximum(0, np.abs(difference)-spacing*.7)*.11
            change[a] -= flux
            change[b] += flux
        h += change
    return h, flow


RIVERS = [
    {'id': 'R_Main', 'width_m': 42, 'points': [[4020,8064,175],[4050,7350,145],[4270,6550,120],[4030,5550,98],[4420,4600,82],[4460,4140,65],[4650,3500,46],[4780,2700,29],[5360,2100,15],[6050,1470,5],[6630,1000,1],[6860,0,0]]},
    {'id': 'R_LakeIn', 'width_m': 17, 'points': [[1900,8064,790],[2080,7150,545],[1860,6440,325],[1540,5650,162],[1430,5140,145]]},
    {'id': 'R_LakeOut', 'width_m': 24, 'points': [[1620,4740,145],[2180,4470,127],[2890,4380,107],[3590,4200,86],[4460,4140,65]]},
    {'id': 'R_East', 'width_m': 21, 'points': [[8064,6490,610],[7300,6010,439],[6630,5260,264],[6050,4650,150],[5590,4020,91],[4650,3500,46]]},
]


def generate():
    OUT.mkdir(parents=True, exist_ok=True)
    concept = json.loads(SOURCE.read_text(encoding='utf-8'))
    for river in RIVERS:
        river['points']=curve(river['points'])
    # 8m analysis grid; final Landscape is 2m. Fine rock silhouettes belong to
    # editable cliff assets, rather than gigantic high-frequency height noise.
    n = 1009
    axis = np.linspace(0, EXTENT, n, dtype='f4')
    x, y = np.meshgrid(axis, axis)
    wx = x + 130*noise(x, y, 1100, SEED)
    wy = y + 110*noise(x, y, 950, SEED+1)
    h = 35 + 22*noise(wx, wy, 1250, SEED+2) + 12*noise(wx, wy, 440, SEED+3)
    ridges = [
        ([[0,7300],[900,7180],[1510,7450],[2240,7100],[2870,7560],[3500,8064]], 490, 890),
        ([[950,7180],[1600,6500],[2170,5970],[2900,5480]], 450, 580),
        ([[2890,7560],[3290,6730],[3020,6010]], 380, 630),
        ([[5700,8064],[6080,7270],[6910,6930],[7520,7480],[8064,7400]], 660, 610),
        ([[6080,7270],[5740,6390],[6010,5630]], 480, 410),
        ([[7520,6900],[7400,5480],[7920,4510]], 430, 430),
        ([[0,3100],[780,3400],[1180,2810]], 550, 180),
        ([[3330,1600],[3700,2080],[3970,2560]], 420, 130),
    ]
    for index, (points, width, height) in enumerate(ridges):
        distance, _ = distance_to_path(wx, wy, points)
        roughness = 1-abs(noise(wx, wy, 210, SEED+20+index))
        h += height*np.exp(-(distance/width)**1.6)*(.72+.28*roughness)
    # Eastern plateau has a broad inhabited top and dissected shoulders.
    plateau = np.exp(-(((x-6600)/1400)**4+((y-6400)/1250)**4))
    h += 310*plateau
    h += 28*noise(wx, wy, 190, SEED+50) + 10*noise(wx, wy, 65, SEED+51)
    h, flow = drainage_erosion(h.astype('f4'), EXTENT/(n-1))
    # A continuous south coast; north/east/west remain land expansion boundaries.
    coast = 520 + 150*noise(x, np.zeros_like(y), 1900, SEED+60) + 65*noise(x, np.zeros_like(y), 650, SEED+61)
    coastal_weight = smooth((y-coast+150)/650)
    h = h*coastal_weight + (-45+(y-coast)*.055)*(1-coastal_weight)
    water_distance = np.full_like(h, 1e6)
    for river in RIVERS:
        distance, level = distance_to_path(x, y, river['points'])
        half = river['width_m']/2
        # Broad valley shoulders with a narrower river bed, sharing one elevation.
        weight = np.exp(-(distance/(half+95))**2)
        valley = level + 2 + np.maximum(distance-half, 0)*.12
        h = np.minimum(h, h*(1-weight)+valley*weight)
        # Dry floodplain shoulders must support the water elevation as well as
        # incising high slopes. A narrow bed alone can leave a perched river.
        support = 1-smooth((distance-half-35)/240)
        h = np.maximum(h, h*(1-support)+(level+2+np.maximum(distance-half,0)*.035)*support)
        bed_weight = 1-smooth((distance-half)/(half*.6+10))
        h = h*(1-bed_weight)+(level-3.5+np.minimum(distance/half,1)*.8)*bed_weight
        water_distance = np.minimum(water_distance, distance-half)
    lake_center = [1400, 4900]
    lake_radius = [530, 720]
    lr = np.sqrt(((x-lake_center[0])/lake_radius[0])**2+((y-lake_center[1])/lake_radius[1])**2)
    shore = smooth((lr-.85)/1.05)
    h = h*shore+(136+10*lr+24*np.maximum(lr-1,0))*(1-shore)
    # Preserve a usable lake inlet/outlet water level and ground below it.
    h = np.where(lr < 1, np.minimum(h, 142), h)
    for river in RIVERS:
        distance, level = distance_to_path(x,y,river['points'])
        bed_weight=1-smooth((distance-river['width_m']/2)/16)
        h=h*(1-bed_weight)+(level-3.5)*bed_weight
    hi = np.asarray(Image.fromarray(h.astype('f4')).resize((RESOLUTION, RESOLUTION), Image.Resampling.BICUBIC), dtype='f4').copy()
    raw_float = 32768+hi*100*128/Z_SCALE
    if raw_float.min() < 0 or raw_float.max() > 65535:
        raise ValueError(f'Height range {hi.min()}..{hi.max()} exceeds Z scale; never silently clip terrain')
    raw = np.rint(raw_float).astype('<u2')
    (OUT/'HM_MainWorld.r16').write_bytes(raw.tobytes())
    gy, gx = np.gradient(h, EXTENT/(n-1))
    slope = np.hypot(gx, gy)
    forest = np.clip((noise(x,y,620,SEED+71)+.4)*.8,0,1)*(1-smooth((slope-.5)/.5))
    forest *= smooth((h-8)/40)*(1-smooth((h-650)/230))
    mask = np.stack([forest, np.clip(slope/.95,0,1), 1-smooth(water_distance/95), np.clip((h-600)/300,0,1)], axis=-1)
    Image.fromarray(np.uint8(np.clip(mask,0,1)*255),'RGBA').resize((2048,2048), Image.Resampling.BILINEAR).save(OUT/'T_MainWorldBiomeMasks.png')
    def sample(px, py):
        return float(hi[round(py/EXTENT*(RESOLUTION-1)),round(px/EXTENT*(RESOLUTION-1))])
    places=[]
    for group, default_type in [('settlements','settlement'),('hunting_grounds','hunting_ground'),('field_bosses','field_boss')]:
        for entry in concept[group]:
            px,py=entry['x_km']*1000,entry['y_km']*1000
            # The concept's S03 point lies in the lake bed. Keep the stable ID
            # and progression, but author the village on its dry northern bank.
            if entry['id']=='S03':px,py=1650,5700
            levels=entry.get('suggested_level',entry.get('levels',entry.get('level_region')))
            radius=430 if entry.get('type')=='capital_city' else (190 if group=='settlements' else (340 if group=='hunting_grounds' else 110))
            # Slight irregularity expresses a planning boundary, not a final Nav polygon.
            polygon=[[px+radius*math.cos(i*math.pi/4)*(1+.1*math.sin(i*2.1)),py+radius*math.sin(i*math.pi/4)] for i in range(8)]
            places.append(dict(id=entry['id'],name=entry.get('name',entry.get('type')),kind=entry.get('type',default_type),levels=levels,center_m=[px,py,sample(px,py)],radius_m=radius,boundary_m=polygon,status='CANDIDATE_TERRAIN_SAMPLED_NAV_UNVERIFIED'))
    roads=[
        {'id':'Road_Capital_Start','points':[[4100,4200],[3710,3910],[3390,3500],[3100,3100]]},
        {'id':'Road_Start_Farm','points':[[3100,3100],[2740,2860],[2380,2500],[2100,2100]]},
        {'id':'Road_Farm_Lake','points':[[2100,2100],[1680,2740],[1130,3540],[950,4100],[650,4550],[650,5200],[1100,5650],[1650,5700]]},
        {'id':'Road_Capital_Lake','points':[[4100,4200],[3500,3910],[2710,4030],[2090,4310],[2300,4800],[2180,5350],[1900,5620],[1650,5700]]},
        {'id':'Road_Lake_Mountain','points':[[1650,5700],[2000,5880],[2400,5900],[2600,6000]]},
        {'id':'Road_Mountain_Boss','points':[[2600,6000],[2940,6240],[2500,6410],[2740,6590],[2150,6660],[2000,6850]]},
        {'id':'Road_Capital_Forest','points':[[4100,4200],[4620,4140],[5200,4430],[5810,4570],[6400,4800]]},
        {'id':'Road_Forest_Plateau','points':[[6400,4800],[6890,5210],[6450,5500],[6890,5780],[6500,6250],[7000,7010],[8064,7240]]},
        {'id':'Road_Capital_Harbor','points':[[4100,4200],[3900,3440],[4000,2740],[4510,2240],[5210,1690],[6010,1320],[6600,1250]]},
        {'id':'Road_NorthExit','points':[[2600,6000],[3240,6330],[3520,7050],[3670,7700],[3700,8064]]},
        {'id':'Road_WestExit','points':[[1650,5700],[1120,5900],[760,5800],[0,5670]]},
    ]
    for road in roads:
        road['width_m']=9 if road['id'] in ['Road_Capital_Harbor','Road_Capital_Forest'] else 6
        road['points']=[p+[sample(*p)] for p in road['points']]
        road['status']='ROUTE_CANDIDATE_SLOPE_AND_BRIDGE_REVIEW_REQUIRED'
    connections=[dict(id='E_N_Road',side='north',kind='road',position_m=[3700,EXTENT,sample(3700,EXTENT)],outward=[0,1,0],width_m=6),dict(id='E_E_Road',side='east',kind='road',position_m=[EXTENT,7240,sample(EXTENT,7240)],outward=[1,0,0],width_m=6),dict(id='E_W_Road',side='west',kind='road',position_m=[0,5670,sample(0,5670)],outward=[-1,0,0],width_m=6),dict(id='E_S_Sea',side='south',kind='sea',position_m=[6860,0,0],outward=[0,-1,0],width_m=240)]
    for river in RIVERS:
        first=river['points'][0];side='north' if first[1]==EXTENT else 'east'
        connections.append(dict(id='E_'+river['id'],side=side,kind='river',position_m=first,outward=[0,1,0] if side=='north' else [1,0,0],width_m=river['width_m'])) if first[0]==EXTENT or first[1]==EXTENT else None
    plan=dict(schema='project-j-starter-world-1',region_id='Starter.Lv1_30',seed=SEED,resolution=RESOLUTION,xy_scale_cm=200,z_scale=Z_SCALE,extent_m=EXTENT,min_height_m=float(hi.min()),max_height_m=float(hi.max()),coordinate_system='SW metres east/north -> centred Unreal centimetres; rows +Y',source_json=str(SOURCE.relative_to(ROOT)).replace('\\','/'),source_sha256=hashlib.sha256(SOURCE.read_bytes()).hexdigest(),places=places,rivers=RIVERS,lake=dict(center_m=lake_center,radius_m=lake_radius,water_level_m=145),roads=roads,connections=connections,phase='FULL_REGION_TERRAIN_AND_CONTENT_CANDIDATES',terrain_method='authored branching ridges, drainage accumulation incision, conservative talus relaxation; final river valleys authored',performance_status='UNMEASURED')
    (OUT/'MainWorld.plan.json').write_text(json.dumps(plan,ensure_ascii=False,indent=2),encoding='utf-8')
    edges={side:{'height_m':values.tolist(),'spacing_m':EXTENT/(RESOLUTION-1)} for side,values in [('west',hi[:,0]),('east',hi[:,-1]),('south',hi[0,:]),('north',hi[-1,:])]}
    (OUT/'MainWorld.edges.json').write_text(json.dumps({'schema':1,'seed':SEED,'coordinate_system':plan['coordinate_system'],'edges':edges,'connections':connections}),encoding='utf-8')
    # Offline review image, explicitly not a screenshot of the final Unreal world.
    shade=np.clip(.65+(-gx*.6-gy*.3)/np.sqrt(1+gx*gx+gy*gy),.25,1)
    base=np.stack([.27+.18*(1-forest),.36+.12*forest,.20+.12*(h>650)],axis=-1)
    base=np.where((h<0)[...,None],np.array([.08,.22,.31]),base)
    base=np.where(((lr<1)|(water_distance<0))[...,None],np.array([.12,.33,.39]),base)
    preview=np.uint8(np.clip(base*shade[...,None],0,1)*255)
    Image.fromarray(preview[::-1]).save(OUT/'MainWorld_TerrainReview.png')
    print(json.dumps({k:plan[k] for k in ['extent_m','resolution','min_height_m','max_height_m','phase']}) )


if __name__=='__main__':
    argparse.ArgumentParser(description=__doc__).parse_args()
    generate()
