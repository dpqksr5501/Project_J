"""Continuation checks use the same Source/Scripts/Config/Plugins scope as Source-Provenance.ps1."""
import hashlib
from pathlib import Path
import subprocess

SOURCE_ROOTS = ('Source', 'Scripts', 'Config', 'Plugins', 'Project_J.uproject')


def source_hashes(root):
    paths = subprocess.check_output(
        ['git', '-c', 'core.fsmonitor=false', 'ls-files', '--cached', '--others',
         '--exclude-standard', '--', *SOURCE_ROOTS], cwd=root).decode().splitlines()
    return [{'path': p, 'sha256': digest(root / p), 'deleted': not (root / p).is_file()}
            for p in sorted(set(paths))]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest() if path.is_file() else None


def compare_sources(root, frozen):
    expected = {row['path']: row['sha256'] for row in frozen}
    actual = {row['path']: row['sha256'] for row in source_hashes(root)}
    return [{'path': path, 'expected': expected.get(path), 'actual': actual.get(path)}
            for path in sorted(expected.keys() | actual.keys())
            if path not in expected or path not in actual or expected[path] != actual[path]]


def plugin_artifacts(root):
    return [{'path': p.relative_to(root).as_posix(), 'sha256': digest(p)}
            for p in sorted((root / 'Plugins').glob('*/Binaries/Win64/*'))
            if p.is_file() and p.suffix in ('.dll', '.modules')]


def check_artifacts(root, frozen):
    return [row['path'] for row in frozen if digest(root / row['path']) != row['sha256']]


def content_worktree_hashes(root):
    paths = subprocess.check_output(
        ['git', '-c', 'core.fsmonitor=false', 'ls-files', '--modified', '--others',
         '--exclude-standard', '--', 'Content'], cwd=root).decode().splitlines()
    return [{'path': p, 'sha256': digest(root / p)} for p in sorted(set(paths))]
