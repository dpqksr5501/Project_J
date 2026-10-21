"""Read-only compressed-pose contracts for the Manny locomotion conversion.

Run in an editor (or Python commandlet). Checks helper tracking on the actual
library at authored keyframes, and source-relative knee flexion over cycles.
At fractional frames, independently interpolated local FK rotations and helper
translations need not produce identical component positions; report that error
separately instead of treating it as a missing helper track.
Selection/Slot tests alone cannot detect these conversion defects.
The independent source samples were captured before archival; runtime tests do
not require loading the retired UEFN animation bank.
"""
import datetime
import json
import math
import traceback
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'Authoring'))
from player_animation_library import load_catalog, motion_fingerprint, sequence_contract

directory=Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir()))/'Validation/PlayerLocomotion'
directory.mkdir(parents=True,exist_ok=True)
output=directory/('MannyKinematics_'+datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f')+'.json')
report={'errors':[],'sequences':0,'pose_samples':0,'max_helper_error_cm':0.,'max_interpolated_helper_error_cm':0.,'cycles':[]}

def point(pose,bone):return unreal.AnimPoseExtensions.get_bone_pose(pose,bone,unreal.AnimPoseSpaces.WORLD).translation
def flex(pose,side):
    a=point(pose,'calf_'+side)-point(pose,'thigh_'+side)
    b=point(pose,'foot_'+side)-point(pose,'calf_'+side)
    return math.degrees(math.acos(max(-1.,min(1.,a.dot(b)/(a.length()*b.length())))))
def options(mesh):
    result=unreal.AnimPoseEvaluationOptions()
    result.optional_skeletal_mesh=mesh
    result.evaluation_type=unreal.AnimDataEvalType.COMPRESSED
    result.retrieve_additive_as_full_pose=True
    return result

try:
    target=unreal.load_asset('/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple')
    catalog=load_catalog()
    expected={r['target_package']:r for r in catalog['entries'] if r['class']=='AnimSequence'}
    target_options=options(target)
    registry=unreal.AssetRegistryHelpers.get_asset_registry();registry.wait_for_completion()
    for data in registry.get_assets_by_path('/Game/Characters/Mannequins/Animations',True):
        if str(data.asset_class_path.asset_name)!='AnimSequence':continue
        path=str(data.package_name)
        if not any(path.startswith('/Game/Characters/Mannequins/Animations/'+p)for p in ['Locomotion/','ProjectJump/']):continue
        seq=data.get_asset();report['sequences']+=1
        assert path in expected, 'Uncataloged animation: '+path
        assert motion_fingerprint(seq)==expected[path]['target_motion_fingerprint'], 'Raw motion/bind-pose contract drift: '+path
        assert sequence_contract(seq)==expected[path]['target_contract'], 'Playback/notify contract drift: '+path
        model=seq.get_editor_property('data_model_interface');rate=model.get_frame_rate()
        frames_per_second=rate.numerator/rate.denominator
        for phase in [0.,.25,.5,.75,1.]:
            time=unreal.AnimationLibrary.get_sequence_length(seq)*phase
            frame=min(model.get_number_of_frames(),round(time*frames_per_second))
            pose=unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,frame/frames_per_second,target_options)
            interpolated=unreal.AnimPoseExtensions.get_anim_pose_at_time(seq,time,target_options)
            assert unreal.AnimPoseExtensions.is_valid(pose),path
            report['pose_samples']+=1
            for fk,ik in [('foot_l','ik_foot_l'),('foot_r','ik_foot_r'),('hand_l','ik_hand_l'),('hand_r','ik_hand_r')]:
                error=(point(pose,fk)-point(pose,ik)).length()
                assert math.isfinite(error),path
                report['max_helper_error_cm']=max(report['max_helper_error_cm'],error)
                interpolated_error=(point(interpolated,fk)-point(interpolated,ik)).length()
                if interpolated_error>report['max_interpolated_helper_error_cm']:
                    report['max_interpolated_helper_error_cm']=interpolated_error
                    report['worst_interpolated_sample']={'asset':path,'helper':ik,'phase':phase,'time':time,'nearest_authored_frame':frame,'additive':str(seq.get_editor_property('additive_anim_type'))}
                if error>.2:
                    report['errors'].append({'asset':path,'phase':phase,'helper':ik,'error_cm':error})
    assert report['sequences']==len(expected),'Unexpected cataloged library size: '+str(report['sequences'])
    for baseline in catalog['reference_cycles']:
        converted=unreal.load_asset(baseline['target_package'])
        row={'clip':baseline['clip'],'source_package_sha256':baseline['source_package_sha256'],
             'max_knee_difference_degrees':0.,'minimum_source_flex':180.,'minimum_target_flex':180.}
        for sample in baseline['samples']:
            b=unreal.AnimPoseExtensions.get_anim_pose_at_time(converted,unreal.AnimationLibrary.get_sequence_length(converted)*sample['phase'],target_options)
            for side,label in [('l','left'),('r','right')]:
                source_flex=sample[label];target_flex=flex(b,side);delta=abs(source_flex-target_flex)
                row['max_knee_difference_degrees']=max(row['max_knee_difference_degrees'],delta)
                row['minimum_source_flex']=min(row['minimum_source_flex'],source_flex)
                row['minimum_target_flex']=min(row['minimum_target_flex'],target_flex)
        if row['max_knee_difference_degrees']>3.:report['errors'].append(row)
        report['cycles'].append(row)
    report['completed']=not report['errors']
except Exception:
    report['completed']=False;report['error']=traceback.format_exc();unreal.log_error(report['error'])
output.write_text(json.dumps(report,indent=2),encoding='utf-8')
unreal.log('Manny retarget kinematics: '+str(output)+' Success='+str(report['completed']))
