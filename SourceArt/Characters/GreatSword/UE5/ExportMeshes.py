"""Export this rig's 19 modular FBXs, excluding animation controls and widgets."""
from pathlib import Path
import bpy


def export_meshes(directory=None):
    directory = Path(directory) if directory else Path(bpy.data.filepath).parent / 'FBX'
    directory.mkdir(parents=True, exist_ok=True)
    arm = bpy.data.objects['Armature']
    assert len(arm.data.bones) == 318
    meshes = [o for o in bpy.data.objects if o.type == 'MESH'
              and any(m.type == 'ARMATURE' and m.object == arm for m in o.modifiers)]
    base = ['body', 'head', 'hand', 'cloak', 'foot', 'inner', 'upper', 'lower', 'sho', 'hel']
    variants = base + ['cloak_damage', 'lower_damage', 'sho_damage', 'upper_damage',
                       'weapon_aiguillette', 'weapon_aiguillette_in',
                       'weapon_longblade', 'weapon_longblade_in']
    groups = {key: [o for o in meshes if o.name.rsplit('_', 1)[0] == key] for key in variants}
    assert all(groups.values())
    groups = {'Complete': [o for key in base for o in groups[key]], **groups}
    active = bpy.context.view_layer.objects.active
    selected = list(bpy.context.selected_objects)
    hidden = {o.name: o.hide_get() for o in [arm] + meshes}
    paths = []
    try:
        if bpy.context.object and bpy.context.object.mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        for key, objects in groups.items():
            bpy.ops.object.select_all(action='DESELECT')
            for obj in [arm] + objects:
                obj.hide_set(False)
                obj.select_set(True)
            bpy.context.view_layer.objects.active = arm
            path = directory / ('SK_GreatSword_' + key + '.fbx')
            status = bpy.ops.export_scene.fbx(
                filepath=str(path), check_existing=False, use_selection=True,
                object_types={'ARMATURE', 'MESH'}, apply_unit_scale=True,
                apply_scale_options='FBX_SCALE_NONE', axis_forward='-Y', axis_up='Z',
                add_leaf_bones=False, use_armature_deform_only=False,
                armature_nodetype='NULL', primary_bone_axis='Y', secondary_bone_axis='X',
                bake_anim=False, path_mode='STRIP', use_mesh_modifiers=True,
                use_tspace=True, mesh_smooth_type='FACE')
            assert status == {'FINISHED'}, str(path)
            paths.append(str(path))
    finally:
        bpy.ops.object.select_all(action='DESELECT')
        for obj in [arm] + meshes:
            obj.hide_set(hidden[obj.name])
        for obj in selected:
            obj.select_set(True)
        bpy.context.view_layer.objects.active = active
    return paths


if __name__ == '__main__':
    export_meshes()
