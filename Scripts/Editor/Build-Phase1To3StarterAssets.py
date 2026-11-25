"""Create starter Phase 1-3 DataAssets, Blueprint, and HLOD policy assets in Content/DataAssetSets/MMO/."""
import unreal


def ensure_dir(path: str):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def create_or_load_data_asset(package_path: str, asset_name: str, asset_class):
    full_path = f"{package_path}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.EditorAssetLibrary.load_asset(full_path)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", asset_class)
    return asset_tools.create_asset(asset_name, package_path, asset_class, factory)


def main():
    base_dir = "/Game/DataAssetSets/MMO"
    ensure_dir(base_dir)

    # 1. DA_WorldStreamingPolicy_Default
    stream_cls = unreal.load_class(None, "/Script/Project_J.Project_JWorldStreamingPolicyAsset")
    if stream_cls:
        asset = create_or_load_data_asset(base_dir, "DA_WorldStreamingPolicy_Default", stream_cls)
        if asset:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
            unreal.log(f"[Project_J] Saved {asset.get_path_name()}")

    # 2. DA_CameraPolicy_Default
    cam_cls = unreal.load_class(None, "/Script/Project_JCharacter.Project_JCameraPolicyAsset")
    if cam_cls:
        asset = create_or_load_data_asset(base_dir, "DA_CameraPolicy_Default", cam_cls)
        if asset:
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
            unreal.log(f"[Project_J] Saved {asset.get_path_name()}")

    # 3. DA_LootTable_FieldBoss_Starter
    loot_cls = unreal.load_class(None, "/Script/Project_J.Project_JLootTableDefinition")
    if loot_cls:
        asset = create_or_load_data_asset(base_dir, "DA_LootTable_FieldBoss_Starter", loot_cls)
        if asset:
            asset.set_editor_property("loot_table_id", "FieldBoss_Starter")
            asset.set_editor_property("min_gold", 1500)
            asset.set_editor_property("max_gold", 3000)
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
            unreal.log(f"[Project_J] Saved {asset.get_path_name()}")

    # 4. DA_NPCBrain_FieldBoss_Starter
    brain_cls = unreal.load_class(None, "/Script/Project_JCharacter.Project_JNPCBrainDefinition")
    if brain_cls:
        asset = create_or_load_data_asset(base_dir, "DA_NPCBrain_FieldBoss_Starter", brain_cls)
        if asset:
            asset.set_editor_property("brain_id", "FieldBoss_Starter")
            asset.set_editor_property("is_giant_boss", True)
            asset.set_editor_property("sight_radius_cm", 4500.0)
            asset.set_editor_property("leash_radius_cm", 8000.0)
            asset.set_editor_property("max_chase_z_delta_cm", 2000.0)
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
            unreal.log(f"[Project_J] Saved {asset.get_path_name()}")

    # 5. HLODLayer_Foliage_Impostor (Instanced HLOD layer for 8x8km trees, preventing Embree OOM)
    hlod_dir = "/Game/World/MainWorld/HLOD"
    ensure_dir(hlod_dir)
    hlod_path = f"{hlod_dir}/HLODLayer_Foliage_Impostor"
    if not unreal.EditorAssetLibrary.does_asset_exist(hlod_path):
        hlod_cls = unreal.HLODLayer
        asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
        hlod = asset_tools.create_asset("HLODLayer_Foliage_Impostor", hlod_dir, hlod_cls, None)
        if hlod:
            hlod.set_editor_property("layer_type", unreal.HLODLayerType.INSTANCING)
            hlod.set_editor_property("is_spatially_loaded", True)
            unreal.EditorAssetLibrary.save_loaded_asset(hlod)
            unreal.log(f"[Project_J] Saved {hlod.get_path_name()} (Type=Instancing)")
    else:
        unreal.log(f"[Project_J] Existing {hlod_path} preserved")

    # 6. BP_MountMover_Starter (Blueprint child of AProject_JMountMoverPawn)
    bp_path = f"{base_dir}/BP_MountMover_Starter"
    if not unreal.EditorAssetLibrary.does_asset_exist(bp_path):
        mount_cls = unreal.load_class(None, "/Script/Project_JMount.Project_JMountMoverPawn")
        if mount_cls:
            asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
            bp_factory = unreal.BlueprintFactory()
            bp_factory.set_editor_property("parent_class", mount_cls)
            bp = asset_tools.create_asset("BP_MountMover_Starter", base_dir, unreal.Blueprint, bp_factory)
            if bp:
                unreal.EditorAssetLibrary.save_loaded_asset(bp)
                unreal.log(f"[Project_J] Saved {bp.get_path_name()}")

    # 7. DA_Dungeon_Starter (UProject_JDungeonDefinition)
    dungeon_cls = unreal.load_class(None, "/Script/Project_J.Project_JDungeonDefinition")
    if dungeon_cls:
        asset = create_or_load_data_asset(base_dir, "DA_Dungeon_Starter", dungeon_cls)
        if asset:
            asset.set_editor_property("dungeon_id", "Dungeon_Starter")
            asset.set_editor_property("min_party_size", 1)
            asset.set_editor_property("max_party_size", 4)
            asset.set_editor_property("time_limit_seconds", 1800.0)
            unreal.EditorAssetLibrary.save_loaded_asset(asset)
            unreal.log(f"[Project_J] Saved {asset.get_path_name()}")

    unreal.log("[Project_J] Phase 1-3 starter assets generation complete.")


if __name__ == "__main__":
    main()
