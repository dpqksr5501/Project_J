"""Generate a comparison asset. Source cloak and physics asset are read-only."""

import unreal


SOURCE = "/Game/Characters/Spellblade/Meshes/SK_Spellblade_cloak"
TRIAL = "/Game/Tests/Cloth/SK_Spellblade_Cloak_PhysicsProxy"


def build():
    if unreal.EditorAssetLibrary.does_asset_exist(TRIAL):
        raise RuntimeError(f"Preserve the existing trial at {TRIAL}")
    source = unreal.load_asset(SOURCE, unreal.SkeletalMesh)
    if not source:
        raise RuntimeError("Source cloak is missing")
    target = unreal.EditorAssetLibrary.duplicate_asset(SOURCE, TRIAL)
    if not target:
        raise RuntimeError("Unable to duplicate the source cloak")
    result = unreal.Project_JPlayerLocomotionLibrary.build_surface_clothing_physics_proxy(target)
    if not result:
        raise RuntimeError("Proxy did not pass native section, anchor and reduction checks")
    if not unreal.EditorAssetLibrary.save_loaded_asset(target):
        raise RuntimeError("Unable to save the validated trial")
    unreal.log(f"PROJECT_J_CLOTH_PROXY {result}")


build()
