"""PIE: fixed-camera Strafe reversals and staggered diagonal keyboard edges.

Run on a clear floor in a fresh single-player PIE. No assets are saved.
The forward/backward cases must select their authored reversal. Diagonal
cases vary both key edges, including opposing held keys and a neutral gap.
All durations below drive the fixture only; no runtime grace period is added.
"""
import json
import math
import sys
import traceback
from pathlib import Path
import unreal

sys.path.insert(0, str(Path(__file__).parent))
from PlayerLocomotionRegression import local_world, local_inputs, output_file

world = local_world()
pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
controller = unreal.GameplayStatics.get_player_controller(world, 0)
anim = pawn.get_source_mesh().get_anim_instance()
inputs = local_inputs(world)
names = ['Move', 'ToggleCombat', 'MoveIntent_Forward', 'MoveIntent_Backward', 'MoveIntent_Left', 'MoveIntent_Right']
actions = {n: unreal.load_asset('/Game/Input/Actions/IA_' + n) for n in names}
assert all(actions.values())
bases = {'Forward': (0., 1.), 'Lateral': (1., 0.), 'DiagonalRight': (1., 1.), 'DiagonalLeft': (-1., 1.)}
cases = [(gait, axis, sign, 'Together', 0.) for gait in ['Run', 'Walk'] for axis in bases for sign in [1., -1.]]
cases += [('Run', axis, sign, pattern, lag) for axis in ['DiagonalRight', 'DiagonalLeft'] for sign in [1., -1.]
          for pattern, lag in [('HorizontalFirst', 1./60.), ('HorizontalFirst', .05), ('VerticalFirst', .05), ('LateRelease', .1), ('NeutralGap', .05)]]
reverse_time = 5.8
case_duration = 9.
moving_end = 8.3
original_combat = pawn.is_combat_mode_active()
original_rotation = controller.get_control_rotation()
# A fresh PIE starts in Run; the native walk input latch is intentionally private.
original_walk = False
perf = unreal.get_default_object(unreal.load_class(None, '/Script/UnrealEd.EditorPerformanceSettings'))
original_throttle = perf.get_editor_property('bThrottleCPUWhenNotForeground')
perf.set_editor_property('bThrottleCPUWhenNotForeground', False)
output = output_file('StrafePivot')
report = {'cases': cases, 'samples': []}
start = unreal.GameplayStatics.get_time_seconds(world)
finished = False
running = False
meshes = []
for mesh in pawn.get_components_by_class(unreal.SkeletalMeshComponent):
    bones = [b for b in ['calf_l', 'calf_r', 'Bip01-L-Calf', 'Bip01-R-Calf'] if mesh.get_bone_index(b) >= 0]
    if bones:
        meshes.append((mesh, bones))


def validate():
    checks = []
    for case, (gait, axis, sign, pattern, lag) in enumerate(cases):
        samples = [s for s in report['samples'] if s['case'] == case]
        assert len(samples) >= 60, 'Insufficient evaluated samples: ' + str(case)
        assert not any(s['invalid'] for s in samples), 'Missing animation: ' + str(case)
        assert all(s['foot'] and s['offset'] for s in samples), 'Procedural correction disabled'
        assert all(math.isfinite(v) for s in samples for v in [s['speed'], s['turn'], s['facing']]), 'Nonfinite movement'
        turns = [s for s in samples if reverse_time + .01 < s['u'] < reverse_time + .55 and 'Pivot' in s['tags']]
        assert turns, 'No pivot transition: ' + str(case)
        assert any(s['selected'] in s['played'] for s in turns), 'Selected pivot was not evaluated: ' + str(case)
        if axis == 'Forward':
            family = '_Pivot_F_B_' if sign > 0 else '_Pivot_B_F_'
            assert all(family in s['selected'] for s in turns), 'Wrong reversal family: ' + str((case, sorted({s['selected'] for s in turns})))
            assert any(family in s['played'] for s in turns), 'Selected pivot did not reach an evaluated BlendStack'
        settled = [s for s in samples if moving_end - .8 < s['u'] < moving_end - .1]
        assert settled and min(s['speed'] for s in settled) > (100. if gait == 'Walk' else 400.), 'Reversal did not recover: ' + str(case)
        assert all('STRAFE' in s['rotation_mode'] for s in settled), 'Expected Strafe was not evaluated'
        assert max(abs(s['facing']) for s in settled) < 20., 'Displayed facing did not recover: ' + str(case)
        tx, ty = -bases[axis][1] * sign, -bases[axis][0] * sign
        length = math.hypot(tx, ty)
        assert all((s['velocity'][0]*tx + s['velocity'][1]*ty)/length > .99*s['speed'] for s in settled), 'Movement did not follow the final opposite input'
        frozen = {}
        for mesh, bones in meshes:
            name = mesh.get_name()
            last = None
            since = longest = 0.
            for s in [s for s in samples if reverse_time + .1 < s['u'] < moving_end - .1 and s['speed'] > 100.]:
                if last is None or s['t'] - last['t'] > .15 or max(abs(a-b) for a, b in zip(s['knees'][name], last['knees'][name])) > 1.e-5:
                    since = s['t']
                longest = max(longest, s['t'] - since)
                last = s
            assert longest < .5, 'Frozen evaluated pose: ' + str((case, name, longest))
            frozen[name] = longest
        checks.append({'case': case, 'pattern': pattern, 'lag': lag, 'clips': sorted({s['selected'] for s in turns}), 'longest_frozen_pose': frozen})
    return checks


def finish(error=None):
    global finished
    if finished:
        return
    finished = True
    unreal.unregister_slate_post_tick_callback(handle)
    for action in actions.values():
        inputs.inject_input_vector_for_action(action, unreal.Vector(), [], [])
    controller.set_control_rotation(original_rotation)
    pawn.set_walk(original_walk)
    if pawn.is_combat_mode_active() != original_combat:
        inputs.inject_input_vector_for_action(actions['ToggleCombat'], unreal.Vector(1., 0., 0.), [], [])
    perf.set_editor_property('bThrottleCPUWhenNotForeground', original_throttle)
    if error is None:
        try:
            report['checks'] = validate()
        except Exception:
            error = traceback.format_exc()
    report['completed'] = error is None
    if error:
        report['error'] = error
    output.write_text(json.dumps(report, indent=2), encoding='utf-8')
    unreal.log('Strafe pivot regression completed: ' + str(output) + ' Success=' + str(report['completed']))


def tick(dt):
    global running
    if running or finished:
        return
    running = True
    try:
        t = unreal.GameplayStatics.get_time_seconds(world) - start
        if t >= case_duration * len(cases):
            finish()
            return
        case = int(t / case_duration)
        u = t % case_duration
        gait, axis, sign, pattern, lag = cases[case]
        pawn.set_walk(gait == 'Walk')
        controller.set_control_rotation(unreal.Rotator())
        toggle = .1 < u < 2.5 and int(u * 10.) % 2 == 0 and not pawn.is_combat_mode_active()
        if u > 2.8:
            assert pawn.is_combat_mode_active(), 'Strafe was not established'
        x, y = [v * sign for v in bases[axis]]
        old = ({'Right' if x > 0 else 'Left'} if x else set()) | ({'Forward' if y > 0 else 'Backward'} if y else set())
        opposite = {'Right': 'Left', 'Left': 'Right', 'Forward': 'Backward', 'Backward': 'Forward'}
        new = {opposite[n] for n in old}
        keys = old if u < reverse_time else new
        if reverse_time <= u < reverse_time + lag:
            horizontal = old & {'Left', 'Right'}
            vertical = old & {'Forward', 'Backward'}
            if pattern == 'HorizontalFirst':
                keys = {opposite[n] for n in horizontal} | vertical
            elif pattern == 'VerticalFirst':
                keys = horizontal | {opposite[n] for n in vertical}
            elif pattern == 'LateRelease':
                keys = new | vertical
            elif pattern == 'NeutralGap':
                keys = set()
        if not 2.8 <= u < moving_end:
            keys = set()
        x = float('Right' in keys) - float('Left' in keys)
        y = float('Forward' in keys) - float('Backward' in keys)
        values = {'Move': unreal.Vector(x, y, 0.), 'ToggleCombat': unreal.Vector(float(toggle), 0., 0.)}
        values.update({'MoveIntent_' + n: unreal.Vector(float(n in keys), 0., 0.) for n in ['Forward', 'Backward', 'Left', 'Right']})
        for n, action in actions.items():
            inputs.inject_input_vector_for_action(action, values[n], [], [])
        blend = anim.get_editor_property('PlayerBlendStackInputs')
        selected = blend.get_editor_property('Anim')
        velocity = pawn.get_velocity()
        knees = {}
        for mesh, bones in meshes:
            qs = [mesh.get_socket_transform(b, unreal.RelativeTransformSpace.RTS_PARENT_BONE_SPACE).rotation for b in bones]
            knees[mesh.get_name()] = [c for q in qs for c in [q.x, q.y, q.z, q.w]]
        report['samples'].append({'t': t, 'u': u, 'case': case, 'speed': velocity.length2d(), 'velocity': [velocity.x, velocity.y], 'keys': sorted(keys),
            'knees': knees,
            'selected': selected.get_name() if selected else '', 'tags': str(blend.get_editor_property('Tags')),
            'pivot': anim.evaluate_is_pivoting(), 'direction': str(anim.get_editor_property('PlayerMovementDirection')),
            'recent': str(anim.get_editor_property('PlayerMovementDirectionRecent')),
            'rotation_mode': str(anim.get_editor_property('PlayerRotationMode')),
            'facing': anim.get_editor_property('PlayerFutureFacingDelta'),
            'turn': anim.get_editor_property('PlayerTrajectoryTurnAngle'),
            'invalid': anim.get_editor_property('PlayerNoValidAnim'),
            'foot': anim.get_editor_property('PlayerFootPlacementEnable'),
            'offset': anim.get_editor_property('PlayerOffsetRootBoneEnabled'),
            'played': anim.inspect_foundation_playback()})
    except Exception:
        finish(traceback.format_exc())
    finally:
        running = False


handle = unreal.register_slate_post_tick_callback(tick)
