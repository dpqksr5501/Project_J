"""Read actual Project J assets using the editor tools needed for the 5.9 upgrade."""
import unreal


def tool_default(class_path):
    tool_class = unreal.load_class(None, class_path)
    assert tool_class, class_path
    return unreal.get_default_object(tool_class)


enabled = set(unreal.PluginBlueprintLibrary.get_enabled_plugin_names())
required = {
    "ModelContextProtocol", "GraphDsl", "BlueprintToolsets", "KnowledgeBaseToolset",
    "GeometryToolset", "ModularTestRunnerEditor", "WorldStreamingInsights",
    "NetworkPredictionInsights", "NavMeshToolset", "PerfToolset", "RewindDebuggerToolset",
}
assert required <= enabled, sorted(required - enabled)
if "ProjectJPreviewTools" in enabled:
    assert {"MassCharacters", "AnimGenOffline"} <= enabled
    tool_default("/Script/MassCharactersRuntime.MassCharacterInterpolationTrait")
    tool_default("/Script/AnimGenOffline.AnimGenOfflineBatchImportSettings")
    unreal.log("ProjectJ character preview classes loaded")

knowledge = tool_default("/Script/KnowledgeBaseToolset.KnowledgeBaseTools")
hits = knowledge.call_method("QueryKnowledgeBase", args=("ProjectJ", "", [], 3))
assert hits, "ProjectJ knowledge base contains no indexed documents"

graph = tool_default("/Script/BlueprintToolsets.BlueprintDslToolset")
game_mode = "/Game/ThirdPerson/Blueprints/BP_Project_JGameMode.BP_Project_JGameMode"
result = graph.call_method("Read", args=(game_mode, "", False))
assert result.dsl and result.revision, "GameMode graph has no DSL or revision"

ground = unreal.load_asset("/Game/Animation_Logic/Chooser/CHT_ProjectJ_Ground")
rows = unreal.ChooserTableEditorLibrary.get_num_rows(ground)
assert rows > 0, "Ground movement Chooser is empty"
unreal.log(f"ProjectJ editor tools passed: {len(hits)} documents, "
           f"{len(result.dsl)} DSL characters, {rows} ground Chooser rows")
