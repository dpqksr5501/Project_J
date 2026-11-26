"""Capture a rendered PIE retreat and return. A pose may set min_far_proxies=0 for an intentional near-only view."""
import json
from datetime import datetime, timezone
import math
import os
from pathlib import Path
import time
import traceback

import unreal

root = Path(__file__).resolve().parents[2]
output = Path(os.environ["PROJECT_J_BIOME_REPORT"]).resolve()
assert output.is_relative_to(root / "Saved/Validation") and not output.exists()
output.parent.mkdir(parents=True, exist_ok=True)
source = Path(os.environ["PROJECT_J_BIOME_CAMERAS"])
poses = json.loads(source.read_text(encoding="utf-8"))["cameras"]
names = os.environ["PROJECT_J_BIOME_VIEWS"].split(",")
region = os.environ["PROJECT_J_STREAMING_MAP"]
distance = float(os.environ.get("PROJECT_J_BIOME_RETREAT_CM", "80000"))
assert names and all(n in poses and n.isidentifier() for n in names) and math.isfinite(distance) and distance > 0
far_minima = {n: poses[n].get("min_far_proxies", 1) for n in names}
if any(type(minimum) is not int or minimum < 0 for minimum in far_minima.values()):
    raise ValueError("Camera min_far_proxies must be a nonnegative integer")
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(region)
report = dict(map=region, retreat_cm=distance, min_far_proxies=far_minima, observations=[], motion=[], hlod_checks=0, complete=False)
labels = ("near", "outbound", "far", "inbound", "return")
camera = None
stage = index = 0
started = time.perf_counter()
deadline = started + 210
pending = None
previous_motion_tick = None
motion_pixels = os.environ.get("PROJECT_J_BIOME_MOTION_PIXELS") == "1"
moving_capture = None
gameplay = unreal.get_default_object(unreal.GameplayStatics)
hlod = unreal.load_class(None, "/Script/Engine.WorldPartitionHLOD")
probe_path = os.environ.get("PROJECT_J_BIOME_PROBE_MESH")
probe_mesh = unreal.load_asset(probe_path) if probe_path else None
if probe_path and not probe_mesh:
    raise ValueError(f"Missing probe mesh {probe_path}")


def finish(error=None):
    if error:
        report["error"] = error
    output.write_text(json.dumps(report, indent=2), encoding="utf-8")
    unreal.unregister_slate_post_tick_callback(handle)
    levels.editor_request_end_play()
    unreal.EditorPythonScripting.set_keep_python_script_alive(False)


def tick(_delta):
    global camera, index, stage, started, pending, previous_motion_tick, moving_capture
    try:
        now = time.perf_counter()
        if now > deadline:
            raise TimeoutError("PIE capture deadline expired")
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if len(worlds) != 1:
            return
        world = worlds[0]
        pc = unreal.GameplayStatics.get_player_controller(world, 0)
        if not pc or not unreal.GameplayStatics.get_player_pawn(world, 0):
            return
        if not camera:
            transform = unreal.Transform()
            spawned = gameplay.call_method("BeginDeferredActorSpawnFromClass", args=(world, unreal.CameraActor.static_class(), transform,
                unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN))
            camera = gameplay.call_method("FinishSpawningActor", args=(spawned, transform))
            pc.set_view_target_with_blend(camera)
            started = now
            previous_motion_tick = None
            moving_capture = None
        # The engine maintains this count as HLOD objects register/unregister. Sample it even
        # while a screenshot is pending; enumerating actors here would distort movement timing.
        outdated = unreal.Project_JWorldAuthoringLibrary.get_outdated_runtime_hlod_count(world)
        report["hlod_checks"] += 1
        if outdated != 0:
            position = camera.get_actor_location()
            report["hlod_failure"] = dict(view=names[index], phase=labels[stage], count=outdated,
                location=[position.x, position.y, position.z])
            raise RuntimeError("HLOD runtime subsystem unavailable" if outdated < 0 else
                               "Outdated runtime HLOD during capture; inspect LogHLODRuntimeSubsystem")
        if pending:
            if not pending.is_file() or pending.stat().st_size == 0:
                return
            pending = None
            stage += 1
            if stage == 5:
                stage = 0
                index += 1
            if index == len(names):
                for sample in report["observations"]:
                    if sample["phase"] == "far" and sample["recently_rendered_world_proxies"] < far_minima[sample["view"]]:
                        raise RuntimeError(f"Missing expected distant proxies for {sample['view']}")
                if probe_mesh:
                    def views_rendering(kind):
                        return {s["view"] for s in report["observations"] if s["recently_rendered_mesh"][kind]}
                    if not views_rendering("source").intersection(views_rendering("proxy")):
                        raise RuntimeError(f"No single camera route rendered both source and proxy for {probe_path}")
                report["complete"] = True
                finish()
                return
            started = now
            previous_motion_tick = None
            moving_capture = None
        pose = poses[names[index]]
        origin = unreal.Vector(**pose["location"])
        rotation = unreal.Rotator(**pose["rotation"])
        # Preserve the sightline throughout the retreat and return.
        direction = unreal.MathLibrary.get_forward_vector(rotation)
        elapsed = now - started
        end = (0.0, 0.5, 1.0, 0.5, 0.0)[stage]
        begin = (0.0, 0.0, 0.5, 1.0, 0.5)[stage]
        fraction = begin + (end - begin) * min(elapsed / 2.5, 1.0)
        location = origin - direction * distance * fraction
        camera.set_actor_location_and_rotation(location, rotation, False, True)
        if stage and elapsed <= 2.5:
            if previous_motion_tick is not None:
                # Timestamp at the end of the interval for correlation with the engine's UTC log.
                report["motion"].append(dict(view=names[index], phase=labels[stage], fraction=round(fraction, 5),
                    utc=datetime.now(timezone.utc).isoformat(timespec="milliseconds"), interval_ms=round((now-previous_motion_tick)*1000, 3)))
            previous_motion_tick = now
            if motion_pixels and moving_capture is None and elapsed >= 1.25:
                moving_capture = output.with_name(f"{names[index]}-{labels[stage]}-moving.png")
                if moving_capture.exists():
                    raise FileExistsError(moving_capture)
                unreal.SystemLibrary.execute_console_command(world, f'HighResShot 1 filename="{moving_capture.as_posix()}"')
        if elapsed < 6:
            return
        if motion_pixels and stage and (not moving_capture or not moving_capture.is_file() or not moving_capture.stat().st_size):
            raise RuntimeError(f"No in-motion image for {names[index]}/{labels[stage]}")
        proxies = unreal.GameplayStatics.get_all_actors_of_class(world, hlod)
        rendered = sum(any(c.was_recently_rendered(0.35) for c in p.get_components_by_class(unreal.StaticMeshComponent)) for p in proxies)
        pending = output.with_name(f"{names[index]}-{labels[stage]}.png")
        if pending.exists():
            raise FileExistsError(pending)
        report["observations"].append(dict(view=names[index], phase=labels[stage], location=[location.x, location.y, location.z],
            loaded_world_proxies=len(proxies), recently_rendered_world_proxies=rendered, screenshot=pending.name,
            outdated_hlods=outdated,
            moving_screenshot=moving_capture.name if moving_capture else None))
        if probe_mesh:
            counts = {"source": 0, "proxy": 0}
            for actor in unreal.GameplayStatics.get_all_actors_of_class(world, unreal.Actor):
                group = "proxy" if isinstance(actor, unreal.WorldPartitionHLOD) else "source"
                counts[group] += sum(c.static_mesh == probe_mesh and c.was_recently_rendered(0.35)
                                     for c in actor.get_components_by_class(unreal.StaticMeshComponent))
            report["observations"][-1]["recently_rendered_mesh"] = counts
        unreal.SystemLibrary.execute_console_command(world, f'HighResShot 1 filename="{pending.as_posix()}"')
    except Exception:
        finish(traceback.format_exc())


unreal.EditorPythonScripting.set_keep_python_script_alive(True)
handle = unreal.register_slate_post_tick_callback(tick)
levels.editor_request_begin_play()
