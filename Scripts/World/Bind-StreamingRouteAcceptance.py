"""Add the same gameplay portal and a separate mount Blueprint to a generated validation route."""
import os
import re

import unreal


def bind(name):
    if not re.fullmatch(r"[A-Za-z][A-Za-z0-9_]{0,31}", name):
        raise ValueError("Invalid validation route package name")
    root = f"/Game/World/Validation/{name}"
    route = f"{root}/L_{name}"
    entry = f"{root}/L_{name}Entry"
    assets = unreal.EditorAssetLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    fixture_class = unreal.load_class(None, "/Script/Project_J.Project_JWorldRouteFixture")
    mount_source = "/Game/DataAssetSets/MMO/BP_MountMover_Starter"
    mount_target = f"{root}/BP_{name}GroundMount"
    if not fixture_class:
        raise ValueError("The native acceptance fixture is unavailable")
    if not levels.load_level(route):
        raise ValueError(f"Missing generated route {route}")
    if not assets.does_asset_exist(mount_target) and not assets.duplicate_asset(mount_source, mount_target):
        raise RuntimeError("Could not create the validation mount Blueprint")
    mount_class = assets.load_blueprint_class(mount_target)
    mount = unreal.get_default_object(mount_class)
    mover = mount.get_mount_mover()
    profile = mover.get_editor_property("motion_profile")
    profile.set_editor_property("ground_gallop_speed", 2600.0)
    profile.set_editor_property("can_fly", False)
    mover.modify()
    mover.set_editor_property("motion_profile", profile)
    if not assets.save_asset(mount_target, False):
        raise RuntimeError(f"Could not save {mount_target}")

    fixtures = [actor for actor in actors.get_all_level_actors() if actor.get_class() == fixture_class]
    if len(fixtures) > 1:
        raise ValueError("The route has duplicate acceptance fixtures")
    fixture = fixtures[0] if fixtures else actors.spawn_actor_from_class(fixture_class, unreal.Vector())
    fixture.set_editor_property("is_spatially_loaded", False)
    fixture.set_editor_property("mount_class", mount_class)
    fixture.set_editor_property("flight_mount_class", assets.load_blueprint_class(mount_source))
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {route}")
    if assets.does_asset_exist(entry):
        unreal.log(f"Project J route acceptance updated: {entry} -> {route} via {mount_class.get_path_name()}")
        return
    if not levels.new_level(entry):
        raise RuntimeError(f"Could not save {route} or create {entry}")

    floor = actors.spawn_actor_from_class(unreal.StaticMeshActor, unreal.Vector(0, 0, -50))
    floor.static_mesh_component.set_static_mesh(unreal.load_asset("/Engine/BasicShapes/Cube"))
    floor.set_actor_scale3d(unreal.Vector(12, 12, 1))
    actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 150))
    light = actors.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 1000))
    light.set_actor_rotation(unreal.Rotator(pitch=-45), False)
    portal = actors.spawn_actor_from_class(unreal.Project_JWorldPortal, unreal.Vector(145, 0, 100))
    portal.set_editor_property("standalone_destination", assets.load_asset(route))
    fixture = actors.spawn_actor_from_class(fixture_class, unreal.Vector())
    fixture.set_editor_property("entry", True)
    if not levels.save_current_level():
        raise RuntimeError(f"Could not save {entry}")
    unreal.log(f"Project J route acceptance: {entry} -> {route} via {mount_class.get_path_name()}")


if __name__ == "__main__":
    bind(os.environ["PROJECT_J_ACCEPTANCE_ROUTE"])
