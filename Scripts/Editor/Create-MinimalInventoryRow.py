"""Create minimal list/tile skins and the default UI profile catalog.
No existing designer assets are modified. Run after the native Editor build."""
import unreal
path = '/Game/UI/WBP_ProjectJItemRow'
if not unreal.EditorAssetLibrary.does_asset_exist(path):
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property('parent_class', unreal.Project_JItemWidget.static_class())
    blueprint = unreal.AssetToolsHelpers.get_asset_tools().create_asset('WBP_ProjectJItemRow', '/Game/UI', unreal.WidgetBlueprint, factory)
    assert blueprint
    unreal.BlueprintEditorLibrary.compile_blueprint(blueprint)
    assert unreal.EditorAssetLibrary.save_loaded_asset(blueprint, only_if_is_dirty=False)
    print('PROJECT_J_DEFAULT_ROW_CREATED')

tile_path = '/Game/UI/WBP_ProjectJItemTile'
if not unreal.EditorAssetLibrary.does_asset_exist(tile_path):
    factory = unreal.WidgetBlueprintFactory()
    factory.set_editor_property('parent_class', unreal.Project_JItemWidget.static_class())
    tile = unreal.AssetToolsHelpers.get_asset_tools().create_asset('WBP_ProjectJItemTile', '/Game/UI', unreal.WidgetBlueprint, factory)
    assert tile
    unreal.BlueprintEditorLibrary.compile_blueprint(tile)
    tile_class = unreal.EditorAssetLibrary.load_blueprint_class(tile_path)
    unreal.get_default_object(tile_class).set_editor_property('compact_tile', True)
    assert unreal.EditorAssetLibrary.save_loaded_asset(tile, only_if_is_dirty=False)
    print('PROJECT_J_DEFAULT_TILE_CREATED')

profile_path = '/Game/UI/DA_ProjectJUIDefault'
catalog_path = '/Game/UI/DA_ProjectJUIProfiles'
if not unreal.EditorAssetLibrary.does_asset_exist(catalog_path):
    tools = unreal.AssetToolsHelpers.get_asset_tools()
    profile = unreal.load_asset(profile_path) if unreal.EditorAssetLibrary.does_asset_exist(profile_path) else None
    if not profile:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.Project_JCharacterUIProfile.static_class())
        profile = tools.create_asset('DA_ProjectJUIDefault', '/Game/UI', unreal.DataAsset, factory)
        assert profile
        profile.set_editor_property('profile_id', 'Default')
        # No fictional class/resource mechanics: inherit actual HP/MP and the Controller input slots.
        assert unreal.EditorAssetLibrary.save_loaded_asset(profile, only_if_is_dirty=False)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property('data_asset_class', unreal.Project_JUIProfileCatalog.static_class())
    catalog = tools.create_asset('DA_ProjectJUIProfiles', '/Game/UI', unreal.DataAsset, factory)
    assert catalog
    catalog.set_editor_property('profiles', [profile])
    assert unreal.EditorAssetLibrary.save_loaded_asset(catalog, only_if_is_dirty=False)
    print('PROJECT_J_DEFAULT_UI_PROFILE_CREATED')
