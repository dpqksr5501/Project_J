"""Summarize validated real-RHI Mover crowd runs; frame time is never a sum of CPU scopes."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


def stats(values):
    values = sorted(values)
    if not values:
        raise ValueError('No measured samples')
    return dict(samples=len(values), mean=statistics.mean(values), p50=statistics.median(values),
                p95=values[math.ceil(.95*len(values))-1], p99=values[math.ceil(.99*len(values))-1])


def summarize(root):
    manifest = json.loads((root/'manifest.json').read_text(encoding='utf-8-sig'))
    verification = json.loads((root/'verification.json').read_text(encoding='utf-8-sig'))
    if not verification['success'] or not manifest['movement'] or manifest['actors'] != 200:
        raise ValueError('Requires verified 200-body Mover workload: '+str(root))
    with (root/'frames.csv').open(encoding='utf-8-sig', newline='') as stream:
        rows = list(csv.DictReader(stream))
    phases = {}
    for phase, name in ((2,'idle'),(3,'move_pivot'),(4,'jump_land'),(5,'attack'),(6,'return')):
        group = [r for r in rows if int(r['phase']) == phase]
        first = float(group[0]['elapsed_s'])
        # Screenshot readback can affect the following frame deltas as well.
        captures = {i+j for i, r in enumerate(group) if int(r['capture_requested']) for j in range(3)}
        measured = [r for i, r in enumerate(group) if float(r['elapsed_s']) >= first+1 and i not in captures]
        if phase == 5:
            measured = [r for r in measured if int(r['montages']) == 200]
        if not all(int(r['actors']) == 200 for r in measured):
            raise ValueError('Crowd size changed')
        phases[name] = dict(total_frames=len(group), excluded_frames=len(group)-len(measured),
                            rendered_200_frames=sum(int(r['rendered_actors']) == 200 for r in measured),
                            duration_seconds=float(group[-1]['elapsed_s'])-first,
                            measured_span_seconds=float(measured[-1]['elapsed_s'])-float(measured[0]['elapsed_s']),
                            frame_ms=stats([float(r['frame_delta_ms']) for r in measured]),
                            gpu_ms=stats([float(r['gpu_ms']) for r in measured]),
                            gt_ms=stats([float(r['gt_ms']) for r in measured]),
                            render_ms=stats([float(r['render_ms']) for r in measured]),
                            rhi_ms=stats([float(r['rhi_ms']) for r in measured]),
                            prediction30=stats([int(r['prediction30']) for r in measured]))
    return dict(run=root.name, phases=phases, frames_sha256=hashlib.sha256((root/'frames.csv').read_bytes()).hexdigest(),
                manifest=manifest, verification=verification)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    result = dict(method='p50 median; p95/p99 nearest rank. Separate runs retain their variance. '
                         'Exclude first 1s per phase, screenshot request frame and next 2 frames. '
                         'Attack uses only simultaneous 200-montage rows. D3D12 offscreen; no socket clients.',
                  runs=[summarize(path) for path in args.runs])
    args.output.write_text(json.dumps(result, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
