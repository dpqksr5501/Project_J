"""Regenerate and compare render-density and socket-count baselines from retained runs."""
import argparse
import csv
import hashlib
import json
import math
import re
import runpy
from pathlib import Path

_render = runpy.run_path(str(Path(__file__).with_name('Summarize-MoverCrowd.py')))
stats = _render['stats']


def read_json(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def read_csv(path):
    with path.open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    hasher = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            hasher.update(chunk)
    return hasher.hexdigest()


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True).encode()).hexdigest()


def timing_args(arguments, varying):
    output_paths = {'-abslog', '-userdir', '-tracefile', '-projectjdoutput', '-projectjrenderedcrowdoutput'}
    return [arg for arg in arguments if arg.partition('=')[0].lower() not in output_paths | varying
            and not arg.lower().endswith('.uproject')]


def with_provenance(path, manifest, workload, metrics, evidence):
    require(all(math.isfinite(value) and value >= 0 for value in metrics.values()), f'Invalid metric in {path}')
    provenance = manifest['sourceProvenance']
    require(provenance['binaries'] and provenance['files'], f'Missing source/runtime fingerprint in {path}')
    return {'run': str(path.resolve()), 'conditions': workload, 'metrics': metrics,
            'source_sha256': digest(provenance['files']), 'runtime_sha256': digest(provenance['binaries']),
            'evidence': {name: sha(path / name) for name in evidence}}


def summarize_render(path):
    result = _render['summarize'](path)
    manifest = result['manifest']
    process = read_json(path/'process.json')
    viewport = read_json(path/'viewport-output.json')
    outputs = {(row['width'], row['height']) for row in viewport['observedScreenshots']}
    require(process['exitCode'] == 0 and process['hasExited'], f'Incomplete render process: {path}')
    require(manifest['offscreenViewport'] and not manifest['nullRHI'] and manifest['connections'] == 0,
            f'Expected an offscreen render workload without socket clients: {path}')
    require(len(outputs) == 1 and viewport['matchesRequest'] and len(viewport['observedScreenshots']) >= 2,
            f'Actual viewport output not qualified: {path}')
    log = (path/'Run.log').read_text(encoding='utf-8-sig', errors='replace')
    adapters = set(re.findall(r'LogRHI:\s+Name: ([^\r\n]+)', log))
    require(len(adapters) == 1, f'Actual render adapter not identified: {path}')
    require(process['sampledPeakPrivateBytes'] > 0 and process['sampledPeakWorkingSetBytes'] > 0, f'Missing render memory samples: {path}')
    metrics = {'private_peak_mib': process['sampledPeakPrivateBytes']/2**20,
               'working_set_peak_mib': process['sampledPeakWorkingSetBytes']/2**20}
    for phase, rows in result['phases'].items():
        for percentile in ('p50', 'p95', 'p99'):
            metrics[f'{phase}_frame_{percentile}_ms'] = rows['frame_ms'][percentile]
        metrics[f'{phase}_frames'] = rows['frame_ms']['samples']
    conditions = {'adapter': next(iter(adapters)).strip(), 'cpu': manifest['cpu'].strip(),
                  'output': list(next(iter(outputs))), 'host': Path(manifest['executable']).name,
                  'arguments': timing_args(manifest['arguments'], {'-projectjrenderedcrowdcount'})}
    evidence = ('frames.csv', 'manifest.json', 'process.json', 'verification.json', 'viewport-output.json', 'Run.log')
    return manifest['actors'], with_provenance(path, manifest, conditions, metrics, evidence)


def game_intervals(path):
    rows = read_csv(path)
    require(len(rows) > 4 and all(row['ThreadName'] == 'Game Frames' and row['ThreadId'] == '16777216' for row in rows),
            f'Expected complete Game Frames rows: {path}')
    require(all(re.fullmatch(r'Game Frame \d+', row['TimerName']) for row in rows), f'Invalid Game Frame name: {path}')
    indices = [int(row['TimerName'].rsplit(' ', 1)[1]) for row in rows]
    require(indices == list(range(indices[0], indices[0]+len(rows))), f'Incomplete frame sequence: {path}')
    timestamps = [float(row['StartTime']) for row in rows[1:-1]]
    intervals = [(after-before)*1000 for before, after in zip(timestamps, timestamps[1:])]
    require(all(math.isfinite(value) and value > 0 for value in intervals), f'Invalid frame intervals: {path}')
    return intervals


def summarize_server(path, reference_runs=None):
    manifest = read_json(path/'manifest.json')
    check = read_json(path/'verification.json')
    memory = read_json(path/'server-baseline.json')
    args = read_json(path/'Server-effective-arguments.json')
    count = manifest['clientCount']
    require(check['passed'] and check['serverModeVerified'] and check['serverNetworkMode'] == 'DedicatedServer'
            and check['exitCodes'] == [0]*(count+1) and check['timeouts'] == 0
            and len(check['results']) == count+1 and all(row['success'] for row in check['results']),
            f'Expected a verified dedicated socket workload: {path}')
    require('-NullRHI' in args and '-trace=frame,region' in args, f'Server baseline capture flags differ: {path}')
    require(memory['samples'] > 0 and memory['sampledPeakPrivateBytes'] > 0 and memory['peakWorkingSetBytes'] > 0,
            f'Missing server memory samples: {path}')
    stages = read_csv(path/'Server/network-phases.csv')
    require([int(row['stage']) for row in stages] == list(range(1, 6))
            and all(int(row['connections']) == count for row in stages), f'Unexpected socket stage coverage: {path}')
    seconds = [float(row['seconds']) for row in stages]
    byte_counts = [int(row['out_bytes']) for row in stages]
    require(all(math.isfinite(value) and value > 0 for value in seconds) and all(value >= 0 for value in byte_counts),
            f'Invalid network duration or byte count: {path}')
    frame_files = [f'ServerFrames/Server-ProjectJ.Network.Stage{stage}.csv' for stage in range(1, 6)]
    intervals = stats([value for name in frame_files for value in game_intervals(path/name)])
    metrics = {'server_tick_hz': 1000/intervals['mean'], 'server_frame_p95_ms': intervals['p95'],
               'server_frame_p99_ms': intervals['p99'], 'server_frames': intervals['samples'],
               'server_out_kib_s': sum(byte_counts)/sum(seconds)/1024, 'server_out_bytes': sum(byte_counts),
               'server_private_peak_mib': memory['sampledPeakPrivateBytes']/2**20,
               'server_working_set_peak_mib': memory['peakWorkingSetBytes']/2**20}
    conditions = {'cpu': manifest['cpu'].strip(), 'npc_count': manifest['npcCount'], 'animation': manifest['animation'],
                  'host': Path(manifest['executable']).name, 'sample_interval_ms': memory['sampleIntervalMs'],
                  'arguments': timing_args(args, {'-projectjfixtureclients', '-port'})}
    evidence = ['manifest.json', 'verification.json', 'Server-effective-arguments.json',
                'Server/network-phases.csv', 'server-baseline.json', *frame_files]
    trace_name = 'Server.utrace'
    if (path / trace_name).is_file():
        return count, with_provenance(path, manifest, conditions, metrics, [*evidence, trace_name])

    reference = (reference_runs or {}).get(str(count), {})
    reference_evidence = dict(reference.get('evidence', {}))
    trace_sha = reference_evidence.pop(trace_name, '')
    require(re.fullmatch(r'[0-9a-f]{64}', trace_sha),
            f'Missing {trace_name}; replay requires its original comparison baseline: {path}')
    entry = with_provenance(path, manifest, conditions, metrics, evidence)
    require(entry['evidence'] == reference_evidence and all(
        entry[key] == reference.get(key) for key in ('conditions', 'source_sha256', 'runtime_sha256')
    ), f'Missing {trace_name}; the retained inputs differ from the original comparison baseline: {path}')
    entry['evidence'][trace_name] = trace_sha
    entry['trace_evidence'] = 'recorded_sha256'
    return count, entry


def axis(paths, summarize):
    entries = {}
    for path in paths:
        count, entry = summarize(path)
        require(str(count) not in entries, f'Duplicate population {count}: {path}')
        if entries:
            first = next(iter(entries.values()))
            require(entry['conditions'] == first['conditions'] and entry['runtime_sha256'] == first['runtime_sha256'],
                    f'Population series conditions or runtime differ: {path}')
        entries[str(count)] = entry
    return dict(sorted(entries.items(), key=lambda pair: int(pair[0])))


def compare(current, previous):
    require(previous['schema'] == current['schema'] == 1, 'Unsupported baseline schema')
    differences = {}
    for kind in ('render', 'server'):
        require(current[kind].keys() == previous[kind].keys(), f'Baseline {kind} populations differ')
        differences[kind] = {}
        for count, new in current[kind].items():
            old = previous[kind][count]
            require(new['conditions'] == old['conditions'], f'Baseline {kind} conditions differ for population {count}')
            require(new['metrics'].keys() == old['metrics'].keys(), f'Baseline {kind} metrics differ for population {count}')
            differences[kind][count] = {metric: {'previous': old['metrics'][metric], 'current': value,
                                                'delta': value-old['metrics'][metric]}
                                        for metric, value in new['metrics'].items()}
    return differences


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--render', type=Path, nargs='+', required=True)
    parser.add_argument('--server', type=Path, nargs='+', required=True)
    parser.add_argument('--compare', type=Path)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    previous = read_json(args.compare) if args.compare else None
    reference_runs = previous['server'] if previous else None
    result = {'schema': 1, 'render': axis(args.render, summarize_render),
              'server': axis(args.server, lambda path: summarize_server(path, reference_runs))}
    if previous:
        result['comparison'] = compare(result, previous)
    with args.output.open('x', encoding='utf-8') as stream:
        json.dump(result, stream, indent=2)
        stream.write('\n')


if __name__ == '__main__':
    main()
