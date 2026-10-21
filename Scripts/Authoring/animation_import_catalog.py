"""Portable, fail-closed planning for the player animation library; no Unreal dependency."""
import hashlib
import json
import re

SCHEMA_VERSION = 1
FINGERPRINT_VERSION = 'ProjectJ.AnimationMotion.v1'
TARGET_ROOTS = ('/Game/Characters/Mannequins/Animations/Locomotion/',
                '/Game/Characters/Mannequins/Animations/ProjectJump/')


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     ensure_ascii=False, allow_nan=False).encode('utf-8')).hexdigest()


def signature(motion_fingerprint, contract):
    if not re.fullmatch('[0-9a-fA-F]{64}', motion_fingerprint):
        raise ValueError('Missing or unsupported raw-motion fingerprint')
    return digest({'version': FINGERPRINT_VERSION, 'motion': motion_fingerprint.lower(),
                   'contract': contract})


def valid_target(path):
    return (isinstance(path, str) and path.startswith(TARGET_ROOTS)
            and not any(part in ('', '.', '..') for part in path.split('/')[1:])
            and bool(re.fullmatch(r'/[A-Za-z0-9_+/\-]+', path)))


def plan_import(catalog, candidates, profile_fingerprint, occupied_targets=()):
    """Never overwrite or guess between revisions, profiles or ambiguous aliases.

    Reuse requires a source signature AND conversion profile match. Existing
    multiple outputs are retained; this planner never consolidates authored data.
    """
    if catalog.get('schema_version') != SCHEMA_VERSION or catalog.get('fingerprint_version') != FINGERPRINT_VERSION:
        raise ValueError('Unsupported animation catalog version')
    existing = catalog['entries']
    by_id = {}
    by_signature = {}
    occupied = {p.casefold() for p in occupied_targets}
    for row in existing:
        occupied.add(row['target_package'].casefold())
        for identity in [row['source_id']] + row.get('source_aliases', []):
            by_id.setdefault(identity.casefold(), []).append(row)
        by_signature.setdefault(row.get('source_signature'), []).append(row)
    rows = []
    pending_ids = set()
    # Account for duplicate inputs within the same request as well as prior imports.
    pending_signatures = {}
    for candidate in candidates:
        row = dict(candidate)
        identity = row['source_id'].casefold()
        sig = row['source_signature']
        destination = row['target_package']
        status, reason, reuse = 'review', '', None
        if not valid_target(destination):
            reason = 'invalid_target_path'
        elif row.get('class') != 'AnimSequence':
            reason = 'non_sequence_requires_explicit_dependency_migration'
        elif identity in pending_ids:
            reason = 'duplicate_source_id_in_request'
        elif by_id.get(identity):
            matches = by_id[identity]
            exact = {r['target_package'] for r in matches
                     if r['source_signature'] == sig and r['profile_fingerprint'] == profile_fingerprint}
            if len(exact) == 1:
                status, reason, reuse = 'reuse', 'known_source_and_recipe', exact.pop()
            else:
                reason = 'known_source_changed_or_recipe_changed'
        else:
            exact = {r['target_package'] for r in by_signature.get(sig, [])
                     if r['profile_fingerprint'] == profile_fingerprint}
            if len(exact) == 1:
                status, reason, reuse = 'reuse', 'renamed_source_same_content_and_recipe', exact.pop()
            elif len(exact) > 1:
                reason = 'multiple_authored_outputs_require_selection'
            elif sig in pending_signatures:
                status, reason, reuse = 'reuse', 'duplicate_content_in_request', pending_signatures[sig]
            elif destination.casefold() in occupied:
                reason = 'target_exists_never_overwrite'
            elif catalog.get('conversion_profile', {}).get('fingerprint') != profile_fingerprint:
                reason = 'unapproved_conversion_recipe'
            else:
                status, reason = 'new', 'new_source_new_target'
                occupied.add(destination.casefold())
                pending_signatures[sig] = destination
        pending_ids.add(identity)
        row.update(status=status, reason=reason, reuse_target=reuse)
        rows.append(row)
    return {'schema_version': SCHEMA_VERSION, 'catalog_sha256': digest(catalog),
            'profile_fingerprint': profile_fingerprint, 'rows': rows,
            'can_apply': not any(r['status'] == 'review' for r in rows)}
