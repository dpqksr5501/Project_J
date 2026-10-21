"""Read-only Unreal adapters shared by import preflight and Manny regression."""
import hashlib
import json
from pathlib import Path
import unreal
from animation_import_catalog import FINGERPRINT_VERSION, signature

ROOT = Path(unreal.Paths.project_dir()).resolve()
CATALOG = ROOT / 'Scripts/Authoring/Data/PlayerAnimationLibrary.json'
_file_hashes = {}


def asset_file(package):
    if not package.startswith('/Game/') or '..' in package.split('/'):
        raise ValueError('Expected a project package')
    return ROOT / 'Content' / (package.removeprefix('/Game/') + '.uasset')


def file_hash(path):
    path = Path(path)
    stat = path.stat()
    key = (path, stat.st_size, stat.st_mtime_ns)
    if key not in _file_hashes:
        h = hashlib.sha256()
        with path.open('rb') as stream:
            for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
                h.update(block)
        _file_hashes[key] = h.hexdigest()
    return _file_hashes[key]


def motion_fingerprint(sequence):
    # Editor modules persist between commands. A path cache would incorrectly
    # reuse stale data after an artist edits a key without renaming the asset.
    value = unreal.Project_JPlayerLocomotionLibrary.get_animation_motion_fingerprint(sequence)
    if len(value) != 64:
        raise ValueError('No raw motion/bind-pose data: ' + sequence.get_path_name())
    return value.lower()


def enum_value(value):
    return int(value.value)


def sequence_contract(sequence):
    """Authored playback/notify semantics; native helper includes custom payloads."""
    get = sequence.get_editor_property
    model = get('data_model_interface')
    rate = model.get_frame_rate()
    notifies = []
    classes = {}
    for event in unreal.AnimationLibrary.get_animation_notify_events(sequence):
        notify = event.get_editor_property('notify')
        state = event.get_editor_property('notify_state_class')
        for obj in (notify, state):
            if obj:
                package = obj.get_class().get_path_name().split('.')[0]
                if package.startswith('/Game/'):
                    classes[package] = file_hash(asset_file(package))
        notifies.append({'name': str(event.get_editor_property('notify_name')),
                         'time': unreal.AnimationLibrary.get_anim_notify_event_trigger_time(event),
                         'duration': unreal.AnimationLibrary.get_anim_notify_event_duration(event),
                         'server': bool(event.get_editor_property('trigger_on_dedicated_server')),
                         'class': notify.get_class().get_path_name() if notify else '',
                         'state_class': state.get_class().get_path_name() if state else ''})
    base = get('ref_pose_seq')
    return {'length': unreal.AnimationLibrary.get_sequence_length(sequence),
            'skeleton_policy_sha256': file_hash(asset_file(get('skeleton').get_path_name().split('.')[0])),
            'frames': model.get_number_of_frames(),
            'frame_rate': [rate.numerator, rate.denominator],
            'additive_type': enum_value(get('additive_anim_type')),
            'ref_pose_type': enum_value(get('ref_pose_type')),
            'ref_frame_index': get('ref_frame_index'),
            'ref_pose_name': base.get_name() if base else '',
            'ref_pose_motion_fingerprint': motion_fingerprint(base) if base else '',
            'enable_root_motion': bool(get('enable_root_motion')),
            'root_lock': enum_value(get('root_motion_root_lock')),
            'force_root_lock': bool(get('force_root_lock')),
            'normalized_root_motion_scale': bool(get('use_normalized_root_motion_scale')),
            'rate_scale': get('rate_scale'),
            'interpolation': enum_value(get('interpolation')),
            'notify_payload_fingerprint': unreal.Project_JPlayerLocomotionLibrary.get_animation_notify_fingerprint(sequence).lower(),
            'notify_class_files': classes, 'notifies': notifies}


def sequence_record(source_id, sequence, target_package, profile_fingerprint):
    contract = sequence_contract(sequence)
    motion = motion_fingerprint(sequence)
    base = sequence.get_editor_property('ref_pose_seq')
    return {'source_id': source_id, 'source_package': sequence.get_path_name().split('.')[0],
            'source_skeleton_package': sequence.get_editor_property('skeleton').get_path_name().split('.')[0],
            'source_additive_base_package': base.get_path_name().split('.')[0] if base else '',
            'source_additive_skeleton_package': base.get_editor_property('skeleton').get_path_name().split('.')[0] if base else '',
            'source_package_sha256': file_hash(asset_file(sequence.get_path_name().split('.')[0])),
            'source_motion_fingerprint': motion, 'source_contract': contract,
            'source_signature': signature(motion, contract), 'class': 'AnimSequence',
            'target_package': target_package, 'profile_fingerprint': profile_fingerprint}


def conversion_profile():
    retargeter = unreal.load_asset('/Game/Animation_Logic/RTG_Player_UEFN_to_Manny')
    controller = unreal.IKRetargeterController.get_controller(retargeter)
    packages = {'retargeter': retargeter.get_path_name().split('.')[0]}
    for side, label in [(unreal.RetargetSourceOrTarget.SOURCE, 'source'),
                        (unreal.RetargetSourceOrTarget.TARGET, 'target')]:
        rig = controller.get_ik_rig(side)
        mesh = unreal.IKRigController.get_controller(rig).get_skeletal_mesh()
        packages[label + '_rig'] = rig.get_path_name().split('.')[0]
        packages[label + '_mesh'] = mesh.get_path_name().split('.')[0]
    files = {role: {'package': package, 'sha256': file_hash(asset_file(package))}
             for role, package in packages.items()}
    from animation_import_catalog import digest
    return {'id': 'Player_UEFN_to_Manny_v1', 'fingerprint': digest(files), 'files': files,
            'fingerprint_version': FINGERPRINT_VERSION}


def load_catalog():
    return json.loads(CATALOG.read_text(encoding='utf-8-sig'))
