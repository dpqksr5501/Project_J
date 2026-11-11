"""Author Winger's coordinated wing poses on the existing UE5 deform skeleton."""
import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

parser = argparse.ArgumentParser()
parser.add_argument("base_blend", type=Path, help="Output of RebindWinger.py before authoring")
parser.add_argument("output", type=Path)
parser.add_argument("--clips", nargs="+", choices=("idle", "walkN", "run", "Sprint", "Jump", "Air", "Land"),
                    help="Export only these FBXs while writing the complete Blender rig and actions")
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
args.output.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(args.base_blend))
scene = bpy.context.scene
rig, = [obj for obj in scene.objects if obj.type == "ARMATURE"]
body = [obj for obj in scene.objects if obj.type == "MESH"]
animation = rig.animation_data_create()
prefix = "A_Winger_Wings_"
fps = scene.render.fps / scene.render.fps_base
assert fps == 30

joints = {
    side: [f"{side}_{i:02}" for i in range(1, 5)]
    for side in "LR"
}
feathers = {
    side: [f"jiggle_wing{side}_{i:02}" for i in range(1, 6)]
    for side in "LR"
}
branch = {
    bone.name for bone in rig.data.bones
    if bone.name == "a_01" or any(parent.name == "a_01" for parent in bone.parent_recursive)
}
bone_names = tuple(sorted(branch))
assert {name for side in "LR" for name in joints[side] + feathers[side]} <= branch
assert len(branch) == 20
rest = {bone.name: bone.matrix_local.copy() for bone in rig.data.bones}
position = {name: matrix.translation for name, matrix in rest.items()}
parents = {bone.name: bone.parent.name for bone in rig.data.bones if bone.parent}
rest_local = {name: rest[parents[name]].inverted() @ matrix if name in parents else matrix
              for name, matrix in rest.items()}

# Find which surface each existing feather bone actually moves. Use the
# weighted surface direction to set its fan sign, including mirrored sides.
skin = {name: [] for side in "LR" for name in feathers[side]}
for obj in body:
    for vertex in obj.data.vertices:
        for group in vertex.groups:
            name = obj.vertex_groups[group.group].name
            if name in skin and group.weight > 0.02:
                skin[name].append((obj.matrix_world @ vertex.co, group.weight))
assert all(skin.values())
up = Vector((0, 0, 1))
back = sum((position[f"foot_{side}"] - position[f"ball_{side}"] for side in "lr"), Vector())
back.z = 0
back.normalize()
outward = {"L": back.cross(up), "R": up.cross(back)}
axes = {}
panel_frames = {}
open_span = {}
for side in "LR":
    chain = joints[side]
    span = (position[chain[-1]] - position[chain[0]]).normalized()
    # Continue the original root-to-wrist line through the entire spar.
    # Use the two inner bones together so a single short segment cannot set the slope.
    open_span[side] = (position[chain[2]] - position[chain[0]]).normalized()
    wing_normal = span.cross(up).normalized()
    feather_rays = []
    for name in feathers[side]:
        weighted = skin[name]
        centroid = sum((co * w for co, w in weighted), Vector()) / sum(w for _, w in weighted)
        radial = (centroid - position[name]).normalized()
        sign = 1 if wing_normal.cross(radial).dot(span) >= 0 else -1
        inv = rest[name].to_3x3().inverted()
        axes[name] = (inv @ (sign * wing_normal), inv @ radial.cross(back).normalized())
        if name in (feathers[side][1], feathers[side][2]):
            feather_rays.append(radial)
    chord = sum(feather_rays, Vector()).normalized()
    for name, child in zip(chain, chain[1:] + [feathers[side][1]]):
        length = (position[child] - position[name]).normalized()
        inv = rest[name].to_3x3().inverted()
        feather_side = (chord - length * chord.dot(length)).normalized()
        panel_frames[name] = Matrix((inv @ length, inv @ feather_side, inv @ length.cross(feather_side))).transposed()


def read_pose():
    return {name: rig.pose.bones[name].matrix_basis.decompose()[:2] for name in bone_names}


def read_action(label):
    action = bpy.data.actions[prefix + label + "_UE5"]
    animation.action = action
    animation.action_slot, = action.slots
    start, end = map(round, action.frame_range)
    frames = []
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        frames.append(read_pose())
    animation.action = None
    return frames


def mix(a, b, amount):
    return {name: (a[name][0].lerp(b[name][0], amount), a[name][1].slerp(b[name][1], amount))
            for name in bone_names}


def envelope(value):
    value = min(1, max(0, value))
    return value * value * (3 - 2 * value)


def channel(keys, t):
    """Monotonic cubic interpolation with flat ends and no overshoot."""
    t = min(keys[-1][0], max(keys[0][0], t))
    for i, ((start, a), (end, b)) in enumerate(zip(keys, keys[1:])):
        if t > end:
            continue
        width = end - start
        secant = (b - a) / width

        def slope(other):
            return 2 * secant * other / (secant + other) if secant * other > 0 else 0.0

        left = slope((a - keys[i - 1][1]) / (start - keys[i - 1][0])) if i else 0
        right = slope((keys[i + 2][1] - b) / (keys[i + 2][0] - end)) if i + 2 < len(keys) else 0
        u = (t - start) / width
        return a + (b - a) * envelope(u) + width * (
            left * u * (1 - u) ** 2 + right * u * u * (u - 1))
    raise ValueError(t)


def component_transform(pose, name):
    if name not in pose:
        return rest[name]
    location, rotation = pose[name]
    return component_transform(pose, parents[name]) @ rest_local[name] @ Matrix.LocRotScale(location, rotation, None)


def panel_frame(span, feather_side, bank):
    side = (feather_side - span * feather_side.dot(span)).normalized()
    side = Quaternion(span, math.radians(bank)) @ side
    return Matrix((span, side, span.cross(side))).transposed()


def orient_panel(pose, name, basis):
    target = (basis @ panel_frames[name].transposed()).to_quaternion()
    parent = component_transform(pose, parents[name]) @ rest_local[name]
    pose[name] = pose[name][0], parent.to_quaternion().inverted() @ target


def gesture(pose, controls, frame, duration, posture_profile, cyclic=False, tip_fold_ratio=None):
    def drive(delay):
        t = (frame - delay) % duration if cyclic else frame - delay
        values = [channel(c, t) for c in controls]
        if cyclic:
            # Center the delayed loop on its authored air reference at the seam.
            values = [value - channel(c, -delay % duration) for value, c in zip(values, controls)]
        else:
            # Each phase reaches its exact shared boundary, even for distal delays.
            settle = envelope(frame / 2) * envelope((duration - frame) / 3)
            values = [value * settle for value in values]
        return values

    lift, fold, outer_trim = (channel(keys, frame) for keys in posture_profile)
    alignment = min(1, fold / 45)
    delayed_frame = frame - 1.5 * envelope(frame / 3) * envelope((duration - frame) / 3)
    tip_fold = min(2, max(0, .04 * channel(posture_profile[1], delayed_frame) + .10 * alignment * drive(1.5)[1]))
    shaped = dict(pose)
    shaped["a_01"] = pose["a_01"][0], Quaternion()
    for side in "LR":
        chain = joints[side]
        feather_side = (.76 * back - .65 * up).normalized()
        mirror = 1 if side == "L" else -1
        bind_span = open_span[side]
        bind_heading = (bind_span - up * bind_span.dot(up)).normalized()
        bind_elevation = math.asin(bind_span.dot(up))
        for i, name in enumerate(chain[:3]):
            pulse, _, twist, _ = drive(i)
            elevation = bind_elevation + math.radians(lift + .32 * pulse)
            heading = (bind_heading + alignment * (.015, .035, .05)[i] * back).normalized()
            span = heading * math.cos(elevation) + up * math.sin(elevation)
            bank = mirror * (.55 * twist + alignment * (1.2, 2.4, 3.2)[i])
            basis = panel_frame(span, feather_side, bank)
            if i < 2:
                orient_panel(shaped, name, basis)

        # Rotate the outer panel from the raised baseline in its feather plane.
        # Trim is relative to that fold, so its effect disappears in the open pose.
        wrist_fold = max(0, fold + .4 * alignment * drive(0)[1])
        basis = Quaternion(basis.col[2], math.radians(wrist_fold)).to_matrix() @ basis
        folded_span = basis.col[0]
        planar = (folded_span - up * folded_span.dot(up)).normalized()
        elevation = math.asin(folded_span.dot(up)) + math.radians(outer_trim)
        span = planar * math.cos(elevation) + up * math.sin(elevation)
        basis = folded_span.rotation_difference(span).to_matrix() @ basis
        orient_panel(shaped, chain[2], basis)
        outer_fold = tip_fold if tip_fold_ratio is None else wrist_fold * tip_fold_ratio
        tip_basis = Quaternion(basis.col[2], math.radians(outer_fold)).to_matrix() @ basis
        orient_panel(shaped, chain[3], tip_basis)
        for name, delay, gain in zip(feathers[side], (3.2, 4.4, 3.7, 2.5, 1.7), (.54, 1., .82, .64, .46)):
            _, _, twist, fan = drive(delay)
            fan_axis, bank_axis = axes[name]
            location, base = pose[name]
            spread = Quaternion(fan_axis, math.radians(gain * fan))
            bank = Quaternion(bank_axis, math.radians(twist * .6))
            shaped[name] = location, Quaternion().slerp(base, .22) @ spread @ bank
    return shaped


original = {label: read_action(label) for label in ("idle", "walkN", "run", "Jump")}
# Start the takeoff from a traveling posture. The old launch from Idle and
# the legacy open-air reference spread the panels between Sprint and Air.
travel = original["run"][round((len(original["run"]) - 1) * .64)]
push = mix(travel, original["Jump"][4], .12)
air = mix(travel, original["Jump"][-1], .14)


def from_rows(rows):
    return [tuple((row[0], row[i]) for row in rows) for i in range(1, len(rows[0]))]


# Values are degrees for lift, fold, twist and feather spread. The authored
# phase is shared by both wings. Joint/feather delays propagate the movement.
phases = {
    "Jump": (13, [(0, 0, 0, 0, 0), (3, 6, 2, 1, 1), (6, -7, 3, -3, -5),
                   (9, 4, 0, 3, 6), (13, 0, 0, 0, 0)]),
    "Air": (34, [(0, 0, 0, 0, 0), (8, 3, -2.5, 2, 5), (17, -2, 2.5, -1, -4),
                  (25, 1, -1, 1.5, 2), (34, 0, 0, 0, 0)]),
    "Land": (10, [(0, 0, 0, 0, 0), (2, -2, 1, -1, -2), (4, -3, 1.5, -2, -3),
                   (7, -1, .5, -.6, -1), (10, 0, 0, 0, 0)]),
}
# Inner lift, fold toward the feathers and relative pitch trim after the fold.
air_posture = (5, 37, 0)
postures = {
    "Jump": [(0, 8, 43, -2), (4, 10, 47, -5), (7, 8, 45, -3), (13, *air_posture)],
    "Air": [(0, *air_posture), (34, *air_posture)],
    # Absorb the impact and return to the shared airborne reference. The
    # runtime ground blend owns the move into the actual grounded gait.
    "Land": [(0, *air_posture), (3, 4, 38.5, -1.5), (6, 4.5, 38, -.75), (10, *air_posture)],
}


variants = {}
ground_wrist_folds = {"run": 35, "Sprint": 65}
ground_tip_ratio = .25
for label, carrier, amplitude, posture in (
    ("idle", "idle", .45, (0, 0, 0)), ("walkN", "walkN", 1.15, (1, 23, 0)),
    ("run", "run", 2.2, (3, ground_wrist_folds["run"], 0)),
    ("Sprint", "run", 2.2, (9, ground_wrist_folds["Sprint"], -3)),
):
    source = original[carrier]
    end = len(source) - 1
    rows = [(round(end * f), v * amplitude, w * amplitude, q * amplitude, k * amplitude)
            for f, v, w, q, k in [(0, 0, 0, 0, 0), (.25, 1.1, -.5, .4, 1),
                                   (.55, -.8, 1.25, -.35, -1), (.78, .3, .1, .25, .5), (1, 0, 0, 0, 0)]]
    controls = from_rows(rows)
    profile = from_rows([(0, *posture), (end, *posture)])
    variants[label] = [gesture(mix(base, source[0], envelope((frame / end - .82) / .18)),
                               controls, frame, end, profile,
                               tip_fold_ratio=ground_tip_ratio if label in ground_wrist_folds else None)
                       for frame, base in enumerate(source)]

for label, (end, rows) in phases.items():
    controls = from_rows(rows)
    if label == "Jump":
        bases = [mix(travel, push, envelope(frame / 4)) if frame <= 4 else
                 mix(push, air, envelope((frame - 4) / (end - 4))) for frame in range(end + 1)]
    else:
        bases = [air] * (end + 1)
    profile = from_rows(postures[label])
    variants[label] = [gesture(pose, controls, frame, end, profile, label == "Air")
                       for frame, pose in enumerate(bases)]
def angular_distance(a, b):
    a, b = a.normalized(), b.normalized()
    sign = -1 if a.dot(b) < 0 else 1
    difference = math.sqrt(sum((x - sign * y) ** 2 for x, y in zip(a, b)))
    return math.degrees(4 * math.asin(min(1, difference * .5)))


for a, a_key, b, b_key in (
    ("Jump", -1, "Air", 0), ("Air", -1, "Land", 0), ("Land", -1, "Air", 0),
    *((name, 0, name, -1) for name in ("idle", "walkN", "run", "Sprint", "Air")),
):
    assert all(angular_distance(variants[a][a_key][name][1], variants[b][b_key][name][1]) < .08
               and (variants[a][a_key][name][0] - variants[b][b_key][name][0]).length < 1e-4
               for name in bone_names), (a, b)

hinge_report = {}
outer_spar_report = {}
open_spar_report = {}
for side in "LR":
    inner = joints[side][0]
    wrist, outer = joints[side][2:]
    tip = feathers[side][1]
    def geometry(pose):
        point = lambda name: component_transform(pose, name).translation
        return (point(wrist) - point(inner)).z, point(outer) - point(wrist), point(tip) - point(outer)

    outer_spar_report[side] = {}
    def frontal_angle(span):
        return math.degrees(math.atan2(span.dot(up), span.dot(outward[side])))

    original_angle = frontal_angle(open_span[side])
    def open_error(pose):
        point = lambda name: component_transform(pose, name).translation
        chain = joints[side] + [tip]
        return max(abs(frontal_angle(point(b) - point(a)) - original_angle)
                   for a, b in zip(chain, chain[1:]))

    idle_error = max(map(open_error, variants["idle"]))
    assert idle_error < .5, (side, original_angle, idle_error)
    open_spar_report[side] = {"original_root_to_wrist_deg": original_angle,
                             "max_open_front_error_deg": idle_error}
    for label, frames in variants.items():
        angle_max = max(math.degrees(first.angle(last)) for _, first, last in map(geometry, frames))
        if label in ground_wrist_folds:
            target = ground_wrist_folds[label] * ground_tip_ratio
            angles = [math.degrees(first.angle(last)) for _, first, last in map(geometry, frames)]
            assert max(abs(angle - target) for angle in angles) < .5, (label, side, angles)
        else:
            assert angle_max < 2.2, (label, side, angle_max)
        outer_spar_report[side][label] = round(angle_max, 3)
    deltas = []
    for run, sprint in zip(variants["run"], variants["Sprint"], strict=True):
        run_lift, run_panel, _ = geometry(run)
        sprint_lift, sprint_panel, sprint_end = geometry(sprint)
        sprint_tip = sprint_panel + sprint_end
        assert run_lift > 34 and sprint_lift - run_lift > 3.5
        assert sprint_panel.z < run_panel.z - 3
        assert sprint_tip.dot(outward[side]) > 0, (side, sprint_tip.dot(outward[side]))
        assert (sprint_panel - run_panel).dot(back) > 4
        outer_follow = angular_distance(run[outer][1], sprint[outer][1])
        assert abs(outer_follow - (ground_wrist_folds["Sprint"] - ground_wrist_folds["run"])
                   * ground_tip_ratio) < .5, (side, outer_follow)
        deltas.append((sprint_lift - run_lift, sprint_panel.z - run_panel.z,
                       (sprint_panel - run_panel).dot(back), sprint_tip.dot(outward[side]), outer_follow))
    hinge_report[side] = {name: [round(min(values), 2), round(max(values), 2)]
                          for name, values in zip(("inner_rise_cm", "outer_height_delta_cm",
                                                  "outer_plan_delta_cm", "outer_span_cm",
                                                  "outer_local_delta_deg"), zip(*deltas))}

bpy.ops.object.select_all(action="DESELECT")
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
report = {"sprint_sweep_over_run": hinge_report, "outer_spar_bend_deg": outer_spar_report,
          "open_spar_front_deg": open_spar_report,
          "weighted_feathers": {name: len(values) for name, values in skin.items()}, "clips": {}}
for label, frames in variants.items():
    name = prefix + label + "_UE5"
    previous_action = bpy.data.actions.get(name)
    if previous_action:
        bpy.data.actions.remove(previous_action)
    action = bpy.data.actions.new(name)
    action.use_fake_user = True
    animation.action = action
    previous = {}
    for frame, transforms in enumerate(frames):
        for bone_name, (location, rotation) in transforms.items():
            bone = rig.pose.bones[bone_name]
            bone.rotation_mode = "QUATERNION"
            bone.location = location
            rotation = rotation.normalized()
            if bone_name in previous:
                rotation.make_compatible(previous[bone_name])
            bone.rotation_quaternion = rotation
            previous[bone_name] = rotation
            bone.keyframe_insert("location", frame=frame, group=bone_name)
            bone.keyframe_insert("rotation_quaternion", frame=frame, group=bone_name)
    scene.frame_start, scene.frame_end = 0, len(frames) - 1
    scene.frame_set(0)
    if args.clips is None or label in args.clips:
        assert bpy.ops.export_scene.fbx(
            filepath=str(args.output / (name + ".fbx")), use_selection=True, object_types={"ARMATURE"},
            apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE", axis_forward="-Y", axis_up="Z",
            add_leaf_bones=False, use_armature_deform_only=False, armature_nodetype="NULL",
            primary_bone_axis="Y", secondary_bone_axis="X", bake_anim=True,
            bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
            bake_anim_step=1, bake_anim_simplify_factor=0) == {"FINISHED"}
    excursion = {bone: round(max(angular_distance(frames[0][bone][1], frame[bone][1])
                                 for frame in frames), 3) for bone in bone_names}
    assert all(excursion[bone] > .1 for bone in skin), (label, excursion)
    report["clips"][label] = {"seconds": scene.frame_end / fps, "local_excursion_deg": excursion}
    animation.action = None
    for pose_bone in rig.pose.bones:
        pose_bone.matrix_basis = Matrix.Identity(4)

scene.frame_start, scene.frame_end = 0, 120
scene.frame_set(0)
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(args.output / args.base_blend.name), compress=True)
(args.output / "ArticulationBake.json").write_text(json.dumps(report, indent=2), encoding="utf-8")
