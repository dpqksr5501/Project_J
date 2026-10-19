"""Promote the validated biome contract to all suitable starter-region land.

Own masks, material and grass types only. Retires generated corridor grass after
archiving its exact instances; never removes trees, wheat, rocks or buildings.
"""
import hashlib,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source'
OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';BASE='/Game/World/MainWorld'
TAG='ProjectJ.Starter.Biome.v1';OWNER='ProjectJ.Starter.WorldBiome.v2'
PRESET_PATH=SOURCE/'MainWorld.biomes.json';PRESET=json.loads(PRESET_PATH.read_text(encoding='utf-8'))
assert PRESET['schema']==2 and not PRESET['height_editing']
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith('/Game/World/MainWorld/Legacy/Maps/L_StarterRegion.')
assert (ROOT/'Saved/StarterWorld/BeforeWorldBiome/Materials').exists()
LIB=unreal.EditorAssetLibrary;EDIT=unreal.MaterialEditingLibrary;TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
REPORT_PATH=OUT/'world-biome-build.json'
REPORT=json.loads(REPORT_PATH.read_text(encoding='utf-8')) if REPORT_PATH.exists() else dict(completed=[],phase='QUEUED',height_modified=False)
assert REPORT['phase']!='COMPLETE','Inspect saved output; do not overwrite completed authoring automatically'
assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(),'Preserve unsaved map edits'
HEIGHT_HASH=hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest()
assert HEIGHT_HASH=='bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8'
TOUCHED=set()


def record(phase):
    REPORT['phase']=phase;REPORT_PATH.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')


def save():
    dirty=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    packages=[p for p in dirty if p.get_name() in TOUCHED or p.get_name()=='/Game/World/MainWorld/Legacy/Maps/L_StarterRegion' or p.get_name().startswith('/Game/__ExternalActors__/World/MainWorld/Legacy/')]
    if packages:assert unreal.EditorLoadingAndSavingUtils.save_packages(packages,True)
    REPORT['unrelated_dirty_preserved']=[p.get_name() for p in dirty if p not in packages]


def struct(cls,**values):
    obj=cls()
    for k,v in values.items():obj.set_editor_property(k,v)
    return obj


def mask_stage():
    for name in ['T_MainWorldEcology','T_MainWorldPlacement']:
        path='/Game/Map_Resource/MainWorld/Materials/'+name;obj=unreal.load_asset(path);assert obj and LIB.get_metadata_tag(obj,'ProjectJ.Authoring')==TAG
        task=unreal.AssetImportTask()
        for k,v in dict(filename=str(SOURCE/(name+'.png')),destination_path='/Game/Map_Resource/MainWorld/Materials',destination_name=name,automated=True,save=False,replace_existing=True,replace_existing_settings=False).items():task.set_editor_property(k,v)
        TOOLS.import_asset_tasks([task]);obj=unreal.load_asset(path)
        obj.set_editor_property('srgb',False);obj.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
        obj.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP);obj.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
        LIB.set_metadata_tag(obj,'ProjectJ.Authoring',TAG);TOUCHED.add(path)


def instancing_material(interface):
    base=interface.get_base_material()
    if base.get_editor_property('used_with_instanced_static_meshes'):return interface
    # Preserve every inherited instance layer if an unprepared provider material
    # needs instancing. Overrides reference owned copies, not a changed provider.
    suffix=hashlib.sha256(interface.get_path_name().encode()).hexdigest()[:8]
    path='/Game/Map_Resource/MainWorld/Materials/Biome/'+interface.get_name()+'_'+suffix
    copy=unreal.load_asset(path)
    if copy is None:
        copy=LIB.duplicate_asset(interface.get_path_name().split('.')[0],path);assert copy
        LIB.set_metadata_tag(copy,'ProjectJ.Authoring',OWNER)
    assert LIB.get_metadata_tag(copy,'ProjectJ.Authoring')==OWNER
    if isinstance(copy,unreal.Material):EDIT.set_material_usage(copy,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
    else:EDIT.set_material_instance_parent(copy,instancing_material(interface.get_editor_property('parent')))
    TOUCHED.add(path);return copy


def palette_stage():
    LIB.make_directory('/Game/Map_Resource/MainWorld/Materials/Biome');paths={};counts={}
    for palette,species in PRESET['groundcover_palettes'].items():
        path='/Game/Map_Resource/MainWorld/Materials/'+('LG_MainWorldUnderstory' if palette=='Meadow' else 'LG_MainWorld'+palette)
        obj=unreal.load_asset(path)
        if obj is None:
            obj=LIB.duplicate_asset(PRESET['groundcover_source'],path);assert obj
            LIB.set_metadata_tag(obj,'ProjectJ.Authoring',OWNER)
        assert LIB.get_metadata_tag(obj,'ProjectJ.Authoring') in [TAG,OWNER]
        varieties=[]
        for entry in species:
            mesh=unreal.load_asset(entry['mesh']);assert isinstance(mesh,unreal.StaticMesh),entry['mesh']
            density=entry['density'];lo,hi=entry['scale']
            v=unreal.GrassVariety();start_cull,end_cull=entry.get('cull_cm',[PRESET['grass_start_cull_cm'],PRESET['grass_end_cull_cm']])
            values=dict(grass_mesh=mesh,grass_density=struct(unreal.PerPlatformFloat,default=density),
                        grass_density_quality=struct(unreal.PerQualityLevelFloat,default=density),
                        start_cull_distance=struct(unreal.PerPlatformInt,default=start_cull),
                        end_cull_distance=struct(unreal.PerPlatformInt,default=end_cull),
                        start_cull_distance_quality=struct(unreal.PerQualityLevelInt,default=start_cull),
                        end_cull_distance_quality=struct(unreal.PerQualityLevelInt,default=end_cull),
                        scaling=unreal.GrassScaling.UNIFORM,scale_x=struct(unreal.FloatInterval,min=lo,max=hi),
                        scale_y=struct(unreal.FloatInterval,min=lo,max=hi),scale_z=struct(unreal.FloatInterval,min=lo,max=hi),
                        random_rotation=True,align_to_surface=True,cast_dynamic_shadow=False,cast_contact_shadow=False)
            for k,value in values.items():v.set_editor_property(k,value)
            v.set_editor_property('override_materials',[instancing_material(slot.material_interface) if slot.material_interface else None for slot in mesh.static_materials])
            varieties.append(v)
        obj.modify();obj.set_editor_property('grass_varieties',varieties);TOUCHED.add(path);paths[palette]=path;counts[palette]=len(varieties)
    REPORT.update(grass_types=paths,palette_variety_counts=counts,unique_groundcover_meshes=len({s['mesh'] for p in PRESET['groundcover_palettes'].values() for s in p}))


def output_stage():
    mat=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain_Biome');assert LIB.get_metadata_tag(mat,'ProjectJ.Authoring')==TAG
    expressions=list(EDIT.get_material_expressions(mat))
    output=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionLandscapeGrassOutput))
    inputs={'P':next(e for e in expressions if isinstance(e,unreal.MaterialExpressionWorldPosition))}
    for expr in expressions:
        if isinstance(expr,unreal.MaterialExpressionTextureObject):
            tex=expr.get_editor_property('texture')
            if tex.get_name() in ['T_MainWorldEcology','T_MainWorldPlacement']:inputs['Ecology' if tex.get_name()=='T_MainWorldEcology' else 'Placement']=expr
    assert len(inputs)==3
    # Delete only the previous generated grass expression, keeping all surface logic.
    for expr in expressions:
        if isinstance(expr,unreal.MaterialExpressionCustom) and ('float existing=' in expr.get_editor_property('code') or expr.get_editor_property('desc')==OWNER):EDIT.delete_material_expression(mat,expr)
    pins=[]
    for name,path in REPORT['grass_types'].items():pins.append(struct(unreal.GrassInput,name=name,grass_type=unreal.load_asset(path)))
    output.set_editor_property('grass_types',pins)
    weights=dict(Meadow='(1-f)*(1-w)*(1-a)',Forest='f*(1-w)*(1-a)',Riparian='w*(1-a)',Alpine='a*(1-e.g*.65)')
    for name in REPORT['grass_types']:
        code='float2 uv=(P.xy/100+4032)/8064; float4 m=Texture2DSample(Placement,PlacementSampler,uv); float4 e=Texture2DSample(Ecology,EcologySampler,uv); float f=saturate(e.r); float w=saturate(e.b); float a=smoothstep(75000,100000,P.z); return m.r*m.g*('+weights[name]+');'
        node=EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom,0,600)
        node.set_editor_property('desc',OWNER);node.set_editor_property('code',code);node.set_editor_property('output_type',unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        node.set_editor_property('inputs',[struct(unreal.CustomInput,input_name=k) for k in inputs])
        for key,value in inputs.items():assert EDIT.connect_material_expressions(value,'',node,key)
        assert EDIT.connect_material_expressions(node,'',output,name)
    errors=EDIT.recompile_material(mat);assert not errors,list(errors)
    LIB.set_metadata_tag(mat,'ProjectJ.GroundcoverOwner',OWNER);TOUCHED.add('/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain_Biome')
    REPORT['material_expressions']=len(EDIT.get_material_expressions(mat))


def retire_stage():
    descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
    unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
    archive=OUT/'retired-corridor-grass.json';owned=[];entries=[]
    for actor in ACTORS.get_all_level_actors():
        if 'ProjectJ.Starter.Corridor.v1' not in [str(t) for t in actor.tags] or not actor.get_actor_label().startswith('S00_Grass_'):continue
        comps=actor.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
        assert len(comps)==1 and comps[0].get_name()=='GrassInstances'
        assert not actor.get_attached_actors(),'Preserve attached manual work'
        comp=comps[0];transforms=[]
        for i in range(comp.get_instance_count()):
            t=comp.get_instance_transform(i,True);r=t.rotation.rotator()
            transforms.append([list(t.translation.to_tuple()),[r.pitch,r.yaw,r.roll],list(t.scale3d.to_tuple())])
        entries.append(dict(label=actor.get_actor_label(),mesh=comp.static_mesh.get_path_name(),transforms=transforms));owned.append(actor)
    expected=json.loads((ROOT/'Saved/Validation/StarterWorld/corridor-build.json').read_text(encoding='utf-8'))['counts']['Grass']
    assert sum(len(e['transforms']) for e in entries)==expected
    assert not archive.exists(),'Inspect prior retirement before repeating'
    archive.write_text(json.dumps(dict(reason='LandscapeGrass ownership, not unused asset deletion',actors=entries),indent=2),encoding='utf-8')
    for actor in owned:assert ACTORS.destroy_actor(actor)
    PRESET['retired_legacy_grass_actors']=len(owned);PRESET['retired_legacy_grass_instances']=expected
    PRESET_PATH.write_text(json.dumps(PRESET,indent=2),encoding='utf-8')
    REPORT.update(retired_legacy_grass_actors=len(owned),retired_legacy_grass_instances=expected)
    assert hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest()==HEIGHT_HASH
    REPORT['height_sha256']=HEIGHT_HASH


stages=[(n,f) for n,f in [('masks',mask_stage),('palettes',palette_stage),('output',output_stage),('retirement',retire_stage)] if n not in REPORT['completed']]
last=time.perf_counter();busy=False


def tick(dt):
    global last,busy
    if busy or time.perf_counter()-last<3:return
    if not stages:
        record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
    name,fn=stages.pop(0);busy=True;record(name+'_RUNNING')
    try:fn();save();REPORT['completed'].append(name);record(name+'_DONE')
    except Exception:
        REPORT['error']=traceback.format_exc();record(name+'_FAILED');unreal.log_error(REPORT['error']);unreal.unregister_slate_post_tick_callback(handle)
    finally:busy=False;last=time.perf_counter()


record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
