"""Bake the Spellblade travel curves into the sequences its combat kit plays."""

import math

import unreal as u


# Times are in source-sequence seconds, before montage or ability play rate.
# Intermediate keys set travel landmarks without stopping the character.
TRAVEL = {
    'Light.1': ((.12, 0), (.445, 18), (.63, 42)),
    'Light.2': ((.06, 0), (.39, 34), (.65, 53)),
    'Light.3': ((.09, 0), (.31, 17), (.60, 38)),
    'Heavy': ((.27, 0), (.49, 34), (.64, 60)),
    'Quick': ((.045, 0), (.108, 6.7), (.225, 16)),
    'QuickFollow': ((0, 0), (.28, 42), (.53, 59)),
    'Release': ((.48, 0), (.664, 28.1), (.815, 42)),
    'Finisher': ((1.38, 0), (1.707, 18.5), (1.975, 44)),
    'ChordLunge': ((.405, 0), (.549, 21.4), (.725, 65)),
    'SprintAttack': ((.085, 0), (.508, 162.4), (.735, 280)),
    'Air.Light': ((.02, 0), (.16, 17), (.40, 45)),
    'Air.Heavy': ((.06, 0), (.23, 32), (.52, 72)),
    'MovementDash': ((.015, 0), (.108, 349), (.221, 774), (.315, 1000)),
}


def get(obj, property_name):
    return obj.get_editor_property(property_name)


def travel_curve(keys):
    widths = [b[0] - a[0] for a, b in zip(keys, keys[1:])]
    speeds = [(b[1] - a[1]) / width for a, b, width in zip(keys, keys[1:], widths)]
    assert all(speed > 0 for speed in speeds)
    # Monotone cubic tangents. Endpoints ease in/out and internal knots share
    # a velocity so the blend between movement phases has no artificial stop.
    tangents = [0.0]
    for h0, h1, v0, v1 in zip(widths, widths[1:], speeds, speeds[1:]):
        w0, w1 = 2 * h1 + h0, h1 + 2 * h0
        tangents.append((w0 + w1) / (w0 / v0 + w1 / v1))
    tangents.append(0.0)

    def sample(time):
        if time <= keys[0][0]:
            return keys[0][1]
        for index, width in enumerate(widths):
            start, distance = keys[index]
            stop, next_distance = keys[index + 1]
            if time <= stop:
                t = (time - start) / width
                t2, t3 = t * t, t * t * t
                return (distance + (next_distance - distance) * (3 * t2 - 2 * t3)
                        + width * (tangents[index] * (t3 - 2 * t2 + t)
                                   + tangents[index + 1] * (t3 - t2)))
        return keys[-1][1]

    return sample


def resolve_routes():
    style = u.load_asset('/Game/DataAssetSets/CombatStyle/Spellblade/DA_CombatStyle_SpellbladeKit')
    combo = get(style, 'combo_definition')
    routes = {}
    prefix = 'Attack.Spellblade.'
    for node in get(combo, 'nodes'):
        attack = get(node, 'attack_definition')
        tag = str(u.GameplayTagLibrary.get_tag_name(get(attack, 'attack_tag')))
        assert tag.startswith(prefix), tag
        role = tag[len(prefix):]
        if role in TRAVEL:
            assert get(attack, 'movement_policy') == u.Project_JAttackMovementPolicy.ROOT_MOTION_MONTAGE, tag
            routes[role] = get(attack, 'montage')
    dash = u.EditorAssetLibrary.load_blueprint_class('/Game/GAs/Spellblade/GA_Spellblade_MovementDash')
    routes['MovementDash'] = get(u.get_default_object(dash), 'montage')
    return routes


def bake(roles=TRAVEL):
    player = u.EditorAssetLibrary.load_blueprint_class('/Game/Character_BPs/Spellblade/BP_Spellblade')
    mesh = u.get_default_object(player).get_component_by_class(u.SkeletalMeshComponent)
    forward = u.MathLibrary.quat_unrotate_vector(
        u.MathLibrary.conv_rotator_to_quaternion(get(mesh, 'relative_rotation')), u.Vector(1, 0, 0))
    assert abs(forward.z) < 0.001
    routes = resolve_routes()
    assert set(roles) <= set(TRAVEL) <= set(routes)
    api = u.AnimPoseExtensions
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property('optional_skeletal_mesh', mesh.get_skeletal_mesh_asset())
    options.set_editor_property('should_retarget', False)
    options.set_editor_property('extract_root_motion', False)
    space = u.AnimPoseSpaces.WORLD
    prepared = []
    for role, montage in routes.items():
        segments = [segment for slot in get(montage, 'slot_anim_tracks')
                    for segment in get(get(slot, 'anim_track'), 'anim_segments')]
        assert segments and all(get(get(segment, 'anim_reference'), 'enable_root_motion') for segment in segments), role
        if role not in roles:
            continue
        clips = {get(segment, 'anim_reference') for segment in segments}
        clip, = clips
        keys = TRAVEL[role]
        assert any(get(segment, 'anim_start_time') <= keys[0][0] < keys[-1][0] <= get(segment, 'anim_end_time')
                   for segment in segments), role
        assert not get(clip, 'force_root_lock'), role
        curve = travel_curve(keys)
        count = get(clip, 'number_of_sampled_keys')
        times = [clip.get_play_length() * i / (count - 1) for i in range(count)]
        distance = [curve(time) for time in times]
        assert all(a <= b + 0.0001 for a, b in zip(distance, distance[1:])), role
        assert math.isclose(distance[-1] - distance[0], keys[-1][1], abs_tol=0.001), role
        old_root = [api.get_bone_pose(api.get_anim_pose_at_time(clip, time, options), 'root', space) for time in times]
        translations = [forward * value + u.Vector(0, 0, old.translation.z) for value, old in zip(distance, old_root)]
        prepared.append((role, clip, translations, old_root))

    for role, clip, translations, old_root in prepared:
        controller = get(clip, 'controller')
        controller.open_bracket('Refine Spellblade planar travel', False)
        try:
            assert controller.set_bone_track_keys('root', translations, [key.rotation for key in old_root],
                                                  [key.scale3d for key in old_root], False), role
        finally:
            controller.close_bracket(False)
    for role, clip, _, _ in prepared:
        assert u.EditorAssetLibrary.save_loaded_asset(clip, only_if_is_dirty=False), role
        u.log(f'SPELLBLADE_ROOT_BAKED {role} {TRAVEL[role][-1][1]}cm')


if __name__ == '__main__':
    bake()
