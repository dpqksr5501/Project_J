"""Configure owned forest PCG selectors, preserving provider materials/meshes."""
import json
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];BASE='/Game/World/Starter';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
TREE_CULL=json.loads((ROOT/'Content/World/Starter/Source/StarterWorld.forest-streaming.json').read_text()).get('tree_instance_cull_cm',[0,0])
assert not (OUT/'forest-rollout.json').exists(),'Initial palette setup is retired after rollout; edit the owned graph explicitly'
graph=unreal.load_asset(BASE+'/PCG/PCG_StarterForest');assert graph
lib=unreal.EditorAssetLibrary
assert lib.get_metadata_tag(graph,'ProjectJ.Authoring')=='ProjectJ.Starter.WorldBiome.v2'
node=unreal.load_object(None,graph.get_path_name()+':Trees');assert node
settings=node.get_settings();selector=settings.get_editor_property('mesh_selector_parameters')
assert isinstance(selector,unreal.PCGMeshSelectorWeighted)
entries=[];report=[]
for name,weight in [('SM_BlackAlder_Field_02',3),('SM_BlackAlder_Field_04',2),('SM_BlackAlder_Forest_01',5)]:
    mesh=unreal.load_asset('/Game/ElderboomVillage/Meshes/'+name);assert mesh
    assert all(s.material_interface.get_base_material().get_editor_property('used_with_instanced_static_meshes') for s in mesh.static_materials if s.material_interface)
    entry=unreal.PCGMeshSelectorWeightedEntry();descriptor=entry.get_editor_property('descriptor')
    descriptor.set_editor_property('static_mesh',mesh)
    descriptor.set_editor_property('component_class',unreal.HierarchicalInstancedStaticMeshComponent)
    descriptor.set_editor_property('include_in_hlod',True)
    descriptor.set_editor_property('instance_start_cull_distance',TREE_CULL[0])
    descriptor.set_editor_property('instance_end_cull_distance',TREE_CULL[1])
    body=descriptor.get_editor_property('body_instance');body.set_editor_property('collision_profile_name','BlockAll');descriptor.set_editor_property('body_instance',body)
    entry.set_editor_property('descriptor',descriptor);entry.set_editor_property('weight',weight);entries.append(entry)
    bounds=mesh.get_bounds();setup=mesh.get_editor_property('body_setup');geo=setup.get_editor_property('agg_geom') if setup else None
    shapes=sum(len(geo.get_editor_property(k)) for k in ['box_elems','sphere_elems','sphyl_elems','convex_elems']) if geo else 0
    report.append(dict(mesh=mesh.get_path_name(),simple_collision_shapes=shapes,height_cm=bounds.box_extent.z*2))
selector.set_editor_property('mesh_entries',entries)
shrubs=unreal.load_object(None,graph.get_path_name()+':Shrubs').get_settings().get_editor_property('mesh_selector_parameters')
shrub_entries=[]
for index in [1,3,5]:
    mesh=unreal.load_asset('/Game/Map_Resource/Nanite_Env_Bundle_1/StunningNaturalPlace/Meshes/Foliage/Bush/S_Bush_Type_1_Small_'+str(index));assert mesh
    assert all(s.material_interface.get_base_material().get_editor_property('used_with_instanced_static_meshes') for s in mesh.static_materials if s.material_interface)
    entry=unreal.PCGMeshSelectorWeightedEntry();descriptor=entry.get_editor_property('descriptor')
    descriptor.set_editor_property('static_mesh',mesh);descriptor.set_editor_property('component_class',unreal.HierarchicalInstancedStaticMeshComponent)
    descriptor.set_editor_property('include_in_hlod',False)
    descriptor.set_editor_property('instance_start_cull_distance',24000);descriptor.set_editor_property('instance_end_cull_distance',36000)
    entry.set_editor_property('descriptor',descriptor);entry.set_editor_property('weight',1);shrub_entries.append(entry)
shrubs.set_editor_property('mesh_entries',shrub_entries)
terrain=unreal.load_object(None,graph.get_path_name()+':Terrain').get_settings()
selection=terrain.get_editor_property('actor_selector')
selection.set_editor_property('actor_filter',unreal.PCGActorFilter.ALL_WORLD_ACTORS)
selection.set_editor_property('actor_selection',unreal.PCGActorSelection.BY_CLASS)
selection.set_editor_property('actor_selection_class',unreal.LandscapeProxy)
selection.set_editor_property('select_multiple',True)
terrain.set_editor_property('actor_selector',selection)
lib.save_loaded_asset(graph)
actor=next(a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors() if a.get_actor_label().startswith('PCG_Forest_S04_Test'))
actor.modify();actor.set_actor_location(unreal.Vector(236800,83200,60000),False,False);actor.set_actor_label('PCG_Forest_12_9')
(OUT/'pcg-tree-palette.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('STARTER_PCG_PALETTE_READY')
