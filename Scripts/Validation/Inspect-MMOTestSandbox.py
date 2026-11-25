"""Validate the saved sandbox packages and read the wiring of both test maps."""
import json
import os
import gc
from pathlib import Path
import runpy

import unreal


ROOT = "/Game/DataAssetSets/MMO"
WHOLE_MAPS = ["/Game/Maps/Test_MMO_Sandbox", "/Game/Maps/Test_MMO_Dungeon"]
REPAIR = os.getenv("PROJECT_J_MMO_REPAIR_TEST_ASSETS") == "1"
AUTHOR = runpy.run_path(str(Path(__file__).resolve().parents[1] / "Editor" / "Build-MMOTestSandboxAssets.py")) if REPAIR else {}
PACKAGES = [
    f"{ROOT}/{name}" for name in (
        "BP_MountMover_Starter", "BP_FieldBoss_Test", "BP_GatherNode_Mining_Test",
        "BP_GatherNode_Fishing_Test", "BP_DungeonPortal_Test", "WBP_MMOTestHarness",
        "DA_Dungeon_Starter",
        "DA_NPCBrain_FieldBoss_Starter",
    )
] + [
    "/Game/NPCs/BP_NPC_Test",
    "/Game/NPCs/Animation/ABP_NPC_Mover_Unarmed",
    "/Game/Mount_Assets/Wyvern/BP_Wyvern",
] + WHOLE_MAPS


def read(obj, key):
    try:
        value = obj.get_editor_property(key)
        return value.get_path_name() if isinstance(value, unreal.Object) else str(value)
    except Exception as exc:
        return str(exc)


def inspect_map(path, result):
    editor = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    if not levels.load_level(path):
        result["load_failed"] = True
        return
    world = editor.get_editor_world()
    settings = world.get_world_settings()
    repair_map = False
    if REPAIR and read(settings, "default_game_mode") in ("None", "/Script/Project_J.Project_JGameMode"):
        mode = unreal.load_class(None, "/Game/ThirdPerson/Blueprints/BP_Project_JGameMode.BP_Project_JGameMode_C")
        assert mode
        settings.set_editor_property("default_game_mode", mode)
        repair_map = True
    result["game_mode"] = read(world.get_world_settings(), "default_game_mode")
    result["actors"] = {}
    for actor in actors.get_all_level_actors():
        name = actor.get_actor_label()
        if not (name.startswith(("Test", "Sandbox", "Dungeon")) or "Boss" in name):
            continue
        row = {"class": actor.get_class().get_path_name()}
        if REPAIR and name == "DungeonFinalBossEncounter":
            definition = unreal.EditorAssetLibrary.load_asset(f"{ROOT}/DA_Dungeon_Starter")
            assert definition
            if actor.get_editor_property("dungeon_definition") != definition:
                actor.set_editor_property("dungeon_definition", definition)
                repair_map = True
            if actor.get_editor_property("boss_loot_table") == definition.get_editor_property("clear_loot_table"):
                actor.set_editor_property("boss_loot_table", None)
                repair_map = True
        if REPAIR and name == "TestCraftingBench" and str(actor.get_editor_property("recipe_id")) == "None":
            recipe = unreal.EditorAssetLibrary.load_asset("/Game/DataAssetSets/Crafting/DA_PrototypeHealthToMana")
            assert recipe
            actor.set_editor_property("recipe_id", recipe.get_editor_property("recipe_id"))
            actor.set_editor_property("recipe_version", recipe.get_editor_property("schema_version"))
            repair_map = True
        if "Encounter" in name:
            row.update({key: read(actor, key) for key in (
                "encounter_id", "npc_class", "combat_definition", "boss_loot_table",
                "trigger_dungeon_clear_on_defeat", "dungeon_definition",
            )})
            activation = actor.get_editor_property("combat_definition")
            if isinstance(activation, unreal.Project_JNPCActivationDefinition):
                attack = activation.get_editor_property("attack_definition_override")
                ability = activation.get_editor_property("attack_ability")
                if not attack and ability:
                    attack = unreal.get_default_object(ability).get_editor_property("attack_definition")
                montage = attack.get_editor_property("montage") if attack else None
                row["attack_montage"] = montage.get_path_name() if montage else None
                row["authored_warning_cues"] = [
                    read(notify, "cue_tag")
                    for event in unreal.AnimationLibrary.get_animation_notify_events(montage)
                    if isinstance(notify := event.get_editor_property("notify_state_class"),
                                  unreal.Project_JAnimNotifyState_CombatPresentationCue)
                    and "GAMEPLAY_WARNING" in read(notify, "purpose").upper()
                ] if montage else []
        if "Crafting" in name:
            row.update({key: read(actor, key) for key in ("recipe_id", "recipe_version", "batches")})
        if "Mining" in name or "Fishing" in name:
            row.update({key: read(actor, key) for key in ("node_id", "yield_item_id", "yield_count")})
        result["actors"][name] = row
    if repair_map:
        assert levels.save_current_level()
    result["repaired"] = repair_map


def save(results):
    with open(os.environ["PROJECT_J_MMO_ASSET_REPORT"], "w", encoding="utf-8") as report:
        json.dump(results, report, indent=2, ensure_ascii=False)


def main():
    library = unreal.EditorAssetLibrary
    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    results = {}
    fields = {
        "DA_Dungeon_Starter": ["dungeon_id", "dungeon_map", "clear_loot_table", "quest_clear_event_id"],
        "BP_FieldBoss_Test": ["brain_definition", "quest_kill_event_id"],
        "BP_GatherNode_Mining_Test": ["node_id", "activity_type", "yield_item_id", "yield_count"],
        "BP_GatherNode_Fishing_Test": ["node_id", "activity_type", "yield_item_id", "yield_count"],
        "BP_DungeonPortal_Test": ["dungeon_id", "dungeon_definition", "destination_place_id"],
    }
    for path in PACKAGES:
        if path in WHOLE_MAPS:
            continue
        asset = library.load_asset(path)
        if not asset:
            results[path] = {"missing": True}
            continue
        name = path.rsplit("/", 1)[-1]
        target = unreal.get_default_object(asset.generated_class()) if hasattr(asset, "generated_class") and asset.generated_class() else asset
        repaired = False
        if REPAIR and name.startswith("BP_GatherNode") and str(target.get_editor_property("yield_item_id")) == "None":
            target.set_editor_property("yield_item_id", "HealthPotion")
            repaired = True
        if REPAIR and name == "BP_FieldBoss_Test":
            repaired = AUTHOR["configure_boss_presentation"](asset, target)
        if repaired:
            unreal.BlueprintEditorLibrary.compile_blueprint(asset)
            assert library.save_loaded_asset(asset)
            target = unreal.get_default_object(asset.generated_class())
        status, errors, warnings = validator.is_asset_valid(library.find_asset_data(path), unreal.DataValidationUsecase.MANUAL)
        result = {"result": str(status), "errors": list(map(str, errors)), "warnings": list(map(str, warnings))}
        results[path] = result
        result["repaired"] = repaired
        result["fields"] = {key: read(target, key) for key in fields.get(name, [])}
        if name == "BP_MountMover_Starter":
            mover = target.get_mount_mover()
            profile = mover.get_editor_property("motion_profile") if mover else None
            result["can_fly"] = read(profile, "can_fly") if profile else None
        if name in ("BP_FieldBoss_Test", "BP_NPC_Test"):
            mesh = target.get_component_by_class(unreal.SkeletalMeshComponent)
            result["mesh"] = read(mesh, "skeletal_mesh_asset") if mesh else None
            result["anim_class"] = read(mesh, "anim_class") if mesh else None
            subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
            helpers = unreal.SubobjectDataBlueprintFunctionLibrary
            parts = next((obj for handle in subsystem.k2_gather_subobject_data_for_blueprint(asset)
                          if (obj := helpers.get_object_for_blueprint(helpers.get_data(handle), asset))
                          and isinstance(obj, unreal.Project_JMultiPartHitComponent)), None)
            result["hit_parts"] = len(parts.get_editor_property("hit_parts")) if parts else 0

    del asset, target
    save(results)
    for path in WHOLE_MAPS:
        gc.collect()
        result = results[path] = {}
        inspect_map(path, result)
        status, errors, warnings = validator.is_asset_valid(library.find_asset_data(path), unreal.DataValidationUsecase.MANUAL)
        result.update(result=str(status), errors=list(map(str, errors)), warnings=list(map(str, warnings)))
        save(results)


main()
