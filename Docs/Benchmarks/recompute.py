"""Recompute published measurements; standard library only, no engine execution."""
import csv
import hashlib
import json
import math
import statistics
from collections import defaultdict
from pathlib import Path

DATA = Path(__file__).resolve().parent / 'Data'


def read_json(name):
    return json.loads((DATA / name).read_text(encoding='utf-8-sig'))


def read_csv(name):
    with (DATA / name).open(encoding='utf-8-sig', newline='') as stream:
        return list(csv.DictReader(stream))


def p95(values):
    return sorted(values)[math.ceil(len(values) * 0.95) - 1]


def reduction(before, after):
    return (before - after) / before * 100


def main():
    for entry in read_json('provenance.json')['files']:
        # Git checkouts may use LF or CRLF. Hash the documented canonical text.
        canonical = (DATA / entry['file']).read_text(encoding='utf-8-sig').encode('utf-8')
        actual = hashlib.sha256(canonical).hexdigest()
        if actual != entry['sha256']:
            raise ValueError(f"Checksum mismatch: {entry['file']}")

    groups = defaultdict(list)
    for row in read_csv('mass-crowd.csv'):
        groups[(int(row['count']), row['mode'])].append(float(row['total_ms']))
    assert len(groups) == 8 and all(len(v) == 120 for v in groups.values())
    mass = {f'{count}/{mode}': {'samples': len(v), 'median_ms': statistics.median(v), 'p95_ms': p95(v)}
            for (count, mode), v in sorted(groups.items())}
    # p50 in the original summary used nearest rank; report it explicitly too.
    for (count, mode), values in groups.items():
        mass[f'{count}/{mode}']['p50_ms'] = sorted(values)[math.ceil(len(values) * .5) - 1]
    pool_rows = read_csv('effects-pool.csv')
    pool = {}
    for enabled in ['0', '1']:
        values = [float(r['spawn_ms']) for r in pool_rows if r['pool'] == enabled and int(r['cycle']) > 0]
        assert len(values) == 11
        pool[enabled] = {'samples': len(values), 'median_ms': statistics.median(values), 'p95_ms': p95(values)}
    network = defaultdict(lambda: {'bytes': 0, 'seconds': 0.0})
    for row in read_csv('network.csv'):
        network[row['run']]['bytes'] += int(row['out_bytes'])
        network[row['run']]['seconds'] += float(row['seconds'])
    for values in network.values():
        values['bytes_per_second'] = values['bytes'] / values['seconds']
    phases = [int(r['updates']) for r in read_csv('animation-phases.csv')]
    regressions = {}
    for name in ['main-regression.json', 'main-rendered.json']:
        v = read_json(name)
        assert v['passed'] and v['exitCode'] == 0 and v['errors'] == 0
        regressions[name] = {'passed': v['succeeded'] + v['succeededWithWarnings'], 'warnings': v['warnings']}
    result = {
        'checksums': 'matched', 'mass': mass, 'pool': pool, 'network': dict(network),
        'reductions_percent': {
            'mass_2048_p95': reduction(mass['2048/Serial']['p95_ms'], mass['2048/Parallel']['p95_ms']),
            'pool_spawn_p95': reduction(pool['0']['p95_ms'], pool['1']['p95_ms']),
            'spatial_bytes_per_second': reduction(network['NetworkAllRelevant02']['bytes_per_second'], network['NetworkSpatial02']['bytes_per_second']),
        },
        'animation_selection': {'frames_including_initial': len(phases), 'first_frame': phases[0], 'subsequent_frames': len(phases) - 1, 'subsequent_peak': max(phases[1:]), 'subsequent_total': sum(phases[1:]), 'total_including_initial': sum(phases)},
        'regressions': regressions,
    }
    print(json.dumps(result, ensure_ascii=False, indent=2))


if __name__ == '__main__':
    main()
