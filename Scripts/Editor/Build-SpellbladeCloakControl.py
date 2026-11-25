"""Build cloak controls or promote the validated Spellblade collision subsets."""

import json
from pathlib import Path

import unreal


SOURCE = "/Game/Characters/Spellblade/Meshes/SK_Spellblade_cloak"
CONTROL = "/Game/Tests/Cloth/SK_Spellblade_Cloak_NoBodyCollision"
REDUCED = "/Game/Tests/Cloth/SK_Spellblade_Cloak_ReducedBodyCollision"
REDUCED_PHYSICS = "/Game/Tests/Cloth/PA_Spellblade_Cloak_ReducedBodyCollision"
LIVE_CLOAK_PHYSICS = "/Game/Characters/Spellblade/Physics/PA_Spellblade_ClothCollision"
LIVE_HAIR = "/Game/Characters/Spellblade/Physics/PA_Spellblade_Hair"
REDUCED_HAIR = "/Game/Tests/Physics/PA_Spellblade_Hair_CollisionTrial"
OTHER_GEOMETRY = (
    "sphere_elems", "box_elems", "convex_elems", "tapered_capsule_elems",
    "level_set_elems", "skinned_level_set_elems", "ml_level_set_elems", "skinned_triangle_mesh_elems",
)


def clothing_assets(mesh):
    return list(mesh.get_editor_property("mesh_clothing_assets"))


def check_control(source, control):
    source_sections = clothing_assets(source)
    control_sections = clothing_assets(control)
    if not source_sections or [part.get_name() for part in source_sections] != [part.get_name() for part in control_sections]:
        raise RuntimeError("The cloak control must have the original clothing sections.")
    if source.get_editor_property("skeleton") != control.get_editor_property("skeleton"):
        raise RuntimeError("The cloak control must use the original skeleton.")
    if any(part.get_outer() != control for part in control_sections):
        raise RuntimeError("The cloak control must own its clothing sections.")
    if any(not part.get_editor_property("physics_asset") for part in source_sections):
        raise RuntimeError("A source clothing section has no body collision asset.")
    return control_sections


def build_cloak(source, destination, physics):
    if unreal.EditorAssetLibrary.does_asset_exist(destination):
        control = unreal.load_asset(destination, unreal.SkeletalMesh)
        if control is None or any(part.get_editor_property("physics_asset") != physics
                                  for part in check_control(source, control)):
            raise RuntimeError(f"Preserve and inspect the existing control at {destination}")
        unreal.log(f"Reusing the existing cloak control {destination}")
        return

    control = unreal.EditorAssetLibrary.duplicate_asset(SOURCE, destination)
    if control is None:
        raise RuntimeError(f"Could not duplicate the source cloak to {destination}")
    for part in check_control(source, control):
        part.set_editor_property("physics_asset", physics)
    if any(part.get_editor_property("physics_asset") != physics for part in check_control(source, control)):
        raise RuntimeError(f"A control clothing section has the wrong body collision asset at {destination}.")
    if not unreal.EditorAssetLibrary.save_loaded_asset(control):
        raise RuntimeError(f"Could not save the cloak control {destination}")
    unreal.log(f"Built the cloak collision control {destination}")


def body_setups(asset):
    return {str(body.get_editor_property("bone_name")): body
            for body in unreal.ObjectIterator(unreal.BodySetup) if body.get_outer() == asset}


def capsules(body):
    geometry = body.get_editor_property("agg_geom")
    if any(geometry.get_editor_property(kind) for kind in OTHER_GEOMETRY):
        raise RuntimeError(f"Unexpected geometry on {body.get_path_name()}; inspect before editing.")
    return list(geometry.get_editor_property("sphyl_elems"))


def capsule_values(shape):
    center = shape.get_editor_property("center")
    rotation = shape.get_editor_property("rotation")
    return (str(shape.get_editor_property("name")), center.x, center.y, center.z,
            rotation.pitch, rotation.yaw, rotation.roll,
            shape.get_editor_property("radius"), shape.get_editor_property("length"))


def reduced_collision(original):
    original_parts = body_setups(original)
    if not original_parts:
        raise RuntimeError("The original cloak Physics Asset has no loaded body setups.")
    original_capsules = {bone: [capsule_values(shape) for shape in capsules(part)]
                         for bone, part in original_parts.items()}
    kept = {bone: [shape for shape in values if shape[0].startswith("Cape")]
            for bone, values in original_capsules.items()}
    original_count = sum(map(len, original_capsules.values()))
    kept_count = sum(map(len, kept.values()))
    if not 0 < kept_count <= original_count:
        raise RuntimeError("Expected named Cape capsules in the source.")

    exists = unreal.EditorAssetLibrary.does_asset_exist(REDUCED_PHYSICS)
    if exists:
        control = unreal.load_asset(REDUCED_PHYSICS, unreal.PhysicsAsset)
    else:
        control = unreal.EditorAssetLibrary.duplicate_asset(original.get_path_name(), REDUCED_PHYSICS)
    if not control:
        raise RuntimeError(f"Missing Physics Asset {REDUCED_PHYSICS}")
    parts = body_setups(control)
    if not exists and parts.keys() != original_parts.keys():
        raise RuntimeError("The reduced Physics Asset must preserve the original body bindings.")
    if not exists:
        for bone, part in parts.items():
            to_keep = [shape for shape in capsules(part) if capsule_values(shape) in kept[bone]]
            geometry = part.get_editor_property("agg_geom")
            if len(to_keep) != len(geometry.get_editor_property("sphyl_elems")):
                geometry.set_editor_property("sphyl_elems", to_keep)
                part.set_editor_property("agg_geom", geometry)
    actual = {bone: [capsule_values(shape) for shape in capsules(part)] for bone, part in parts.items()}
    if not actual.keys() <= kept.keys() or {bone: shapes for bone, shapes in actual.items() if shapes} != {
            bone: shapes for bone, shapes in kept.items() if shapes}:
        raise RuntimeError("The reduced Physics Asset does not match the authored Cape capsules.")
    removed = unreal.Project_JAssetMigrationLibrary.remove_empty_physics_asset_bodies(control)
    if removed < 0 or (removed not in (0, sum(not shapes for shapes in kept.values()))):
        raise RuntimeError("The reduced Physics Asset did not retire the expected empty bodies.")
    if (not exists or removed > 0) and not unreal.EditorAssetLibrary.save_loaded_asset(control):
        raise RuntimeError(f"Could not save {REDUCED_PHYSICS}")
    unreal.log(f"CLOAK_REDUCED_COLLISION original_capsules={original_count} retained_capsules={kept_count} "
               f"retained_bodies={sum(bool(shapes) for shapes in kept.values())} retired_empty_bodies={removed}")
    return control


def build():
    source = unreal.load_asset(SOURCE, unreal.SkeletalMesh)
    if source is None:
        raise RuntimeError(f"Missing source cloak {SOURCE}")
    original_assets = {part.get_editor_property("physics_asset").get_path_name()
                       for part in clothing_assets(source) if part.get_editor_property("physics_asset")}
    if len(original_assets) != 1 or not all(part.get_editor_property("physics_asset") for part in clothing_assets(source)):
        raise RuntimeError("The three source sections must share their body collision asset.")
    original = unreal.load_asset(original_assets.pop(), unreal.PhysicsAsset)
    build_cloak(source, CONTROL, None)
    build_cloak(source, REDUCED, reduced_collision(original))


def prepare_promotion(registry, destination, trial, expected_referencers, expected_counts):
    live = unreal.load_asset(destination, unreal.PhysicsAsset)
    candidate = unreal.load_asset(trial, unreal.PhysicsAsset)
    if not live or not candidate:
        raise RuntimeError(f"Missing promotion input for {destination}")
    options = unreal.AssetRegistryDependencyOptions()
    options.set_editor_property("include_hard_package_references", True)
    options.set_editor_property("include_soft_package_references", True)
    options.set_editor_property("include_searchable_names", False)
    options.set_editor_property("include_soft_management_references", False)
    options.set_editor_property("include_hard_management_references", False)
    if set(map(str, registry.get_referencers(destination, options))) != expected_referencers:
        raise RuntimeError(f"Unexpected reference scope for {destination}")

    original_bodies = body_setups(live)
    tested_bodies = body_setups(candidate)
    tested = {bone: list(map(capsule_values, capsules(body))) for bone, body in tested_bodies.items()}
    original_count = sum(len(capsules(body)) for body in original_bodies.values())
    tested_count = sum(map(len, tested.values()))
    before_count, after_count = expected_counts
    if (tested_count != after_count or original_count not in (before_count, after_count)
            or not tested.keys() <= original_bodies.keys()
            or any(not shapes for shapes in tested.values())):
        raise RuntimeError(f"Unexpected source or tested capsule population at {destination}")

    changes = []
    for bone, body in original_bodies.items():
        original = capsules(body)
        target = tested.get(bone, [])
        if bone in tested_bodies and body.get_editor_property("physics_type") != tested_bodies[bone].get_editor_property("physics_type"):
            raise RuntimeError(f"Tested physics type differs on {bone}")
        keep = [shape for shape in original if capsule_values(shape) in target]
        if list(map(capsule_values, keep)) != target:
            raise RuntimeError(f"Tested capsule geometry or order differs on {bone}")
        if len(keep) != len(original) or not keep:
            changes.append((body, keep))

    return live, changes, len(original_bodies) - len(tested_bodies), tested_count


def promote_or_verify(apply):
    mesh = unreal.load_asset(SOURCE, unreal.SkeletalMesh)
    if not mesh:
        raise RuntimeError(f"Missing cloak mesh {SOURCE}")
    sections = clothing_assets(mesh)
    if len(sections) != 3 or any(
        part.get_outer() != mesh or part.get_editor_property("physics_asset").get_path_name().split(".")[0] != LIVE_CLOAK_PHYSICS
        for part in sections
    ):
        raise RuntimeError("Unexpected production cloak section ownership or collision binding.")

    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.search_all_assets(True)
    graph_root = "/Game/Characters/Spellblade/Animation/"
    plans = [
        prepare_promotion(registry, LIVE_CLOAK_PHYSICS, REDUCED_PHYSICS, {SOURCE}, (16, 10)),
        prepare_promotion(registry, LIVE_HAIR, REDUCED_HAIR,
                          {graph_root + "ABP_Spellblade_Body", graph_root + "ABP_Spellblade_Retarget"}, (49, 5)),
    ]
    if apply:
        for asset, changes, removed, _ in plans:
            for body, keep in changes:
                geometry = body.get_editor_property("agg_geom")
                geometry.set_editor_property("sphyl_elems", keep)
                body.set_editor_property("agg_geom", geometry)
            if unreal.Project_JAssetMigrationLibrary.remove_empty_physics_asset_bodies(asset) != removed:
                raise RuntimeError(f"Unexpected empty body count for {asset.get_path_name()}")
        for asset, changes, _, _ in plans:
            if changes and not unreal.EditorAssetLibrary.save_loaded_asset(asset):
                raise RuntimeError(f"Could not save {asset.get_path_name()}")
        return

    if any(changes or removed for _, changes, removed, _ in plans):
        raise RuntimeError("The installed assets differ from the tested collision subsets.")
    settings = unreal.ValidateAssetsSettings()
    settings.set_editor_property("skip_excluded_directories", False)
    subsystem = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    paths = (SOURCE, LIVE_CLOAK_PHYSICS, LIVE_HAIR)
    assets = [unreal.EditorAssetLibrary.find_asset_data(path) for path in paths]
    issues, results = subsystem.validate_assets_with_settings(assets, settings)
    report = {
        "asset_paths": paths,
        "cloth_sections": len(sections),
        "cloak_capsules": plans[0][3],
        "hair_capsules": plans[1][3],
        "checked": results.num_checked,
        "valid": results.num_valid,
        "invalid": results.num_invalid,
        "warnings": results.num_warnings,
        "unable": results.num_unable_to_validate,
        "issues": issues,
    }
    if results.num_checked != len(assets) or results.num_invalid or results.num_unable_to_validate:
        raise RuntimeError(f"Installed assets failed Data Validation: {report}")
    out = Path(unreal.Paths.project_saved_dir()) / "Validation" / "SpellbladeSecondaryMotionPromotion_20261124"
    out.mkdir(parents=True, exist_ok=True)
    (out / "InstalledAssets.json").write_text(json.dumps(report, indent=2), encoding="utf-8")


if __name__ == "__main__":
    command_line = unreal.SystemLibrary.get_command_line().lower().split()
    if "-projectjpromotesecondarymotion" in command_line:
        promote_or_verify(apply=True)
    elif "-projectjverifysecondarymotion" in command_line:
        promote_or_verify(apply=False)
    else:
        build()
