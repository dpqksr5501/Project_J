"""Trim inactive montage tails without changing damage, combos, or root travel."""

from pathlib import Path
import runpy

import unreal as u


# End time in montage seconds and blend-out duration. Keep the authored short
# recoveries that already leave enough time for their late combo windows.
RECOVERY = {
    'Heavy': (.99, .14),
    'QuickFollow': (.90, .13),
    'SprintAttack': (1.20, .17),
    'Release': (1.28, .17),
    'Finisher': (3.04, .20),
    'Air.Light': (.94, .16),
    'Air.Heavy': (.96, .16),
}


def apply():
    helper = runpy.run_path(str(Path(__file__).with_name('Refine-SpellbladeRootMotion.py')))
    get, routes = helper['get'], helper['resolve_routes']()
    travel = helper['TRAVEL']
    anim = u.AnimationLibrary
    prepared = []
    for role, (end, blend_time) in RECOVERY.items():
        montage = routes[role]
        slot, = get(montage, 'slot_anim_tracks')
        track = get(slot, 'anim_track')
        segments = get(track, 'anim_segments')
        events = anim.get_animation_notify_events(montage)
        last_event = max(anim.get_anim_notify_event_trigger_time(event)
                         + anim.get_anim_notify_event_duration(event) for event in events)
        # The fade must not overlap hit/VFX windows or shorten root extraction.
        fade_start = end - blend_time
        assert fade_start > last_event + .01, (role, last_event, fade_start)
        move_start, move_end = travel[role][0][0], travel[role][-1][0]
        root_end = None
        kept = []
        for segment in segments:
            start = get(segment, 'start_pos')
            source_start = get(segment, 'anim_start_time')
            source_end = get(segment, 'anim_end_time')
            rate = get(segment, 'anim_play_rate')
            assert rate > 0 and get(segment, 'looping_count') == 1, role
            if source_start <= move_start < move_end <= source_end:
                root_end = start + (move_end - source_start) / rate
            if start >= end - .0001:
                continue
            cut = min(source_end, source_start + (end - start) * rate)
            segment.set_editor_property('anim_end_time', cut)
            kept.append(segment)
        assert kept and root_end is not None and fade_start > root_end + .01, (role, root_end)
        last = kept[-1]
        segment_end = (get(last, 'start_pos') + (get(last, 'anim_end_time') - get(last, 'anim_start_time'))
                       / get(last, 'anim_play_rate'))
        assert abs(segment_end - end) < .005, (role, segment_end, end)
        prepared.append((role, montage, slot, track, kept, blend_time, end))

    for role, montage, slot, track, segments, blend_time, end in prepared:
        track.set_editor_property('anim_segments', segments)
        slot.set_editor_property('anim_track', track)
        montage.set_editor_property('slot_anim_tracks', [slot])
        blend = get(montage, 'blend_out')
        blend.set_editor_property('blend_time', blend_time)
        montage.set_editor_property('blend_out', blend)
    for role, montage, _, _, _, _, end in prepared:
        assert u.EditorAssetLibrary.save_loaded_asset(montage, only_if_is_dirty=False), role
        u.log(f'SPELLBLADE_RECOVERY_TRIMMED {role} {end:.2f}s')


if __name__ == '__main__':
    apply()
