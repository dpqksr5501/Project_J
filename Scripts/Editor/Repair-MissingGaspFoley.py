"""Explicit dry-run/apply recovery of stripped GASP Foley objects.

Set FOLEY_REPAIR_MODE='apply' only after inspecting dry-run.json. The input is an
explicit Missing.json audit, not AssetRegistry referencers (which omit stripped
objects). Original GASP files are read under a separate mount and never saved.
"""
import hashlib
import json
import os
import re
import shutil
import unreal

ROOT = globals().get('FOLEY_REPAIR_ROOT', os.path.join(unreal.Paths.project_saved_dir(), 'Validation/FoleyForwardFix_20261008'))
MODE = globals().get('FOLEY_REPAIR_MODE', 'dry-run')
assert MODE in ('dry-run', 'apply', 'verify')
SOURCE = r'C:/Users/I/Documents/Unreal Projects/GameAnimationSample/Content'
with open(os.path.join(ROOT, 'Missing.json'), encoding='utf-8-sig') as f:
    audit = json.load(f)
packages = sorted({r['package'] for r in audit})
assert packages and len(packages) == len(audit)

def write(name, value):
    with open(os.path.join(ROOT, name), 'w', encoding='utf-8') as f:
        json.dump(value, f, indent=2)

def digest(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def metadata(event, repaired):
    text = event.export_text()
    if repaired:
        text = re.sub(r'Notify="[^"]*"', 'Notify=None', text)
        text = text.replace('bTriggerOnDedicatedServer=False', 'bTriggerOnDedicatedServer=True')
    return text

if MODE == 'verify':
    with open(os.path.join(ROOT, 'apply.json'), encoding='utf-8') as f: result = json.load(f)
    assert not result['errors']
    checked = 0
    for row in result['packages']:
        events = unreal.AnimationLibrary.get_animation_notify_events(unreal.load_asset(row['package']))
        indices = {r['index'] for r in row['events']}
        assert [metadata(e, i in indices) for i,e in enumerate(events)] == row['before'], row['package']
        for expected in row['events']:
            event = events[expected['index']]
            assert isinstance(event.notify, unreal.Project_JAnimNotify_FoleyEvent)
            payload = event.notify.foley_event
            assert expected['event'] in payload.event.export_text()
            assert int(payload.side.value) == expected['side']
            assert abs(payload.volume_multiplier - expected['volume']) < 1e-6
            assert abs(payload.pitch_multiplier - expected['pitch']) < 1e-6
            assert not event.trigger_on_dedicated_server
            checked += 1
    write('verify.json', {'packages':len(result['packages']), 'native_events':checked, 'errors':0})
    print('FOLEY_REPAIR_VERIFIED', len(result['packages']), checked)
else:
    if MODE == 'apply':
        with open(os.path.join(ROOT, 'dry-run.json'), encoding='utf-8') as f: preflight = json.load(f)
        assert not preflight['errors'] and len(preflight['packages']) == len(packages)
        manifest = []
        for path in packages:
            file = os.path.join(unreal.Paths.project_content_dir(), path[6:]+'.uasset')
            backup = os.path.join(ROOT, 'Backup', path[6:]+'.uasset')
            assert not os.path.exists(backup), 'Existing recovery backup; verify manually before resuming.'
            os.makedirs(os.path.dirname(backup), exist_ok=True)
            shutil.copy2(file, backup)
            assert digest(file) == digest(backup)
            manifest.append({'package':path, 'file':file, 'backup':backup, 'sha256':digest(file)})
        write('BackupManifest.json', manifest)
    result = {'mode':MODE, 'errors':0, 'repaired':0, 'saved':0, 'packages':[]}
    for start in range(0, len(packages), 8):
        batch_paths = packages[start:start+8]
        before = {path:[e.export_text() for e in unreal.AnimationLibrary.get_animation_notify_events(unreal.load_asset(path))] for path in batch_paths}
        if MODE == 'apply':
            assert all(before[path] == next(row['before'] for row in preflight['packages'] if row['package']==path) for path in batch_paths)
        batch = json.loads(unreal.Project_JFoleyMigrationLibrary.repair_missing_gasp_foley(batch_paths, SOURCE, MODE=='apply'))
        for row in batch['packages']:
            if 'error' not in row: row['before'] = before[row['package']]
        result['packages'].extend(batch['packages'])
        for key in ('errors','repaired','saved'): result[key] += batch[key]
        write(MODE+'.json', result)
        assert not batch['errors'], [row for row in batch['packages'] if 'error' in row]
    print('FOLEY_REPAIR', MODE, len(result['packages']), result['repaired'], result['saved'])
