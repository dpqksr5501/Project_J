"""Fingerprint narrowly selected local evidence; never modifies project inputs."""
from pathlib import Path
from datetime import datetime, timezone
import hashlib
import json
import shutil
import subprocess

BASE = Path(__file__).resolve().parents[1]
REPO = BASE.parents[3]
PATHS = [
    "AGENTS.md", "Config/DefaultEngine.ini",
    "Source/Project_JCharacter/Public/Animation/Project_JMovingTurnPolicy.h",
    "Source/Project_JCharacter/Public/Animation/Project_JLocomotionContextBuilder.h",
    "Source/Project_JCharacter/Public/Animation/Project_JMotionMatchingSelectionPolicy.h",
    "Source/Project_JCharacter/Private/Components/Project_JPlayerInputBindingComponent.cpp",
    "Source/Project_JCharacter/Private/Project_JPlayerCharacter.cpp",
    "Source/Project_JCharacter/Private/Project_JPlayerCharacter.Movement.cpp",
    "Source/Project_JCharacter/Private/Project_JLocomotionAnimStateComponent.cpp",
    "Source/Project_JCharacter/Private/Animation/Project_JMotionMatchingTrajectoryComponent.cpp",
    "Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstance.cpp",
    "Source/Project_JCharacter/Private/Animation/Project_JCharacterAnimInstanceProxy.cpp",
    "Source/Project_JCharacter/Private/Animation/Project_JMotionMatchingAssetSet.cpp",
    "Source/Project_JCharacter/Private/Animation/Project_JLocomotionProfile.cpp",
    "Docs/Animation/Locomotion/GASP_ProjectJ_Locomotion_Parity.md",
    "Docs/Animation/Architecture/OneShot_Command_Lifetime_2026-10-05.md",
    "Docs/Animation/Architecture/Landing_Return_Linked_Search_2026-10-05.md",
    "Docs/Animation/Architecture/MotionMatching_Return_Crowd_2026-10-05.md",
    "Docs/Animation/Architecture/Moving_Turn_180_2026-10-06.md",
    "Saved/Validation/MovingTurnTrace_20261006/PIE_1053/Analysis.json",
    "Content/Animation_Logic/ABPs/ABP_Humanoid_Master.uasset",
    "Content/Animation_Logic/ABPs/ABP_Player.uasset",
    "Content/Animation_Logic/PSS/PSS_Player.uasset",
    "Content/Animation_Logic/PSS/PSS_Combat.uasset",
    "Content/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Run_Cycle.uasset",
    "Content/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Run_Turn.uasset",
    "Content/Animation_Logic/PSD/PSD_Player_Locomotion/PSD_Combat_Run_Turn.uasset",
    "Content/Animation_Logic/PSD/PSD_Player_Combat_Locomotion/PSD_Combat_Run_Cycle.uasset",
    "Content/DataAssetSets/Animation_Profiles/Combat_MMProfile/DA_Player_Combat_Strafe.uasset",
    "Content/DataAssetSets/Animation_Profiles/LocomotionProfiles/DA_Player_Profile.uasset",
    "Content/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_Turn_L_180_Rfoot.uasset",
]
records = []
for rel in PATHS:
    path = REPO / rel
    row = {"path": rel, "exists": path.exists()}
    if path.exists():
        stat = path.stat()
        row.update(size=stat.st_size,
                   mtime_utc=datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
                   sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    records.append(row)
gasp = Path("C:/Users/I/Documents/Unreal Projects/GameAnimationSample")
for rel in ["Content/Blueprints/SandboxCharacter_CMC_ABP.uasset",
            "Content/Characters/UEFN_Mannequin/Animations/ExperimentalStateMachineData/CHT_CMCCharacterAnimations.uasset"]:
    path = gasp / rel
    row = {"path": str(path), "exists": path.exists(), "project": "GASP"}
    if path.exists():
        stat = path.stat()
        row.update(size=stat.st_size, mtime_utc=datetime.fromtimestamp(stat.st_mtime, timezone.utc).isoformat(),
                   sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    records.append(row)
git = subprocess.run(["git", "status", "--short"], cwd=REPO,
                     capture_output=True, text=True, encoding="utf-8", check=True).stdout
out = {"packaged_at_utc": datetime.now(timezone.utc).isoformat(), "repository": str(REPO),
       "scope": "Packaging-time fingerprints; not original graph inspection timestamps or build verification.",
       "git_status": git.splitlines(), "files": records}
(BASE/"Evidence/file_snapshot.json").write_text(json.dumps(out, ensure_ascii=False, indent=2), encoding="utf-8")
historical = REPO/"Saved/Validation/MovingTurnTrace_20261006/PIE_1053/Analysis.json"
if historical.exists():
    shutil.copyfile(historical, BASE/"Evidence/MovingTurnTrace_PIE1053.Analysis.json")
print(json.dumps({"fingerprinted": len(records), "missing": [r["path"] for r in records if not r["exists"]]}, ensure_ascii=False))
