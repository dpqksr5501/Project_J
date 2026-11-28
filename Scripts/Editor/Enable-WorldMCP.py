"""Enable editor-only MCP plugins in explicitly supplied UE 5.8 or 5.9 projects.

Backups are kept under this task's Saved directory. No asset, renderer, remote
Python setting, firewall rule, user-wide Codex setting, or engine file is changed.
"""
import argparse
import hashlib
import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def enable(project):
    project = Path(project).resolve(strict=True)
    data = json.loads(project.read_text(encoding="utf-8-sig"))
    version = data.get("EngineAssociation")
    if version not in {"5.8", "5.9"}:
        raise ValueError(f"Unsupported EngineAssociation {version!r}: {project}")
    backup = ROOT / "Saved/StarterWorld/ProjectDescriptorBackups"
    backup.mkdir(parents=True, exist_ok=True)
    digest = hashlib.sha256(project.read_bytes()).hexdigest()
    snapshot = backup / (project.stem + "-" + digest[:12] + ".uproject")
    if not snapshot.exists():
        snapshot.write_bytes(project.read_bytes())
    plugins = data.setdefault("Plugins", [])
    for name in ["ModelContextProtocol", "AllToolsets", "PythonScriptPlugin"]:
        entry = next((p for p in plugins if p["Name"] == name), None)
        if entry is None:
            entry = {"Name": name}
            plugins.append(entry)
        entry.update(Enabled=True, TargetAllowList=["Editor"])
    project.write_text(json.dumps(data, indent="\t", ensure_ascii=False) + "\n", encoding="utf-8")
    # Per-project defaults, with distinct loopback ports for the three requested editors.
    port = {"Project_J": 8000, "assets": 8001, "MedievalVillage": 8002}.get(project.stem, 8003)
    config = project.parent / "Config/DefaultEditorPerProjectUserSettings.ini"
    before = config.read_text(encoding="utf-8-sig") if config.exists() else ""
    module = "ModelContextProtocol" if version == "5.9" else "ModelContextProtocolEngine"
    section = f"[/Script/{module}.ModelContextProtocolSettings]"
    lines = before.splitlines()
    legacy = "[/Script/ModelContextProtocolEngine.ModelContextProtocolSettings]"
    if version == "5.9" and section not in lines:
        lines = [section if line.strip() == legacy else line for line in lines]
    index = next((i for i, line in enumerate(lines) if line.strip() == section), None)
    if index is None:
        lines.extend(["", section])
        index = len(lines) - 1
    end = next((i for i in range(index + 1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
    settings = {"bAutoStartServer": "True", "ServerPortNumber": str(port), "ServerUrlPath": "/mcp"}
    body = [line for line in lines[index + 1:end] if line.split("=", 1)[0].strip() not in settings]
    lines[index + 1:end] = body + [f"{key}={value}" for key, value in settings.items()]
    config.parent.mkdir(parents=True, exist_ok=True)
    if before:
        config_backup = backup / (project.stem + "-EditorSettings-" + hashlib.sha256(before.encode()).hexdigest()[:12] + ".ini")
        if not config_backup.exists():
            config_backup.write_text(before, encoding="utf-8")
    config.write_text("\n".join(lines) + "\n", encoding="utf-8")
    print(json.dumps({"project": str(project), "original_sha256": digest,
                      "backup": str(snapshot), "plugins": "editor-only MCP + AllToolsets + Python"}))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("projects", nargs="+")
    for argument in parser.parse_args().projects:
        enable(argument)
