"""Temporarily allow background rendering during a visual capture, then restore.

EditorEngine disables rendering when unfocused and CPU throttling is enabled.
Waiting in a host process otherwise cannot settle grass, occlusion or streaming.
No config is saved and no rendering quality or content is changed.
"""
import json
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2]
LABEL=globals()['LABEL'];OP=globals()['OP']
out=ROOT/'Saved/Validation/StarterWorld/VisualHarness'/LABEL/'session.json'
settings=unreal.get_default_object(unreal.load_class(None,'/Script/UnrealEd.EditorPerformanceSettings'))
if OP=='begin':
    assert not out.exists()
    level=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    r=dict(background_throttle_before=settings.get_editor_property('bThrottleCPUWhenNotForeground'),game_view_before=level.editor_get_game_view(),config_saved=False)
    settings.set_editor_property('bThrottleCPUWhenNotForeground',False)
    level.editor_set_game_view(True)
    r['phase']='ACTIVE';out.write_text(json.dumps(r,indent=2))
elif OP=='end':
    r=json.loads(out.read_text());assert r['phase']=='ACTIVE'
    settings.set_editor_property('bThrottleCPUWhenNotForeground',r['background_throttle_before'])
    unreal.get_editor_subsystem(unreal.LevelEditorSubsystem).editor_set_game_view(r['game_view_before'])
    r['phase']='RESTORED';out.write_text(json.dumps(r,indent=2))
else:raise ValueError(OP)
