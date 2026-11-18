"""Read effective Blueprint component templates and selected combat FX assets."""
import json
import re
from pathlib import Path

import unreal


def asset_paths(params, name):
    paths = params.get(name, "").split(",") if params.get(name) else []
    if len(paths) > 16 or len(set(paths)) != len(paths) or any(
        not re.fullmatch(r"/Game/[A-Za-z0-9_/]+(?:\.[A-Za-z0-9_]+)?", path) for path in paths
    ):
        raise ValueError(f"{name} requires up to 16 distinct, explicit /Game asset paths")
    return paths


def object_path(obj):
    return obj.get_path_name() if obj else None


def tag_name(tag):
    return str(unreal.GameplayTagLibrary.get_tag_name(tag))


def inspect_blueprint(path):
    blueprint = unreal.load_asset(path)
    if not isinstance(blueprint, unreal.Blueprint):
        raise ValueError(f"Expected a Blueprint at {path}")
    subsystem = unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    lod_tools = unreal.get_editor_subsystem(unreal.SkeletalMeshEditorSubsystem)
    library = unreal.SubobjectDataBlueprintFunctionLibrary
    meshes = []
    visual_budgets = []
    base_sets = {}
    for handle in subsystem.k2_gather_subobject_data_for_blueprint(blueprint):
        data = library.get_data(handle)
        component = library.get_object_for_blueprint(data, blueprint)
        if isinstance(component, unreal.Project_JCombatPresentationComponent):
            source = component.get_editor_property("base_presentation_set")
            if source:
                base_sets[object_path(source)] = source
        if isinstance(component, unreal.Project_JAnimationUpdateCoordinatorComponent):
            visual_budgets.append(bool(component.get_editor_property("allow_visual_budget")))
        if not isinstance(component, unreal.SkeletalMeshComponent):
            continue
        mesh = component.get_skeletal_mesh_asset()
        clothing = [object_path(asset) for asset in mesh.get_editor_property("mesh_clothing_assets")] if mesh else []
        no_visual_budget = any(
            str(tag).casefold() == "novisualbudget" for tag in component.get_editor_property("component_tags")
        )
        modular = isinstance(component, unreal.Project_JModularMeshComponent)
        quality_allowed = bool(component.get_editor_property("allow_cloth_quality_budget")) if modular else None
        if not clothing:
            route = "no_clothing_asset_on_template"
        elif not modular:
            route = "mesh_owned_cloth_outside_modular_budget"
        elif not quality_allowed or no_visual_budget:
            route = "modular_budget_opted_out"
        else:
            route = "modular_candidate_requires_runtime_leader"
        meshes.append({
            "component": component.get_name(), "class": component.get_class().get_name(),
            "inherited": library.is_inherited_component(data), "mesh": object_path(mesh),
            "lod_count": lod_tools.get_lod_count(mesh) if mesh else None,
            "forced_lod_model": component.get_editor_property("forced_lod_model"),
            "clothing_assets": clothing, "cloth_route": route, "no_visual_budget": no_visual_budget,
            "allow_cloth_quality_budget": quality_allowed,
            "allow_cloth_execution_budget": component.is_cloth_execution_budget_allowed() if modular else None,
            "follow_attach_parent_pose": bool(component.get_editor_property("follow_attach_parent_pose")) if modular else None,
        })
    if not meshes:
        raise ValueError(f"No skeletal component templates found in {path}")
    return {"blueprint": object_path(blueprint), "meshes": meshes,
            "visual_budget_permissions": visual_budgets, "base_presentation_sets": sorted(base_sets)}, base_sets


def inspect_fx(asset):
    if isinstance(asset, unreal.Project_JCombatPresentationSet):
        return {"asset": object_path(asset), "attacks": [
            {"attack": tag_name(entry.get_editor_property("attack_tag")),
             "profile": inspect_fx(profile) if (profile := entry.get_editor_property("profile")) else None}
            for entry in asset.get_editor_property("attack_presentations")
        ]}
    if not isinstance(asset, unreal.Project_JAttackPresentationProfile):
        raise ValueError(f"Expected an attack profile or combat presentation set at {object_path(asset)}")
    cues = []
    for cue in asset.get_editor_property("cues"):
        system = cue.get_editor_property("niagara_system")
        effect_type = system.get_editor_property("effect_type") if system else None
        cues.append({"cue": tag_name(cue.get_editor_property("cue_tag")), "niagara_system": object_path(system),
                     "effect_type": object_path(effect_type), "looping": bool(cue.get_editor_property("looping")),
                     "remote_spawn_budget": bool(cue.get_editor_property("use_remote_spawn_budget"))})
    return {"asset": object_path(asset), "cues": cues}


def main():
    _, _, params = unreal.SystemLibrary.parse_command_line(unreal.SystemLibrary.get_command_line())
    run = params.get("ProjectJBudgetRun", "")
    blueprints = asset_paths(params, "ProjectJBudgetBlueprints")
    presentations = asset_paths(params, "ProjectJBudgetPresentations")
    if not re.fullmatch(r"[A-Za-z0-9_-]{1,64}", run) or not blueprints:
        raise ValueError("Supply ProjectJBudgetRun and at least one ProjectJBudgetBlueprints path")
    receipt = Path(unreal.Paths.project_dir()) / "Saved/PresentationBudgetInspection" / run / "report.json"
    if receipt.exists():
        raise FileExistsError(receipt)

    characters, base_sets = [], {}
    for path in blueprints:
        character, sources = inspect_blueprint(path)
        characters.append(character)
        base_sets.update(sources)
    for path in presentations:
        asset = unreal.load_asset(path)
        if asset is None:
            raise ValueError(f"Selected presentation asset did not load: {path}")
        base_sets[object_path(asset)] = asset
    report = {"schema": 1, "characters": characters, "selected_presentations": presentations,
              "presentations": [inspect_fx(base_sets[key]) for key in sorted(base_sets)],
              "scope": "Stored component templates and selected presentation assets. Runtime construction, gear, leader pose, cloth interactor, active effects and gameplay-warning timing require runtime evidence."}
    receipt.parent.mkdir(parents=True, exist_ok=True)
    with receipt.open("x", encoding="utf-8") as output:
        json.dump(report, output, ensure_ascii=False, indent=2)
        output.write("\n")
    unreal.log(f"Project J presentation budget inspection: {receipt}")


main()
