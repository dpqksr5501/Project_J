"""Audit retargeted animation keys and identify which findings can reach play.

Run with UnrealEditor-Cmd -run=pythonscript -script="<this file> [--profile <json>]" -nullrhi.
With no profile, use the project's Spellblade setup.
Angle outliers are review candidates, not proof of a broken motion.
"""

import argparse
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

import unreal as u


SPELLBLADE_ROOTS = (
    "/Game/Anim_Assets/Spellblade",
    "/Game/Characters/Spellblade",
    "/Game/Animation_Logic/BSs/Spellblade",
    "/Game/Animation_Logic/ABPs/Spellblade",
)
SPELLBLADE_CONSUMERS = (
    "/Game/Character_BPs/Spellblade/BP_Spellblade",
    "/Game/DataAssetSets/Animation_Profiles/Equip/DA_Spellblade_Equip",
    "/Game/DataAssetSets/CombatStyle/Spellblade/DA_CombatStyle_SpellbladeKit",
    "/Game/DataAssetSets/Skill_Input_Mapping_Data/Spellblade/DA_Spellblade_Mapping_Data",
)
BONES = (
    "pelvis", "spine_03", "spine_05", "neck_01", "neck_02", "head",
    "clavicle_l", "upperarm_l", "lowerarm_l", "hand_l",
    "clavicle_r", "upperarm_r", "lowerarm_r", "hand_r",
    "thigh_l", "calf_l", "foot_l", "thigh_r", "calf_r", "foot_r",
)
BLEND_TYPES = ("BlendSpace", "BlendSpace1D", "AimOffsetBlendSpace")


def angle(first, second):
    dot = sum(a * b for a, b in zip(first, second))
    return math.degrees(2 * math.acos(min(1, abs(dot))))


def direction_angle(first, second):
    length = first.length() * second.length()
    if length < 1e-8:
        return 0.0
    dot = (first.x * second.x + first.y * second.y + first.z * second.z) / length
    return math.degrees(math.acos(max(-1, min(1, dot))))


def rotation(pose, bone, space):
    transform = u.AnimPoseExtensions.get_bone_pose(pose, bone, space)
    q = transform.rotation
    p = transform.translation
    s = transform.scale3d
    if not all(math.isfinite(x) for x in (q.x, q.y, q.z, q.w, p.x, p.y, p.z, s.x, s.y, s.z)):
        raise ValueError(f"non-finite transform on {bone}")
    return q.x, q.y, q.z, q.w


def package(obj):
    return obj.get_path_name().split(".")[0]


def find_consumers(registry, roots, folders):
    options = u.AssetRegistryDependencyOptions(
        include_hard_package_references=True, include_soft_package_references=True
    )
    visited = set()
    pending = list(roots)
    prefixes = tuple(path.rstrip("/") + "/" for path in folders)
    while pending:
        path = pending.pop()
        if path in visited:
            continue
        visited.add(path)
        pending.extend(str(dep) for dep in registry.get_dependencies(path, options) or []
                       if str(dep).startswith(prefixes) and str(dep) not in visited)
    return visited


def inspect_sequence(asset, ranges, blend_owners, bone_map):
    model = asset.get_editor_property("data_model_interface")
    count = model.get_number_of_frames() + 1
    playback_keys = u.AnimationLibrary.get_num_keys(asset)
    length = asset.get_play_length()
    if count < 2:
        return dict(keys=count, playback_keys=playback_keys, length=length, events=[], seams={})
    opts = u.AnimPoseEvaluationOptions()
    opts.set_editor_property("should_retarget", False)
    local = u.AnimPoseSpaces.LOCAL
    world = u.AnimPoseSpaces.WORLD
    get_pose = u.AnimPoseExtensions.get_anim_pose_at_frame
    available = {str(bone) for bone in u.AnimPoseExtensions.get_bone_names(get_pose(asset, 0, opts))}
    tracks = {bone: [] for bone, name in bone_map.items() if name in available}
    forearms = {side: [] for side in ("l", "r")
                if all(name + side in tracks for name in ("upperarm_", "lowerarm_", "hand_"))}
    for index in range(count):
        pose = get_pose(asset, index, opts)
        for bone in tracks:
            name = bone_map[bone]
            tracks[bone].append((rotation(pose, name, local), rotation(pose, name, world)))
        for side, frames in forearms.items():
            shoulder, elbow, wrist = (
                u.AnimPoseExtensions.get_bone_pose(pose, bone_map[name + side], world).translation
                for name in ("upperarm_", "lowerarm_", "hand_")
            )
            if not all(math.isfinite(v) for p in (shoulder, elbow, wrist) for v in (p.x, p.y, p.z)):
                raise ValueError(f"non-finite arm position at key {index}")
            frames.append((wrist - elbow, (wrist - shoulder).length()))

    step = length / (count - 1)
    if not math.isfinite(step) or step <= 0:
        raise ValueError("invalid animation sample interval")
    # Express review thresholds at 30 Hz while inspecting every authored key.
    key_scale = 30 * step
    neighborhood = max(1, round(4 / key_scale))
    events, seams = [], {}
    for bone, frames in tracks.items():
        delta = [angle(a[0], b[0]) for a, b in zip(frames, frames[1:])]
        seams[bone] = round(angle(frames[0][0], frames[-1][0]), 2)
        for index, jump in enumerate(delta):
            around = delta[max(0, index - neighborhood):index] + delta[index + 1:index + 1 + neighborhood]
            if jump < 30 * key_scale or not around or jump < 2 * max(4 * key_scale, statistics.median(around)):
                continue
            t0, t1 = index * step, (index + 1) * step
            owners = [owner for owner, start, end in ranges if t0 < end - 0.0001 and t1 > start + 0.0001]
            world_jump = angle(frames[index][1], frames[index + 1][1])
            event = dict(bone=bone, key=index, time=round(t0, 4), local=round(jump, 2),
                         component=round(world_jump, 2), reachable_owners=sorted(set(owners + blend_owners)))
            if bone.startswith("lowerarm_") and bone[-1] in forearms:
                side = bone[-1]
                a, b = forearms[side][index:index + 2]
                wrist = tracks["hand_" + side]
                swing = direction_angle(a[0], b[0])
                wrist_change = angle(wrist[index][1], wrist[index + 1][1])
                event.update(axis_swing=round(swing, 2), wrist_change=round(wrist_change, 2),
                             reach=round(min(a[1], b[1]), 2),
                             axial_candidate=(world_jump >= 25 * key_scale and swing <= 10 * key_scale
                                              and wrist_change <= 12 * key_scale))
            events.append(event)
    return dict(keys=count, playback_keys=playback_keys, length=length, sample_hz=1 / step, events=events, seams=seams,
                missing_bones=sorted(name for name in bone_map.values() if name not in available))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--profile", type=Path)
    args = parser.parse_args()
    saved = Path(u.Paths.project_saved_dir())
    if args.profile:
        profile = json.loads(args.profile.read_text(encoding="utf-8-sig"))
        roots = profile["asset_roots"]
        consumers = profile["consumer_roots"]
        style_roots = profile.get("style_roots", [])
        dependency_roots = profile.get("dependency_roots", [])
        aliases = profile.get("bone_map", {})
        if set(aliases) - set(BONES):
            raise ValueError("bone_map keys must use the audit's UE5 role names")
        target = saved / "AnimationAudit" / (args.profile.stem + ".json")
    else:
        roots = SPELLBLADE_ROOTS
        consumers = SPELLBLADE_CONSUMERS
        style_roots = SPELLBLADE_CONSUMERS[1:3]
        dependency_roots = ("/Game/DataAssetSets", "/Game/Character_BPs/Spellblade", "/Game/GAs/Spellblade")
        aliases = {}
        target = saved / "SpellbladeCombatNext" / "SpellbladeContinuityAudit.json"
    if not roots or any(not isinstance(path, str) or not path.startswith("/Game/")
                        for path in (*roots, *consumers, *style_roots, *dependency_roots)):
        raise ValueError("Supply at least one asset root and use /Game/ package paths")
    bone_map = {bone: aliases.get(bone, bone) for bone in BONES}
    if any(not isinstance(name, str) or not name for name in bone_map.values()):
        raise ValueError("bone_map values must be non-empty skeleton bone names")

    registry = u.AssetRegistryHelpers.get_asset_registry()
    folders = (*roots, *dependency_roots)
    style_reachable = find_consumers(registry, style_roots, folders)
    reachable = find_consumers(registry, consumers, folders) | style_reachable
    rows = {str(row.package_name): row for root in roots for row in registry.get_assets_by_path(root, True)}
    if not rows:
        raise ValueError(f"No assets found under {roots}")
    report = dict(clips={}, montages={}, blend_spaces={}, errors={})
    ranges, blend_owners = defaultdict(list), defaultdict(list)
    for path, row in sorted(rows.items()):
        kind = str(row.asset_class_path.asset_name)
        if kind != "AnimMontage" and kind not in BLEND_TYPES:
            continue
        try:
            asset = row.get_asset()
            if kind == "AnimMontage":
                segments = []
                for slot in asset.get_editor_property("slot_anim_tracks"):
                    slot_name = str(slot.get_editor_property("slot_name"))
                    track = slot.get_editor_property("anim_track")
                    for segment in track.get_editor_property("anim_segments"):
                        clip = segment.get_editor_property("anim_reference")
                        if not clip:
                            continue
                        clip_path = package(clip)
                        start = segment.get_editor_property("anim_start_time")
                        end = segment.get_editor_property("anim_end_time")
                        segments.append(dict(clip=clip_path, start=start, end=end, slot=slot_name))
                        if path in reachable:
                            ranges[clip_path].append((path, start, end))
                report["montages"][path] = dict(reachable=path in reachable, style_reachable=path in style_reachable,
                                                segments=segments)
            else:
                clips = []
                for sample in asset.get_editor_property("sample_data"):
                    clip = sample.get_editor_property("animation")
                    if clip:
                        clips.append(package(clip))
                        if path in reachable:
                            blend_owners[package(clip)].append(path)
                report["blend_spaces"][path] = dict(reachable=path in reachable, style_reachable=path in style_reachable,
                                                   clips=sorted(set(clips)))
        except Exception as exc:
            report["errors"][path] = str(exc)
    for path, row in sorted(rows.items()):
        if str(row.asset_class_path.asset_name) != "AnimSequence":
            continue
        try:
            result = inspect_sequence(row.get_asset(), ranges[path], blend_owners[path], bone_map)
            report["clips"][path] = dict(reachable=path in reachable, **result)
        except Exception as exc:
            report["errors"][path] = str(exc)
    target.parent.mkdir(parents=True, exist_ok=True)
    target.write_text(json.dumps(report, separators=(",", ":")), encoding="utf-8")
    counts = {name: len(rows) for name, rows in report.items()}
    u.log(f"SPELLBLADE_CONTINUITY {counts}")
    if report["errors"]:
        raise RuntimeError(f"Spellblade continuity audit encountered errors. See {target}")


if __name__ == "__main__":
    main()
