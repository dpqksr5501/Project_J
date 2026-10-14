"""Read-only session; normal quit through the public editor lifecycle, never saves."""
from pathlib import Path
import json,time,unreal
root=Path(unreal.Paths.project_saved_dir())/'Validation'/'Reaudit_20261014'
stop=root/'McpSession1_Stop.flag'
deadline=time.monotonic()+1200
handle=None
def tick(_delta):
    global handle
    if not stop.exists() and time.monotonic()<deadline:return
    unreal.unregister_slate_post_tick_callback(handle)
    handle=None
    unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(tick)
(root/'McpSession1_Ready.json').write_text(json.dumps({'ready':True,'asset_mutation':False,'maximum_seconds':1200}),encoding='utf-8')
