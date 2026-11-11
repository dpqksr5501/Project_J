"""Read-only Winger offline assets, root, limb and IK marker checks in Unreal."""
import json
import math
import os
from pathlib import Path

import unreal as ue

SOURCE = "/Game/Characters/Mannequins/Animations"
DEST = "/Game/Characters/Winger/Animation/Retargeted"
source = ue.load_asset("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple")
target = ue.load_asset("/Game/Characters/Winger/Meshes/SK_Winger_UE5")
api = ue.AnimationLibrary
pose_api = ue.AnimPoseExtensions
world = ue.AnimPoseSpaces.WORLD
report = {"sequences": 0, "blend_spaces": 0, "root_enabled": 0, "pose_samples": 0,
          "max_ik_marker_error_cm": 0.0, "max_root_error_cm": 0.0, "arm_worst": {}, "issues": [],
          "marker_errors_by_additive_type": {}, "marker_outliers": []}
limbs = ("foot_l", "foot_r", "hand_l", "hand_r")


def check(condition, asset, issue, detail=None):
    if not condition:
        report["issues"].append(dict(asset=asset, issue=issue, detail=detail))


def option(mesh):
    opts = ue.AnimPoseEvaluationOptions()
    opts.optional_skeletal_mesh = mesh
    opts.evaluation_type = ue.AnimDataEvalType.COMPRESSED
    opts.retrieve_additive_as_full_pose = True
    return opts


def notify_contract(seq):
    result = []
    for event in api.get_animation_notify_events(seq):
        obj = event.get_editor_property("notify")
        state = event.get_editor_property("notify_state_class")
        if state and state.get_class().get_name() == "AnimNotifyState_PoseSearchBranchIn":
            continue
        result.append((str(event.get_editor_property("notify_name")),
                       round(api.get_anim_notify_event_trigger_time(event), 4),
                       round(api.get_anim_notify_event_duration(event), 4),
                       obj.get_class().get_path_name() if obj else "",
                       state.get_class().get_path_name() if state else ""))
    return result


def marker_contract(seq):
    return [(str(m.get_editor_property("marker_name")), round(m.get_editor_property("time"), 4))
            for m in api.get_animation_sync_markers(seq)]


def point(pose, name):
    return pose_api.get_bone_pose(pose, name, world).translation


def bend(points):
    a, b, c = points
    ab, bc = b - a, c - b
    return math.degrees(math.acos(max(-1, min(1, ab.dot(bc) / (ab.length() * bc.length())))))


def pelvis_z(mesh):
    ref = json.loads(ue.Project_JAssetMigrationLibrary.inspect_reference_skeleton(mesh))
    pelvis = next(row for row in ref["bones"] if row["name"] == "pelvis")
    return float(pelvis["component"].split("|")[0].split(",")[2])


registry = ue.AssetRegistryHelpers.get_asset_registry()
registry.wait_for_completion()
dependencies = ue.AssetRegistryDependencyOptions(
    include_hard_package_references=True, include_soft_package_references=True,
    include_searchable_names=False, include_hard_management_references=False,
    include_soft_management_references=False)
target_skeleton = target.get_editor_property("skeleton")
source_skeleton = source.get_editor_property("skeleton")
source_option, target_option = option(source), option(target)
scale = pelvis_z(target) / pelvis_z(source)
report["root_length_scale"] = scale
root_props = ("enable_root_motion", "root_motion_root_lock", "force_root_lock", "use_normalized_root_motion_scale")
for data in registry.get_assets_by_path(SOURCE, recursive=True):
    kind = str(data.asset_class_path.asset_name)
    if kind != "AnimSequence" and "BlendSpace" not in kind:
        continue
    original = data.get_asset()
    if original.get_editor_property("skeleton") != source_skeleton:
        continue
    path = str(data.package_name)
    output_path = DEST + path.removeprefix(SOURCE)
    converted = ue.load_asset(output_path)
    check(bool(converted), path, "Missing conversion")
    if not converted:
        continue
    external = sorted(str(name) for name in registry.get_referencers(output_path, dependencies) or ()
                      if not str(name).startswith(DEST + "/"))
    check(not external, output_path, "External reference to offline bank", external)
    check(converted.get_editor_property("skeleton") == target_skeleton, path, "Wrong target skeleton")
    if isinstance(original, ue.BlendSpace):
        report["blend_spaces"] += 1
        before = original.get_editor_property("sample_data")
        after = converted.get_editor_property("sample_data")
        check(len(before) == len(after), path, "Blend sample count")
        for a, b in zip(before, after):
            check(a.get_editor_property("sample_value") == b.get_editor_property("sample_value"), path, "Blend coordinate")
            sample = b.get_editor_property("animation")
            check(bool(sample) and sample.get_path_name().startswith(DEST + "/"), path, "Blend sample reference")
        continue

    report["sequences"] += 1
    report["root_enabled"] += int(converted.get_editor_property("enable_root_motion"))
    for prop in (*root_props, "additive_anim_type", "ref_pose_type", "ref_frame_index"):
        check(original.get_editor_property(prop) == converted.get_editor_property(prop), path, prop)
    original_base = original.get_editor_property("ref_pose_seq")
    converted_base = converted.get_editor_property("ref_pose_seq")
    if original_base:
        check(bool(converted_base) and converted_base.get_path_name().startswith(DEST + "/"), path, "Additive base")
    check(notify_contract(original) == notify_contract(converted), path, "Notify names, timing or class")
    check(marker_contract(original) == marker_contract(converted), path, "Sync markers")
    curve = ue.RawCurveTrackTypes.RCT_FLOAT
    check(set(api.get_animation_curve_names(original, curve)) == set(api.get_animation_curve_names(converted, curve)),
          path, "Float curve names")
    a_model = original.get_editor_property("data_model_interface")
    b_model = converted.get_editor_property("data_model_interface")
    a_rate, b_rate = a_model.get_frame_rate(), b_model.get_frame_rate()
    check((a_rate.numerator, a_rate.denominator) == (b_rate.numerator, b_rate.denominator), path, "Frame rate")
    check(a_model.get_number_of_frames() == b_model.get_number_of_frames(), path, "Frame count")

    length = original.get_play_length()
    additive_type = str(converted.get_editor_property("additive_anim_type"))
    a_root = api.get_bone_pose_for_time(original, "root", 0.0, False).translation
    b_root = api.get_bone_pose_for_time(converted, "root", 0.0, False).translation
    rate = a_rate.numerator / a_rate.denominator
    for phase in (0.0, 0.5, 1.0):
        t = min(length, round(length * phase * rate) / rate)
        a = pose_api.get_anim_pose_at_time(original, t, source_option)
        b = pose_api.get_anim_pose_at_time(converted, t, target_option)
        check(pose_api.is_valid(a) and pose_api.is_valid(b), path, "Invalid pose", phase)
        if not pose_api.is_valid(a) or not pose_api.is_valid(b):
            continue
        report["pose_samples"] += 1
        a_delta = api.get_bone_pose_for_time(original, "root", t, False).translation - a_root
        b_delta = api.get_bone_pose_for_time(converted, "root", t, False).translation - b_root
        root_error = (a_delta * scale - b_delta).length()
        report["max_root_error_cm"] = max(report["max_root_error_cm"], root_error)
        for limb in limbs:
            error = (point(b, limb) - point(b, "ik_" + limb)).length()
            category = report["marker_errors_by_additive_type"]
            category[additive_type] = max(error, category.get(additive_type, 0.0))
            if error > 0.35:
                source_error = (point(a, limb) - point(a, "ik_" + limb)).length()
                report["marker_outliers"].append(dict(
                    asset=path, limb=limb, phase=phase, type=additive_type, target_cm=error, source_cm=source_error))
            if error > report["max_ik_marker_error_cm"]:
                report["max_ik_marker_error_cm"] = error
                report["worst_ik_marker"] = [path, phase, limb]
        for s in ("l", "r"):
            for joint, bones in (
                ("elbow", ("upperarm", "lowerarm", "hand")),
                ("wrist", ("lowerarm", "hand", "middle_01")),
            ):
                src = bend([point(a, f"{name}_{s}") for name in bones])
                out = bend([point(b, f"{name}_{s}") for name in bones])
                delta = abs(src - out)
                key = f"{joint}_{s}"
                if delta > report["arm_worst"].get(key, {}).get("delta_deg", -1):
                    report["arm_worst"][key] = dict(asset=path, phase=phase, source_deg=src, target_deg=out, delta_deg=delta)
output = Path(os.environ.get("WINGER_AUDIT_OUTPUT", str(Path(ue.Paths.project_saved_dir()) / "WingerWork/Direct/OfflineAudit.json")))
output.parent.mkdir(parents=True, exist_ok=True)
output.write_text(json.dumps(report, indent=2), encoding="utf-8")
ue.log("WINGER_OFFLINE_AUDIT " + str({k: v for k, v in report.items()
                                    if k not in ("issues", "arm_worst", "marker_outliers")}))
assert report["sequences"] == 575 and report["blend_spaces"] == 11
assert not report["issues"], report["issues"][:5]
assert report["max_root_error_cm"] < 0.05, report["max_root_error_cm"]
assert report["max_ik_marker_error_cm"] < 0.35, report["worst_ik_marker"]
