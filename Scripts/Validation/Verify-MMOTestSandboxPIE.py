"""Exercise the saved sandbox mount and console-opened harness without saving PIE changes."""
import json
import math
import os
import time
import traceback

import unreal


levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Game/Maps/Test_MMO_Sandbox")
mount_type = unreal.load_class(None, "/Game/DataAssetSets/MMO/BP_MountMover_Starter.BP_MountMover_Starter_C")
harness_type = unreal.load_class(None, "/Game/DataAssetSets/MMO/WBP_MMOTestHarness.WBP_MMOTestHarness_C")
widget_library = unreal.get_default_object(unreal.load_class(None, "/Script/UMG.WidgetBlueprintLibrary"))
assert mount_type and harness_type and widget_library
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
report = {"passed": False}
state = {}
deadline = time.monotonic() + 65


def get_harnesses(world):
    return widget_library.call_method("GetAllWidgetsOfClass", args=(world, harness_type, True))


def finish():
    with open(os.environ["PROJECT_J_MMO_PIE_REPORT"], "w", encoding="utf-8") as output:
        json.dump(report, output, indent=2)
    unreal.unregister_slate_post_tick_callback(handle)
    levels.editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    try:
        assert time.monotonic() <= deadline, "Sandbox PIE did not complete"
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return
        world = worlds[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        if not pc or not pawn:
            return
        elapsed = unreal.GameplayStatics.get_time_seconds(world)
        if "closed_at" in state:
            if elapsed - state["closed_at"] < 0.1:
                return
            report["harness_closed"] = not get_harnesses(world)
            report["cursor_restored"] = pc.get_editor_property("show_mouse_cursor") == state["before_cursor"]
            report["passed"] = all(report[key] for key in ("flying", "harness_closed", "cursor_restored")) and report["distance_cm"] > 100
            finish()
            return
        if "opened" not in state:
            if elapsed < 1.0:
                return
            assert isinstance(pc, unreal.Project_JPlayerController), "The sandbox is not using the project controller"
            state["before_cursor"] = pc.get_editor_property("show_mouse_cursor")
            unreal.SystemLibrary.execute_console_command(world, "PJ_ToggleTestUI", pc)
            state["opened"] = elapsed
            return
        if "mount" not in state:
            if "tested_on_foot" not in state:
                widgets = get_harnesses(world)
                assert len(widgets) == 1, "The configured harness WBP did not open"
                assert pc.get_editor_property("show_mouse_cursor"), "The harness did not enable the cursor"
                widgets[0].call_method("HandleCycleVFXOpacityClicked", args=())
                opacity = unreal.SystemLibrary.get_console_variable_float_value("ProjectJ.Combat.Presentation.OtherPlayerOpacityScale")
                assert abs(opacity - 0.35) < 0.02
                report["vfx_cvar_changed"] = True
                unreal.SystemLibrary.execute_console_command(world, "PJ_ToggleTestUI", pc)
                state["tested_on_foot"] = elapsed
                return
            if elapsed - state["tested_on_foot"] < 0.1:
                return
            report["on_foot_cursor_restored"] = pc.get_editor_property("show_mouse_cursor") == state["before_cursor"]
            mounts = unreal.GameplayStatics.get_all_actors_of_class(world, mount_type)
            assert len(mounts) == 1, "Expected one placed sandbox mount"
            mount = mounts[0]
            mover = mount.get_mount_mover()
            assert mover.get_editor_property("motion_profile").get_editor_property("can_fly")
            if isinstance(pawn, unreal.Project_JPlayerCharacter) and pawn.is_combat_mode_active():
                if "sheathed" not in state:
                    pawn.toggle_combat_mode()
                    state["sheathed"] = True
                return
            pawn.set_actor_location(mount.get_actor_location() + unreal.Vector(175, 0, 20), False, True)
            report["mount_admission"] = {
                "distance_cm": round(math.dist(pawn.get_actor_location().to_tuple(), mount.get_actor_location().to_tuple()), 1),
                "interaction_distance_cm": mount.get_editor_property("mount_interaction_distance"),
                "pawn_class": pawn.get_class().get_path_name(),
                "mount_health": mount.get_editor_property("health"),
                "interaction_allowed": mount.call_method("CanInteract", args=(pawn,)),
                "previous_mount_controller": mount.get_controller().get_class().get_path_name() if mount.get_controller() else None,
            }
            mounted = mount.try_mount_rider(pawn)
            owner_pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
            report["mount_admission"]["owned_after_attempt"] = owner_pawn.get_class().get_path_name() if owner_pawn else None
            assert mounted, "The placed mount rejected the sandbox rider"
            unreal.SystemLibrary.execute_console_command(world, "PJ_ToggleTestUI", pc)
            state["mount"] = mount
            return
        mount = state["mount"]
        if "start" not in state:
            widgets = get_harnesses(world)
            assert len(widgets) == 1 and pawn == mount
            widgets[0].call_method("HandleToggleFlightClicked", args=())
            state.update(start=elapsed, position=mount.get_actor_location().to_tuple())
            report["mounted_and_harness_open"] = True
            return
        if elapsed - state["start"] < 3:
            mount.set_mount_control_input(unreal.Vector2D(1, 0), 0.45, False, False)
            return
        report["flying"] = mount.is_flying()
        report["distance_cm"] = round(math.dist(state["position"], mount.get_actor_location().to_tuple()), 1)
        unreal.SystemLibrary.execute_console_command(world, "PJ_ToggleTestUI", pc)
        state["closed_at"] = elapsed
    except Exception:
        report["error"] = traceback.format_exc()
        finish()


handle = unreal.register_slate_post_tick_callback(tick)
levels.editor_request_begin_play()
