"""Export this rig's 19 modular FBXs, excluding animation controls and widgets."""
from pathlib import Path
from contextlib import contextmanager
import bpy


@contextmanager
def export_selection(objects):
    """FBX selection honours collection exclusion, visibility and selectability too."""
    layer = bpy.context.view_layer
    active, selected = layer.objects.active, list(bpy.context.selected_objects)
    mode = active.mode if active else 'OBJECT'
    layers = []
    def visit(node):
        contains = any(o.name in node.collection.all_objects for o in objects)
        if contains:
            layers.append((node, node.exclude, node.hide_viewport))
        for child in node.children:
            visit(child)
    visit(layer.layer_collection)
    collections = {node.collection for node, _, _ in layers}
    collection_flags = [(c, c.hide_viewport, c.hide_select) for c in collections]
    object_flags = [(o, o.hide_get(), o.hide_viewport, o.hide_select) for o in objects]
    try:
        if active and mode != 'OBJECT':
            bpy.ops.object.mode_set(mode='OBJECT')
        for node, _, _ in layers:
            node.exclude = node.hide_viewport = False
        for collection, _, _ in collection_flags:
            collection.hide_viewport = collection.hide_select = False
        for obj, _, _, _ in object_flags:
            obj.hide_viewport = obj.hide_select = False
            obj.hide_set(False)
        layer.update()
        bpy.ops.object.select_all(action='DESELECT')
        for obj in objects:
            obj.select_set(True)
        layer.objects.active = objects[0]
        assert set(bpy.context.selected_objects) == set(objects), 'Incomplete FBX selection'
        for obj in objects:
            if obj.type == 'MESH':
                assert len(obj.data.vertices) and len(obj.data.polygons), obj.name
                assert any(v.groups for v in obj.data.vertices), 'Unweighted mesh: ' + obj.name
        yield
    finally:
        bpy.ops.object.select_all(action='DESELECT')
        for obj in selected:
            obj.select_set(True)
        layer.objects.active = active
        for obj, hidden, viewport, select in object_flags:
            obj.hide_set(hidden)
            obj.hide_viewport, obj.hide_select = viewport, select
        for collection, viewport, select in collection_flags:
            collection.hide_viewport, collection.hide_select = viewport, select
        for node, excluded, hidden in reversed(layers):
            node.hide_viewport, node.exclude = hidden, excluded
        if active and mode != 'OBJECT':
            bpy.ops.object.mode_set(mode=mode)


def export_meshes(directory=None, prefix='SK_Spellblade_'):
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
    paths = []
    for key, objects in groups.items():
        with export_selection([arm] + objects):
            path = directory / (prefix + key + '.fbx')
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
    return paths


if __name__ == '__main__':
    export_meshes()
