"""Connect the saved MMO boss encounters to the shared NPC combat path."""
import json
import os
from pathlib import Path

import unreal


ROOT = "/Game/DataAssetSets/MMO"
MAPS = {
    "/Game/Maps/Test_MMO_Sandbox": "Sandbox_FieldBoss_01",
    "/Game/Maps/Test_MMO_Dungeon": "Dungeon_Starter_Boss",
}
SOURCE_ATTACK = "/Game/DataAssetSets/AttackDefinition/NPC/DA_Attack_NPC_PrototypeCleave"
SOURCE_ACTIVATION = "/Game/DataAssetSets/NPC/DA_NPC_PrototypeCleaveActivation"
ANIM_BP = "/Game/NPCs/Animation/ABP_NPC_Mover_Unarmed"


def validate(path):
    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    data = unreal.EditorAssetLibrary.find_asset_data(path)
    status, errors, warnings = validator.is_asset_valid(data, unreal.DataValidationUsecase.MANUAL)
    result = {"status": str(status), "errors": list(map(str, errors)), "warnings": list(map(str, warnings))}
    assert not errors and not warnings and status != unreal.DataValidationResult.INVALID, (path, result)
    return result


def load_or_duplicate(source, path):
    library = unreal.EditorAssetLibrary
    result = library.load_asset(path) if library.does_asset_exist(path) else library.duplicate_asset(source, path)
    assert result, path
    return result


def bind():
    library = unreal.EditorAssetLibrary
    animation = unreal.AnimationLibrary
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    source = unreal.load_asset(SOURCE_ATTACK)
    assert source
    source_montage = source.get_editor_property("montage")
    assert source_montage
    impacts = [animation.get_anim_notify_event_trigger_time(event)
               for event in animation.get_animation_notify_events(source_montage)
               if isinstance(event.get_editor_property("notify_state_class"), unreal.Project_JAnimNotifyState_MeleeHit)]
    assert len(impacts) == 1 and impacts[0] > 0.35, impacts

    montage = load_or_duplicate(source_montage.get_path_name(), f"{ROOT}/AM_MMOTestBoss_Cleave")
    attack = load_or_duplicate(SOURCE_ATTACK, f"{ROOT}/DA_Attack_MMOTestBoss_Cleave")
    activation = load_or_duplicate(SOURCE_ACTIVATION, f"{ROOT}/DA_NPC_MMOTestBoss_Activation")
    assert not attack.get_editor_property("melee_trajectory").get_editor_property("enabled")
    cue = unreal.GameplayTag()
    cue.import_text('(TagName="PresentationCue.Combat.BossTestCleave")')
    warning_start, warning_end = impacts[0] - 0.35, impacts[0] - 0.02
    assert unreal.Project_JAssetMigrationLibrary.add_montage_gameplay_warning(montage, cue, warning_start, warning_end)
    if attack.get_editor_property("montage") != montage:
        attack.set_editor_property("montage", montage)
    if activation.get_editor_property("attack_definition_override") != attack:
        activation.set_editor_property("attack_definition_override", attack)

    boss = unreal.load_asset(f"{ROOT}/BP_FieldBoss_Test")
    anim_class = library.load_blueprint_class(ANIM_BP)
    assert boss and anim_class
    boss_mesh = unreal.get_default_object(boss.generated_class()).get_component_by_class(unreal.SkeletalMeshComponent)
    assert boss_mesh and boss_mesh.get_editor_property("skeletal_mesh_asset")
    if boss_mesh.get_editor_property("anim_class") != anim_class:
        boss_mesh.modify()
        boss_mesh.set_editor_property("anim_class", anim_class)
        unreal.BlueprintEditorLibrary.compile_blueprint(boss)
        boss_mesh = unreal.get_default_object(boss.generated_class()).get_component_by_class(unreal.SkeletalMeshComponent)
        assert boss_mesh.get_editor_property("anim_class") == anim_class

    report = {"warning_start": warning_start, "warning_end": warning_end, "impact_start": impacts[0], "validation": {}}
    for asset in (montage, attack, activation, boss):
        path = asset.get_path_name()
        report["validation"][path] = validate(path)
        assert library.save_loaded_asset(asset), path

    for path, encounter_id in MAPS.items():
        assert levels.load_level(path), path
        candidates = [actor for actor in actors.get_all_level_actors()
                      if isinstance(actor, unreal.Project_JWorldEncounter)
                      and str(actor.get_editor_property("encounter_id")) == encounter_id]
        assert len(candidates) == 1, (path, encounter_id)
        encounter = candidates[0]
        assert not encounter.get_editor_property("use_initial_navigation")
        assert str(boss.generated_class().get_name()) in str(encounter.get_editor_property("npc_class"))
        if encounter.get_editor_property("combat_definition") != activation:
            encounter.modify()
            encounter.set_editor_property("combat_definition", activation)
            assert levels.save_current_level(), path
        report["validation"][path] = validate(path)
    return report


if __name__ == "__main__":
    result = bind()
    if output := os.getenv("PROJECT_J_MMO_BOSS_ASSET_REPORT"):
        Path(output).write_text(json.dumps(result, indent=2), encoding="utf-8")
    unreal.log(json.dumps(result))
