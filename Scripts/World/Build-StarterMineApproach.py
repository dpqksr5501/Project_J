"""H08's authored footpath: static stone treads and scanned timber guardrails."""
import hashlib,importlib.util,json,math,traceback
from pathlib import Path
import unreal
ROOT=Path(__file__).resolve().parents[2];SOURCE=ROOT/'Content/World/Starter/Source';OUT=ROOT/'Saved/Validation/StarterWorld/Phase2';FILE=OUT/'H08-mine-approach-build.json';assert not FILE.exists()
def module(name,file):
    s=importlib.util.spec_from_file_location(name,ROOT/'Scripts/World'/file);m=importlib.util.module_from_spec(s);s.loader.exec_module(m);return m
P=json.loads((SOURCE/'StarterWorld.H08.approach.json').read_text());A=module('authoring','Starter-SettlementAuthoring.py').SettlementAuthoring(P['owner'],'Starter/V4/Hunting/H08/Approach');G=module('geometry','Build-StarterCorridor.py');C=module('contract','Starter-WorldDefinitionContract.py');T=module('terrain','Starter-TerrainProfile.py').TerrainProfile(SOURCE/'StarterRegion.manifest.json');assert not any(P['owner'] in [str(t) for t in a.tags] for a in A.subsystem.get_all_level_actors())
definition=next(a for a in A.subsystem.get_all_level_actors() if isinstance(a,unreal.Project_JWorldState)).get_editor_property('definition');R=dict(phase='STARTED',source_sha256=hashlib.sha256((SOURCE/'StarterWorld.H08.approach.json').read_bytes()).hexdigest(),previous_places=[C.snapshot_place(v) for v in definition.places],previous_connections=[C.snapshot_connection(v) for v in definition.connections],source_plan_hash=definition.source_plan_hash,art_complete=False,terrain_modified=False)
FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
try:
    x0,y0,z0=P['route_points_m'][0];a=A.spawn('H08_MineApproach','/Engine/BasicShapes/Cube',x0,y0,z0);a.static_mesh_component.set_static_mesh(None);a.set_actor_location(A.point(x0,y0,z0),False,False);A.created[-1].update(mesh=None,location_cm=list(a.get_actor_location().to_tuple()));a.tags=[unreal.Name(P['owner']),unreal.Name('ProjectJ.Location.H08')]
    cube=unreal.load_asset('/Engine/BasicShapes/Cube');stone=unreal.load_asset('/Game/World/Starter/Materials/M_StarterBridgeStone');assert stone
    c=G.component(a,unreal.HierarchicalInstancedStaticMeshComponent,'StoneTreads');c.set_static_mesh(cube);c.set_material(0,A.instanced_material(stone));c.set_collision_profile_name('BlockAll')
    for p in P['pieces']:
        x,y=p['center_xy_m'];depth=p['top_m']-p['bottom_m'];c.add_instance(G.centered_transform(cube,unreal.Vector((x-x0)*100,(y-y0)*100,(p['bottom_m']-z0)*100),p['yaw_degrees'],(p['length_m'],p['width_m'],depth)),False)
    timber=unreal.load_asset('/Game/ElderboomVillage/Meshes/SM_Worn_Wooden_Beam_tgmsdc0fa_lod3');assert timber;bounds=timber.get_bounds();frames=G.component(a,unreal.HierarchicalInstancedStaticMeshComponent,'TimberGuardrails');frames.set_static_mesh(timber);frames.set_collision_profile_name('BlockAll')
    for i,s in enumerate(timber.static_materials):frames.set_material(i,A.instanced_material(s.material_interface))
    beams=[]
    def beam(start,end):
        delta=[end[i]-start[i] for i in range(3)];length=math.sqrt(sum(v*v for v in delta));pitch=-math.degrees(math.acos(max(-1,min(1,delta[2]/length))));yaw=math.degrees(math.atan2(delta[1],delta[0]));center=[(start[i]+end[i])/2 for i in range(3)];rotation=unreal.Rotator(pitch=pitch,yaw=yaw,roll=0);transform=unreal.Transform(rotation=rotation,scale=unreal.Vector(.8,.8,length*100/(bounds.box_extent.z*2)));offset=transform.transform_location(bounds.origin);transform.translation=unreal.Vector((center[0]-x0)*100,(center[1]-y0)*100,(center[2]-z0)*100)-offset;frames.add_instance(transform,False);beams.append(dict(start_m=start,end_m=end))
    for b in P['guardrail_beams']:beam(b['start_m'],b['end_m'])
    A.save();R.update(phase='COMPLETE',actor_path=a.get_path_name(),actors=A.created,treads=c.get_instance_count(),guardrail_instances=frames.get_instance_count(),guardrail_beams=beams,dirty_maps=[p.get_name() for p in unreal.EditorLoadingAndSavingUtils.get_dirty_map_packages()])
except Exception:R.update(phase='FAILED',error=traceback.format_exc());unreal.log_error(R['error'])
FILE.write_text(json.dumps(R,indent=2),encoding='utf-8')
