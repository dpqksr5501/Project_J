"""Bounded offline composition adjustment; preserves the authored heightmap.

House slots, gardens and surface masks share the same source. This pass brings
the eight plots into a walkable village core while retaining the 8 km region.
"""
import array,importlib.util,json,math
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source'
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Generate-StarterWorld.py');terrain=importlib.util.module_from_spec(spec);spec.loader.exec_module(terrain)
plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'))
layout=json.loads((SOURCE/'StarterWorld.corridor.json').read_text(encoding='utf-8'))
assert layout['revision']==1 and plan['corridor_revision']==1
raw=np.fromfile(SOURCE/'HM_StarterRegion.r16',dtype='<u2').reshape(4033,4033).astype('f4')
def height(x,y):
    u=x/2;v=y/2;i,j=int(u),int(v);s,t=u-i,v-j
    q=(raw[j,i]*(1-s)+raw[j,i+1]*s)*(1-t)+(raw[j+1,i]*(1-s)+raw[j+1,i+1]*s)*t
    return float((q-32768)*.0625)
slots={'Inn':(3077,3128),'Smith':(3128,3084),'CottageWest':(3066,3080),'Barn':(3038,3048),'CottageSouth':(3090,3056),'Granary':(3141,3120),'CottageNorth':(3148,3158),'Weaver':(3080,3162)}
paths=[]
for house in layout['houses']:
    house['x'],house['y']=slots[house['id']]
    house['yaw']=math.degrees(math.atan2(3100-house['x'],house['y']-3100))
    if house['id']=='Inn':house['width'],house['depth']=15,8
    if house['id']=='Granary':house['width'],house['depth']=10.5,9.6
    house['ground_m']=height(house['x'],house['y'])
    a=math.radians(house['yaw']);s,c=math.sin(a),math.cos(a)
    door=[house['x']+s*(house['depth']/2+2),house['y']-c*(house['depth']/2+2)]
    paths.append([door,[(door[0]+3100)/2+(2 if house['x']>3100 else -2),(door[1]+3100)/2],[3100,3100]])
layout['footpaths']=paths;layout['composition_revision']=2;layout.setdefault('player_start_yaw',125)
(SOURCE/'StarterWorld.corridor.json').write_text(json.dumps(layout,indent=2),encoding='utf-8')
axis=np.arange(2017,dtype='f4')*4;x,y=np.meshgrid(axis,axis)
road_mask=np.zeros(x.shape,dtype='f4')
for road in plan['roads']:
    if road['id'] not in layout['graded_roads']:continue
    d,_=terrain.distance_to_path(x,y,road['points']);road_mask=np.maximum(road_mask,1-terrain.smooth((d-2.9)/2.4))
for path in paths:
    d,_=terrain.distance_to_path(x,y,path);road_mask=np.maximum(road_mask,1-terrain.smooth((d-1.4)/1.7))
if layout.get('capital_approach'):
    d,_=terrain.distance_to_path(x,y,layout['capital_approach']);road_mask=np.maximum(road_mask,1-terrain.smooth((d-3.2)/2.5))
village=np.hypot(x-3100,y-3100)
road_mask=np.maximum(road_mask,1-terrain.smooth((village-12)/6))
for house in layout['houses']:
    d=np.hypot(x-house['x'],y-house['y']);road_mask=np.maximum(road_mask,.6*(1-terrain.smooth((d-7)/6)))
field_mask=np.zeros(x.shape,dtype='f4')
for field in layout['fields']:
    a=math.radians(field['yaw']);u=(x-field['x'])*math.cos(a)+(y-field['y'])*math.sin(a);v=-(x-field['x'])*math.sin(a)+(y-field['y'])*math.cos(a)
    outside=np.maximum(np.abs(u)-field['width']/2,np.abs(v)-field['depth']/2)
    field_mask=np.maximum(field_mask,1-terrain.smooth(outside/3))
plaza=1-terrain.smooth((village-9)/3)
# Opaque alpha: this mask carries data in RGB. Transparent PNG pixels can lose
# their colour during texture import/compression and make every path disappear.
mask=np.stack([road_mask,field_mask,plaza,np.ones_like(x)],axis=-1)
Image.fromarray(np.uint8(np.clip(mask,0,1)*255)).resize((4096,4096),Image.Resampling.BILINEAR).save(SOURCE/'T_StarterSurfaceMasks.png')
print(json.dumps(dict(composition_revision=2,houses=len(slots),terrain_changed=False)))
