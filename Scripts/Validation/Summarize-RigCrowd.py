"""Summarize current direct-body 200-avatar runs, retaining historical workload differences."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics

from importlib.util import module_from_spec, spec_from_file_location

spec = spec_from_file_location('weapon_crowd', Path(__file__).with_name('Summarize-WeaponCrowd.py'))
crowd = module_from_spec(spec)
spec.loader.exec_module(crowd)


def summarize(root):
    result = crowd.summarize(root)
    if result['actors'] != 200 or result['weapon_visible'] != 1 or result['manifest']['trace']:
        raise ValueError('Requires untraced 200-avatar runs with equipped weapons visible')
    rows = list(csv.DictReader((root/'frames.csv').open(encoding='utf-8-sig', newline='')))
    measured = [r for r in rows if int(r['phase']) > 1 and 2 <= float(r['phase_game_s']) <= 7]
    expected = {'direct_bodies': 200, 'retarget_bodies': 0, 'modular_parts': 2000,
                'cloth_parts': 200, 'cloth_tick_parts': 200, 'body_lod0': 200}
    if not measured or any(int(r[key]) != value for r in measured for key, value in expected.items()):
        raise ValueError('Direct pose, LOD or cloth workload changed: '+str(root))
    result['observed_workload'] = {key: sorted({int(r[key]) for r in measured})
                                   for key in (*expected, 'nanite_candidates', 'nanite_parts')}
    result['pose_routes_sha256'] = hashlib.sha256((root/'pose_routes.csv').read_bytes()).hexdigest()
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('runs', type=Path, nargs='+')
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    runs = [summarize(root) for root in args.runs]
    if len(runs) < 2:
        raise ValueError('At least two untraced repetitions are required')
    contracts = [json.dumps({key: run['manifest'][key] for key in
                             ('sourceProvenance','artifacts','actors','movement','consoleCommands',
                              'naniteMode','localPlayer','resolution','expectedWeaponMesh')}, sort_keys=True)
                 for run in runs]
    if len(set(contracts)) != 1:
        raise ValueError('Repeat source/binary/assets or conditions differ')
    aggregate = {name: {metric: {'mean_of_run_means': statistics.mean(run['phases'][name][metric]['mean'] for run in runs),
                                'run_means': [run['phases'][name][metric]['mean'] for run in runs]}
                         for metric in crowd.METRICS} for name in crowd.PHASES.values()}
    args.output.write_text(json.dumps(dict(
        method='Equal weight per run; game-time 2–7s; exclude screenshot frame and next two; attack requires 200 active montages. '
               'CPU/GPU execute concurrently. Historical retarget rigs have different bones, LOD and physics; no causal speedup claim.',
        aggregate=aggregate, runs=runs), ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
