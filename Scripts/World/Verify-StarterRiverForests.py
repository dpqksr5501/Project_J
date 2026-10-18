"""Check preserved distant generated transforms, then save only owned forest data."""
import hashlib,json,runpy
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'river-forest-regeneration.json';r=json.loads(FILE.read_text(encoding='utf-8'));assert r['phase'] in ['GENERATED_NOT_SAVED','COMPLETE'] and not unreal.EditorLevelLibrary.get_pie_worlds(False)
descs=unreal.WorldPartitionBlueprintLibrary.get_actor_descs();descs=descs[-1] if isinstance(descs,tuple) else descs;unreal.WorldPartitionBlueprintLibrary.load_actors([d.get_editor_property('guid') for d in descs])
before=json.loads((OUT/'river-forest-before.json').read_text(encoding='utf-8'));actors={a.get_path_name():a for a in unreal.get_editor_subsystem(unreal.EditorActorSubsystem).get_all_level_actors()};records=[]
for row in before['tiles']:
    a=actors[row['path']];t=a.get_actor_transform();pose=[t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w,*t.translation.to_tuple(),*t.scale3d.to_tuple()];assert max(abs(a-b) for a,b in zip(pose,row['pose_numeric']))<.001;outside=[];inside=0
    for c in a.get_components_by_class(unreal.InstancedStaticMeshComponent):
        for i in range(c.get_instance_count()):
            t=c.get_instance_transform(i,True);v=t.translation;distance=((v.x/100+4032-4900)**2+(v.y/100+4032-2670)**2)**.5
            if distance<165:inside+=1
            if distance<290:continue
            outside.append([c.static_mesh.get_path_name()]+[round(v,3) for v in (*t.translation.to_tuple(),t.rotation.x,t.rotation.y,t.rotation.z,t.rotation.w,*t.scale3d.to_tuple())])
    outside.sort();digest=hashlib.sha256(json.dumps(outside,separators=(',',':')).encode()).hexdigest();assert digest==row['outside_sha256'],('Unexpected change outside S07 influence; inspect preserved snapshot',row['label']);assert inside==0,(row['label'],inside);records.append(dict(label=row['label'],outside_sha256=digest,inside_reserved_core=inside))
if r['phase']=='GENERATED_NOT_SAVED':runpy.run_path(str(ROOT/'Scripts/World/Save-StarterForests.py'))
else:r['reloaded_verified']=True
r.update(phase='COMPLETE',validation=records,outside_protection_radius_m=290,full_world_regenerated=False);FILE.write_text(json.dumps(r,indent=2),encoding='utf-8')
