"""Explicit bounded biome authoring. Never edits heights or provider packages.

Derived material retains the legacy surface outside the authored mask. Grass
uses Landscape Grass; spatial HISM owns shrubs, not the same groundcover.
"""
import hashlib,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source'
OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';BASE='/Game/World/MainWorld'
MAP='/Game/World/MainWorld/Legacy/Maps/L_StarterRegion';TAG='ProjectJ.Starter.Biome.v1'
REPORT_PATH=OUT/'biome-build.json'
PREVIOUS=json.loads(REPORT_PATH.read_text(encoding='utf-8')) if REPORT_PATH.exists() else None
LIB=unreal.EditorAssetLibrary;EDIT=unreal.MaterialEditingLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools();ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
PRESET=json.loads((SOURCE/'MainWorld.biomes.json').read_text(encoding='utf-8'))
PLACEMENT=json.loads((OUT/'biome-placement.json').read_text(encoding='utf-8'))
assert not PRESET['height_editing']
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith(MAP+'.')
dirty_maps=unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()
assert not dirty_maps or (PREVIOUS and PREVIOUS['phase']=='assignment_FAILED' and all(p.get_name().startswith((MAP,'/Game/__ExternalActors__/World/MainWorld/Legacy/')) for p in dirty_maps)),'Preserve unsaved user map edits'
assert hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest()==PLACEMENT['height_sha256']
REPORT=dict(phase='QUEUED',height_modified=False,completed=[],preserved_provider_packages=True)
if REPORT_PATH.exists():
    previous=json.loads(REPORT_PATH.read_text(encoding='utf-8'))
    repair=bool(globals().get('repair_owned_placement',False))
    assert (repair and previous['phase']=='COMPLETE') or (previous['phase'] in ['material_FAILED','assignment_FAILED'] and set(previous['completed'])<={'understory','material'}),'Inspect previous execution before replacing outputs'
    REPORT.update({k:v for k,v in previous.items() if k not in ['error','phase','repair_history']})
    REPORT['repair_history']=previous.get('repair_history',[])+([previous['error']] if 'error' in previous else [])


def record(phase):
    REPORT['phase']=phase;REPORT_PATH.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')


def owned_save():
    dirty=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    owned=[p for p in dirty if p.get_name().startswith((BASE+'/', '/Game/Map_Resource/MainWorld/', '/Game/__ExternalActors__/World/MainWorld/Legacy/','/Game/__ExternalObjects__/World/MainWorld/Legacy/'))]
    if owned:assert unreal.EditorLoadingAndSavingUtils.save_packages(owned,True)
    REPORT['unrelated_dirty_preserved']=[p.get_name() for p in dirty if p not in owned]


def texture_node(mat,key,path):
    obj=unreal.load_asset(path);assert isinstance(obj,unreal.Texture2D),path
    assert not obj.get_editor_property('virtual_texture_streaming'),'Custom material path requires non-VT texture'
    node=EDIT.create_material_expression(mat,unreal.MaterialExpressionTextureObject,-1000,0)
    node.set_editor_property('texture',obj);node.set_editor_property('desc',key)
    return node


def custom(mat,code,inputs,kind=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
    expr=EDIT.create_material_expression(mat,unreal.MaterialExpressionCustom,0,0)
    expr.set_editor_property('code',code);expr.set_editor_property('output_type',kind)
    descriptors=[]
    for k in inputs:
        descriptor=unreal.CustomInput();descriptor.set_editor_property('input_name',k);descriptors.append(descriptor)
    expr.set_editor_property('inputs',descriptors)
    for k,v in inputs.items():assert EDIT.connect_material_expressions(v,'',expr,k)
    return expr


def material_stage():
    for name in ['T_MainWorldEcology','T_MainWorldPlacement']:
        path='/Game/Map_Resource/MainWorld/Materials/'+name;obj=unreal.load_asset(path)
        if obj is None:
            task=unreal.AssetImportTask()
            for k,v in dict(filename=str(SOURCE/(name+'.png')),destination_path='/Game/Map_Resource/MainWorld/Materials',destination_name=name,automated=True,save=False).items():task.set_editor_property(k,v)
            TOOLS.import_asset_tasks([task]);obj=unreal.load_asset(path);assert obj
            LIB.set_metadata_tag(obj,'ProjectJ.Authoring',TAG)
        assert LIB.get_metadata_tag(obj,'ProjectJ.Authoring')==TAG
        obj.set_editor_property('srgb',False);obj.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
        obj.set_editor_property('address_x',unreal.TextureAddress.TA_CLAMP);obj.set_editor_property('address_y',unreal.TextureAddress.TA_CLAMP)
    path='/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain_Biome';mat=unreal.load_asset(path)
    if mat is None:
        mat=TOOLS.create_asset('M_MainWorldTerrain_Biome','/Game/Map_Resource/MainWorld/Materials',unreal.Material,unreal.MaterialFactoryNew());assert mat
        LIB.set_metadata_tag(mat,'ProjectJ.Authoring',TAG)
    assert LIB.get_metadata_tag(mat,'ProjectJ.Authoring')==TAG
    for expression in list(EDIT.get_material_expressions(mat)):EDIT.delete_material_expression(mat,expression)
    original=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain');assert original
    expressions=list(EDIT.get_material_expressions(original));colors=[e for e in expressions if isinstance(e,unreal.MaterialExpressionCustom) and 'return base;' in e.get_editor_property('code')]
    assert len(colors)==1,'Existing authoring material contract changed'
    color=colors[0];normal=next(e for e in expressions if isinstance(e,unreal.MaterialExpressionCustom) and e!=color)
    # Reuse actual existing texture expressions without depending on opaque
    # FExpressionInput internals in Python.
    inputs={'P':EDIT.create_material_expression(mat,unreal.MaterialExpressionWorldPosition,-900,0),
            'N':EDIT.create_material_expression(mat,unreal.MaterialExpressionVertexNormalWS,-900,180)}
    names={'T_MainWorldBiomeMasks':'Mask','T_MainWorldSurfaceMasks':'Surface','T_Grass_Detail_D':'Grass',
           'T_Forest_Detail_D':'Forest','T_Rock_D':'Rock','T_Mud_D':'Mud',
           'T_Grass_Detail_N':'DetailN','T_Cobblestone_A':'Cobble','T_Cobblestone_N':'CobbleN'}
    for expr in expressions:
        if isinstance(expr,unreal.MaterialExpressionTextureObject):
            tex=expr.get_editor_property('texture');assert tex and tex.get_name() in names
            copy=EDIT.create_material_expression(mat,unreal.MaterialExpressionTextureObject,-650,0)
            copy.set_editor_property('texture',tex);inputs[names[tex.get_name()]]=copy
    assert len(inputs)==11
    for key,path in [('Ecology','/Game/Map_Resource/MainWorld/Materials/T_MainWorldEcology'),('Placement','/Game/Map_Resource/MainWorld/Materials/T_MainWorldPlacement'),
                     ('ForestN','/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Forest/T_Forest_Detail_N'),
                     ('MudN','/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Mud/T_Mud_N')]:inputs[key]=texture_node(mat,key,path)
    extra='''
float4 ecology=Texture2DSample(Ecology,EcologySampler,worldUV);
float4 placement=Texture2DSample(Placement,PlacementSampler,worldUV);
float mixWeight=.5+.5*sin(P.x/7300+sin(P.y/9100));
float2 rotated=float2(-uv.y,uv.x)*.413+float2(.19,.37);
float3 turf=lerp(Texture2DSample(Grass,GrassSampler,uv).rgb,Texture2DSample(Grass,GrassSampler,rotated).rgb,.32+.18*mixWeight)*float3(.84,.96,.76);
float3 litter=lerp(Texture2DSample(Forest,ForestSampler,uv*.83).rgb,Texture2DSample(Forest,ForestSampler,rotated*.71).rgb,.28);
float stoneWeight=max(saturate((.86-N.z)*3.8),ecology.g*.62);
float3 improved=lerp(turf,litter,saturate(ecology.r*1.12));
improved=lerp(improved,rock,stoneWeight);
improved=lerp(improved,Texture2DSample(Mud,MudSampler,uv*.85).rgb*float3(.85,.9,.93),ecology.b*.72);
improved*=.93+.07*mixWeight;
improved=lerp(improved,dirt*float3(1.65,1.38,1.04)+float3(.055,.038,.018),s.r*.97);
improved=lerp(improved,dirt*float3(.66,.61,.48),s.g*.95);
improved=lerp(improved,Texture2DSample(Cobble,CobbleSampler,P.xy/400).rgb,s.b);
return lerp(base,improved,placement.r);
'''
    code=color.get_editor_property('code').replace('return base;',extra)
    revised=custom(mat,code,inputs);assert EDIT.connect_material_property(revised,'',unreal.MaterialProperty.MP_BASE_COLOR)
    # BC5 normal blue is reconstructed. Orient tangent detail to the surface.
    ncode='''float2 worldUV=(P.xy/100+4032)/8064; float2 uv=P.xy/260;
float4 e=Texture2DSample(Ecology,EcologySampler,worldUV);
float4 s=Texture2DSample(Surface,SurfaceSampler,worldUV);
float cover=Texture2DSample(Placement,PlacementSampler,worldUV).r;
float2 xy=lerp(Texture2DSample(DetailN,DetailNSampler,uv).rg,Texture2DSample(ForestN,ForestNSampler,uv*.83).rg,e.r);
xy=lerp(xy,Texture2DSample(MudN,MudNSampler,uv*.85).rg,e.b);
xy=lerp(xy,Texture2DSample(CobbleN,CobbleNSampler,P.xy/400).rg,s.b)*2-1;
xy*=lerp(.42,.72,s.b);float3 detail=float3(xy,sqrt(saturate(1-dot(xy,xy))));
float3 T=normalize(cross(abs(N.z)>.9?float3(0,1,0):float3(0,0,1),N));float3 B=cross(N,T);
float3 improved=normalize(T*detail.x+B*detail.y+N*detail.z);
float3 old=lerp(Texture2DSample(DetailN,DetailNSampler,uv).xyz,Texture2DSample(CobbleN,CobbleNSampler,P.xy/400).xyz,s.b)*2-1;
return normalize(lerp(normalize(N+float3(old.xy*.5,0)),improved,cover));'''
    revised=custom(mat,ncode,inputs);assert EDIT.connect_material_property(revised,'',unreal.MaterialProperty.MP_NORMAL)
    mat.set_editor_property('tangent_space_normal',False)
    rough=custom(mat,'float2 uv=(P.xy/100+4032)/8064; float wet=Texture2DSample(Ecology,EcologySampler,uv).b; float cover=Texture2DSample(Placement,PlacementSampler,uv).r; return lerp(.91,lerp(.88,.63,wet),cover);',{'P':inputs['P'],'Ecology':inputs['Ecology'],'Placement':inputs['Placement']},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    assert EDIT.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    # Reuse tested provider varieties, tuning only the owned derivative.
    grasspath='/Game/Map_Resource/MainWorld/Materials/LG_MainWorldUnderstory';grass=unreal.load_asset(grasspath)
    if grass is None:
        grass=LIB.duplicate_asset(PRESET['groundcover_source'],grasspath);assert grass
        LIB.set_metadata_tag(grass,'ProjectJ.Authoring',TAG)
    assert LIB.get_metadata_tag(grass,'ProjectJ.Authoring')==TAG
    varieties=list(unreal.load_asset(PRESET['groundcover_source']).get_editor_property('grass_varieties'));selected=[]
    for v in varieties:
        mesh=v.get_editor_property('grass_mesh');name=mesh.get_name() if mesh else ''
        if name not in ['S_Grass','S_Clovers_02']:continue
        density=unreal.PerPlatformFloat();density.set_editor_property('default',PRESET['grass_density'] if name=='S_Grass' else PRESET['clover_density'])
        start=unreal.PerPlatformInt();start.set_editor_property('default',PRESET['grass_start_cull_cm'])
        end=unreal.PerPlatformInt();end.set_editor_property('default',PRESET['grass_end_cull_cm'])
        v.set_editor_property('grass_density',density)
        v.set_editor_property('start_cull_distance',start)
        v.set_editor_property('end_cull_distance',end)
        v.set_editor_property('cast_dynamic_shadow',False);v.set_editor_property('cast_contact_shadow',False)
        selected.append(v)
    assert selected,'Existing Grass Type has no expected groundcover variety'
    grass.set_editor_property('grass_varieties',selected)
    output=EDIT.create_material_expression(mat,unreal.MaterialExpressionLandscapeGrassOutput,600,600)
    grass_input=unreal.GrassInput();grass_input.set_editor_property('name','Understory');grass_input.set_editor_property('grass_type',grass)
    output.set_editor_property('grass_types',[grass_input])
    old=PRESET['existing_grass_bounds_m']
    gcode=f'''float2 xy=P.xy/100+4032; float4 m=Texture2DSample(Placement,PlacementSampler,xy/8064);
float existing=(xy.x>={old[0]} && xy.x<={old[2]} && xy.y>={old[1]} && xy.y<={old[3]})?0:1;
return m.r*m.g*existing;'''
    gnode=custom(mat,gcode,{'P':inputs['P'],'Placement':inputs['Placement']},unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    assert EDIT.connect_material_expressions(gnode,'',output,'Understory')
    errors=EDIT.recompile_material(mat);assert not errors,list(errors)
    LIB.save_loaded_asset(mat);LIB.save_loaded_asset(grass)
    REPORT.update(material=mat.get_path_name(),groundcover=grass.get_path_name(),groundcover_varieties=[v.grass_mesh.get_path_name() for v in selected])


def persistent_hism(owner):
    sds=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem);helper=unreal.SubobjectDataBlueprintFunctionLibrary
    handles=sds.k2_gather_subobject_data_for_instance(owner)
    parent=next(h for h in handles if helper.is_actor(helper.get_data(h)))
    root,why=sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=parent,new_class=unreal.SceneComponent));assert why.is_empty(),str(why)
    helper.get_associated_object(helper.get_data(root)).set_mobility(unreal.ComponentMobility.STATIC)
    handle,why=sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root,new_class=unreal.HierarchicalInstancedStaticMeshComponent));assert why.is_empty(),str(why)
    comp=helper.get_associated_object(helper.get_data(handle));comp.set_mobility(unreal.ComponentMobility.STATIC)
    parent_component=helper.get_associated_object(helper.get_data(root))
    assert comp.attach_to_component(parent_component,unreal.Name(''),unreal.AttachmentRule.KEEP_RELATIVE,unreal.AttachmentRule.KEEP_RELATIVE,unreal.AttachmentRule.KEEP_RELATIVE,False)
    return comp


def placement_stage():
    assert not any(TAG in [str(t) for t in a.tags] for a in ACTORS.get_all_level_actors()),'Do not duplicate authored shrubs'
    groups={};meshes={}
    for p in PLACEMENT['points']:
        index=p['mesh_index'];key=(index,int(p['x_m']//PRESET['chunk_m']),int(p['y_m']//PRESET['chunk_m']))
        groups.setdefault(key,[]).append(p)
    for (index,tx,ty),points in groups.items():
        if index not in meshes:
            mesh=unreal.load_asset(PRESET['bush_meshes'][index]);assert mesh
            for slot in mesh.static_materials:
                if slot.material_interface:
                    assert slot.material_interface.get_base_material().get_editor_property('used_with_instanced_static_meshes'),'Do not mutate provider master just to enable instancing'
            meshes[index]=mesh
        mesh=meshes[index];origin=unreal.Vector((tx*128+64-4032)*100,(ty*128+64-4032)*100,0)
        actor=ACTORS.spawn_actor_from_class(unreal.Actor,origin);assert actor
        actor.modify();actor.set_actor_label(f'Biome_Shrub_{index}_{tx}_{ty}');actor.set_folder_path('StarterRegion/Biomes/Understory')
        actor.set_editor_property('tags',[unreal.Name(TAG)]);comp=persistent_hism(actor);comp.set_static_mesh(mesh)
        # A bare Actor has no root at spawn. Set location AFTER persistent root
        # creation, before converting world instances to component space.
        actor.set_actor_location(origin,False,False)
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);comp.set_cast_shadow(True)
        comp.set_cull_distances(PRESET['shrub_start_cull_cm'],PRESET['shrub_end_cull_cm'])
        comp.add_instances(instance_transforms(mesh,points),False,True,False)
    REPORT.update(shrubs=len(PLACEMENT['points']),spatial_groups=len(groups))


def instance_transforms(mesh,points):
    transforms=[];bounds=mesh.get_bounds();o=bounds.origin;e=bounds.box_extent
    for p in points:
        s=p['scale'];a=math.radians(p['yaw']);ox=o.x*s;oy=o.y*s
        location=unreal.Vector((p['x_m']-4032)*100-math.cos(a)*ox+math.sin(a)*oy,(p['y_m']-4032)*100-math.sin(a)*ox-math.cos(a)*oy,p['z_m']*100-(o.z-e.z)*s-4)
        transforms.append(unreal.Transform(location=location,rotation=unreal.Rotator(pitch=0,yaw=p['yaw'],roll=0),scale=unreal.Vector(s,s,s)))
    return transforms


def repair_placement_stage():
    before=OUT/'biome-placement-before-fix.json'
    if not before.exists():before.write_text((OUT/'biome-saved-verification.json').read_text(encoding='utf-8'),encoding='utf-8')
    groups={};owned={a.get_actor_label():a for a in ACTORS.get_all_level_actors() if TAG in [str(t) for t in a.tags]}
    for p in PLACEMENT['points']:
        key=(p['mesh_index'],int(p['x_m']//128),int(p['y_m']//128));groups.setdefault(key,[]).append(p)
    assert len(groups)==len(owned)==166
    work=[]
    for (index,tx,ty),points in groups.items():
        actor=owned[f'Biome_Shrub_{index}_{tx}_{ty}'];comp=actor.get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
        assert comp and comp.get_instance_count()==len(points)
        assert comp.static_mesh.get_path_name().split('.')[0]==PRESET['bush_meshes'][index]
        work.append((actor,comp,tx,ty,points))
    for actor,comp,tx,ty,points in work:
        actor.modify();comp.modify();actor.set_actor_location(unreal.Vector((tx*128+64-4032)*100,(ty*128+64-4032)*100,0),False,False)
        comp.clear_instances();comp.add_instances(instance_transforms(comp.static_mesh,points),False,True,False)
    REPORT['persistent_root_transform_fixed']=True


def assign_stage():
    mat=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain_Biome');assert mat
    descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
    unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
    count=0
    for actor in ACTORS.get_all_level_actors():
        if isinstance(actor,unreal.LandscapeProxy):
            current=actor.get_editor_property('landscape_material')
            assert current and current.get_path_name() in ['/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain.M_MainWorldTerrain',mat.get_path_name()]
            if current!=mat:
                actor.modify();actor.set_editor_property('landscape_material',mat)
            count+=1
    assert count>=256,count
    REPORT['landscape_proxies_assigned']=count
    assert hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest()==PLACEMENT['height_sha256']
    REPORT['height_sha256']=PLACEMENT['height_sha256']


stages=[(n,f) for n,f in [('material',material_stage),('understory',placement_stage),('assignment',assign_stage)] if n not in REPORT['completed']]
last=time.monotonic();busy=False


def tick(dt):
    global last,busy
    if busy or time.monotonic()-last<3:return
    if not stages:
        record('COMPLETE');unreal.unregister_slate_post_tick_callback(handle);return
    name,fn=stages.pop(0);busy=True;record(name+'_RUNNING')
    try:
        fn();owned_save();REPORT['completed'].append(name);record(name+'_DONE')
    except Exception:
        REPORT['error']=traceback.format_exc();record(name+'_FAILED');unreal.log_error(REPORT['error'])
        unreal.unregister_slate_post_tick_callback(handle)
    finally:busy=False;last=time.monotonic()


if globals().get('repair_owned_placement',False):
    repair_placement_stage();owned_save();record('COMPLETE')
else:
    record('QUEUED');handle=unreal.register_slate_post_tick_callback(tick)
