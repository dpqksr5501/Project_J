"""Small native sensor for fixed-camera visual iterations, never authors assets."""
import collections,hashlib,json,time
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];LABEL=globals().get('LABEL');assert LABEL and LABEL.replace('-','').replace('_','').isalnum()
out=ROOT/'Saved/Validation/StarterWorld/VisualHarness'/LABEL;out.mkdir(parents=True,exist_ok=True)
world=unreal.EditorLevelLibrary.get_editor_world();actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();keys=['sg.FoliageQuality','sg.ViewDistanceQuality','grass.DensityScale','foliage.DensityScale','r.ViewDistanceScale','grass.CullDistanceScale','foliage.CullDistanceScale','r.Streaming.PoolSize','r.ScreenPercentage']
r=dict(timestamp=time.time(),world=world.get_path_name(),pie=bool(unreal.EditorLevelLibrary.get_pie_worlds(False)),actor_count=len(actors),quality={k:unreal.SystemLibrary.get_console_variable_float_value(k) for k in keys},dirty_map=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],dirty_content=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()],height_sha256=hashlib.sha256((ROOT/'Content/World/MainWorld/Source/HM_MainWorld.r16').read_bytes()).hexdigest(),forest_groups=[],nature_counts={},hlod_groups=[])
for a in actors:
    if isinstance(a,unreal.WorldPartitionHLOD):
        r['hlod_groups'].append(dict(label=a.get_actor_label(),editor_hidden=a.is_temporarily_hidden_in_editor(),ism_instances=sum(c.get_instance_count() for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent))))
    elif a.get_actor_label().startswith('PCG_Forest_'):
        for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if c.static_mesh and 'BlackAlder' in c.static_mesh.get_name():r['forest_groups'].append(dict(path=c.get_path_name(),count=c.get_instance_count(),start=c.get_editor_property('instance_start_cull_distance'),end=c.get_editor_property('instance_end_cull_distance')))
    elif 'ProjectJ.Starter.NaturalDiversity.v1' in [str(t) for t in a.tags]:
        for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
            if c.static_mesh:r['nature_counts'][c.static_mesh.get_path_name()]=r['nature_counts'].get(c.static_mesh.get_path_name(),0)+c.get_instance_count()
r['saved_pcg_trees']=sum(q['count'] for q in r['forest_groups']);(out/'native.json').write_text(json.dumps(r,indent=2),encoding='utf-8')
