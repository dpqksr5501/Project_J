"""Exercise the production Spellblade key mapping and quick bar in a rendered PIE instance."""
import json
import os
from pathlib import Path
import time
import traceback
import unreal as u

report = Path(os.environ["PROJECT_J_PULSE_INPUT_REPORT"]).resolve()
assert report.is_relative_to(Path(__file__).resolve().parents[2] / "Saved/Validation") and not report.exists()
report.parent.mkdir(parents=True, exist_ok=True)
levels = u.get_editor_subsystem(u.LevelEditorSubsystem)
assert levels.load_level("/Game/World/MainWorld/Maps/L_MainWorld")
folder = "/Game/DataAssetSets/AttackDefinition/Spellblade/GroundPulse"
cost = u.get_default_object(u.load_asset(folder + "/GE_GroundPulse_Cost").generated_class())
mana = cost.get_editor_property("modifiers")[0].get_editor_property("attribute")
out = {"passed": False}
started = time.monotonic()
next_step = started
stage = 0


def tick(_dt):
    global stage, next_step
    try:
        now = time.monotonic()
        if now - started > 85:
            raise TimeoutError(f"Pulse input stage {stage}")
        if now < next_step:
            return
        worlds = u.EditorLevelLibrary.get_pie_worlds(False)
        if len(worlds) != 1:
            return
        world = worlds[0]
        pawn = u.GameplayStatics.get_player_pawn(world, 0)
        if not pawn:
            return
        def key(command):
            u.SystemLibrary.execute_console_command(world, command)
        def current_mana():
            value, found = u.AbilitySystemLibrary.get_float_attribute(pawn, mana)
            assert found
            return value
        if stage == 0:
            pc = u.GameplayStatics.get_player_controller(world, 0)
            ui = pc.get_component_by_class(u.Project_JPlayerUIComponent)
            if not ui or not ui.get_active_profile() or u.GameplayStatics.get_time_seconds(world) < 3:
                return
            out["profile"] = ui.get_active_profile().get_name()
            out["initial_mana"] = current_mana()
            assert out["initial_mana"] >= 20
            key("Input.+key CapsLock")
        elif stage == 1:
            key("Input.+key T")
        elif stage == 2:
            key("Input.-key T")
            key("Input.-key CapsLock")
            out["key_mana"] = current_mana()
            assert out["key_mana"] == out["initial_mana"] - 10, out
        elif stage == 3:
            pc = u.GameplayStatics.get_player_controller(world, 0)
            ui = pc.get_component_by_class(u.Project_JPlayerUIComponent)
            ui.press_skill(5)
            ui.release_skill(5)
        elif stage == 4:
            out["quick_slot_mana"] = current_mana()
            assert out["quick_slot_mana"] == out["key_mana"] - 10, out
            filename = report.with_suffix(".png")
            u.SystemLibrary.execute_console_command(world, f'shot showUI -nosuffix filename="{filename.as_posix()}"')
        else:
            if not report.with_suffix(".png").is_file():
                return
            out["passed"] = True
        stage += 1
        next_step = now + (5.5 if stage == 3 else 1.0)
        if not out["passed"]:
            return
    except Exception:
        out["error"] = traceback.format_exc()
    report.write_text(json.dumps(out, indent=2), encoding="utf-8")
    u.unregister_slate_post_tick_callback(handle)
    levels.editor_request_end_play()
    u.EditorPythonScripting.set_keep_python_script_alive(False)


u.EditorPythonScripting.set_keep_python_script_alive(True)
handle = u.register_slate_post_tick_callback(tick)
levels.editor_request_begin_play()
