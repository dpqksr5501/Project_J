"""Summarize matched cooked UI captures. Stdlib only; does not claim a before/after improvement."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path


def summarize(evidence: Path, discard: int):
    result = json.loads((evidence / "Results.json").read_text(encoding="utf-8-sig"))
    for run in result["runs"]:
        if not run["passed"]:
            raise ValueError(f"Failed runtime: {run['log']}")
        resolution = f"{run['width']}x{run['height']}"
        files = list((evidence / resolution).rglob("*.csv"))
        if len(files) != 1:
            raise ValueError(f"Expected one CSV for {evidence}/{resolution}, found {len(files)}")
        path = files[0]
        with path.open(encoding="utf-8-sig", newline="") as stream:
            reader = csv.DictReader(stream)
            keys = [k for k in reader.fieldnames if k in ("FrameTime", "Exclusive/GameThread/UI",
                    "Slate/GameThread/SObjectWidget_Tick", "DrawCall/SlateUI") or k.startswith("ProjectJUI/")]
            rows = []
            for row in reader:
                try:
                    frame = float(row.get("FrameTime", ""))
                except (TypeError, ValueError):
                    continue  # Footer metadata and repeated header are not frames.
                if math.isfinite(frame):
                    rows.append(row)
        if len(rows) != 240 or discard < 0 or discard >= len(rows):
            raise ValueError(f"Invalid sample: {len(rows)} frames, discard={discard}")
        metrics = {}
        for key in keys:
            values = [float(row[key]) for row in rows[discard:]]
            if not all(math.isfinite(v) for v in values):
                raise ValueError(f"Non-finite metric {key}")
            metrics[key] = {"mean": sum(values) / len(values), "p95": sorted(values)[math.ceil(.95 * len(values)) - 1],
                            "max": max(values)}
        yield {"evidence": str(evidence.resolve()), "resolution": resolution, "executableSha256": result["sha256"],
               "csv": str(path.resolve()), "csvSha256": hashlib.sha256(path.read_bytes()).hexdigest(),
               "capturedNumericFrames": len(rows), "discardedInitialFrames": discard,
               "sampleFrames": len(rows) - discard, "metrics": metrics}


if __name__ == "__main__":
    parser = argparse.ArgumentParser()
    parser.add_argument("evidence", nargs="+", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--discard", type=int, default=120)
    args = parser.parse_args()
    runs = [run for directory in args.evidence for run in summarize(directory, args.discard)]
    if len({r["executableSha256"] for r in runs}) != 1:
        raise ValueError("Matched measurements must use the same executable")
    report = {"condition": "Development/D3D12/RenderOffscreen/t.MaxFPS=60; CSV starts after fixture setup; steady-state sample; no pre-change comparison",
              "runs": runs}
    args.output.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    for run in runs:
        metric = run["metrics"]["Exclusive/GameThread/UI"]
        print(f"{Path(run['evidence']).name} {run['resolution']}: UI mean={metric['mean']:.4f}ms p95={metric['p95']:.4f}ms")
