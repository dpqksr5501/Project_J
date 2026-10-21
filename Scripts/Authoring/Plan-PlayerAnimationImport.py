"""Read-only preflight in Project J's editor, before retargeting staged sources.

Set PLAYER_ANIMATION_IMPORT_REQUEST to a JSON filename via runpy init_globals.
The request lists explicit source_id/source_package/target_package rows. Produces
new/reuse/review results in Saved; never duplicates, retargets or modifies assets.
"""
import datetime
import json
import sys
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).resolve().parent))
from animation_import_catalog import plan_import, digest
from player_animation_library import ROOT, load_catalog, sequence_record, conversion_profile, motion_fingerprint, sequence_contract


def main(request_file):
    request = json.loads(Path(request_file).read_text(encoding='utf-8-sig'))
    catalog = load_catalog()
    profile = conversion_profile()
    candidates = []
    for item in request['assets']:
        source = unreal.load_asset(item['source_package'])
        if not isinstance(source, unreal.AnimSequence):
            raise ValueError('Explicit sequence-only preflight; BlendSpaces require dependency review')
        candidates.append(sequence_record(item['source_id'], source, item['target_package'], profile['fingerprint']))
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    occupied = [str(a.package_name) for a in registry.get_assets_by_path('/Game/Characters/Mannequins/Animations', True)]
    result = plan_import(catalog, candidates, profile['fingerprint'], occupied)
    # A catalog hit is not usable if an artist changed or removed its output.
    targets = {r['target_package']: r for r in catalog['entries']}
    new_targets = {r['target_package'] for r in result['rows'] if r['status'] == 'new'}
    for row in result['rows']:
        path = row.get('reuse_target')
        if row['status'] != 'reuse' or path in new_targets:
            continue
        expected = targets[path]
        target = unreal.load_asset(path)
        if (not isinstance(target, unreal.AnimSequence)
                or motion_fingerprint(target) != expected['target_motion_fingerprint']
                or sequence_contract(target) != expected['target_contract']):
            row.update(status='review', reason='catalog_output_missing_or_changed', reuse_target=None)
    relevant = {p['package'] for p in profile['files'].values()}
    relevant.add(catalog['canonical_skeleton'])
    for row in result['rows']:
        relevant.update([row['source_package'], row['target_package']])
        relevant.update(row.get(key, '') for key in ['source_skeleton_package',
                        'source_additive_base_package', 'source_additive_skeleton_package'])
        relevant.update(row['source_contract']['notify_class_files'])
        if row.get('reuse_target') in targets:
            relevant.add(row['reuse_target'])
            relevant.update(targets[row['reuse_target']]['target_contract']['notify_class_files'])
    dirty = {p.get_path_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()}
    result['unsaved_relevant_packages'] = sorted(dirty & relevant)
    if result['unsaved_relevant_packages']:
        for row in result['rows']:
            row.update(status='review', reason='save_or_revert_relevant_authoring_changes_before_planning', reuse_target=None)
    result['can_apply'] = not any(r['status'] == 'review' for r in result['rows'])
    result['plan_sha256'] = digest(result)
    result['read_only'] = True
    folder = ROOT / 'Saved/AnimationImport'
    folder.mkdir(parents=True, exist_ok=True)
    output = folder / ('Plan_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.json')
    output.write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
    unreal.log('Player animation import preflight: ' + str(output))
    return output


if __name__ == '__main__':
    main(globals()['PLAYER_ANIMATION_IMPORT_REQUEST'])
