"""PIE-only proof of the current unbound boss owner's stable identity/lifecycle."""
import hashlib,json,time,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/MainWorld/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';target=globals().get('TARGET_ID','B01');FILE=OUT/(target+'-boss-runtime-verification.json');assert not FILE.exists();manifest=json.loads((SOURCE/'MainWorld.manifest.json').read_text());f=SOURCE/manifest['boss_profiles'];P=next(p for p in json.loads(f.read_text())['bosses'] if p['target_id']==target);assert P['encounter']['npc_class'] is None;started=time.perf_counter()
def tick(dt):
    if time.perf_counter()-started>120:
        unreal.unregister_slate_post_tick_callback(handle);FILE.write_text(json.dumps(dict(passed=False,error='No ready PIE game clock observed')),encoding='utf-8');return
    ready=[w for w in unreal.EditorLevelLibrary.get_pie_worlds(False) if unreal.GameplayStatics.get_time_seconds(w)>0 and unreal.GameplayStatics.get_player_pawn(w,0)]
    if not ready:return
    world=ready[0];pending=[a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Project_JWorldEncounter) if str(a.get_editor_property('encounter_id'))==P['encounter']['id']]
    # Actor construction and its first replicated phase may precede BeginPlay
    # completion. Observe the native phase transition, never force it or pass
    # a dormant owner. A missing transition still fails the bounded check.
    if not pending or (len(pending)==1 and 'DORMANT' in str(pending[0].get_editor_property('phase')).upper()):return
    unreal.unregister_slate_post_tick_callback(handle);R=dict(passed=False,target_id=target,gameplay_ready=False,read_only=True)
    try:
        world=ready[0];owners=[a for a in unreal.GameplayStatics.get_all_actors_of_class(world,unreal.Project_JWorldEncounter) if str(a.get_editor_property('encounter_id'))==P['encounter']['id']];assert len(owners)==1;a=owners[0];assert not a.get_editor_property('npc_class');phase=str(a.get_editor_property('phase'));assert 'DISABLED' in phase.upper();assert a.get_editor_property('replicates');R.update(passed=True,encounter_id=P['encounter']['id'],single_owner=True,phase=phase,npc_unbound=True,active_boss=False,runtime_actor_path=a.get_path_name(),profile_sha256=hashlib.sha256(f.read_bytes()).hexdigest(),scope='Current region-qualified native owner gracefully disables with no NPC class; no boss AI/skills/rewards/multiplayer approval')
    except Exception:R['error']=traceback.format_exc();unreal.log_error(R['error'])
    FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
handle=unreal.register_slate_post_tick_callback(tick)
