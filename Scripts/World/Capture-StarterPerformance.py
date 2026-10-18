"""Bounded engine CSV capture with actual PIE settings; never sets a budget.

Invoke with LABEL (unique), FRAMES and WARMUP_S. Records context; the engine
stops automatically after FRAMES. GPU CSV instrumentation restores afterwards.
"""
import json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Structure'
label=globals().get('LABEL','capital-pie');frames=int(globals().get('FRAMES',1800));warmup=float(globals().get('WARMUP_S',5));assert 100<=frames<=10000 and 0<=warmup<=120
file=OUT/(label+'-capture.json');assert not file.exists(),'Preserve previous capture context'
assert unreal.EditorLevelLibrary.get_pie_worlds(False)
before={str(p) for p in (ROOT/'Saved/Profiling/CSV').glob('*.csv')}
gpu_before=unreal.SystemLibrary.get_console_variable_int_value('r.GPUCsvStatsEnabled')
registered=time.perf_counter();capturing=False;frame_start=None;r=dict(label=label,phase='WARMUP',requested_frames=frames,warmup_s=warmup,scope='Local PIE observation; not packaged/minimum-spec benchmark')
def save():file.write_text(json.dumps(r,indent=2),encoding='utf-8')
def tick(dt):
    global capturing,frame_start
    worlds=unreal.EditorLevelLibrary.get_pie_worlds(False)
    try:
        assert worlds,'PIE ended during capture'
        world=worlds[0];now=time.perf_counter()
        if not capturing:
            if now-registered<warmup:return
            pc=unreal.GameplayStatics.get_player_controller(world,0);pawn=unreal.GameplayStatics.get_player_pawn(world,0);assert pc and pawn
            keys=['sg.'+k+'Quality' for k in ['ViewDistance','AntiAliasing','Shadow','GlobalIllumination','Reflection','PostProcess','Texture','Effects','Foliage','Shading','Landscape']]+['r.ScreenPercentage','r.Streaming.PoolSize','r.Streaming.UseFixedPoolSize','r.Lumen.HardwareRayTracing','r.VSync','t.MaxFPS']
            r.update(world=world.get_path_name(),viewport_size=list(pc.get_viewport_size()),pawn_start_cm=list(pawn.get_actor_location().to_tuple()),cvars={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in keys},previous_gpu_csv=gpu_before)
            unreal.SystemLibrary.execute_console_command(world,'r.GPUCsvStatsEnabled 1');unreal.SystemLibrary.execute_console_command(world,'csvprofile frames='+str(frames))
            capturing=True;frame_start=unreal.SystemLibrary.get_frame_count();r['phase']='CAPTURING';save()
        elif unreal.SystemLibrary.get_frame_count()-frame_start>frames+20:
            r.update(phase='COMPLETE',csv_files=[str(p.relative_to(ROOT)) for p in (ROOT/'Saved/Profiling/CSV').glob('*.csv') if str(p) not in before],pawn_end_cm=list(unreal.GameplayStatics.get_player_pawn(world,0).get_actor_location().to_tuple()))
            unreal.SystemLibrary.execute_console_command(world,'r.GPUCsvStatsEnabled '+str(gpu_before));save();unreal.unregister_slate_post_tick_callback(handle)
    except Exception:
        r.update(phase='ERROR',error=traceback.format_exc());save();unreal.unregister_slate_post_tick_callback(handle)
        unreal.SystemLibrary.execute_console_command(worlds[0] if worlds else unreal.EditorLevelLibrary.get_editor_world(),'r.GPUCsvStatsEnabled '+str(gpu_before))
save();handle=unreal.register_slate_post_tick_callback(tick)
