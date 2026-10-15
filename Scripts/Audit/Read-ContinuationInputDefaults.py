"""Read selected authored input CDOs and Greatsword notify connections using public APIs."""
from pathlib import Path
import json
import re
import unreal

root = Path(unreal.Paths.project_dir())
match = re.search(r'-ProjectJAuditReadName=([A-Za-z0-9_-]+)', unreal.SystemLibrary.get_command_line())
name = match.group(1) if match else 'InputDefaults1'
out = root / 'Saved/Validation/Continuation_20261014' / name
receipt = out / 'AssetConnections.json'
if receipt.exists():
    raise RuntimeError('Preserve prior evidence')
out.mkdir(parents=True, exist_ok=True)
errors = []

def path(value):
    if value is None:
        return None
    if isinstance(value, unreal.GameplayTag):
        return str(unreal.GameplayTagLibrary.get_tag_name(value))
    if isinstance(value, unreal.Object):
        return value.get_path_name()
    if isinstance(value, (list, tuple, unreal.Array)):
        return [path(v) for v in value]
    return str(value)

def read_properties(obj, names):
    result = {}
    for name in names:
        try:
            result[name] = path(obj.get_editor_property(name))
        except Exception as error:
            errors.append({'object': path(obj), 'property': name, 'error': str(error)})
    return result

gm_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/ThirdPerson/Blueprints/BP_Project_JGameMode')
if not gm_class:
    raise RuntimeError('Configured global GameMode blueprint missing')
gm = unreal.get_default_object(gm_class)
controller_class = gm.get_editor_property('player_controller_class')
pawn_class = gm.get_editor_property('default_pawn_class')
controller = unreal.get_default_object(controller_class) if controller_class else None
pawn = unreal.get_default_object(pawn_class) if pawn_class else None
greatsword_class = unreal.EditorAssetLibrary.load_blueprint_class('/Game/Character_BPs/GreatSword/BP_GreatSword')
greatsword = unreal.get_default_object(greatsword_class) if greatsword_class else None
actions = ['jump_action','move_action','move_intent_forward_action','move_intent_backward_action',
           'move_intent_left_action','move_intent_right_action','look_action','mouse_look_action',
           'sprint_action','toggle_combat_action','attack_action','heavy_attack_action','skill_modifier_action',
           'interact_action','prototype_starting_weapon','sprint_ability_tag']
registry = json.loads((root / 'Saved/Validation/Continuation_20261014/AssetRegistry1/registry.json').read_text(encoding='utf-8-sig'))
packages = sorted({row['package'] for row in registry['assets']
                   if row['package'].startswith('/Game/Anim_Assets/Great_Sword/Animations/Sword/Montage/')
                   and 'AnimMontage' in row['asset_class_path']})
montages = []
for package in packages:
    montage = unreal.load_asset(package)
    if not montage:
        errors.append({'asset': package, 'error': 'load failed'})
        continue
    notifies = []
    try:
        # Official BlueprintPure copy API. Do not access the protected Notifies property.
        for index, event in enumerate(unreal.AnimationLibrary.get_animation_notify_events(montage)):
            state = event.get_editor_property('notify_state_class')
            if not state:
                continue
            name = state.get_class().get_name()
            row = {'index': index, 'state': path(state), 'class': name}
            if name == 'Project_JAnimNotifyState_WeaponMotion':
                row['motion_key_count'] = len(state.get_editor_property('motion_keys'))
            notifies.append(row)
    except Exception as error:
        notifies = None
        errors.append({'asset': package, 'public_api': 'AnimationLibrary.get_animation_notify_events', 'error': str(error)})
    montages.append({'asset': package, 'state_notifies': notifies})
result = {
    'scope_ko': 'Config global GameMode와 그 Controller/Pawn CDO, actual Greatsword BP CDO의 입력 property 및 기존 registry에서 관련 Sword montage만 public API로 로드. 실제 레벨 WorldSettings override·runtime instance·focus/device/BP graph 전체 완료가 아님.',
    'asset_mutation': False, 'asset_save': False,
    'game_mode': {'class': path(gm_class), 'defaults': read_properties(gm, ['player_controller_class','default_pawn_class'])},
    'controller': {'class': path(controller_class), 'defaults': read_properties(controller, ['default_mapping_contexts','mobile_excluded_mapping_contexts','force_touch_controls'])} if controller else None,
    'default_pawn': {'class': path(pawn_class), 'defaults': read_properties(pawn, actions)} if pawn else None,
    'greatsword': {'class': path(greatsword_class), 'defaults': read_properties(greatsword, actions)} if greatsword else None,
    'montages': montages, 'errors': errors,
    'notify_read_api': 'UE5.8 UAnimationBlueprintLibrary::GetAnimationNotifyEvents (BlueprintPure public copy API)',
    'interpretation_ko': 'CDO 값은 실제 instance의 현재 연결/소유권과 다르다. inherited IMC DefaultKeyMappings27행을 다시 재검사하지 않는다. notify 존재만으로 montage가 선택·실행되거나 품질이 검증됐다고 표시하지 않는다. API read 실패는 unknown이며 빈 배열·notify0으로 해석하지 않는다.'
}
receipt.write_text(json.dumps(result, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.log(f'Continuation selected asset connections: montage={len(montages)} errors={len(errors)} save=0')
