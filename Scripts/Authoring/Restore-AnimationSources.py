"""Verify/list or restore explicit archived source packages with their dependencies.

Run with ordinary Python. Defaults to listing; --restore --package is explicit.
Preflights every path/hash/conflict before writing, never overwrites different
current data and never puts an archive into Unreal's Content search roots.
"""
import argparse
import hashlib
import json
from pathlib import Path
import zipfile

ROOT = Path(__file__).resolve().parents[2]
FOLDER = ROOT / 'Archives/AnimationSources/UEFN_2026-10-21'


def digest(stream):
    h = hashlib.sha256()
    for block in iter(lambda: stream.read(4 * 1024 * 1024), b''):
        h.update(block)
    return h.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--verify', action='store_true')
    parser.add_argument('--restore', action='store_true')
    parser.add_argument('--package', action='append', default=[])
    args = parser.parse_args()
    manifest = json.loads((FOLDER / 'ArchiveManifest.json').read_text(encoding='utf-8-sig'))
    archive = FOLDER / 'Originals.zip'
    with archive.open('rb') as stream:
        if digest(stream) != manifest['archive_sha256']:
            raise ValueError('Source archive checksum mismatch')
    files = manifest['files']
    if not args.verify and not args.restore:
        print(json.dumps({'archive': str(archive), 'packages': len({e['package'] for e in files}),
                          'files': len(files), 'archive_bytes': archive.stat().st_size,
                          'restore': 'Use --restore --package /Game/...; dependencies are included.'}, indent=2))
        return
    selected = set(args.package)
    if args.restore and not selected:
        raise ValueError('Specify explicit --package entries; bulk restore is not implicit')
    known = {e['package'] for e in files}
    if not selected.issubset(known):
        raise ValueError('A requested package is not in this archive')
    dependencies = manifest['package_dependencies']
    pending = list(selected)
    while pending:
        for dependency in dependencies.get(pending.pop(), []):
            if dependency in known and dependency not in selected:
                selected.add(dependency)
                pending.append(dependency)
    chosen = files if args.verify else [e for e in files if e['package'] in selected]
    writes = []
    with zipfile.ZipFile(archive) as container:
        archived = json.loads(container.read('ArchiveManifest.json'))
        if archived['files'] != files:
            raise ValueError('Archive index does not match the preservation manifest')
        for entry in chosen:
            target = (ROOT / entry['path']).resolve()
            if not target.is_relative_to((ROOT / 'Content').resolve()):
                raise ValueError('Unsafe archive path')
            with container.open(entry['path']) as stream:
                if digest(stream) != entry['sha256']:
                    raise ValueError('Archived package hash mismatch: ' + entry['path'])
            if args.restore:
                if target.exists():
                    with target.open('rb') as stream:
                        if digest(stream) != entry['sha256']:
                            raise ValueError('Current data differs; refusing overwrite: ' + entry['path'])
                else:
                    writes.append((entry, target))
        # All conflicts and hashes passed; no extraction can escape Content.
        for entry, target in writes:
            target.parent.mkdir(parents=True, exist_ok=True)
            with container.open(entry['path']) as source, target.open('xb') as output:
                for block in iter(lambda: source.read(4 * 1024 * 1024), b''):
                    output.write(block)
    print(json.dumps({'verified_files': len(chosen), 'restored_files': len(writes),
                      'selected_packages': sorted(selected)}, indent=2))


if __name__ == '__main__':
    main()
