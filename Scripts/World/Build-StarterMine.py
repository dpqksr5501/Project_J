"""Save H08's hollow adit and scanned timber supports, without editing Landscape."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'H08-mine-build.json'
assert not FILE.exists(),'Inspect the existing build before attempting another'
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
P=json.loads((SOURCE/'StarterWorld.H08.mine.json').read_text());A=module('authoring','Starter-SettlementAuthoring.py').SettlementAuthoring(P['owner'],'Starter/V4/Hunting/H08/Mine');G=module('geometry','Build-StarterCorridor.py');C=module('contract','Starter-WorldDefinitionContract.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json')
assert not any(P['owner'] in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
assert not unreal.EditorAssetLibrary.does_asset_exist(P['mesh_asset'])
definition=next(a for a in A.subsystem.get_all_level_actors() if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition')
R=dict(phase='STARTED',source_sha256=hashlib.sha256((SOURCE/'StarterWorld.H08.mine.json').read_bytes()).hexdigest(),previous_places=[C.snapshot_place(v) for v in definition.places],previous_connections=[C.snapshot_connection(v) for v in definition.connections],source_plan_hash=definition.source_plan_hash,art_complete=False,terrain_modified=False)
FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
try:
    lib=unreal.EditorAssetLibrary;edit=unreal.MaterialEditingLibrary;material_path='/Game/World/Starter/Materials/M_H08_MineRock';assert not lib.does_asset_exist(material_path)
    material=unreal.AssetToolsHelpers.get_asset_tools().create_asset('M_H08_MineRock','/Game/World/Starter/Materials',unreal.Material,unreal.MaterialFactoryNew());assert material
    for suffix,prop in [('D',unreal.MaterialProperty.MP_BASE_COLOR),('N',unreal.MaterialProperty.MP_NORMAL)]:
        tex=unreal.load_asset('/Game/Map_Resource/Brushify/Materials/Landscape/Textures/Cliffs/T_CliffSurface_'+suffix);assert tex
        node=edit.create_material_expression(material,unreal.MaterialExpressionTextureSample,-300,0 if suffix=='D' else 250);node.texture=tex
        virtual=tex.get_editor_property('virtual_texture_streaming')
        node.sampler_type=(unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_NORMAL if virtual else unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL) if suffix=='N' else (unreal.MaterialSamplerType.SAMPLERTYPE_VIRTUAL_COLOR if virtual else unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
        assert edit.connect_material_property(node,'RGB',prop)
    rough=edit.create_material_expression(material,unreal.MaterialExpressionConstant,-300,500);rough.set_editor_property('r',.86);assert edit.connect_material_property(rough,'',unreal.MaterialProperty.MP_ROUGHNESS)
    assert not edit.recompile_material(material);lib.set_metadata_tag(material,'ProjectJ.Authoring',P['owner']);assert lib.save_loaded_asset(material)
    dynamic=unreal.DynamicMesh();buffers=unreal.GeometryScriptSimpleMeshBuffers();buffers.vertices=[unreal.Vector(*(v*100 for v in p)) for p in P['vertices_m']];buffers.triangles=[unreal.IntVector(*t) for t in P['triangles']];buffers.uv0=[unreal.Vector2D(*uv) for uv in P['uv0']];unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic,buffers)
    options=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();options.enable_collision=True;options.collision_mode=unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE;options.enable_recompute_normals=True;options.enable_recompute_tangents=True
    mesh=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dynamic,P['mesh_asset'],options)[0];assert mesh;mesh.set_material(0,material);lib.set_metadata_tag(mesh,'ProjectJ.Authoring',P['owner']);lib.set_metadata_tag(mesh,'ProjectJ.SourceHash',R['source_sha256']);assert lib.save_loaded_asset(mesh)
    cx,cy,floor=P['center_m'];actor=A.spawn('H08_RidgeMine',P['mesh_asset'],cx,cy,floor,P['yaw_degrees']);actor.set_actor_transform(unreal.Transform(location=A.point(cx,cy,floor),rotation=unreal.Rotator(pitch=0,yaw=P['yaw_degrees'],roll=0),scale=unreal.Vector(1,1,1)),False,False);A.created[-1]['location_cm']=list(actor.get_actor_location().to_tuple())
    actor.tags=[unreal.Name(P['owner']),unreal.Name('ProjectJ.Location.H08')]
    timber=unreal.load_asset('/Game/ElderboomVillage/Meshes/SM_Worn_Wooden_Beam_tgmsdc0fa_lod3');assert timber
    frames=G.component(actor,unreal.HierarchicalInstancedStaticMeshComponent,'TimberFrames');frames.set_static_mesh(timber);frames.set_collision_profile_name('BlockAll')
    for i,s in enumerate(timber.static_materials):frames.set_material(i,A.instanced_material(s.material_interface))
    b=timber.get_bounds();length=b.box_extent.z*2;beam_transforms=[]
    def beam(center,rotation,height):
        scale=unreal.Vector(1.25,1.25,height*100/length);transform=unreal.Transform(rotation=rotation,scale=scale);offset=transform.transform_location(b.origin);transform.translation=unreal.Vector(*center)-offset;frames.add_instance(transform,False);beam_transforms.append(dict(center_cm=center,rotation=list(rotation.to_tuple()),length_m=height))
    for u in P['timber_frame_stations_m']:
        w=2.4 if u<8 else 2.4+(u-8)/10*1.2
        for v in [-w+.18,w-.18]:beam([u*100,v*100,135],unreal.Rotator(),2.7)
        beam([u*100,0,280],unreal.Rotator(pitch=0,yaw=0,roll=90),w*2,)
    stairs=G.component(actor,unreal.HierarchicalInstancedStaticMeshComponent,'EntryStairs');cube=unreal.load_asset('/Engine/BasicShapes/Cube');stairs.set_static_mesh(cube);stairs.set_material(0,A.instanced_material(material));stairs.set_collision_profile_name('BlockAll')
    steps=module('support','Starter-SettlementSupport.py').plan_mine_entry_steps(P,T.height)
    for step in steps:
        u,top,bottom=step['local_u_m'],step['top_m'],step['bottom_m'];stairs.add_instance(G.centered_transform(cube,unreal.Vector(u*100,0,(bottom-floor)*100),0,(.36,1.8,top-bottom)),False)
    assert steps,'No entrance connection was constructed'
    A.save();R.update(phase='COMPLETE',actor_path=actor.get_path_name(),actors=A.created,mesh=P['mesh_asset'],triangles=mesh.get_num_triangles(0),timber_frame_instances=len(beam_transforms),timbers=beam_transforms,stairs=steps,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
except Exception:R.update(phase='FAILED',error=traceback.format_exc());unreal.log_error(R['error'])
FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
