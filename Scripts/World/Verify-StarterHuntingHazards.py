"""Measure actual rock-edge falls and separation from authored hunting paths."""
import hashlib,importlib.util,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';target=globals().get('TARGET_ID','H07');manifest=json.loads((SOURCE/'MainWorld.manifest.json').read_text());P=next(v for v in json.loads((SOURCE/manifest['hunting_profiles']).read_text())['hunting_grounds'] if v['target_id']==target);B=json.loads((OUT/(target+'-hunting-build.json')).read_text());assert B['phase']=='COMPLETE' and not unreal.EditorLevelLibrary.get_pie_worlds(False);origin=manifest['world_grid']['unreal_origin_cm'];units=manifest['world_grid']['centimetres_per_source_metre']
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');W=module('water','Starter-WetlandGeometry.py')
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
def source(p):return [(p.x-origin[0])/units,(p.y-origin[1])/units,(p.z-origin[2])/units]
def line(world,x,y,top,bottom,ignore=()):
    hit=unreal.SystemLibrary.line_trace_single(world,point(x,y,top),point(x,y,bottom),unreal.TraceTypeQuery.ECC_VISIBILITY,True,list(ignore),unreal.DrawDebugTrace.NONE,True);return hit.to_tuple() if hit else None
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);R=dict(passed=False,read_only=True,target_id=target)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();paths={a.get_path_name():a for a in actors};world=unreal.EditorLevelLibrary.get_editor_world();checks=[]
        for hazard in P['hazards']:
            row=next(v for v in B['props'] if v['id']==hazard['prop_id']);q=next(v for v in P['props'] if v['id']==hazard['prop_id']);a=paths[row['path']];c=a.static_mesh_component;assert c.static_mesh.get_path_name().split('.')[0]==q['mesh'];center,extent=a.get_actor_bounds(False);assert abs((center.z+extent.z-origin[2])/units-q['vertical_anchor']['height_m'])<.001;best=None;cap_samples=0
            x0,y0=source(center-extent)[:2];x1,y1=source(center+extent)[:2];top=(center.z+extent.z-origin[2])/units;bottom=(center.z-extent.z-origin[2])/units
            for x in range(math.ceil(x0),math.floor(x1)+1,2):
                for y in range(math.ceil(y0),math.floor(y1)+1,2):
                    hit=line(world,x,y,top+3,bottom-1)
                    if not hit or not hit[0] or hit[10]!=c or hit[7].z<.8:continue
                    cap=source(hit[4])
                    if cap[2]<T.height(x,y)+1:continue
                    cp=hit[4]+unreal.Vector(0,0,111);capsule=unreal.SystemLibrary.capsule_trace_single(world,cp,cp+unreal.Vector(0,0,.1),42,96,unreal.TraceTypeQuery.ECC_VISIBILITY,True,[],unreal.DrawDebugTrace.NONE,True)
                    if capsule and capsule.to_tuple()[0]:continue
                    cap_samples+=1
                    for dx,dy in [(2,0),(-2,0),(0,2),(0,-2),(3,0),(-3,0),(0,3),(0,-3)]:
                        toe=line(world,x+dx,y+dy,top+3,bottom-3)
                        if not toe or not toe[0]:continue
                        low=source(toe[4]);drop=cap[2]-low[2]
                        if drop>=hazard['minimum_verified_drop_m'] and (best is None or drop>best['drop_m']):best=dict(cap_position_m=cap,landing_position_m=low,drop_m=drop,horizontal_separation_m=math.hypot(dx,dy),cap_normal_z=hit[7].z,landing_actor=toe[9].get_path_name(),cap_capsule_clear=True)
            assert best,('No measured abrupt rock fall',hazard['id'],cap_samples);trail_clearance=min(math.dist(v,hazard['center_m'][:2])-hazard['radius_m']-trail['width_m']/2 for trail in P['trails'] for v in trail['points_m']);combat_clearance=min(math.dist(v['center_m'][:2],hazard['center_m'][:2])-hazard['radius_m']-v['radius_m'] for v in P['clusters']);assert trail_clearance>0 and combat_clearance>=hazard['spawn_buffer_m'];checks.append(dict(id=hazard['id'],physical_drop=best,walkable_cap_query_samples=cap_samples,trail_edge_hazard_clearance_m=trail_clearance,combat_circle_hazard_clearance_m=combat_clearance,scope='Actual collider fall and exclusion only; reaching rock cap/fall damage/AI behavior unverified'))
        water=[]
        for ref in P['coastal_water_references']:
            file=SOURCE/ref['geometry_source'];assert hashlib.sha256(file.read_bytes()).hexdigest()==ref['geometry_sha256'];water.append(json.loads(file.read_text()))
        trail_water=min(W.surface_distance(*v,g)-trail['width_m']/2 for trail in P['trails'] for v in trail['points_m'] for g in water);combat_water=min(W.surface_distance(*v['center_m'][:2],g)-v['radius_m'] for v in P['clusters'] for g in water);minimum_ground=min(T.height(*v) for trail in P['trails'] for v in trail['points_m']);assert trail_water>=P['coastal_clearance']['trail_water_margin_m'] and combat_water>=P['coastal_clearance']['combat_water_margin_m'] and minimum_ground>P['coastal_clearance']['minimum_land_height_m'];R.update(passed=True,hazards=checks,minimum_trail_water_edge_clearance_m=trail_water,minimum_combat_water_clearance_m=combat_water,minimum_trail_ground_height_m=minimum_ground,height_sha256=T.manifest['terrain_profile']['sha256'],alternate_route_id='H07.InlandBypass',art_complete=False,cliff_cap_access_walked=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:R['error']=traceback.format_exc();unreal.log_error(R['error'])
    (OUT/(target+'-hunting-hazard-verification.json')).write_text(json.dumps(R,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
