"""Focused regressions for the 6850ed45 audit; no editor or real asset writes.

Run: python -m unittest discover -s Scripts/Tests -p test_source_audit_tools.py
"""
import importlib.util
import json
from pathlib import Path
import runpy
import shutil
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


class SourceAuditToolsTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'Saved/Audit6850/ToolTests'
        parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)

    def test_console_executes_guarded_and_unguarded_scripts(self):
        spec = importlib.util.spec_from_file_location('audit_console', ROOT / 'Scripts/Editor/WorldEditorConsole.py')
        console = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(console)
        for guarded in (False, True):
            fixture = self.work / 'entry.py'
            fixture.write_text("if __name__ == '__main__':\n    receipt = 'done'\n" if guarded else "receipt = 'done'\n", encoding='utf-8')
            commands = []
            console.console = commands.append
            console.script(fixture)
            captured = {}
            original = runpy.run_path
            try:
                runpy.run_path = lambda *a, **kw: captured.update(original(*a, **kw))
                exec(commands[0].removeprefix('py '), {})
            finally:
                runpy.run_path = original
            self.assertEqual(captured.get('receipt'), 'done', f'guarded={guarded}')

    def summarize(self, text):
        source = self.work / 'frames.csv'
        output = self.work / 'summary.json'
        source.write_text(text, encoding='utf-8')
        result = subprocess.run([sys.executable, str(ROOT / 'Scripts/World/Summarize-StarterPerformance.py'),
                                 str(source), '--output', str(output)], capture_output=True, text=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(output.read_text(encoding='utf-8'))

    def test_late_csv_column_excludes_unmeasured_frames(self):
        summary = self.summarize('EVENTS,FrameTime\n,10\n,10\nEVENTS,FrameTime,GPUTime\n,10,10\n,10,20\n')
        gpu = summary['measurements']['GPUTime']
        self.assertEqual(gpu['mean'], 15)
        self.assertEqual(gpu['samples'], 2)
        self.assertEqual(gpu['missing'], 2)
        self.assertEqual(summary['frames'], 4)

    def test_missing_metric_has_explicit_zero_samples(self):
        summary = self.summarize('EVENTS,FrameTime,GPUTime\n,10,\n,10,\n')
        gpu = summary['measurements']['GPUTime']
        self.assertEqual(gpu['samples'], 0)
        self.assertEqual(gpu['missing'], 2)
        self.assertIsNone(gpu['mean'])

    def stage(self, manifest, seeds=None):
        # Execute unchanged production bytes in a disposable checkout so a
        # pre-fix successful copy cannot touch the real project's Content.
        project = self.work / 'Project'
        script_dir = project / 'Scripts/World'
        editor_dir = project / 'Scripts/Editor'
        script_dir.mkdir(parents=True)
        editor_dir.mkdir(parents=True)
        script = script_dir / 'Stage-WorldAssets.py'
        shutil.copy2(ROOT / 'Scripts/World/Stage-WorldAssets.py', script)
        shutil.copy2(ROOT / 'Scripts/Editor/World-MCP.py', editor_dir / 'World-MCP.py')
        content = self.work / 'Library/Content'
        (content / 'Pack').mkdir(parents=True)
        for name in ('A', 'B'):
            (content / 'Pack' / (name + '.uasset')).write_bytes(name.encode())
        manifest_path = self.work / 'manifest.json'
        manifest_path.write_text(json.dumps(manifest), encoding='utf-8')
        seed_path = self.work / 'seeds.json'
        seed_path.write_text(json.dumps(seeds or ['/Game/Pack/A']), encoding='utf-8')
        result = subprocess.run([sys.executable, str(script), '--source-content', str(content),
                                 '--scope', '/Game/Pack/', '--seed-file', str(seed_path),
                                 '--resolved-manifest', str(manifest_path), '--output', str(self.work / 'receipt.json')],
                                capture_output=True, text=True)
        return result, project

    @staticmethod
    def complete_manifest():
        return dict(source_mcp_checked=True, seeds=['/Game/Pack/A'], packages=['/Game/Pack/A', '/Game/Pack/B'],
                    missing_source_packages=[], unresolved_external_game_dependencies=[],
                    dependency_edges={'/Game/Pack/A': ['/Game/Pack/B'], '/Game/Pack/B': []})

    def test_complete_dependency_closure_copies_and_receipts(self):
        result, project = self.stage(self.complete_manifest())
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((project / 'Content/Pack/A.uasset').read_bytes(), b'A')
        self.assertEqual((project / 'Content/Pack/B.uasset').read_bytes(), b'B')
        self.assertTrue((self.work / 'receipt.json').exists())

    def test_missing_dependency_is_rejected_before_any_copy(self):
        manifest = self.complete_manifest()
        manifest['missing_source_packages'] = ['/Game/Pack/Lost']
        result, project = self.stage(manifest)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((project / 'Content').exists())

    def test_missing_requested_seed_is_rejected_before_any_copy(self):
        result, project = self.stage(self.complete_manifest(), ['/Game/Pack/A', '/Game/Pack/Lost'])
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((project / 'Content').exists())

    def test_incomplete_edge_closure_is_rejected_before_any_copy(self):
        manifest = self.complete_manifest()
        manifest['packages'].remove('/Game/Pack/B')
        result, project = self.stage(manifest)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((project / 'Content').exists())

    def test_external_game_dependency_is_rejected_before_any_copy(self):
        manifest = self.complete_manifest()
        manifest['unresolved_external_game_dependencies'] = ['/Game/Elsewhere/C']
        result, project = self.stage(manifest)
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse((project / 'Content').exists())


if __name__ == '__main__':
    unittest.main()
