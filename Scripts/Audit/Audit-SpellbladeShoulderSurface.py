"""Find triangle crossings between armor and exposed skin from authored weights."""
import argparse
from collections import Counter, defaultdict
import itertools
import json
import math
from pathlib import Path
import re


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    x, y, z = a
    X, Y, Z = b
    return y * Z - z * Y, z * X - x * Z, x * Y - y * X


def rotation(q):
    x, y, z, w = q
    return (
        (1 - 2 * (y*y + z*z), 2 * (x*y - z*w), 2 * (x*z + y*w)),
        (2 * (x*y + z*w), 1 - 2 * (x*x + z*z), 2 * (y*z - x*w)),
        (2 * (x*z - y*w), 2 * (y*z + x*w), 1 - 2 * (x*x + y*y)),
    )


def segment_hit(a, b, triangle):
    p, q, r = triangle
    edge1, edge2 = sub(q, p), sub(r, p)
    delta = sub(b, a)
    h = cross(delta, edge2)
    determinant = dot(edge1, h)
    if abs(determinant) < 1e-7:
        return None
    offset = sub(a, p)
    u = dot(offset, h) / determinant
    if u < 0 or u > 1:
        return None
    v_cross = cross(offset, edge1)
    v = dot(delta, v_cross) / determinant
    if v < 0 or u + v > 1:
        return None
    t = dot(edge2, v_cross) / determinant
    if not 1e-5 < t < 1 - 1e-5:
        return None
    return tuple(x + t * dx for x, dx in zip(a, delta))


def triangle_hit(a, b):
    for triangle, target in ((a, b), (b, a)):
        for i in range(3):
            point = segment_hit(triangle[i], triangle[(i + 1) % 3], target)
            if point is not None:
                return point
    return None


class Skin:
    def __init__(self, mesh, pattern):
        self.vertices = []
        self.triangles = []
        self.bones = {}
        self.slots = set()
        for section in mesh['sections']:
            slot = section['slot']
            if not re.search(pattern, slot, re.IGNORECASE):
                continue
            self.slots.add(slot)
            indices = {}
            for key, vertex in section['vertices'].items():
                weights = sum(vertex[4::2])
                influences = []
                for index, weight in zip(vertex[3::2], vertex[4::2]):
                    bone = mesh['bones'][str(index)]
                    name, bind = bone['name'], bone['bind']
                    self.bones[name] = bind
                    x, y, z, w = bind[3:7]
                    displacement = sub(vertex[:3], bind[:3])
                    local = tuple(dot(axis, displacement) / scale for axis, scale in
                                  zip(rotation((-x, -y, -z, w)), bind[7:10]))
                    influences.append((name, local, weight / weights))
                indices[int(key)] = len(self.vertices)
                self.vertices.append(influences)
            self.triangles.extend((slot, tuple(indices[i] for i in triangle))
                                  for triangle in section['triangles'])
        if not self.triangles:
            raise ValueError(f'No sections match {pattern!r}')

    def pose(self, bones):
        matrices = {}
        for name, bind in self.bones.items():
            transform = bones[name]
            scale = transform[7:10] or bind[7:10]
            matrix = tuple(tuple(value * factor for value, factor in zip(row, scale))
                           for row in rotation(transform[3:7]))
            matrices[name] = matrix, transform[:3]
        points = []
        for influences in self.vertices:
            point = [0., 0., 0.]
            for name, local, weight in influences:
                matrix, position = matrices[name]
                for i in range(3):
                    point[i] += (dot(matrix[i], local) + position[i]) * weight
            points.append(point)
        return [(slot, tuple(points[i] for i in tri)) for slot, tri in self.triangles]

    def triangle_weights(self):
        result = []
        for _, triangle in self.triangles:
            total = Counter()
            for index in triangle:
                for name, _, weight in self.vertices[index]:
                    total[name] += weight
            result.append(total.most_common(1)[0][0])
        return result


def bounds(triangle):
    return tuple((min(axis), max(axis)) for axis in zip(*triangle))


def cells(aabb):
    return itertools.product(*(range(math.floor(low / 5), math.floor(high / 5) + 1)
                               for low, high in aabb))


def crossings(a, b, neck, labels=None):
    def neck_region(aabb):
        return all(lo <= coordinate + 24. and hi >= coordinate - 24.
                   for (lo, hi), coordinate in zip(aabb, neck))

    grid = defaultdict(list)
    for i, (_, triangle) in enumerate(b):
        aabb = bounds(triangle)
        if not neck_region(aabb):
            continue
        for cell in cells(aabb):
            grid[cell].append((i, aabb))
    count = 0
    closest = None
    pairs = Counter()
    for j, (slot_a, triangle) in enumerate(a):
        aabb = bounds(triangle)
        if not neck_region(aabb):
            continue
        seen = set()
        for cell in cells(aabb):
            for i, other_aabb in grid.get(cell, ()):
                if i in seen:
                    continue
                seen.add(i)
                if any(hi < lo2 or hi2 < lo for (lo, hi), (lo2, hi2) in zip(aabb, other_aabb)):
                    continue
                slot_b, other = b[i]
                hit = triangle_hit(triangle, other)
                if hit is not None:
                    count += 1
                    if labels:
                        pairs[labels[0][j], labels[1][i]] += 1
                    distance = math.dist(hit, neck)
                    if closest is None or distance < closest['distance_from_neck']:
                        closest = dict(distance_from_neck=distance, point=hit, sections=[slot_a, slot_b])
    result = dict(crossing_pairs=count, closest=closest)
    if labels:
        result['dominant_skin_pairs'] = pairs.most_common(5)
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('mesh', type=Path)
    parser.add_argument('section_a')
    parser.add_argument('section_b')
    parser.add_argument('--other-mesh', type=Path)
    parser.add_argument('--poses', type=Path)
    parser.add_argument('--cases', default='.*')
    parser.add_argument('--stride', type=int, default=4)
    args = parser.parse_args()
    mesh = json.loads(args.mesh.read_text(encoding='utf-8'))
    other = json.loads(args.other_mesh.read_text(encoding='utf-8')) if args.other_mesh else mesh
    a, b = Skin(mesh, args.section_a), Skin(other, args.section_b)
    if a.slots & b.slots:
        raise ValueError(f'Overlapping section selection: {a.slots & b.slots}')
    bind_a, bind_b = ({bone['name']: bone['bind'] for bone in asset['bones'].values()}
                      for asset in (mesh, other))
    neck = other.get('neck', 'Bip01_Neck')
    labels = a.triangle_weights(), b.triangle_weights()
    print('sections', sorted(a.slots), sorted(b.slots), flush=True)
    print('triangles', len(a.triangles), len(b.triangles), flush=True)
    print('bind', crossings(a.pose(bind_a), b.pose(bind_b), bind_b[neck][:3], labels), flush=True)
    if not args.poses:
        return
    data = json.loads(args.poses.read_text(encoding='utf-8'))
    assert not data['error'], data['error']
    cases = [(name, rows) for name, rows in data['cases'].items()
             if re.fullmatch(args.cases, name)]
    if not cases:
        parser.error(f'No pose cases match {args.cases!r}')
    for name, rows in cases:
        if not rows:
            parser.error(f'Pose case {name!r} has no frames')
        missing = (a.bones.keys() | b.bones.keys()) - rows[0]['bones'].keys()
        if missing:
            raise ValueError(f'Missing pose bones: {sorted(missing)}')
        result = []
        for row in rows[::args.stride]:
            bones = row['bones']
            hit = crossings(a.pose(bones), b.pose(bones), bones[neck][:3], labels)
            if hit['crossing_pairs']:
                hit['case_time'] = round(row['t'], 3)
                hit['montage_time'] = round(row['montage_position'], 3)
                result.append(hit)
        nearest = min(result, key=lambda hit: hit['closest']['distance_from_neck'], default=None)
        largest = max(result, key=lambda hit: hit['crossing_pairs'], default=None)
        print(name, dict(crossing_frames=len(result), sampled=len(rows[::args.stride]),
                         nearest=nearest, largest=largest), flush=True)


if __name__ == '__main__':
    main()
