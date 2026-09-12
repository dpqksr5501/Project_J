"""Recompute F fixture statistics using only Python's standard library.

Usage: python Summarize-Experiments.py METRICS_DIRECTORY
Each timing is described by its originating CSV; these are not FPS benchmarks.
"""
import csv
import json
import math
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def read(root, name):
    with (root / name).open(encoding="utf-8-sig", newline="") as stream:
        return list(csv.DictReader(stream))


def stats(values):
    values = sorted(map(float, values))
    return dict(n=len(values), median=statistics.median(values),
                p95=values[math.ceil(len(values) * .95) - 1], maximum=values[-1])


def grouped(rows, keys, timings, sums=()):
    groups = defaultdict(list)
    for row in rows:
        groups[tuple(row[key] for key in keys)].append(row)
    result = []
    for key, group in sorted(groups.items()):
        entry = dict(zip(keys, key))
        entry.update({field: stats(row[field] for row in group) for field in timings})
        entry.update({field + "_sum": sum(int(row[field]) for row in group) for field in sums})
        result.append(entry)
    return result


def summarize(root):
    return {
        "method": "Median; p95 nearest rank ceil(0.95*N). One process run; samples are not independent process replications.",
        "scheduling": grouped(read(root, "scheduling.csv"), ["candidates", "api"], ["total_us"]),
        "tick": grouped(read(root, "tick.csv"), ["iterations", "concurrent", "prerequisite"],
                        ["world_tick_us"], ["stale", "off_gt"]),
        "consumer_shutdown_ms": stats(row["shutdown_ms"] for row in read(root, "consumer.csv")),
        "contention": grouped(read(root, "consumer-contention.csv"), [], ["drain_ms", "stop_join_ms"], ["completed"]),
        "physics": grouped(read(root, "physics.csv"), ["taskgraph"], ["compute_us", "dispatch_us", "gt_wait_us", "delivery_us"], ["off_gt", "discarded"]),
        "gpu": grouped(read(root, "gpu.csv"), ["requested_async"], ["host_delivery_ms"], ["cancelled"]),
        "audio": grouped(read(root, "audio.csv"), ["mode", "stopping"],
                         ["fixture_active_sounds", "device_sources", "virtual_components"]),
        "pcg": read(root, "pcg.csv"),
        "gpu_queue": gpu_queue(root) if (root / "gpu-queue-events.csv").exists() else [],
        "light_pso": [dict(condition=case, **{
            key: read(root, "light-pso-" + case + ".csv")[0][key]
            for key in ("Full Precached", "Full Missed", "Full TooLate", "Full Untracked", "Total Time (sec)")
        }) for case in ("baseline", "after", "warm")
            if (root / ("light-pso-" + case + ".csv")).exists()],
    }


def gpu_queue(root):
    events = read(root, "gpu-queue-events.csv")
    values = sorted((e for e in events if e["TimerName"] == "ProjectJ_F_ReadbackValues"), key=lambda e: float(e["StartTime"]))
    graphics = sorted((e for e in events if e["TimerName"] == "ProjectJ_F_GraphicsQueueCompetition"), key=lambda e: float(e["StartTime"]))
    samples = read(root, "gpu.csv")
    if len(values) != 32 or len(graphics) != 32 or len(samples) != 32:
        raise ValueError("Expected exactly 32 separately drained GPU jobs")
    rows = []
    for value, graphic, sample in zip(values, graphics, samples):
        start, end = float(value["StartTime"]), float(value["EndTime"])
        gs, ge = float(graphic["StartTime"]), float(graphic["EndTime"])
        overlap = max(0, min(end, ge) - max(start, gs)) * 1e6
        rows.append(dict(requested_async=sample["requested_async"],
                         scope_us=(end - start) * 1e6,
                         graphics_scope_us=(ge - gs) * 1e6,
                         overlap_us=overlap, overlapped=int(overlap > 0),
                         compute_queue=int(value["ThreadId"] == "65537")))
    return grouped(rows, ["requested_async"], ["scope_us", "graphics_scope_us", "overlap_us"], ["overlapped", "compute_queue"])


if __name__ == "__main__":
    print(json.dumps(summarize(Path(sys.argv[1])), indent=2, ensure_ascii=False))
