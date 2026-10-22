"""Verify shared validation evidence or restore original paths without overwriting files."""
import argparse
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import zipfile


def digest(stream):
    result = hashlib.sha256()
    for chunk in iter(lambda: stream.read(1024 * 1024), b''):
        result.update(chunk)
    return result.hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('manifest', type=Path)
    selection = parser.add_mutually_exclusive_group()
    selection.add_argument('--restore', help='Exact repository-relative original path from the manifest')
    selection.add_argument('--restore-prefix', help='Original directory prefix; restores all matching paths')
    parser.add_argument('--destination', type=Path, help='Empty output directory for restoration')
    args = parser.parse_args()
    manifest = json.loads(args.manifest.read_text(encoding='utf-8'))
    archive = args.manifest.parent / manifest['archive']
    with archive.open('rb') as stream:
        if digest(stream) != manifest['archiveSha256']:
            raise SystemExit('Archive SHA-256 mismatch')
    with zipfile.ZipFile(archive) as package:
        unique = {}
        for item in manifest['files']:
            previous = unique.setdefault(item['member'], (item['sha256'], item['bytes']))
            if previous != (item['sha256'], item['bytes']):
                raise SystemExit('Inconsistent duplicate mapping')
        if set(package.namelist()) != set(unique):
            raise SystemExit('Archive member set mismatch')
        for member, (expected, size) in unique.items():
            if package.getinfo(member).file_size != size:
                raise SystemExit('Size mismatch: ' + member)
            with package.open(member) as stream:
                if digest(stream) != expected:
                    raise SystemExit('Member SHA-256 mismatch: ' + member)
        print(f'Verified {len(unique)} payloads / {len(manifest["files"])} original paths')
        if args.restore or args.restore_prefix:
            if not args.destination:
                parser.error('Restoration requires --destination')
            matches = [item for item in manifest['files'] if
                       (item['path'] == args.restore if args.restore else
                        item['path'].startswith(args.restore_prefix.rstrip('/') + '/'))]
            if not matches or (args.restore and len(matches) != 1):
                raise SystemExit('Unknown or ambiguous original path')
            root = args.destination.resolve()
            targets = []
            for item in matches:
                relative = PurePosixPath(item['path'])
                if relative.is_absolute() or '..' in relative.parts or ':' in str(relative):
                    raise SystemExit('Unsafe original path')
                target = root.joinpath(*relative.parts).resolve()
                if not target.is_relative_to(root):
                    raise SystemExit('Restoration escapes destination')
                if target.exists():
                    raise SystemExit('Refusing overwrite: ' + str(target))
                targets.append((item, target))
            for item, target in targets:
                target.parent.mkdir(parents=True, exist_ok=True)
                with target.open('xb') as output, package.open(item['member']) as stream:
                    shutil.copyfileobj(stream, output)
            print(f'Restored {len(targets)} files under {root}')


if __name__ == '__main__':
    main()
