"""Measure a local, unsaved PIE flight using the existing MainWorld and Mover mount."""
import json
import math
from pathlib import Path
import re
import time
import traceback

import unreal


match = re.search(r"-ProjectJFlightRun=([A-Za-z0-9_-]+)", unreal.SystemLibrary.get_command_line())
assert match, "Supply a unique -ProjectJFlightRun"
output = Path(unreal.Paths.project_dir()) / "Saved/Validation/Continuation_20261014" / match.group(1)
output.mkdir(parents=True, exist_ok=True)
report_path = output / "flight.json"
assert not report_path.exists(), "Choose a fresh run name"
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level("/Game/World/MainWorld/Maps/L_MainWorld"), "Cannot load MainWorld"
performance = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings"))
previous_throttle = performance.get_editor_property("bThrottleCPUWhenNotForeground")
performance.set_editor_property("bThrottleCPUWhenNotForeground", False)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
mount_class = unreal.load_class(None, "/Game/DataAssetSets/MMO/BP_MountMover_Starter.BP_MountMover_Starter_C")
assert mount_class, "Cannot load the existing Mover mount"
gameplay = unreal.get_default_object(unreal.GameplayStatics)
subsystems = unreal.get_default_object(unreal.load_class(None, "/Script/Engine.SubsystemBlueprintLibrary"))
partition_class = unreal.load_class(None, "/Script/Engine.WorldPartitionSubsystem")
mount = None
partition = None
start = None
deadline = time.monotonic() + 150.0
last_sample = None
last_position = None
captures = set()
result = {"passed": False, "map": "L_MainWorld", "changes_saved": False, "flight_enabled_only_in_pie": True, "samples": []}


def finish(error=None):
    if error:
        result["error"] = error
    report_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(handle)
    editor.editor_request_end_play()
    performance.set_editor_property("bThrottleCPUWhenNotForeground", previous_throttle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    global mount, partition, start, last_sample, last_position
    try:
        if time.monotonic() > deadline:
            raise TimeoutError("PIE flight did not complete within the runner's time limit")
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return
        world = worlds[0]
        rider = unreal.GameplayStatics.get_player_pawn(world, 0)
        if not rider:
            return
        simulated = unreal.GameplayStatics.get_time_seconds(world)
        if mount is None:
            if simulated < 5.0:
                return
            transform = unreal.Transform(location=rider.get_actor_location(), rotation=unreal.Rotator(pitch=0, yaw=20, roll=0))
            mount = gameplay.call_method("BeginDeferredActorSpawnFromClass", args=(world, mount_class, transform,
                unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN))
            mount = gameplay.call_method("FinishSpawningActor", args=(mount, transform))
            assert mount and mount.try_mount_rider(rider), "Mounting the existing PIE pawn failed"
            mover = mount.get_mount_mover()
            profile = mover.get_editor_property("motion_profile")
            profile.set_editor_property("can_fly", True)
            mover.set_editor_property("motion_profile", profile)
            assert mover.try_enter_flight_mode(), "Mover rejected takeoff at the chosen spawn"
            partition = subsystems.call_method("GetWorldSubsystem", args=(world, partition_class))
            assert partition, "No World Partition subsystem in PIE"
            last_position = mount.get_actor_location().to_tuple()
            result["start_cm"] = list(last_position)
            start = last_sample = simulated
            return
        elapsed = simulated - start
        mount.set_mount_control_input(unreal.Vector2D(1.0, 0.15 if 13 < elapsed < 17 else 0.0),
            0.35 if elapsed < 8.0 else -0.08 if elapsed > 18.0 else 0.0, False, False)
        if simulated - last_sample >= 0.25:
            position = mount.get_actor_location().to_tuple()
            result["samples"].append({"time_s": round(elapsed, 3), "position_cm": list(position),
                "speed_cm_s": round(math.dist(position, last_position) / (simulated - last_sample), 1),
                "flying": mount.is_flying(), "partition_complete": partition.is_all_streaming_completed()})
            last_position, last_sample = position, simulated
        for checkpoint in (3, 14, 25):
            if checkpoint <= elapsed and checkpoint not in captures:
                captures.add(checkpoint)
                filename = (output / ("flight_%02d.png" % checkpoint)).as_posix()
                unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1 filename="' + filename + '"')
        if elapsed >= 27.0:
            samples = result["samples"]
            result["peak_speed_cm_s"] = max(sample["speed_cm_s"] for sample in samples)
            result["distance_cm"] = round(math.dist(result["start_cm"], last_position), 1)
            result["incomplete_samples"] = sum(not sample["partition_complete"] for sample in samples)
            result["passed"] = result["distance_cm"] > 25000 and all(sample["flying"] for sample in samples)
            finish()
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
