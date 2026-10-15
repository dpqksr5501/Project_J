"""Selected public authoring properties only; no graph edits, reflection fallback or save."""
import json
import re
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
match = re.search(r'-ProjectJAuditReadName=([A-Za-z0-9_-]+)', unreal.SystemLibrary.get_command_line())
if not match:
    raise RuntimeError('Unique read name required')
out = root / 'Saved/Validation/Continuation_20261014' / match.group(1)
receipt = out / 'AssetConnections.json'
if receipt.exists():
    raise RuntimeError('Preserve previous read')
records = []
repair_style_only = '-ProjectJAuditStyleFieldRepair' in unreal.SystemLibrary.get_command_line()

def read_properties(obj, properties):
    result = {'object': obj.get_path_name(), 'values': {}, 'unverified': []}
    for name in properties:
        try:
            value = obj.get_editor_property(name)
            if isinstance(value, unreal.GameplayTag):
                value = str(unreal.GameplayTagLibrary.get_tag_name(value))
            elif isinstance(value, unreal.Object):
                value = value.get_path_name()
            elif isinstance(value, (list, unreal.Array)):
                value = [v.get_path_name() if isinstance(v, unreal.Object) else str(v) for v in value]
            elif not isinstance(value, (str, int, float, bool, type(None))):
                value = str(value)
            result['values'][name] = value
        except Exception as error:
            result['unverified'].append({'property': name, 'error': str(error), 'meaning_ko': '공개 조회 실패; property명/접근/환경 원인을 원문으로 구분하며 우회 없음'})
    return result

asset_paths = (['/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword'] if repair_style_only else
               ['/Game/DataAssetSets/Class/DA_Class_Greatsword', '/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword'])
for path in asset_paths:
    obj = unreal.load_asset(path)
    if not obj:
        records.append({'object': path, 'unverified': [{'error': 'load failed'}]})
        continue
    properties = (['class_id', 'schema_version', 'starting_level', 'ability_sets', 'default_combat_style']
                  if '/Class/' in path else ['combat_style_tag', 'schema_version', 'ability_sets', 'inline_abilities', 'inline_effects', 'derive_attack_catalog', 'attack_set'])
    if repair_style_only:
        properties = ['combat_style_tag', 'derive_attack_catalog']
    records.append(read_properties(obj, properties))

progression_class = unreal.load_class(None, '/Script/Project_JCharacter.Project_JProgressionComponent')
for path in ([] if repair_style_only else ['/Script/Project_J.Project_JPlayerState', '/Script/Project_JCharacter.Project_JNPCCharacter', '/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C']):
    cls = unreal.load_class(None, path)
    if not cls:
        records.append({'object': path, 'unverified': [{'error': 'class load failed'}]})
        continue
    default = unreal.get_default_object(cls)
    entry = {'object': default.get_path_name(), 'progression': []}
    components = default.get_components_by_class(progression_class)
    for component in components:
        entry['progression'].append(read_properties(component, ['maximum_level', 'experience_requirements']))
    if not components:
        entry['unverified'] = [{'error': 'CDO component enumeration returned no progression; no creation/hidden reflection fallback'}]
    records.append(entry)

receipt.write_text(json.dumps({'scope_ko': '레지스트리 근거로 지정한 class/style/native persistent-owner 및 NPC BP CDO의 공개 속성만 읽음. 전수 BP/DA 인스턴스/그래프·실서비스 검토 아님.',
                              'asset_mutation': False, 'asset_save': False, 'records': records}, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
unreal.log('Selected level authoring read retained; no asset changed or saved.')
