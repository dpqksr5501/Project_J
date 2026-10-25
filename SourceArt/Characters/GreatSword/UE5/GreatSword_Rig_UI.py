"""Run once in Blender's Text Editor after opening GreatSword_UE5_Rig.blend.
Adds the GreatSword sidebar with IK/FK snap, visibility, and pose reset tools.
No installation or security preference changes are needed.
"""
import bpy,json,math
from mathutils import Matrix,Vector
from pathlib import Path

LIMBS={
 'arm_l':['upperarm_l','lowerarm_l','hand_l'],
 'arm_r':['upperarm_r','lowerarm_r','hand_r'],
 'leg_l':['thigh_l','calf_l','foot_l'],
 'leg_r':['thigh_r','calf_r','foot_r']}

def update(rig):
    rig.update_tag();bpy.context.view_layer.update()

def reset(rig):
    for p in rig.pose.bones:
        stored=p.get('default_basis')
        p.matrix_basis=Matrix([stored[i:i+4] for i in range(0,16,4)]) if stored else Matrix.Identity(4)
    rig['weapon_reference_independent']=False
    for key in LIMBS:
        rig[key+'_ik_fk']=0.0
        rig[key+'_pole_angle']=rig.get(key+'_default_pole_angle',0.0)
    update(rig)

def snap(rig,limb,to_ik):
    chain=LIMBS[limb]
    update(rig)
    matrices=[rig.pose.bones['RES_'+n].matrix.copy() for n in chain]
    if to_ik:
        rig.pose.bones['IK_'+limb].matrix=matrices[2]
        p0,p1,p2=[m.translation for m in matrices]
        axis=(p2-p0).normalized();proj=p0+axis*(p1-p0).dot(axis);normal=p1-proj
        if normal.length<.01:
            normal=rig.pose.bones['POLE_'+limb].matrix.translation-proj
            normal-=axis*normal.dot(axis)
        pole=rig.pose.bones['POLE_'+limb].matrix.copy()
        pole.translation=p1+normal.normalized()*50
        rig.pose.bones['POLE_'+limb].matrix=pole
        update(rig)
        # Fit the bend plane to the current middle joint, including near-straight knees.
        def error(angle):
            rig[limb+'_pole_angle']=angle;update(rig)
            return (rig.pose.bones['MCH_'+limb+'_1'].head-p1).length
        best=min((error(-math.pi+2*math.pi*i/72),-math.pi+2*math.pi*i/72) for i in range(72))
        center=best[1]
        for step in [.01,.001,.0001]:
            best=min((error(center+i*step),center+i*step) for i in range(-10,11));center=best[1]
        error(center)
        # Match axial rotation as well as joint position. These offsets remain
        # parented to the solved IK chain, so subsequent IK movement stays live.
        for name,matrix in zip(chain,matrices):
            rig.pose.bones['IKRES_'+name].matrix=matrix;update(rig)
        rig[limb+'_ik_fk']=1.0
    else:
        for n,m in zip(chain,matrices):
            rig.pose.bones['FK_'+n].matrix=m;update(rig)
        rig[limb+'_ik_fk']=0.0
    update(rig)

def set_weapon_reference_mode(rig, independent):
    """Snap into independent authoring without changing the evaluated weapon pose.
    Return to wrist-follow deliberately; animation should key the mode explicitly.
    """
    update(rig)
    matrix=rig.pose.bones['RES_ik_hand_gun'].matrix.copy()
    rig['weapon_reference_independent']=bool(independent)
    update(rig)
    if independent:
        rig.pose.bones['FK_ik_hand_gun'].matrix=matrix
        update(rig)

class GREATSWORD_OT_weapon_reference(bpy.types.Operator):
    bl_idname='greatsword.weapon_reference';bl_label='Weapon reference mode';bl_options={'REGISTER','UNDO'}
    independent:bpy.props.BoolProperty()
    def execute(self,context):
        set_weapon_reference_mode(bpy.data.objects['CTRL_GreatSword'],self.independent)
        return {'FINISHED'}

class GREATSWORD_OT_snap(bpy.types.Operator):
    bl_idname='greatsword.snap';bl_label='Match IK/FK';bl_options={'REGISTER','UNDO'}
    limb:bpy.props.StringProperty()
    to_ik:bpy.props.BoolProperty()
    def execute(self,context):
        rig=bpy.data.objects.get('CTRL_GreatSword')
        snap(rig,self.limb,self.to_ik)
        return {'FINISHED'}

class GREATSWORD_OT_reset(bpy.types.Operator):
    bl_idname='greatsword.reset_pose';bl_label='Reset all controls';bl_options={'REGISTER','UNDO'}
    def execute(self,context):
        rig=bpy.data.objects.get('CTRL_GreatSword')
        reset(rig);return {'FINISHED'}

class GREATSWORD_OT_key(bpy.types.Operator):
    bl_idname='greatsword.key_pose';bl_label='Key controls and IK/FK';bl_options={'REGISTER','UNDO'}
    def execute(self,context):
        rig=bpy.data.objects.get('CTRL_GreatSword')
        for p in rig.pose.bones:
            if p.name.startswith(('FK_','IK_','IKRES_','POLE_','AUX_')):
                p.keyframe_insert('location',group=p.name)
                p.keyframe_insert('rotation_euler',group=p.name)
        rig.keyframe_insert('["weapon_reference_independent"]',group='Weapon Reference')
        for key in LIMBS:
            rig.keyframe_insert('["'+key+'_ik_fk"]',group='IK FK Switches')
            rig.keyframe_insert('["'+key+'_pole_angle"]',group='IK Pole Angles')
        return {'FINISHED'}

class GREATSWORD_PT_rig(bpy.types.Panel):
    bl_label='GreatSword Rig';bl_idname='GREATSWORD_PT_rig'
    bl_space_type='VIEW_3D';bl_region_type='UI';bl_category='GreatSword'
    @classmethod
    def poll(cls,context):return bpy.data.objects.get('CTRL_GreatSword') is not None and context.scene.name=='GreatSword_UE5_Rig'
    def draw(self,context):
        layout=self.layout;rig=bpy.data.objects['CTRL_GreatSword']
        layout.label(text='0 = FK   /   1 = IK')
        for key in LIMBS:
            box=layout.box();box.prop(rig,'["'+key+'_ik_fk"]',text=key.replace('_',' ').title(),slider=True)
            row=box.row(align=True)
            op=row.operator('greatsword.snap',text='Match to IK');op.limb=key;op.to_ik=True
            op=row.operator('greatsword.snap',text='Match to FK');op.limb=key;op.to_ik=False
        box=layout.box();box.label(text='Weapon: independent' if rig.get('weapon_reference_independent',False) else 'Weapon: wrist reference')
        row=box.row(align=True)
        op=row.operator('greatsword.weapon_reference',text='Snap independent');op.independent=True
        op=row.operator('greatsword.weapon_reference',text='Follow wrist');op.independent=False
        layout.label(text='Control visibility')
        for name in ['Main FK','IK and Poles','Face','Cloth and Hair','Equipment and Correctives']:
            col=rig.data.collections.get(name)
            if col:layout.prop(col,'is_visible',text=name)
        layout.operator('greatsword.key_pose')
        layout.operator('greatsword.reset_pose')
        box=layout.box();box.label(text='Modular parts')
        for col in context.scene.collection.children:
            if col.name.startswith('PART_'):box.prop(col,'hide_viewport',text='Hide '+col.name[5:])

CLASSES=[GREATSWORD_OT_weapon_reference,GREATSWORD_OT_snap,GREATSWORD_OT_reset,GREATSWORD_OT_key,GREATSWORD_PT_rig]
def register():
    for cls in CLASSES:
        previous=getattr(bpy.types,cls.__name__,None)
        if previous:bpy.utils.unregister_class(previous)
        bpy.utils.register_class(cls)
if __name__=='__main__':register()
