"""Shared settlement surface/exclusion raster, plus guarded owned-mask baking."""
import hashlib,importlib.util,json,math,shutil
from pathlib import Path
import numpy as np
from PIL import Image
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source'
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Generate-StarterWorld.py')
T=importlib.util.module_from_spec(spec);spec.loader.exec_module(T)

def load_surface_profiles(source,places):
    """Derive surfaces from authored locations; trails keep a single source writer."""
    manifest=json.loads((source/'StarterRegion.manifest.json').read_text(encoding='utf-8'))
    profiles=json.loads((source/manifest['settlement_profiles']).read_text(encoding='utf-8'))['settlements']
    for p in profiles:
        dock=p.get('dock')
        if not dock:continue
        cx,cy=places[p['stable_id']]['center_m'][:2]
        # Placement pixels span about3.94m. Include their diagonal footprint
        # and the grass clump radius so bilinear sampling stays zero on deck.
        padding=4.0
        p['groundcover_exclusions']=[
            dict(offset_m=[dock['axis_x_m']-cx,(dock['land_y_m']+dock['head_y_m'])/2-cy],width_m=dock['width_m'],depth_m=dock['land_y_m']-dock['head_y_m'],padding_m=padding),
            dict(offset_m=[dock['axis_x_m']-cx,dock['head_y_m']-cy],width_m=dock['head_width_m'],depth_m=dock['head_depth_m'],padding_m=padding)]
    hunting=source/manifest['hunting_profiles'] if 'hunting_profiles' in manifest else None
    if hunting is not None:
        for ground in json.loads(hunting.read_text(encoding='utf-8'))['hunting_grounds']:
            cx,cy=places[ground['stable_id']]['center_m'][:2]
            plots=[dict(offset_m=[q['center_m'][0]-cx,q['center_m'][1]-cy],width=q['width'],depth=q['depth'],yaw_degrees=q['yaw_degrees']) for q in ground.get('structures',[])]
            for ref in ground.get('authored_surfaces',[]):
                file=source/ref['source'];assert hashlib.sha256(file.read_bytes()).hexdigest()==ref['sha256']
                if ref['kind']=='mine_gallery':
                    q=json.loads(file.read_text());angle=math.radians(q['yaw_degrees']);plots.append(dict(offset_m=[q['center_m'][0]+5*math.cos(angle)-cx,q['center_m'][1]+5*math.sin(angle)-cy],width=26,depth=18,yaw_degrees=q['yaw_degrees']))
            pools=[]
            for water in ground.get('water_surfaces',[]):
                file=source/water['geometry_source'];assert hashlib.sha256(file.read_bytes()).hexdigest()==water['geometry_sha256'];pools.append(next(p for p in json.loads(file.read_text())['pools'] if p['id']==water['geometry_id']))
            profiles.append(dict(stable_id=ground['stable_id'],target_id=ground['target_id'],surface_extent_m=ground['surface_extent_m'],houses=plots,fields=[],plaza=dict(enabled=False),streets=[dict(id=r['id'],width_m=r['width_m'],points_relative_m=[[q[0]-cx,q[1]-cy] for q in r['points_m']]) for r in ground['trails']],wet_pools=pools))
    if manifest.get('boss_profiles'):
        for boss in json.loads((source/manifest['boss_profiles']).read_text(encoding='utf-8'))['bosses']:
            cx,cy=places[boss['stable_id']]['center_m'][:2]
            plots=[dict(offset_m=[q['position_xy_m'][0]-cx,q['position_xy_m'][1]-cy],width=q.get('surface_mask_radius_m',q['conservative_radius_m'])*2,depth=q.get('surface_mask_radius_m',q['conservative_radius_m'])*2,yaw_degrees=q['yaw_degrees']) for q in boss['props']]
            profiles.append(dict(stable_id=boss['stable_id'],target_id=boss['target_id'],surface_extent_m=boss['surface_extent_m'],houses=plots,fields=[],plaza=dict(enabled=False),streets=[dict(id=q['id'],width_m=q['width_m'],points_relative_m=[[x-cx,y-cy] for x,y in q['points_m']]) for q in boss['trails']]))
    return profiles
def raster(p,center,x,y):
    cx,cy=center;street=np.zeros_like(x);field=np.zeros_like(x);clear=np.ones_like(x)
    roads=[dict(width_m=r['width_m'],points=[[cx+q[0],cy+q[1]] for q in r['points_relative_m']]) for r in p['streets']]
    def closest(px,py):
        result=(1e9,None)
        for road in roads:
            for a,b in zip(road['points'],road['points'][1:]):
                dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((px-a[0])*dx+(py-a[1])*dy)/max(dx*dx+dy*dy,1e-9)))
                q=[a[0]+t*dx,a[1]+t*dy];d=np.hypot(px-q[0],py-q[1])
                if d<result[0]:result=(d,q)
        return result[1]
    plots=p['houses']+p.get('tents',[])
    for q in p.get('groundcover_exclusions',[]):
        px,py=cx+q['offset_m'][0],cy+q['offset_m'][1]
        outside=np.maximum(np.abs(x-px)-q['width_m']/2,np.abs(y-py)-q['depth_m']/2)
        clear*=T.smooth((outside-q['padding_m'])/1.5)
    for h in plots:
        hx,hy=cx+h['offset_m'][0],cy+h['offset_m'][1];a=np.radians(h['yaw_degrees']) if 'yaw_degrees' in h else np.arctan2(cx-hx,hy-cy)
        u=(x-hx)*np.cos(a)+(y-hy)*np.sin(a);v=-(x-hx)*np.sin(a)+(y-hy)*np.cos(a)
        outside=np.maximum(np.abs(u)-h['width']/2-1.3,np.abs(v)-h['depth']/2-1.3);clear*=T.smooth(outside/2)
        door=[hx+np.sin(a)*(h['depth']/2+1),hy-np.cos(a)*(h['depth']/2+1)]
        roads.append(dict(width_m=2,points=[door,closest(*door)]))
    for road in roads:
        d,_=T.distance_to_path(x,y,road['points']);street=np.maximum(street,1-T.smooth((d-road['width_m']/2)/2))
        clear*=T.smooth((d-road['width_m']/2-.6)/1.6)
    for f in p['fields']+p.get('work_yards',[]):
        fx,fy=cx+f['offset_m'][0],cy+f['offset_m'][1]
        outside=np.maximum(np.abs(x-fx)-f['width_m']/2,np.abs(y-fy)-f['depth_m']/2)
        field=np.maximum(field,1-T.smooth(outside/2));clear*=T.smooth((outside-1)/2)
    config=p.get('plaza',{});px,py=config.get('offset_m',[0,0]);plaza=1-T.smooth((np.hypot(x-cx-px,y-cy-py)-config.get('radius_m',10.5))/config.get('blend_m',3)) if config.get('enabled',True) else np.zeros_like(x);clear*=1-plaza
    for pool in p.get('wet_pools',[]):
        x0,y0,x1,y1=pool['bounds_m'];roi=(x>=x0-3)&(x<=x1+3)&(y>=y0-3)&(y<=y1+3);px,py=x[roi],y[roi];distance=np.full_like(px,1e9);inside=np.zeros_like(px,dtype=bool)
        for a,b in pool['shore_segments_m']:
            dx,dy=b[0]-a[0],b[1]-a[1];length=dx*dx+dy*dy;t=np.clip(((px-a[0])*dx+(py-a[1])*dy)/max(length,1e-15),0,1);distance=np.minimum(distance,np.hypot(px-a[0]-t*dx,py-a[1]-t*dy))
            if abs(dy)>1e-12:inside^=((a[1]>py)!=(b[1]>py))&(px<dx*(py-a[1])/dy+a[0])
        clear[roi]*=np.where(inside,0,T.smooth((distance-.3)/1.2))
    return street,field,plaza,clear
def capital_infill_raster(capital,x,y):
    """Only later frontage passes; original capital is already in the base mask.

    The saved capital owns its lot/road IDs. Both incremental amendments and a
    future full settlement bake consume this same delta, avoiding a second writer.
    """
    passes=capital.get('urban_infill',[])
    road_ids={i for p in passes for i in p['street_ids']}
    plot_ids={i for p in passes for i in p['plot_ids']}
    roads=[r for r in capital['streets'] if r['id'] in road_ids]
    plots=[p for p in capital['plots'] if p['id'] in plot_ids]
    assert len(roads)==len(road_ids) and len(plots)==len(plot_ids)
    if not roads:return np.zeros_like(x),np.ones_like(x)
    profile=dict(streets=[dict(id=r['id'],width_m=r['width_m'],points_relative_m=r['points']) for r in roads],
                 houses=[dict(offset_m=p['center_m'][:2],width=p['width_m'],depth=p['depth_m'],yaw_degrees=p['yaw']) for p in plots],
                 fields=[],plaza=dict(enabled=False))
    street,_,_,clear=raster(profile,[0,0],x,y)
    # Placement texels are ~3.94m; protect foundations and short entry stairs
    # under bilinear sampling instead of painting only the exact footprint.
    for p in plots:
        a=math.radians(p['yaw']);dx=x-p['center_m'][0];dy=y-p['center_m'][1]
        outside=np.maximum(np.abs(dx*np.cos(a)+dy*np.sin(a))-p['width_m']/2,
                           np.abs(-dx*np.sin(a)+dy*np.cos(a))-p['depth_m']/2)
        clear*=T.smooth((outside-3.5)/1.5)
    return street,clear

def apply_capital_infill(data,name,capital):
    """Bound the raster to capital lots/roads, leaving every other pixel intact."""
    passes=capital.get('urban_infill',[])
    if not passes:return None
    road_ids={i for p in passes for i in p['street_ids']};plot_ids={i for p in passes for i in p['plot_ids']}
    points=[q[:2] for r in capital['streets'] if r['id'] in road_ids for q in r['points']]
    margin=20
    for p in capital['plots']:
        if p['id'] in plot_ids:
            r=math.hypot(p['width_m'],p['depth_m'])/2
            points.extend([[p['center_m'][0]-r,p['center_m'][1]-r],[p['center_m'][0]+r,p['center_m'][1]+r]])
    n=data.shape[0];bounds=[min(p[0] for p in points)-margin,min(p[1] for p in points)-margin,max(p[0] for p in points)+margin,max(p[1] for p in points)+margin]
    i0,j0=[max(0,int(v/8064*(n-1))) for v in bounds[:2]];i1,j1=[min(n,int(v/8064*(n-1))+2) for v in bounds[2:]]
    y,x=np.mgrid[j0:j1,i0:i1].astype('f4')*8064/(n-1);street,clear=capital_infill_raster(capital,x,y);crop=data[j0:j1,i0:i1]
    if name=='T_StarterSurfaceMasks':crop[:,:,0]=np.maximum(crop[:,:,0],np.uint8(np.clip(street,0,1)*255))
    else:crop[:,:,1]=np.uint8(crop[:,:,1]*np.clip(clear,0,1))
    return [i0,j0,i1,j1]

def bake():
    plan=json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'));places={p['id']:p for p in plan['places']}
    profiles=load_surface_profiles(SOURCE,places)
    backup=ROOT/'Saved/StarterWorld/BeforeV4SettlementMasks';backup.mkdir(parents=True,exist_ok=True)
    record=ROOT/'Saved/Validation/StarterWorld/Phase2/settlement-mask-bake.json'
    previous=json.loads(record.read_text(encoding='utf-8')) if record.exists() else None
    hashes={}
    for name in ['T_StarterSurfaceMasks','T_StarterPlacement']:
        path=SOURCE/(name+'.png');base=backup/path.name
        if previous:assert hashlib.sha256(path.read_bytes()).hexdigest()==previous['outputs'][name],'Preserve unrecognized mask edits'
        if not base.exists():shutil.copy2(path,base)
        data=np.array(Image.open(base)).copy();n=data.shape[0]
        for p in profiles:
            cx,cy=places[p['stable_id']]['center_m'][:2];extent=p.get('surface_extent_m',200)
            i0,i1=max(0,int((cx-extent)/8064*(n-1))),min(n,int((cx+extent)/8064*(n-1))+1)
            j0,j1=max(0,int((cy-extent)/8064*(n-1))),min(n,int((cy+extent)/8064*(n-1))+1)
            y,x=np.mgrid[j0:j1,i0:i1].astype('f4')*8064/(n-1);street,field,plaza,clear=raster(p,[cx,cy],x,y);crop=data[j0:j1,i0:i1]
            if name=='T_StarterSurfaceMasks':
                for channel,mask in enumerate([street,field,plaza]):crop[:,:,channel]=np.maximum(crop[:,:,channel],np.uint8(np.clip(mask,0,1)*255))
            else:crop[:,:,1]=np.uint8(crop[:,:,1]*np.clip(clear,0,1))
        # Keep the same order as the incremental capital amendment: other
        # settlement surfaces first, then later capital frontage exclusions.
        apply_capital_infill(data,name,json.loads((SOURCE/'StarterWorld.capital.json').read_text()))
        data[:,:,3]=255;Image.fromarray(data).save(path);hashes[name]=hashlib.sha256(path.read_bytes()).hexdigest()
    record.write_text(json.dumps(dict(outputs=hashes,height_modified=False,settlements=[p['stable_id'] for p in profiles]),indent=2),encoding='utf-8')
    print('Settlement roads/fields/plaza masks baked; height unchanged')
if __name__=='__main__':bake()
