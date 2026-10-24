"""Compare verified real-RHI crowd runs with equipped weapon meshes shown/hidden."""
import argparse
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics


PHASES = {2: 'idle', 3: 'move_pivot', 4: 'jump_land', 5: 'attack', 6: 'return'}
METRICS = ('frame_delta_ms', 'gt_ms', 'render_ms', 'rhi_ms', 'gpu_ms')


def stats(values):
    ordered = sorted(values)
    if not ordered or any(not math.isfinite(v) or v < 0 for v in ordered):
        raise ValueError('Missing or invalid timing samples')
    return dict(n=len(ordered), mean=statistics.mean(ordered), median=statistics.median(ordered),
                p95=ordered[math.ceil(.95*len(ordered))-1])


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def summarize(root):
    manifest = read_json(root/'manifest.json')
    verification = read_json(root/'verification.json')
    process = read_json(root/'process.json')
    count = manifest['actors']
    visible = manifest['weaponVisibility']
    provenance = manifest.get('sourceProvenance', {})
    if provenance.get('schema') != 1 or not provenance.get('files') or not provenance.get('binaries'):
        raise ValueError('Missing source/binary provenance: '+str(root))
    if not verification['success'] or process['exitCode'] != 0 or not manifest['movement'] or visible not in (0, 1):
        raise ValueError('Requires a verified moving weapon comparison: '+str(root))
    rows = list(csv.DictReader((root/'frames.csv').open(encoding='utf-8-sig', newline='')))
    phases = {}
    for phase, name in PHASES.items():
        group = [r for r in rows if int(r['phase']) == phase]
        captures = {i+j for i, row in enumerate(group) if int(row['capture_requested']) for j in range(3)}
        measured = [r for i, r in enumerate(group) if 2 <= float(r['phase_game_s']) <= 7 and i not in captures]
        if phase == 5:
            measured = [r for r in measured if int(r['montages']) == count]
        if not measured:
            raise ValueError(f'No steady-state phase {phase}: {root}')
        if any(float(row['gpu_ms']) <= 0 for row in measured):
            raise ValueError('No valid GPU timing: '+str(root))
        for row in measured:
            expected = {'actors': count, 'weapon_meshes': count, 'weapon_mesh_matches': count,
                        'weapon_visible': count if visible else 0, 'weapon_auto_lod': count}
            if any(int(row[key]) != value for key, value in expected.items()):
                raise ValueError('Workload/weapon contract changed: '+str(root))
        phases[name] = {key: stats([float(r[key]) for r in measured]) for key in METRICS}
        phases[name]['rendered_bodies'] = sorted({int(r['rendered_actors']) for r in measured})
        phases[name]['rendered_weapons'] = sorted({int(r['weapon_rendered']) for r in measured})
        phases[name]['body_lods'] = {key: sorted({int(r[key]) for r in measured})
                                     for key in ('body_lod0','body_lod1','body_lod2')}
    return dict(run=root.name, actors=count, weapon_visible=visible, phases=phases,
                frames_sha256=hashlib.sha256((root/'frames.csv').read_bytes()).hexdigest(),
                peak_process_memory=process, manifest=manifest)


def compare(roots):
    runs = [summarize(root) for root in roots]
    provenance = {json.dumps(run['manifest']['sourceProvenance'], sort_keys=True) for run in runs}
    assets = {json.dumps(run['manifest']['artifacts'], sort_keys=True) for run in runs}
    conditions = {json.dumps({k: run['manifest'][k] for k in
                             ('actors','movement','consoleCommands','naniteMode','localPlayer','resolution','expectedWeaponMesh')},sort_keys=True)
                  for run in runs}
    if len(provenance) != 1 or len(assets) != 1 or len(conditions) != 1:
        raise ValueError('Comparison source/binary/assets or rendering conditions differ')
    groups = {mode: [r for r in runs if r['weapon_visible'] == mode] for mode in (0, 1)}
    if any(not group for group in groups.values()):
        raise ValueError('Both shown and hidden runs are required')
    comparison = {}
    for phase in PHASES.values():
        comparison[phase] = {}
        for metric in METRICS:
            hidden = statistics.mean(r['phases'][phase][metric]['mean'] for r in groups[0])
            shown = statistics.mean(r['phases'][phase][metric]['mean'] for r in groups[1])
            comparison[phase][metric] = dict(hidden=hidden, shown=shown, added_ms=shown-hidden,
                                               added_percent=(shown/hidden-1)*100 if hidden else None)
    return dict(method='Equal weight per run. Steady game-time 2–7s; exclude screenshot request and next 2 frames. '
                       'Attack requires all authored montages. Hidden keeps weapon actors/equipment/animation alive. '
                       'GPU and CPU execute in parallel; this is a standalone authority-avatar workload, not 200 remote connections.',
                comparison=comparison, runs=runs)


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='+')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(compare(args.runs), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
