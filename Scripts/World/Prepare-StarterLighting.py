"""Owned per-map lighting and known template spawn cleanup; no global quality changes."""
import unreal
import runpy
from pathlib import Path
save_scope=runpy.run_path(str(Path(__file__).with_name('Starter-OwnedSave.py')))['OwnedSaveScope']()
actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
all_actors=actors.get_all_level_actors()
world=unreal.EditorLevelLibrary.get_editor_world()
assert world.get_path_name().startswith('/Game/World/Starter/Maps/L_StarterRegion.')
environment=next(a for a in all_actors if isinstance(a,unreal.Project_JWorldEnvironment))
sun=next(a for a in all_actors if isinstance(a,unreal.DirectionalLight))
save_scope.own(environment);save_scope.own(sun)
environment.set_editor_property('sun',sun)
environment.set_editor_property('clear_sun_intensity',60000)
start=next(a for a in all_actors if a.get_actor_label()=='StarterRegion_PlayerStart')
# Retire only the known OpenWorld template spawn, which is below the new terrain.
# Unrecognized authored starts remain untouched and fail the bounded verifier.
for candidate in all_actors:
    if isinstance(candidate,unreal.PlayerStart) and candidate!=start:
        if (candidate.get_actor_location()-unreal.Vector(-200,0,92)).length()<1 and 'ProjectJ.StarterRegion.v1' not in [str(t) for t in candidate.get_editor_property('tags')]:
            save_scope.own(candidate)
            assert actors.destroy_actor(candidate)
sun.modify()
light=sun.get_component_by_class(unreal.DirectionalLightComponent)
light.modify()
light.set_editor_property('intensity',60000)
sun.set_actor_rotation(unreal.Rotator(pitch=-45,yaw=-25,roll=0),False)
volume=next((a for a in all_actors if a.get_actor_label()=='StarterRegion_Exposure'),None)
if volume is None:
    volume=actors.spawn_actor_from_class(unreal.PostProcessVolume,unreal.Vector())
    save_scope.own(volume)
    volume.set_actor_label('StarterRegion_Exposure');volume.set_folder_path('StarterRegion/Systems')
save_scope.own(volume)
volume.set_editor_property('unbound',True)
volume.set_editor_property('is_spatially_loaded',False)
settings=volume.get_editor_property('settings')
settings.set_editor_property('override_auto_exposure_min_brightness',True)
settings.set_editor_property('override_auto_exposure_max_brightness',True)
settings.set_editor_property('auto_exposure_min_brightness',14)
settings.set_editor_property('auto_exposure_max_brightness',14)
volume.set_editor_property('settings',settings)
# Material usage can be updated by the renderer after the first import/save.
# Persist that actual parent state before the independent reload/Cook boundary.
for path in ['/Game/World/Starter/Materials/M_StarterTerrain','/Game/World/Starter/Materials/M_StarterRouteReview','/Game/World/Starter/Materials/M_StarterWaterReview']:
    material=unreal.load_asset(path);assert material
    save_scope.own(material);material.modify()
save_scope.save()
