"""Read-only source audit + isolated, deliberately unapproved terrain experiment.

Never writes Content, changes a plan, or imports Unreal. Output is in Saved.
The candidate tests detail attenuation and channel shoulder profiles; it does
not claim to solve mountain macro-shape, hydraulic erosion, navigation or art.
"""
import hashlib
import importlib.util
import json
import math
from pathlib import Path

import numpy as np
from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[2]
SOURCE = ROOT / 'Content/World/MainWorld/Source'
OUT = ROOT / 'Saved/Validation/StarterWorld/Phase2'
SPEC = importlib.util.spec_from_file_location('terrain', ROOT / 'Scripts/World/Generate-StarterWorld.py')
T = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(T)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def sample(h, x, y, spacing=8):
    u = np.clip(np.asarray(x) / spacing, 0, h.shape[1]-1)
    v = np.clip(np.asarray(y) / spacing, 0, h.shape[0]-1)
    i = np.minimum(u.astype(int), h.shape[1]-2)
    j = np.minimum(v.astype(int), h.shape[0]-2)
    s, t = u-i, v-j
    return (h[j, i]*(1-s)+h[j, i+1]*s)*(1-t)+(h[j+1, i]*(1-s)+h[j+1, i+1]*s)*t


def summary(values):
    return dict(zip(['min', 'p50', 'p95', 'p99', 'max'], map(float, np.percentile(values, [0, 50, 95, 99, 100]))))


def slopes(h):
    gy, gx = np.gradient(h, 8)
    return np.degrees(np.arctan(np.hypot(gx, gy)))


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    protected = [p for p in SOURCE.iterdir() if p.is_file()]
    hashes = {p.name: digest(p) for p in protected}
    plan = json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'))
    layout = json.loads((SOURCE/'MainWorld.corridor.json').read_text(encoding='utf-8'))
    raw = np.fromfile(SOURCE/'HM_MainWorld.r16', dtype='<u2').reshape(4033, 4033)
    height = (raw.astype('f4')-32768)*.0625
    h = height[::4, ::4].copy()
    y, x = np.mgrid[0:1009, 0:1009].astype('f4')*8
    edge = np.minimum.reduce([x, y, 8064-x, 8064-y])
    water = np.full_like(h, 1e9)
    rivers = []
    for river in plan['rivers']:
        points = np.asarray(river['points'])
        d, z = T.distance_to_path(x, y, points)
        water = np.minimum(water, d-river['width_m']/2)
        ds = np.linalg.norm(np.diff(points[:, :2], axis=0), axis=1)
        grade = np.diff(points[:, 2])/ds
        bed = sample(h, points[:, 0], points[:, 1])
        rivers.append(dict(id=river['id'], samples=len(points),
                           water_grade_pct=summary(grade*100),
                           upstream_rises=int(np.count_nonzero(grade > .0001)),
                           center_bed_minus_water_m=summary(bed-points[:, 2]),
                           bed_above_water_samples=int(np.count_nonzero(bed > points[:, 2]))))
    lake = plan['lake']
    cx, cy = lake['center_m']; rx, ry = lake['radius_m']
    lr = np.sqrt(((x-cx)/rx)**2+((y-cy)/ry)**2)
    water = np.minimum(water, (lr-1)*min(rx, ry))
    protection = T.smooth((water-70)/80)*T.smooth((edge-160)/120)*T.smooth((y-1000)/400)
    # Keep existing authored objects/roads/plots and all place cores unchanged.
    # Protection is input-driven, rather than special-case coordinate patches.
    for p in plan['places']:
        distance = np.hypot(x-p['center_m'][0], y-p['center_m'][1])
        radius = p['radius_m'] if p['id'].startswith('S') else 80
        protection *= T.smooth((distance-radius)/90)
    for road in plan['roads']:
        d, _ = T.distance_to_path(x, y, road['points'])
        protection *= T.smooth((d-85)/70)
    # Human direction: keep terrain relief and traversal interest. Test only two
    # local review areas, not the whole region; this is not a flattening policy.
    review_mask=np.maximum(1-T.smooth((np.hypot(x-3370,y-3310)-240)/160),
                           1-T.smooth((np.hypot(x-4046,y-4160)-240)/160))
    protection*=review_mask
    # Retain macro ridges, valleys and all waterways. No target-plane fitting.
    detail = h-T.blur(h, 3.0)
    delta = np.clip(-detail*.4, -2, 2)*protection
    coarse = h+delta
    fine_delta = np.asarray(Image.fromarray(delta).resize((4033, 4033), Image.Resampling.BILINEAR))
    encoded = np.rint((height+fine_delta)/.0625+32768)
    assert encoded.min() >= 0 and encoded.max() <= 65535
    candidate = encoded.astype('<u2')
    # Exact edge contract, not merely close visual agreement.
    assert np.array_equal(candidate[0], raw[0]) and np.array_equal(candidate[-1], raw[-1])
    assert np.array_equal(candidate[:, 0], raw[:, 0]) and np.array_equal(candidate[:, -1], raw[:, -1])
    candidate.tofile(OUT/'HM_DetailAttenuation_Candidate.r16')
    for label, x0, y0 in [('Hamlet',2880,2880),('River',3544,3656)]:
        for variant, data in [('Before',raw),('DetailTest',candidate)]:
            patch=data[y0//2:y0//2+505:4,x0//2:x0//2+505:4]
            assert patch.shape==(127,127)
            patch.tofile(OUT/(label+'_'+variant+'.r16'))
    before_slope, after_slope = slopes(h), slopes(coarse)
    places = []
    for p in plan['places']:
        px, py = p['center_m'][:2]
        area = np.hypot(x-px, y-py) <= p['radius_m']
        # Grade threshold is a planning probe, not CharacterMovement/Nav acceptance.
        places.append(dict(id=p['id'], kind=p['kind'], radius_m=p['radius_m'],
                           slope_deg=summary(before_slope[area]),
                           area_under_15deg_m2=int(np.count_nonzero(area & (before_slope <= 15))*64),
                           fraction_under_15deg=float(np.mean(before_slope[area] <= 15)),
                           center_height_delta_m=float(sample(coarse, px, py)-sample(h, px, py)),
                           nav_collision_combat_clearance_verified=False))
    roads = []
    for road in plan['roads']:
        p = np.asarray(road['points'])
        # Dense 8m samples, including segment endpoints; coarse plan z is insufficient.
        grades = []
        for a, b in zip(p, p[1:]):
            length = math.dist(a[:2], b[:2]); n = max(2, math.ceil(length/8)+1)
            q = np.linspace(a[:2], b[:2], n)
            grades.extend(np.abs(np.diff(sample(h, q[:, 0], q[:, 1])))/(length/(n-1))*100)
        roads.append(dict(id=road['id'], terrain_grade_pct=summary(grades),
                          includes_water_and_bridge_ground=True, traversability_approved=False))
    regions = [('hamlet-forest', (2750, 2750, 3650, 3650)),
               ('river-bridge', (3650, 3850, 4550, 4750)),
               ('mountain', (1800, 5650, 3600, 7450))]
    panels = []
    for name, bounds in regions:
        x0, y0, x1, y1 = bounds
        sl = np.s_[int(y0/8):int(y1/8)+1, int(x0/8):int(x1/8)+1]
        frames = []
        for label, field in [('BASELINE', h), ('DETAIL TEST', coarse)]:
            gy, gx = np.gradient(field, 8)
            illumination = np.clip((.8-.55*gx-.65*gy)/np.sqrt(1+gx*gx+gy*gy), .08, 1)
            rgb = np.stack([illumination*.8, illumination*.86, illumination*.73], axis=-1)
            im = Image.fromarray(np.uint8(rgb[sl][::-1]*255)).resize((600, 600))
            canvas = Image.new('RGB', (600, 632), '#19202b'); canvas.paste(im, (0, 32))
            ImageDraw.Draw(canvas).text((12, 10), name+' / '+label+' / 8m analysis', fill='white')
            frames.append(canvas)
        pair = Image.new('RGB', (1200, 632)); pair.paste(frames[0]); pair.paste(frames[1], (600, 0))
        pair.save(OUT/(name+'-height-analysis.png')); panels.append(name)
    report = dict(source_hashes=hashes, source_unchanged=all(digest(p)==hashes[p.name] for p in protected),
                  grid_m=8, height_quantization_m=.0625, edges_exact=True,
                  candidate='HM_DetailAttenuation_Candidate.r16', candidate_approved=False,
                  experiment_scope='two local detail tests only, +/-2m maximum; retain relief, valleys and water; no flattening policy',
                  change_m=summary(coarse-h), modified_fraction=float(np.mean(np.abs(coarse-h) > .0625)),
                  baseline_slope_deg=summary(before_slope), candidate_slope_deg=summary(after_slope),
                  rivers=rivers, places=places, roads=roads, panels=panels,
                  limitations=['8m sampling misses narrow obstacles', 'low slope area is not connected navigable space',
                               'no material/foliage/collision or gameplay acceptance', 'not an Unreal screenshot'])
    assert report['source_unchanged']
    (OUT/'terrain-diagnosis.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps({k: report[k] for k in ['source_unchanged','edges_exact','modified_fraction','change_m']}))


if __name__ == '__main__':
    main()
