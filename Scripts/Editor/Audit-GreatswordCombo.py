"""Read-only audit of the authored greatsword combo and its montage windows."""
import json
import os
import unreal

combo = unreal.load_asset('/Game/DataAssetSets/Combo/DA_Combo_Greatsword')
rows = []
for node in combo.get_editor_property('nodes'):
    attack = node.get_editor_property('attack_definition')
    montage = attack.get_editor_property('montage')
    events = unreal.AnimationLibrary.get_animation_notify_events(montage)
    rows.append({
        'node': str(unreal.GameplayTagLibrary.get_tag_name(node.get_editor_property('node_tag'))),
        'montage': montage.get_path_name(),
        'length': montage.get_play_length(),
        'events': [e.export_text() for e in events],
    })
root = os.path.join(unreal.Paths.project_saved_dir(), 'Validation/Combo_20261010')
os.makedirs(root, exist_ok=True)
with open(os.path.join(root, 'AuthoredCombo.json'), 'w', encoding='utf-8') as stream:
    json.dump(rows, stream, indent=2)
unreal.log('COMBO_AUDIT: inspected %d nodes' % len(rows))
