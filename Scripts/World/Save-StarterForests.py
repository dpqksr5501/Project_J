"""Explicit PCG forest batch validation/save; no generation or height changes."""
import array,json,math,time,runpy
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';BASE='/Game/World/MainWorld'
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith('/Game/World/MainWorld/Legacy/Maps/L_StarterRegion.')
SAVE=runpy.run_path(str(Path(__file__).with_name('Starter-OwnedSave.py')))
save_scope=SAVE['OwnedSaveScope']()
graph=unreal.load_asset(BASE+'/PCG/PCG_MainWorldForest');assert graph
streaming=json.loads((ROOT/'Content/World/MainWorld/Source/MainWorld.forest-streaming.json').read_text())
forest_layer=unreal.load_asset(streaming['forest_hlod_layer']) if streaming.get('forest_hlod_layer') else None
raw=array.array('H')
with (ROOT/'Content/World/MainWorld/Source/HM_MainWorld.r16').open('rb') as stream:raw.fromfile(stream,4033**2)


def height(x,y):
    u=max(0,min(4031.99999,x/2));v=max(0,min(4031.99999,y/2));i,j=int(u),int(v);s,t=u-i,v-j
    value=(raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t
    return (value-32768)*6.25


places=json.loads((ROOT/'Content/World/MainWorld/Source/MainWorld.plan.json').read_text(encoding='utf-8'))['places']
settlements=[p for p in places if p['id'].startswith('S')]
records=[];errors=[];minimum_margin=1e9
for actor in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors():
    if not isinstance(actor,unreal.PCGVolume) or not actor.get_actor_label().startswith('PCG_Forest_'):continue
    save_scope.own(actor)
    component=actor.get_component_by_class(unreal.PCGComponent)
    assert component and component.get_graph()==graph
    if forest_layer:
        actor.modify();actor.set_editor_property('hlod_layer',forest_layer)
        component.modify();component.set_editor_property('regenerate_in_editor',False)
    if 'ProjectJ.Starter.WorldForest.v1' not in [str(t) for t in actor.tags]:
        actor.modify();actor.set_folder_path('StarterRegion/Biomes/Forests');actor.set_editor_property('tags',[unreal.Name('ProjectJ.Starter.WorldForest.v1')])
    if component.get_editor_property('generation_trigger')!=unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND:
        component.modify();component.set_editor_property('generation_trigger',unreal.PCGComponentGenerationTrigger.GENERATE_ON_DEMAND)
    assert actor.get_editor_property('is_spatially_loaded') and not actor.get_editor_property('is_editor_only_actor')
    groups=actor.get_components_by_class(unreal.InstancedStaticMeshComponent);trees=shrubs=0
    for comp in groups:
        n=comp.get_instance_count();is_tree=comp.static_mesh and comp.static_mesh.get_name().startswith('SM_BlackAlder')
        if forest_layer:comp.modify();comp.set_editor_property('enable_auto_lod_generation',bool(is_tree))
        if is_tree:
            comp.modify();comp.set_cull_distances(*streaming.get('tree_instance_cull_cm',[0,0]));trees+=n
        else:shrubs+=n
        if is_tree:
            for i in range(n):
                p=comp.get_instance_transform(i,True).translation
                margin=min(math.hypot(p.x/100+4032-s['center_m'][0],p.y/100+4032-s['center_m'][1])-s['radius_m'] for s in settlements)
                minimum_margin=min(minimum_margin,margin)
        for i in sorted(set([0,n//2,n-1])) if n else []:
            t=comp.get_instance_transform(i,True);p=t.translation;r=t.rotation.rotator()
            assert abs(r.pitch)<.01 and abs(r.roll)<.01
            expected=height(p.x/100+4032,p.y/100+4032)-(8 if is_tree else 4)
            errors.append(abs(p.z-expected))
    records.append(dict(label=actor.get_actor_label(),path=actor.get_path_name(),trees=trees,shrubs=shrubs))
assert records
assert max(errors,default=0)<30,('PCG projection differs from saved height',max(errors))
assert minimum_margin>-12,('Forest entered reserved settlement core',minimum_margin)
dirty=list(SAVE['dirty_packages']().values())
saved_packages=save_scope.save()
report=dict(saved_at=time.time(),tiles=records,total_trees=sum(r['trees'] for r in records),total_shrubs=sum(r['shrubs'] for r in records),
            maximum_height_sample_error_cm=max(errors,default=0),minimum_settlement_core_margin_m=minimum_margin,runtime_generation=False,height_modified=False,
            saved_packages=saved_packages,unrelated_dirty_preserved=[])
(OUT/'forest-save.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
