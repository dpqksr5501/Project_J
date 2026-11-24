"""Inspect or bake the melee windows referenced by the equipped Spellblade kit.

Run with UnrealEditor-Cmd -run=pythonscript outside PIE. Set
PROJECT_J_SPELLBLADE_BAKE_OUTPUT to a local validation directory. The default
mode is preflight. Apply requires an Approved.json map of asset path to exact
source stamps from the preflight, including each additional window in order.
"""

import hashlib
import json
import os
from pathlib import Path
import shutil

import unreal


EQUIP = "/Game/DataAssetSets/Animation_Profiles/Equip/DA_Spellblade_Equip"
MESH = "/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"
ATTACK_ROOT = "/Game/DataAssetSets/AttackDefinition/Spellblade/CombatKit/"


def read(obj, prop):
    return obj.get_editor_property(prop)


def dump(path, value):
    path.write_text(json.dumps(value, indent=2), encoding="utf-8")


def stamps(result):
    return [result["source_stamp"], *[item["source_stamp"] for item in result["additional_windows"]]]


def saved_stamps(attack):
    return [
        read(read(attack, "melee_trajectory"), "source_stamp"),
        *[read(path, "source_stamp") for path in read(attack, "additional_melee_trajectories")],
    ]


def content_file(project, attack):
    object_path = attack.get_path_name()
    if not object_path.startswith(ATTACK_ROOT):
        raise RuntimeError(f"Attack is outside the production kit: {object_path}")
    package_path = object_path.split(".", 1)[0].removeprefix("/Game/")
    return project / "Content" / (package_path + ".uasset")


def sha256(path):
    with path.open("rb") as file:
        return hashlib.file_digest(file, "sha256").hexdigest()


def main():
    project = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())).resolve()
    output = Path(os.environ["PROJECT_J_SPELLBLADE_BAKE_OUTPUT"]).resolve()
    if not output.is_relative_to(project / "Saved"):
        raise RuntimeError("Use a dedicated output directory under the project Saved folder.")
    output.mkdir(parents=True, exist_ok=True)
    mode = os.environ.get("PROJECT_J_SPELLBLADE_BAKE_MODE", "preflight")
    if mode not in {"preflight", "apply", "verify"}:
        raise RuntimeError(f"Unknown bake mode: {mode}")
    equipment = unreal.load_asset(EQUIP)
    mesh = unreal.load_asset(MESH)
    style = read(equipment, "combat_style_definition")
    combo = read(style, "combo_definition")
    attacks = {read(node, "attack_definition") for node in read(combo, "nodes")}
    attacks.update(read(style, "additional_attacks"))
    attacks.discard(None)
    by_path = {attack.get_path_name(): attack for attack in attacks}
    if not by_path:
        raise RuntimeError("Expected the equipped Spellblade combo and additional attacks.")
    bake = unreal.Project_JAssetMigrationLibrary.bake_canonical_melee_trajectory

    if mode == "preflight":
        report = {}
        for path, attack in sorted(by_path.items()):
            content_file(project, attack)
            result = json.loads(bake(attack, mesh, False, False))
            result["stored_enabled"] = read(read(attack, "melee_trajectory"), "enabled")
            hit_spec = read(attack, "hit_spec")
            result["trace_radius_cm"] = read(hit_spec, "trace_radius")
            report[path] = result
        dump(output / "Preflight.json", report)
        return

    approved = json.loads((output / "Approved.json").read_text(encoding="utf-8-sig"))
    if not approved or set(approved) - set(by_path):
        raise RuntimeError("Approval must select live kit paths from this equipment definition.")
    for path, expected in approved.items():
        result = json.loads(bake(by_path[path], mesh, False, False))
        if not result["preflight_success"] or stamps(result) != expected:
            raise RuntimeError(f"Source inputs have changed for {path}: {result.get('reason')}")

    if mode == "apply":
        manifest = {}
        for path in sorted(approved):
            source = content_file(project, by_path[path])
            backup = output / "OriginalAssets" / source.name
            backup.parent.mkdir(exist_ok=True)
            if not backup.exists():
                shutil.copy2(source, backup)
                if sha256(source) != sha256(backup):
                    raise RuntimeError(f"Backup does not match {source}")
            manifest[path] = {"path": str(source), "backup": str(backup), "sha256": sha256(backup)}
        dump(output / "Backups.json", manifest)
        report = {}
        for path, expected in sorted(approved.items()):
            result = json.loads(bake(by_path[path], mesh, True, True))
            report[path] = result
            dump(output / "Applied.json", report)
            if not result["executed"] or not (result["saved"] or result.get("unchanged")) or stamps(result) != expected:
                raise RuntimeError(f"Asset write failed or changed: {path}: {result.get('reason')}")
        return

    validator = unreal.get_editor_subsystem(unreal.EditorValidatorSubsystem)
    validation = {}
    for path, expected in approved.items():
        if not read(read(by_path[path], "melee_trajectory"), "enabled") or saved_stamps(by_path[path]) != expected:
            raise RuntimeError(f"Saved melee windows do not match the reviewed candidate: {path}")
        data = unreal.EditorAssetLibrary.find_asset_data(path)
        status, errors, warnings = validator.is_asset_valid(data, unreal.DataValidationUsecase.MANUAL)
        validation[path] = {"status": str(status), "errors": list(map(str, errors)), "warnings": list(map(str, warnings))}
    dump(output / "Verified.json", validation)
    if any(row["errors"] or row["warnings"] or row["status"] != str(unreal.DataValidationResult.VALID)
           for row in validation.values()):
        raise RuntimeError("Production attack validation did not pass. See Verified.json.")


if __name__ == "__main__":
    main()
