"""Source identities, revisions and output collisions must survive renamed imports."""
import copy
import importlib.util
from pathlib import Path
import unittest

path = Path(__file__).resolve().parents[1] / 'Authoring/animation_import_catalog.py'
spec = importlib.util.spec_from_file_location('animation_import_catalog', path)
module = importlib.util.module_from_spec(spec)
spec.loader.exec_module(module)
ROOT = module.TARGET_ROOTS[0]


class ImportPlanningTests(unittest.TestCase):
    def setUp(self):
        self.entry = {'source_id': 'sample/Walk', 'source_signature': 'abc',
                      'profile_fingerprint': 'recipe1', 'target_package': ROOT + 'Walk',
                      'class': 'AnimSequence'}
        self.catalog = {'schema_version': 1, 'fingerprint_version': module.FINGERPRINT_VERSION,
                        'conversion_profile': {'fingerprint': 'recipe1'}, 'entries': [self.entry]}

    def plan(self, **overrides):
        candidate = dict(self.entry, **overrides)
        return module.plan_import(self.catalog, [candidate], 'recipe1')['rows'][0]

    def test_known_source_reuses(self):
        self.assertEqual(self.plan()['status'], 'reuse')

    def test_renamed_clip_reuses_content(self):
        row = self.plan(source_id='sample/SomeRenamedWalk', target_package=ROOT + 'Walk2')
        self.assertEqual(row['reuse_target'], ROOT + 'Walk')

    def test_same_name_revised_data_is_reviewed(self):
        self.assertEqual(self.plan(source_signature='changed')['status'], 'review')

    def test_different_source_cannot_overwrite(self):
        self.assertEqual(self.plan(source_id='sample/New', source_signature='new')['reason'],
                         'target_exists_never_overwrite')

    def test_case_collision_is_reviewed(self):
        self.assertEqual(self.plan(source_id='sample/New', source_signature='new',
                                   target_package=ROOT + 'walk')['status'], 'review')

    def test_same_batch_duplicate_uses_one_destination(self):
        first = dict(self.entry, source_id='sample/NewA', source_signature='new', target_package=ROOT + 'NewA')
        second = dict(first, source_id='sample/NewB', target_package=ROOT + 'NewB')
        rows = module.plan_import(self.catalog, [first, second], 'recipe1')['rows']
        self.assertEqual([r['status'] for r in rows], ['new', 'reuse'])
        self.assertEqual(rows[1]['reuse_target'], ROOT + 'NewA')

    def test_recipe_change_does_not_reuse_or_import(self):
        row = module.plan_import(self.catalog, [self.entry], 'recipe2')['rows'][0]
        self.assertEqual(row['status'], 'review')

    def test_ambiguous_existing_outputs_preserved(self):
        self.catalog['entries'].append(dict(self.entry, source_id='other/Walk', target_package=ROOT + 'WalkVariant'))
        self.assertEqual(self.plan(source_id='sample/Renamed')['status'], 'review')

    def test_unsafe_paths_rejected(self):
        self.assertFalse(module.valid_target(ROOT + '../Outside'))
        self.assertFalse(module.valid_target('/Game/Other/Clip'))

    def test_notify_semantics_change_signature(self):
        a = {'notifies': [{'time': 0.2, 'payload': 'left'}]}
        b = copy.deepcopy(a)
        b['notifies'][0]['payload'] = 'right'
        self.assertNotEqual(module.signature('a' * 64, a), module.signature('a' * 64, b))

    def test_additive_base_change_is_not_a_duplicate(self):
        a = {'ref_pose_motion_fingerprint': 'original_base'}
        b = {'ref_pose_motion_fingerprint': 'different_base'}
        self.assertNotEqual(module.signature('a' * 64, a), module.signature('a' * 64, b))

    def test_skeleton_policy_change_is_not_a_duplicate(self):
        a = {'skeleton_policy_sha256': 'original_policy'}
        b = {'skeleton_policy_sha256': 'different_policy'}
        self.assertNotEqual(module.signature('a' * 64, a), module.signature('a' * 64, b))


if __name__ == '__main__':
    unittest.main()
