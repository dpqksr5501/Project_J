"""Read saved spatial forest output after reload, without generating or saving."""
import hashlib,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';BASE='/Game/World/MainWorld'
EXPECTED=json.loads((OUT/'forest-save.json').read_text(encoding='utf-8'))
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith('/Game/World/MainWorld/Legacy/Maps/L_StarterRegion.')
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
started=time.perf_counter()


def verify(dt):
    if time.perf_counter()-started<10:return
    unreal.unregister_slate_post_tick_callback(handle)
    report=dict(passed=False,generated=False,read_only=True,after_independent_reload=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        volumes=[a for a in actors if isinstance(a,unreal.PCGVolume) and 'ProjectJ.Starter.WorldForest.v1' in [str(t) for t in a.tags]]
        report.update(loaded_tiles=len(volumes),expected_tiles=len(EXPECTED['tiles']),
                      loaded_labels=sorted(a.get_actor_label() for a in volumes),
                      missing_labels=sorted({r['label'] for r in EXPECTED['tiles']}-{a.get_actor_label() for a in volumes}))
        rollout=json.loads((OUT/'forest-rollout.json').read_text(encoding='utf-8'))
        assert rollout['phase']=='COMPLETE'
        planned={t['label'] for t in rollout['tiles']}|{'PCG_Forest_12_9'}
        assert len(volumes)==len(EXPECTED['tiles'])==len(planned)
        assert {a.get_actor_label() for a in volumes}==planned
        expected={r['label']:r for r in EXPECTED['tiles']};trees=shrubs=0;profiles={}
        for a in volumes:
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor')
            c=a.get_component_by_class(unreal.PCGComponent)
            assert c.get_editor_property('generation_trigger')==unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND
            nt=ns=0
            for comp in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
                if comp.static_mesh.get_name().startswith('SM_BlackAlder'):
                    nt+=comp.get_instance_count();profiles[str(comp.get_collision_enabled())]=profiles.get(str(comp.get_collision_enabled()),0)+1
                else:ns+=comp.get_instance_count()
            assert (nt,ns)==(expected[a.get_actor_label()]['trees'],expected[a.get_actor_label()]['shrubs']),a.get_actor_label()
            trees+=nt;shrubs+=ns
        assert trees==EXPECTED['total_trees'] and shrubs==EXPECTED['total_shrubs']
        height=hashlib.sha256((ROOT/'Content/World/MainWorld/Source/HM_MainWorld.r16').read_bytes()).hexdigest()
        assert height=='bc3a7957a44629beb807d4a453443e9d02e48981be0cfb93d5054432c077f8c8'
        report.update(passed=True,tiles=len(volumes),trees=trees,shrubs=shrubs,tree_collision_modes=profiles,height_sha256=height,
                      dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],performance_measured=False)
    except Exception:report['error']=traceback.format_exc()
    (OUT/'forest-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('STARTER_FOREST_SAVED_VERIFICATION '+str(report['passed']))


handle=unreal.register_slate_post_tick_callback(verify)
