"""Run in a PIE with a local BP_GreatSword on an open, flat test floor.

Measures the evaluated source mesh, not just Slot connectivity. Does not save assets.
The callback restores combat/walk state and stops its montage on success or failure.
"""
import datetime
import json
import math
import re
from pathlib import Path
import traceback
import unreal
import sys
sys.path.insert(0,str(Path(__file__).parent))
from PlayerLocomotionRegression import local_inputs

worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
worlds = [w for w in worlds if unreal.GameplayStatics.get_player_pawn(w, 0)
          and unreal.GameplayStatics.get_player_pawn(w, 0).is_locally_controlled()]
assert len(worlds) == 1, 'Expected one local player world'
world = worlds[0]
pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
mesh = pawn.get_source_mesh()
anim = mesh.get_anim_instance()
assert isinstance(anim, unreal.Project_JPlayerAnimInstance)
assert not pawn.get_current_montage(), 'Finish the current action first'
was_combat = pawn.is_combat_mode_active()
inputs=local_inputs(world)
combat_action=unreal.load_asset('/Game/Input/Actions/IA_ToggleCombat')

names = ['LMB1', 'LMB2', 'Q1_or_LR2', 'Q2', 'R', 'T']
montages = [unreal.load_asset('/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/AM_Greatsword_' + n) for n in names]
assert all(montages)
bones = ['thigh_l', 'calf_l', 'foot_l', 'thigh_r', 'calf_r', 'foot_r']
body_contact = bool(globals().get('PLAYER_MONTAGE_VALIDATE_BODY_CONTACT', False))
sole_clearance = bool(globals().get('PLAYER_MONTAGE_VALIDATE_SOLE_CLEARANCE', False))
assert not sole_clearance or body_contact, 'Sole validation requires the direct body contact fixture'
if body_contact:
    # A direct body's contact solver may move the pelvis/thighs while retaining
    # the authored foot anchors. Measure its uncorrected input and final feet;
    # requiring identical post-IK local leg rotations would reject the feature.
    post = mesh.get_post_process_instance()
    assert isinstance(post, unreal.Project_JRetargetAnimInstance)
    assert not post.get_editor_property('RetargetBodyProfile'), 'Expected a direct body, not a retarget follower'
    assert all(mesh.does_socket_exist(n) for n in ['ik_foot_l', 'ik_foot_r'])
options = unreal.AnimPoseEvaluationOptions()
options.set_editor_property('optional_skeletal_mesh', mesh.get_skeletal_mesh_asset())
options.set_editor_property('evaluation_type', unreal.AnimDataEvalType.COMPRESSED)
options.set_editor_property('extract_root_motion', True)
options.set_editor_property('incorporate_root_motion_into_pose', False)
directory = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'Validation/PlayerLocomotion'
directory.mkdir(parents=True, exist_ok=True)
output = directory / ('MontagePose_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.json')
report = {'samples': [], 'checks': {}, 'completed': False,
          'validation_mode': 'DirectBodySoleAndFinalFeet' if sole_clearance else 'DirectBodyInputAndFinalFeet' if body_contact else 'FinalLocalLegRotations'}
start = unreal.GameplayStatics.get_time_seconds(world)
last_case = -1
handle = None


def finish(error=None):
    unreal.unregister_slate_post_tick_callback(handle)
    pawn.stop_anim_montage()
    if pawn.is_combat_mode_active() != was_combat:
        inputs.inject_input_vector_for_action(combat_action,unreal.Vector(1,0,0),[],[])
    report['completed'] = error is None
    if error:
        report['error'] = error
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    if error:
        unreal.log_error('PlayerLocomotion montage pose regression failed: ' + str(output))
    else:
        unreal.log('PlayerLocomotion montage pose regression passed: ' + str(output))


def tick(_):
    global last_case
    try:
        elapsed = unreal.GameplayStatics.get_time_seconds(world) - start
        inputs.inject_input_vector_for_action(combat_action,unreal.Vector(float(.2<=elapsed<.35 and not pawn.is_combat_mode_active()),0,0),[],[])
        if elapsed >= 28.:
            for montage in montages:
                samples = [s for s in report['samples'] if s['asset'] == montage.get_name()]
                assert len(samples) >= 8, 'Insufficient full-weight pose samples: ' + montage.get_name()
                maximum = max(max(s['errors'].values()) for s in samples)
                report['checks'][montage.get_name()] = {'samples': len(samples), 'max_error_degrees': maximum}
                assert maximum < .2, 'Authored pose/contact foot rotation changed: ' + montage.get_name()
                if body_contact:
                    position = max(max(s['position_errors_cm'].values()) for s in samples)
                    report['checks'][montage.get_name()]['max_position_error_cm'] = position
                    if sole_clearance:
                        # This flat-floor fixture permits only the minimal upward sole solve
                        # before the input markers. The hand/body solver must still pin them.
                        lifts = [d for s in samples for d in s['sole_lifts_cm'].values()]
                        assert all(abs(d[0]) < .2 and abs(d[1]) < .2 and d[2] > -.2 for d in lifts), 'Sole solve changed the authored horizontal foot anchor'
                        assert all(abs(d[2]-s['solver_applied_lifts_cm'][bone]) < .2 for s in samples for bone,d in s['sole_lifts_cm'].items()), 'Input foot delta differs from the measured sole correction'
                        assert all(v < .2 for s in samples for k,v in s['position_errors_cm'].items() if k.startswith('Contact_')), 'Final contact moved the corrected foot anchor'
                        report['checks'][montage.get_name()]['max_sole_lift_cm'] = max(d[2] for d in lifts)
                    else:
                        assert position < .2, 'Authored input/final contact foot position changed: ' + montage.get_name()
                assert len({str(s['base_playback']) for s in samples}) > 1, 'GASP source clock froze under the skill: ' + montage.get_name()
            assert not pawn.get_current_montage(), 'Cancelled montage failed to release'
            assert anim.blueprint_get_slot_montage_local_weight('DefaultSlot') < .01, 'Slot blend-out failed'
            finish()
            return
        case = int((elapsed - 2.) / 4.) if elapsed >= 2. else -1
        if case >= len(montages):
            if last_case != case:
                pawn.stop_anim_montage()
                last_case = case
            return
        if case >= 0 and case != last_case:
            pawn.play_anim_montage(montages[case], 1., '')
            last_case = case
        active = pawn.get_current_montage()
        if not active or anim.blueprint_get_slot_montage_local_weight('DefaultSlot') < .9999:
            return
        time = anim.montage_get_position(active)
        # Slot weight can remain 1 while two consecutive montages crossfade.
        if time < .25 or (report['samples'] and elapsed - report['samples'][-1]['elapsed'] < .05):
            return
        segment = active.get_editor_property('SlotAnimTracks')[0].get_editor_property('AnimTrack').get_editor_property('AnimSegments')[0]
        sequence = segment.get_editor_property('AnimReference')
        source_time = segment.get_editor_property('AnimStartTime') + (time - segment.get_editor_property('StartPos')) * segment.get_editor_property('AnimPlayRate')
        pose = unreal.AnimPoseExtensions.get_anim_pose_at_time(sequence, source_time, options)
        errors = {}
        positions = {}
        sole_lifts = {}
        for bone in ([] if body_contact else bones):
            actual = unreal.MathLibrary.make_relative_transform(mesh.get_socket_transform(bone, unreal.RelativeTransformSpace.RTS_COMPONENT), mesh.get_socket_transform(mesh.get_parent_bone(bone), unreal.RelativeTransformSpace.RTS_COMPONENT)).rotation
            expected = unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.LOCAL).rotation
            dot = min(1., abs(actual.x * expected.x + actual.y * expected.y + actual.z * expected.z + actual.w * expected.w))
            errors[bone] = math.degrees(2. * math.acos(dot))
        if body_contact:
            root_transform = mesh.get_socket_transform('root', unreal.RelativeTransformSpace.RTS_COMPONENT)
            expected_root = unreal.AnimPoseExtensions.get_bone_pose(pose, 'root', unreal.AnimPoseSpaces.WORLD)
            for side in ['l', 'r']:
                bone = 'foot_' + side
                marker = mesh.get_socket_transform('ik_' + bone, unreal.RelativeTransformSpace.RTS_COMPONENT)
                final = mesh.get_socket_transform(bone, unreal.RelativeTransformSpace.RTS_COMPONENT)
                expected = unreal.MathLibrary.make_relative_transform(
                    unreal.AnimPoseExtensions.get_bone_pose(pose, bone, unreal.AnimPoseSpaces.WORLD), expected_root)
                incoming = unreal.MathLibrary.make_relative_transform(marker, root_transform)
                if sole_clearance:
                    sole_lifts[bone] = (incoming.translation-expected.translation).to_tuple()
                for label, actual, target in [('Input_' + bone, incoming, expected), ('Contact_' + bone, final, marker)]:
                    a, b = actual.rotation, target.rotation
                    dot = min(1., abs(a.x*b.x+a.y*b.y+a.z*b.z+a.w*b.w))
                    errors[label] = math.degrees(2.*math.acos(dot))
                    positions[label] = (actual.translation-target.translation).length()
        base = re.findall(r'Index=(\d+) Asset=(\S+) Time=([0-9.]+)', anim.inspect_foundation_playback())
        solver_lifts = {}
        if sole_clearance:
            diagnostic = json.loads(unreal.Project_JPlayerLocomotionLibrary.inspect_foot_clearance(mesh))
            solver_lifts = {leg['foot']: leg['applied_lift_cm'] for leg in diagnostic['legs'] if leg['prepared'] and leg['applied_lift_cm'] >= 0}
            assert all(bone in solver_lifts for bone in sole_lifts), 'Missing completed sole solve'
        report['samples'].append({'elapsed': elapsed, 'asset': active.get_name(), 'time': time, 'errors': errors,
                                  'position_errors_cm': positions, 'sole_lifts_cm': sole_lifts,
                                  'solver_applied_lifts_cm': solver_lifts, 'base_playback': base})
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
unreal.log('PlayerLocomotion montage pose regression started: ' + str(output))
