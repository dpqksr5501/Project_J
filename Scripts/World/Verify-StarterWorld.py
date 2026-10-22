"""Reload and inspect the saved starter map without regenerating its contents."""
import array,datetime,json,time,traceback,runpy
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=Path(globals().get('verification_output',ROOT/'Saved/Validation/StarterWorld'))
OUT.mkdir(parents=True,exist_ok=True)
SOURCE=ROOT/'Content/World/MainWorld/Source'
plan=json.loads((SOURCE/'MainWorld.plan.json').read_text(encoding='utf-8'))
manifest=json.loads((SOURCE/'MainWorld.manifest.json').read_text(encoding='utf-8'))
MAP=manifest['world_asset']
CONTRACT=runpy.run_path(str(ROOT/'Scripts/World/Starter-WorldDefinitionContract.py'))
unreal.EditorPythonScripting.set_keep_python_script_alive(True)
levels=unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
assert levels.load_level(MAP)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
started=time.monotonic();finished=False
session_started=time.time()

def verify(dt):
    global finished
    if finished or time.monotonic()-started<20:return
    finished=True;unreal.unregister_slate_post_tick_callback(handle)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        states=[a for a in actors if isinstance(a,unreal.Project_JWorldState)];assert len(states)==1
        definition=states[0].get_editor_property('definition');assert definition
        places=definition.get_editor_property('places');ids=[str(p.get_editor_property('place_id')) for p in places]
        CONTRACT['validate_places'](plan,[CONTRACT['snapshot_place'](p) for p in places],
            manifest['world_grid']['unreal_origin_cm'],manifest['world_grid']['centimetres_per_source_metre'])
        connections=definition.get_editor_property('connections');assert len(connections)==7
        proxies=[a for a in actors if isinstance(a,unreal.LandscapeProxy)]
        component_count=sum(len(a.get_components_by_class(unreal.LandscapeComponent)) for a in proxies)
        assert component_count==1024,component_count
        bounds=[a.get_actor_bounds(False) for a in proxies if a.get_components_by_class(unreal.LandscapeComponent)]
        minimum=[min(c.to_tuple()[i]-e.to_tuple()[i] for c,e in bounds) for i in (0,1)]
        maximum=[max(c.to_tuple()[i]+e.to_tuple()[i] for c,e in bounds) for i in (0,1)]
        extent=[maximum[i]-minimum[i] for i in (0,1)]
        assert all(abs(v-806400)<100 for v in extent),extent
        raw=array.array('H')
        with (SOURCE/'HM_MainWorld.r16').open('rb') as stream:raw.fromfile(stream,4033**2)
        ignore=[a for a in actors if not isinstance(a,unreal.LandscapeProxy)]
        errors=[]
        for entry in plan['places']:
            x,y,z=entry['center_m'];px=(x-4032)*100;py=(y-4032)*100
            hit=unreal.SystemLibrary.line_trace_single(unreal.EditorLevelLibrary.get_editor_world(),unreal.Vector(px,py,200000),unreal.Vector(px,py,-15000),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,unreal.DrawDebugTrace.NONE,True)
            values=hit.to_tuple() if hit else None;assert values and values[0],'Missing terrain at '+entry['id']
            u=x/2;v=y/2;i,j=int(u),int(v);a,b=u-i,v-j
            q=(raw[j*4033+i]*(1-a)+raw[j*4033+i+1]*a)*(1-b)+(raw[(j+1)*4033+i]*(1-a)+raw[(j+1)*4033+i+1]*a)*b
            expected=(q-32768)/128*800
            errors.append(abs(values[4].z-expected))
        assert max(errors)<100,errors
        runtime_hash=unreal.Project_JWorldAuthoringLibrary.get_region_runtime_hash_name()
        assert runtime_hash=='WorldPartitionRuntimeHashSet',runtime_hash
        report=dict(passed=True,map=MAP,place_count=len(ids),hunting_candidates=9,boss_candidates=2,connections=len(connections),world_state_actors=1,landscape_components=component_count,landscape_extent_cm=extent,terrain_collision_samples=len(errors),maximum_collision_source_error_cm=max(errors),runtime_hash=runtime_hash,nav_unverified=True,performance_measured=False,packaging_verified=False,visual_quality_approved=False)
        # Inspect persisted content before any repair or PIE. Running preparation
        # here masked a runtime-only light setter that had never dirtied its asset.
        refreshed=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        environments=[a for a in refreshed if isinstance(a,unreal.Project_JWorldEnvironment)]
        assert len(environments)==1
        environment=environments[0]
        assert environment.get_editor_property('state')==states[0]
        assert environment.get_editor_property('sun')
        assert environment.get_editor_property('clear_sun_intensity')==60000
        sun=environment.get_editor_property('sun')
        saved_lux=sun.get_component_by_class(unreal.DirectionalLightComponent).get_editor_property('intensity')
        assert saved_lux==60000,('Persisted editor light differs from runtime clear light',saved_lux)
        exposure=next(a for a in refreshed if a.get_actor_label()=='MainWorld_Exposure')
        assert all(not a.get_editor_property('is_spatially_loaded') for a in [states[0],environment,exposure])
        starts=[a for a in refreshed if isinstance(a,unreal.PlayerStart)]
        assert len(starts)==1,'Unexpected duplicate PlayerStart'
        x,y,z=next(p for p in plan['places'] if p['id']=='S00')['center_m']
        assert (starts[0].get_actor_location()-unreal.Vector((x-4032)*100,(y-4032)*100,(z+1.3)*100)).length()<1
        assert not starts[0].get_editor_property('is_spatially_loaded')
        report['player_start_count']=1
        report['lighting_saved']=True
        report['saved_directional_light_lux']=saved_lux
        report['authoring_repairs_executed']=False
        report['verified_at_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat()
        (OUT/'saved-map-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        # Keep world reports distinct from UBT's case-insensitive Build.json.
        (OUT/'world-build.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
        unreal.log('STARTER_WORLD_SAVED_MAP_VERIFIED '+json.dumps(report))
        if '-StarterVisualOnly' not in unreal.SystemLibrary.get_command_line():
            smoke_fixture.update(runpy.run_path(str(ROOT/'Scripts/World/Smoke-StarterWorld.py'),init_globals={'smoke_output':OUT}))
    except Exception:
        (OUT/'verification-error.txt').write_text(traceback.format_exc(),encoding='utf-8');unreal.log_error(traceback.format_exc())
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        unreal.SystemLibrary.quit_editor()
handle=unreal.register_slate_post_tick_callback(verify)
smoke_fixture={}

def lifecycle(dt):
    request=OUT/'close-verification.request'
    if finished and request.exists() and request.stat().st_mtime>=session_started:
        unreal.unregister_slate_post_tick_callback(lifecycle_handle)
        if 'restore_play_settings' in smoke_fixture:smoke_fixture['restore_play_settings']()
        unreal.EditorPythonScripting.set_keep_python_script_alive(False)
        unreal.SystemLibrary.quit_editor()
lifecycle_handle=unreal.register_slate_post_tick_callback(lifecycle)
