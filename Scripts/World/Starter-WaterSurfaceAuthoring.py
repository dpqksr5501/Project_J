"""Owned level-water meshes; shared by basin and coastal authoring."""
import unreal

def create_owned_mesh(spec,surface,owner,units=100):
    assert not unreal.EditorAssetLibrary.does_asset_exist(spec['mesh_asset'])
    cx,cy,cz=spec['center_m'];dynamic=unreal.DynamicMesh();buffers=unreal.GeometryScriptSimpleMeshBuffers()
    buffers.set_editor_property('vertices',[unreal.Vector((x-cx)*units,(y-cy)*units,(z-cz)*units) for x,y,z in surface['vertices_m']]);buffers.set_editor_property('triangles',[unreal.IntVector(*v) for v in surface['triangles']]);buffers.set_editor_property('normals',[unreal.Vector(0,0,1) for v in surface['vertices_m']]);buffers.set_editor_property('uv0',[unreal.Vector2D((x-cx)/5,(y-cy)/5) for x,y,z in surface['vertices_m']]);unreal.GeometryScript_MeshEdits.append_buffers_to_mesh(dynamic,buffers);assert dynamic.get_triangle_count()==len(surface['triangles'])
    options=unreal.GeometryScriptCreateNewStaticMeshAssetOptions();options.set_editor_property('enable_collision',True);options.set_editor_property('collision_mode',unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE);options.set_editor_property('enable_recompute_normals',True);options.set_editor_property('enable_recompute_tangents',True)
    mesh=unreal.GeometryScript_NewAssetUtils.create_new_static_mesh_asset_from_mesh(dynamic,spec['mesh_asset'],options)[0];assert mesh;material=unreal.load_asset(spec['material']);assert material;mesh.set_material(0,material)
    for key,value in [('ProjectJ.Authoring',owner),('ProjectJ.SourceHash',spec['geometry_sha256']),('ProjectJ.GeometryID',spec['geometry_id'])]:unreal.EditorAssetLibrary.set_metadata_tag(mesh,key,value)
    assert unreal.EditorAssetLibrary.save_loaded_asset(mesh);return mesh

def configure_surface_query(component):
    component.set_collision_enabled(unreal.CollisionEnabled.QUERY_ONLY);component.set_collision_response_to_all_channels(unreal.CollisionResponseType.ECR_IGNORE);component.set_collision_response_to_channel(unreal.CollisionChannel.ECC_VISIBILITY,unreal.CollisionResponseType.ECR_BLOCK);component.set_cast_shadow(False)
