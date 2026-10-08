"""Inspect or create the initial mannequin Foley profile from migrated audio.

Run in Project_J's editor Python console via exec(open(...).read()). Inspection
is the default. Explicitly set FOLEY_SETUP_APPLY=True to create/link the profile.
Existing authored profiles and non-empty character assignments are never replaced.
No map, animation, Blueprint or migrated audio asset is saved by this script.
"""
import json
from pathlib import Path
import unreal

APPLY = bool(globals().get('FOLEY_SETUP_APPLY', False))
PROFILE_PATH = '/Game/DataAssetSets/Audio/DA_Mannequin_Foley'
CHARACTER_PATH = '/Game/DataAssetSets/Animation_Profiles/AnimProfiles/DA_GreatSword_AnimProfile'
PRESETS = '/Game/Audio/Foley/MetaSounds/Presets/'
REPORT_PATH = Path(unreal.Paths.project_saved_dir()) / 'Validation/FoleyExtension_20261008/AudioSetup.json'

# This table is initial CONTENT authoring, never a runtime path/naming contract.
ROWS = [
    ('Walk', 'Walk', 'FOOTSTEP', 'FOOT', True),
    ('Run', 'Run_Soft', 'FOOTSTEP', 'FOOT', True),
    ('Jump', 'Jump', 'JUMP', 'CAPSULE', False),
    ('Land', 'Land', 'LAND', 'CAPSULE', True),
    ('Scuff', 'Scuff', 'SCUFF', 'FOOT', True),
    ('Handplant', 'Handplant', 'OTHER', 'HAND', True),
    ('RunBackwds', 'RunBackwards', 'FOOTSTEP', 'FOOT', True),
    ('ScuffPivot', 'ScuffPivot', 'SCUFF', 'FOOT', True),
    ('ScuffWall', 'ScuffWall', 'OTHER', 'FOOT', True),
    ('RunStrafe', 'RunStrafe', 'FOOTSTEP', 'FOOT', True),
    ('Tumble', 'Tumble', 'OTHER', 'CAPSULE', True),
    ('WalkBackwds', 'WalkBackwards', 'FOOTSTEP', 'FOOT', True),
]
FALLBACKS = {'RunStrafe': 'Run', 'RunBackwds': 'Run', 'WalkBackwds': 'Walk',
             'ScuffPivot': 'Scuff', 'ScuffWall': 'Scuff'}

def tag(name):
    value = unreal.GameplayTag()
    value.import_text('(TagName="Foley.Event.' + name + '")')
    return value

character = unreal.load_asset(CHARACTER_PATH)
if not isinstance(character, unreal.Project_JCharacterAnimProfile):
    raise RuntimeError('Expected character animation profile is missing: ' + CHARACTER_PATH)
sounds = {}
for event, preset, group, contact, trace in ROWS:
    path = PRESETS + 'MSS_FoleySound_' + preset
    sound = unreal.load_asset(path)
    if not isinstance(sound, unreal.SoundBase):
        raise RuntimeError('Migrate the audio dependency first: ' + path)
    sounds[event] = sound

profile = unreal.load_asset(PROFILE_PATH) if unreal.EditorAssetLibrary.does_asset_exist(PROFILE_PATH) else None
if profile is not None and not isinstance(profile, unreal.Project_JFoleyAudioProfile):
    raise RuntimeError('Target path is occupied by another asset type.')
assignment = character.get_editor_property('foley_audio_profile')
if assignment is not None and (profile is None or assignment.get_path_name() != profile.get_path_name()):
    raise RuntimeError('Character has an authored Foley assignment; refusing to replace it.')
dirty = unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()
target_packages = {PROFILE_PATH, CHARACTER_PATH}
if any(p.get_path_name() in target_packages for p in dirty):
    raise RuntimeError('A target asset has unsaved user changes; refusing to edit it.')

created = False
saved = []
if APPLY:
    if profile is None:
        factory = unreal.DataAssetFactory()
        factory.set_editor_property('data_asset_class', unreal.Project_JFoleyAudioProfile)
        folder, name = PROFILE_PATH.rsplit('/', 1)
        profile = unreal.AssetToolsHelpers.get_asset_tools().create_asset(name, folder, unreal.Project_JFoleyAudioProfile, factory)
        if profile is None:
            raise RuntimeError('Could not create mannequin Foley profile.')
        events = {}
        for event, preset, group, contact, trace in ROWS:
            definition = unreal.Project_JFoleySoundSet()
            definition.set_editor_property('group', getattr(unreal.Project_JFoleyGroup, group))
            definition.set_editor_property('contact', getattr(unreal.Project_JFoleyContact, contact))
            definition.set_editor_property('default_sound', sounds[event])
            definition.set_editor_property('trace_surface', trace)
            definition.set_editor_property('importance', 1.5 if event == 'Land' else 1.0)
            events[tag(event)] = definition
        profile.set_editor_property('events', events)
        profile.set_editor_property('event_fallbacks', {tag(a): tag(b) for a, b in FALLBACKS.items()})
        # Handplant shares the source mesh's hand bones, without forcing offscreen pose updates.
        created = True
    if created:
        if not unreal.EditorAssetLibrary.save_loaded_asset(profile, only_if_is_dirty=False):
            raise RuntimeError('Could not save Foley profile.')
        saved.append(PROFILE_PATH)
    if assignment is None:
        character.set_editor_property('foley_audio_profile', profile)
        if not unreal.EditorAssetLibrary.save_loaded_asset(character, only_if_is_dirty=False):
            raise RuntimeError('Could not save character Foley assignment.')
        saved.append(CHARACTER_PATH)

report = {'mode': 'apply' if APPLY else 'inspect', 'profile': PROFILE_PATH,
          'character': CHARACTER_PATH, 'created': created, 'saved': saved,
          'assignment': str(character.get_editor_property('foley_audio_profile')),
          'available_sounds': {event: sound.get_path_name() for event, sound in sounds.items()},
          'events': {}, 'fallbacks': {}, 'note': 'Slide loop excluded; no terrain slots or map edits.'}
if profile is not None:
    for key, value in profile.get_editor_property('events').items():
        report['events'][key.export_text()] = {
            'sound': str(value.get_editor_property('default_sound')),
            'group': str(value.get_editor_property('group')),
            'contact': str(value.get_editor_property('contact')),
            'trace_surface': value.get_editor_property('trace_surface')}
    report['fallbacks'] = {a.export_text(): b.export_text() for a, b in profile.get_editor_property('event_fallbacks').items()}
REPORT_PATH.parent.mkdir(parents=True, exist_ok=True)
REPORT_PATH.write_text(json.dumps(report, indent=2, ensure_ascii=False), encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))
