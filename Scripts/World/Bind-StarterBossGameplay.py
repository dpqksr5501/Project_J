"""Set or verify the existing B01 encounter in MainWorld's external actor package."""
import hashlib
import json
import os
import runpy
import traceback
from pathlib import Path

import unreal


SCRIPTS = Path(__file__).resolve().parent
ROOT = SCRIPTS.parents[1]
SOURCE = ROOT / 'Content/World/MainWorld/Source'
BINDING = runpy.run_path(str(SCRIPTS / 'Starter-BossGameplayContract.py'))
SAVE = runpy.run_path(str(SCRIPTS / 'Starter-OwnedSave.py'))


def read_source(filename):
    return json.loads((SOURCE / filename).read_text(encoding='utf-8'))


def package_sha256(package, extension):
    assert package.startswith('/Game/'), package
    path = ROOT / 'Content' / (package.removeprefix('/Game/') + extension)
    return hashlib.sha256(path.read_bytes()).hexdigest()


def descriptors(result):
    if isinstance(result, tuple):
        assert result[0], 'World Partition actor descriptors unavailable'
        return result[-1]
    return result


def validate_ground(world, point, npc_class):
    cdo = unreal.get_default_object(npc_class)
    mesh = cdo.get_component_by_class(unreal.SkeletalMeshComponent)
    assert mesh.get_editor_property('skeletal_mesh_asset') and mesh.get_editor_property('anim_class')
    assert cdo.get_editor_property('brain_definition').get_editor_property('is_giant_boss')
    capsule = cdo.get_component_by_class(unreal.CapsuleComponent)
    radius, height = capsule.get_scaled_capsule_radius(), capsule.get_scaled_capsule_half_height()
    floor_hit = unreal.SystemLibrary.line_trace_single_for_objects(
        world, point + unreal.Vector(0, 0, 5000), point - unreal.Vector(0, 0, 15000),
        [unreal.ObjectTypeQuery.OBJECT_TYPE_QUERY1], False, [], unreal.DrawDebugTrace.NONE, True,
    )
    assert floor_hit, 'No WorldStatic support at the B01 spawn origin'
    hit = floor_hit.to_tuple()
    assert hit[0] and hit[9] and 'Landscape' in hit[9].get_class().get_name()
    assert abs(hit[4].z - point.z) < 10 and hit[7].z > 0.71, ('Unsupported boss floor', hit[4], hit[7])
    # The runtime spawn makes the final collision decision at the supporting surface.
    center = hit[4] + unreal.Vector(0, 0, height + 15 + radius * (1 / hit[7].z - 1))
    profile = capsule.get_collision_profile_name()
    overlap = unreal.SystemLibrary.capsule_trace_single_by_profile(
        world, center, center + unreal.Vector(0, 0, 0.1), radius, height,
        profile, False, [], unreal.DrawDebugTrace.NONE, True,
    )
    assert not overlap or not overlap.to_tuple()[0], ('Boss CDO capsule is blocked', overlap)
    return dict(location_cm=list(hit[4].to_tuple()), floor_normal_z=hit[7].z,
                capsule_radius_cm=radius, capsule_half_height_cm=height, capsule_profile=str(profile))


def run(mode, report):
    assert mode in ('APPLY', 'VERIFY'), mode
    assert not unreal.EditorLevelLibrary.get_pie_worlds(False), 'Stop PIE before authoring'
    save_scope = SAVE['OwnedSaveScope']()
    manifest = read_source('MainWorld.manifest.json')
    profiles = {entry['target_id']: entry for entry in read_source(manifest['boss_profiles'])['bosses']}
    places = {entry['target_id']: entry for entry in read_source(manifest['location_registry'])['locations']}
    world_package = manifest['world_asset']
    world_path = f"{world_package}.{world_package.rsplit('/', 1)[-1]}"
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    world = unreal.EditorLevelLibrary.get_editor_world()
    if not world or world.get_path_name() != world_path:
        assert levels.load_level(world_package), world_package
    world = unreal.EditorLevelLibrary.get_editor_world()
    assert world.get_path_name() == world_path, world.get_path_name()
    assert not SAVE['dirty_packages'](), 'Unexpected dirty packages after loading MainWorld'

    partition = unreal.WorldPartitionBlueprintLibrary
    actor_paths = {target: places[target]['encounter_actor'] for target in ('B01', 'B02')}
    encounter_descriptors = [
        item for item in descriptors(partition.get_actor_descs())
        if item.get_editor_property('native_class') == unreal.Project_JWorldEncounter.static_class()
    ]
    by_path = {str(item.get_editor_property('actor_path')): item for item in encounter_descriptors}
    for path in actor_paths.values():
        assert path in by_path, ('Missing saved encounter descriptor', path)
    packages = {
        target: str(by_path[path].get_editor_property('actor_package')) for target, path in actor_paths.items()
    }
    profile = profiles['B01']
    encounter = profile['encounter']
    assert packages['B01'] == encounter['owner_package'] and packages['B01'] != packages['B02'], packages
    before = {target: package_sha256(package, '.uasset') for target, package in packages.items()}
    before['map'] = package_sha256(world_package, '.umap')
    report['before_sha256'] = before

    grid = manifest['world_grid']
    origin = unreal.Vector(*grid['unreal_origin_cm'])
    units = grid['centimetres_per_source_metre']
    center = origin + unreal.Vector(*profile['arena']['center_m']) * units
    radius = encounter['activation_radius_cm']
    assert radius >= profile['arena']['radius_m'] * units
    assert radius <= profile['arena']['reset_candidate_radius_m'] * units
    assert (profile['arena']['reset_candidate_radius_m'] +
            profile['safe_staging']['reset_candidate_edge_clearance_m']) * units > radius
    area = unreal.Box(min=center - unreal.Vector(7500, 7500, 15500),
                      max=center + unreal.Vector(7500, 7500, 5500))
    local_descriptors = descriptors(partition.get_intersecting_actor_descs(area))
    guids = [item.get_editor_property('guid') for item in encounter_descriptors]
    guids.extend(item.get_editor_property('guid') for item in local_descriptors)
    partition.load_actors(guids)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
    owners = {}
    for target, path in actor_paths.items():
        expected = profiles[target]['encounter']
        same_id = [actor for actor in actors if isinstance(actor, unreal.Project_JWorldEncounter) and
                   str(actor.get_editor_property('encounter_id')) == expected['id']]
        assert len(same_id) == 1 and same_id[0].get_path_name() == path, (target, path, same_id)
        owner = same_id[0]
        assert owner.get_outermost().get_name() == packages[target]
        assert owner.get_class().get_path_name() == expected['owner_class']
        assert owner.get_editor_property('replicates') and not owner.get_editor_property('is_spatially_loaded')
        owners[target] = owner
    b01, b02 = owners['B01'], owners['B02']
    assert not BINDING['differences'](b02, BINDING['resolve'](profiles['B02']['encounter']))
    assert (b01.get_actor_location() - center).length() < 0.01
    report['owner_location_cm'] = list(center.to_tuple())
    values = BINDING['resolve'](encounter)
    combat = values['combat_definition']
    attack = combat.get_editor_property('attack_definition_override')
    assert combat.get_editor_property('attack_ability') and attack and attack.get_editor_property('montage')
    loot = values['boss_loot_table']
    assert str(loot.get_editor_property('loot_table_id')) != 'None'
    low, high = loot.get_editor_property('min_gold'), loot.get_editor_property('max_gold')
    assert 0 <= low <= high and (high > 0 or loot.get_editor_property('entries'))
    report.update(loot_gold_range=[low, high], support=validate_ground(world, center, values['npc_class']))
    assert not SAVE['dirty_packages'](), 'Unexpected dirty packages after loading B01 actors and data'

    saved = []
    if mode == 'APPLY' and BINDING['differences'](b01, values):
        original = dict(values, npc_class=None, combat_definition=None, boss_loot_table=None,
                        quest_kill_event_id=unreal.Name('None'), activation_radius=150000)
        assert not BINDING['differences'](b01, original), 'B01 has an unexpected saved setup; inspect before updating'
        save_scope.own(b01)
        updates = BINDING['apply'](b01, values)
        saved = save_scope.save()
        assert saved == [packages['B01']], (saved, updates)
    assert not BINDING['differences'](b01, values), 'B01 saved fields differ from the production profile'
    assert not SAVE['dirty_packages'](), 'Dirty packages remained'
    after = {target: package_sha256(package, '.uasset') for target, package in packages.items()}
    after['map'] = package_sha256(world_package, '.umap')
    assert after['B02'] == before['B02'] and after['map'] == before['map']
    assert bool(saved) == (after['B01'] != before['B01'])
    report.update(passed=True, saved_packages=saved, after_sha256=after,
                  b01=BINDING['snapshot'](b01), b02=BINDING['snapshot'](b02))


if __name__ == '__main__':
    mode = os.environ.get('PROJECT_J_STARTER_BOSS_MODE', 'VERIFY').upper()
    output = Path(os.environ['PROJECT_J_STARTER_BOSS_REPORT']).resolve()
    assert output.is_relative_to(ROOT / 'Saved/Validation') and not output.exists(), output
    output.parent.mkdir(parents=True, exist_ok=True)
    result = dict(passed=False, mode=mode)
    try:
        run(mode, result)
    except Exception:
        result['error'] = traceback.format_exc()
        raise
    finally:
        output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
