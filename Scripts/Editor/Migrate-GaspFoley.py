"""Run in Project_J's editor Python after enabling its loopback-only session connection.

Requires an explicit Candidates.json and verified BackupManifest.json in Saved/Validation.
Set FOLEY_MIGRATION_MODE to 'dry-run', 'apply', 'resume', or 'verify' before executing this file.
Dry-run must succeed for the complete list before apply; stops on the first bad batch.
The editor-only C++ library replaces object pointers, preserving full event metadata.
"""
import hashlib
import json
import os
import re
import time
import unreal

ROOT = globals().get('FOLEY_MIGRATION_ROOT', os.path.join(unreal.Paths.project_saved_dir(), 'Validation/FoleyMigration_20261008'))
MODE = globals().get('FOLEY_MIGRATION_MODE', 'dry-run')
assert MODE in ('dry-run', 'apply', 'resume', 'verify')
with open(os.path.join(ROOT, 'Candidates.json'), encoding='utf-8-sig') as f:
    candidates = json.load(f)
with open(os.path.join(ROOT, 'BackupManifest.json'), encoding='utf-8-sig') as f:
    backups = json.load(f)
packages = candidates['packages']
with open(os.path.join(ROOT, 'OriginalNamedEvents.json'), encoding='utf-8-sig') as f:
    named_events = json.load(f)
named_keys = [r['package'] + '|' + r['guid'] + '|' + r['name'] for r in named_events]
assert len(packages) == len(set(packages)) and packages
assert set(packages) == {row['package'] for row in backups}

def digest(path):
    with open(path, 'rb') as f:
        return hashlib.file_digest(f, 'sha256').hexdigest()

def write(name, data):
    path = os.path.join(ROOT, name)
    with open(path + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(data, f, indent=2)
    for attempt in range(10):
        try:
            os.replace(path + '.tmp', path)
            return
        except PermissionError:
            if attempt == 9:
                raise
            time.sleep(0.2)

def metadata(event, is_foley):
    text = event.export_text()
    if is_foley:
        text = re.sub(r'Notify="[^"]*"', 'Notify=<Foley>', text)
        text = text.replace('bTriggerOnDedicatedServer=True', 'bTriggerOnDedicatedServer=False')
    return text

if MODE in ('dry-run', 'apply', 'resume'):
    start_at = 0
    if MODE == 'resume':
        with open(os.path.join(ROOT, 'apply.json'), encoding='utf-8') as f:
            audit = json.load(f)
        assert audit['errors'] == 0
        completed = [p['package'] for b in audit['batches'] for p in b['packages']]
        start_at = audit['completed_packages']
        assert completed == packages[:start_at] and audit['saved'] == start_at
        for b in audit['batches']:
            for p in b['packages']:
                events = unreal.AnimationLibrary.get_animation_notify_events(unreal.load_asset(p['package']))
                indices = {row['index'] for row in p['events']}
                assert [metadata(e, i in indices) for i,e in enumerate(events)] == p['metadata'], 'Completed package changed: '+p['package']
                assert all(isinstance(events[i].notify, unreal.Project_JAnimNotify_FoleyEvent) for i in indices)
        MODE = 'apply'
    for row in backups:
        assert digest(row['backup']) == row['sha256'], 'Backup changed: ' + row['backup']
        if row['package'] not in packages[:start_at]:
            assert digest(row['path']) == row['sha256'], 'Target changed since backup: ' + row['path']
    if MODE == 'apply':
        with open(os.path.join(ROOT, 'dry-run.json'), encoding='utf-8') as f:
            preflight = json.load(f)
        assert preflight['errors'] == 0 and preflight['completed_packages'] == len(packages)
    if not start_at:
        audit = {'mode': MODE, 'errors': 0, 'replacements': 0, 'saved': 0, 'completed_packages': 0, 'batches': []}
    for start in range(start_at, len(packages), 24):
        batch = json.loads(unreal.Project_JFoleyMigrationLibrary.migrate_gasp_foley(
            packages[start:start+24], candidates['source_content'], MODE == 'apply', named_keys))
        if not batch['errors']:
            for package in batch['packages']:
                events = unreal.AnimationLibrary.get_animation_notify_events(unreal.load_asset(package['package']))
                foley_indices = {row['index'] for row in package['events']}
                package['metadata'] = [metadata(e, i in foley_indices) for i, e in enumerate(events)]
                for original_named in named_events:
                    if original_named['package'] == package['package']:
                        assert any(e.export_text() == original_named['metadata'] for e in events), 'Original named event differs: ' + package['package']
                if MODE == 'apply':
                    before = next(p for b in preflight['batches'] for p in b['packages'] if p['package'] == package['package'])
                    assert package['metadata'] == before['metadata'], 'Serialized metadata changed: ' + package['package']
        audit['batches'].append(batch)
        for key in ('errors', 'replacements', 'saved'):
            audit[key] += batch[key]
        audit['completed_packages'] += len(packages[start:start+24])
        write(MODE + '.json', audit)
        if batch['errors']:
            raise RuntimeError(json.dumps([p for p in batch['packages'] if 'error' in p]))
        unreal.SystemLibrary.collect_garbage()
    print(json.dumps({k: v for k, v in audit.items() if k != 'batches'}))
else:
    with open(os.path.join(ROOT, 'apply.json'), encoding='utf-8') as f:
        applied = json.load(f)
    assert applied['errors'] == 0 and applied['saved'] > 0
    expected = {p['package']: p for b in applied['batches'] for p in b['packages']}
    with_source = globals().get('FOLEY_VERIFY_WITH_SOURCE', False)
    full_metadata = globals().get('FOLEY_VERIFY_METADATA', True)
    audit = {'mode': MODE, 'with_source': with_source, 'full_metadata': full_metadata, 'packages': 0, 'native_events': 0, 'errors': []}
    result_name = 'verify-with-source.json' if with_source else ('verify.json' if full_metadata else 'verify-native-only.json')
    for package_index, path in enumerate(packages):
        if with_source and package_index % 24 == 0:
            probe = json.loads(unreal.Project_JFoleyMigrationLibrary.migrate_gasp_foley(
                packages[package_index:package_index+24], candidates['source_content'], False, named_keys))
            assert probe['errors'] == 0 and probe['replacements'] == 0 and probe['saved'] == 0, 'Migration is not idempotent'
        seq = unreal.load_asset(path)
        events = unreal.AnimationLibrary.get_animation_notify_events(seq)
        original = expected[path]
        assert len(events) == original['notify_count'], 'Notify count changed: ' + path
        foley_indices = {row['index'] for row in original['events']}
        if full_metadata:
            assert [metadata(e, i in foley_indices) for i, e in enumerate(events)] == original['metadata'], 'Reloaded metadata differs: ' + path
        else:
            # Native-only pass has no GASP mount. Pre-existing missing non-Foley BP
            # states cannot resolve there; the separate source pass checks them fully.
            assert all(metadata(events[i], True) == original['metadata'][i] for i in foley_indices), path
        for row in original['events']:
            event = events[row['index']]
            notify = event.notify
            assert isinstance(notify, unreal.Project_JAnimNotify_FoleyEvent), path
            payload = notify.get_editor_property('foley_event')
            tag_text = payload.event.export_text()
            tag = re.search(r'TagName=(?:"([^"]*)"|([^,)]+))', tag_text)
            assert tag and (tag.group(1) or tag.group(2)) == row['event'], path
            assert int(payload.side.value) == row['side'], path
            assert abs(payload.volume_multiplier - row['volume']) < 1e-6, path
            assert abs(payload.pitch_multiplier - row['pitch']) < 1e-6, path
            assert not event.trigger_on_dedicated_server, path
            # Engine API returns trigger time including the original trigger offset.
            assert abs(unreal.AnimationLibrary.get_anim_notify_event_trigger_time(event) - row['trigger_time']) < 1e-6, path
            audit['native_events'] += 1
        audit['packages'] += 1
        if audit['packages'] % 24 == 0:
            write(result_name, audit)
            unreal.SystemLibrary.collect_garbage()
    write(result_name, audit)
    print(json.dumps(audit))
