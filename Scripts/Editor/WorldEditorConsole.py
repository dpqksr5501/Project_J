"""Serial official MCP console input; submission is not execution completion.

Callers must inspect their native result receipt before proceeding or retrying.
Never assumes a stale widget ref or appends to a previous console command.
"""
import importlib.util,json,re,time
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
PORT=8000
spec=importlib.util.spec_from_file_location('world_mcp',Path(__file__).with_name('World-MCP.py'));M=importlib.util.module_from_spec(spec);spec.loader.exec_module(M)
def call(name,args=None,toolset='EditorToolset.EditorAppToolset'):
    r=M.invoke(PORT,'tools/call',dict(name='call_tool',arguments=dict(toolset_name=toolset,tool_name=name,arguments=args or {})))
    assert not r.get('error') and not r.get('result',{}).get('isError'),str(r)[:400]
    return json.loads(r['result']['content'][0]['text'])['returnValue']
def console(command):
    ts='SlateInspectorToolset.SlateInspectorToolset';observer=None
    try:
        observer=call('Observe',dict(ref='',maxDepth=40),ts)
        snapshot=call('Snapshot',dict(ref='',maxDepth=40),ts)
        boxes=[(int(y),int(w),ref) for y,w,h,ref in re.findall(r'textbox[^\n]*?pos=\d+,(\d+) size=(\d+),(\d+)\].*?\[ref=(\w+)\]',snapshot) if int(h)<30 and int(w)>=250]
        assert boxes and 'Cmd' in snapshot,'Observe the main editor console input first'
        ref=max(boxes)[2];assert call('Click',dict(ref=ref),ts)
        assert call('PressKey',dict(key='Ctrl+A'),ts)
        assert call('Type',dict(ref=ref,text=command,submit=True),ts)
    finally:
        if observer:call('Unobserve',dict(identifier=observer),ts)
def script(path,variables=None):
    path=Path(path).resolve();assert path.is_relative_to(ROOT)
    console('py import runpy; runpy.run_path('+repr(path.as_posix())+', init_globals='+repr(variables or {})+')')
