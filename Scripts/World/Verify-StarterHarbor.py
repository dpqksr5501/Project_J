"""Independent saved harbor support/ramp/deck/access checks, not an art gate."""
import importlib.util,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';STATE=json.loads((OUT/'harbor-build.json').read_text(encoding='utf-8'));assert STATE['phase']=='COMPLETE'
assert not unreal.EditorLevelLibrary.get_pie_worlds(False)
spec=importlib.util.spec_from_file_location('terrain',ROOT/'Scripts/World/Starter-TerrainProfile.py');m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);T=m.TerrainProfile(SOURCE/'StarterRegion.manifest.json');origin=T.grid['unreal_origin_cm'];units=T.grid['centimetres_per_source_metre']
def point(x,y,z):return unreal.Vector(origin[0]+x*units,origin[1]+y*units,origin[2]+z*units)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs]);started=time.perf_counter()
def verify(dt):
    if time.perf_counter()-started<8:return
    unreal.unregister_slate_post_tick_callback(handle);report=dict(passed=False,read_only=True)
    try:
        actors=unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors();world=unreal.EditorLevelLibrary.get_editor_world();owned=[a for a in actors if 'ProjectJ.Starter.V4.Harbor.v1' in [str(t) for t in a.tags]];bylabel={a.get_actor_label():a for a in owned};assert len(owned)==len(bylabel)==len(STATE['actors'])+len(STATE['entries'])==20
        for row in STATE['actors']+STATE['entries']:
            a=bylabel[row['label']];assert a.get_path_name()==row['path'] and (a.get_actor_location()-unreal.Vector(*row['location_cm'])).length()<.01
            assert a.get_editor_property('is_spatially_loaded') and not a.get_editor_property('is_editor_only_actor') and {'ProjectJ.Location.S06','ProjectJ.Target.S05'}<={str(t) for t in a.tags}
        def trace(a,p,half=10):
            hit=unreal.SystemLibrary.line_trace_single(world,p+unreal.Vector(0,0,half),p-unreal.Vector(0,0,half),unreal.TraceTypeQuery.ECC_VISIBILITY,True,[v for v in actors if v!=a],unreal.DrawDebugTrace.NONE,True);values=hit.to_tuple() if hit else None;assert values and values[0],(a.get_actor_label(),p);return values[4].z
        foundation=[];steps=[];access={h['label']:h for h in STATE['access']};assert not STATE['pending_access']
        for h in STATE['houses']:
            a=bylabel[h['label']];p=point(*h['center_m']);foundation.append(abs(trace(a,p)-p.z));c=next(c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent) if c.get_name()=='EntryStairs');assert c.get_instance_count()==access[h['label']]['instances'];b=c.static_mesh.get_bounds()
            for i in range(c.get_instance_count()):
                p=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y,b.origin.z+b.box_extent.z));steps.append(abs(trace(a,p,5)-p.z))
        tower=STATE['watchtower'];p=point(*tower['center_m']);foundation.append(abs(trace(bylabel[tower['foundation_label']],p)-p.z))
        q=STATE['quay'];a=bylabel[q['label']];parts={c.get_name():c for c in a.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
        for name,n in q['components'].items():assert parts[name].get_instance_count()==n
        clearance=q.get('pile_access_clearance');assert clearance and not clearance['deck_moved']
        for change in clearance['changes']:
            pile=parts['Piles'];bounds=pile.static_mesh.get_bounds();top=pile.get_instance_transform(change['index'],True).transform_location(unreal.Vector(bounds.origin.x,bounds.origin.y,bounds.origin.z+bounds.box_extent.z));assert abs(top.z-(origin[2]+change['new_top_m']*units))<.01 and top.z<(origin[2]+q['deck_top_m']*units)
        deck=[];ramp=[];joints=[];b=parts['Deck'].static_mesh.get_bounds();c=parts['Deck'];n=c.get_instance_count();start,end=q['bank_point_m'],q['ramp_attach_m']
        for i in [1,n//4,n//2,n*3//4,n-2]:
            for v in [-30,0,30]:
                p=c.get_instance_transform(i,True).transform_location(unreal.Vector(b.origin.x,b.origin.y+v,b.origin.z+b.box_extent.z))
                # At the attachment the sloped ramp legitimately overlaps the deck.
                # Compare the upper authored support plane rather than the lower plank.
                px,py=(p.x-origin[0])/units,(p.y-origin[1])/units;dx,dy=end[0]-start[0],end[1]-start[1];t=((px-start[0])*dx+(py-start[1])*dy)/(dx*dx+dy*dy);expected=p.z
                if 0<=t<=1 and ((px-start[0]-t*dx)**2+(py-start[1]-t*dy)**2)**.5<2.5:
                    expected=max(expected,origin[2]+(start[2]*(1-t)+end[2]*t)*units)
                hit_z=trace(a,unreal.Vector(p.x,p.y,expected));joints.append(hit_z-p.z)
                deck.append(abs(hit_z-expected))
        for i in range(1,10):
            t=i/10;p=point(*[start[k]*(1-t)+end[k]*t for k in range(3)]);ramp.append(abs(trace(a,p,30)-p.z))
        c=bylabel[STATE['cargo']['label']];parts={p.get_name():p for p in c.get_components_by_class(unreal.HierarchicalInstancedStaticMeshComponent)}
        for name,n in STATE['cargo']['components'].items():assert parts[name].get_instance_count()==n
        for row in STATE['entries']:assert not bylabel[row['label']].get_editor_property('replicates')
        assert max(foundation+steps+deck+ramp)<.1,(max(foundation),max(steps),max(deck),max(ramp))
        report.update(passed=True,actors=20,buildings=12,watchtowers=1,exterior_accesses=len(access),stairs_threshold_instances=len(steps),deck_collision_samples=len(deck),ramp_collision_samples=len(ramp),ramp_grade=q['ramp_grade'],entrance_pile_clearance=True,maximum_deck_joint_rise_cm=max(joints,default=0),maximum_collision_error_cm=max(foundation+steps+deck+ramp),height_sha256=T.manifest['terrain_profile']['sha256'],art_complete=False,navigation_verified=False,boat_navigation_verified=False,performance_measured=False,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
    except Exception:report['error']=traceback.format_exc();unreal.log_error(report['error'])
    (OUT/'harbor-saved-verification.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(verify)
