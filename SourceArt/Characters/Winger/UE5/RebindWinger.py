"""Rebind the original Winger mesh to the project UE5 biped in Blender."""
import argparse
import json
import sys
from pathlib import Path

import bpy
from mathutils import Matrix

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("quinn", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:])
args.output.mkdir(parents=True, exist_ok=True)
bpy.ops.wm.open_mainfile(filepath=str(args.source))
scene = bpy.context.scene
legacy, = [obj for obj in scene.objects if obj.type == "ARMATURE"]
meshes = [obj for obj in scene.objects if obj.type == "MESH"]
legacy.animation_data_clear()
for pose in legacy.pose.bones:
    pose.matrix_basis = Matrix.Identity(4)
bpy.context.view_layer.update()

old = {bone.name: bone.matrix_local.copy() for bone in legacy.data.bones}
names_before = set(bpy.data.objects.keys())
bpy.ops.import_scene.fbx(filepath=str(args.quinn), use_anim=False, use_image_search=False)
references = [obj for obj in bpy.data.objects if obj.name not in names_before]
quinn, = [obj for obj in references if obj.type == "ARMATURE"]
standard = {bone.name: (quinn.matrix_world @ bone.matrix_local, bone.parent.name if bone.parent else "root")
            for bone in quinn.data.bones}
assert "pelvis" in standard and "root" not in standard and len(standard) == 88
scene.render.fps = 30
scene.render.fps_base = 1.0
for obj in references:
    bpy.data.objects.remove(obj, do_unlink=True)

prefix = "ValveBiped.Bip01"
mapping = {"root": "root", **{
    f"{prefix}_{source}": target for source, target in (
        ("Pelvis", "pelvis"), ("Spine", "spine_01"), ("Spine1", "spine_02"),
        ("Spine2", "spine_04"), ("Spine4", "spine_05"), ("Neck1", "neck_01"), ("Head1", "head"))
}}
for s in "LR":
    for source, target in (
        ("Clavicle", "clavicle"), ("UpperArm", "upperarm"), ("Forearm", "lowerarm"),
        ("Hand", "hand"), ("Thigh", "thigh"), ("Calf", "calf"), ("Foot", "foot"), ("Toe0", "ball")
    ):
        mapping[f"{prefix}_{s}_{source}"] = f"{target}_{s.lower()}"
    for digit, standard_digit in enumerate(("thumb", "index", "middle", "ring", "pinky")):
        for index, suffix in enumerate(("", "1", "2"), 1):
            mapping[f"{prefix}_{s}_Finger{digit}{suffix}"] = f"{standard_digit}_{index:02}_{s.lower()}"
reverse = {target: source for source, target in mapping.items()}
assert set(mapping) <= set(old)


def palm_frame(wrist, middle, index, pinky):
    along = (middle - wrist).normalized()
    normal = along.cross(pinky - index).normalized()
    across = normal.cross(along)
    return Matrix((along, across, normal)).transposed()


def quinn_pos(name):
    return standard[name][0].translation


def set_world(pose, rotation, position):
    transform = (rotation @ old[pose.name].to_quaternion()).to_matrix().to_4x4()
    transform.translation = position
    pose.matrix = transform
    bpy.context.view_layer.update()


# Match Quinn's elbow plane and palm orientation while retaining Winger's bone lengths.
for s in "lr":
    u, e, w = (reverse[f"{part}_{s}"] for part in ("upperarm", "lowerarm", "hand"))
    hand, elbow, shoulder = (old[name].translation for name in (w, e, u))
    upper = elbow - shoulder
    lower = hand - elbow
    target_upper = quinn_pos(f"lowerarm_{s}") - quinn_pos(f"upperarm_{s}")
    target_lower = quinn_pos(f"hand_{s}") - quinn_pos(f"lowerarm_{s}")
    source_palm = palm_frame(hand, *(old[reverse[f"{d}_01_{s}"]].translation for d in ("middle", "index", "pinky")))
    target_palm = palm_frame(quinn_pos(f"hand_{s}"), *(quinn_pos(f"{d}_01_{s}") for d in ("middle", "index", "pinky"))
    )
    hand_rotation = (target_palm @ source_palm.transposed()).to_quaternion()
    upper_rotation = upper.rotation_difference(target_upper)
    # Reorient the forearm in the same palm frame, then correct its swing.
    lower_rotation = (hand_rotation @ lower).rotation_difference(target_lower) @ hand_rotation
    new_elbow = shoulder + target_upper.normalized() * upper.length
    new_wrist = new_elbow + target_lower.normalized() * lower.length
    set_world(legacy.pose.bones[u], upper_rotation, shoulder)
    set_world(legacy.pose.bones[e], lower_rotation, new_elbow)
    set_world(legacy.pose.bones[w], hand_rotation, new_wrist)

reposed = {pose.name: pose.matrix.copy() for pose in legacy.pose.bones}
depsgraph = bpy.context.evaluated_depsgraph_get()
skinned_data = {obj.name: bpy.data.meshes.new_from_object(
    obj.evaluated_get(depsgraph), preserve_all_data_layers=True, depsgraph=depsgraph) for obj in meshes}
surface_checks = {}
for obj in meshes:
    posed = skinned_data[obj.name]
    assert len(posed.vertices) == len(obj.data.vertices)
    # Measure geometry change before rebinding. The main movement should be a rigid swing.
    ratios = []
    for edge in obj.data.edges:
        a, b = edge.vertices
        length = (obj.data.vertices[a].co - obj.data.vertices[b].co).length
        if length > 0.05:
            ratios.append((posed.vertices[a].co - posed.vertices[b].co).length / length)
    ratios.sort()
    surface_checks[obj.name] = [ratios[len(ratios) // 100], ratios[len(ratios) * 99 // 100]]
    assert surface_checks[obj.name][0] > 0.65 and surface_checks[obj.name][1] < 1.45, surface_checks

positions = {target: reposed[source].translation for target, source in reverse.items()}
positions["spine_03"] = positions["spine_02"].lerp(positions["spine_04"], 0.5)
positions["neck_02"] = positions["neck_01"].lerp(positions["head"], 0.5)
for s in "lr":
    for digit in ("index", "middle", "ring", "pinky"):
        positions[f"{digit}_metacarpal_{s}"] = positions[f"hand_{s}"].lerp(positions[f"{digit}_01_{s}"], 0.35)
    for bone, child in (("upperarm", "lowerarm"), ("lowerarm", "hand"), ("thigh", "calf"), ("calf", "foot")):
        for index in (1, 2):
            positions[f"{bone}_twist_{index:02}_{s}"] = positions[f"{bone}_{s}"].lerp(positions[f"{child}_{s}"], index / 3)
    positions[f"ik_foot_{s}"] = positions[f"foot_{s}"]
    positions[f"ik_hand_{s}"] = positions[f"hand_{s}"]
positions["ik_hand_gun"] = positions["hand_r"]

# Preserve weighted nonstandard bones and their transform ancestors; collapse only
# the unmapped, unweighted legacy framework. Wing chains keep their original axes.
used = {obj.vertex_groups[g.group].name for obj in meshes for v in obj.data.vertices for g in v.groups if g.weight > 0}
wing = {bone.name for bone in legacy.data.bones if bone.name == "a_01" or
        any(parent.name == "a_01" for parent in bone.parent_recursive)}
extra = (used | wing) - set(mapping)
assert extra <= set(old)
legacy.name = "WingerSource"
data = bpy.data.armatures.new("WingerUE5")
rig = bpy.data.objects.new("Armature", data)
scene.collection.objects.link(rig)
bpy.ops.object.select_all(action="DESELECT")
rig.select_set(True)
bpy.context.view_layer.objects.active = rig
bpy.ops.object.mode_set(mode="EDIT")
root = data.edit_bones.new("root")
root.head = old["root"].translation
root.tail = root.head + old["root"].to_3x3().col[1].normalized() * 12
root.roll = 0
root.use_deform = False
for name, (matrix, parent) in standard.items():
    bone = data.edit_bones.new(name)
    bone.length = 4
    bone.matrix = matrix
    bone.use_deform = name in reverse
    bone.head = positions.get(name, matrix.translation)
    bone.tail = bone.head + matrix.to_3x3().col[1].normalized() * 4
for name, (_, parent) in standard.items():
    data.edit_bones[name].parent = data.edit_bones[parent]
for name in sorted(extra):
    bone = data.edit_bones.new(name)
    bone.length = legacy.data.bones[name].length
    bone.matrix = reposed[name]
    bone.use_deform = name in used
for name in sorted(extra):
    parent = legacy.data.bones[name].parent
    while parent and parent.name not in mapping and parent.name not in extra:
        parent = parent.parent
    data.edit_bones[name].parent = data.edit_bones[mapping.get(parent.name, parent.name)] if parent else root
bpy.ops.object.mode_set(mode="OBJECT")

max_error = 0.0
for obj in meshes:
    obj.data = skinned_data[obj.name]
    obj.parent = rig
    obj.matrix_parent_inverse = Matrix.Identity(4)
    for modifier in obj.modifiers:
        if modifier.type == "ARMATURE":
            modifier.object = rig
    for group in obj.vertex_groups:
        if group.name in mapping:
            group.name = mapping[group.name]
    assert {obj.vertex_groups[g.group].name for v in obj.data.vertices for g in v.groups if g.weight > 0} <= set(data.bones.keys())
    bpy.context.view_layer.update()
    evaluated = obj.evaluated_get(bpy.context.evaluated_depsgraph_get()).data
    max_error = max(max_error, max((a.co - b.co).length for a, b in zip(evaluated.vertices, obj.data.vertices)))
assert max_error < 0.005, max_error

def export(path, meshes=(), animated=False):
    bpy.ops.object.select_all(action="DESELECT")
    for obj in (rig, *meshes):
        obj.select_set(True)
    bpy.context.view_layer.objects.active = rig
    assert bpy.ops.export_scene.fbx(
        filepath=str(path), use_selection=True, object_types={"ARMATURE", "MESH"},
        apply_unit_scale=True, apply_scale_options="FBX_SCALE_NONE",
        axis_forward="-Y", axis_up="Z", add_leaf_bones=False, use_armature_deform_only=False,
        primary_bone_axis="Y", secondary_bone_axis="X", bake_anim=animated,
        bake_anim_use_all_actions=False, bake_anim_use_nla_strips=False,
        bake_anim_step=1, bake_anim_simplify_factor=0, path_mode="STRIP",
        use_tspace=bool(meshes), mesh_smooth_type="FACE") == {"FINISHED"}

export(args.output / "SK_Winger_UE5.fbx", meshes)
clip_names = [f"A_Winger_Wings_{name}" for name in ("idle", "walkN", "run", "Jump", "Land")]
for name in clip_names:
    original = bpy.data.actions.get(name)
    assert original, name
    start, end = (round(value) for value in original.frame_range)
    legacy.animation_data_create().action = original
    legacy.animation_data.action_slot, = original.slots
    action = bpy.data.actions.new(name + "_UE5")
    action.use_fake_user = True
    rig.animation_data_create().action = action
    for frame in range(start, end + 1):
        scene.frame_set(frame)
        for bone_name in sorted(wing):
            pose = rig.pose.bones[bone_name]
            pose.rotation_mode = "QUATERNION"
            pose.matrix_basis = legacy.pose.bones[bone_name].matrix_basis
            pose.keyframe_insert("location", frame=frame, group=bone_name)
            pose.keyframe_insert("rotation_quaternion", frame=frame, group=bone_name)
    scene.frame_start, scene.frame_end = start, end
    scene.frame_set(start)
    assert rig.pose.bones["a_01"].matrix_basis.to_quaternion().angle > 0.01, name
    export(args.output / (action.name + ".fbx"), animated=True)
    rig.animation_data.action = None
    for pose in rig.pose.bones:
        pose.matrix_basis = Matrix.Identity(4)

bpy.data.objects.remove(legacy, do_unlink=True)
for action in list(bpy.data.actions):
    if action.name not in {name + "_UE5" for name in clip_names}:
        bpy.data.actions.remove(action)
scene.frame_start, scene.frame_end = 0, 120
scene.frame_set(0)
bpy.data.orphans_purge(do_recursive=True)
for image in bpy.data.images:
    if image.source == "FILE" and image.filepath.startswith("//"):
        image.filepath = str((args.source.parent / image.filepath[2:]).resolve())
        if Path(image.filepath).is_file():
            image.pack()
bpy.context.preferences.filepaths.save_version = 0
bpy.ops.wm.save_as_mainfile(filepath=str(args.output / "Winger_UE5.blend"), compress=True)
(args.output / "BindReport.json").write_text(json.dumps({
    "bone_count": len(data.bones), "core_count": len(standard) + 1, "wing_bone_count": len(wing),
    "vertex_counts": {obj.name: len(obj.data.vertices) for obj in meshes},
    "max_rebind_position_error_cm": max_error, "edge_length_ratio_p01_p99": surface_checks,
}, indent=2))
