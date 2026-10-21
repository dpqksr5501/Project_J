"""Fresh-process asset verification; no project asset writes or deletions."""
import hashlib
import json
import re
from pathlib import Path
import unreal

root = Path(unreal.Paths.project_dir())
match = re.search(r'-ProjectJAssetValidationName=([A-Za-z0-9_]+)', unreal.SystemLibrary.get_command_line())
if not match:
    raise RuntimeError('Dedicated verification name required')
out = root / 'Saved/Validation/AssetCleanup_20261015' / match.group(1)
receipt = out / 'Verification.json'
if receipt.exists():
    raise RuntimeError('Preserve previous verification')
errors = []
registry = unreal.AssetRegistryHelpers.get_asset_registry()
registry.search_all_assets(synchronous_search=True)
assets = [a for a in registry.get_all_assets(include_only_on_disk_assets=True) if str(a.package_name).startswith('/Game/')]
options = {
    'hard': unreal.AssetRegistryDependencyOptions(include_hard_package_references=True, include_soft_package_references=False,
        include_searchable_names=False, include_soft_management_references=False, include_hard_management_references=False),
    'soft': unreal.AssetRegistryDependencyOptions(include_hard_package_references=False, include_soft_package_references=True,
        include_searchable_names=False, include_soft_management_references=False, include_hard_management_references=False),
    'management': unreal.AssetRegistryDependencyOptions(include_hard_package_references=False, include_soft_package_references=False,
        include_searchable_names=False, include_soft_management_references=True, include_hard_management_references=True),
}
metadata = []
for asset in assets:
    cls = str(asset.asset_class_path.package_name) + '.' + str(asset.asset_class_path.asset_name)
    if cls in ('/Script/Engine.Blueprint', '/Script/Engine.AnimBlueprint', '/Script/UMGEditor.WidgetBlueprint',
               '/Script/GameplayAbilities.GameplayAbilityBlueprint') or cls.startswith('/Script/Project_J'):
        metadata.append({'package':str(asset.package_name), 'class':cls,
            'referencers':{kind:sorted(str(x) for x in registry.get_referencers(asset.package_name, opt)) for kind,opt in options.items()},
            'dependencies':{kind:sorted(str(x) for x in registry.get_dependencies(asset.package_name, opt)) for kind,opt in options.items()}})

bp_path = '/Game/Character_BPs/GreatSword/BP_GreatSword'
bp = unreal.load_asset(bp_path)
assert bp, 'Saved production BP reload failed'
assert unreal.BlueprintEditorLibrary.compile_blueprint(bp), 'Reloaded BP compile failed'
graphs = [g.get_name() for g in unreal.BlueprintEditorLibrary.list_graphs(bp)]
retired = ('IsDead', 'GetCharacterLevel', 'GetCombatSocketLocation')
assert not set(retired).intersection(graphs), 'Constant-return graphs remain after fresh reload'
assert set(graphs) == {'UserConstructionScript', 'EventGraph'}, 'Unrelated graph set changed'
functions = []
for f in unreal.BlueprintEditorLibrary.list_functions(bp):
    if str(f.get_editor_property('name')) in retired:
        functions.append({'name':str(f.get_editor_property('name')), 'implemented_in_bp':bool(f.get_editor_property('is_implemented'))})
assert len(functions) == 3 and not any(f['implemented_in_bp'] for f in functions), 'Expected inherited native functions'

foundation = unreal.load_asset('/Game/Animation_Logic/ABPs/ABP_Player_Master')
assert foundation and unreal.BlueprintEditorLibrary.compile_blueprint(foundation), 'GASP foundation compile failed'
foundation_graphs = unreal.BlueprintEditorLibrary.list_graphs(foundation)
assert not unreal.EditorAssetLibrary.does_asset_exist('/Game/Animation_Logic/ABPs/ABP_Player'), 'Retired test ABP returned'
assert not unreal.EditorAssetLibrary.does_asset_exist('/Game/Animation_Logic/ABPs/ABP_Humanoid_Master'), 'Retired master returned'
production_class = unreal.get_default_object(bp.generated_class()).get_source_mesh().get_editor_property('AnimClass')
assert production_class == foundation.generated_class(), 'Production source mesh does not use GASP'
gasp_nodes = []
for graph in foundation_graphs:
    editor = unreal.BlueprintGraphEditor.get_graph_editor(graph)
    for node in editor.list_all_nodes():
        name = node.get_class().get_name()
        assert name not in ('AnimGraphNode_ProjectJLocomotion', 'AnimGraphNode_ProjectJOneShotHandoff'), 'Retired playback owner returned'
        if name in ('AnimGraphNode_BlendStack', 'AnimGraphNode_Slot'):
            gasp_nodes.append({'node': node.get_path_name(), 'class': name})
assert any(n['class'] == 'AnimGraphNode_BlendStack' for n in gasp_nodes), 'Actual GASP stack missing'

# Verify the generator against compiled native defaults and existing assets, without creating anything.
defaults = [
    (unreal.Project_JHUDStyle, {'quick_slot_size':46., 'item_size':46., 'font_size':12}),
    (unreal.Project_JMapDefinition, {'center':unreal.Vector2D(0,0), 'half_extent':unreal.Vector2D(5000,5000)}),
    (unreal.Project_JConsumableDefinition, {'cooldown_seconds':10.}),
]
for cls, fields in defaults:
    cdo = unreal.get_default_object(cls.static_class())
    for key, expected in fields.items():
        assert cdo.get_editor_property(key) == expected, 'Generator/native default mismatch: ' + key
generator_assets = ['UI/DA_ProjectJHUDStyle', 'UI/DA_ProjectJMap', 'UI/Gameplay/DA_ProjectJFirstSteps',
                    'UI/Gameplay/DA_ProjectJHealthPotion', 'UI/Gameplay/DA_ProjectJManaPotion']
before = {}
for path in generator_assets:
    assert unreal.EditorAssetLibrary.does_asset_exist('/Game/' + path), 'Refuse to create missing prototype assets'
    file = root / 'Content' / (path + '.uasset')
    before[path] = hashlib.sha256(file.read_bytes()).hexdigest()
generator = root / 'Scripts/Editor/Create-CompactHUDData.py'
exec(compile(generator.read_text(encoding='utf-8-sig'), str(generator), 'exec'), {'__name__':'__asset_cleanup_verification__'})
assert all(hashlib.sha256((root / 'Content' / (p + '.uasset')).read_bytes()).hexdigest() == h for p,h in before.items()), 'Existing authored data was overwritten'
item_paths = ['/Game/UI/Gameplay/DA_ProjectJHealthPotion', '/Game/UI/Gameplay/DA_ProjectJManaPotion',
              '/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip']
items = [{'asset':p, 'item_id':str(unreal.load_asset(p).get_editor_property('item_id'))} for p in item_paths]
assert len({r['item_id'] for r in items}) == 3 and all(r['item_id'] != 'None' for r in items), 'Existing item identity collision'

receipt.write_text(json.dumps({'asset_save':False, 'asset_delete':False, 'game_package_count':len(assets),
    'selected_registry_metadata':metadata, 'bp':bp_path, 'reloaded_graphs':graphs, 'inherited_functions':functions,
    'gasp_reloaded_playback_nodes':gasp_nodes,
    'native_generator_defaults_verified':True, 'existing_generator_assets_unchanged':before, 'items':items,
    'scope_ko':'fresh load/compile·세 native 함수 상속·제작 기본값·기존 데이터 보존. registry 분류는 상세 graph/asset 전수 완료 아님.'}, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
unreal.log('ProjectJ asset architecture fresh reload and authoring verification passed; save/delete=0.')
