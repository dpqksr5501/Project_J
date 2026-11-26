"""Build a small two-level WP fixture to exercise inherited HLOD with the native builder."""
import os
import re
import unreal as u


def build(name):
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,31}", name):
        raise ValueError("Invalid fixture package name")
    root = "/Game/World/Validation/" + name
    leaf = root + "/L_Canopy"
    host = root + "/L_Host"
    levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    assets = u.EditorAssetLibrary
    if assets.does_directory_exist(root):
        raise FileExistsError(root)
    tree = u.load_asset("/Game/World/Validation/RidgeRoute03/SM_ValidationCanopy")
    cube = u.load_asset("/Engine/BasicShapes/Cube")
    if not tree or not cube:
        raise ValueError("Saved canopy or start platform mesh unavailable")
    assert levels.new_level(leaf, True)
    for x in (0, 650, 1300):
        actor = actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(x, 0, 0))
        actor.static_mesh_component.set_static_mesh(tree)
        actor.static_mesh_component.set_mobility(u.ComponentMobility.STATIC)
        actor.static_mesh_component.set_collision_profile_name("NoCollision")
        actor.set_actor_scale3d(u.Vector(3, 3, 3))
    assert levels.save_current_level()
    assert levels.new_level(host, True)
    assert u.Project_JWorldAuthoringLibrary.enable_region_streaming()
    instance = actors.spawn_actor_from_class(u.LevelInstance, u.Vector(16000, 2000, 0))
    instance.set_editor_property("world_asset", u.load_asset(leaf))
    floor = actors.spawn_actor_from_class(u.StaticMeshActor, u.Vector(14000, 2000, -50))
    floor.static_mesh_component.set_static_mesh(cube)
    floor.set_actor_scale3d(u.Vector(15, 15, 1))
    floor.set_editor_property("is_spatially_loaded", False)
    floor.set_editor_property("enable_auto_lod_generation", False)
    start = actors.spawn_actor_from_class(u.PlayerStart, u.Vector(14000, 2000, 250))
    start.set_editor_property("is_spatially_loaded", False)
    sun = actors.spawn_actor_from_class(u.DirectionalLight, u.Vector(0, 0, 1200), u.Rotator(pitch=-35, yaw=-25))
    sun.set_editor_property("is_spatially_loaded", False)
    assert levels.save_current_level()
    u.log("PROJECTJ_NESTED_CANOPY host=" + host + " leaf=" + leaf)


build(os.environ["PROJECT_J_NESTED_FIXTURE"])
