"""Bake shared surface/vegetation rules, without changing a single height sample.

Editor authoring consumes the same masks as the material. Regions, scales and
asset choices live in an editable preset; the preset is never silently reset.
"""
import hashlib,importlib.util,json,math
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source'
OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Generate-StarterWorld.py')
T=importlib.util.module_from_spec(spec);spec.loader.exec_module(T)


def exclusion_fields(x,y,plan,layout):
    """Shared authored clearance, independent of vegetation slope eligibility."""
    water=np.full_like(x,1e9)
    for river in plan['rivers']:
        d,_=T.distance_to_path(x,y,river['points']);water=np.minimum(water,d-river['width_m']/2)
    lake=plan['lake'];cx,cy=lake['center_m'];rx,ry=lake['radius_m']
    water=np.minimum(water,(np.sqrt(((x-cx)/rx)**2+((y-cy)/ry)**2)-1)*min(rx,ry))
    clear=np.ones_like(x)
    for road in plan['roads']:
        d,_=T.distance_to_path(x,y,road['points']);clear*=T.smooth((d-road['width_m']/2-3)/5)
    for path in layout['footpaths']:
        d,_=T.distance_to_path(x,y,path);clear*=T.smooth((d-1.7)/1.6)
    for house in layout['houses']:
        a=math.radians(house['yaw']);u=(x-house['x'])*math.cos(a)+(y-house['y'])*math.sin(a);v=-(x-house['x'])*math.sin(a)+(y-house['y'])*math.cos(a)
        outside=np.maximum(np.abs(u)-house['width']/2-1.3,np.abs(v)-house['depth']/2-1.3)
        clear*=T.smooth(outside/2)
    for field in layout['fields']:
        a=math.radians(field['yaw']);u=(x-field['x'])*math.cos(a)+(y-field['y'])*math.sin(a);v=-(x-field['x'])*math.sin(a)+(y-field['y'])*math.cos(a)
        clear*=T.smooth(np.maximum(np.abs(u)-field['width']/2-3,np.abs(v)-field['depth']/2-3)/4)
    clear*=T.smooth((np.hypot(x-3100,y-3100)-13)/5)*T.smooth((water-6)/9)
    # Reserve the complete gate/bridge approach; not just the plan centerline.
    gate=layout['capital_gate_center_m'];d=np.hypot(x-gate[0],y-gate[1]);clear*=T.smooth((d-65)/20)
    capital_path=SOURCE/'MainWorld.capital.json'
    if capital_path.exists():
        capital=json.loads(capital_path.read_text(encoding='utf-8'))
        for road in capital['streets']:
            d,_=T.distance_to_path(x,y,road['points']);clear*=T.smooth((d-road['width_m']/2-1)/2)
        for key,radius in [('market',21),('ridge',19)]:
            cx,cy=capital['nodes'][key];clear*=T.smooth((np.hypot(x-cx,y-cy)-radius-1)/3)
        for plot in capital['plots']:
            a=math.radians(plot['yaw']);dx=x-plot['center_m'][0];dy=y-plot['center_m'][1]
            u=dx*math.cos(a)+dy*math.sin(a);v=-dx*math.sin(a)+dy*math.cos(a)
            clear*=T.smooth(np.maximum(np.abs(u)-plot['width_m']/2-1,np.abs(v)-plot['depth_m']/2-1)/2)
    settlements_path=SOURCE/'MainWorld.settlements.json'
    if settlements_path.exists():
        raster_spec=importlib.util.spec_from_file_location('settlement_raster',ROOT/'Scripts/World/Bake-StarterSettlementSurfaces.py')
        raster_module=importlib.util.module_from_spec(raster_spec);raster_spec.loader.exec_module(raster_module)
        for settlement in raster_module.load_surface_profiles(SOURCE,{p['id']:p for p in plan['places']}):
            center=next(p['center_m'][:2] for p in plan['places'] if p['id']==settlement['stable_id'])
            clear*=raster_module.raster(settlement,center,x,y)[3]
    return clear,water


def generate():
    preset_path=SOURCE/'MainWorld.biomes.json'
    if not preset_path.exists():
        preset=dict(schema=1,seed=103031,height_editing=False,
                    regions=[dict(id='S00_forest_edge',bounds_m=[2790,2940,3670,3800],blend_m=85),dict(id='S01_river_approach',bounds_m=[3850,3930,4330,4440],blend_m=60)],
                    shrub_spacing_m=7,shrub_max_count=700,shrub_max_slope_degrees=31,
                    chunk_m=128,grass_density=34,clover_density=6,
                    grass_start_cull_cm=7500,grass_end_cull_cm=14000,
                    shrub_start_cull_cm=24000,shrub_end_cull_cm=36000,
                    bush_meshes=['/Game/Map_Resource/Nanite_Env_Bundle_1/StunningNaturalPlace/Meshes/Foliage/Bush/S_Bush_Type_1_Small_'+str(i) for i in [1,3,5]],
                    groundcover_source='/Game/Map_Resource/Brushify/Materials/Landscape/GrassTypes/LG_Grass',
                    existing_grass_bounds_m=[2960,2930,3310,3330],
                    description='Preserve height and manual placement. Surface and understory share authored exclusion fields.')
        preset_path.write_text(json.dumps(preset,indent=2),encoding='utf-8')
    preset=json.loads(preset_path.read_text(encoding='utf-8'));assert not preset['height_editing']
    height_file=SOURCE/'HM_MainWorld.r16';fingerprint=hashlib.sha256(height_file.read_bytes()).hexdigest()
    plan=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'))
    layout=json.loads((SOURCE/'MainWorld.corridor.json').read_text(encoding='utf-8'))
    palette=json.loads((OUT/'biome-palette.json').read_text(encoding='utf-8'))
    h=(np.fromfile(height_file,dtype='<u2').reshape(4033,4033)[::4,::4].astype('f4')-32768)*.0625
    y,x=np.mgrid[:1009,:1009].astype('f4')*8
    coverage=np.zeros_like(h)
    for region in preset['regions']:
        x0,y0,x1,y1=region['bounds_m'];outside=np.maximum.reduce([x0-x,x-x1,y0-y,y-y1])
        coverage=np.maximum(coverage,1-T.smooth(outside/region['blend_m']))
    if preset['schema']>=2:coverage*=T.smooth((h-1)/4)
    gy,gx=np.gradient(h,8);slope=np.hypot(gx,gy)
    clear,water=exclusion_fields(x,y,plan,layout)
    canopy=np.zeros_like(h)
    assert len(palette['trees_m'])>=500,'Load the authored tree chunks before exporting their canopy'
    for tx,ty in palette['trees_m']:
        i,j=round(tx/8),round(ty/8);i0,i1=max(0,i-5),min(1009,i+6);j0,j1=max(0,j-5),min(1009,j+6)
        q=np.exp(-((x[j0:j1,i0:i1]-tx)**2+(y[j0:j1,i0:i1]-ty)**2)/(2*13**2))
        canopy[j0:j1,i0:i1]=np.maximum(canopy[j0:j1,i0:i1],q)
    patch=np.clip(.5+.45*T.noise(x,y,80,preset['seed']),0,1)
    forest=np.clip(canopy*.82+patch*.15,0,1)*(1-T.smooth((slope-.7)/.5))
    if preset['schema']>=2:
        cfg=preset['forest_distribution']
        regional=T.smooth((T.noise(x,y,cfg['macro_spacing_m'],preset['seed']+43)+.18)/.7)
        regional*=T.smooth((h-cfg['lower_m'])/40)*(1-T.smooth((h-cfg['upper_m'])/cfg['transition_m']))
        regional*=1-T.smooth((slope-.55)/.65)
        # Reserve settlement/encounter cores before trees or dense undergrowth.
        for place in plan['places']:
            distance=np.hypot(x-place['center_m'][0],y-place['center_m'][1])
            radius=place['radius_m'] if place['id'].startswith('S') else place['radius_m']*.55
            regional*=T.smooth((distance-radius)/90)
        forest=np.maximum(forest,regional*.88)
    stony=T.smooth((slope-.22)/.55)*(.45+.55*patch)
    moist=(1-T.smooth((water-5)/28))*(1-T.smooth((slope-.35)/.3))
    veg=clear*(1-T.smooth((slope-.42)/.23))
    # Both PNGs have opaque alpha: importers must not discard meaningful RGB.
    masks=[('T_MainWorldEcology',[forest,stony,moist,np.ones_like(h)]),('T_MainWorldPlacement',[coverage,veg,canopy,np.ones_like(h)])]
    if preset['schema']>=2:
        tree=forest*veg*coverage*T.smooth((h-18)/25)*(1-T.smooth((h-900)/130))
        for place in plan['places']:
            distance=np.hypot(x-place['center_m'][0],y-place['center_m'][1])
            radius=place['radius_m'] if place['id'].startswith('S') else place['radius_m']*.6
            tree*=T.smooth((distance-radius)/80)
        shrubs=tree.copy()
        tree*=1-T.smooth((canopy-.08)/.2)
        original=json.loads((OUT/'biome-placement.json').read_text(encoding='utf-8'))
        for p in original['points']:
            i,j=round(p['x_m']/8),round(p['y_m']/8);i0,i1=max(0,i-2),min(1009,i+3);j0,j1=max(0,j-2),min(1009,j+3)
            distance=np.hypot(x[j0:j1,i0:i1]-p['x_m'],y[j0:j1,i0:i1]-p['y_m'])
            shrubs[j0:j1,i0:i1]*=T.smooth((distance-7)/6)
        masks.append(('T_MainWorldForestPlacement',[tree,shrubs,stony,np.ones_like(h)]))
    for name,channels in masks:
        image=np.uint8(np.clip(np.stack(channels,axis=-1),0,1)*255)
        Image.fromarray(image).resize((2048,2048),Image.Resampling.BILINEAR).save(SOURCE/(name+'.png'))
    # Preserve the analysis grid for deterministic native authoring, not runtime loading.
    eligible=(coverage>.65)&(veg>.65)&(canopy>.12)&(slope<math.tan(math.radians(preset['shrub_max_slope_degrees'])))
    points=[];occupied=set();rng=np.random.default_rng(preset['seed'])
    indices=np.argwhere(eligible);rng.shuffle(indices)
    for j,i in indices:
        if rng.random()>.34+.34*canopy[j,i]:continue
        tx,ty=float(x[j,i]),float(y[j,i]);cell=(int(tx/preset['shrub_spacing_m']),int(ty/preset['shrub_spacing_m']))
        if any((cell[0]+dx,cell[1]+dy) in occupied for dx in [-1,0,1] for dy in [-1,0,1]):continue
        occupied.add(cell)
        points.append(dict(x_m=tx,y_m=ty,z_m=float(h[j,i]),yaw=float(rng.uniform(0,360)),scale=float(rng.uniform(.48,.88)),mesh_index=int(rng.integers(len(preset['bush_meshes'])))))
        if len(points)>=preset['shrub_max_count']:break
    data=dict(height_sha256=fingerprint,preset_sha256=hashlib.sha256(preset_path.read_bytes()).hexdigest(),points=points,
              mask_coverage_fraction=float(np.mean(coverage>.5)),height_modified=False)
    output='world-biome-placement.json' if preset['schema']>=2 else 'biome-placement.json'
    (OUT/output).write_text(json.dumps(data,indent=2),encoding='utf-8')
    assert hashlib.sha256(height_file.read_bytes()).hexdigest()==fingerprint
    print(json.dumps(dict(height_modified=False,shrubs=len(points),coverage_fraction=data['mask_coverage_fraction'])))


if __name__=='__main__':generate()
