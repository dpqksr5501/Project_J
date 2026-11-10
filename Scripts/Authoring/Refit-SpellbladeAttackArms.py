"""Refit the Spellblade strike arm paths while preserving the authored hand and weapon anchors.

Run in an Unreal Python commandlet. With no arguments, evaluate without saving.
Pass --apply only after reviewing the trial.
"""

import argparse
import json
import math
from pathlib import Path

import unreal as u


KIT = "/Game/Anim_Assets/Spellblade/Animations/Sword/"
# Montage directory, montage role, arm, inclusive authored key interval, pole limits.
RECIPES = (
    ("CombatRevision", "BasicCut02", "l", (16, 26), .01, 19.),
    ("Melee", "SprintAttack", "r", (14, 26), .05, 12.),
)
MARKER = "ProjectJ.SpellbladeAttackArmRefit"
REVISION = "1"
SOURCE_RATE = 30
BAKE_RATE = 120
SUBSTEPS = BAKE_RATE // SOURCE_RATE
API = u.AnimPoseExtensions
WORLD = u.AnimPoseSpaces.WORLD
LOCAL = u.AnimPoseSpaces.LOCAL
ANCHORS = ("root", "Bip_Weapon_R", "ik_hand_gun", "hand_l", "hand_r",
           "pelvis", "head", "foot_l", "foot_r")


def add(a, b):
    return tuple(x + y for x, y in zip(a, b))


def sub(a, b):
    return tuple(x - y for x, y in zip(a, b))


def mul(a, scale):
    return tuple(x * scale for x in a)


def dot(a, b):
    return sum(x * y for x, y in zip(a, b))


def cross(a, b):
    x, y, z = a
    u, v, w = b
    return y * w - z * v, z * u - x * w, x * v - y * u


def unit(a):
    return mul(a, 1 / math.sqrt(dot(a, a)))


def project(a, normal):
    return sub(a, mul(normal, dot(a, normal)))


def qmul(a,b):
    v=add(add(mul(a[:3],b[3]),mul(b[:3],a[3])),cross(a[:3],b[:3]))
    return (*v,a[3]*b[3]-dot(a[:3],b[:3]))
def qinv(q):return (*mul(q[:3],-1),q[3])
def between(a,b):
    a,b=unit(a),unit(b)
    if dot(a,b)<-.99999:
        axis=min(((1,0,0),(0,1,0),(0,0,1)),key=lambda n:abs(dot(n,a)))
        return (*unit(cross(a,axis)),0.)
    return unit((*cross(a,b),1+dot(a,b)))
def qslerp(a,b,t):
    d=dot(a,b)
    if d<0:b=mul(b,-1);d=-d
    if d>.9995:return unit(add(mul(a,1-t),mul(b,t)))
    w=math.acos(min(1,d))
    return add(mul(a,math.sin((1-t)*w)/math.sin(w)),mul(b,math.sin(t*w)/math.sin(w)))
def angle(a,b):return math.degrees(2*math.acos(min(1,abs(dot(a,b)))))
def fit(rows,side,window,penalty,limit):
    bones=[n+'_'+side for n in ('upperarm','lowerarm','hand')]
    first,last=window
    frames=[]
    for i,r in enumerate(rows[first:last+1],first):
        shoulder,elbow,hand=(r[b][:3] for b in bones)
        arm = sub(elbow, shoulder)
        axis=unit(sub(hand,shoulder))
        width=math.dist(hand,shoulder)
        a,b=math.dist(shoulder,elbow),math.dist(elbow,hand)
        along=(a*a-b*b+width*width)/(2*width)
        radius=math.sqrt(max(0,a*a-along*along))
        pole=unit(project(arm,axis)) if radius>1e-5 else (0,0,1)
        radial=cross(axis,pole)
        def candidate(deg):
            theta=math.radians(deg)
            new=add(add(shoulder,mul(axis,along)),mul(add(mul(pole,math.cos(theta)),mul(radial,math.sin(theta))),radius))
            if deg==0:new=elbow
            shift=math.dist(elbow,new)
            return (new,unit(sub(new,shoulder)),unit(sub(hand,new)),shift)
        angles=(0,) if i in window else range(-180,180,5)
        head=r['head'][:3]
        chest=r['spine_05'][:3]
        allowed=[c for deg in angles if (c:=candidate(deg))[3]<=limit and
                 math.dist(c[0],head)>=min(12,math.dist(elbow,head))-.001 and
                 math.dist(c[0],chest)>=min(11,math.dist(elbow,chest))-.001]
        frames.append(allowed)
    # Each pose has a ring of feasible elbows. Find the lowest-motion path through the rings.
    cost = [0.]
    parents = []
    for old,new in zip(frames,frames[1:]):
        prev=[]
        for e,u,f,shift in new:
            prev.append(min((c+2-dot(u,prev_u)-dot(f,prev_f),j)
                            for j,(c,(_,prev_u,prev_f,_)) in enumerate(zip(cost,old))))
        cost=[score+penalty*(c[3]/15)**2 for (score,_),c in zip(prev,new)]
        parents.append([j for _,j in prev])
    j = 0
    path = [frames[-1][0]]
    for i in range(len(parents)-1,-1,-1):
        j = parents[i][j]
        path.append(frames[i][j])
    path.reverse()
    # Transport both limb frames along the chosen axes and meet the authored end rotations.
    edits=[]
    for bone,axis in zip(bones[:2],(1,2)):
        transport=[tuple(rows[first][bone][3:7])]
        for prev,next in zip(path,path[1:]):
            transport.append(qmul(between(prev[axis],next[axis]),transport[-1]))
        end=qmul(qinv(transport[-1]),rows[last][bone][3:7])
        out=[]
        for i,q in enumerate(transport):
            t=i/(len(transport)-1)
            alpha=t*t*(3-2*t)
            out.append(qmul(q,qslerp((0,0,0,1),end,alpha)))
        edits.append(out)
    peaks=[]
    for k,bone in enumerate(bones[:2]):
        before=max(angle(rows[i][bone][3:7],rows[i+1][bone][3:7]) for i in range(first,last))
        after=max(angle(a,b) for a,b in zip(edits[k],edits[k][1:]))
        peaks.append((before,after))
    return peaks,max(c[3] for c in path),path,edits



def transform_values(transform):
    p = transform.translation
    q = transform.rotation
    return p.x, p.y, p.z, q.x, q.y, q.z, q.w


def transforms(pose, bones):
    return {bone: API.get_bone_pose(pose, bone, WORLD) for bone in bones}


def resolve_clip(folder, role):
    montage = u.load_asset(KIT + f"Montage/CombatKit/{folder}/AM_SpellbladeKit_{role}")
    assert montage, role
    slots = montage.get_editor_property("slot_anim_tracks")
    segments = [segment for slot in slots
                for segment in slot.get_editor_property("anim_track").get_editor_property("anim_segments")]
    assert len(segments) == 1, montage.get_path_name()
    segment = segments[0]
    clip = segment.get_editor_property("anim_reference")
    assert clip and clip.get_name() == f"AS_SpellbladeDirect_{role}", montage.get_path_name()
    return clip, segment


def prepare(role, clip, segment, side, window, penalty, limit, options):
    model = clip.get_editor_property("data_model_interface")
    count = model.get_number_of_frames() + 1
    length = clip.get_play_length()
    assert count > window[1] and abs((count - 1) / length - SOURCE_RATE) < .01, role
    first, last = (frame * SUBSTEPS for frame in window)
    t0, t1 = first / BAKE_RATE, last / BAKE_RATE
    assert segment.get_editor_property("anim_start_time") <= t0 < t1 <= segment.get_editor_property("anim_end_time"), role
    assert model.get_number_of_transform_curves() == 0, role
    track_names = tuple(str(name) for name in model.get_bone_track_names())
    float_curves = u.AnimationLibrary.get_animation_curve_names(clip, u.RawCurveTrackTypes.RCT_FLOAT)
    original_curves = {str(name): u.AnimationLibrary.get_float_keys(clip, name) for name in float_curves}
    notify_fingerprint = u.Project_JPlayerLocomotionLibrary.get_animation_notify_fingerprint(clip)
    parent = "clavicle_" + side
    arm = tuple(name + "_" + side for name in ("upperarm", "lowerarm", "hand"))
    bones = (parent, "head", "spine_05", *arm)
    poses = [API.get_anim_pose_at_time(clip, i / BAKE_RATE, options) for i in range((count - 1) * SUBSTEPS + 1)]
    world = [transforms(pose, bones) for pose in poses]
    rows = [{bone: transform_values(transform) for bone, transform in row.items()} for row in world]
    peaks, shift, path, rotations = fit(rows, side, (first, last), penalty / SUBSTEPS ** 2, limit)
    assert max(after for _, after in peaks) < max(before for before, _ in peaks) * .9, (role, peaks)
    tracks = {bone: [API.get_bone_pose(pose, bone, LOCAL) for pose in poses] for bone in track_names}
    for index in range(first + 1, last):
        row = world[index]
        upper = row[arm[0]]
        lower = row[arm[1]]
        hand = row[arm[2]]
        i = index - first
        upper.rotation = u.Quat(*rotations[0][i])
        lower.rotation = u.Quat(*rotations[1][i])
        lower.translation = u.Vector(*path[i][0])
        for bone, child, parent_frame in zip(arm, (upper, lower, hand), (row[parent], upper, lower)):
            tracks[bone][index] = u.MathLibrary.make_relative_transform(child, parent_frame)

    active_end = segment.get_editor_property("anim_end_time")
    times = [i / (BAKE_RATE * 2) for i in range(int(active_end * BAKE_RATE * 2) + 1)]
    def sample(time):
        pose = API.get_anim_pose_at_time(clip, time, options)
        return {bone: transform_values(tr) for bone, tr in transforms(pose, ANCHORS).items()}
    before = [sample(time) for time in times]
    controller = clip.get_editor_property("controller")
    controller.open_bracket("Refit authored strike arm", False)
    try:
        controller.set_frame_rate(u.FrameRate(BAKE_RATE, 1), False)
        for bone, keys in tracks.items():
            assert controller.set_bone_track_keys(bone, [k.translation for k in keys], [k.rotation for k in keys],
                                                  [k.scale3d for k in keys], False), (role, bone)
    finally:
        controller.close_bracket(False)
    assert u.Project_JAssetMigrationLibrary.match_animation_playback_to_authored_rate(clip), role
    assert abs(clip.get_play_length() - length) < .0001, role
    assert notify_fingerprint == u.Project_JPlayerLocomotionLibrary.get_animation_notify_fingerprint(clip), role
    assert original_curves == {str(name): u.AnimationLibrary.get_float_keys(clip, name) for name in float_curves}, role
    after = [sample(time) for time in times]
    anchors = {}
    for bone in ANCHORS:
        position = [math.dist(a[bone][:3], b[bone][:3]) for a, b in zip(before, after)]
        rotation = [angle(a[bone][3:], b[bone][3:]) for a, b in zip(before, after)]
        anchors[bone] = dict(max_cm=max(position), max_deg=max(rotation), key_cm=max(position[::2]),
                             key_deg=max(rotation[::2]))
    return dict(role=role, arm=side, sample_hz=BAKE_RATE, peaks=peaks, elbow_shift_cm=shift, anchors=anchors)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--apply", action="store_true")
    mode.add_argument("--verify", action="store_true")
    args = parser.parse_args()
    body = u.load_asset("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete")
    assert body
    options = u.AnimPoseEvaluationOptions()
    options.set_editor_property("optional_skeletal_mesh", body)
    options.set_editor_property("should_retarget", False)
    library = u.EditorAssetLibrary
    if args.verify:
        compressed = u.AnimPoseEvaluationOptions()
        compressed.set_editor_property("optional_skeletal_mesh", body)
        compressed.set_editor_property("should_retarget", False)
        compressed.set_editor_property("evaluation_type", u.AnimDataEvalType.COMPRESSED)
        for folder, role, _, _, _, _ in RECIPES:
            clip, segment = resolve_clip(folder, role)
            model = clip.get_editor_property("data_model_interface")
            rate = model.get_frame_rate()
            count = model.get_number_of_frames() + 1
            assert library.get_metadata_tag(clip, MARKER) == REVISION, role
            assert rate.numerator == BAKE_RATE and rate.denominator == 1, role
            assert u.AnimationLibrary.get_num_keys(clip) == count, role
            end = segment.get_editor_property("anim_end_time")
            position_error = rotation_error = 0.
            for index in range(int(end * BAKE_RATE) + 1):
                time = index / BAKE_RATE
                raw = transforms(API.get_anim_pose_at_time(clip, time, options), ANCHORS)
                cooked = transforms(API.get_anim_pose_at_time(clip, time, compressed), ANCHORS)
                for bone in ANCHORS:
                    a, b = transform_values(raw[bone]), transform_values(cooked[bone])
                    position_error = max(position_error, math.dist(a[:3], b[:3]))
                    rotation_error = max(rotation_error, angle(a[3:], b[3:]))
            u.log(f"SPELLBLADE_ATTACK_REFIT_VERIFIED {role} {count} keys "
                  f"compression error {position_error:.3f} cm / {rotation_error:.3f} deg")
            assert position_error < .5 and rotation_error < .5, role
        return
    prepared = []
    report = []
    for folder, role, side, window, penalty, limit in RECIPES:
        clip, segment = resolve_clip(folder, role)
        if library.get_metadata_tag(clip, MARKER) == REVISION:
            continue
        result = prepare(role, clip, segment, side, window, penalty, limit, options)
        report.append(result)
        prepared.append(clip)
        u.log(f"SPELLBLADE_ATTACK_REFIT {result}")

    out = Path(u.Paths.project_saved_dir()) / "SpellbladeCombatNext"
    filename = "SpellbladeAttackRefitApplied.json" if args.apply else "SpellbladeAttackRefitTrial.json"
    (out / filename).write_text(json.dumps(report, indent=2), encoding="utf-8")
    for result in report:
        for bone, delta in result["anchors"].items():
            assert delta["key_cm"] < .05 and delta["key_deg"] < .15, (result["role"], bone, delta)
            if bone in ("Bip_Weapon_R", "hand_l", "hand_r"):
                assert delta["max_cm"] < 1. and delta["max_deg"] < 1.05, (result["role"], bone, delta)
            else:
                assert delta["max_cm"] < .5 and delta["max_deg"] < .5, (result["role"], bone, delta)
    if args.apply:
        for clip in prepared:
            library.set_metadata_tag(clip, MARKER, REVISION)
            assert library.save_loaded_asset(clip, only_if_is_dirty=False), clip.get_path_name()


if __name__ == "__main__":
    main()
