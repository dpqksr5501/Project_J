"""Bind the validated Spellblade cloth proxy to the production remote-view slot."""

import unreal


MESHES = "/Game/Characters/Spellblade/Meshes/"
SOURCE = MESHES + "SK_Spellblade_cloak"
REMOTE = MESHES + "SK_Spellblade_cloak_remote"
TRIAL = "/Game/Tests/Cloth/SK_Spellblade_Cloak_PhysicsProxy"
BLUEPRINT = "/Game/Character_BPs/Spellblade/BP_Spellblade"


def publish():
    assets = unreal.EditorAssetLibrary
    source = unreal.load_asset(SOURCE, unreal.SkeletalMesh)
    blueprint = unreal.load_asset(BLUEPRINT, unreal.Blueprint)
    subobjects = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    helpers = unreal.SubobjectDataBlueprintFunctionLibrary
    parts = [obj for handle in subobjects.k2_gather_subobject_data_for_blueprint(blueprint)
             if isinstance(obj := helpers.get_object_for_blueprint(helpers.get_data(handle), blueprint),
                           unreal.Project_JModularMeshComponent)
             and obj.get_name() in ("Cloak", "Cloak_GEN_VARIABLE")]
    if len(parts) != 1 or parts[0].get_editor_property("skeletal_mesh_asset") != source:
        raise RuntimeError("The Spellblade Cloak template no longer uses the expected original mesh")

    existing = assets.does_asset_exist(REMOTE)
    remote = unreal.load_asset(REMOTE, unreal.SkeletalMesh) if existing else assets.duplicate_asset(TRIAL, REMOTE)
    validation = unreal.Project_JPlayerLocomotionLibrary.finalize_remote_surface_clothing_proxy(source, remote)
    if not validation:
        raise RuntimeError("The remote cloak failed render LOD, cloth binding or body collision validation")
    if not assets.save_loaded_asset(remote, only_if_is_dirty=existing):
        raise RuntimeError("Unable to save the production remote cloak")

    cloak = parts[0]
    binding = cloak.get_editor_property("remote_presentation_mesh")
    if binding and binding != remote:
        raise RuntimeError("The remote slot already has a different binding")
    if not binding:
        blueprint.modify()
        cloak.modify()
        cloak.set_editor_property("remote_presentation_mesh", remote)
        unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
        if not assets.save_loaded_asset(blueprint, only_if_is_dirty=False):
            raise RuntimeError("Unable to save the Spellblade Blueprint")
    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    for path in (REMOTE, BLUEPRINT):
        status, errors, warnings = validator.is_asset_valid(assets.find_asset_data(path), unreal.DataValidationUsecase.MANUAL)
        unreal.log(f"PROJECT_J_REMOTE_CLOTH_VALIDATION {path} {status} errors={list(map(str, errors))} "
                   f"warnings={list(map(str, warnings))}")
        if status == unreal.DataValidationResult.INVALID or errors:
            raise RuntimeError(f"Data Validation failed for {path}")
    unreal.log(f"PROJECT_J_REMOTE_CLOTH {validation}")


publish()
