"""Author a boss space and reuse the native server owner; never enable a dummy NPC."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';target=globals().get('TARGET_ID','B01');FILE=OUT/(target+'-boss-build.json');assert not FILE.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'MainWorld.manifest.json');C=module('contract','Starter-WorldDefinitionContract.py');P=next(p for p in json.loads((SOURCE/T.manifest['boss_profiles']).read_text())['bosses'] if p['target_id']==target);assert P['stage']=='PLANNED';tag='ProjectJ.Starter.V4.Boss.'+target+'.v'+str(P['generation_version']);A=module('authoring','Starter-SettlementAuthoring.py').SettlementAuthoring(tag,'Starter/V4/Bosses/'+target);actors=A.subsystem.get_all_level_actors();assert not any(tag in [str(t) for t in a.tags] for a in actors);assert not any(isinstance(a,unreal.Project_JWorldEncounter) and str(a.get_editor_property('encounter_id'))==P['encounter']['id'] for a in actors),'Reuse the existing encounter owner instead of creating another'
definition=next(a for a in actors if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');R=dict(phase='STARTED',target_id=target,owned_tag=tag,profile_sha256=hashlib.sha256((SOURCE/T.manifest['boss_profiles']).read_bytes()).hexdigest(),height_sha256=T.manifest['terrain_profile']['sha256'],source_plan_hash=definition.source_plan_hash,previous_places=[C.snapshot_place(v) for v in definition.places],previous_connections=[C.snapshot_connection(v) for v in definition.connections],props=[],bounds=[],markers=[],encounter=None,art_complete=False,active_npc=False,completed=[])
def record():FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
def label(a,id,role):
    a.modify();a.set_actor_label(id.replace('.','_'));a.set_folder_path(A.folder);a.tags=[unreal.Name(tag),unreal.Name('ProjectJ.Location.'+target),unreal.Name('Role.'+role)];return a
def marker(id,p,role):
    a=label(A.subsystem.spawn_actor_from_class(unreal.TargetPoint,A.point(*p)),id,role);a.set_editor_property('is_spatially_loaded',True);assert not a.get_editor_property('replicates');R['markers'].append(dict(id=id,path=a.get_path_name(),position_m=p,role=role))
def bounds(q,role):
    x,y,z=q['center_m'];a=label(A.subsystem.spawn_actor_from_class(unreal.TriggerBox,A.point(x,y,z+2)),q['id']+'.Bounds',role);a.set_editor_property('is_spatially_loaded',True);c=a.get_component_by_class(unreal.BoxComponent);c.modify();c.set_box_extent(unreal.Vector(q['radius_m']*100,q['radius_m']*100,600),False);c.set_collision_profile_name('NoCollision');c.set_editor_property('generate_overlap_events',False);R['bounds'].append(dict(id=q['id'],path=a.get_path_name(),center_m=q['center_m'],radius_m=q['radius_m'],scope='Authoring box; source circle is the encounter/staging footprint, not an active trigger'))
def prop(q):
    x,y=q['position_xy_m'];m=unreal.load_asset(q['mesh']);assert isinstance(m,unreal.StaticMesh);b=m.get_bounds();sx,sy,sz=q['scale'];radius=math.hypot(b.box_extent.x*sx,b.box_extent.y*sy)/100;assert radius<=q['conservative_radius_m'],('Update the planned prop clearance',q['id'],radius);half=max(b.box_extent.x*sx,b.box_extent.y*sy)/100;z=min(T.height(x+u,y+v) for u in [-half,0,half] for v in [-half,0,half]);a=A.spawn(q['id'].replace('.','_'),q['mesh'],x,y,z,q['yaw_degrees'],tuple(q['scale']),collision=q['collision']);label(a,q['id'],q['role']);R['props'].append(dict(q,path=a.get_path_name(),bottom_m=z))
def encounter():
    e=P['encounter'];assert e['npc_class'] is None;assert unreal.Project_JWorldEncounter.static_class().get_path_name()==e['owner_class'];a=label(A.subsystem.spawn_actor_from_class(unreal.Project_JWorldEncounter,A.point(*P['arena']['center_m'])),target+'.EncounterOwner','NativeServerEncounter');a.set_editor_property('encounter_id',unreal.Name(e['id']));a.set_editor_property('activation_radius',e['activation_radius_cm']);a.set_editor_property('respawn_seconds',e['respawn_seconds']);assert not a.get_editor_property('npc_class');assert a.get_editor_property('replicates') and not a.get_editor_property('is_spatially_loaded');R['encounter']=dict(path=a.get_path_name(),encounter_id=e['id'],npc_class=None,replicates=True,is_spatially_loaded=False,source_class=e['owner_class'],status='NPC_UNBOUND_RUNTIME_DISABLED_UNTIL_GAMEPLAY_INTEGRATION')
tasks=[('Arena',lambda:bounds(P['arena'],'BossArena.NotBound')),('Staging',lambda:bounds(P['safe_staging'],'SafeStaging.NotBound'))]+[(q['id'],lambda q=q:prop(q)) for q in P['props']]+[(q['id'],lambda q=q:marker(q['id'],q['position_m'],'Approach')) for q in P['entries']]+[(target+'.Staging%02d'%i,lambda i=i,p=p:marker(target+'.Staging%02d'%i,p,'PlayerAssembly.NotBound')) for i,p in enumerate(P['safe_staging']['points_m'],1)]+[('EncounterOwner',encounter)];index=0;started=False
def tick(dt):
    global index,started
    if started:return
    started=True
    try:
        if index==len(tasks):A.save();R.update(phase='COMPLETE',actors=A.created,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()]);record();unreal.unregister_slate_post_tick_callback(handle);return
        key,action=tasks[index];R['phase']='APPLYING_'+key;record();action();R['completed'].append(key);index+=1
        if index%3==0 or index==len(tasks):A.save()
        record()
    except Exception:R.update(phase='ERROR',error=traceback.format_exc());record();unreal.unregister_slate_post_tick_callback(handle);unreal.log_error(R['error'])
    finally:started=False
record();handle=unreal.register_slate_post_tick_callback(tick)
