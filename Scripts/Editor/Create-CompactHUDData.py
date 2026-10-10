"""Author only new Project J prototype definitions; never overwrite designer assets."""
import unreal

def create(name, folder, cls, fields):
    path = folder + '/' + name
    if unreal.EditorAssetLibrary.does_asset_exist(path):
        return unreal.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', cls.static_class())
    asset = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.DataAsset, factory)
    assert asset
    for key, value in fields.items():
        asset.set_editor_property(key, value)
    assert unreal.EditorAssetLibrary.save_loaded_asset(asset, only_if_is_dirty=False)
    print('PROJECT_J_COMPACT_DATA_CREATED ' + path)
    return asset

create('DA_ProjectJHUDStyle','/Game/UI',unreal.Project_JHUDStyle,{
    'quick_slot_size':46.0, 'item_size':46.0, 'font_size':12,
})
create('DA_ProjectJMap','/Game/UI',unreal.Project_JMapDefinition,{
    'map_name':unreal.Text('시작 지역'),
    'center':unreal.Vector2D(0,0), 'half_extent':unreal.Vector2D(5000,5000),
})
create('DA_ProjectJFirstSteps','/Game/UI/Gameplay',unreal.Project_JQuestDefinition,{
    'quest_id':'FirstSteps', 'title':unreal.Text('첫걸음'),
    'description':unreal.Text('시작 지역에서 20m 이동하세요. 완료 후 의뢰 창에서 경험치 보상을 받습니다.'),
    'objective_event':'TravelMetres','required_count':20,'reward_experience':100,'travel_objective':True,
})
create('DA_ProjectJHealthPotion','/Game/UI/Gameplay',unreal.Project_JConsumableDefinition,{
    'item_id':'HealthPotion','item_name':unreal.Text('체력 물약'), 'item_description':unreal.Text('체력을 최대 25 회복합니다.'),
    'max_stack_count':99,'restore_health':25.0,'cooldown_seconds':10.0,
})
create('DA_ProjectJManaPotion','/Game/UI/Gameplay',unreal.Project_JConsumableDefinition,{
    'item_id':'ManaPotion','item_name':unreal.Text('마나 물약'), 'item_description':unreal.Text('마나를 최대 25 회복합니다.'),
    'max_stack_count':99,'restore_mana':25.0,'cooldown_seconds':10.0,
})
