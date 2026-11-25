"""Start an unsaved PIE host so the editor can duplicate one world per UE client."""
import unreal

levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level("/Engine/Maps/Entry"), "Could not load the engine's empty fixture level"
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
game_mode = unreal.load_class(None, "/Script/Project_J.Project_JLoopbackCohort")
assert world and game_mode
world.get_world_settings().set_editor_property("default_game_mode", game_mode)
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
levels.editor_request_begin_play()


def stop_pie_on_result(_delta_seconds):
    if unreal.SystemLibrary.get_console_variable_int_value("ProjectJ.Cohort.Result"):
        unreal.unregister_slate_post_tick_callback(tick_handle)
        levels.editor_request_end_play()


tick_handle = unreal.register_slate_post_tick_callback(stop_pie_on_result)
