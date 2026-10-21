"""Shared evidence checks for the live player PIE fixtures; no asset mutation."""
import datetime
import math
import re
from pathlib import Path
import unreal


def local_world():
    worlds = [w for w in unreal.EditorLevelLibrary.get_pie_worlds(False)
              if unreal.GameplayStatics.get_player_pawn(w, 0)
              and unreal.GameplayStatics.get_player_pawn(w, 0).is_locally_controlled()]
    assert len(worlds) == 1, 'Expected one local player world'
    return worlds[0]


def local_inputs(world):
    """Resolve inputs in the tested PIE world, never another client's subsystem."""
    pawn = unreal.GameplayStatics.get_player_pawn(world, 0)
    assert pawn and pawn.is_locally_controlled(), 'The fixture needs a local pawn'
    systems = [s for s in unreal.ObjectIterator(unreal.EnhancedInputLocalPlayerSubsystem)
               if not s.get_path_name().startswith('/Script/') and s.get_world() == world]
    assert len(systems) == 1, 'Expected one local input subsystem in the tested world'
    return systems[0]


def output_file(scenario):
    directory = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_saved_dir())) / 'Validation/PlayerLocomotion'
    directory.mkdir(parents=True, exist_ok=True)
    return directory / (scenario + '_' + datetime.datetime.now().strftime('%Y%m%d_%H%M%S_%f') + '.json')


def check_air(report):
    samples = report['samples']
    assert samples, 'No evaluated frames'
    assert all(not s['PlayerNoValidAnim'] for s in samples), 'Missing animation during a tested transition'
    assert all(s['PlayerFootPlacementEnable'] and s['PlayerOffsetRootBoneEnabled'] for s in samples), 'GASP correction was globally disabled'
    assert all(s['LastAirReselectionToTime'] + .001 >= s['LastAirReselectionFromTime'] for s in samples), 'Air reselection rewound playback'
    for case in range(8):
        frames = [s for s in samples if s['case'] == case]
        assert len(frames) >= 20, 'Insufficient samples for case ' + str(case)
        assert any('Falling' in s['mode'] for s in frames), 'Jump did not physically enter air: ' + str(case)
        expected = 'STRAFE' if report['rotation_modes_expected'][case] == 'Strafe' else 'ORIENT_TO_MOVEMENT'
        assert any('Falling' in s['mode'] and expected in s['PlayerRotationMode'] for s in frames), 'Expected rotation mode was not exercised in air: ' + str(case)
        assert any('Walking' in s['mode'] and s['u'] > 3.5 for s in frames), 'Jump did not return to ground: ' + str(case)
    counts = [s['AirReselectionCount'] for s in samples]
    assert max(counts) > min(counts), 'No live air reselection was exercised'
    return {'frames': len(samples), 'cases': 8, 'reselection_count_delta': max(counts) - min(counts), 'invalid_animations': 0, 'playhead_rewinds': 0}


def check_network(report):
    samples = report['samples']
    assert len(samples) >= 100, 'Insufficient network samples'
    paths = {a['path'] for a in report['actors']}
    assert len(paths) == 4, 'Expected authority, autonomous and simulated roles'
    played = {a['montage'] for s in samples for a in s['actors'] if a['montage']}
    assert len(played) >= 6, 'The real skill/attack flow was not exercised'
    for path in paths:
        frames = [a for s in samples for a in s['actors'] if a['path'] == path]
        assert any(a['montage'] for a in frames), 'No replicated montage on ' + path
        assert not frames[-1]['montage'] and 'Walking' in frames[-1]['mode'], 'Montage/air ownership did not release: ' + path
        assert all(a['foot'] and a['root_offset'] for a in frames), 'Procedural locomotion was disabled globally'
    return {'frames': len(samples), 'roles': len(paths), 'montages': len(played)}


def check_ground(report):
    samples = report['samples']
    assert len(samples) >= 500, 'Insufficient evaluated ground frames'
    assert all(not s['PlayerNoValidAnim'] for s in samples), 'Missing ground selection'
    assert all(s['PlayerFootPlacementEnable'] and s['PlayerOffsetRootBoneEnabled'] for s in samples), 'Corrections disabled globally'
    assert len({s['PlayerRotationMode'] for s in samples}) >= 2, 'Both OTM and Strafe must execute'
    assert any('Player_FastFeet_FastRoot' in s['PlayerBlendStackInputs'] for s in samples), 'Ground Chooser profile names did not resolve to the migrated Manny blend profile'
    clips = {asset for s in samples for asset, weight in re.findall(r'Asset=(\S+) Time=[0-9.]+ Length=[0-9.]+ Weight=([0-9.]+)', s['played']) if float(weight) > .25}
    assert any('_Start' in p for p in clips), 'No evaluated start clip'
    assert any('_Stop' in p for p in clips), 'No evaluated stop clip'
    assert max(math.hypot(*s['vel'][:2]) for s in samples) > 500., 'Sprint did not reach its ground family'
    assert math.hypot(*samples[-1]['vel'][:2]) < 2., 'Released input did not stop'
    return {'frames': len(samples), 'rotation_modes': 2, 'evaluated_clips': sorted(clips), 'start_and_stop': True}


def check_sprint_turn(report):
    samples = report['samples']
    assert len(samples) >= 500, 'Insufficient evaluated sprint-turn frames'
    assert all(not s['PlayerNoValidAnim'] for s in samples), 'Missing animation during a sprint turn'
    assert all(math.isfinite(v) for s in samples for v in [s['speed'], s['yaw'], *s['velocity']]), 'Nonfinite movement'
    cases = []
    for case in range(8):
        frames = [s for s in samples if s['case'] == case]
        assert len(frames) >= 30, 'Missing turn case ' + str(case)
        settled = [s for s in frames if 4.5 <= s['u'] < 4.9]
        assert settled and min(s['speed'] for s in settled) > 450., 'Sprint failed to resume after turn ' + str(case)
        assert all(('STRAFE' if case >= 4 else 'ORIENT_TO_MOVEMENT') in s['PlayerRotationMode'] for s in settled), 'Expected turn mode was not evaluated: ' + str(case)
        error = max(abs((s['control'] - s['yaw'] + 180.) % 360. - 180.) for s in settled)
        assert error < 5., 'Facing failed to settle after turn ' + str(case)
        assert any('Weight=1.000' in s['played'] for s in settled), 'No active GASP player after turn'
        cases.append({'case': case, 'mode': 'Strafe' if case >= 4 else 'OTM', 'turn': [170,179,181,190][case%4],
                      'settled_min_speed': min(s['speed'] for s in settled), 'settled_max_facing_error': error})
    return {'frames': len(samples), 'cases': cases, 'invalid_animations': 0}


def check_view_rotation(report):
    samples=report['samples']
    assert len(samples)>=200, 'Insufficient network view samples'
    # A listen server retains a controller for the remote authoritative pawn;
    # only the client-side simulated pawn is controller-less.
    remote=[a['path'] for a in report['actors'] if not a['local']]
    assert len(remote)==2, 'Expected both nonlocal presentations'
    assert sum(not a['controller'] for a in report['actors'])==1, 'Expected the controller-less client simulated pawn'
    assert len({a['role'] for a in report['actors']})==3, 'Authority, autonomous and simulated roles must execute'
    assert all(math.isfinite(v) for s in samples for a in s['actors'] for v in [*a['aim'],*a['smoothed'],*a['ao'],a['body_yaw']]), 'Nonfinite view or pose value'
    assert all(not a['invalid'] for s in samples for a in s['actors']), 'Missing GASP animation'
    results=[]
    for path in remote:
        frames=[a for s in samples if 6.<=s['t']<16. for a in s['actors'] if a['path']==path]
        assert len(frames)>=100 and all(a['combat'] for a in frames), 'Strafe presentation was not established'
        separation=max(abs((a['aim'][1]-a['body_yaw']+180.)%360.-180.) for a in frames)
        assert separation>5., 'Remote camera-facing input collapsed to body yaw'
        yaw=[(a['aim'][1]+180.)%360.-180. for a in frames]
        assert max(yaw)>170. and min(yaw)<-170., 'Camera did not cross both sides of the 180-degree seam'
        assert any(abs(a['ao'][0])>1. for a in frames), 'Independent view did not reach evaluated aim-offset state'
        results.append({'path':path,'samples':len(frames),'max_view_body_separation':separation})
    return {'frames':len(samples),'roles':4,'remote_presentations':results,'invalid_animations':0}
