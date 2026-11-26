"""Record near/far/near HLOD residency and render observations in an isolated PIE."""
import json
import os
from pathlib import Path
import time
import traceback

import unreal


output = Path(os.environ["PROJECT_J_ROUTE_HLOD_REPORT"]).resolve()
saved = Path(__file__).resolve().parents[2] / "Saved"
if not output.is_relative_to(saved / "Validation") or output.exists():
    raise ValueError("Use a fresh HLOD report under Saved/Validation")
output.parent.mkdir(parents=True, exist_ok=True)
config = json.loads(os.environ["PROJECT_J_ROUTE_HLOD_VIEWS"])
if len(config["views"]) != 3:
    raise ValueError("Declare near, far and return camera views")
targets = ("near", "far", "return")
report = {"passed": False, "map": config["map"], "observations": []}
levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(config["map"])
hlod_type = unreal.load_class(None, "/Script/Engine.WorldPartitionHLOD")
assert hlod_type
gameplay = unreal.get_default_object(unreal.GameplayStatics)
editor = unreal.EditorPythonScripting
editor.set_keep_python_script_alive(True)
deadline = time.monotonic() + 70
camera = None
stage = 0
stage_started = 0.0
captured_at = None


def place_view(index):
    global stage_started
    location, rotation = config["views"][index]
    camera.set_actor_location_and_rotation(unreal.Vector(*location), unreal.Rotator(**rotation), False, True)
    stage_started = time.monotonic()


def finish(error=None):
    if error:
        report["error"] = error
        unreal.log_error(error)
    with output.open("x", encoding="utf-8") as handle_out:
        json.dump(report, handle_out, indent=2)
    unreal.unregister_slate_post_tick_callback(tick_handle)
    levels.editor_request_end_play()
    editor.set_keep_python_script_alive(False)


def tick(_delta):
    global camera, stage, captured_at
    try:
        if time.monotonic() >= deadline:
            raise TimeoutError(f"Did not observe a stable {targets[stage]} view")
        worlds = unreal.EditorLevelLibrary.get_pie_worlds(False)
        if len(worlds) != 1:
            return
        world = worlds[0]
        controller = unreal.GameplayStatics.get_player_controller(world, 0)
        if not controller or unreal.GameplayStatics.get_time_seconds(world) < 2:
            return
        if camera is None:
            # This probe isolates engine source/proxy transitions. The mount run separately exercises both project providers.
            for name in ("world_streaming_preparation", "world_streaming_preload"):
                controller.get_editor_property(name).disable_streaming_source()
            transform = unreal.Transform()
            camera = gameplay.call_method("BeginDeferredActorSpawnFromClass", args=(
                world, unreal.CameraActor.static_class(), transform,
                unreal.SpawnActorCollisionHandlingMethod.ALWAYS_SPAWN))
            camera = gameplay.call_method("FinishSpawningActor", args=(camera, transform))
            controller.set_view_target_with_blend(camera)
            report["actual_world"] = world.get_path_name()
            place_view(stage)
        if captured_at is not None:
            if time.monotonic() - captured_at < 0.6:
                return
            stage += 1
            if stage == len(targets):
                report["passed"] = True
                finish()
                return
            captured_at = None
            place_view(stage)
        if time.monotonic() - stage_started < 1.0:
            return
        originals = unreal.GameplayStatics.get_all_actors_with_tag(world, config["source_tag"])
        proxies = unreal.GameplayStatics.get_all_actors_of_class(world, hlod_type)

        def recently_rendered(actors):
            return sum(any(c.was_recently_rendered(0.35) for c in a.get_components_by_class(unreal.StaticMeshComponent))
                       for a in actors)

        originals_rendered = recently_rendered(originals)
        proxies_rendered = recently_rendered(proxies)
        report["latest"] = {"view": targets[stage], "originals": len(originals), "proxies": len(proxies),
                            "originals_recently_rendered": originals_rendered, "proxies_recently_rendered": proxies_rendered}
        ready = (not originals and proxies_rendered > 0) if stage == 1 else originals_rendered > 0
        if ready:
            snapshot = output.with_name(f"{output.stem}-{targets[stage]}.png")
            unreal.SystemLibrary.execute_console_command(world, f'HighResShot 1 filename="{snapshot.as_posix()}"')
            report["observations"].append(dict(report["latest"], screenshot=snapshot.name))
            captured_at = time.monotonic()
    except Exception:
        finish(traceback.format_exc())


tick_handle = unreal.register_slate_post_tick_callback(tick)
levels.editor_request_begin_play()
