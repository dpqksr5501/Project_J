"""Export independent weapon rigs without copying the 318-bone character graph.

Preserves weapon skin and auxiliary bones. Bind geometry is expressed in the
original drawn/sheath mount's local space; no posed/world-space bake is used.
Run from the UE5 character .blend. Source objects and their skin are unchanged.
"""
from pathlib import Path
import json
import bpy
from mathutils import Matrix
from ExportMeshes import export_selection


def export_weapon_rig(directory):
    directory=Path(directory)
    directory.mkdir(parents=True,exist_ok=True)
    source=bpy.data.objects['Armature']
    source_name=source.name
    root_name='Bip_Weapon_R'
    names=[b.name for b in source.data.bones
           if b.name==root_name or b.name.startswith(('Bip_Weapon_R_0','B_Add_b_'))]
    assert len(names)==8, names
    drawn_inverse=source.data.bones[root_name].matrix_local.inverted()
    collection=bpy.data.collections.new('WeaponExportTemporary')
    bpy.context.scene.collection.children.link(collection)
    data=bpy.data.armatures.new('SpellbladeWeaponRig')
    # UE's legacy importer removes Blender's conventional Armature object node.
    # A custom object name otherwise inserts an unrelated ninth root bone.
    source.name='CharacterAuthoringArmature'
    arm=bpy.data.objects.new('Armature',data)
    collection.objects.link(arm)
    objects=[arm]
    mesh_data=[]
    report=[]
    try:
        with export_selection([arm]):
            bpy.ops.object.mode_set(mode='EDIT')
            for name in names:
                original=source.data.bones[name]
                bone=data.edit_bones.new(name)
                bone.head=drawn_inverse @ original.head_local
                bone.tail=drawn_inverse @ original.tail_local
                bone.matrix=drawn_inverse @ original.matrix_local
                bone.length=original.length
            for name in names:
                parent=source.data.bones[name].parent
                if parent and parent.name in names:
                    data.edit_bones[name].parent=data.edit_bones[parent.name]
            bpy.ops.object.mode_set(mode='OBJECT')
        assert len(data.bones)==8
        for key in ('longblade','longblade_in','aiguillette','aiguillette_in'):
            original=bpy.data.objects['weapon_'+key+'_00']
            mount='Bip_Weapon_R_Attach_In' if key.endswith('_in') else root_name
            inverse=source.data.bones[mount].matrix_local.inverted()
            mesh=original.data.copy();mesh_data.append(mesh)
            mesh.transform(inverse @ source.matrix_world.inverted() @ original.matrix_world)
            obj=original.copy();obj.data=mesh;objects.append(obj)
            obj.parent=None;obj.matrix_world=Matrix.Identity(4)
            obj.animation_data_clear()
            for modifier in list(obj.modifiers):obj.modifiers.remove(modifier)
            collection.objects.link(obj)
            if mount!=root_name:
                assert len(obj.vertex_groups)==1 and obj.vertex_groups[0].name==mount
                obj.vertex_groups[0].name=root_name
            assert all(g.name in names for g in obj.vertex_groups)
            modifier=obj.modifiers.new('WeaponSkin','ARMATURE');modifier.object=arm
            path=directory/('SK_Spellblade_'+key+'.fbx')
            with export_selection([arm,obj]):
                status=bpy.ops.export_scene.fbx(filepath=str(path),check_existing=False,
                    use_selection=True,object_types={'ARMATURE','MESH'},
                    apply_unit_scale=True,apply_scale_options='FBX_SCALE_NONE',
                    axis_forward='-Y',axis_up='Z',add_leaf_bones=False,
                    use_armature_deform_only=False,armature_nodetype='NULL',
                    primary_bone_axis='Y',secondary_bone_axis='X',bake_anim=False,
                    path_mode='STRIP',use_mesh_modifiers=True,use_tspace=True,mesh_smooth_type='FACE')
                assert status=={'FINISHED'}
            report.append({'file':path.name,'vertices':len(mesh.vertices),
                           'weighted_groups':[g.name for g in obj.vertex_groups],
                           'source_mount':mount,'bones':names,
                           'source_to_mount':[list(row) for row in inverse]})
            bpy.data.objects.remove(obj,do_unlink=True);objects.remove(obj)
        (directory/'ExportManifest.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        return report
    finally:
        if bpy.context.object and bpy.context.object.mode!='OBJECT':bpy.ops.object.mode_set(mode='OBJECT')
        for obj in objects:bpy.data.objects.remove(obj,do_unlink=True)
        for mesh in mesh_data:
            if mesh.users==0:bpy.data.meshes.remove(mesh)
        if data.users==0:bpy.data.armatures.remove(data)
        bpy.data.collections.remove(collection)
        source.name=source_name
