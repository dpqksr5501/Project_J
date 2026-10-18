"""Create the complete starter-region authoring skeleton once, in Project J.

Run explicitly through the editor's local Python entry point. Existing saved maps
are never regenerated on startup. The v3 plan remains the source for stable IDs;
the resulting actors, landscape edit layers and splines are designer-editable.
"""
import array
import hashlib
import json
import math
import traceback
from pathlib import Path
import unreal

ROOT=Path(__file__).resolve().parents[2]
BASE='/Game/World/Starter'
MAP=BASE+'/Maps/L_StarterRegion'
OUT=ROOT/'Saved/Validation/StarterWorld'
SOURCE=ROOT/'Content/World/Starter/Source'
OUT.mkdir(parents=True,exist_ok=True)
PLAN=json.loads((SOURCE/'StarterWorld.plan.json').read_text(encoding='utf-8'))
N=PLAN['resolution'];EXTENT=PLAN['extent_m']
RAW=array.array('H')
with (SOURCE/'HM_StarterRegion.r16').open('rb') as stream:RAW.fromfile(stream,N*N)


def height(x,y):
    u=max(0,min(N-1,x/EXTENT*(N-1)));v=max(0,min(N-1,y/EXTENT*(N-1)))
    i,j=int(u),int(v);ii,jj=min(N-1,i+1),min(N-1,j+1);a,b=u-i,v-j
    value=(RAW[j*N+i]*(1-a)+RAW[j*N+ii]*a)*(1-b)+(RAW[jj*N+i]*(1-a)+RAW[jj*N+ii]*a)*b
    return (value-32768)/128*PLAN['z_scale']/100


def point(x,y,z=None):
    return unreal.Vector((x-EXTENT/2)*100,(y-EXTENT/2)*100,(height(x,y) if z is None else z)*100)


def build():
    assert Path(unreal.Paths.project_dir()).resolve()==ROOT
    library=unreal.EditorAssetLibrary;tools=unreal.AssetToolsHelpers.get_asset_tools()
    actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    assert not library.does_asset_exist(MAP),'Preserve the existing authored world; use explicit bounded edits.'
    for folder in ['Maps','Data','Materials','HLOD','Review']:library.make_directory(BASE+'/'+folder)
    assert levels.new_level_from_template(MAP,'/Engine/Maps/Templates/OpenWorld')
    for actor in actors.get_all_level_actors():
        if isinstance(actor,(unreal.LandscapeProxy,unreal.PlayerStart)):actors.destroy_actor(actor)
    edit=unreal.MaterialEditingLibrary
    def material(name,color,roughness=.9):
        path=BASE+'/Materials/'+name
        obj=unreal.load_asset(path) or tools.create_asset(name,BASE+'/Materials',unreal.Material,unreal.MaterialFactoryNew())
        assert edit.get_num_material_expressions(obj)==0,'Do not overwrite edited materials '+path
        c=edit.create_material_expression(obj,unreal.MaterialExpressionConstant3Vector,-250,0);c.set_editor_property('constant',unreal.LinearColor(*color,1));edit.connect_material_property(c,'',unreal.MaterialProperty.MP_BASE_COLOR)
        r=edit.create_material_expression(obj,unreal.MaterialExpressionConstant,-250,160);r.set_editor_property('r',roughness);edit.connect_material_property(r,'',unreal.MaterialProperty.MP_ROUGHNESS)
        return obj
    terrain=material('M_StarterTerrain',(.18,.23,.11))
    # Reuse imported PBR source textures, keeping provider master materials intact.
    def texture(path):
        value=unreal.load_asset(path);assert isinstance(value,unreal.Texture),path
        return value
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(SOURCE/'T_StarterBiomeMasks.png'),destination_path=BASE+'/Materials',destination_name='T_StarterBiomeMasks',automated=True,save=True).items():task.set_editor_property(key,value)
    tools.import_asset_tasks([task]);mask=unreal.load_asset(BASE+'/Materials/T_StarterBiomeMasks');assert mask
    mask.set_editor_property('srgb',False);mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS);library.save_loaded_asset(mask)
    def custom(mat,code,inputs,kind=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
        node=edit.create_material_expression(mat,unreal.MaterialExpressionCustom,-100,0);node.set_editor_property('code',code);node.set_editor_property('output_type',kind)
        descriptors=[]
        for name,value in inputs.items():
            descriptor=unreal.CustomInput();descriptor.set_editor_property('input_name',name);descriptors.append(descriptor)
        node.set_editor_property('inputs',descriptors)
        for name,value in inputs.items():assert edit.connect_material_expressions(value,'',node,name)
        return node
    def sample(mat,value):
        node=edit.create_material_expression(mat,unreal.MaterialExpressionTextureObject,-600,0);node.set_editor_property('texture',value);return node
    pos=edit.create_material_expression(terrain,unreal.MaterialExpressionWorldPosition,-800,0)
    normal=edit.create_material_expression(terrain,unreal.MaterialExpressionVertexNormalWS,-800,180)
    texroot='/Game/Map_Resource/Brushify/Materials/Landscape/Textures/'
    # The exact existing source paths are discovered rather than fabricating assets.
    registry=unreal.AssetRegistryHelpers.get_asset_registry()
    available=[str(a.package_name) for a in registry.get_assets_by_path('/Game/Map_Resource/Brushify',True) if str(a.asset_class_path.asset_name)=='Texture2D']
    def named(name):
        matches=[p for p in available if p.rsplit('/',1)[-1]==name];assert len(matches)==1,(name,matches)
        return texture(matches[0])
    inputs={'P':pos,'N':normal,'Mask':sample(terrain,mask)}
    for key,name in [('Grass','T_Grass_Detail_D'),('Rock','T_Rock_D'),('Forest','T_Forest_Detail_D')]:inputs[key]=sample(terrain,named(name))
    color=custom(terrain,'float2 uv=P.xy/320; float4 m=Texture2DSample(Mask,MaskSampler,(P.xy/100+4032)/8064); float3 grass=Texture2DSample(Grass,GrassSampler,uv).rgb; float3 forest=Texture2DSample(Forest,ForestSampler,uv*.7).rgb; float3 rock=Texture2DSample(Rock,RockSampler,uv*.55).rgb; float cliff=saturate((.80-N.z)*3); float3 base=lerp(grass,forest,m.r*.8); base=lerp(base,rock,max(cliff,m.a*.65)); return base*(.83+.17*sin(P.x/19100+sin(P.y/13500)));',inputs)
    edit.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    terrain.set_editor_property('tangent_space_normal',False)
    for mat in [terrain]:edit.recompile_material(mat);library.save_loaded_asset(mat)
    landscape=unreal.Project_JWorldAuthoringLibrary.import_region_landscape(str(SOURCE/'HM_StarterRegion.r16'),N,PLAN['xy_scale_cm'],PLAN['z_scale'],terrain)
    assert landscape,'Landscape import failed'
    landscape.set_actor_label('StarterRegion_Landscape_8064m_2m')
    assert unreal.Project_JWorldAuthoringLibrary.enable_region_streaming()
    def spawn(cls,label,location,folder,editor_only=False):
        actor=actors.spawn_actor_from_class(cls,location);assert actor,label
        actor.set_actor_label(label);actor.set_folder_path('StarterRegion/'+folder)
        actor.set_editor_property('tags',[unreal.Name('ProjectJ.StarterRegion.v1')])
        if editor_only:actor.set_editor_property('is_editor_only_actor',True)
        return actor
    factory=unreal.DataAssetFactory();factory.set_editor_property('data_asset_class',unreal.Project_JWorldDefinition)
    definition=tools.create_asset('DA_StarterRegion',BASE+'/Data',unreal.Project_JWorldDefinition,factory);assert definition
    for key,value in dict(region_id=PLAN['region_id'],generation_seed=PLAN['seed'],authoring_version=1,source_plan_hash=hashlib.sha256((SOURCE/'StarterWorld.plan.json').read_bytes()).hexdigest()).items():definition.set_editor_property(key,value)
    definition.set_editor_property('authoring_bounds',unreal.Box(min=point(0,0,PLAN['min_height_m']),max=point(EXTENT,EXTENT,PLAN['max_height_m'])))
    places=[]
    for entry in PLAN['places']:
        x,y,z=entry['center_m'];place=unreal.Project_JWorldPlace()
        for key,value in dict(place_id=entry['id'],display_name=entry['name'],location=point(x,y,z),radius=entry['radius_m']*100,environment_id=entry['kind'],place_type=entry['kind'],recommended_min_level=entry['levels'][0],recommended_max_level=entry['levels'][1],boundary=[point(*p) for p in entry['boundary_m']]).items():place.set_editor_property(key,value)
        places.append(place)
        marker=spawn(unreal.TargetPoint,entry['id']+'__'+entry['name'],point(x,y,z+2),'Review/ContentCandidates',True)
        marker.set_editor_property('tags',[unreal.Name('ProjectJ.StarterRegion.v1'),unreal.Name(entry['id']),unreal.Name('Candidate.NavUnverified')])
    definition.set_editor_property('places',places)
    connections=[]
    for entry in PLAN['connections']:
        item=unreal.Project_JWorldConnection()
        for key,value in dict(connection_id=entry['id'],boundary_side=entry['side'],connection_type=entry['kind'],location=point(*entry['position_m']),outward_direction=unreal.Vector(*entry['outward']),width_cm=entry['width_m']*100).items():item.set_editor_property(key,value)
        connections.append(item)
    definition.set_editor_property('connections',connections);library.save_loaded_asset(definition)
    state=spawn(unreal.Project_JWorldState,'StarterRegion_WorldState',unreal.Vector(),'Systems');state.set_editor_property('definition',definition)
    # Preserve the native server state contract. Full weather art is a later,
    # measured pass; do not silently substitute a second state writer.
    environment=spawn(unreal.Project_JWorldEnvironment,'StarterRegion_Environment',unreal.Vector(),'Systems');environment.set_editor_property('state',state)
    environment.set_editor_property('clear_sun_intensity',60000)
    for actor in actors.get_all_level_actors():
        if isinstance(actor,unreal.DirectionalLight):
            environment.set_editor_property('sun',actor)
            actor.modify()
            light=actor.get_component_by_class(unreal.DirectionalLightComponent)
            light.modify()
            light.set_editor_property('intensity',60000)
        if isinstance(actor,unreal.ExponentialHeightFog):environment.set_editor_property('fog',actor)
    road=material('M_StarterRouteReview',(.25,.19,.11));water=material('M_StarterWaterReview',(.025,.13,.18),.18)
    for mat in [road,water]:edit.recompile_material(mat);library.save_loaded_asset(mat)
    route_count=0
    for entry in PLAN['roads']:
        points=[]
        for a,b in zip(entry['points'],entry['points'][1:]):
            steps=max(2,math.ceil(math.dist(a[:2],b[:2])/35))
            for index in range(steps):
                t=index/steps;x=a[0]+(b[0]-a[0])*t;y=a[1]+(b[1]-a[1])*t;points.append(point(x,y,height(x,y)+.15))
        points.append(point(*entry['points'][-1]))
        for index in range(0,len(points)-1,8):
            chunk=points[index:index+9]
            if len(chunk)<2:continue
            actor=unreal.Project_JWorldAuthoringLibrary.create_spline_ribbon(entry['id']+'_'+str(index//8),chunk,road,entry['width_m']*100);assert actor
            actor.set_folder_path('StarterRegion/Review/RouteCandidates');actor.set_editor_property('tags',[unreal.Name('ProjectJ.StarterRegion.v1'),unreal.Name('Candidate.SlopeUnverified')]);route_count+=1
    for entry in PLAN['rivers']:
        points=[point(*p) for p in entry['points']]
        for index in range(0,len(points)-1,8):
            chunk=points[index:index+9]
            if len(chunk)<2:continue
            actor=unreal.Project_JWorldAuthoringLibrary.create_spline_ribbon(entry['id']+'_'+str(index//8),chunk,water,entry['width_m']*100);assert actor
            actor.set_folder_path('StarterRegion/Hydrology/PrototypeSurface')
    def surface(path,label,center,scale):
        actor=spawn(unreal.StaticMeshActor,label,center,'Hydrology/PrototypeSurface');c=actor.static_mesh_component;c.set_static_mesh(unreal.load_asset(path));c.set_material(0,water);c.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION);actor.set_actor_scale3d(unreal.Vector(*scale));return actor
    lake=PLAN['lake'];surface('/Engine/BasicShapes/Cylinder','StarterRegion_LakePrototype',point(*lake['center_m'],lake['water_level_m']-.025),(lake['radius_m'][0]*2,lake['radius_m'][1]*2,.05))
    surface('/Engine/BasicShapes/Plane','StarterRegion_SeaPrototype',point(EXTENT/2,-1000,0),(EXTENT*1.5,4200,1))
    start=next(p for p in PLAN['places'] if p['id']=='S00')['center_m']
    player_start=spawn(unreal.PlayerStart,'StarterRegion_PlayerStart',point(start[0],start[1],start[2]+1.3),'Systems')
    player_start.set_editor_property('is_spatially_loaded',False)
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(point(3900,1700,1700),unreal.Rotator(pitch=-26,yaw=90,roll=0))
    assert levels.save_current_level()
    assert unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True,True)
    report=dict(map=MAP,extent_m=EXTENT,resolution=N,places=len(places),hunting_grounds=sum(p['kind']=='hunting_ground' for p in PLAN['places']),boss_candidates=sum(p['kind']=='field_boss' for p in PLAN['places']),connections=len(connections),route_review_chunks=route_count,phase='FULL_REGION_AUTHORING_SKELETON',visual_quality='NOT_APPROVED',navigation='UNVERIFIED',packaging='UNVERIFIED',performance='UNMEASURED',old_map_deleted=False)
    (OUT/'world-build.json').write_text(json.dumps(report,indent=2),encoding='utf-8');unreal.log('STARTER_WORLD_BUILD_COMPLETE '+json.dumps(report))


try:build()
except Exception:
    (OUT/'build-error.txt').write_text(traceback.format_exc(),encoding='utf-8');unreal.log_error(traceback.format_exc())
