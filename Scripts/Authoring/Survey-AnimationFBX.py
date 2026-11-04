"""Read-only FBX survey. Run with Blender --background --python THIS -- SOURCE OUTPUT.

One file at a time; never saves or changes a source FBX/blend. JSON is resumable
only when both source SHA256 and this script SHA256 match. No Unreal imports.
"""
import bpy
from io_scene_fbx import parse_fbx
from pathlib import Path
import sys, json, hashlib, math, statistics, traceback, time

TICKS = 46186158000
BONES = ['Bip01 Pelvis', 'Bip01 Spine', 'Bip01 Spine1', 'Bip01 Spine2',
         'Bip01 Neck', 'Bip01 Head']
for side in ('L', 'R'):
    BONES += [f'Bip01 {side} {part}' for part in
              ('Clavicle', 'UpperArm', 'Forearm', 'Hand', 'Thigh', 'Calf', 'Foot', 'Toe0')]
BONES += ['Bip_Weapon_R', 'Bip_Weapon_R_01', 'Bip_Weapon_R_02',
          'Bip_Weapon_R_03', 'Bip_Weapon_L', 'Bip_Weapon_R_Attach_In']


def sha(path):
    return hashlib.file_digest(path.open('rb'), 'sha256').hexdigest()


def metadata(path):
    root, version = parse_fbx.parse(str(path))
    settings, takes, counts, steps = {}, [], {}, []
    objects = next(e for e in root.elems if e.id == b'Objects')
    curve_hash = hashlib.sha256()
    for e in root.elems:
        if e.id == b'GlobalSettings':
            for props in e.elems:
                if props.id == b'Properties70':
                    for p in props.elems:
                        settings[p.props[0].decode()] = [v.decode(errors='replace') if isinstance(v, bytes) else v
                                                       for v in p.props[4:]]
        if e.id == b'Takes':
            for take in e.elems:
                if take.id == b'Take':
                    span = next((c.props for c in take.elems if c.id == b'LocalTime'), [])
                    takes.append({'name': take.props[0].decode(), 'ticks': list(span)})
    for e in objects.elems:
        k = e.id.decode()
        counts[k] = counts.get(k, 0) + 1
        if e.id == b'AnimationCurve':
            for c in e.elems:
                if c.id in (b'KeyTime', b'KeyValueFloat'):
                    arr = c.props[0]
                    curve_hash.update(c.id)
                    curve_hash.update(arr.tobytes())
                    if c.id == b'KeyTime' and len(arr) > 1:
                        steps.extend(int(arr[i+1])-int(arr[i]) for i in range(min(len(arr)-1, 8))
                                     if arr[i+1] > arr[i])
    dt = statistics.median(steps) if steps else 0
    return {'fbx_version': version, 'settings': settings, 'takes': takes,
            'object_counts': counts, 'key_rate_hz': TICKS/dt if dt else None,
            'ordered_curve_arrays_sha256': curve_hash.hexdigest()}


def sample(arm, scene, seconds, first, fps):
    f = first + seconds*fps
    scene.frame_set(math.floor(f), subframe=f-math.floor(f))
    bpy.context.view_layer.update()
    result = {}
    for name in BONES:
        pb = arm.pose.bones.get(name)
        if pb:
            m = arm.matrix_world @ pb.matrix
            p, q, s = m.decompose()
            result[name] = {'p': [round(x, 7) for x in p],
                            'q': [round(x, 7) for x in q]}
    return {'t': round(seconds, 7), 'bones': result}


def extract(path):
    meta = metadata(path)
    # Fixed analysis rate. Duration comes from seconds, not source filename or
    # the FBX's often unspecified TimeMode=0 / misleading global 1s span.
    scene = bpy.context.scene
    scene.render.fps = 30
    scene.render.fps_base = 1
    before = set(bpy.data.objects)
    bpy.ops.import_scene.fbx(filepath=str(path), use_anim=True,
                             ignore_leaf_bones=False, automatic_bone_orientation=False)
    arms = [o for o in scene.objects if o not in before and o.type == 'ARMATURE' and o.animation_data
            and o.animation_data.action]
    if len(arms) != 1:
        raise ValueError(f'Expected one animated armature, got {len(arms)}')
    arm = arms[0]
    action = arm.animation_data.action
    first, last = action.frame_range
    fps = scene.render.fps / scene.render.fps_base
    duration = (last-first)/fps
    keyed_duration = duration
    if duration < 0:
        raise ValueError('Negative duration')
    take_seconds = [(t['ticks'][1]-t['ticks'][0])/TICKS for t in meta['takes'] if len(t['ticks']) == 2]
    if duration == 0 and len(take_seconds) == 1 and take_seconds[0] >= 0:
        # A single keyed pose can have a nonzero declared hold span. Preserve
        # both facts rather than labelling this static figure a broken clip.
        duration = take_seconds[0]
    if len(take_seconds) != 1 or abs(take_seconds[0]-duration) > 0.002:
        raise ValueError(f'Action/FBX Take duration mismatch: {duration}, {take_seconds}')
    rest = [(b.name, b.parent.name if b.parent else '',
             [round(v, 5) for row in b.matrix_local for v in row])
            for b in arm.data.bones if b.name in BONES]
    preview = [sample(arm, scene, duration*i/16, first, fps) for i in range(17)]
    width = min(.30, duration/3)
    start = [sample(arm, scene, width*i/9, first, fps) for i in range(10)]
    end = [sample(arm, scene, duration-width+width*i/9, first, fps) for i in range(10)]
    # All bone/object animation channels, excluding arbitrary action/FBX IDs.
    curves = []
    for layer in action.layers:
        for strip in layer.strips:
            for bag in strip.channelbags:
                for fc in bag.fcurves:
                    keys = [(round((kp.co.x-first)/fps, 7), round(kp.co.y, 6), kp.interpolation)
                            for kp in fc.keyframe_points]
                    curves.append((fc.data_path, fc.array_index, keys))
    motion_hash = hashlib.sha256(json.dumps(sorted(curves), separators=(',', ':')).encode()).hexdigest()
    return {'metadata': meta, 'duration_s': duration, 'keyed_duration_s': keyed_duration,
            'static_pose': keyed_duration == 0, 'analysis_fps': fps,
            'armature_bones': len(arm.data.bones), 'sample_bones': len(rest),
            'rest_signature': hashlib.sha256(json.dumps(rest).encode()).hexdigest(),
            'motion_sha256': motion_hash, 'curves': len(curves),
            'preview': preview, 'start_window': start, 'end_window': end}


def clean():
    # Only the caller's dedicated analysis scene and its now-unused data.
    data_blocks, actions, materials = set(), set(), set()
    for obj in list(bpy.context.scene.objects):
        if obj.data:
            data_blocks.add(obj.data)
            if hasattr(obj.data, 'materials'):
                materials.update(m for m in obj.data.materials if m)
        if obj.animation_data and obj.animation_data.action:
            actions.add(obj.animation_data.action)
        bpy.data.objects.remove(obj, do_unlink=True)
    for data in data_blocks | actions | materials:
        if data.users == 0:
            bpy.data.batch_remove(ids=[data])


def survey(source, output):
    """Yield after each clip, suitable for a responsive interactive timer too.

    clean() requires a dedicated empty analysis scene/process, never an artist's
    existing file. The caller must enforce this before consuming the generator.
    """
    source, output = Path(source), Path(output)
    output.mkdir(parents=True, exist_ok=True)
    script_hash = sha(Path(__file__))
    files = sorted(source.glob('*.fbx'))
    failures, completed, reused = [], [], 0
    started = time.time()
    for i, path in enumerate(files):
        dest = output / (path.stem+'.json')
        source_hash = sha(path)
        try:
            if dest.exists():
                old = json.loads(dest.read_text('utf-8'))
                if old.get('source_sha256') == source_hash and old.get('script_sha256') == script_hash:
                    completed.append(path.name)
                    reused += 1
                    continue
            clean()
            row = extract(path)
            if sha(path) != source_hash:
                raise ValueError('Source changed during analysis')
            row.update(name=path.stem, source_sha256=source_hash,
                       source_bytes=path.stat().st_size, script_sha256=script_hash)
            tmp = dest.with_suffix('.tmp')
            tmp.write_text(json.dumps(row, ensure_ascii=False, separators=(',', ':')), 'utf-8')
            tmp.replace(dest)
            completed.append(path.name)
        except Exception:
            failures.append({'name': path.name, 'error': traceback.format_exc()})
            (output.parent/'Failures.json').write_text(json.dumps(failures, indent=2), 'utf-8')
            if len(failures) >= 3 and not completed:
                raise RuntimeError('Repeated extraction failure; inspect Failures.json before retrying')
        print(f'SURVEY {i+1}/{len(files)} ok={len(completed)} failed={len(failures)} {path.name}', flush=True)
        progress = {'done': i+1, 'total': len(files), 'ok': len(completed),
                    'failed': len(failures), 'last': path.name}
        (output.parent/'Progress.json').write_text(json.dumps(progress), 'utf-8')
        yield progress
    clean()
    receipt = {'schema': 1, 'source': str(source), 'blender': bpy.app.version_string,
               'script_sha256': script_hash, 'files': len(files), 'completed': len(completed),
               'reused': reused, 'failures': failures, 'elapsed_s': time.time()-started}
    (output/'Receipt.json').write_text(json.dumps(receipt, ensure_ascii=False, indent=2), 'utf-8')
    print(json.dumps(receipt), flush=True)
    if failures:
        raise RuntimeError(f'{len(failures)} failed clips')


def retry_failed(source, output):
    """Repair only recorded failures, preserving the original receipt and each
    row's extraction-script hash. Does NOT claim old rows used the new script.
    Revalidate every input; refuse stale/missing prior successes.
    """
    source, output = Path(source), Path(output)
    receipt_path = output/'Receipt.json'
    receipt = json.loads(receipt_path.read_text('utf-8'))
    if not receipt['failures']:
        raise ValueError('No recorded failures to retry')
    original = output/'Receipt.initial.json'
    if original.exists() and original.read_bytes() != receipt_path.read_bytes():
        raise ValueError('Prior repair changed the receipt; inspect it before another repair')
    if not original.exists():
        original.write_bytes(receipt_path.read_bytes())
    names = [r['name'] for r in receipt['failures']]
    script_hash = sha(Path(__file__))
    for name in names:
        if Path(name).name != name or not name.endswith('.fbx'):
            raise ValueError('Unsafe failure filename')
        path = source/name
        before = sha(path)
        clean()
        row = extract(path)
        if sha(path) != before:
            raise ValueError('Source changed during retry')
        row.update(name=path.stem, source_sha256=before, source_bytes=path.stat().st_size,
                   script_sha256=script_hash)
        (output/(path.stem+'.json')).write_text(json.dumps(row,ensure_ascii=False,separators=(',',':')),'utf-8')
    versions = {}
    files = sorted(source.glob('*.fbx'))
    for path in files:
        row = json.loads((output/(path.stem+'.json')).read_text('utf-8'))
        if row['source_sha256'] != sha(path):
            raise ValueError('Stale input: '+path.name)
        version = row['script_sha256']
        versions[version] = versions.get(version,0)+1
    if len(files) != receipt['files']:
        raise ValueError('Input set changed')
    receipt.update(completed=len(files), failures=[], script_versions=versions,
                   repair_scope=names, repair_script_sha256=script_hash,
                   initial_receipt_sha256=sha(original), source_hashes_reverified=len(files))
    receipt_path.write_text(json.dumps(receipt,ensure_ascii=False,indent=2),'utf-8')
    clean()
    return receipt


if __name__ == '__main__':
    args = sys.argv[sys.argv.index('--')+1:]
    if args and args[0] == '--retry-failed':
        print(json.dumps(retry_failed(*args[1:]),ensure_ascii=False))
    else:
        for _ in survey(*args):
            pass
