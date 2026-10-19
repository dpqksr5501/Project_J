"""Prepare owned PCG texture inputs and a high-quality editor art preview."""
import json
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];BASE='/Game/World/MainWorld';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
world=unreal.EditorLevelLibrary.get_editor_world();assert world.get_path_name().startswith('/Game/World/MainWorld/Legacy/Maps/L_StarterRegion.')
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
lib=unreal.EditorAssetLibrary;path='/Game/Map_Resource/MainWorld/Materials/T_MainWorldForestPlacement';texture=unreal.load_asset(path)
if texture is None:
    task=unreal.AssetImportTask()
    for k,v in dict(filename=str(ROOT/'Content/World/MainWorld/Source/T_MainWorldForestPlacement.png'),destination_path='/Game/Map_Resource/MainWorld/Materials',destination_name='T_MainWorldForestPlacement',automated=True,save=False).items():task.set_editor_property(k,v)
    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task]);texture=unreal.load_asset(path);assert texture
    lib.set_metadata_tag(texture,'ProjectJ.Authoring','ProjectJ.Starter.WorldBiome.v2')
assert lib.get_metadata_tag(texture,'ProjectJ.Authoring')=='ProjectJ.Starter.WorldBiome.v2'
texture.set_editor_property('srgb',False);texture.set_editor_property('compression_settings',unreal.TextureCompressionSettings.TC_MASKS)
lib.save_loaded_asset(texture)
graph=unreal.load_asset(BASE+'/PCG/PCG_MainWorldForest');assert graph
lib.set_metadata_tag(graph,'ProjectJ.Authoring','ProjectJ.Starter.WorldBiome.v2');lib.save_loaded_asset(graph)
keys=['sg.ViewDistanceQuality','sg.AntiAliasingQuality','sg.ShadowQuality','sg.GlobalIlluminationQuality','sg.ReflectionQuality','sg.TextureQuality','sg.EffectsQuality','sg.FoliageQuality']
report=OUT/'art-preview-settings.json'
if not report.exists():report.write_text(json.dumps({k:unreal.SystemLibrary.get_console_variable_int_value(k) for k in keys},indent=2),encoding='utf-8')
for key in keys:unreal.SystemLibrary.execute_console_command(world,key+' 3')
unreal.EditorLevelLibrary.editor_set_game_view(True)
unreal.log('STARTER_PCG_INPUTS_READY')
