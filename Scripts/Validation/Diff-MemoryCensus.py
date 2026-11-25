"""Compare two UE memreport / obj-list snapshots to isolate Batch 106 memory attribution leaks."""
import argparse
import json
import re
from pathlib import Path


def parse_obj_list(path: Path) -> dict[str, tuple[int, float]]:
    result: dict[str, tuple[int, float]] = {}
    pattern = re.compile(r"^\s*([A-Za-z0-9_]+)\s+(\d+)\s+([0-9.]+)\s+([0-9.]+)")
    for line in path.read_text(encoding="utf-8", errors="ignore").splitlines():
        m = pattern.match(line)
        if m:
            cls, count, num_kb = m.group(1), int(m.group(2)), float(m.group(3))
            result[cls] = (count, num_kb)
    return result


def main() -> int:
    parser = argparse.ArgumentParser(description="Diff two UE obj-list memory census files.")
    parser.add_argument("baseline", type=Path)
    parser.add_argument("later", type=Path)
    parser.add_argument("--top", type=int, default=20)
    args = parser.parse_args()

    a = parse_obj_list(args.baseline)
    b = parse_obj_list(args.later)
    deltas = []
    for cls in sorted(set(a) | set(b)):
        c0, kb0 = a.get(cls, (0, 0.0))
        c1, kb1 = b.get(cls, (0, 0.0))
        if c1 > c0 or kb1 > kb0:
            deltas.append({
                "class": cls,
                "countDelta": c1 - c0,
                "kbDelta": round(kb1 - kb0, 2),
                "laterCount": c1,
                "laterKB": round(kb1, 2),
            })
    deltas.sort(key=lambda r: (r["kbDelta"], r["countDelta"]), reverse=True)
    print(json.dumps({"topGrowth": deltas[: args.top], "totalTrackedClasses": len(b)}, indent=2))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
