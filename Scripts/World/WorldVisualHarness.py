"""Fixed-camera native world visual loop: snapshot, edit one cause, compare.

This harness records images and actual settings; numeric actor counts do not
grade art quality. Never starts PIE, moves a pawn, regenerates PCG or changes
quality while capturing. A run is immutable and requires visual inspection.
"""
import argparse,base64,importlib.util,json,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source'
spec=importlib.util.spec_from_file_location('console',ROOT/'Scripts/Editor/WorldEditorConsole.py');C=importlib.util.module_from_spec(spec);spec.loader.exec_module(C)
parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('label');parser.add_argument('--cameras',nargs='+');args=parser.parse_args();assert args.label.replace('-','').replace('_','').isalnum()
settings=json.loads((SOURCE/'StarterWorld.visual-harness.json').read_text());out=ROOT/'Saved/Validation/StarterWorld/VisualHarness'/args.label;assert not out.exists(),'Preserve previous captures; use a new iteration label';out.mkdir(parents=True)
assert not C.call('IsPIERunning')
previous=C.call('GetCameraTransform');(out/'original-camera.json').write_text(json.dumps(previous,indent=2))
try:
    C.script(ROOT/'Scripts/World/WorldVisualHarnessSession.py',dict(LABEL=args.label,OP='begin'))
    deadline=time.monotonic()+20
    while not (out/'session.json').exists():
        assert time.monotonic()<deadline,'Background rendering session did not start';time.sleep(.25)
    C.script(ROOT/'Scripts/World/Inspect-StarterVisualHarness.py',dict(LABEL=args.label))
    deadline=time.monotonic()+20
    while not (out/'native.json').exists():
        assert time.monotonic()<deadline,'Native sensor receipt absent; do not assume console submission succeeded';time.sleep(.25)
    for name,pose in settings['cameras'].items():
        if args.cameras and name not in args.cameras:continue
        C.call('SetCameraTransform',dict(transform=pose))
        # SetCameraTransform does not invalidate the viewport. Redraw first, then
        # allow actual engine ticks for grass/streaming/occlusion to settle.
        C.call('CaptureViewport',dict(captureTransform=None,annotations=None,bShowUI=False))
        time.sleep(max(8,settings['settle_seconds']))
        captured=C.call('CaptureViewport',dict(captureTransform=None,annotations=None,bShowUI=False));(out/(name+'.png')).write_bytes(base64.b64decode(captured['image']['data']));captured.pop('image');captured['camera']=pose;(out/(name+'.json')).write_text(json.dumps(captured,indent=2));print(name,flush=True)
    (out/'review.json').write_text(json.dumps(dict(status='REQUIRES_VISUAL_REVIEW',criteria=settings['visual_acceptance'],counts_are_not_art_acceptance=True),indent=2))
finally:
    # Keep user camera unchanged after each comparison run.
    if previous:C.call('SetCameraTransform',dict(transform=previous))
    if (out/'session.json').exists() and json.loads((out/'session.json').read_text())['phase']=='ACTIVE':
        C.script(ROOT/'Scripts/World/WorldVisualHarnessSession.py',dict(LABEL=args.label,OP='end'))
