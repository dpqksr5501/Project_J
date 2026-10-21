#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimationAsset.h"
#include "Animation/Skeleton.h"
#include "Animation/BlendProfile.h"
#include "Animation/MirrorDataTable.h"
#include "StructUtils/InstancedStruct.h"
#include "EdGraph/EdGraph.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "Chooser.h"

int32 UProject_JPlayerLocomotionLibrary::RemapLocomotionBlendProfileNames(UChooserTable* Chooser,USkeleton* Source,USkeleton* Target)
{
 if(!Chooser || !Source || !Target || Source==Target)return INDEX_NONE;
 TMap<FName,FName> Names;
 for(const UBlendProfile* Profile:Source->BlendProfiles)
 {
  const FName Replacement(*(TEXT("Player_")+Profile->GetName()));
  if(Target->GetBlendProfile(Replacement))Names.Add(Profile->GetFName(),Replacement);
 }
 int32 Changed=0;TSet<UObject*> Modified;
 TFunction<void(const FProperty*,void*,UObject*)> Visit;
 TFunction<void(const UStruct*,void*,UObject*)> VisitStruct=[&](const UStruct* Type,void* Data,UObject* Owner)
 {for(TFieldIterator<FProperty> Field(Type);Field;++Field)Visit(*Field,Field->ContainerPtrToValuePtr<void>(Data),Owner);};
 Visit=[&](const FProperty* Field,void* Data,UObject* Owner)
 {
  if(const auto* Name=CastField<FNameProperty>(Field))
  {
   // The authored name is followed by the UserDefinedStruct index/GUID.
   if(Field->GetFName()!=TEXT("BlendProfile") && !Field->GetName().StartsWith(TEXT("BlendProfile_")))return;
   if(const auto* Replacement=Names.Find(Name->GetPropertyValue(Data)))
   {
    if(!Modified.Contains(Owner)){Owner->Modify();Modified.Add(Owner);}
    Name->SetPropertyValue(Data,*Replacement);++Changed;
   }
  }
  else if(const auto* Struct=CastField<FStructProperty>(Field))
  {
   if(Struct->Struct==FInstancedStruct::StaticStruct())
   {auto* Value=static_cast<FInstancedStruct*>(Data);if(Value->IsValid())VisitStruct(Value->GetScriptStruct(),Value->GetMutableMemory(),Owner);}
   else VisitStruct(Struct->Struct,Data,Owner);
  }
  else if(const auto* Array=CastField<FArrayProperty>(Field))
  {FScriptArrayHelper Values(Array,Data);for(int32 Index=0;Index<Values.Num();++Index)Visit(Array->Inner,Values.GetRawPtr(Index),Owner);}
 };
 TArray<UObject*> Owned;GetObjectsWithOuter(Chooser,Owned,EGetObjectsFlags::IncludeNestedObjects);Owned.Add(Chooser);
 for(UObject* Object:Owned)VisitStruct(Object->GetClass(),Object,Object);
 if(Changed){Chooser->PostEditChange();Chooser->MarkPackageDirty();}
 return Changed;
}

bool UProject_JPlayerLocomotionLibrary::RefreshLocomotionMirrorTable(UMirrorDataTable* Table, USkeleton* Skeleton)
{
 if(!Table || !Skeleton)return false;
 Table->Modify();Table->Skeleton=Skeleton;
 Table->UpdateFromFindReplaceExpressions(UMirrorDataTable::FFindReplaceOptions::Sync());
 Table->PostEditChange();Table->MarkPackageDirty();return true;
}

int32 UProject_JPlayerLocomotionLibrary::RemoveLocomotionDatabaseCandidate(UPoseSearchDatabase* Database,UAnimationAsset* Candidate)
{
 if(!Database || !Candidate)return INDEX_NONE;
 int32 Removed=0;
 for(int32 Index=Database->GetNumAnimationAssets()-1;Index>=0;--Index)
  if(Database->GetDatabaseAnimationAsset(Index)->GetAnimationAsset()==Candidate)
  {
   if(!Removed)Database->Modify();
   Database->RemoveAnimationAssetAt(Index);++Removed;
  }
 if(Removed){Database->PostEditChange();Database->MarkPackageDirty();}
 return Removed;
}

int32 UProject_JPlayerLocomotionLibrary::RemapLocomotionAssets(UObject* Owner,const TArray<UObject*>& Sources,const TArray<UObject*>& Replacements)
{
 if(!Owner || Sources.Num()!=Replacements.Num() || Owner->GetOutermost()==GetTransientPackage())return INDEX_NONE;
 TMap<UObject*,UObject*> Map;
 for(int32 Index=0;Index<Sources.Num();++Index)
 {
  UObject* From=Sources[Index];UObject* To=Replacements[Index];
  if(!From || !To || From==To || From->GetClass()!=To->GetClass())return INDEX_NONE;
  if(Map.Contains(From) && Map[From]!=To)return INDEX_NONE;
  Map.Add(From,To);
 }
 // A migration may edit consumers and copies, never the retained originals.
 if(Map.Contains(Owner))return INDEX_NONE;
 Owner->Modify();
 const auto Flags=EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef;
 FArchiveReplaceObjectRef<UObject> References(Owner,Map,Flags);int32 Changed=References.GetCount();
 if(auto* BP=Cast<UAnimBlueprint>(Owner))
 {
  if(BP->GeneratedClass)
  {
   FArchiveReplaceObjectRef<UObject> ClassRefs(BP->GeneratedClass,Map,Flags);Changed+=ClassRefs.GetCount();
   FArchiveReplaceObjectRef<UObject> Defaults(BP->GeneratedClass->GetDefaultObject(),Map,Flags);Changed+=Defaults.GetCount();
  }
  // Blueprint pin/default values use exported object paths, rather than UObject fields.
  auto RemapText=[&](FString& Text)
  {
   for(const auto& Pair:Map)
   {
    const FString From=Pair.Key->GetPathName();
    if(Text==From || Text.Contains(TEXT("'")+From+TEXT("'")))
    {Text=Text.Replace(*From,*Pair.Value->GetPathName());++Changed;}
   }
  };
  for(auto& Variable:BP->NewVariables)RemapText(Variable.DefaultValue);
  TArray<UEdGraph*> Graphs;BP->GetAllGraphs(Graphs);
  for(auto* Graph:Graphs)for(UEdGraphNode* Node:Graph->Nodes)for(auto* Pin:Node->Pins)RemapText(Pin->DefaultValue);
  if(Changed)
  {
   FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP);
   FBlueprintEditorUtils::RefreshAllNodes(BP);FKismetEditorUtilities::CompileBlueprint(BP);
   if(BP->Status==BS_Error)return INDEX_NONE;
  }
 }
 if(Changed)
 {
  TArray<UObject*> Owned;GetObjectsWithOuter(Owner,Owned,EGetObjectsFlags::IncludeNestedObjects);Owned.Add(Owner);
  for(auto* Object:Owned)
   if(Object->IsA<UPoseSearchDatabase>() || Object->IsA<UAnimationAsset>() || Object->IsA<UChooserTable>())Object->PostEditChange();
  Owner->MarkPackageDirty();
 }
 return Changed;
}
