"""Native saved HISM habitat chunks from the curated terrain-aware plan."""
import importlib.util,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SRC=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Structure';FILE=OUT/'natural-diversity-build.json';P=json.loads((SRC/'StarterWorld.natural-diversity.json').read_text());TAG=P['native_tag']
TREE_CULL=json.loads((SRC/'StarterWorld.forest-streaming.json').read_text()).get('tree_instance_cull_cm',[0,0])
assert not FILE.exists(),'Inspect checkpoint and scene before any recovery'
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages() and not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
spec=importlib.util.spec_from_file_location('authoring',Path(__file__).with_name('Starter-SettlementAuthoring.py'));M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M);A=M.SettlementAuthoring(TAG,'StarterRegion/Environment/NaturalDiversity')
assert not any(TAG in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
R=dict(phase='READY',completed=[],counts=P['counts'],chunks=len(P['chunks']),height_modified=False,actors=[]);chunks=list(P['chunks'].items());index=0;busy=False
def record(phase):R.update(phase=phase,actors=A.created);FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def tick(dt):
    global index,busy
    if busy:return
    busy=True
    try:
        if index==len(chunks):
            A.save();R['saved']=True;record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
        key,entries=chunks[index];record('CREATE_UNKNOWN_'+key);i,j=[int(s) for s in key.split('_')];cx,cy=(i+.5)*256,(j+.5)*256
        origin_z=sum(q['position_m'][2] for q in entries)/len(entries) if entries else 0
        actor=A.spawn('Nature_'+key,'/Engine/BasicShapes/Cube',cx,cy,origin_z,collision=False);actor.static_mesh_component.set_static_mesh(None);actor.set_actor_location(A.point(cx,cy,origin_z),False,False)
        actor.set_editor_property('enable_auto_lod_generation',True);groups={}
        if P.get('forest_hlod_layer'):actor.set_editor_property('hlod_layer',unreal.load_asset(P['forest_hlod_layer']))
        for p in entries:
            path=p['mesh'];kind=p['kind']
            if path not in groups:
                mesh=unreal.load_asset(path);assert isinstance(mesh,unreal.StaticMesh),path
                comp=A.add_static_component(actor,path,'Instances_'+str(len(groups)),unreal.HierarchicalInstancedStaticMeshComponent)
                overrides=P.get('mesh_material_overrides',{}).get(path)
                for slot in range(len(mesh.static_materials)):
                    material=unreal.load_asset(overrides[slot]) if overrides else mesh.static_materials[slot].material_interface;assert material;comp.set_material(slot,A.instanced_material(material))
                comp.set_collision_profile_name('BlockAll')
                comp.set_editor_property('enable_auto_lod_generation',kind=='Tree')
                comp.set_cull_distances(*(TREE_CULL if kind=='Tree' else [220000,360000]));groups[path]=(mesh,comp)
            mesh,comp=groups[path];b=mesh.get_bounds();o,e=b.origin,b.box_extent;sx,sy,sz=p['scale'];a=math.radians(p['yaw']);co,si=math.cos(a),math.sin(a);x,y,z=p['position_m'];z-=e.z*.02*sz*p['sink_fraction']
            pos=A.point(x,y,z)-unreal.Vector(co*o.x*sx-si*o.y*sy,si*o.x*sx+co*o.y*sy,(o.z-e.z)*sz)
            comp.add_instance(unreal.Transform(location=pos,rotation=unreal.Rotator(pitch=0,yaw=p['yaw'],roll=0),scale=unreal.Vector(sx,sy,sz)),True)
        R['completed'].append(key);index+=1
        if index%20==0 or index==len(chunks):A.save()
        record('READY')
    except Exception:R['error']=traceback.format_exc();record(R['phase'] if R['phase'].startswith('CREATE_UNKNOWN') else 'ERROR');unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(R['error'])
    finally:busy=False
record('READY');handle=unreal.register_slate_post_tick_callback(tick)
