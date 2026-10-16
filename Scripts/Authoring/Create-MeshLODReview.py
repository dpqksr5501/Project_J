"""Create one editor-only LOD comparison map; never generate or save mesh LODs.

Run in a fresh editor with -ExecutePythonScript=<this file> and either
-ProjectJCreateLODReview or -ProjectJVerifyLODReview. Creation refuses overwrite.
"""
import hashlib
import json
from pathlib import Path
import unreal

MAP = '/Game/Developers/LODReview/L_MeshLODReview'
BODY = '/Game/Characters/GreatSword/GreatSword_Woman'
CLOTHES = '/Game/Characters/GreatSword/Clothes/'
PARTS = ('cloak', 'foot', 'hand', 'hel', 'inner', 'lower', 'sho', 'upper')
SWORD = '/Game/Anim_Assets/Great_Sword/Weapon/SM_Sword'
ROOT = Path(unreal.Paths.project_dir())
OUT = ROOT / 'Saved/Validation/MeshLODReview'


def asset_file(path):
    return ROOT / 'Content' / (path.removeprefix('/Game/') + '.uasset')


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest().upper()


def protected_files():
    paths = [BODY, SWORD] + [CLOTHES + name for name in PARTS]
    # Existing gameplay map and the shared generation policy must also stay intact.
    paths += ['/Game/ThirdPerson/Lvl_ThirdPerson',
              '/Game/DataAssetSets/Editor/LOD/DA_MeshLOD_Default']
    result = {}
    for path in paths:
        filename = asset_file(path)
        if path.endswith('Lvl_ThirdPerson'):
            filename = filename.with_suffix('.umap')
        result[str(filename.relative_to(ROOT))] = digest(filename)
    return result


def validate_level(actors):
    by_label = {actor.get_actor_label(): actor for actor in actors}
    records = []
    for row, dressed in [('BareFront', False), ('DressedFront', True), ('DressedBack', True)]:
        for lod in range(3):
            label = f'{row}_LOD{lod}_Body'
            actor = by_label[label]
            rotation = actor.get_actor_rotation()
            assert abs(rotation.roll) < .01 and abs(rotation.pitch) < .01, label
            assert abs(rotation.yaw - (90 if row == 'DressedBack' else -90)) < .01, label
            component = actor.skeletal_mesh_component
            assert component.get_skinned_asset().get_path_name().split('.')[0] == BODY
            assert component.get_forced_lod() == lod + 1, label
            assert not component.get_editor_property('sync_attach_parent_lod'), label
            assert not component.is_simulating_physics(), label
            assert component.get_editor_property('disable_post_process_blueprint'), label
            followers = []
            for part in PARTS if dressed else ():
                follower = by_label[f'{row}_LOD{lod}_{part}']
                mesh = follower.skeletal_mesh_component
                assert mesh.get_editor_property('leader_pose_component') == component
                assert mesh.get_skinned_asset().get_path_name().split('.')[0] == CLOTHES + part
                followers.append(part)
            records.append({'actor': label, 'forced_model': component.get_forced_lod(),
                            'requested_lod': lod, 'followers': followers})
    assert len(records) == 9
    assert 'Camera_Overview' in by_label and 'Camera_DressedFront' in by_label
    assert 'StudioFixedExposure' in by_label
    for row in ('BareFront', 'DressedFront', 'DressedBack'):
        assert row + '_Backdrop' in by_label
        camera_rotation = by_label['Camera_' + row].get_actor_rotation()
        assert abs(camera_rotation.roll) < .01 and abs(abs(camera_rotation.yaw) - 180) < .01
    return records


def create():
    if unreal.EditorAssetLibrary.does_asset_exist(MAP):
        raise RuntimeError('Review map exists; preserve user placements. Open it or verify it instead.')
    # Refuse to discard unrelated in-memory user work when run from a live session.
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages(), 'Unsaved content exists'
    assert not unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages(), 'Unsaved map exists'
    body = unreal.load_asset(BODY)
    parts = {name: unreal.load_asset(CLOTHES + name) for name in PARTS}
    assert body and all(parts.values())
    lod_tools = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    assert lod_tools.get_lod_count(body) == 3, 'Review layout expects the existing three body LODs'
    assert all(lod_tools.get_lod_count(mesh) == 1 for mesh in parts.values())
    cube = unreal.load_asset('/Engine/BasicShapes/Cube')
    sword = unreal.load_asset(SWORD)
    assert cube and sword
    assert unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    def spawn(cls, label, position, rotation=unreal.Rotator()):
        actor = actors.spawn_actor_from_class(cls, unreal.Vector(*position), rotation)
        assert actor, label
        actor.set_actor_label(label)
        actor.set_folder_path('LODReview')
        return actor

    def text(label, value, position, size=24, color=(230, 230, 230)):
        actor = spawn(unreal.TextRenderActor, label, position)
        component = actor.get_component_by_class(unreal.TextRenderComponent)
        component.set_text(value.replace('\n', '<br>'))
        component.set_text_material(unreal.load_asset('/Engine/EngineMaterials/UnlitText'))
        component.set_world_size(size)
        component.set_horizontal_alignment(unreal.HorizTextAligment.EHTA_CENTER)
        component.set_text_render_color(unreal.Color(r=color[0], g=color[1], b=color[2], a=255))
        return actor

    def block(label, position, scale):
        actor = spawn(unreal.StaticMeshActor, label, position)
        component = actor.static_mesh_component
        component.set_static_mesh(cube)
        component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        actor.set_actor_scale3d(unreal.Vector(*scale))
        return actor

    colors = [(100, 230, 255), (255, 220, 90), (255, 145, 100)]
    for row, x, dressed, yaw in [('BareFront', 0, False, -90),
                                  ('DressedFront', -650, True, -90),
                                  ('DressedBack', -1300, True, 90)]:
        for lod, y in enumerate((-300, 0, 300)):
            actor = spawn(unreal.SkeletalMeshActor, f'{row}_LOD{lod}_Body', (x, y, 0),
                          unreal.Rotator(pitch=0, yaw=yaw, roll=0))
            component = actor.skeletal_mesh_component
            component.set_skeletal_mesh_asset(body)
            # Per-component review overrides only; production assets remain untouched.
            component.set_editor_property('disable_post_process_blueprint', True)
            component.set_editor_property('sync_attach_parent_lod', False)
            component.set_forced_lod(lod + 1)
            component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            component.set_simulate_physics(False)
            for part, mesh in parts.items() if dressed else ():
                follower = spawn(unreal.SkeletalMeshActor, f'{row}_LOD{lod}_{part}', (x, y, 0),
                                 unreal.Rotator(pitch=0, yaw=yaw, roll=0))
                follower.attach_to_actor(actor, 'None', unreal.AttachmentRule.KEEP_WORLD,
                                         unreal.AttachmentRule.KEEP_WORLD, unreal.AttachmentRule.KEEP_WORLD, False)
                follower_mesh = follower.skeletal_mesh_component
                follower_mesh.set_skeletal_mesh_asset(mesh)
                follower_mesh.set_editor_property('disable_post_process_blueprint', True)
                follower_mesh.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
                follower_mesh.set_leader_pose_component(component, True, False)
                follower_mesh.set_simulate_physics(False)
            block(f'{row}_LOD{lod}_Pedestal', (x, y, -8), (1.8, 1.8, .16))
            text(f'{row}_LOD{lod}_Label', f'LOD {lod}\n' + ('REFERENCE / preserved' if lod == 0 else 'ADOPTED / body geometry'),
                 (x + 40, y, 260), 16, colors[lod])
        text(row + '_Title', {'BareFront': 'BODY ONLY / reference pose',
                            'DressedFront': 'FULL OUTFIT / original garment LODs',
                            'DressedBack': 'BACK / cloak and outfit unchanged'}[row],
             (x + 20, 0, 360), 24)
        backdrop = block(row + '_Backdrop', (x - 170, 0, 225), (.1, 11, 4.5))
        backdrop.static_mesh_component.set_cast_shadow(False)
        camera = spawn(unreal.CameraActor, 'Camera_' + row, (x + 450, 0, 140),
                       unreal.Rotator(pitch=-1.8, yaw=180, roll=0))
        camera.get_component_by_class(unreal.CameraComponent).set_field_of_view(90)

    block('StudioFloor', (-650, 0, -35), (30, 20, .30))
    text('Review_Header', 'PROJECT J / MESH LOD REVIEW', (20, 0, 475), 34)
    text('Review_Instructions', 'Same scale / Forced LOD0,1,2 / F to focus selected body\n'
         'Outliner: pilot Camera_BareFront, Camera_DressedFront, Camera_DressedBack\n'
         'Reference pose only / no cloth simulation or gameplay approval', (20, 0, 425), 16)
    sword_actor = spawn(unreal.StaticMeshActor, 'OriginalSword_Unchanged', (0, -700, 80))
    sword_actor.static_mesh_component.set_static_mesh(sword)
    sword_actor.static_mesh_component.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
    text('Sword_Label', 'SWORD / ORIGINAL\nNo LOD generated here', (40, -700, 260), 18)
    light = spawn(unreal.DirectionalLight, 'StudioKeyLight', (300, -400, 600),
                  unreal.Rotator(pitch=-35, yaw=160, roll=0))
    light_component = light.get_component_by_class(unreal.DirectionalLightComponent)
    light_component.set_editor_property('mobility', unreal.ComponentMobility.MOVABLE)
    light_component.set_intensity(8)
    fill = spawn(unreal.DirectionalLight, 'StudioFillLight', (-1000, 500, 500),
                 unreal.Rotator(pitch=-35, yaw=-25, roll=0))
    fill.get_component_by_class(unreal.DirectionalLightComponent).set_intensity(3)
    spawn(unreal.SkyLight, 'StudioSkyLight', (0, 0, 500))
    spawn(unreal.SkyAtmosphere, 'StudioSkyAtmosphere', (0, 0, -100))
    exposure = spawn(unreal.PostProcessVolume, 'StudioFixedExposure', (0, 0, 0))
    exposure.set_editor_property('unbound', True)
    settings = unreal.PostProcessSettings()
    settings.set_editor_property('override_auto_exposure_method', True)
    settings.set_editor_property('auto_exposure_method', unreal.AutoExposureMethod.AEM_MANUAL)
    settings.set_editor_property('override_auto_exposure_apply_physical_camera_exposure', True)
    settings.set_editor_property('auto_exposure_apply_physical_camera_exposure', False)
    settings.set_editor_property('override_auto_exposure_bias', True)
    settings.set_editor_property('auto_exposure_bias', 0)
    settings.set_editor_property('ambient_cubemap', unreal.load_asset(
        '/Engine/EditorMaterials/AssetViewer/EpicQuadPanorama_CC+EV1'))
    settings.set_editor_property('override_ambient_cubemap_intensity', True)
    settings.set_editor_property('ambient_cubemap_intensity', .35)
    exposure.set_editor_property('settings', settings)
    camera_position = unreal.Vector(2150, -150, 3200)
    camera_target = unreal.Vector(-450, 0, 150)
    camera_rotation = unreal.MathLibrary.find_look_at_rotation(camera_position, camera_target)
    overview = spawn(unreal.CameraActor, 'Camera_Overview',
                     (camera_position.x, camera_position.y, camera_position.z), camera_rotation)
    overview.get_component_by_class(unreal.CameraComponent).set_field_of_view(58)
    unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).set_level_viewport_camera_info(
        camera_position, camera_rotation)
    actors.set_selected_level_actors([])
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property('default_game_mode', unreal.GameModeBase.static_class())
    records = validate_level(actors.get_all_level_actors())
    assert unreal.EditorLoadingAndSavingUtils.save_map(world, MAP)
    return records


def main():
    command = unreal.SystemLibrary.get_command_line()
    create_mode = '-ProjectJCreateLODReview' in command
    verify_mode = '-ProjectJVerifyLODReview' in command
    assert create_mode != verify_mode, 'Choose exactly one explicit create/verify mode'
    before = protected_files()
    if create_mode:
        records = create()
    else:
        assert unreal.EditorLoadingAndSavingUtils.load_map(MAP)
        records = validate_level(unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors())
    after = protected_files()
    assert before == after, 'Production input changed unexpectedly'
    OUT.mkdir(parents=True, exist_ok=True)
    receipt = OUT / ('Created.json' if create_mode else 'Reloaded.json')
    assert not receipt.exists(), 'Preserve previous evidence'
    receipt.write_text(json.dumps({'map': MAP, 'scope': 'Reference-pose geometry comparison only; no mesh generation/save',
                                  'mode': 'create' if create_mode else 'reload_verify', 'bodies': records,
                                  'protected_files': after, 'production_unchanged': True}, indent=2) + '\n', encoding='utf-8')
    unreal.log('PROJECT_J_LOD_REVIEW_READY ' + MAP)


if __name__ == '__main__':
    main()
