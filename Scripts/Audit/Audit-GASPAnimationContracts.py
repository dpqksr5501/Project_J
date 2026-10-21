"""Read-only GASP clip contracts. Run in an editor; never saves or modifies assets.

Project: traces authored hard/soft locomotion roots. Reference: reads only the
explicit counterpart request written by the project pass. Exports one temporary
text asset at a time to hash full authored curve data, then removes that export.
Dependency closure includes authoring references; it does not prove selection.
"""
import hashlib,json,re,traceback
from pathlib import Path
import unreal
OUT=Path(__file__).resolve().parents[2]/'Saved/GASP58Upgrade_20261020/AnimationParity'
reference=unreal.Paths.get_project_file_path().endswith('GameAnimationSample.uproject')
label=globals().get('AUDIT_LABEL','Reference' if reference else 'Project')
receipt=OUT/(label+'.json')
assert not receipt.exists() and not receipt.with_name(receipt.name+'.gz').exists(), 'Preserve prior evidence; choose a new AUDIT_LABEL'
r={'read_only':True,'assets':[],'errors':[],'reference':reference}
registry=unreal.AssetRegistryHelpers.get_asset_registry();registry.wait_for_completion()
opts=unreal.AssetRegistryDependencyOptions(include_hard_package_references=True,include_soft_package_references=True)
def normalize(s):
 return s.replace('/Game/GASP58Native/','/Game/Blueprints/').replace('/Game/Blueprints/Characters/','/Game/Characters/')
def digest(s):return hashlib.sha256(normalize(s).encode('utf-8')).hexdigest()
def value(v):
 if isinstance(v,unreal.Object):return normalize(v.get_path_name())
 return str(v)
if reference:
 paths=json.loads((OUT/'ReferenceRequest.json').read_text())
else:
 roots=['/Game/Animation_Logic/ABPs/ABP_Player_Master','/Game/Animation_Logic/Chooser/CHT_ProjectJ_Air','/Game/Animation_Logic/PlayerMovement/CHT_RotationOffsetCurve']
 seen=set();pending=roots[:]
 while pending:
  p=str(pending.pop())
  if p in seen or not p.startswith('/Game/'):continue
  seen.add(p);pending.extend(str(x)for x in registry.get_dependencies(p,opts))
 r['roots']=roots;r['closure']=sorted(seen)
 inventory=[]
 for folder in ['/Game/Characters/UEFN_Mannequin/Animations','/Game/GASP58Native/Characters/UEFN_Mannequin/Animations']:
  for d in registry.get_assets_by_path(folder,True):
   if str(d.asset_class_path.asset_name) not in ['AnimSequence','AnimMontage','BlendSpace','BlendSpace1D']:continue
   p=str(d.package_name)
   inventory.append({'path':p,'class':str(d.asset_class_path.asset_name),'in_authored_closure':p in seen,'referencers':[str(x)for x in registry.get_referencers(p,opts)]})
 r['inventory']=inventory
 paths=sorted(x['path']for x in inventory if x['in_authored_closure'] and x['class']=='AnimSequence')
 request=sorted(set(p.replace('/Game/GASP58Native/Characters/','/Game/Characters/')for p in paths))
 (OUT/'ReferenceRequest.json').write_text(json.dumps(request,indent=2))
paths=globals().get('AUDIT_PATHS',paths)
for p in paths:
 row={'path':p}
 try:
  if not registry.get_assets_by_package_name(p):row['missing']=True;r['assets'].append(row);continue
  a=unreal.load_asset(p);assert isinstance(a,unreal.AnimSequence),p
  row['settings']={n:value(a.get_editor_property(n))for n in ['enable_root_motion','root_motion_root_lock','force_root_lock','use_normalized_root_motion_scale','additive_anim_type','ref_pose_type','ref_pose_seq','ref_frame_index','rate_scale','skeleton','loop']}
  row['length']=unreal.AnimationLibrary.get_sequence_length(a);row['frames']=unreal.AnimationLibrary.get_num_frames(a)
  row['notifies']=[]
  for e in unreal.AnimationLibrary.get_animation_notify_events(a):
   n=e.get_editor_property('notify')or e.get_editor_property('notify_state_class')
   row['notifies'].append({'metadata':normalize(e.export_text()),'class':value(n.get_class())if n else None,'name':str(e.get_editor_property('notify_name')),'time':unreal.AnimationLibrary.get_anim_notify_event_trigger_time(e),'duration':unreal.AnimationLibrary.get_anim_notify_event_duration(e)})
  f=OUT/'TemporaryAnimation.copy';t=unreal.AssetExportTask();t.object=a;t.filename=str(f);t.automated=True;t.prompt=False;t.replace_identical=True
  assert unreal.Exporter.run_asset_export_task(t),p
  b=f.read_bytes();text=b.decode('utf-16'if b[:2]in[b'\xff\xfe',b'\xfe\xff']else'utf-8-sig');f.unlink()
  row['raw_data_guid']=next((x.strip().split('=',1)[1]for x in text.splitlines()if x.strip().startswith('RawDataGuid=')),None)
  row['curves']={}
  for line in text.splitlines():
   for key in ['RawCurveData=','LegacyCurveData=']:
    if line.strip().startswith(key):
     canonical=re.sub(r'CurveName="([^"]+)"',lambda match:'CurveName="'+match.group(1).casefold()+'"',line.strip())
     row['curves'][key]={'sha256':digest(line.strip()),'fname_sha256':digest(canonical),'names':re.findall(r'CurveName="([^"]+)"',line)}

  r['assets'].append(row)
 except Exception:r['errors'].append({'path':p,'error':traceback.format_exc()});break
 (OUT/(label+'Progress.json')).write_text(json.dumps({'processed':len(r['assets']),'total':len(paths)}))
r['dirty_content_packages']=[x.get_path_name()for x in unreal.EditorLoadingAndSavingUtils.get_dirty_content_packages()];r['completed']=not r['errors'];receipt.write_text(json.dumps(r,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('GASP animation contracts %s: %d assets, %d errors'%(label,len(r['assets']),len(r['errors'])))
if reference:unreal.SystemLibrary.quit_editor()
