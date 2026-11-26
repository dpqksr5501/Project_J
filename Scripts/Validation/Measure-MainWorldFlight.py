"""Measure local, unsaved PIE travel with a Mover mount on a selected map."""
import json
import math
from pathlib import Path
import re
import time
import traceback

import unreal


command_line = unreal.SystemLibrary.get_command_line()


def option(name, default=None):
    match = re.search(rf"(?:^|\s)-{name}=(\S+)", command_line)
    return match.group(1) if match else default


match = re.search(r"-ProjectJFlightRun=([A-Za-z0-9_-]+)", command_line)
assert match, "Supply a unique -ProjectJFlightRun"
route = option("ProjectJFlightRoute", "Baseline")
assert route in ("Baseline", "ClimbTurnDive", "GroundBridge"), "Unknown -ProjectJFlightRoute"
ground = route == "GroundBridge"
map_package = option("ProjectJRouteMap", "/Game/World/MainWorld/Maps/L_MainWorld")
mount_path = option("ProjectJRouteMount", "/Game/DataAssetSets/MMO/BP_MountMover_Starter.BP_MountMover_Starter_C")
ground_phase = "outgoing"
if ground:
    distance_limits = option("ProjectJGroundBridgeCm")
    assert distance_limits, "Supply bridge start, end and turnaround X values via -ProjectJGroundBridgeCm"
    bridge_start, bridge_end, turnaround = map(float, distance_limits.split(","))
    assert all(map(math.isfinite, (bridge_start, bridge_end, turnaround))) and bridge_start < bridge_end < turnaround
    deck_tag_prefix = option("ProjectJGroundDeckTagPrefix", "RouteDeck")
    ground_speed = float(option("ProjectJGroundSpeed", "2600"))
    assert math.isfinite(ground_speed) and ground_speed > 600.0
speed_clamp = "-ProjectJFlightSpeedClamp" in command_line.split()
capture = "-ProjectJFlightNoCapture" not in command_line.split()
duration = 27.0 if route == "Baseline" else 32.0
checkpoints = (3, 14, 25) if route == "Baseline" else (5, 10, 12, 25)
output = Path(unreal.Paths.project_dir()) / "Saved/Validation/Continuation_20261014" / match.group(1)
output.mkdir(parents=True, exist_ok=True)
report_path = output / "flight.json"
assert not report_path.exists(), "Choose a fresh run name"
editor = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert editor.load_level(map_package), f"Cannot load {map_package}"
performance = unreal.get_default_object(unreal.load_class(None, "/Script/UnrealEd.EditorPerformanceSettings"))
previous_throttle = performance.get_editor_property("bThrottleCPUWhenNotForeground")
performance.set_editor_property("bThrottleCPUWhenNotForeground", False)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
mount_class = unreal.load_class(None, mount_path)
assert mount_class, "Cannot load the existing Mover mount"
gameplay = unreal.get_default_object(unreal.GameplayStatics)
subsystems = unreal.get_default_object(unreal.load_class(None, "/Script/Engine.SubsystemBlueprintLibrary"))
partition_class = unreal.load_class(None, "/Script/Engine.WorldPartitionSubsystem")
mount = None
partition = None
start = None
mounted_at = None
deadline = time.monotonic() + 150.0
last_sample = None
last_position = None
last_frame_wall_time = None
skip_frame_samples = 0
frame_ms_by_phase = {}
path_distance = 0.0
captures = set()
result = {"passed": False, "map": map_package.rsplit("/", 1)[-1], "map_package": map_package,
          "mount_class": mount_class.get_path_name(), "changes_saved": False, "flight_enabled_only_in_pie": not ground,
          "route": route, "local_speed_clamp": speed_clamp, "samples": []}
if ground:
    result["ground_profile"] = {"gallop_cm_s": ground_speed, "bridge_cm": [bridge_start, bridge_end],
                                "turnaround_cm": turnaround}
    result["missing_deck_frames"] = 0


def controls(elapsed):
    if route == "Baseline":
        return ("baseline", 0.15 if 13 < elapsed < 17 else 0.0,
                0.35 if elapsed < 8.0 else -0.08 if elapsed > 18.0 else 0.0)
    if elapsed < 8.0:
        return "climb", 0.0, 0.62
    if elapsed < 11.0:
        return "turn", 1.0, 0.0
    if elapsed < 13.0:
        return "dive", 0.0, -0.80
    if elapsed < 16.0:
        return "recover", 0.0, 0.30
    return "cruise", 0.0, 0.0


def ground_controls(world, location):
    global ground_phase
    yaw = mount.get_actor_rotation().yaw
    if ground_phase == "outgoing" and location.x >= turnaround:
        ground_phase = "turn"
    if ground_phase == "turn" and abs((yaw + 180.0) % 360.0 - 180.0) >= 178.0:
        ground_phase = "returning"
    hit = unreal.SystemLibrary.line_trace_single(
        world, location + unreal.Vector(0, 0, 40), location - unreal.Vector(0, 0, 300),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, False, [mount], unreal.DrawDebugTrace.NONE, True)
    values = hit.to_tuple() if hit else ()
    hit_actor = values[9] if values and values[0] else None
    if not hit_actor or not any(str(tag).startswith(deck_tag_prefix) for tag in hit_actor.tags):
        result["missing_deck_frames"] += 1
    if ground_phase == "outgoing" and bridge_start <= location.x <= bridge_end and "source_dump_x_cm" not in result:
        result["source_dump_x_cm"] = location.x
        unreal.SystemLibrary.execute_console_command(world, "wp.Runtime.DumpStreamingSources")
    if ground_phase == "turn":
        mount.set_mount_control_input(unreal.Vector2D(0.0, 1.0), 0.0, False, False)
    else:
        # Follow the road center after the turnaround instead of letting a one-degree
        # heading error accumulate across the entire narrow bridge.
        ahead = 3000.0 if ground_phase == "outgoing" else -3000.0
        desired = math.degrees(math.atan2(result["start_cm"][1] - location.y, ahead))
        error = (desired - yaw + 180.0) % 360.0 - 180.0
        mount.set_mount_control_input(unreal.Vector2D(1.0, max(-1.0, min(1.0, error / 25.0))),
                                      0.0, True, False)
    return ground_phase


def percentile(values, ratio):
    ordered = sorted(values)
    return round(ordered[math.ceil(len(ordered) * ratio) - 1], 3)


def summarize():
    samples = result["samples"]
    result["peak_speed_cm_s"] = max(sample["speed_cm_s"] for sample in samples)
    result["distance_cm"] = round(math.dist(result["start_cm"], last_position), 1)
    result["path_distance_cm"] = round(path_distance, 1)
    result["incomplete_samples"] = sum(not sample["partition_complete"] for sample in samples)
    result["wall_frame_ms_by_phase"] = {
        phase: {"samples": len(values), "p50": percentile(values, 0.50), "p95": percentile(values, 0.95),
                "p99": percentile(values, 0.99)} for phase, values in frame_ms_by_phase.items() if values
    }
    if ground:
        outbound = [s for s in samples if s["phase"] == "outgoing"]
        inbound = [s for s in samples if s["phase"] == "returning"]
        result["passed"] = bool(outbound and inbound and ground_phase == "returning"
                                and result["missing_deck_frames"] == 0
                                and max(s["speed_cm_s"] for s in outbound) > 600.0
                                and all(not s["flying"] for s in samples))
        return
    result["passed"] = path_distance > 25000 and all(sample["flying"] for sample in samples)
    if route == "ClimbTurnDive":
        climb = [sample["vertical_speed_cm_s"] for sample in samples if sample["phase"] == "climb"]
        dive = [sample["vertical_speed_cm_s"] for sample in samples if sample["phase"] == "dive"]
        headings = [sample["heading_deg"] for sample in samples if sample["phase"] == "turn"]
        turn = sum(abs((current - previous + 180.0) % 360.0 - 180.0) for previous, current in zip(headings, headings[1:]))
        result["motion_checks"] = {"climb_peak_cm_s": max(climb, default=0.0), "dive_peak_cm_s": min(dive, default=0.0),
                                   "turn_degrees": round(turn, 1)}
        result["passed"] &= max(climb, default=0.0) > 200.0 and min(dive, default=0.0) < -200.0 and turn > 45.0


def finish(error=None):
    if error:
        result["error"] = error
    report_path.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(handle)
    editor.editor_request_end_play()
    performance.set_editor_property("bThrottleCPUWhenNotForeground", previous_throttle)
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    global mount, partition, start, mounted_at, last_sample, last_position, last_frame_wall_time, skip_frame_samples, path_distance
    try:
        if time.monotonic() > deadline:
            raise TimeoutError("PIE flight did not complete within the runner's time limit")
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if not worlds:
            return
        world = worlds[0]
        player_pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
        if not player_pawn:
            return
        simulated = unreal.GameplayStatics.get_time_seconds(world)
        if mount is None:
            if simulated < 5.0:
                return
            rider = player_pawn
            controller = rider.get_controller()
            source = controller.get_editor_property("world_streaming_preparation")
            assert source, "The player controller has no activation source"
            result["actual_world"] = world.get_path_name()
            if speed_clamp:
                source.set_editor_property("limit_flight_speed_for_local_streaming", True)
            transform = unreal.Transform(location=rider.get_actor_location(), rotation=unreal.Rotator(yaw=0 if ground else 20))
            collision = (unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN if ground
                         else unreal.SpawnActorCollisionHandlingMethod.ADJUST_IF_POSSIBLE_BUT_DONT_SPAWN_IF_COLLIDING)
            mount = gameplay.call_method("BeginDeferredActorSpawnFromClass", args=(world, mount_class, transform,
                collision))
            assert mount, "No collision-safe space for the mount at the chosen spawn"
            mount = gameplay.call_method("FinishSpawningActor", args=(mount, transform))
            assert mount and mount.try_mount_rider(rider), "Mounting the existing PIE pawn failed"
            mover = mount.get_mount_mover()
            profile = mover.get_editor_property("motion_profile")
            profile.set_editor_property("can_fly", not ground)
            if ground:
                profile.set_editor_property("ground_gallop_speed", ground_speed)
                assert rider.get_actor_location().x < bridge_start, "Start before the bridge"
            mover.set_editor_property("motion_profile", profile)
            partition = subsystems.call_method("GetWorldSubsystem", args=(world, partition_class))
            assert partition, "No World Partition subsystem in PIE"
            mounted_at = simulated
        if start is None:
            if not ground and not mount.is_flying():
                # A new mount can start at the rider's lower capsule center. Give Mover
                # time to settle, then wait for the native queued flight mode to take effect.
                if simulated - mounted_at > 3.0:
                    raise RuntimeError(f"Mover did not take off at {mount.get_actor_location().to_tuple()}")
                if simulated - mounted_at > 0.25:
                    mount.get_mount_mover().try_enter_flight_mode()
                return
            last_position = mount.get_actor_location().to_tuple()
            result["start_cm"] = list(last_position)
            start = last_sample = simulated
            last_frame_wall_time = time.perf_counter()
            return
        elapsed = simulated - start
        location = mount.get_actor_location()
        if ground:
            phase = ground_controls(world, location)
        else:
            phase, turn, pitch = controls(elapsed)
            mount.set_mount_control_input(unreal.Vector2D(1.0, turn), pitch, False, False)
        frame_wall_time = time.perf_counter()
        upcoming_capture = capture and any(checkpoint <= elapsed and checkpoint not in captures for checkpoint in checkpoints)
        if skip_frame_samples:
            skip_frame_samples -= 1
        elif not upcoming_capture:
            frame_ms_by_phase.setdefault(phase, []).append(1000.0 * (frame_wall_time - last_frame_wall_time))
        last_frame_wall_time = frame_wall_time
        if simulated - last_sample >= 0.25:
            position = location.to_tuple()
            frame_distance = math.dist(position, last_position)
            path_distance += frame_distance
            result["samples"].append({"time_s": round(elapsed, 3), "position_cm": list(position),
                "phase": phase, "speed_cm_s": round(frame_distance / (simulated - last_sample), 1),
                "vertical_speed_cm_s": round((position[2] - last_position[2]) / (simulated - last_sample), 1),
                "heading_deg": round(math.degrees(math.atan2(position[1] - last_position[1], position[0] - last_position[0])), 1),
                "flying": mount.is_flying(), "partition_complete": partition.is_all_streaming_completed()})
            last_position, last_sample = position, simulated
        if upcoming_capture:
            for checkpoint in checkpoints:
                if checkpoint <= elapsed and checkpoint not in captures:
                    captures.add(checkpoint)
                    filename = (output / ("flight_%02d.png" % checkpoint)).as_posix()
                    unreal.SystemLibrary.execute_console_command(world, 'HighResShot 1 filename="' + filename + '"')
            skip_frame_samples = 2
        if ground and ground_phase == "returning" and location.x <= bridge_start:
            summarize()
            finish()
        elif elapsed >= duration:
            summarize()
            if ground:
                result["passed"] = False
            finish()
    except Exception:
        finish(traceback.format_exc())


handle = unreal.register_slate_post_tick_callback(tick)
editor.editor_request_begin_play()
