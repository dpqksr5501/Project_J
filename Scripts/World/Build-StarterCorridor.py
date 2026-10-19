"""Explicit editor authoring of the first inhabited corridor; never runs at startup.

Invoke with the editor Python console through MCP. Stages yield to Slate between
operations. Check corridor-build.json before retrying: a timed out MCP request
does not cancel native editor work. Only this stage's tagged actors are replaced.
"""
import array,collections,datetime,hashlib,importlib.util,json,math,random,time,traceback
from pathlib import Path
import unreal

ROOT=Path(__file__).resolve().parents[2]
SOURCE=ROOT/'Content/World/MainWorld/Source'
OUT=ROOT/'Saved/Validation/StarterWorld'
BASE='/Game/World/MainWorld';MAP='/Game/World/MainWorld/Legacy/Maps/L_StarterRegion'
TAG='ProjectJ.Starter.Corridor.v1'
PLAN=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'))
LAYOUT=json.loads((SOURCE/'MainWorld.corridor.json').read_text(encoding='utf-8'))
RAW=array.array('H')
with (SOURCE/'HM_MainWorld.r16').open('rb') as stream:RAW.fromfile(stream,4033**2)
ACTORS=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
LEVELS=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
LIB=unreal.EditorAssetLibrary;EDIT=unreal.MaterialEditingLibrary
TOOLS=unreal.AssetToolsHelpers.get_asset_tools()
MESHES={};TOUCHED_MATERIALS=set();NEW_ACTORS=[]
TRANSFER=json.loads((OUT/'Megascans-transfer.json').read_text(encoding='utf-8'))
REPORT_FILE=OUT/'corridor-build.json'
REPORT=json.loads(REPORT_FILE.read_text(encoding='utf-8')) if REPORT_FILE.exists() else {'completed':[],'counts':{}}
assert Path(unreal.Paths.project_dir()).resolve()==ROOT
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith(MAP+'.')
assert PLAN['corridor_revision']==LAYOUT['revision']==1
assert (ROOT/'Saved/StarterWorld/BeforeCorridor/Content').exists()
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)


def report(phase,**values):
    REPORT.update(phase=phase,updated_utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),**values)
    REPORT_FILE.write_text(json.dumps(REPORT,indent=2),encoding='utf-8')
    unreal.log('STARTER_CORRIDOR '+phase)


def height(x,y):
    u=max(0,min(4031.999,x/2));v=max(0,min(4031.999,y/2));i,j=int(u),int(v);s,t=u-i,v-j
    q=(RAW[j*4033+i]*(1-s)+RAW[j*4033+i+1]*s)*(1-t)+(RAW[(j+1)*4033+i]*(1-s)+RAW[(j+1)*4033+i+1]*s)*t
    return (q-32768)*.0625


def point(x,y,z=None):return unreal.Vector((x-4032)*100,(y-4032)*100,(height(x,y) if z is None else z)*100)


def mesh(path):
    if path not in MESHES:
        if path.startswith(('/Game/Map_Resource/Megascans/','/Game/Map_Resource/Meshes/','/Game/Map_Resource/ThirdParty/')):
            seen=set();pending=[path];missing=set(TRANSFER['missing_source_packages'])
            while pending:
                p=pending.pop()
                if p in seen:continue
                assert p not in missing,'Missing provider dependency: '+p
                seen.add(p);pending.extend(TRANSFER['dependency_edges'].get(p,[]))
        result=unreal.load_asset(path);assert isinstance(result,unreal.StaticMesh),path
        MESHES[path]=result
    return MESHES[path]


def spawn(label,where,category,cls=unreal.Actor):
    actor=ACTORS.spawn_actor_from_class(cls,where);assert actor,label
    actor.modify();actor.set_actor_label('S00_'+label)
    actor.set_folder_path('StarterRegion/Settlements/S00/'+category)
    actor.set_editor_property('tags',[unreal.Name(TAG),unreal.Name('Corridor.'+category)])
    NEW_ACTORS.append(actor);return actor


def clear_category(category):
    for actor in ACTORS.get_all_level_actors():
        tags={str(t) for t in actor.get_editor_property('tags')}
        if TAG in tags and 'Corridor.'+category in tags:assert ACTORS.destroy_actor(actor)


def component(owner,cls,name):
    sds=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lib=unreal.SubobjectDataBlueprintFunctionLibrary
    root=None;actor_handle=None
    for handle in sds.k2_gather_subobject_data_for_instance(owner):
        data=lib.get_data(handle)
        if lib.is_actor(data):actor_handle=handle
        elif lib.is_root_component(data):root=handle
    assert actor_handle
    if root is None:
        root,why=sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=actor_handle,new_class=unreal.SceneComponent,blueprint_context=None));assert why.is_empty(),str(why)
    parent=lib.get_associated_object(lib.get_data(root));parent.modify();parent.set_mobility(unreal.ComponentMobility.STATIC)
    handle,why=sds.add_new_subobject(unreal.AddNewSubobjectParams(parent_handle=root,new_class=cls,blueprint_context=None));assert why.is_empty(),str(why)
    sds.rename_subobject(handle=handle,new_name=unreal.Text(name))
    owner.modify();result=lib.get_associated_object(lib.get_data(handle));result.modify()
    assert result.attach_to_component(parent,unreal.Name(''),unreal.AttachmentRule.KEEP_RELATIVE,unreal.AttachmentRule.KEEP_RELATIVE,unreal.AttachmentRule.KEEP_RELATIVE,False)
    result.set_mobility(unreal.ComponentMobility.STATIC)
    return result


def centered_transform(obj,center,yaw=0,scale=(1,1,1),bottom=True):
    bounds=obj.get_bounds();o=bounds.origin;e=bounds.box_extent
    ox=o.x*scale[0];oy=o.y*scale[1];oz=(o.z-e.z if bottom else o.z)*scale[2]
    a=math.radians(yaw);c,s=math.cos(a),math.sin(a)
    return unreal.Transform(location=center-unreal.Vector(c*ox-s*oy,s*ox+c*oy,oz),rotation=unreal.Rotator(pitch=0,yaw=yaw,roll=0),scale=unreal.Vector(*scale))


def static(label,path,x,y,z=None,yaw=0,scale=(1,1,1),category='Details',collision=True):
    obj=mesh(path);actor=spawn(label,point(x,y,z),category,unreal.StaticMeshActor)
    c=actor.static_mesh_component;c.modify();c.set_mobility(unreal.ComponentMobility.STATIC);c.set_static_mesh(obj)
    c.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS if collision else unreal.CollisionEnabled.NO_COLLISION)
    actor.set_actor_transform(centered_transform(obj,point(x,y,z),yaw,scale),False,False)
    return actor


def save_owned():
    packages=list(unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages())+list(unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages())
    owned=[];preserved=[]
    for package in packages:
        name=package.get_name()
        if name.startswith((BASE+'/', '/Game/Map_Resource/MainWorld/', '/Game/__ExternalActors__/World/MainWorld/Legacy/','/Game/__ExternalObjects__/World/MainWorld/Legacy/')) or name in TOUCHED_MATERIALS:owned.append(package)
        else:preserved.append(name)
    if owned:assert unreal.EditorLoadingAndSavingUtils.save_packages(owned,True)
    REPORT['unrelated_dirty_packages_preserved']=preserved


def terrain_stage():
    descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
    unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
    current=ACTORS.get_all_level_actors();landscape=next(a for a in current if isinstance(a,unreal.Landscape))
    terrain_hash=hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest()
    if REPORT.get('terrain_sha256')!=terrain_hash:
        assert unreal.Project_JWorldAuthoringLibrary.reimport_region_heightmap(landscape,str(SOURCE/'HM_MainWorld.r16'),4033)
    definition=unreal.load_asset(BASE+'/Data/DA_MainWorld');definition.modify()
    places=list(definition.get_editor_property('places'));entries={p['id']:p for p in PLAN['places']}
    for place in places:
        entry=entries[str(place.get_editor_property('place_id'))]
        place.set_editor_property('location',point(*entry['center_m']))
        place.set_editor_property('boundary',[point(*p[:2]) for p in entry['boundary_m']])
    definition.set_editor_property('places',places)
    definition.set_editor_property('source_plan_hash',hashlib.sha256((SOURCE/'MainWorld.plan.json').read_bytes()).hexdigest())
    definition.set_editor_property('authoring_bounds',unreal.Box(min=point(0,0,PLAN['min_height_m']),max=point(8064,8064,PLAN['max_height_m'])))
    for actor in current:
        label=actor.get_actor_label()
        if isinstance(actor,unreal.TargetPoint) and label.split('__')[0] in entries:
            entry=entries[label.split('__')[0]];x,y,z=entry['center_m'];actor.modify();actor.set_actor_location(point(x,y,z+2),False,False)
        if label=='MainWorld_PlayerStart':
            x,y,z=entries['S00']['center_m'];actor.modify();actor.set_actor_location(point(x,y,z+1.3),False,False)
            actor.set_actor_rotation(unreal.Rotator(pitch=0,yaw=LAYOUT.get('player_start_yaw',125),roll=0),False)
        # Keep editable candidate splines, retire their obsolete floating ribbons.
        if str(actor.get_folder_path())=='StarterRegion/Review/RouteCandidates':
            actor.modify();actor.set_editor_property('is_editor_only_actor',True)
            for c in actor.get_components_by_class(unreal.SplineMeshComponent):c.modify();c.set_editor_property('visible',False);c.set_editor_property('hidden_in_game',True)
    REPORT['terrain_sha256']=terrain_hash


def surface_stage():
    task=unreal.AssetImportTask()
    for key,value in dict(filename=str(SOURCE/'T_MainWorldSurfaceMasks.png'),destination_path='/Game/Map_Resource/MainWorld/Materials',destination_name='T_MainWorldSurfaceMasks',automated=True,replace_existing=True,save=True).items():task.set_editor_property(key,value)
    TOOLS.import_asset_tasks([task]);mask=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/T_MainWorldSurfaceMasks');assert mask
    mask.modify();mask.set_editor_property('srgb',False);mask.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS);LIB.save_loaded_asset(mask)
    mat=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain');mat.modify()
    # The engine's DeleteAll loop removes entries while iterating its live array;
    # use a snapshot so no disconnected expressions survive repeated revisions.
    for expr in list(EDIT.get_material_expressions(mat)):EDIT.delete_material_expression(mat,expr)
    assert EDIT.get_num_material_expressions(mat)==0
    def node(cls,x=-600,y=0):return EDIT.create_material_expression(mat,cls,x,y)
    pos=node(unreal.MaterialExpressionWorldPosition,-900);normal=node(unreal.MaterialExpressionVertexNormalWS,-900,180)
    inputs={'P':pos,'N':normal}
    paths={'Mask':'/Game/Map_Resource/MainWorld/Materials/T_MainWorldBiomeMasks','Surface':'/Game/Map_Resource/MainWorld/Materials/T_MainWorldSurfaceMasks',
           'Grass':'/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Grass/T_Grass_Detail_D',
           'Forest':'/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Forest/T_Forest_Detail_D',
           'Rock':'/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Rock/T_Rock_D',
           'Mud':'/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Mud/T_Mud_D',
           'DetailN':'/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Grass/T_Grass_Detail_N',
           'Cobble':'/Game/Map_Resource/Megascans/Surfaces/Cobblestone/T_Cobblestone_A',
           'CobbleN':'/Game/Map_Resource/Megascans/Surfaces/Cobblestone/T_Cobblestone_N'}
    for i,(key,path) in enumerate(paths.items()):
        value=unreal.load_asset(path);assert value,path
        obj=node(unreal.MaterialExpressionTextureObject,-650,i*100);obj.set_editor_property('texture',value);inputs[key]=obj
    def custom(code,kind):
        expr=node(unreal.MaterialExpressionCustom,-200);expr.set_editor_property('code',code);expr.set_editor_property('output_type',kind)
        descriptors=[]
        for name in inputs:
            d=unreal.CustomInput();d.set_editor_property('input_name',name);descriptors.append(d)
        expr.set_editor_property('inputs',descriptors)
        for name,value in inputs.items():assert EDIT.connect_material_expressions(value,'',expr,name)
        return expr
    color=custom('''float2 worldUV=(P.xy/100+4032)/8064;
float4 m=Texture2DSample(Mask,MaskSampler,worldUV);
float4 s=Texture2DSample(Surface,SurfaceSampler,worldUV);
float2 uv=P.xy/260;
float3 grass=Texture2DSample(Grass,GrassSampler,uv).rgb;
float3 broad=Texture2DSample(Grass,GrassSampler,uv*.173+0.27).rgb;
grass=lerp(grass,broad,.24)*float3(.78,1.02,.69);
float3 forest=Texture2DSample(Forest,ForestSampler,uv*.7).rgb;
float3 dirt=Texture2DSample(Mud,MudSampler,uv*.85).rgb;
float3 weights=pow(abs(N),4);weights/=max(dot(weights,1),.0001);
float3 rock=Texture2DSample(Rock,RockSampler,P.yz/700).rgb*weights.x+Texture2DSample(Rock,RockSampler,P.xz/700).rgb*weights.y+Texture2DSample(Rock,RockSampler,P.xy/700).rgb*weights.z;
float cliff=saturate((.82-N.z)*4);
float3 base=lerp(grass,forest,m.r*.5);
base=lerp(base,rock,max(cliff,m.a*.45));
float variation=.9+.1*sin(P.x/17300+sin(P.y/11900));
base*=variation;
base=lerp(base,dirt*float3(1.65,1.38,1.04)+float3(.055,.038,.018),s.r*.97);
base=lerp(base,dirt*float3(.66,.61,.48),s.g*.95);
base=lerp(base,Texture2DSample(Cobble,CobbleSampler,P.xy/400).rgb,s.b);
return base;''',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    assert EDIT.connect_material_property(color,'',unreal.MaterialProperty.MP_BASE_COLOR)
    detail=custom('float mask=Texture2DSample(Surface,SurfaceSampler,(P.xy/100+4032)/8064).b; float3 d=lerp(Texture2DSample(DetailN,DetailNSampler,P.xy/260).xyz,Texture2DSample(CobbleN,CobbleNSampler,P.xy/400).xyz,mask)*2-1; return normalize(N+float3(d.xy*.5,0));',unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    assert EDIT.connect_material_property(detail,'',unreal.MaterialProperty.MP_NORMAL)
    rough=node(unreal.MaterialExpressionConstant);rough.set_editor_property('r',.91);EDIT.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    mat.set_editor_property('tangent_space_normal',False)
    errors=EDIT.recompile_material(mat);assert not errors,list(errors)
    LIB.save_loaded_asset(mat)


ELDER='/Game/Map_Resource/ElderboomVillage/Meshes/'
SKY='/Game/Map_Resource/SkyTown/Meshes/'
WALL=ELDER+'SM_Medieval_Modular_Wall_uewibaydw_lod3_Var1'
WINDOW=ELDER+'SM_Medieval_Modular_Wall_uewibagdw_lod3_Var1'
DOOR=ELDER+'SM_Modular_Building_Door_xd4ffcpdw_lod3_Var1'
ROOF='/Game/Map_Resource/Meshes/Houses/Roofs/House08_Roof/SM_House08_Roof'


def apply_modular_roof(owner,house):
    """Keep existing shell/pivot; authored intact roof has its ridge along +Y."""
    w,d=house['width']*100,house['depth']*100
    existing={c.get_name():c for c in owner.get_components_by_class(unreal.StaticMeshComponent)}
    c=existing.get('HipRoof') or component(owner,unreal.StaticMeshComponent,'HipRoof')
    if house.get('roof_style')!='intact_thatch_gable':
        obj=mesh(ROOF);size=obj.get_bounds().box_extent*2
        c.modify();c.set_static_mesh(obj)
        c.set_relative_transform(centered_transform(obj,unreal.Vector(0,0,285),0,((w+90)/size.x,(d+90)/size.y,1 if house['id']!='Barn' else 1.15)),False,False)
        return
    obj=mesh('/Game/Map_Resource/Meshes/Houses/Roofs/House02_Roof/SM_House02_Roof');size=obj.get_bounds().box_extent*2
    ridge_x=w>d
    cross,long=(d,w) if ridge_x else (w,d)
    scale=((cross+90)/size.x,(long+90)/size.y,(cross+90)/size.x)
    eave=house.get('storeys',1)*300-15
    contact=house.get('roof_contact_profile')
    drop=(contact['wall_contact_source_z_cm']-(obj.get_bounds().origin.z-obj.get_bounds().box_extent.z))*scale[2] if contact else 0
    c.modify();c.set_static_mesh(obj);c.set_material(0,obj.static_materials[0].material_interface)
    c.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    c.set_relative_transform(centered_transform(obj,unreal.Vector(0,0,eave-drop),90 if ridge_x else 0,scale),False,False)
    gable=mesh(ELDER+'SM_Medieval_Modular_Gable_udsreiudw_lod3_Var1');gs=gable.get_bounds().box_extent*2
    for sign in [-1,1]:
        name='GableNorth' if sign==1 else 'GableSouth'
        g=existing.get(name) or component(owner,unreal.StaticMeshComponent,name)
        g.modify();g.set_static_mesh(gable);g.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        where=unreal.Vector(sign*w/2,0,eave+2) if ridge_x else unreal.Vector(0,sign*d/2,eave+2)
        angle=(-90 if sign==1 else 90) if ridge_x else (0 if sign==1 else 180)
        gable_height=(size.z*scale[2]-drop)*(contact['gable_tip_ratio'] if contact else .90)
        g.set_relative_transform(centered_transform(gable,where,angle,(cross/gs.x,1,gable_height/gs.z)),False,False)


def assemble_modular_house(owner,house):
    """Shared scanned shell for village homes, stores and two-storey inns."""
    w,d=house['width']*100,house['depth']*100
    def piece(path,name,cx,cy,bottom,width,angle):
        obj=mesh(path);size=obj.get_bounds().box_extent*2
        c=component(owner,unreal.StaticMeshComponent,name);c.set_static_mesh(obj)
        c.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
        c.set_relative_transform(centered_transform(obj,unreal.Vector(cx,cy,bottom),angle,(width/size.x,1,300/size.z)),False,False)
    for floor in range(house.get('storeys',1)):
        bottom=floor*300;prefix='' if floor==0 else 'Upper'+str(floor)
        if floor==0:
            piece(DOOR,'FrontDoor',0,-d/2,bottom,300,180)
            side=(w-300)/2;n=max(1,round(side/200));unit=side/n
            for sign in [-1,1]:
                for i in range(n):piece(WINDOW if i%2==0 else WALL,'FrontWall'+str(sign)+'_'+str(i),sign*(150+unit*(i+.5)),-d/2,bottom,unit,180)
        else:
            n=max(1,round(w/200));unit=w/n
            for i in range(n):piece(WINDOW if i%2==0 else WALL,prefix+'Front'+str(i),-w/2+unit*(i+.5),-d/2,bottom,unit,180)
        n=max(1,round(w/200));unit=w/n
        for i in range(n):piece(WINDOW if i%3==1 else WALL,prefix+'Rear'+str(i),-w/2+unit*(i+.5),d/2,bottom,unit,0)
        n=max(1,round(d/200));unit=d/n
        for sign in [-1,1]:
            for i in range(n):piece(WINDOW if i%3==1 else WALL,prefix+'Side'+str(sign)+'_'+str(i),sign*w/2,-d/2+unit*(i+.5),bottom,unit,-90 if sign==1 else 90)
    apply_modular_roof(owner,house)


def buildings_stage():
    clear_category('Buildings')
    for house in LAYOUT['houses']:
        label=house['id'];x,y,z=house['x'],house['y'],house['ground_m'];yaw=house['yaw']
        if label in ['Inn','Granary'] and house.get('architecture_style')!='scanned_modular':
            static(label,SKY+('SM_House01' if label=='Inn' else 'SM_House02'),x,y,z-.06,yaw,category='Buildings');continue
        owner=spawn(label,point(x,y,z-.05),'Buildings');owner.set_actor_rotation(unreal.Rotator(pitch=0,yaw=yaw,roll=0),False)
        assemble_modular_house(owner,house)
    REPORT['counts']['buildings']=len(LAYOUT['houses'])


def details_stage():
    clear_category('Details')
    well=static('PlazaWell','/Game/Map_Resource/Meshes/Well/SM_MainWell',3106,3096,yaw=28)
    # These source pieces share the main frame's origin; do not bottom-align
    # each piece (the shingles' authored bottom is 171 cm above the origin).
    for name,path in [('StoneBase','/Game/Map_Resource/Meshes/Well/WellBase/SM_WellBase'),('Shingles','/Game/Map_Resource/Meshes/Well/SM_WellShingles'),('CrankDetails','/Game/Map_Resource/Meshes/Well/SM_WellDetailPieces')]:
        c=component(well,unreal.StaticMeshComponent,name);c.set_static_mesh(mesh(path));c.set_relative_transform(unreal.Transform(),False,False)
    static('InnCart',SKY+'SM_Cart01',3070,3117,yaw=-25)
    static('PlazaBenchWest',SKY+'SM_Furniture01_Bench01',3090,3105,yaw=75)
    static('PlazaBenchEast',SKY+'SM_Furniture01_Bench01',3112,3102,yaw=-80)
    static('BarnHay01',SKY+'SM_HayPile01',3035,3059,yaw=65)
    static('BarnHay02',SKY+'SM_HayBale01',3044,3059,yaw=-20)
    static('SmithAnvil','/Game/Map_Resource/Megascans/3D_Assets/BlacksmithAnvilA/SM_BlacksmithAnvilA',3121,3080,yaw=30)
    static('InnFirewood',SKY+'SM_Firewood01_Item01',3070,3133,yaw=65)
    rng=random.Random(5201)
    for i,house in enumerate(LAYOUT['houses']):
        a=math.radians(house['yaw']);c,s=math.cos(a),math.sin(a)
        def local(u,v):return house['x']+c*u-s*v,house['y']+s*u+c*v
        for j in range(3 if house['style'] in ['inn','workshop','store'] else 1):
            x,y=local(house['width']/2+1.2+j*.6,-house['depth']/3)
            static(house['id']+'_Barrel'+str(j),ELDER+'SM_Wooden_Barrel_uc2ichvqx_lod3',x,y,yaw=rng.uniform(0,360),scale=(1.3,1.3,1.3))
        # Fences frame small irregular gardens, leaving the front lane open.
        for side in [-1,1]:
            n=max(2,round(house['depth']/2));v0=-house['depth']/2
            for j in range(n):
                x,y=local(side*(house['width']/2+4),v0+j*2+1)
                static(house['id']+'_Fence'+str(side)+'_'+str(j),SKY+'SM_FenceGarden01_200cm',x,y,yaw=house['yaw']+90)
    REPORT['counts']['detail_actors']=sum('Corridor.Details' in [str(t) for t in a.get_editor_property('tags')] for a in ACTORS.get_all_level_actors())


def distance_path(x,y,points):
    best=1e9
    for a,b in zip(points,points[1:]):
        dx,dy=b[0]-a[0],b[1]-a[1];t=max(0,min(1,((x-a[0])*dx+(y-a[1])*dy)/max(1e-6,dx*dx+dy*dy)))
        best=min(best,math.hypot(x-a[0]-dx*t,y-a[1]-dy*t))
    return best


def clearance(x,y):
    for house in LAYOUT['houses']:
        a=math.radians(house['yaw']);dx,dy=x-house['x'],y-house['y'];u=dx*math.cos(a)+dy*math.sin(a);v=-dx*math.sin(a)+dy*math.cos(a)
        if abs(u)<house['width']/2+7 and abs(v)<house['depth']/2+7:return False
    if math.hypot(x-3100,y-3100)<28:return False
    for road in PLAN['roads']:
        if distance_path(x,y,road['points'])<road['width_m']/2+5:return False
    for path in LAYOUT['footpaths']:
        if distance_path(x,y,path)<4:return False
    for river in PLAN['rivers']:
        if distance_path(x,y,river['points'])<river['width_m']/2+12:return False
    return True


def foliage_stage():
    clear_category('Nature')
    rng=random.Random(103017);groups=collections.defaultdict(list)
    trees=[ELDER+'SM_BlackAlder_Field_02',ELDER+'SM_BlackAlder_Field_04',ELDER+'SM_BlackAlder_Forest_01','/Game/Map_Resource/ThirdParty/PN_interactiveSpruceForest/Meshes/full/high/spruce_full_01']
    grass='/Game/Map_Resource/Megascans/3D_Plants/ThatchingGrass/SM_ThatchingGrass00_V2'
    green_grass='/Game/Map_Resource/Brushify/Materials/Landscape/ProceduralMeshes/Foliage/Grass/S_Grass'
    wheat='/Game/Map_Resource/Megascans/3D_Plants/WheatGrassB/SM_WheatGrass01_V1'
    rock='/Game/Map_Resource/Megascans/3D_Assets/GraniteRockAssembly/SM_GraniteRockAssembly'
    def add(path,kind,x,y,scale=1):
        obj=mesh(path);tile=(int(x//128),int(y//128));z=height(x,y)-(.12 if kind=='Tree' else .035)
        groups[(path,kind,*tile)].append(centered_transform(obj,point(x,y,z),rng.uniform(0,360),(scale,scale,scale)))
    accepted=[]
    # Ecological patches and open sight corridors, rather than uniform forest noise.
    for i in range(1600):
        x=rng.uniform(2740,3600);y=rng.uniform(2750,3750);r=math.hypot(x-3100,y-3100)
        patch=.5+.24*math.sin(x/63)*math.cos(y/78)+.16*math.sin((x+y)/41)
        if r<55 or rng.random()>patch or not clearance(x,y):continue
        if any(math.hypot(x-a,y-b)<10 for a,b in accepted):continue
        slope=math.hypot(height(x+3,y)-height(x-3,y),height(x,y+3)-height(x,y-3))/6
        if slope>.62:continue
        accepted.append((x,y));index=3 if y>3330 and rng.random()<.5 else rng.randrange(3)
        add(trees[index],'Tree',x,y,rng.uniform(.78,1.22))
    for i in range(1400):
        x=rng.uniform(2760,3530);y=rng.uniform(2770,3540)
        if rng.random()>.28 or not clearance(x,y):continue
        if math.hypot(x-3100,y-3100)<115:continue
        add(rock,'Rock',x,y,rng.uniform(.45,1.9))
    biome_preset=SOURCE/'MainWorld.biomes.json'
    landscape_owns_grass=biome_preset.exists() and json.loads(biome_preset.read_text(encoding='utf-8')).get('groundcover_owner')=='LandscapeGrass'
    for i in range(0 if landscape_owns_grass else 30000):
        x=rng.uniform(2960,3310);y=rng.uniform(2930,3330)
        if not clearance(x,y):continue
        if rng.random()>.78+.18*math.sin(x/37)*math.cos(y/48):continue
        if math.hypot(height(x+2,y)-height(x-2,y),height(x,y+2)-height(x,y-2))/4>.6:continue
        lush=rng.random()<.27
        add(green_grass if lush else grass,'Grass',x,y,rng.uniform(.28,.52) if lush else rng.uniform(.5,.85))
    for field in LAYOUT['fields']:
        a=math.radians(field['yaw']);c,s=math.cos(a),math.sin(a)
        for row in range(int(field['width']/1.3)):
            for column in range(int(field['depth']/1.2)):
                if rng.random()<.13:continue
                u=-field['width']/2+row*1.3+rng.uniform(-.18,.18);v=-field['depth']/2+column*1.2+rng.uniform(-.22,.22)
                x=field['x']+c*u-s*v;y=field['y']+s*u+c*v
                if clearance(x,y):add(wheat,'Wheat',x,y,rng.uniform(.68,.92))
    counts=collections.Counter(Grass=0)
    for i,((path,kind,tx,ty),transforms) in enumerate(groups.items()):
        obj=mesh(path)
        # Explicitly persist only the actual parents used by these instances.
        for slot in obj.get_editor_property('static_materials'):
            interface=slot.material_interface
            if not interface:continue
            parent=interface.get_base_material()
            if parent.get_path_name().startswith('/Game/'):
                parent.modify();EDIT.set_material_usage(parent,unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES)
                TOUCHED_MATERIALS.add(parent.get_outermost().get_name())
        owner=spawn(kind+'_'+str(tx)+'_'+str(ty)+'_'+str(i),point(tx*128+64,ty*128+64,0),'Nature')
        comp=component(owner,unreal.HierarchicalInstancedStaticMeshComponent,kind+'Instances');comp.set_static_mesh(obj)
        comp.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS if kind in ['Tree','Rock'] else unreal.CollisionEnabled.NO_COLLISION)
        comp.set_cast_shadow(kind in ['Tree','Rock'])
        if kind in ['Grass','Wheat']:comp.set_cull_distances(6500 if kind=='Grass' else 10000,14000 if kind=='Grass' else 22000)
        comp.add_instances(transforms,False,True,False);counts[kind]+=len(transforms)
    REPORT['counts'].update(dict(counts));REPORT['counts']['nature_spatial_groups']=len(groups)


def gateway_stage():
    clear_category('Gateway')
    # A bounded entrance composition, not a claim that the capital is complete.
    x,y=LAYOUT.get('capital_gate_center_m',[4100,4200]);yaw=-53.3658861240326
    gate='/Game/Map_Resource/MedCastle/Mesh/Houses_Towers/SM_Gate'
    tower='/Game/Map_Resource/MedCastle/Mesh/Houses_Towers/SM_MainTower2Doors'
    wall='/Game/Map_Resource/MedCastle/Mesh/HouseParts/SM_CastleWall'
    a=math.radians(yaw);c,s=math.cos(a),math.sin(a)
    for label,path,u,v,angle in [('CapitalGate',gate,0,0,yaw),('WestWatchtower',tower,-12,0,yaw),('EastWatchtower',tower,12,0,yaw),('WestCurtain',wall,-27,0,yaw+90),('EastCurtain',wall,27,0,yaw+90)]:
        px,py=x+c*u-s*v,y+s*u+c*v
        obj=mesh(path);e=obj.get_bounds().box_extent;ar=math.radians(angle);ca,sa=math.cos(ar),math.sin(ar)
        footing=min(height(px+(ca*dx-sa*dy)/100,py+(sa*dx+ca*dy)/100) for dx in [-e.x,e.x] for dy in [-e.y,e.y])-.2
        actor=static(label,path,px,py,footing,angle,category='Gateway')
        actor.set_actor_label('S01_'+label);actor.set_folder_path('StarterRegion/Settlements/S01/Entrance')
    # The provider's ArchedBridge is curved side geometry. Preserve it as a
    # library candidate; create a complete viaduct for this measured crossing.
    bx,by=4046.161531341347,4159.966266894848;heading=36.6341138759674
    spec=importlib.util.spec_from_file_location('starter_bridge',ROOT/'Scripts/World/Starter-BridgeGeometry.py')
    geometry=importlib.util.module_from_spec(spec);spec.loader.exec_module(geometry)
    obj,data=geometry.build(height,bx,by,heading)
    bridge=spawn('LakeOutStoneBridge',point(bx,by,data['base_m']),'Gateway',unreal.StaticMeshActor)
    bridge.static_mesh_component.modify();bridge.static_mesh_component.set_mobility(unreal.ComponentMobility.STATIC)
    bridge.static_mesh_component.set_static_mesh(obj);bridge.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    bridge.set_actor_rotation(unreal.Rotator(pitch=0,yaw=heading,roll=0),False)
    bridge.set_actor_label('S01_LakeOutStoneBridge');bridge.set_folder_path('StarterRegion/Settlements/S01/Entrance')
    REPORT['bridge']=data;REPORT['counts']['capital_entrance_actors']=6


def bridge_collision_stage():
    # Validate after the authoring stage yields so the new physics body is live.
    bridge=next(a for a in ACTORS.get_all_level_actors() if a.get_actor_label()=='S01_LakeOutStoneBridge')
    data=REPORT['bridge'];bx,by=data['center_m'];heading=data['heading']
    a=math.radians(heading);dx,dy=math.cos(a),math.sin(a)
    sample_span=data['span_m']*.48
    world=unreal.EditorLevelLibrary.get_editor_world();ignore=[v for v in ACTORS.get_all_level_actors() if v!=bridge]
    def deck(distance):
        p=point(bx+dx*distance,by+dy*distance,0)
        hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,200000),p-unreal.Vector(0,0,10000),unreal.TraceTypeQuery.ECC_VISIBILITY,True,ignore,unreal.DrawDebugTrace.NONE,True)
        values=hit.to_tuple() if hit else None;assert values and values[0],'Missing bridge collision deck'
        return values[4].z/100
    samples=[dict(distance_m=sample_span*i/4,deck_m=deck(sample_span*i/4)) for i in range(-4,5)]
    left,right=data['bank_heights_m'];length=data['span_m']
    expected=[left*(1-(p['distance_m']+length/2)/length)+right*((p['distance_m']+length/2)/length)+3.4*math.sin(math.pi*(p['distance_m']+length/2)/length) for p in samples]
    assert max(abs(p['deck_m']-z) for p,z in zip(samples,expected))<.015,'Collision must match the upper roadway face'
    assert min(p['deck_m'] for p in samples)>76.47,samples
    max_grade=max(abs(b['deck_m']-a['deck_m'])/(b['distance_m']-a['distance_m']) for a,b in zip(samples,samples[1:]))
    assert max_grade<.3,(max_grade,samples)
    REPORT['bridge'].update(deck_samples=samples,maximum_sampled_deck_grade=max_grade,nav_verified=False)


def finish_stage():
    save_owned()
    x,y=3145,3005
    unreal.EditorLevelLibrary.set_level_viewport_camera_info(point(x,y,height(x,y)+34),unreal.Rotator(pitch=-22,yaw=114,roll=0))
    unreal.EditorLevelLibrary.editor_set_game_view(True)
    REPORT['used_mesh_paths']=sorted(set(c.static_mesh.get_path_name().split('.')[0] for a in ACTORS.get_all_level_actors() if TAG in [str(t) for t in a.get_editor_property('tags')] for c in a.get_components_by_class(unreal.StaticMeshComponent) if c.static_mesh))
    REPORT['composition_revision']=LAYOUT.get('composition_revision',1)
    REPORT.update(map=MAP,scope='S00 inhabited hamlet and first S01 approach',weather_deferred=True,visual_quality_approved=False,performance_measured=False,hlod_built=False)


STAGES=[('terrain',terrain_stage),('surface',surface_stage),('buildings',buildings_stage),('details',details_stage),('foliage',foliage_stage),('gateway',gateway_stage),('bridge_collision',bridge_collision_stage),('saved',finish_stage)]
pending=[(name,func) for name,func in STAGES if name not in REPORT['completed']]
last_tick=time.monotonic();busy=False


def tick(dt):
    global last_tick,busy
    if busy or time.monotonic()-last_tick<3:return
    if not pending:
        unreal.unregister_slate_post_tick_callback(tick_handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False);report('COMPLETE');return
    name,func=pending.pop(0);busy=True;report(name+'_RUNNING',error=None)
    try:
        func();REPORT['completed'].append(name)
        if name!='terrain':save_owned()
        report(name+'_DONE')
    except Exception:
        unreal.unregister_slate_post_tick_callback(tick_handle);unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        report(name+'_FAILED',error=traceback.format_exc());unreal.log_error(traceback.format_exc())
    finally:busy=False;last_tick=time.monotonic()

if __name__=='__main__':
    unreal.EditorPythonScripting.set_keep_python_script_alive(True)
    report('QUEUED')
    tick_handle=unreal.register_slate_post_tick_callback(tick)
