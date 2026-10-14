"""Public Blueprint subobject read; supplements MCP's absent CDO SCS component value."""
from pathlib import Path
import json,unreal
root=Path(unreal.Paths.project_saved_dir())/'Validation'/'Reaudit_20261014'
record={'blueprint':'/Game/Character_BPs/GreatSword/BP_GreatSword','method':'public SubobjectDataSubsystem / SubobjectDataBlueprintFunctionLibrary','asset_mutation':False,'does_not_prove_runtime_or_quality':True,'components':[],'unsupported':[]}
try:
    bp=unreal.load_asset(record['blueprint'])
    service=unreal.get_engine_subsystem(unreal.SubobjectDataSubsystem)
    library=unreal.SubobjectDataBlueprintFunctionLibrary
    for handle in service.k2_gather_subobject_data_for_blueprint(bp):
        data=library.get_data(handle)
        obj=library.get_object_for_blueprint(data,bp)
        if not isinstance(obj,unreal.SkeletalMeshComponent):continue
        entry={'path':obj.get_path_name(),'name':obj.get_name(),'fields':{},'unsupported':{}}
        for field in ['skeletal_mesh_asset','anim_class','component_tags','hidden_in_game']:
            try:
                value=obj.get_editor_property(field)
                entry['fields'][field]=value.get_path_name() if isinstance(value,unreal.Object) else str(value)
            except Exception as error:entry['unsupported'][field]=str(error)
        try:
            parent=obj.get_attach_parent()
            entry['parent']=parent.get_path_name() if parent else None
        except Exception as error:entry['unsupported']['attach_parent']=str(error)
        record['components'].append(entry)
except Exception as error:
    record['unsupported'].append(str(error))
(root/'FollowerTemplates1.json').write_text(json.dumps(record,ensure_ascii=False,indent=2),encoding='utf-8')
unreal.log('Reaudit public follower templates read completed; no saves.')
unreal.SystemLibrary.quit_editor()
