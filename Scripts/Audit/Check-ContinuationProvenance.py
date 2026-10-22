"""Reject changed sources, plugin artifacts, or Cook editor binaries before deployment."""
import json
from pathlib import Path
import sys
import subprocess
from continuation_provenance import compare_sources, plugin_artifacts, check_artifacts, content_worktree_hashes

root = Path(__file__).resolve().parents[2]
identity = json.loads(Path(sys.argv[1]).read_text(encoding='utf-8-sig'))
version = root / 'Saved/Validation/Continuation_20261014' / identity['editor_frozen_version']
frozen = json.loads((version / 'SourceHashes.json').read_text(encoding='utf-8-sig'))
errors = compare_sources(root, frozen)
errors += check_artifacts(root, identity['artifacts'])
errors += check_artifacts(root,identity['assets'])
if content_worktree_hashes(root) != json.loads((version/'ContentWorktreeHashes.json').read_text(encoding='utf-8-sig')):
    errors.append('Content worktree changed')
if (version/'Head.txt').read_text(encoding='utf-8').strip() != subprocess.check_output(['git','rev-parse','HEAD'],cwd=root).decode().strip():
    errors.append('HEAD changed')
expected_plugins = [r for r in identity['artifacts'] if r['path'].startswith('Plugins/')]
if plugin_artifacts(root) != expected_plugins:
    errors.append('Plugin artifact set changed')
if sys.argv[2] == 'Cook':
    errors += check_artifacts(root, json.loads((version / 'EditorBinaryHashes.json').read_text(encoding='utf-8-sig')))
if errors:
    raise SystemExit('Continuation provenance mismatch: ' + json.dumps(errors, ensure_ascii=False))
print('Continuation source/plugin/binary provenance matched')
