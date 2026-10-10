"""Read only, six imported Run clips. Original GASP is mounted by the existing
recovery helper and is never saved. Run in Project J's 5.8 editor."""
import json
import os
import unreal

names = ['Box_LR_F_Lfoot', 'Box_LR_F_Rfoot', 'Box_RL_F_Lfoot',
         'Box_RL_F_Rfoot', 'Pivot_B_F_Lfoot', 'Pivot_B_F_Rfoot']
packages = ['/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_' + n for n in names]
source = 'C:/Users/I/Documents/Unreal Projects/GameAnimationSample/Content'
root = os.path.join(unreal.Paths.project_saved_dir(), 'Validation/MMOUI_20261010')
os.makedirs(root, exist_ok=True)
# A read-only helper establishes original /Game/Blueprints and /FoleySource mounts
# during load; the original sequences remain loaded after the scope ends.
recovery = json.loads(unreal.Project_JFoleyMigrationLibrary.repair_missing_gasp_foley(packages, source, False))
assert not recovery['errors'], recovery
rows = []
for path in packages:
    original_path = '/FoleySource/' + path[6:]
    original = unreal.find_object(None, original_path + '.' + path.rsplit('/', 1)[1])
    assert original, original_path
    target = unreal.load_asset(path)
    target_events = unreal.AnimationLibrary.get_animation_notify_events(target)
    for event in unreal.AnimationLibrary.get_animation_notify_events(original):
        notify = event.notify_state_class
        if not notify or 'EarlyTransition' not in notify.get_class().get_name():
            continue
        payload = {}
        for prop in ['TransitionDestination', 'TransitionCondition', 'GaitNotEqual']:
            payload[prop] = str(notify.get_editor_property(prop))
        rows.append({'package': path, 'source_event': event.export_text(),
                     'source_notify': notify.get_path_name(), 'payload': payload,
                     'target_events': [e.export_text() for e in target_events]})
assert len({r['package'] for r in rows}) == 6, rows
with open(os.path.join(root, 'EarlyTransitionSource.json'), 'w', encoding='utf-8') as f:
    json.dump(rows, f, indent=2)
print('EARLY_TRANSITION_SOURCE_AUDIT', len(rows))
