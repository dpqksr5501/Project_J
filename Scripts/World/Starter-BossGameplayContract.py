"""Shared source-to-actor binding for starter world encounter authoring checks."""
import unreal


def asset_path(value):
    return value.get_path_name() if value else None


def resolve(encounter):
    paths = {
        'npc_class': encounter['npc_class'],
        'combat_definition': encounter.get('combat_definition'),
        'boss_loot_table': encounter.get('loot_table'),
    }
    assert all(paths.values()) or not any(paths.values()), paths
    assets = {
        name: (unreal.load_class(None, path) if name == 'npc_class' else unreal.load_asset(path)) if path else None
        for name, path in paths.items()
    }
    assert {name: asset_path(asset) for name, asset in assets.items()} == paths, paths
    values = dict(
        encounter_id=unreal.Name(encounter['id']),
        activation_radius=encounter['activation_radius_cm'],
        respawn_seconds=encounter['respawn_seconds'],
        combat_team_id=encounter.get('combat_team_id', 1),
        quest_kill_event_id=unreal.Name(encounter.get('quest_kill_event_id', 'None')),
        use_initial_navigation=False,
        trigger_dungeon_clear_on_defeat=False,
        use_schedule=False,
        **assets,
    )
    return values


def differences(actor, values):
    return {name: value for name, value in values.items() if actor.get_editor_property(name) != value}


def apply(actor, values):
    updates = differences(actor, values)
    if updates:
        actor.modify()
        for name, value in updates.items():
            actor.set_editor_property(name, value)
    assert not differences(actor, values), actor.get_path_name()
    return sorted(updates)


def snapshot(actor):
    return dict(
        path=actor.get_path_name(),
        package=actor.get_outermost().get_name(),
        encounter_id=str(actor.get_editor_property('encounter_id')),
        npc_class=asset_path(actor.get_editor_property('npc_class')),
        combat_definition=asset_path(actor.get_editor_property('combat_definition')),
        loot_table=asset_path(actor.get_editor_property('boss_loot_table')),
        combat_team_id=actor.get_editor_property('combat_team_id'),
        quest_kill_event_id=str(actor.get_editor_property('quest_kill_event_id')),
        activation_radius_cm=actor.get_editor_property('activation_radius'),
        respawn_seconds=actor.get_editor_property('respawn_seconds'),
        replicates=actor.get_editor_property('replicates'),
        is_spatially_loaded=actor.get_editor_property('is_spatially_loaded'),
        source_class=actor.get_class().get_path_name(),
    )
