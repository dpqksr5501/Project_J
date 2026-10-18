"""Read-only saved farm geometry/support/instance checks after map reload."""
import array,hashlib,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2'
STATE=json.loads((OUT/'farm-village-build.json').read_text(encoding='utf-8'));assert STATE['phase']=='COMPLETE'
ENTRY=json.loads((OUT/'farm-entry-registration.json').read_text(encoding='utf-8'))
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs
unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
raw=array.array('H')
with (ROOT/'Content/World/Starter/Source/HM_StarterRegion.r16').open('rb') as f:raw.fromfile(f,4033**2)
def height(x,y):
    u,v=x/2,y/2;i,j=int(u),int(v);s,t=u-i,v-j
    return ((raw[j*4033+i]*(1-s)+raw[j*4033+i+1]*s)*(1-t)+(raw[(j+1)*4033+i]*(1-s)+raw[(j+1)*4033+i+1]*s)*t-32768)*6.25
started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<10:return
    unreal.unregister_slate_post_tick_callback(handle);report=dict(passed=False,read_only=True,authoring_executed=False)
    try:
        world=unreal.EditorLevelLibrary.get_editor_world();actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()
        owned=[a for a in actors if 'ProjectJ.Starter.V4.Farm.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned}
        assert len(owned)==len(bylabel)==len(STATE['actors'])==17
        errors=[];support=[]
        for r in STATE['actors']:
            a=bylabel[r['label']];assert a.get_path_name()==r['path']
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor')
            assert 'ProjectJ.Location.S02' in [str(t) for t in a.tags]
            assert (a.get_actor_location()-unreal.Vector(*r['location_cm'])).length()<.01
        for h in STATE['houses']:
            a=bylabel[h['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.StaticMeshComponent)}
            assert parts['HipRoof'].static_mesh and {'GableNorth','GableSouth','Foundation'}<=parts.keys()
            x,y,z=h['center_m'];p=unreal.Vector((x-4032)*100,(y-4032)*100,z*100)
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,10),p-unreal.Vector(0,0,10),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True)
            values=hit.to_tuple() if hit else None;assert values and values[0],h['label']
            support.append(abs(values[4].z-z*100))
        fields=[]
        for field in STATE['fields']:
            comp=bylabel[field['label']].get_component_by_class(unreal.HierarchicalInstancedStaticMeshComponent)
            assert comp.get_instance_count()==field['instances'];fields.append(comp.get_instance_count())
            b=comp.static_mesh.get_bounds()
            for i in [0,comp.get_instance_count()//2,comp.get_instance_count()-1]:
                t=comp.get_instance_transform(i,True)
                foot=t.transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z-b.box_extent.z))
                errors.append(abs(foot.z-(height(foot.x/100+4032,foot.y/100+4032)-3.5)))
        rotor=next(c for c in bylabel['S02_Farm_Mill'].get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='SlattedMillRotor')
        assert rotor.get_instance_count()==92 and rotor.get_material(0).get_base_material().get_editor_property('used_with_instanced_static_meshes')
        assert len(bylabel['S02_Farm_Well'].get_components_by_class(unreal.StaticMeshComponent))==4
        entry=next(a for a in actors if a.get_path_name()==ENTRY['path'])
        assert (entry.get_actor_location()-unreal.Vector(*ENTRY['location_cm'])).length()<.01
        assert entry.get_editor_property('is_spatially_loaded') and not entry.get_editor_property('is_editor_only_actor')
        assert not entry.get_editor_property('replicates') and 'ProjectJ.Entry.S02.Main' in [str(t) for t in entry.tags]
        assert max(support)<.1 and max(errors)<.1,(max(support),max(errors))
        digest=hashlib.sha256((ROOT/'Content/World/Starter/Source/HM_StarterRegion.r16').read_bytes()).hexdigest();assert digest==STATE['height_sha256']
        report.update(passed=True,actors=17,buildings=13,crop_fields=3,wheat_instances=sum(fields),mill_rotor_instances=92,
                      maximum_foundation_top_collision_error_cm=max(support),maximum_crop_grounding_error_cm=max(errors),height_sha256=digest,
                      entry_actor=entry.get_path_name(),entry_registered=True,
                      navigation_verified=False,walking_verified=False,art_complete=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
    (OUT/'farm-village-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    unreal.log('STARTER_FARM_SAVED_VERIFICATION '+str(report['passed']))
handle=unreal.register_slate_post_tick_callback(verify)
