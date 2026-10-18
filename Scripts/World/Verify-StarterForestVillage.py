"""Independent read-only saved forestry settlement geometry/support checks."""
import array,hashlib,json,math,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';SOURCE=ROOT/'Content/World/Starter/Source'
STATE=json.loads((OUT/'forest-village-build.json').read_text(encoding='utf-8'));assert STATE['phase']=='COMPLETE'
ACCESS=json.loads((OUT/'forest-village-access.json').read_text(encoding='utf-8'));assert ACCESS['phase']=='COMPLETE'
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
raw=array.array('H');raw.frombytes((SOURCE/'HM_StarterRegion.r16').read_bytes())
def height(x,y):
    u,v=x/2,y/2;i,j=int(u),int(v);s,t=u-i,v-j
    return ((raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t-32768)*6.25
started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);report=dict(passed=False,read_only=True,authoring_executed=False)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world()
        owned=[a for a in actors if 'ProjectJ.Starter.V4.Forestry.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned}
        assert len(owned)==len(bylabel)==len(STATE['actors'])+len(STATE['entries'])==18
        for row in STATE['actors']+STATE['entries']:
            a=bylabel[row['label']];assert a.get_path_name()==row['path']
            assert (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor')
            assert {'ProjectJ.Location.S04','ProjectJ.Target.S03'}<={str(t) for t in a.tags}
        support=[];ground=[];counts={};stair_errors=[];stairs_bylabel={h['label']:h for h in ACCESS['houses']}
        for h in STATE['houses']:
            a=bylabel[h['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.StaticMeshComponent)}
            assert {'HipRoof','Foundation','GableNorth','GableSouth'}<=parts.keys()
            x,y,z=h['center_m'];p=unreal.Vector((x-4032)*100,(y-4032)*100,z*100)
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,10),p-unreal.Vector(0,0,10),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True)
            values=hit.to_tuple() if hit else None;assert values and values[0],h['label'];support.append(abs(values[4].z-z*100))
            stairs=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');access=stairs_bylabel[h['label']]
            assert stairs.get_instance_count()==access['instances'] and access['rise_m']<=.20
            bounds=stairs.static_mesh.get_bounds()
            for i in range(stairs.get_instance_count()):
                t=stairs.get_instance_transform(i,True);top=t.transform_location(unreal.Vector(bounds.origin.x,bounds.origin.y,bounds.origin.z+bounds.box_extent.z))
                hit=unreal.SystemLibrary.line_trace_single(world,top+unreal.Vector(0,0,5),top-unreal.Vector(0,0,5),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True)
                values=hit.to_tuple() if hit else None;assert values and values[0],(h['label'],i);stair_errors.append(abs(values[4].z-top.z))
        for group in STATE['groups']:
            components={c.get_name():c for c in bylabel[group['label']].get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
            for row in group['sets']:
                c=components[row['component']];n=c.get_instance_count();assert n==row['instances'] and c.static_mesh.get_path_name().split('.')[0]==row['mesh']
                counts[group['label']+'/'+row['component']]=n;b=c.static_mesh.get_bounds()
                for index in range(c.get_num_materials()):
                    mat=c.get_material(index);assert not mat or mat.get_base_material().get_editor_property('used_with_instanced_static_meshes')
                for i in sorted({0,n//2,n-1}) if n else []:
                    t=c.get_instance_transform(i,True);foot=t.transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z-b.box_extent.z))
                    ground.append(abs(foot.z-(height(foot.x/100+4032,foot.y/100+4032)-3.5)))
        for row in STATE['entries']:
            a=bylabel[row['label']];assert not a.get_editor_property('replicates') and isinstance(a,unreal.TargetPoint)
        assert max(support)<.1 and max(ground)<.1 and max(stair_errors)<.1
        digest=hashlib.sha256((SOURCE/'HM_StarterRegion.r16').read_bytes()).hexdigest();assert digest==STATE['height_sha256']
        report.update(passed=True,actors=len(owned),buildings=len(STATE['houses']),entries=len(STATE['entries']),instance_counts=counts,exterior_accesses=len(stairs_bylabel),stairs_threshold_instances=len(stair_errors),maximum_stair_top_collision_error_cm=max(stair_errors),maximum_foundation_collision_error_cm=max(support),maximum_grounding_error_cm=max(ground),height_sha256=digest,art_complete=False,navigation_verified=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
    (OUT/'forest-village-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
