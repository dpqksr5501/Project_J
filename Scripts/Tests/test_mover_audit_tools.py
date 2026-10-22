"""Real CSV and disposable Git regressions for the Mover audit tools."""
import csv
import importlib.util
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[2]


def module(name, relative):
    spec = importlib.util.spec_from_file_location(name, ROOT / relative)
    result = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(result)
    return result


buffer = module('mover_buffer', 'Scripts/Validation/Validate-MoverBufferCapture.py')
provenance = module('continuation_provenance', 'Scripts/Audit/continuation_provenance.py')


class MoverAuditToolsTests(unittest.TestCase):
    def setUp(self):
        parent = ROOT / 'Saved/Validation/MoverAudit/ToolTests'
        parent.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=parent)
        self.addCleanup(self.temp.cleanup)
        self.work = Path(self.temp.name)

    def capture(self, fault=None):
        rows = []
        for i in range(300):
            row = dict(World='Client', NetMode=3, WallSeconds=i / 60, LatestReceiveMS=200+i*16,
                       DesiredMS=100, InterpolationMS=100+i*16, BufferedMS=100,
                       WorldDeltaMS=16, ReceiveAdvanceMS=16, MaxMS=300)
            if i < 5 or (fault == 'late_reset' and i >= 250) or (fault == 'middle_reset' and i == 150):
                row.update(LatestReceiveMS=0, InterpolationMS=0)
            if fault == 'stopped':
                row.update(LatestReceiveMS=200, InterpolationMS=100, ReceiveAdvanceMS=0)
            rows.append(row)
        path = self.work / 'capture.csv'
        with path.open('w', newline='', encoding='utf-8') as stream:
            writer = csv.DictWriter(stream, fieldnames=rows[0]); writer.writeheader(); writer.writerows(rows)
        return path

    def test_healthy_capture_preserves_warmup_and_duration(self):
        result = buffer.validate(self.capture())['worlds'][0]
        self.assertEqual(result['warmup_frames'], 5)
        self.assertEqual(result['total_frames'], 300)
        self.assertAlmostEqual(result['total_duration_seconds'], 299 / 60)

    def test_stopped_supply_fails_with_default_options(self):
        with self.assertRaisesRegex(AssertionError, 'supply stopped'):
            buffer.validate(self.capture('stopped'))

    def test_late_reset_is_not_warmup(self):
        with self.assertRaisesRegex(AssertionError, 'reset or became invalid'):
            buffer.validate(self.capture('late_reset'))

    def test_middle_reset_is_not_filtered(self):
        with self.assertRaisesRegex(AssertionError, 'reset or became invalid'):
            buffer.validate(self.capture('middle_reset'))

    def test_ready_boundary_preserves_startup_diagnostics(self):
        path=self.work/'Run.log'
        path.write_text('LogNetworkPrediction: Warning: initial load\nMoverBufferCapture Started Seconds=120\nhealthy\n')
        result=buffer.validate_diagnostics(path)
        self.assertEqual(len(result['startup_diagnostics']),1)
        self.assertEqual(result['captured_diagnostics'],[])

    def test_diagnostic_after_capture_start_fails(self):
        path=self.work/'Run.log'
        path.write_text('MoverBufferCapture Started Seconds=120\nLogNetworkPrediction: Warning: late failure\n')
        with self.assertRaisesRegex(AssertionError,'during the captured stream'):
            buffer.validate_diagnostics(path)

    def test_missing_ready_capture_marker_fails(self):
        path=self.work/'Run.log';path.write_text('healthy but never captured\n')
        with self.assertRaisesRegex(AssertionError,'No explicit'):
            buffer.validate_diagnostics(path)

    def git_fixture(self):
        subprocess.run(['git', 'init', '-q', str(self.work)], check=True)
        for path in ('Source/Game.cpp', 'Plugins/Mover/Source/Mode.cpp',
                     'Plugins/NetworkPrediction/NetworkPrediction.uplugin'):
            full = self.work / path; full.parent.mkdir(parents=True, exist_ok=True); full.write_text('original')
        subprocess.run(['git','-C',str(self.work),'add','.'],check=True)
        return provenance.source_hashes(self.work)

    def test_unchanged_provenance_matches(self):
        frozen = self.git_fixture()
        self.assertEqual(provenance.compare_sources(self.work, frozen), [])

    def test_mover_and_network_prediction_changes_are_rejected(self):
        frozen = self.git_fixture()
        for path in ('Plugins/Mover/Source/Mode.cpp', 'Plugins/NetworkPrediction/NetworkPrediction.uplugin'):
            (self.work / path).write_text('changed')
        self.assertEqual(len(provenance.compare_sources(self.work, frozen)), 2)

    def test_new_plugin_source_is_rejected(self):
        frozen = self.git_fixture()
        (self.work / 'Plugins/Mover/Source/Added.cpp').write_text('new')
        self.assertEqual(len(provenance.compare_sources(self.work, frozen)), 1)

    def test_plugin_binary_changes_are_rejected(self):
        self.git_fixture()
        path = self.work / 'Plugins/Mover/Binaries/Win64/UnrealEditor-Mover.dll'
        path.parent.mkdir(parents=True); path.write_bytes(b'old')
        frozen = provenance.plugin_artifacts(self.work)
        path.write_bytes(b'new')
        self.assertEqual(provenance.check_artifacts(self.work, frozen), [path.relative_to(self.work).as_posix()])

    def test_content_worktree_changes_are_not_hidden_by_source_identity(self):
        self.git_fixture()
        path=self.work/'Content/CurrentMaster.uasset';path.parent.mkdir();path.write_bytes(b'first')
        frozen=provenance.content_worktree_hashes(self.work)
        self.assertEqual(provenance.content_worktree_hashes(self.work),frozen)
        path.write_bytes(b'second')
        self.assertNotEqual(provenance.content_worktree_hashes(self.work),frozen)


if __name__ == '__main__':
    unittest.main()
