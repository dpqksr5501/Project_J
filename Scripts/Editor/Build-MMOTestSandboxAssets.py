"""Create playable MMO Test Blueprints, Test UI WBP, and Graybox Test Levels (Sandbox + Dungeon Boss Arena)."""
import unreal


def ensure_dir(path: str):
    if not unreal.EditorAssetLibrary.does_directory_exist(path):
        unreal.EditorAssetLibrary.make_directory(path)


def create_or_load_bp(package_path: str, asset_name: str, parent_class):
    full_path = f"{package_path}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.EditorAssetLibrary.load_asset(full_path)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.BlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return asset_tools.create_asset(asset_name, package_path, unreal.Blueprint, factory)


def create_or_load_wbp(package_path: str, asset_name: str, parent_class):
    full_path = f"{package_path}/{asset_name}"
    if unreal.EditorAssetLibrary.does_asset_exist(full_path):
        return unreal.EditorAssetLibrary.load_asset(full_path)
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property("parent_class", parent_class)
    return asset_tools.create_asset(asset_name, package_path, unreal.WidgetBlueprint, factory)


def compile_and_save(bp):
    if not bp:
        return
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    except Exception as ex:
        unreal.log_warning(f"[Project_J] compile_blueprint note for {bp.get_path_name()}: {ex}")
    unreal.EditorAssetLibrary.save_loaded_asset(bp)
    unreal.log(f"[Project_J] Saved {bp.get_path_name()}")


def get_cdo(bp):
    gen_cls = bp.generated_class()
    return unreal.get_default_object(gen_cls) if gen_cls else None


def configure_boss_presentation(bp, cdo):
    """Use the project's existing non-motion-matching preview mesh and authored hit parts."""
    changed = False
    mesh = cdo.get_component_by_class(unreal.SkeletalMeshComponent)
    if mesh and not mesh.get_editor_property("skeletal_mesh_asset"):
        mannequin = unreal.EditorAssetLibrary.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
        assert mannequin
        mesh.set_skeletal_mesh_asset(mannequin)
        changed = True
    component_class = unreal.load_class(None, "/Script/Project_JCharacter.Project_JMultiPartHitComponent")
    assert component_class
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    helpers = unreal.SubobjectDataBlueprintFunctionLibrary
    handles = subsystem.k2_gather_subobject_data_for_blueprint(bp)
    component = next((obj for handle in handles
                      if (obj := helpers.get_object_for_blueprint(helpers.get_data(handle), bp))
                      and isinstance(obj, unreal.Project_JMultiPartHitComponent)), None)
    if not component:
        handle, reason = subsystem.add_new_subobject(unreal.AddNewSubobjectParams(
            parent_handle=handles[0], new_class=component_class, blueprint_context=bp))
        assert reason.is_empty(), str(reason)
        component = helpers.get_object_for_blueprint(helpers.get_data(handle), bp)
        assert component
        changed = True
    if not component.get_editor_property("hit_parts"):
        body = unreal.Project_JBossHitPartSpec(
            part_name="Body", socket_or_bone_name="spine_03", sphere_radius=90, damage_multiplier=1)
        head = unreal.Project_JBossHitPartSpec(
            part_name="Head", socket_or_bone_name="head", sphere_radius=45, damage_multiplier=1.5, is_weakpoint=True)
        component.set_editor_property("hit_parts", [body, head])
        changed = True
    return changed


def spawn_cube(location, scale, label, mesh, actor_sub):
    actor = actor_sub.spawn_actor_from_class(unreal.StaticMeshActor, location, unreal.Rotator(0, 0, 0))
    if actor:
        actor.set_actor_label(label)
        actor.set_actor_scale3d(scale)
        smc = actor.static_mesh_component
        if smc and mesh:
            smc.set_static_mesh(mesh)
            smc.set_mobility(unreal.ComponentMobility.STATIC)
    return actor


def add_basic_lighting(actor_sub):
    sun = actor_sub.spawn_actor_from_class(unreal.DirectionalLight, unreal.Vector(0, 0, 500), unreal.Rotator(-45, 35, 0))
    if sun:
        sun.set_actor_label("TestSunLight")
    sky = actor_sub.spawn_actor_from_class(unreal.SkyLight, unreal.Vector(0, 0, 600), unreal.Rotator(0, 0, 0))
    if sky:
        sky.set_actor_label("TestSkyLight")


def configure_world_gamemode(level_sub, gamemode_cls):
    world = unreal.EditorLevelLibrary.get_editor_world()
    if world and gamemode_cls:
        ws = world.get_world_settings()
        if ws:
            ws.set_editor_property("default_game_mode", gamemode_cls)


def main():
    base_dir = "/Game/DataAssetSets/MMO"
    maps_dir = "/Game/Maps"
    ensure_dir(base_dir)
    ensure_dir(maps_dir)

    cube_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cube")
    sphere_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Sphere")
    cylinder_mesh = unreal.EditorAssetLibrary.load_asset("/Engine/BasicShapes/Cylinder")

    da_brain = unreal.EditorAssetLibrary.load_asset(f"{base_dir}/DA_NPCBrain_FieldBoss_Starter")
    da_loot = unreal.EditorAssetLibrary.load_asset(f"{base_dir}/DA_LootTable_FieldBoss_Starter")
    da_dungeon = unreal.EditorAssetLibrary.load_asset(f"{base_dir}/DA_Dungeon_Starter")

    # 1. Update BP_MountMover_Starter: enable bCanFly=True (fixes flight testing out-of-the-box)
    bp_mount = unreal.EditorAssetLibrary.load_asset(f"{base_dir}/BP_MountMover_Starter")
    if bp_mount:
        cdo = get_cdo(bp_mount)
        if cdo:
            mover = cdo.get_mount_mover()
            if mover:
                profile = mover.get_editor_property("motion_profile")
                profile.set_editor_property("can_fly", True)
                mover.set_editor_property("motion_profile", profile)
                unreal.log("[Project_J] Enabled MotionProfile.bCanFly=True on BP_MountMover_Starter")
        compile_and_save(bp_mount)

    # 2. BP_FieldBoss_Test (AProject_JNPCCharacter + Giant Boss Brain + larger capsule)
    npc_cls = unreal.load_class(None, "/Script/Project_JCharacter.Project_JNPCCharacter")
    bp_boss = create_or_load_bp(base_dir, "BP_FieldBoss_Test", npc_cls) if npc_cls else None
    if bp_boss:
        cdo = get_cdo(bp_boss)
        if cdo:
            if da_brain:
                cdo.set_editor_property("brain_definition", da_brain)
            cdo.set_editor_property("quest_kill_event_id", "Kill_FieldBoss_Test")
            capsule = cdo.get_component_by_class(unreal.CapsuleComponent)
            if capsule:
                capsule.set_capsule_size(110.0, 220.0, False)
            configure_boss_presentation(bp_boss, cdo)
        compile_and_save(bp_boss)

    # 3. BP_GatherNode_Mining_Test & BP_GatherNode_Fishing_Test
    gather_cls = unreal.load_class(None, "/Script/Project_J.Project_JGatheringNode")
    activity_enum = unreal.Project_JLifeSkillActivity
    mode_enum = unreal.Project_JResourceNodeMode

    bp_mining = create_or_load_bp(base_dir, "BP_GatherNode_Mining_Test", gather_cls) if gather_cls else None
    if bp_mining:
        cdo = get_cdo(bp_mining)
        if cdo:
            cdo.set_editor_property("activity_type", activity_enum.MINING)
            cdo.set_editor_property("use_personal_phase_life_skill", True)
            cdo.set_editor_property("resource_node_mode", mode_enum.PERSONAL_PHASE)
            cdo.set_editor_property("required_tool_tier", 1)
            cdo.set_editor_property("yield_item_id", "HealthPotion")
            cdo.set_editor_property("cooldown_seconds", 10.0)
            cdo.set_editor_property("quest_gather_event_id", "Gather_Mining_Test")
            vis = cdo.get_editor_property("node_visual")
            if vis and sphere_mesh:
                vis.set_static_mesh(sphere_mesh)
        compile_and_save(bp_mining)

    bp_fishing = create_or_load_bp(base_dir, "BP_GatherNode_Fishing_Test", gather_cls) if gather_cls else None
    if bp_fishing:
        cdo = get_cdo(bp_fishing)
        if cdo:
            cdo.set_editor_property("activity_type", activity_enum.FISHING)
            cdo.set_editor_property("use_personal_phase_life_skill", True)
            cdo.set_editor_property("resource_node_mode", mode_enum.PERSONAL_PHASE)
            cdo.set_editor_property("required_tool_tier", 1)
            cdo.set_editor_property("yield_item_id", "HealthPotion")
            cdo.set_editor_property("cooldown_seconds", 8.0)
            cdo.set_editor_property("quest_gather_event_id", "Gather_Fishing_Test")
            vis = cdo.get_editor_property("node_visual")
            if vis and cylinder_mesh:
                vis.set_static_mesh(cylinder_mesh)
        compile_and_save(bp_fishing)

    # 4. BP_DungeonPortal_Test (AProject_JWorldPortal -> DA_Dungeon_Starter)
    portal_cls = unreal.load_class(None, "/Script/Project_J.Project_JWorldPortal")
    bp_portal = create_or_load_bp(base_dir, "BP_DungeonPortal_Test", portal_cls) if portal_cls else None
    if bp_portal:
        cdo = get_cdo(bp_portal)
        if cdo:
            if da_dungeon:
                cdo.set_editor_property("dungeon_definition", da_dungeon)
            cdo.set_editor_property("dungeon_id", "Dungeon_Starter")
            cdo.set_editor_property("auto_travel_on_ticket", True)
        compile_and_save(bp_portal)

    # 5. WBP_MMOTestHarness (UProject_JMMOTestHarnessWidget)
    harness_cls = unreal.load_class(None, "/Script/Project_J.Project_JMMOTestHarnessWidget")
    if harness_cls:
        wbp = create_or_load_wbp(base_dir, "WBP_MMOTestHarness", harness_cls)
        compile_and_save(wbp)

    # Subsystems for level creation
    level_sub = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_sub = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    gamemode_cls = unreal.load_class(None, "/Game/ThirdPerson/Blueprints/BP_Project_JGameMode.BP_Project_JGameMode_C")
    encounter_cls = unreal.load_class(None, "/Script/Project_J.Project_JWorldEncounter")
    craft_cls = unreal.load_class(None, "/Script/Project_J.Project_JCraftingStation")

    # 6. /Game/Maps/Test_MMO_Dungeon (Playable Graybox Dungeon Boss Arena)
    dungeon_map_path = f"{maps_dir}/Test_MMO_Dungeon"
    if level_sub.new_level(dungeon_map_path):
        configure_world_gamemode(level_sub, gamemode_cls)
        add_basic_lighting(actor_sub)
        spawn_cube(unreal.Vector(0, 0, -50), unreal.Vector(40, 40, 1), "ArenaFloor", cube_mesh, actor_sub)
        spawn_cube(unreal.Vector(2000, 0, 250), unreal.Vector(1, 40, 6), "WallNorth", cube_mesh, actor_sub)
        spawn_cube(unreal.Vector(-2000, 0, 250), unreal.Vector(1, 40, 6), "WallSouth", cube_mesh, actor_sub)
        spawn_cube(unreal.Vector(0, 2000, 250), unreal.Vector(40, 1, 6), "WallEast", cube_mesh, actor_sub)
        spawn_cube(unreal.Vector(0, -2000, 250), unreal.Vector(40, 1, 6), "WallWest", cube_mesh, actor_sub)

        ps = actor_sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(-1400, 0, 120), unreal.Rotator(0, 0, 0))
        if ps:
            ps.set_actor_label("DungeonPlayerStart")

        if encounter_cls:
            enc = actor_sub.spawn_actor_from_class(encounter_cls, unreal.Vector(1100, 0, 100), unreal.Rotator(0, 180, 0))
            if enc:
                enc.set_actor_label("DungeonFinalBossEncounter")
                enc.set_editor_property("encounter_id", "Dungeon_Starter_Boss")
                if bp_boss and bp_boss.generated_class():
                    enc.set_editor_property("npc_class", bp_boss.generated_class())
                enc.set_editor_property("trigger_dungeon_clear_on_defeat", True)
                enc.set_editor_property("dungeon_definition", da_dungeon)
                enc.set_editor_property("enable_boss_camera_zoom_out", True)
                enc.set_editor_property("activation_radius", 5000.0)
                enc.set_editor_property("respawn_seconds", 60.0)

        level_sub.save_current_level()
        unreal.log(f"[Project_J] Saved level {dungeon_map_path}")

        # Wire DungeonMap & Clear rewards into DA_Dungeon_Starter
        if da_dungeon:
            dungeon_world = unreal.EditorAssetLibrary.load_asset(dungeon_map_path)
            if dungeon_world:
                da_dungeon.set_editor_property("dungeon_map", dungeon_world)
            if da_loot:
                da_dungeon.set_editor_property("clear_loot_table", da_loot)
            da_dungeon.set_editor_property("quest_clear_event_id", "Clear_Dungeon_Starter")
            unreal.EditorAssetLibrary.save_loaded_asset(da_dungeon)
            unreal.log("[Project_J] Linked Test_MMO_Dungeon into DA_Dungeon_Starter")

    # 7. /Game/Maps/Test_MMO_Sandbox (Playable Open-Field MMO Sandbox)
    sandbox_map_path = f"{maps_dir}/Test_MMO_Sandbox"
    if level_sub.new_level(sandbox_map_path):
        configure_world_gamemode(level_sub, gamemode_cls)
        add_basic_lighting(actor_sub)
        spawn_cube(unreal.Vector(0, 0, -50), unreal.Vector(140, 140, 1), "SandboxGround", cube_mesh, actor_sub)

        ps = actor_sub.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 120), unreal.Rotator(0, 0, 0))
        if ps:
            ps.set_actor_label("SandboxPlayerStart")

        if bp_mount and bp_mount.generated_class():
            m = actor_sub.spawn_actor_from_class(bp_mount.generated_class(), unreal.Vector(350, 250, 120), unreal.Rotator(0, 0, 0))
            if m:
                m.set_actor_label("TestFlyingMount")

        if bp_portal and bp_portal.generated_class():
            p = actor_sub.spawn_actor_from_class(bp_portal.generated_class(), unreal.Vector(600, -350, 100), unreal.Rotator(0, 0, 0))
            if p:
                p.set_actor_label("TestDungeonPortal")

        if bp_mining and bp_mining.generated_class():
            n1 = actor_sub.spawn_actor_from_class(bp_mining.generated_class(), unreal.Vector(500, 500, 50), unreal.Rotator(0, 0, 0))
            if n1:
                n1.set_actor_label("TestMiningNode")
                n1.set_editor_property("node_id", "Sandbox_Mining_01")

        if bp_fishing and bp_fishing.generated_class():
            n2 = actor_sub.spawn_actor_from_class(bp_fishing.generated_class(), unreal.Vector(800, 500, 50), unreal.Rotator(0, 0, 0))
            if n2:
                n2.set_actor_label("TestFishingSpot")
                n2.set_editor_property("node_id", "Sandbox_Fishing_01")

        if craft_cls:
            c = actor_sub.spawn_actor_from_class(craft_cls, unreal.Vector(250, -450, 50), unreal.Rotator(0, 0, 0))
            if c:
                c.set_actor_label("TestCraftingBench")
                recipe = unreal.EditorAssetLibrary.load_asset("/Game/DataAssetSets/Crafting/DA_PrototypeHealthToMana")
                if recipe:
                    c.set_editor_property("recipe_id", recipe.get_editor_property("recipe_id"))
                    c.set_editor_property("recipe_version", recipe.get_editor_property("schema_version"))

        if encounter_cls:
            enc = actor_sub.spawn_actor_from_class(encounter_cls, unreal.Vector(3200, 0, 100), unreal.Rotator(0, 180, 0))
            if enc:
                enc.set_actor_label("SandboxFieldBossEncounter")
                enc.set_editor_property("encounter_id", "Sandbox_FieldBoss_01")
                if bp_boss and bp_boss.generated_class():
                    enc.set_editor_property("npc_class", bp_boss.generated_class())
                if da_loot:
                    enc.set_editor_property("boss_loot_table", da_loot)
                enc.set_editor_property("enable_boss_camera_zoom_out", True)
                enc.set_editor_property("activation_radius", 8000.0)
                enc.set_editor_property("respawn_seconds", 30.0)

        level_sub.save_current_level()
        unreal.log(f"[Project_J] Saved level {sandbox_map_path}")

    unreal.log("[Project_J] MMO Test Sandbox assets and levels generation complete.")


if __name__ == "__main__":
    main()
