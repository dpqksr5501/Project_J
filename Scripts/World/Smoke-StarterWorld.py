"""Observe explicit PIE; optionally inspect the corridor without changing weather."""
import json,math,time,traceback
from pathlib import Path
import unreal

out=Path(__file__).resolve().parents[2]/'Saved/Validation/StarterWorld'
editor=unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
phase=0;began=None;changed=None;clear_lux=None;rain_lux=None
network='-StarterNetworkSmoke' in unreal.SystemLibrary.get_command_line()
weather_deferred=bool(globals().get('weather_deferred',False))
report_name='corridor-pie-smoke.json' if weather_deferred else ('network-pie-smoke.json' if network else 'pie-smoke.json')
play=unreal.load_object(None,'/Script/UnrealEd.Default__LevelEditorPlaySettings')
performance=unreal.load_object(None,'/Script/UnrealEd.Default__EditorPerformanceSettings')
assert play and performance
# These editor settings classes are reflected but not Python wrapper classes.
# Use the same native property contract as ObjectTools instead of changing ini.
before=unreal.ToolsetLibrary.get_object_properties(play,['PlayNetMode','PlayNumberOfClients','RunUnderOneProcess','bLaunchSeparateServer'])
throttle=unreal.ToolsetLibrary.get_object_properties(performance,['bThrottleCPUWhenNotForeground'])
restored=False

def restore_play_settings():
    global restored
    if restored:return
    assert unreal.ToolsetLibrary.set_object_properties(play,before)
    assert unreal.ToolsetLibrary.set_object_properties(performance,throttle)
    restored=True

try:
    assert unreal.ToolsetLibrary.set_object_properties(play,json.dumps(dict(PlayNetMode='PIE_Client' if network else 'PIE_Standalone',PlayNumberOfClients=1,RunUnderOneProcess=True,bLaunchSeparateServer=False)))
    assert unreal.ToolsetLibrary.set_object_properties(performance,json.dumps(dict(bThrottleCPUWhenNotForeground=False)))
except Exception:
    restore_play_settings()
    raise

def smoke(dt):
    global phase,began,changed,clear_lux,rain_lux
    worlds=unreal.EditorLevelLibrary.get_pie_worlds(True)
    if not worlds:
        if began is not None:
            (out/report_name).write_text(json.dumps(dict(passed=False,cancelled=True,phase=phase)),encoding='utf-8')
            unreal.unregister_slate_post_tick_callback(smoke_handle)
            restore_play_settings()
        return
    game=next((w for w in worlds if unreal.GameplayStatics.get_game_mode(w)),None)
    if game is None:return
    now=time.monotonic()
    if began is None:began=now
    pawn=None
    try:
        if now-began>90:raise RuntimeError('Starter PIE readiness/weather timeout')
        pawn=unreal.GameplayStatics.get_player_character(game,0)
        if pawn is None or now-began<5:return
        if network and len(worlds)<2:return
        presentation=next((w for w in worlds if w!=game),game)
        states=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Project_JWorldState)
        environments=unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Project_JWorldEnvironment)
        assert len(states)==len(environments)==1
        state=states[0]
        presented_states=unreal.GameplayStatics.get_all_actors_of_class(presentation,unreal.Project_JWorldState)
        presented_environments=unreal.GameplayStatics.get_all_actors_of_class(presentation,unreal.Project_JWorldEnvironment)
        assert len(presented_states)==len(presented_environments)==1
        presentation_state=presented_states[0];environment=presented_environments[0]
        assert environment.get_editor_property('state')==presentation_state
        light=environment.get_editor_property('sun').get_component_by_class(unreal.DirectionalLightComponent)
        definition=state.get_editor_property('definition');assert definition
        controller=unreal.GameplayStatics.get_player_controller(presentation,0)
        preparation=controller.get_component_by_class(unreal.Project_JWorldStreamingComponent);assert preparation
        assert preparation.is_streaming_source_enabled()
        for key in ['prediction_seconds','preparation_radius','max_prediction_distance']:
            assert preparation.get_editor_property(key)==definition.get_editor_property(key),key
        if phase==0:
            assert pawn.has_authority(),'Use one local authority for this smoke test'
            assert len(unreal.GameplayStatics.get_all_actors_of_class(game,unreal.PlayerStart))==1,'Duplicate runtime PlayerStart'
            assert pawn.get_actor_location().z>-1000,'Pawn fell through startup terrain'
            location=pawn.get_actor_location()
            ignore=[a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor) if not isinstance(a,unreal.LandscapeProxy)]
            hit=unreal.SystemLibrary.line_trace_single(game,location+unreal.Vector(0,0,300),location-unreal.Vector(0,0,500),unreal.TraceTypeQuery.TRACE_TYPE_QUERY1,True,ignore,unreal.DrawDebugTrace.NONE,True)
            values=hit.to_tuple() if hit else None
            assert values and values[0],'No loaded Landscape collision beneath PlayerStart pawn'
            assert 0<location.z-values[4].z<250,'Pawn is not supported near the terrain'
            if network:
                client_pawn=unreal.GameplayStatics.get_player_character(presentation,0);assert client_pawn
                assert not client_pawn.has_authority()
                assert (client_pawn.get_actor_location()-location).length()<150,'Server/client spawn mismatch'
            clear_lux=light.get_editor_property('intensity');assert clear_lux>10000,clear_lux
            if weather_deferred:
                if now-began<10:return
                authored=[a for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.Actor) if 'ProjectJ.Starter.Corridor.v1' in [str(t) for t in a.get_editor_property('tags')]]
                labels={a.get_actor_label() for a in authored}
                house_names=['Inn','Smith','CottageWest','Barn','CottageSouth','Granary','CottageNorth','Weaver']
                assert all('S00_'+name in labels for name in house_names),'Missing streamed starting buildings'
                groups=[c for a in authored for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)]
                assert groups and sum(c.get_instance_count() for c in groups)>1000,'Missing runtime foliage instances'
                report=dict(passed=True,pawn_spawned=True,pawn_position_cm=list(location.to_tuple()),player_start_count=1,landscape_support=True,starting_buildings_loaded=8,runtime_hism_groups=len(groups),runtime_instances=sum(c.get_instance_count() for c in groups),one_world_state=True,one_environment=True,preparation_uses_world_definition=True,weather_changed=False,performance_measured=False,scope='Standalone S00 spawn and saved corridor rendering/physics only; no remote network, HLOD, navigation or high speed acceptance')
                (out/report_name).write_text(json.dumps(report,indent=2),encoding='utf-8')
                unreal.log('STARTER_CORRIDOR_PIE_SMOKE_PASS '+json.dumps(report))
                unreal.unregister_slate_post_tick_callback(smoke_handle);restore_play_settings();return
            assert state.set_rain(1,.5)
            changed=now;phase=1;return
        if now-changed<1.5:return
        if phase==1:
            assert state.get_rain()>.99
            assert presentation_state.get_rain()>.99
            rain_lux=light.get_editor_property('intensity')
            assert math.isclose(rain_lux,clear_lux*.25,rel_tol=.02),(clear_lux,rain_lux)
            assert state.set_rain(0,.5)
            changed=now;phase=2;return
        assert state.get_rain()<.01
        assert presentation_state.get_rain()<.01
        assert math.isclose(light.get_editor_property('intensity'),clear_lux,rel_tol=.02)
        report=dict(passed=True,one_world_state=True,one_environment=True,player_start_count=1,pawn_spawned=True,clear_sun_lux=clear_lux,rain_sun_lux=rain_lux,rain_transition_and_recovery=True,preparation_uses_world_definition=True,pawn_position_cm=list(pawn.get_actor_location().to_tuple()),scope='Single local authority PIE at PlayerStart; no remote multiplayer, flight/pop-in, navigation, or performance acceptance')
        if network:report.update(scope='One in-process dedicated server and one client PIE; no separate machine, flight/pop-in, navigation, or performance acceptance',server_and_client_spawn_agree=True,replicated_rain_consumed_by_client=True)
        (out/report_name).write_text(json.dumps(report,indent=2),encoding='utf-8')
        unreal.log('STARTER_WORLD_PIE_SMOKE_PASS '+json.dumps(report))
        unreal.unregister_slate_post_tick_callback(smoke_handle)
        restore_play_settings()
    except Exception:
        diagnostics=dict(passed=False,error=traceback.format_exc(),world=game.get_path_name(),phase=phase)
        if pawn:
            diagnostics['pawn_position_cm']=list(pawn.get_actor_location().to_tuple())
            diagnostics['player_starts_cm']=[list(a.get_actor_location().to_tuple()) for a in unreal.GameplayStatics.get_all_actors_of_class(game,unreal.PlayerStart)]
            diagnostics['loaded_landscape_proxies']=len(unreal.GameplayStatics.get_all_actors_of_class(game,unreal.LandscapeProxy))
        diagnostics['worlds']=[dict(world=w.get_path_name(),authoritative=bool(unreal.GameplayStatics.get_game_mode(w)),pawn_position_cm=list(unreal.GameplayStatics.get_player_character(w,0).get_actor_location().to_tuple()) if unreal.GameplayStatics.get_player_character(w,0) else None,landscape_proxies=len(unreal.GameplayStatics.get_all_actors_of_class(w,unreal.LandscapeProxy))) for w in worlds]
        (out/report_name).write_text(json.dumps(diagnostics,indent=2),encoding='utf-8')
        unreal.log_error('STARTER_WORLD_PIE_SMOKE_FAIL '+traceback.format_exc())
        unreal.unregister_slate_post_tick_callback(smoke_handle)
        restore_play_settings()

smoke_handle=unreal.register_slate_post_tick_callback(smoke)
