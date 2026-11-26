"""Create a small saved WP route under /Game/World/Validation for local verification."""
import json
import os
from pathlib import Path
import re

import unreal


def build_fixture(name, report):
    root = f"/Game/World/Validation/{name}"
    map_path = f"{root}/L_{name}"
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    assets = unreal.EditorAssetLibrary

    if assets.does_directory_exist(root):
        raise ValueError(f"Preserve the existing fixture at {root}. Choose a fresh name.")

    cube = unreal.load_asset("/Engine/BasicShapes/Cube")
    tree = unreal.load_asset("/Game/Map_Resource/main_1/Meshes/SM_AutumnTree01")
    if not all((cube, tree)):
        raise RuntimeError("The fixture inputs are unavailable")
    if not levels.new_level(map_path, True) or not unreal.Project_JWorldAuthoringLibrary.enable_region_streaming():
        raise RuntimeError(f"Could not create a streaming WP level at {map_path}")
    world = editor.get_editor_world()
    partition = world.get_world_settings().get_editor_property("WorldPartition")
    # Keep the New Level template's pairing between its default layer and its runtime hash.
    layer = partition.get_editor_property("DefaultHLODLayer")
    if not layer:
        raise RuntimeError("The WP template lacks a default HLOD layer")

    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.Project_JWorldDefinition)
    definition = unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        f"DA_{name}", root, unreal.Project_JWorldDefinition, factory)
    definition.set_editor_property("region_id", f"Validation.{name}")
    definition.set_editor_property("preparation_radius", 12000.0)
    definition.set_editor_property("max_prediction_distance", 40000.0)
    state = actors.spawn_actor_from_class(unreal.Project_JWorldState, unreal.Vector())
    state.set_editor_property("is_spatially_loaded", False)
    state.set_editor_property("definition", definition)

    def mesh_actor(label, mesh, location, scale, hlod):
        actor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(*location))
        actor.set_actor_label(label)
        actor.tags = [unreal.Name(label)]
        actor.set_actor_scale3d(unreal.Vector(*scale))
        actor.set_editor_property("is_spatially_loaded", True)
        actor.set_editor_property("enable_auto_lod_generation", hlod)
        component = actor.static_mesh_component
        component.set_static_mesh(mesh)
        component.set_mobility(unreal.ComponentMobility.STATIC)
        return actor

    # Decks occupy different cells, so a single giant floor cannot mask streaming.
    for index in range(16):
        width = 24 if index != 1 else 5
        mesh_actor(f"RouteDeck{index:02}", cube, (index * 12000, 0, -50), (120, width, 1), False)

    canopy = assets.duplicate_asset(tree.get_path_name(), f"{root}/SM_ValidationCanopy")
    policy_class = unreal.load_class(None, "/Script/Project_JCore.Project_JDistantMeshPolicy")
    intent = unreal.new_object(policy_class, outer=canopy)
    if not unreal.ToolsetLibrary.set_object_properties(intent, json.dumps({"Role": "InstancedCanopy"})):
        raise RuntimeError("Could not set the distant canopy intent")
    canopy.set_editor_property("asset_user_data", [*canopy.get_editor_property("asset_user_data"), intent])
    for index in range(6):
        row, column = divmod(index, 3)
        specimen = mesh_actor(f"RouteCanopy{index:02}", canopy,
                              (18000 + column * 400, 2700 + row * 500, 0), (3, 3, 3), True)
        specimen.static_mesh_component.set_collision_profile_name("NoCollision")
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(2000, 0, 150))
    start.set_editor_property("is_spatially_loaded", False)
    # Keep a usable image in ordinary game rendering without changing project lighting settings.
    sun = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000),
                                       unreal.Rotator(pitch=-45, yaw=-25))
    sun.set_editor_property("is_spatially_loaded", False)

    if not assets.save_loaded_asset(definition) or not assets.save_loaded_asset(canopy) or not levels.save_current_level():
        raise RuntimeError(f"Could not save fixture packages at {root}")
    with report.open("x", encoding="utf-8") as stream:
        json.dump({"map": map_path, "definition": definition.get_path_name(),
                   "canopy": canopy.get_path_name(), "layer": layer.get_path_name(),
                   "bridge_x_cm": [6000, 18000], "road_end_x_cm": 186000,
                   "runtime_hash": unreal.Project_JWorldAuthoringLibrary.get_region_runtime_hash_name()}, stream, indent=2)


if __name__ == "__main__":
    name = os.environ["PROJECT_J_ROUTE_FIXTURE"]
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,31}", name):
        raise ValueError("Supply a short Unreal package name for PROJECT_J_ROUTE_FIXTURE")
    directory = Path(unreal.Paths.project_saved_dir()).resolve() / "Validation/OpenWorldRoute" / name
    directory.mkdir(parents=True, exist_ok=True)
    report = directory / "fixture.json"
    if report.exists():
        raise ValueError(f"Preserve evidence at {report}")
    build_fixture(name, report)
