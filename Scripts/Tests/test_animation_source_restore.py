"""Archive restoration must reject conflicts/traversal before any write."""
import contextlib
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

script = Path(__file__).resolve().parents[1] / 'Authoring/Restore-AnimationSources.py'
spec = importlib.util.spec_from_file_location('source_restore', script)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)


class SourceRestoreTests(unittest.TestCase):
    def setUp(self):
        scratch = script.parents[2] / 'Saved/Validation/AnimationSourceRestoreTests'
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix='ProjectJ_SourceRestore_', dir=scratch)
        if not Path(self.temp.name).resolve().is_relative_to(scratch.resolve()):
            raise ValueError('Test cleanup must stay inside its workspace scratch directory')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.folder = self.root / 'Archives'
        self.folder.mkdir()
        self.files = [{'path': 'Content/Old/Clip.uasset', 'package': '/Game/Old/Clip',
                       'bytes': 4, 'sha256': hashlib.sha256(b'clip').hexdigest()},
                      {'path': 'Content/Old/Rig.uasset', 'package': '/Game/Old/Rig',
                       'bytes': 3, 'sha256': hashlib.sha256(b'rig').hexdigest()}]
        self.prepare()

    def prepare(self):
        manifest = {'files': self.files, 'package_dependencies': {'/Game/Old/Clip': ['/Game/Old/Rig']}}
        archive = self.folder / 'Originals.zip'
        with zipfile.ZipFile(archive, 'w') as z:
            z.writestr(self.files[0]['path'], b'clip')
            z.writestr(self.files[1]['path'], b'rig')
            z.writestr('ArchiveManifest.json', json.dumps(manifest))
        manifest['archive_sha256'] = hashlib.sha256(archive.read_bytes()).hexdigest()
        (self.folder / 'ArchiveManifest.json').write_text(json.dumps(manifest))

    def invoke(self, *arguments):
        with patch.object(module, 'ROOT', self.root), patch.object(module, 'FOLDER', self.folder), \
             patch.object(sys, 'argv', ['restore'] + list(arguments)), contextlib.redirect_stdout(io.StringIO()):
            module.main()

    def test_restore_includes_dependencies_and_is_idempotent(self):
        self.invoke('--restore', '--package', '/Game/Old/Clip')
        self.assertEqual((self.root / 'Content/Old/Clip.uasset').read_bytes(), b'clip')
        self.assertEqual((self.root / 'Content/Old/Rig.uasset').read_bytes(), b'rig')
        self.invoke('--restore', '--package', '/Game/Old/Clip')

    def test_conflict_preflight_writes_nothing(self):
        target = self.root / 'Content/Old/Rig.uasset'
        target.parent.mkdir(parents=True)
        target.write_bytes(b'user change')
        with self.assertRaises(ValueError):
            self.invoke('--restore', '--package', '/Game/Old/Clip')
        self.assertFalse((target.parent / 'Clip.uasset').exists())
        self.assertEqual(target.read_bytes(), b'user change')

    def test_traversal_rejected(self):
        self.files[0]['path'] = '../Outside.uasset'
        self.prepare()
        with self.assertRaises(ValueError):
            self.invoke('--restore', '--package', '/Game/Old/Clip')

    def test_corruption_rejected(self):
        with (self.folder / 'Originals.zip').open('ab') as stream:
            stream.write(b'changed')
        with self.assertRaises(ValueError):
            self.invoke('--verify')

    def test_verify_does_not_restore(self):
        self.invoke('--verify')
        self.assertFalse((self.root / 'Content').exists())

    def test_explicit_bundle_uses_its_own_manifest(self):
        other = self.root / 'Neutral'
        self.folder.rename(other)
        self.invoke('--bundle', 'Neutral', '--verify')
        self.assertFalse((self.root / 'Content').exists())

    def test_bundle_cannot_escape_archive_directory(self):
        for value in ('..', '../Neutral', '/Neutral'):
            with self.assertRaises(ValueError):
                self.invoke('--bundle', value, '--verify')


if __name__ == '__main__':
    unittest.main()
