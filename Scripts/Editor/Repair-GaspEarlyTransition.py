"""Six-clip migration with dry-run, byte backup, metadata verification.
Use EARLY_MODE=apply only after a successful dry-run. Never saves GASP."""
import hashlib
import json
import os
import re
import shutil
import unreal

mode = globals().get('EARLY_MODE', 'dry-run')
assert mode in ['dry-run', 'apply', 'verify']
root = os.path.join(unreal.Paths.project_saved_dir(), 'Validation/MMOUI_20261010/EarlyTransition')
os.makedirs(root, exist_ok=True)
source = 'C:/Users/I/Documents/Unreal Projects/GameAnimationSample/Content'

def write(name, data):
    with open(os.path.join(root, name), 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)

def read(name):
    with open(os.path.join(root, name), encoding='utf-8') as f:
        return json.load(f)

def normalized(event):
    return re.sub(r'NotifyStateClass="[^"]*"', 'NotifyStateClass=None', event.export_text())

if mode == 'verify':
    report = read('apply.json')
    with open(os.path.join(os.path.dirname(root), 'EarlyTransitionSource.json'), encoding='utf-8') as f:
        original_audit = {r['package']: r['target_events'] for r in json.load(f)}
    def normalize_early_only(text):
        return re.sub(r'NotifyStateClass="[^"]*"', 'NotifyStateClass=None', text) if 'NotifyName="BP_NotifyState_EarlyTransition_C"' in text else text
    assert report['errors'] == 0 and report['saved'] == 6
    for row in report['packages']:
        animation = unreal.load_asset(row['package'])
        events = unreal.AnimationLibrary.get_animation_notify_events(animation)
        early = [e for e in events if 'EarlyTransition' in str(e.notify_name)]
        assert len(early) == 1
        state = early[0].notify_state_class
        assert isinstance(state, unreal.Project_JAnimNotifyState_LocomotionEarlyTransition)
        assert state.require_gait_change and state.excluded_gait == unreal.Project_JLocomotionGaitIntent.RUN
        assert [normalized(e) for e in events] == read('before.json')[row['package']]
        # All other notify/state object references must remain exactly unchanged.
        assert [normalize_early_only(e.export_text()) for e in events] == [normalize_early_only(t) for t in original_audit[row['package']]]
    write('verify.json', {'packages': 6, 'native_notifies': 6, 'metadata_changes': 0, 'errors': 0})
    print('EARLY_TRANSITION_VERIFIED', 6)
else:
    preflight = json.loads(unreal.Project_JEarlyTransitionMigrationLibrary.repair_six_run_clips(source, False))
    write('dry-run.json', preflight)
    assert preflight['errors'] == 0 and len(preflight['packages']) == 6, preflight
    if mode == 'apply':
        before = {}; manifest = []
        for row in preflight['packages']:
            path = row['package']
            before[path] = [normalized(e) for e in unreal.AnimationLibrary.get_animation_notify_events(unreal.load_asset(path))]
            original = os.path.join(unreal.Paths.project_content_dir(), path[6:] + '.uasset')
            backup = os.path.join(root, 'Backup', path[6:] + '.uasset')
            assert not os.path.exists(backup), 'Existing backup: verify before repeating apply'
            os.makedirs(os.path.dirname(backup), exist_ok=True)
            shutil.copy2(original, backup)
            with open(original, 'rb') as f: original_hash = hashlib.file_digest(f, 'sha256').hexdigest()
            with open(backup, 'rb') as f: assert hashlib.file_digest(f, 'sha256').hexdigest() == original_hash
            manifest.append({'package': path, 'file': original, 'backup': backup, 'sha256': original_hash})
        write('before.json', before); write('BackupManifest.json', manifest)
        report = json.loads(unreal.Project_JEarlyTransitionMigrationLibrary.repair_six_run_clips(source, True))
        write('apply.json', report)
        assert report['errors'] == 0 and report['saved'] == 6, report
    print('EARLY_TRANSITION', mode, 6)
