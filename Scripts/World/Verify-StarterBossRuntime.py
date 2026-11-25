"""Standalone PIE proof of B01 admission, combat, defeat and personal mail."""
import json
import os
import time
import traceback
from pathlib import Path

import unreal


ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Content/World/MainWorld/Source'
MANIFEST = json.loads((SOURCE / 'MainWorld.manifest.json').read_text(encoding='utf-8'))
PROFILES = {row['target_id']: row for row in json.loads(
    (SOURCE / MANIFEST['boss_profiles']).read_text(encoding='utf-8'))['bosses']}
B01 = PROFILES['B01']
ENCOUNTER = B01['encounter']
REPORT_PATH = Path(os.environ['PROJECT_J_STARTER_BOSS_REPORT']).resolve()
assert REPORT_PATH.is_relative_to(ROOT / 'Saved/Validation') and not REPORT_PATH.exists(), REPORT_PATH
REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)

LEVELS = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
BOSS_CLASS = unreal.load_class(None, ENCOUNTER['npc_class'])
LOOT = unreal.load_asset(ENCOUNTER['loot_table'])
CUE_CLASS = unreal.load_class(None, '/Script/Project_JCharacter.Project_JGameplayWarningMarkerComponent')
ATTACK = unreal.load_asset('/Game/Input/Actions/IA_Attack_LMB')
COMBAT_TOGGLE = unreal.load_asset('/Game/Input/Actions/IA_ToggleCombat')
assert all((BOSS_CLASS, LOOT, CUE_CLASS, ATTACK, COMBAT_TOGGLE))

report = dict(passed=False, world=MANIFEST['world_asset'], encounter_id=ENCOUNTER['id'],
              changes_saved=False, warning_marker=False, warning_released=False,
              attack_commands=0, health_samples=[])
state = dict(stage='startup', next_teleport=0.0, next_input=0.0, deadline=time.monotonic() + 90)


def world_point(position_m):
    grid = MANIFEST['world_grid']
    return unreal.Vector(*grid['unreal_origin_cm']) + unreal.Vector(*position_m) * grid['centimetres_per_source_metre']


def find_owners(world):
    owners = {}
    actors = unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Project_JWorldEncounter)
    for key in ('B01', 'B02'):
        event_id = PROFILES[key]['encounter']['id']
        matches = [actor for actor in actors if str(actor.get_editor_property('encounter_id')) == event_id]
        if not matches:
            return {}
        assert len(matches) == 1, (event_id, matches)
        owners[key] = matches[0]
    return owners


def phase(owner):
    return owner.get_editor_property('phase')


def teleport(world, controller, destination):
    unreal.SystemLibrary.execute_console_command(
        world, f'PJ_Teleport {destination.x} {destination.y} {destination.z}', controller)


def boss_health(boss):
    attributes = boss.get_editor_property('attribute_set')
    return attributes.get_editor_property('health').get_editor_property('current_value')


def on_inbox(entries, _cursor, success):
    rows = [{
        'mail_id': entry.get_editor_property('mail_id').to_string(),
        'subject': str(entry.get_editor_property('subject')),
        'currency': str(entry.get_editor_property('currency')),
        'amount': entry.get_editor_property('amount'),
    } for entry in entries]
    report[state['inbox_request']] = dict(success=bool(success), rows=rows)


def request_inbox(bridge, label):
    state['inbox_request'] = label
    bridge.server_request_mail_inbox(unreal.Guid())


def assert_reward_mail():
    before, after = report['inbox_before'], report['inbox_after']
    assert before['success'] and after['success'], (before, after)
    previous_ids = {entry['mail_id'] for entry in before['rows']}
    new_mail = [entry for entry in after['rows'] if entry['mail_id'] not in previous_ids]
    assert len(new_mail) == 1 and new_mail[0]['currency'] == 'GOLD', new_mail
    assert new_mail[0]['subject'] == 'MAIL_FIELDBOSS_REWARD', new_mail
    assert LOOT.get_editor_property('min_gold') <= new_mail[0]['amount'] <= LOOT.get_editor_property('max_gold'), new_mail
    report['personal_reward_mail'] = new_mail[0]


def finish():
    unreal.unregister_slate_post_tick_callback(handle)
    LEVELS.editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)
    REPORT_PATH.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')


def tick(_delta):
    try:
        assert time.monotonic() < state['deadline'], f"MainWorld boss PIE stalled in {state['stage']}"
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return
        assert len(worlds) == 1, 'Use one standalone PIE client'
        world = worlds[0]
        controller = unreal.GameplayStatics.get_player_controller(world, 0)
        avatar = unreal.GameplayStatics.get_player_pawn(world, 0)
        if not avatar:
            return
        assert avatar.has_authority() and avatar.is_locally_controlled(), 'Use a standalone local fixture'
        owners = find_owners(world)
        if not owners:
            return
        owner, b02 = owners['B01'], owners['B02']
        phases = unreal.Project_JEncounterPhase
        if phase(b02) == phases.DORMANT:
            return
        assert phase(b02) == phases.DISABLED and not b02.get_editor_property('npc_class')
        report['b02_phase'] = str(phase(b02))
        assert phase(owner) != phases.DISABLED, 'Saved B01 was disabled at runtime'
        report['b01_phase'] = str(phase(owner))
        now = unreal.GameplayStatics.get_time_seconds(world)
        stage = state['stage']
        capsule = avatar.get_component_by_class(unreal.CapsuleComponent)
        lift = unreal.Vector(0, 0, capsule.get_scaled_capsule_half_height() + 10)
        if stage == 'startup':
            if now < 1:
                return
            assert phase(owner) == phases.DORMANT
            bridge = controller.get_component_by_class(unreal.Project_JMMOClientBridgeComponent)
            assert bridge
            bridge.on_mail_inbox.add_callable_unique(on_inbox)
            state.update(stage='staging', bridge=bridge, entered=now)
            request_inbox(bridge, 'inbox_before')
        if stage in ('staging', 'arena'):
            destination = (world_point(B01['safe_staging']['center_m']) + lift if stage == 'staging'
                           else owner.get_actor_location() + unreal.Vector(0, 400, lift.z))
            distance_to_destination = (avatar.get_actor_location() - destination).length()
            if distance_to_destination > 300 and now >= state['next_teleport']:
                teleport(world, controller, destination)
                state['next_teleport'] = now + 0.7
                return
            distance_to_owner = (avatar.get_actor_location() - owner.get_actor_location()).length()
            if stage == 'staging':
                assert phase(owner) == phases.DORMANT and owner.get_editor_property('spawn_revision') == 0
                if distance_to_destination <= 300 and now - state['entered'] >= 2 and 'inbox_before' in report:
                    assert distance_to_owner > ENCOUNTER['activation_radius_cm']
                    report['staging_preserved_dormant'] = True
                    state.update(stage='arena', next_teleport=0.0)
                return
            if phase(owner) != phases.ACTIVE:
                return
            assert distance_to_owner < ENCOUNTER['activation_radius_cm']
            boss = owner.get_active_npc()
            assert boss and boss.get_class() == BOSS_CLASS and owner.get_editor_property('spawn_revision') == 1
            report['encounter_actor'] = owner.get_path_name()
            report['active_npc_actor'] = boss.get_path_name()
            report['spawn_position_cm'] = list(boss.get_actor_location().to_tuple())
            assert (boss.get_actor_location() - owner.get_actor_location()).length() < 450
            state.update(stage='combat', boss=boss, next_input=now + 0.15)
            return
        if stage == 'combat':
            if phase(owner) == phases.COOLDOWN:
                assert report['warning_marker'] and report['warning_released'] and report['attack_commands'] > 0
                assert len(report['health_samples']) >= 2 and report['health_samples'][-1] < report['health_samples'][0]
                remaining = owner.get_editor_property('next_spawn_server_time') - now
                assert ENCOUNTER['respawn_seconds'] - 3 <= remaining <= ENCOUNTER['respawn_seconds']
                report['cooldown_remaining_seconds'] = remaining
                state['stage'] = 'reward'
                request_inbox(state['bridge'], 'inbox_after')
                return
            assert phase(owner) == phases.ACTIVE
            boss = state['boss']
            current_health = boss_health(boss)
            if not report['health_samples'] or current_health != report['health_samples'][-1]:
                report['health_samples'].append(current_health)
            warning = bool(boss.get_components_by_class(CUE_CLASS))
            report['warning_marker'] |= warning
            report['warning_released'] |= report['warning_marker'] and not warning
            if now < state['next_input']:
                return
            if not avatar.is_combat_mode_active():
                assert unreal.Project_JAssetMigrationLibrary.inject_pie_input(controller, COMBAT_TOGGLE, unreal.Vector(1, 0, 0))
                state['next_input'] = now + 0.85
                return
            target, position = boss.get_actor_location(), avatar.get_actor_location()
            if not state.get('in_melee'):
                radial = boss.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_radius()
                offset = radial + capsule.get_scaled_capsule_radius() + 12
                position = target + boss.get_actor_forward_vector() * offset
                position.z = target.z - boss.get_component_by_class(unreal.CapsuleComponent).get_scaled_capsule_half_height() + lift.z
                teleport(world, controller, position)
                state.update(in_melee=True, next_input=now + 0.3)
            controller.set_control_rotation(unreal.MathLibrary.find_look_at_rotation(position, target))
            if now >= state['next_input'] and report['warning_marker']:
                report['attack_commands'] += 1
                if report['attack_commands'] % 4 == 0:
                    unreal.SystemLibrary.execute_console_command(world, 'PJ_HealFull', controller)
                assert unreal.Project_JAssetMigrationLibrary.inject_pie_input(controller, ATTACK, unreal.Vector(1, 0, 0))
                state['next_input'] = now + 1.25
            return
        if stage == 'reward' and 'inbox_after' in report:
            assert_reward_mail()
            report['passed'] = True
            finish()
    except Exception:
        report['error'] = traceback.format_exc()
        unreal.log_error(report['error'])
        finish()


assert not unreal.EditorLevelLibrary.get_pie_worlds(False), 'Start with a fresh standalone PIE session'
world = unreal.EditorLevelLibrary.get_editor_world()
if not world or world.get_path_name().split('.', 1)[0] != MANIFEST['world_asset']:
    assert LEVELS.load_level(MANIFEST['world_asset'])
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
LEVELS.editor_request_begin_play()
