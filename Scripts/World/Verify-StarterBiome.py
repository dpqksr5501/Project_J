"""Read-only persisted biome checks after independent map reload; no repair."""
import collections,hashlib,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
BASE='/Game/World/MainWorld';TAG='ProjectJ.Starter.Biome.v1'
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
assert unreal.EditorLevelLibrary.get_editor_world().get_path_name().startswith('/Game/World/MainWorld/Legacy/Maps/L_StarterRegion.')
subsystem=unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
started=time.perf_counter()


def verify(dt):
    if time.perf_counter()-started<12:return
    unreal.unregister_slate_post_tick_callback(handle)
    report=dict(passed=False,read_only=True,after_independent_reload=True)
    try:
        actors=subsystem.get_all_level_actors();owned=[a for a in actors if TAG in [str(t) for t in a.tags]]
        counts=collections.Counter(a.get_actor_label() for a in owned);assert all(v==1 for v in counts.values())
        groups=[c for a in owned for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)]
        total=sum(c.get_instance_count() for c in groups);assert total==700
        assert all(a.get_editor_property('is_spatially_loaded') for a in owned)
        target='/Game/Map_Resource/MainWorld/Materials/M_MainWorldTerrain_Biome.M_MainWorldTerrain_Biome'
        landscapes=[a for a in actors if isinstance(a,unreal.LandscapeProxy)]
        assert len(landscapes)==257
        assert all(a.get_editor_property('landscape_material').get_path_name()==target for a in landscapes)
        mat=unreal.load_asset(target);expressions=list(unreal.MaterialEditingLibrary.get_material_expressions(mat))
        outputs=[e for e in expressions if isinstance(e,unreal.MaterialExpressionLandscapeGrassOutput)];assert len(outputs)==1
        grass=unreal.load_asset('/Game/Map_Resource/MainWorld/Materials/LG_MainWorldUnderstory');varieties=list(grass.get_editor_property('grass_varieties'))
        preset=json.loads((SOURCE/'MainWorld.biomes.json').read_text(encoding='utf-8'))
        assert len(varieties)==(len(preset['groundcover_palettes']['Meadow']) if preset['schema']>=2 else 2)
        expected={}
        data=json.loads((OUT/'biome-placement.json').read_text(encoding='utf-8'))
        for p in data['points']:expected[(round(p['x_m']),round(p['y_m']))]=p
        errors=[];upright=0
        for comp in groups:
            assert comp.get_collision_enabled()==unreal.CollisionEnabled.NO_COLLISION
            mesh=comp.static_mesh;b=mesh.get_bounds()
            for i in range(comp.get_instance_count()):
                t=comp.get_instance_transform(i,True);p=t.transform_location(b.origin)
                x,y=p.x/100+4032,p.y/100+4032
                assert (round(x),round(y)) in expected,dict(actor=comp.get_owner().get_actor_label(),instance=i,actual_xy=[x,y],translation=list(t.translation.to_tuple()),bounds_origin=list(b.origin.to_tuple()),scale=list(t.scale3d.to_tuple()))
                row=expected[(round(x),round(y))]
                bottom=t.translation.z+(b.origin.z-b.box_extent.z)*t.scale3d.z
                errors.append(abs(bottom-(row['z_m']*100-4)))
                r=t.rotation.rotator();assert abs(r.pitch)<.001 and abs(r.roll)<.001;upright+=1
        assert max(errors)<.05
        fingerprint=hashlib.sha256((SOURCE/'HM_MainWorld.r16').read_bytes()).hexdigest()
        assert fingerprint==data['height_sha256']
        old=[a for a in actors if 'ProjectJ.Starter.Corridor.v1' in [str(t) for t in a.tags]];assert len(old)==390-preset.get('retired_legacy_grass_actors',0)
        stats=unreal.MaterialEditingLibrary.get_statistics(mat)
        report.update(passed=True,shrubs=total,spatial_groups=len(groups),upright_instances=upright,maximum_pivot_ground_error_cm=max(errors),
                      original_corridor_actors=len(old),landscape_proxies=len(landscapes),height_sha256=fingerprint,
                      material_expressions=len(expressions),landscape_grass_outputs=len(outputs),grass_varieties=len(varieties),
                      material_statistics={k:stats.get_editor_property(k) for k in ['num_pixel_shader_instructions','num_samplers','num_pixel_texture_samples']},
                      dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()],
                      runtime_grass_instances_verified=False,pcg_graph_implemented=False,performance_approved=False)
    except Exception:report['error']=traceback.format_exc()
    (OUT/'biome-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('STARTER_BIOME_VERIFICATION '+str(report['passed']))


handle=unreal.register_slate_post_tick_callback(verify)
