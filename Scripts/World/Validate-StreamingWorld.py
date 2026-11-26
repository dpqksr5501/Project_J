"""Inventory saved maps by content root, or validate one intended streaming region."""
import os

import unreal


def inventory_maps(root):
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.scan_paths_synchronous([root])
    maps = registry.get_assets(unreal.ARFilter(
        package_paths=[root], recursive_paths=True,
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "World")],
        include_only_on_disk_assets=True))
    if not maps:
        raise ValueError(f"{root}: no saved maps discovered")
    settings = unreal.get_default_object(unreal.load_class(None, "/Script/DeveloperToolSettings.ProjectPackagingSettings"))
    explicit_cook = {entry.get_editor_property("FilePath") for entry in settings.get_editor_property("MapsToCook")}
    for package in sorted(str(asset.package_name) for asset in maps):
        unreal.log(f"Project J map inventory: {package} MapsToCook={package in explicit_cook}")
    unreal.log(f"Project J map inventory: {len(maps)} saved maps under {root}")


def validate_world(map_package, minimum_hlods=0):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    if not levels.load_level(map_package):
        raise ValueError(f"Cannot open streaming map {map_package}")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    if not unreal.Project_JWorldAuthoringLibrary.is_region_streaming_enabled(world):
        raise ValueError(f"{map_package}: this region requires enabled World Partition streaming")
    partition = unreal.WorldPartitionBlueprintLibrary
    descriptors = partition.get_actor_descs()
    if isinstance(descriptors, tuple):
        available, descriptors = descriptors
        if not available:
            raise ValueError(f"{map_package}: World Partition descriptors are unavailable")
    if not descriptors:
        raise ValueError(f"{map_package}: no saved World Partition descriptors")

    if minimum_hlods:
        hlod_type = unreal.load_class(None, "/Script/Engine.WorldPartitionHLOD")
        hlods = sum(unreal.MathLibrary.class_is_child_of(desc.get_editor_property("native_class"), hlod_type)
                    for desc in descriptors if not desc.get_editor_property("actor_is_editor_only"))
        if hlods < minimum_hlods:
            raise ValueError(f"{map_package}: expected at least {minimum_hlods} saved HLOD actors, found {hlods}")
        unreal.log(f"Project J saved HLOD actor count: {hlods}")
    else:
        unreal.log("HLOD count not checked for this region (set PROJECT_J_STREAMING_MIN_HLODS when distant proxies are required)")

    state_type = unreal.Project_JWorldState.static_class()
    placements = [
        desc for desc in descriptors
        if not desc.get_editor_property("actor_is_editor_only")
        and unreal.MathLibrary.class_is_child_of(desc.get_editor_property("native_class"), state_type)
    ]
    if len(placements) != 1:
        raise ValueError(f"{map_package}: expected one runtime WorldState, found {len(placements)}")
    placement = placements[0]
    if placement.get_editor_property("is_spatially_loaded"):
        raise ValueError(f"{map_package}: WorldState must remain loaded across region boundaries")

    state_path = str(placement.get_editor_property("actor_path"))
    state = unreal.find_object(None, state_path)
    if state:
        validate_state(map_package, placement, state_path, state)
    else:
        # Most always-loaded states are already resident. Only release a temporary
        # load we acquired for this inspection, including on a validation failure.
        guids = [placement.get_editor_property("guid")]
        partition.load_actors(guids)
        try:
            validate_state(map_package, placement, state_path, unreal.find_object(None, state_path))
        finally:
            partition.unload_actors(guids)


def validate_state(map_package, placement, state_path, state):
    if not isinstance(state, unreal.Project_JWorldState):
        raise ValueError(f"{map_package}: cannot resolve saved WorldState {state_path}")
    for layer_path in placement.get_editor_property("data_layer_assets"):
        layer = unreal.load_asset(unreal.SystemLibrary.break_soft_object_path(layer_path))
        if not isinstance(layer, unreal.DataLayerAsset) or layer.is_runtime():
            raise ValueError(f"{state_path}: always-loaded WorldState has an unresolved or runtime data layer {layer_path}")
    definition = state.get_editor_property("definition")
    if not definition:
        raise ValueError(f"{state_path}: assign WorldState.Definition")
    data = unreal.EditorAssetLibrary.find_asset_data(definition.get_path_name())
    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    result, errors, warnings = validator.is_asset_valid(data, unreal.DataValidationUsecase.MANUAL)
    if result != unreal.DataValidationResult.VALID or errors:
        raise ValueError(f"{definition.get_path_name()}: {result}, {errors}")
    for warning in warnings:
        unreal.log_warning(str(warning))
    asset_id = unreal.SystemLibrary.get_primary_asset_id_from_object(definition)
    if unreal.SystemLibrary.get_object_from_primary_asset_id(asset_id) != definition:
        raise ValueError(f"{definition.get_path_name()}: Asset Manager ID {asset_id} does not resolve to this definition")
    unreal.log(f"Project J streaming map validated: {map_package} -> {definition.get_path_name()}")


if __name__ == "__main__":
    region = os.environ.get("PROJECT_J_STREAMING_MAP")
    root = os.environ.get("PROJECT_J_MAP_INVENTORY_ROOT")
    if bool(root) == bool(region):
        raise ValueError("Set exactly one of PROJECT_J_STREAMING_MAP or PROJECT_J_MAP_INVENTORY_ROOT")
    if region:
        minimum_hlods = int(os.environ.get("PROJECT_J_STREAMING_MIN_HLODS", "0"))
        if minimum_hlods < 0:
            raise ValueError("PROJECT_J_STREAMING_MIN_HLODS must be nonnegative")
        validate_world(region, minimum_hlods)
    else:
        inventory_maps(root)
