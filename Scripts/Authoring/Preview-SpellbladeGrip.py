"""Run from Unreal's Python console. Run again to remove the transient preview."""
import unreal as u


def start_preview():
    get = lambda obj, name: obj.get_editor_property(name)
    actors = u.get_editor_subsystem(u.EditorActorSubsystem)
    editor = u.get_editor_subsystem(u.UnrealEditorSubsystem)
    if editor.get_game_world():
        raise RuntimeError('Stop PIE before opening the grip preview.')

    body_asset = u.load_asset('/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete')
    draw_pose = u.load_asset('/Game/Anim_Assets/Spellblade/Animations/Sword/CombatKit/Sequences/DirectBody/Equipment/AS_SpellbladeDirect_Draw')
    weapon_asset = u.load_asset('/Game/Weapons/Spellblade/SK_Spellblade_longblade')
    trim_asset = u.load_asset('/Game/Weapons/Spellblade/SK_Spellblade_aiguillette')
    equip = u.load_asset('/Game/DataAssetSets/Animation_Profiles/Equip/DA_Spellblade_Equip')
    visual = get(equip, 'weapon_presentation_profile')
    grip_name = get(get(visual, 'motion_presentation'), 'primary_grip_socket_name')
    camera, look = editor.get_level_viewport_camera_info()
    position = camera + u.MathLibrary.get_forward_vector(look) * 300.0 - u.Vector(0, 0, 95)
    spawned = []
    callback = None

    def spawn(mesh, label):
        actor = actors.spawn_actor_from_class(u.SkeletalMeshActor, position, transient=True)
        if not actor:
            raise RuntimeError(f'Could not spawn {label}.')
        spawned.append(actor)
        actor.set_actor_label(label)
        component = get(actor, 'skeletal_mesh_component')
        component.set_skeletal_mesh_asset(mesh)
        component.set_collision_enabled(u.CollisionEnabled.NO_COLLISION)
        return actor, component

    def stop():
        if callback is not None:
            u.unregister_slate_post_tick_callback(callback)
        for actor in reversed(spawned):
            if u.SystemLibrary.is_valid(actor):
                actors.destroy_actor(actor)
        if hasattr(u, '_project_j_grip_preview_stop'):
            del u._project_j_grip_preview_stop

    try:
        body_actor, body = spawn(body_asset, 'Grip preview - Spellblade')
        sword_actor, _ = spawn(weapon_asset, 'Grip preview - Longblade')
        trim_actor, _ = spawn(trim_asset, 'Grip preview - Aiguillette')
        body.set_update_animation_in_editor(True)
        body.override_animation_data(draw_pose, False, False, draw_pose.get_play_length(), 1.0)
        def refresh():
            mount = u.Project_JAssetMigrationLibrary.resolve_skeletal_weapon_grip_preview(body, weapon_asset, grip_name)
            if mount is None:
                raise RuntimeError(f'Preview could not resolve the body palm and fixed weapon grip {grip_name}.')
            for actor in (sword_actor, trim_actor):
                actor.set_actor_transform(mount, False, True)

        def tick(_delta):
            try:
                if editor.get_game_world() or not all(u.SystemLibrary.is_valid(actor) for actor in spawned):
                    stop()
                    return
                refresh()
            except Exception as error:
                stop()
                u.log_error(f'Grip preview closed: {error}')

        refresh()
        callback = u.register_slate_post_tick_callback(tick)
        u._project_j_grip_preview_stop = stop
        actors.set_selected_level_actors([body_actor])
        u.log(f'Grip preview ready. Edit {grip_name} in {weapon_asset.get_name()}. Run the script again to close.')
    except Exception:
        stop()
        raise


if hasattr(u, '_project_j_grip_preview_stop'):
    u._project_j_grip_preview_stop()
else:
    start_preview()
