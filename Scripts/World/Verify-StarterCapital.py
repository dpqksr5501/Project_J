"""Read saved capital additions after reload; never repairs or generates."""
import hashlib,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
PLAN=json.loads((ROOT/'Content/World/Starter/Source/StarterWorld.capital.json').read_text(encoding='utf-8'))
records=[]
for name in ['capital-build.json','capital-details.json']:
    state=json.loads((OUT/name).read_text(encoding='utf-8'));assert state['phase']=='COMPLETE';records+=state['actors']
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith('/Game/World/Starter/Maps/L_StarterRegion.')
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<10:return
    unreal.unregister_slate_post_tick_callback(handle)
    report=dict(passed=False,read_only=True,authoring_executed=False)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        tags={'ProjectJ.Starter.Capital.v1','ProjectJ.Starter.CapitalDetails.v1'}
        owned=[a for a in actors if tags.intersection(str(t) for t in a.tags)]
        bylabel={a.get_actor_label():a for a in owned}
        assert len(owned)==len(bylabel)==len(records)==86
        error=0;canvas=0
        for r in records:
            a=bylabel[r['label']];assert a.get_path_name()==r['path']
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor')
            error=max(error,max(abs(x-y) for x,y in zip(a.get_actor_location().to_tuple(),r['location_cm'])))
            root=a.static_mesh_component;assert root.static_mesh.get_path_name().split('.')[0]==r['mesh']
            assert root.get_collision_enabled()==unreal.CollisionEnabled.QUERY_AND_PHYSICS
            for c in a.get_components_by_class(unreal.StaticMeshComponent):
                assert all(c.get_material(i) for i in range(c.get_num_materials()))
                if c.get_name()=='Canvas':
                    canvas+=1;assert c.get_editor_property('relative_location').length()<.01
        assert error<.01 and canvas==6,(error,canvas)
        height=hashlib.sha256((ROOT/'Content/World/Starter/Source/HM_StarterRegion.r16').read_bytes()).hexdigest()
        assert height==PLAN['height_sha256']
        original_plot_count=json.loads((OUT/'capital-build.json').read_text())['plots']
        report.update(passed=True,actors=len(owned),plots=original_plot_count,current_source_plots=len(PLAN['plots']),scope='Original capital tags only; later infill requires its own verification',canvas_components=canvas,maximum_saved_position_error_cm=error,
                      height_sha256=height,navigation_verified=False,art_complete=False,
                      dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:report['error']=traceback.format_exc()
    (OUT/'capital-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('STARTER_CAPITAL_SAVED_VERIFICATION '+str(report['passed']))
handle=unreal.register_slate_post_tick_callback(verify)
