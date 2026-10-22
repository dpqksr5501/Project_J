#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JAnimNotifyState_LocomotionEarlyTransition.h"
#include "Animation/AnimBlueprint.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_FunctionEntry.h"
#include "K2Node_FunctionResult.h"
#include "K2Node_CallFunction.h"
#include "K2Node_IfThenElse.h"
#include "Misc/PackageName.h"
#include "UObject/UnrealType.h"
#include "Chooser.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "Retargeter/IKRetargeter.h"
#include "ChooserPropertyAccess.h"
#include "K2Node_VariableGet.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_PoseSearchHistoryCollector.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JPlayerCharacter.h"
#include "Animation/Project_JPlayerAirTypes.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "EnumColumn.h"
#include "ChooserTypes.h"
#include "ObjectChooser_Asset.h"
#include "AnimGraphNode_BlendStack.h"
#include "AnimStateTransitionNode.h"
#include "Movement/Project_JMoverComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/Project_JAnimNotify_FoleyEvent.h"
#include "MultiEnumColumn.h"
#include "FloatRangeColumn.h"
#include "BoolColumn.h"
#include "OutputStructColumn.h"
#include "PoseSearch/Chooser/PoseSearchChooserColumn.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"

int32 UProject_JPlayerLocomotionLibrary::ConfigureBackwardFacingTransitions(UChooserTable* Root)
{
 if (!Root || (GEditor && GEditor->PlayWorld)) return INDEX_NONE;
 TArray<UObject*> Owned;GetObjectsWithOuter(Root,Owned,EGetObjectsFlags::IncludeNestedObjects);
 UChooserTable* Table=nullptr;
 for (UObject* Object:Owned) if (auto* Candidate=Cast<UChooserTable>(Object);Candidate && Candidate->GetFName()==TEXT("Run B Loco Transitions"))
 {if (Table) return INDEX_NONE;Table=Candidate;}
 if (!Table || Table->ColumnsStructs.Num()!=6) return INDEX_NONE;
 auto* Speed=Table->ColumnsStructs[0].GetMutablePtr<FFloatRangeColumn>();
 auto* Facing=Table->ColumnsStructs[1].GetMutablePtr<FFloatRangeColumn>();
 auto* Pivot=Table->ColumnsStructs[2].GetMutablePtr<FBoolColumn>();
 auto* Recent=Table->ColumnsStructs[3].GetMutablePtr<FMultiEnumColumn>();
 auto* Output=Table->ColumnsStructs[4].GetMutablePtr<FOutputStructColumn>();
 auto* Search=Table->ColumnsStructs[5].GetMutablePtr<FPoseSearchColumn>();
 auto FloatBinding=[](const FFloatRangeColumn* C,FName Name)
 {const auto* P=C?C->InputValue.GetPtr<FFloatContextProperty>():nullptr;return P && P->Binding.ContextIndex==0 && P->Binding.PropertyBindingChain==TArray<FName>{Name};};
 const auto* PivotBinding=Pivot?Pivot->InputValue.GetPtr<FBoolContextProperty>():nullptr;
 const auto* RecentBinding=Recent?Recent->InputValue.GetPtr<FEnumContextProperty>():nullptr;
 if (!FloatBinding(Speed,GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerSpeed2D)) ||
  !FloatBinding(Facing,GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerFutureFacingDelta)) || !PivotBinding ||
  PivotBinding->Binding.PropertyBindingChain!=TArray<FName>{TEXT("EvaluateIsPivoting")} || !RecentBinding ||
  RecentBinding->Binding.PropertyBindingChain!=TArray<FName>{GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerMovementDirectionRecent)} || !Output || !Search) return INDEX_NONE;
 const auto* RowsProperty=FindFProperty<FArrayProperty>(FPoseSearchColumn::StaticStruct(),TEXT("RowValues"));
 const auto* RowProperty=RowsProperty?CastField<FStructProperty>(RowsProperty->Inner):nullptr;
 if (!RowProperty || RowProperty->Struct!=FPoseSearchColumnRow::StaticStruct()) return INDEX_NONE;
 auto& SearchRows=*RowsProperty->ContainerPtrToValuePtr<TArray<FPoseSearchColumnRow>>(Search);
 const int32 Count=Table->ResultsStructs.Num();
 if (Speed->RowValues.Num()!=Count || Facing->RowValues.Num()!=Count || Pivot->RowValuesWithAny.Num()!=Count ||
  Recent->RowValues.Num()!=Count || Output->RowValues.Num()!=Count || SearchRows.Num()!=Count || Table->DisabledRows.Num()!=Count) return INDEX_NONE;
 constexpr uint32 Backward=1u<<uint8(EProject_JPlayerMovementDirection::Backward);
 FInstancedStruct TurnSettings;
 for (int32 Row=0;Row<Count;++Row) if (!Table->DisabledRows[Row] &&
  Pivot->RowValuesWithAny[Row]==EBoolColumnCellValue::MatchTrue && Recent->RowValues[Row].Value!=Backward)
  if (const auto* Settings=Output->RowValues[Row].GetPtr<FProject_JPlayerGroundChooserOutput>();Settings && Settings->Tags.Contains(TEXT("Pivot")))
  {
   if (!TurnSettings.IsValid()) TurnSettings=Output->RowValues[Row];
   else if (TurnSettings!=Output->RowValues[Row]) return INDEX_NONE;
  }
 if (!TurnSettings.IsValid()) return INDEX_NONE;
 TArray<int32> Sources;
 for (int32 Row=0;Row<Count;++Row)
 {
  const auto* Asset=Table->ResultsStructs[Row].GetPtr<FAssetChooser>();
  if (Table->DisabledRows[Row] || !Asset || !Asset->Asset || !Asset->Asset->GetName().StartsWith(TEXT("M_Neutral_Run_Reface_Start_B_"))) continue;
  if (Pivot->RowValuesWithAny[Row]!=EBoolColumnCellValue::MatchFalse) continue;
  if (SearchRows[Row].Data.GetAnimationAsset()!=Asset->Asset || !SearchRows[Row].Database ||
   !Output->RowValues[Row].GetPtr<FProject_JPlayerGroundChooserOutput>()) return INDEX_NONE;
  Sources.Add(Row);
 }
 if (Sources.Num()!=4) return INDEX_NONE;
 TArray<int32> Missing;
 for (int32 Source:Sources)
 {
  int32 Matches=0;
  for (int32 Row=0;Row<Count;++Row)
   if (!Table->DisabledRows[Row] && Table->ResultsStructs[Row]==Table->ResultsStructs[Source] &&
    Recent->RowValues[Row].Value==Backward && Pivot->RowValuesWithAny[Row]==EBoolColumnCellValue::MatchTrue)
   {
    if (!Speed->RowValues[Row].bNoMin || !Speed->RowValues[Row].bNoMax ||
     Facing->RowValues[Row].Min!=Facing->RowValues[Source].Min || Facing->RowValues[Row].Max!=Facing->RowValues[Source].Max ||
     Facing->RowValues[Row].bNoMin!=Facing->RowValues[Source].bNoMin || Facing->RowValues[Row].bNoMax!=Facing->RowValues[Source].bNoMax ||
     !(SearchRows[Row]==SearchRows[Source]) || Output->RowValues[Row]!=TurnSettings) return INDEX_NONE;
    ++Matches;
   }
  if (Matches>1) return INDEX_NONE;
  if (!Matches) Missing.Add(Source);
 }
 if (Missing.IsEmpty()) return 0;
 Root->Modify();Table->Modify();
 TSet<UPoseSearchDatabase*> ChangedDatabases;
 for (int32 Source:Missing) ChangedDatabases.Add(SearchRows[Source].Database);
 // Match the engine's CopyRow lifecycle: the database's index topology changed
 // too. Invalidate it before editing, then publish after rebuilding its mapping.
 for (UPoseSearchDatabase* Database:ChangedDatabases) Database->Modify();
 for (int32 Source:Missing)
 {
  // Copy individual rows, never FPoseSearchColumn (its copy constructor clears
  // database references). The native pivot predicate owns the speed/turn gate.
  auto Result=Table->ResultsStructs[Source];Table->ResultsStructs.Add(MoveTemp(Result));Table->DisabledRows.Add(false);
  FChooserFloatRangeRowData SpeedRange;SpeedRange.bNoMin=SpeedRange.bNoMax=true;Speed->RowValues.Add(SpeedRange);
  const auto FacingRange=Facing->RowValues[Source];Facing->RowValues.Add(FacingRange);
  Pivot->RowValuesWithAny.Add(EBoolColumnCellValue::MatchTrue);
  auto Direction=Recent->RowValues[Source];Direction.Value=Backward;Recent->RowValues.Add(Direction);
  // Use the existing moving-turn blend contract, not the low-speed start's
  // shorter blend. Pose matching still writes the actual entry time last.
  Output->RowValues.Add(TurnSettings);
  const auto PoseRow=SearchRows[Source];SearchRows.Add(PoseRow);
 }
 // Row topology changed: Compile/PostEditChange rebuild search data but do not
 // refresh the column-to-database mapping. Use the engine's virtual authoring
 // lifecycle, which also updates every sibling column sharing that database.
 Table->ColumnsStructs[5].GetMutable<FChooserColumnBase>().Initialize(Table);
 for (UPoseSearchDatabase* Database:ChangedDatabases) Database->PostEditChange();
 Table->Compile(true);Table->PostEditChange();Root->Compile(true);Root->PostEditChange();Root->MarkPackageDirty();
 return Missing.Num();
}

int32 UProject_JPlayerLocomotionLibrary::ConfigureGroundedLandingSearch(UChooserTable* Root)
{
 if (!Root || (GEditor && GEditor->PlayWorld)) return INDEX_NONE;
 struct FChange {UChooserTable* Table;int32 Column;int32 Row;FFloatInterval Range;};
 // UE 5.8 exposes these row accessors without module exports. Use the
 // serialized UPROPERTY boundary rather than changing the installed engine.
 const auto* RowsProperty=FindFProperty<FArrayProperty>(FPoseSearchColumn::StaticStruct(),TEXT("RowValues"));
 const auto* RowProperty=RowsProperty?CastField<FStructProperty>(RowsProperty->Inner):nullptr;
 if (!RowProperty || RowProperty->Struct!=FPoseSearchColumnRow::StaticStruct()) return INDEX_NONE;
 auto Rows=[RowsProperty](FPoseSearchColumn* Search)->TArray<FPoseSearchColumnRow>&
 {return *RowsProperty->ContainerPtrToValuePtr<TArray<FPoseSearchColumnRow>>(Search);};
 TArray<FChange> Changes;
 TArray<UObject*> Owned;GetObjectsWithOuter(Root,Owned,EGetObjectsFlags::IncludeNestedObjects);
 int32 LandingRows=0;
 for (UObject* Object:Owned) if (auto* Table=Cast<UChooserTable>(Object))
 {
  const FMultiEnumColumn* Modes=nullptr;
  for (const auto& C:Table->ColumnsStructs) if (const auto* Mode=C.GetPtr<FMultiEnumColumn>())
   if (const auto* P=Mode->InputValue.GetPtr<FEnumContextProperty>();P && P->Binding.ContextIndex==0 &&
    P->Binding.PropertyBindingChain==TArray<FName>{GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerMovementModeRecent)})
   {if (Modes) return INDEX_NONE;Modes=Mode;}
  if (!Modes) continue;
  for (int32 C=0;C<Table->ColumnsStructs.Num();++C) if (auto* Search=Table->ColumnsStructs[C].GetMutablePtr<FPoseSearchColumn>())
   for (int32 Row=0;Row<Search->GetNumRows();++Row)
   {
    if ((Table->DisabledRows.IsValidIndex(Row) && Table->DisabledRows[Row]) || !Modes->RowValues.IsValidIndex(Row) ||
     Modes->RowValues[Row].Value!=(1u<<uint8(EProject_JPlayerMovementMode::InAir))) continue;
    const auto& Data=Rows(Search)[Row].Data;
    const auto* Sequence=Cast<UAnimSequence>(Data.GetAnimationAsset());
    if (!Sequence || !Data.IsEnabled()) return INDEX_NONE;
    float Impact=MAX_flt,Block=MAX_flt;
    for (const FAnimNotifyEvent& Event:Sequence->Notifies)
    {
     if (const auto* Foley=Cast<UProject_JAnimNotify_FoleyEvent>(Event.Notify);Foley && Foley->FoleyEvent.Event==Project_J::Foley::Land)
      Impact=FMath::Min(Impact,Event.GetTime());
     if (Event.NotifyStateClass && Event.NotifyStateClass->IsA<UAnimNotifyState_PoseSearchBlockTransition>())
      Block=FMath::Min(Block,Event.GetTime());
    }
    // These rows are entered after physical contact. Their full sequences also
    // contain the approach from the air; pose cost alone must not replay it.
    // One authored frame of anticipation keeps a blend into impact available.
    if (Impact==MAX_flt || Block==MAX_flt || Impact<=0.f || Block<=0.f) return INDEX_NONE;
    const float Frame=Sequence->GetSamplingFrameRate().AsInterval();
    const auto Effective=Data.GetEffectiveSamplingRange(FVector::ZeroVector);
    const float Minimum=FMath::Max(Effective.Min,FMath::Max(0.f,FMath::Min(Impact,Block)-Frame));
    if (!FMath::IsFinite(Minimum) || Minimum>=FMath::Min(Block,Effective.Max)) return INDEX_NONE;
    ++LandingRows;
    if (!FMath::IsNearlyEqual(Data.SamplingRange.Min,Minimum) || Data.SamplingRange.Max!=Effective.Max)
     Changes.Add({Table,C,Row,FFloatInterval(Minimum,Effective.Max)});
   }
 }
 if (!LandingRows) return INDEX_NONE;
 if (Changes.IsEmpty()) return 0;
 Root->Modify();TSet<UChooserTable*> Changed;
 for (const auto& Change:Changes)
 {
  if (!Changed.Contains(Change.Table)) {Change.Table->Modify();Changed.Add(Change.Table);}
  Rows(&Change.Table->ColumnsStructs[Change.Column].GetMutable<FPoseSearchColumn>())[Change.Row].Data.SamplingRange=Change.Range;
 }
 for (UChooserTable* Table:Changed) {Table->Compile(true);Table->PostEditChange();}
 Root->Compile(true);Root->PostEditChange();Root->MarkPackageDirty();
 return Changes.Num();
}

bool UProject_JPlayerLocomotionLibrary::ConfigureStandingStopGaitFilter(UChooserTable* Root)
{
 if (!Root || (GEditor && GEditor->PlayWorld)) return false;
 TArray<UObject*> Nested;GetObjectsWithOuter(Root,Nested,EGetObjectsFlags::IncludeNestedObjects);
 UChooserTable* Stops=nullptr;
 for (UObject* Object:Nested) if (auto* Table=Cast<UChooserTable>(Object);Table && Table->GetFName()==TEXT("Stand Stops"))
 {if (Stops) return false;Stops=Table;}
 if (!Stops || Stops->ResultsStructs.IsEmpty()) return false;
 FInstancedStruct NewColumn=FInstancedStruct::Make<FEnumColumn>();auto& Column=NewColumn.GetMutable<FEnumColumn>();
 Column.InputValue.InitializeAs<FEnumContextProperty>();auto& Binding=Column.InputValue.GetMutable<FEnumContextProperty>().Binding;
 Binding.ContextIndex=0;Binding.PropertyBindingChain={GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerGaitRecent)};
 Binding.Enum=StaticEnum<EProject_JLocomotionGaitIntent>();Binding.DisplayName=TEXT("Recent Gait (Stop Family)");
 Column.DefaultRowValue.Comparison=EEnumColumnCellValueComparison::MatchAny;
 int32 RunCount=0,SprintCount=0,WalkCount=0;
 for (int32 Index=0;Index<Stops->ResultsStructs.Num();++Index)
 {
  auto& Row=Column.RowValues.AddDefaulted_GetRef();Row.Comparison=EEnumColumnCellValueComparison::MatchAny;
  Row.Value=uint8(EProject_JLocomotionGaitIntent::Sprint);Row.ValueName=Binding.Enum->GetNameByValue(Row.Value);
  if (Stops->DisabledRows.IsValidIndex(Index) && Stops->DisabledRows[Index]) continue;
  const auto* Asset=Stops->ResultsStructs[Index].GetPtr<FAssetChooser>();
  if (!Asset || !Asset->Asset || !Asset->Asset->IsA<UAnimSequence>()) return false;
  const FString Path=Asset->Asset->GetPathName();
  if (Path.StartsWith(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/Run/")))
  {Row.Comparison=EEnumColumnCellValueComparison::MatchNotEqual;++RunCount;}
  else if (Path.StartsWith(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/Sprint/")))
  {Row.Comparison=EEnumColumnCellValueComparison::MatchEqual;++SprintCount;}
  else if (Path.StartsWith(TEXT("/Game/Characters/Mannequins/Animations/Locomotion/Walk/"))) ++WalkCount;
  else return false; // An unfamiliar bank needs an explicit authoring decision.
 }
 if (!RunCount || !SprintCount || !WalkCount) return false;
 int32 Existing=INDEX_NONE;
 for (int32 Index=0;Index<Stops->ColumnsStructs.Num();++Index)
  if (const auto* C=Stops->ColumnsStructs[Index].GetPtr<FEnumColumn>())
   if (const auto* P=C->InputValue.GetPtr<FEnumContextProperty>();P && P->Binding.PropertyBindingChain==Binding.PropertyBindingChain)
   {if (Existing!=INDEX_NONE) return false;Existing=Index;}
 if (Existing!=INDEX_NONE && Stops->ColumnsStructs[Existing]==NewColumn) return true;
 Stops->Modify();Root->Modify();
 if (Existing==INDEX_NONE) Stops->ColumnsStructs.Insert(MoveTemp(NewColumn),0);
 else Stops->ColumnsStructs[Existing]=MoveTemp(NewColumn);
 Stops->Compile(true);Root->Compile(true);
 // Modify cancels dependent Pose Search tasks. Complete the editor change
 // notification so their manager can leave PreCancelled and rebuild indexes.
 Stops->PostEditChange();Root->PostEditChange();Root->MarkPackageDirty();return true;
}

bool UProject_JPlayerLocomotionLibrary::UseNativeRotationOffsetQuery(UChooserTable* Table)
{
 if (!Table || (GEditor && GEditor->PlayWorld) || Table->ContextData.Num()!=1 || Table->ColumnsStructs.Num()!=2) return false;
 auto* Context=Table->ContextData[0].GetMutablePtr<FContextObjectTypeStruct>();
 if (!Context || !Context->Struct) return false;
 if (Context->Struct==FProject_JPlayerRotationOffsetQuery::StaticStruct()) return true;
 const FByteProperty* Mode=nullptr; const FByteProperty* Direction=nullptr;
 for (TFieldIterator<FByteProperty> P(Context->Struct);P;++P)
 {
  if (P->GetName().StartsWith(TEXT("MovementMode_"))) Mode=*P;
  if (P->GetName().StartsWith(TEXT("MovementDirection_"))) Direction=*P;
 }
 if (!Mode || !Direction) return false;
 TArray<FName> Fields;
 for (const auto& Struct:Table->ColumnsStructs)
 {
  const auto* Column=Struct.GetPtr<FEnumColumn>();
  const auto* Parameter=Column?Column->InputValue.GetPtr<FEnumContextProperty>():nullptr;
  if (!Parameter || Parameter->Binding.ContextIndex!=0 || Parameter->Binding.PropertyBindingChain.Num()!=1) return false;
  const FName Field=Parameter->Binding.PropertyBindingChain[0];
  const UEnum* Enum=Field==Mode->GetFName()?StaticEnum<EProject_JPlayerMovementMode>():Field==Direction->GetFName()?StaticEnum<EProject_JPlayerMovementDirection>():nullptr;
  if (!Enum) return false;
  for (const auto& Row:Column->RowValues)
   if (Row.Comparison!=EEnumColumnCellValueComparison::MatchAny && !Enum->IsValidEnumValue(Row.Value)) return false;
  Fields.Add(Field==Mode->GetFName()?GET_MEMBER_NAME_CHECKED(FProject_JPlayerRotationOffsetQuery,MovementMode):GET_MEMBER_NAME_CHECKED(FProject_JPlayerRotationOffsetQuery,MovementDirection));
 }
 if (Fields[0]==Fields[1]) return false;
 auto Select=[&](int32 MovementMode,int32 MovementDirection,bool bNative)->UObject*
 {
  FProject_JPlayerRotationOffsetQuery Query;
  Query.MovementMode=static_cast<EProject_JPlayerMovementMode>(MovementMode);
  Query.MovementDirection=static_cast<EProject_JPlayerMovementDirection>(MovementDirection);
  FInstancedStruct Legacy;
  FChooserEvaluationContext Evaluation;
  if (bNative) Evaluation.AddStructParam(Query);
  else
  {
   Legacy.InitializeAs(Context->Struct);
   Mode->SetPropertyValue_InContainer(Legacy.GetMutableMemory(),MovementMode);
   Direction->SetPropertyValue_InContainer(Legacy.GetMutableMemory(),MovementDirection);
   Evaluation.AddStructViewParam(FStructView(Legacy.GetScriptStruct(),Legacy.GetMutableMemory()));
  }
  UObject* Selected=nullptr;
  UChooserTable::EvaluateChooser(Evaluation,Table,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Object)
  {Selected=Object;return Object?FObjectChooserBase::EIteratorStatus::Stop:FObjectChooserBase::EIteratorStatus::Failed;}));
  return Selected;
 };
 TArray<UObject*> Before;
 for (int32 M=0;M<6;++M) for (int32 D=0;D<6;++D) Before.Add(Select(M,D,false));
 if (!Before.ContainsByPredicate([](const UObject* Object){return Object!=nullptr;})) return false;
 const auto OldContext=Table->ContextData;
 const auto OldColumns=Table->ColumnsStructs;
 Table->Modify();
 Context->Struct=FProject_JPlayerRotationOffsetQuery::StaticStruct();
 for (int32 Index=0;Index<2;++Index)
 {
  auto* Column=Table->ColumnsStructs[Index].GetMutablePtr<FEnumColumn>();
  auto* Parameter=Column->InputValue.GetMutablePtr<FEnumContextProperty>();
  const UEnum* Enum=Fields[Index]==GET_MEMBER_NAME_CHECKED(FProject_JPlayerRotationOffsetQuery,MovementMode)?StaticEnum<EProject_JPlayerMovementMode>():StaticEnum<EProject_JPlayerMovementDirection>();
  Parameter->Binding.PropertyBindingChain={Fields[Index]};
  Parameter->Binding.Enum=Enum;
  Parameter->Binding.DisplayName=Fields[Index].ToString();
  for (auto& Row:Column->RowValues) Row.ValueName=Enum->GetNameByValue(Row.Value);
  Column->DefaultRowValue.ValueName=Enum->GetNameByValue(Column->DefaultRowValue.Value);
 }
 Table->Compile(true);
 for (int32 M=0;M<6;++M) for (int32 D=0;D<6;++D)
 {
  if (Select(M,D,true)!=Before[M*6+D])
  {
   Table->ContextData=OldContext;Table->ColumnsStructs=OldColumns;Table->Compile(true);
   return false;
  }
  UE_LOG(LogTemp,Display,TEXT("RotationOffset query preserved: Mode=%d Direction=%d Curve=%s"),M,D,*GetPathNameSafe(Before[M*6+D]));
 }
 Table->MarkPackageDirty();
 return true;
}

bool UProject_JPlayerLocomotionLibrary::ConfigureNormalizedLocomotionSync(UBlendSpace* BlendSpace)
{
	if (!IsValid(BlendSpace) || !BlendSpace->bLoop || BlendSpace->IsValidAdditive() || BlendSpace->GetBlendSamples().IsEmpty()) return false;
	// UE 5.8's marker phase reset also resets the leader's PreviousTime. Its
	// per-sample root-motion delta then becomes zero, starving Steering and
	// OffsetRootBone. Normalized sample time is shared by search and playback;
	// retain Pose Search's phase contract and the source clips' contact markers.
	BlendSpace->Modify();
	BlendSpace->bShouldMatchSyncPhases = true;
	BlendSpace->bAllowMarkerBasedSync = false;
	BlendSpace->ValidateSampleData();
	BlendSpace->MarkPackageDirty();
	return true;
}

bool UProject_JPlayerLocomotionLibrary::UpdateRetargetedAnimationData(UAnimSequence* Target, UAnimSequence* Converted)
{
	if (!IsValid(Target) || !IsValid(Converted) || Target == Converted ||
		Target->GetSkeleton() != Converted->GetSkeleton() ||
		!Target->IsDataModelValid() || !Converted->IsDataModelValid()) return false;
	const IAnimationDataModel* Before = Target->GetDataModel();
	const IAnimationDataModel* After = Converted->GetDataModel();
	// A different timeline could invalidate the target's notify and sync-marker timing.
	if (Before->GetFrameRate() != After->GetFrameRate() ||
		Before->GetNumberOfFrames() != After->GetNumberOfFrames()) return false;
	TArray<FName> TargetNames, ConvertedNames;
	Before->GetBoneTrackNames(TargetNames);
	After->GetBoneTrackNames(ConvertedNames);
	if (TargetNames.Num() != ConvertedNames.Num()) return false;
	TMap<FName, TArray<FTransform>> Tracks;
	for (const FName Name : ConvertedNames)
	{
		if (!TargetNames.Contains(Name)) return false;
		TArray<FTransform>& Keys = Tracks.Add(Name);
		After->GetBoneTrackTransforms(Name, Keys);
		if (Keys.Num() != After->GetNumberOfKeys()) return false;
		for (const FTransform& Key : Keys) if (Key.ContainsNaN()) return false;
	}
	Target->Modify();
	IAnimationDataController& Controller = Target->GetController();
	IAnimationDataController::FScopedBracket Bracket(Controller,
		NSLOCTEXT("ProjectJLocomotion", "UpdateRetargetedTracks", "Update retargeted bone tracks"), false);
	for (const auto& Track : Tracks)
	{
		TArray<FVector> Positions, Scales;
		TArray<FQuat> Rotations;
		Positions.Reserve(Track.Value.Num());
		Scales.Reserve(Track.Value.Num());
		Rotations.Reserve(Track.Value.Num());
		for (const FTransform& Key : Track.Value)
		{
			Positions.Add(Key.GetTranslation());
			Rotations.Add(Key.GetRotation());
			Scales.Add(Key.GetScale3D());
		}
		if (!Controller.SetBoneTrackKeys(Track.Key, Positions, Rotations, Scales, false)) return false;
	}
	Target->MarkPackageDirty();
	return true;
}

namespace
{
UEdGraph* FindFoundationFunctionGraph(UBlueprint* Blueprint, FName Name)
{
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs) if (Graph->GetFName() == Name) return Graph;
	return nullptr;
}

bool ReplaceVoidFunction(UBlueprint* Blueprint, FName Name, UFunction* Replacement)
{
	auto* Graph = FindFoundationFunctionGraph(Blueprint, Name);
	if (!Graph) return false;
	UK2Node_FunctionEntry* Entry = nullptr;
	UK2Node_FunctionResult* Result = nullptr;
	const auto Nodes = Graph->Nodes;
	for (UEdGraphNode* Node : Nodes)
	{
		if (auto* FunctionEntry = Cast<UK2Node_FunctionEntry>(Node)) Entry = FunctionEntry;
		else if (auto* FunctionResult = Cast<UK2Node_FunctionResult>(Node)) Result = FunctionResult;
		else FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	}
	if (!Entry) return false;
	for (auto* Pin : Entry->Pins) Pin->BreakAllPinLinks();
	if (Result) for (auto* Pin : Result->Pins) Pin->BreakAllPinLinks();
	UEdGraphPin* Then = Entry->GetThenPin();
	if (Replacement)
	{
		FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);
		auto* Call = Creator.CreateNode();
		Call->SetFromFunction(Replacement);
		Creator.Finalize();
		if (!Graph->GetSchema()->TryCreateConnection(Then, Call->GetExecPin())) return false;
		Then = Call->GetThenPin();
	}
	if (Result) return Graph->GetSchema()->TryCreateConnection(Then, Result->FindPin(UEdGraphSchema_K2::PN_Execute));
	return true;
}

}

bool UProject_JPlayerLocomotionLibrary::InstallNativeTransitionNotify(UBlueprint* Notify)
{
 if (!Notify || !Notify->GeneratedClass || !Notify->GeneratedClass->IsChildOf(UAnimNotifyState::StaticClass())) return false;
 auto* Graph=FindFoundationFunctionGraph(Notify,TEXT("Received_NotifyTick"));
 if (!Graph) return false;
 UK2Node_FunctionEntry* Entry=nullptr;UK2Node_FunctionResult* Result=nullptr;
 for (UEdGraphNode* Node:Graph->Nodes)
 {
  if (auto* Candidate=Cast<UK2Node_FunctionEntry>(Node)) Entry=Candidate;
  if (!Result) Result=Cast<UK2Node_FunctionResult>(Node);
 }
 if (!Entry || !Entry->FindPin(TEXT("MeshComp")) || !Entry->FindPin(TEXT("EventReference"))) return false;
 const FName Fields[]={TEXT("TransitionDestination"),TEXT("TransitionCondition"),TEXT("GaitNotEqual")};
 for (FName Field:Fields)
 {
  const auto* Property=FindFProperty<FByteProperty>(Notify->GeneratedClass,Field);
  if (!Property || !Property->Enum || !Property->Enum->IsValidEnumValue(0) || !Property->Enum->IsValidEnumValue(1)) return false;
 }
 Notify->Modify();Graph->Modify();
 for (UEdGraphNode* Node:TArray<TObjectPtr<UEdGraphNode>>(Graph->Nodes))
 {
  Node->BreakAllNodeLinks();
  if (Node!=Entry && Node!=Result) FBlueprintEditorUtils::RemoveNode(Notify,Node,true);
 }
 FGraphNodeCreator<UK2Node_CallFunction> Creator(*Graph);auto* Call=Creator.CreateNode();
 Call->SetFromFunction(UProject_JAnimNotifyState_LocomotionEarlyTransition::StaticClass()->FindFunctionByName(
  GET_FUNCTION_NAME_CHECKED(UProject_JAnimNotifyState_LocomotionEarlyTransition,RequestGASPTransition)));Creator.Finalize();
 const auto* Schema=Graph->GetSchema();
 if (!Schema->TryCreateConnection(Entry->GetThenPin(),Call->GetExecPin())) return false;
 for (FName Pin:{FName(TEXT("MeshComp")),FName(TEXT("EventReference"))})
  if (!Schema->TryCreateConnection(Entry->FindPin(Pin),Call->FindPin(Pin))) return false;
 const FName Inputs[]={TEXT("Destination"),TEXT("Condition"),TEXT("GaitNotEqual")};
 for (int32 Index=0;Index<3;++Index)
 {
  FGraphNodeCreator<UK2Node_VariableGet> GetCreator(*Graph);auto* Get=GetCreator.CreateNode();
  Get->VariableReference.SetSelfMember(Fields[Index]);GetCreator.Finalize();
  if (!Schema->TryCreateConnection(Get->GetValuePin(),Call->FindPin(Inputs[Index]))) return false;
 }
 if (Result && !Schema->TryCreateConnection(Call->GetThenPin(),Result->FindPin(UEdGraphSchema_K2::PN_Execute))) return false;
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Notify);FKismetEditorUtilities::CompileBlueprint(Notify);
 Notify->MarkPackageDirty();return Notify->Status!=BS_Error;
}

bool UProject_JPlayerLocomotionLibrary::RepairFullBodyMontageOrder(UAnimBlueprint* Target)
{
 auto* Graph = Target ? FindFoundationFunctionGraph(Target, TEXT("AnimGraph")) : nullptr;
 if (!Graph) return false;
 UAnimGraphNode_Slot* Slot = nullptr;
 UAnimGraphNode_LinkedAnimLayer* FullBody = nullptr;
 UAnimGraphNode_SaveCachedPose* Ground = nullptr;
 for (UEdGraphNode* Node : Graph->Nodes)
 {
  if (auto* Candidate = Cast<UAnimGraphNode_Slot>(Node); Candidate && Candidate->Node.SlotName == TEXT("DefaultSlot"))
  {
   // A duplicate playback owner is an authoring error, not something to silently remove.
   if (Slot) return false;
   Slot = Candidate;
  }
  if (auto* Layer = Cast<UAnimGraphNode_LinkedAnimLayer>(Node); Layer && Layer->Node.Layer == TEXT("Procedural_FullBody")) FullBody = Layer;
  if (auto* Cache = Cast<UAnimGraphNode_SaveCachedPose>(Node); Cache && Cache->CacheName == TEXT("ProjectJ_FinalGroundPose")) Ground = Cache;
 }
 if (!Slot || !FullBody || !Ground) return false;
 auto* Input = Slot->FindPin(TEXT("Source"));
 auto* Output = Slot->FindPin(TEXT("Pose"));
 auto* ProceduralOutput = FullBody->FindPin(TEXT("Pose"));
 auto* GroundInput = Ground->FindPin(TEXT("Pose"));
 if (!Input || !Output || !ProceduralOutput || !GroundInput) return false;
 const bool bOrdered = Input->LinkedTo.Num() == 1 && Input->LinkedTo[0] == ProceduralOutput &&
     Output->LinkedTo.Num() == 1 && Output->LinkedTo[0] == GroundInput;
 if (bOrdered && Slot->Node.bAlwaysUpdateSourcePose) return Target->Status != BS_Error;
 if (!bOrdered && (Input->LinkedTo.Num() != 1 || Output->LinkedTo.Num() != 1 ||
     GroundInput->LinkedTo.Num() != 1 || GroundInput->LinkedTo[0] != ProceduralOutput)) return false;
 Target->Modify(); Slot->Modify(); Ground->Modify();
 if (!bOrdered)
 {
  auto* Before = Input->LinkedTo[0]; auto* After = Output->LinkedTo[0];
  Input->BreakAllPinLinks(); Output->BreakAllPinLinks(); GroundInput->BreakAllPinLinks();
  const auto* Schema = Graph->GetSchema();
  if (!Schema->TryCreateConnection(Before, After) ||
      !Schema->TryCreateConnection(ProceduralOutput, Input) ||
      !Schema->TryCreateConnection(Output, GroundInput)) return false;
 }
 // Continue GASP state/stack clocks while a full-weight skill masks evaluation.
 // Cancellation and blend-out must resume the live locomotion state, not its old time.
 Slot->Node.bAlwaysUpdateSourcePose = true;
 Slot->NodePosX = Ground->NodePosX - 250;
 Slot->NodePosY = Ground->NodePosY;
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);
 FKismetEditorUtilities::CompileBlueprint(Target);
 Target->MarkPackageDirty();
 return Target->Status != BS_Error;
}

TArray<FString> UProject_JPlayerLocomotionLibrary::InspectGameplaySockets(USkeletalMesh* Previous, USkeletalMesh* Target)
{
 TArray<FString> Result;
 if (!Previous || !Target) return Result;
 const auto& Before=Previous->GetRefSkeleton(); const auto& After=Target->GetRefSkeleton();
 for (const auto* Socket : Previous->GetActiveSocketList())
 {
  if (!Socket) continue;
  const int32 A=Before.FindBoneIndex(Socket->BoneName), B=After.FindBoneIndex(Socket->BoneName);
  if (A==INDEX_NONE || B==INDEX_NONE) {Result.Add(FString::Printf(TEXT("INVALID %s Bone=%s"),*Socket->SocketName.ToString(),*Socket->BoneName.ToString()));continue;}
  const auto& Old=Before.GetRefBonePose()[A]; const auto& New=After.GetRefBonePose()[B];
  Result.Add(FString::Printf(TEXT("Socket=%s Bone=%s Exists=%d LocalRefAngle=%.4f LocalRefTranslation=%.4f Transform=%s"),
   *Socket->SocketName.ToString(),*Socket->BoneName.ToString(),Target->FindSocket(Socket->SocketName)!=nullptr,
   FMath::RadiansToDegrees(Old.GetRotation().AngularDistance(New.GetRotation())),FVector::Distance(Old.GetTranslation(),New.GetTranslation()),*Socket->GetSocketLocalTransform().ToString()));
 }
 return Result;
}

bool UProject_JPlayerLocomotionLibrary::MigrateGameplaySockets(USkeletalMesh* Previous, USkeletalMesh* Target, const TArray<FName>& RequiredSockets)
{
 if (!Previous || !Target || Previous==Target) return false;
 TArray<const USkeletalMeshSocket*> Sockets;
 for (FName Name : RequiredSockets)
 {
  const auto* Socket=Previous->FindSocket(Name);
  if (!Socket) return false;
  Sockets.AddUnique(Socket);
 }
 // Validate the whole requested set before mutating. Mesh sockets override sample
 // skeleton sockets without changing the shared imported skeleton or other users.
 for (const auto* Socket : Sockets)
 {
  if (!Socket) continue;
  if (Target->GetRefSkeleton().FindBoneIndex(Socket->BoneName)==INDEX_NONE) return false;
  if (const auto* Existing=Target->FindSocket(Socket->SocketName))
   if (Existing->BoneName!=Socket->BoneName || !Existing->GetSocketLocalTransform().Equals(Socket->GetSocketLocalTransform(),.001f)) return false;
 }
 Target->Modify();
 for (const auto* Socket : Sockets)
 {
  if (!Socket) continue;
  if (Target->FindSocket(Socket->SocketName)) continue;
  auto* Copy=DuplicateObject<USkeletalMeshSocket>(Socket,Target);
  if (!Copy) return false;
  Target->GetMeshOnlySocketList().Add(Copy);
 }
 Target->MarkPackageDirty();
 return true;
}

bool UProject_JPlayerLocomotionLibrary::ConfigureGameplayAvatar(UBlueprint* Avatar, USkeletalMesh* SourceMesh, UAnimBlueprint* Foundation)
{
 if (!Avatar || !SourceMesh || !Foundation || !Foundation->GeneratedClass || Foundation->Status == BS_Error) return false;
 auto* Defaults = Avatar->GeneratedClass ? Cast<AProject_JMoverPawn>(Avatar->GeneratedClass->GetDefaultObject()) : nullptr;
 if (!Defaults) return false;
 auto* Mover=Cast<UProject_JMoverComponent>(Defaults->GetMover()); if (!Mover) return false;
 UClass* WalkingClass=Mover->GroundWalkingMode.LoadSynchronous();
 if(!WalkingClass) return false;
 auto* CurrentWalking=Mover->MovementModes.FindRef(DefaultModeNames::Walking).Get();
 if(!CurrentWalking || CurrentWalking->GetClass()!=WalkingClass)
  Mover->MovementModes.Add(DefaultModeNames::Walking,NewObject<UBaseMovementMode>(Mover,WalkingClass,NAME_None,RF_Public|RF_Transactional|RF_ArchetypeObject));
 if(!Cast<UProject_JFallingMode>(Mover->MovementModes.FindRef(DefaultModeNames::Falling)))
  Mover->MovementModes.Add(DefaultModeNames::Falling,NewObject<UProject_JFallingMode>(Mover,NAME_None,RF_Public|RF_Transactional|RF_ArchetypeObject));
 FPropertyChangedEvent ModesChanged(FindFProperty<FProperty>(UMoverComponent::StaticClass(),GET_MEMBER_NAME_CHECKED(UMoverComponent,MovementModes)));
 static_cast<UObject*>(Mover)->PostEditChangeProperty(ModesChanged);
 Defaults->GetMesh()->SetSkeletalMeshAsset(SourceMesh);
 Defaults->GetMesh()->SetAnimInstanceClass(Foundation->GeneratedClass);
 Defaults->GetMesh()->ComponentTags.AddUnique(TEXT("ProjectJ.PoseSource"));
 if (Defaults->IsA<AProject_JPlayerCharacter>()) FinalizePlayerAvatarTemplates(Avatar);
 Avatar->MarkPackageDirty();
 return true;
}


UChooserTable* UProject_JPlayerLocomotionLibrary::DuplicateProjectAirChooser(UChooserTable* Template, const FString& DestinationPackage)
{
 if (!Template || Template->ContextData.Num() != 3 ||
     !FPackageName::IsValidLongPackageName(DestinationPackage) || FPackageName::DoesPackageExist(DestinationPackage)) return nullptr;
 const UScriptStruct* Expected[] = {FProject_JPlayerAirQuery::StaticStruct(), FChooserPlayerSettings::StaticStruct(), FProject_JStateControllerChooserOutput::StaticStruct()};
 for (int32 Index = 0; Index < 3; ++Index)
 {
  const auto* Context = Template->ContextData[Index].GetPtr<FContextObjectTypeStruct>();
  if (!Context || Context->Struct != Expected[Index]) return nullptr;
 }
 auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
 // Duplicate the self-contained typed tree, including row output metadata and nested ownership.
 // No obsolete AnimInstance-property chooser or binding-name conversion is involved.
 auto* Copy = Cast<UChooserTable>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(DestinationPackage),
     FPackageName::GetLongPackagePath(DestinationPackage), Template));
 if (!Copy) return nullptr;
 Copy->Compile(true);
 for (UChooserTable* Nested : Copy->NestedChoosers) if (Nested) Nested->Compile(true);
 Copy->MarkPackageDirty();
 return Copy;
}

bool UProject_JPlayerLocomotionLibrary::RestoreFoundationFallOff(UAnimBlueprint* Target)
{
 if(!Target || !Target->GeneratedClass) return false;
 const auto* Defaults=Cast<UProject_JPlayerAnimInstance>(Target->GeneratedClass->GetDefaultObject());
 if(!Defaults)return false;
 for(FName Name:{FName(TEXT("OnStateEntry_InAirTransition")),FName(TEXT("OnStateEntry_InAirLoop"))})
 {
  const bool bLoop=Name==TEXT("OnStateEntry_InAirLoop");
  const FName Function=bLoop?GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EnterNativeAirLoop):GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,EnterNativeAirTransition);
  if(!ReplaceVoidFunction(Target,Name,UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(Function))) return false;
 }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target);FKismetEditorUtilities::CompileBlueprint(Target);
 Target->MarkPackageDirty();return Target->Status!=BS_Error;
}

bool UProject_JPlayerLocomotionLibrary::InstallProjectAir(UAnimBlueprint* Target,UChooserTable* Chooser)
{
 if (!Target || !Chooser) return false;
 auto* Function=UProject_JPlayerAnimInstance::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,BeginProjectAir));
 for (FName Name : {FName(TEXT("OnStateEntry_InAirTransition")),FName(TEXT("OnStateEntry_InAirLoop"))})
 {
  if (!ReplaceVoidFunction(Target,Name,Function)) return false;
  auto* Graph=FindFoundationFunctionGraph(Target,Name);
  for (UEdGraphNode* Node : Graph->Nodes) if (auto* Call=Cast<UK2Node_CallFunction>(Node))
   if (Call->FunctionReference.GetMemberName()==Function->GetFName())
    if (auto* Loop=Call->FindPin(TEXT("bLoop"))) Loop->DefaultValue=Name==TEXT("OnStateEntry_InAirLoop")?TEXT("true"):TEXT("false");
 }
 auto* Graph=FindFoundationFunctionGraph(Target,TEXT("AnimGraph")); if (!Graph) return false;
 bool bFound=false;
 for (UEdGraphNode* Node : Graph->Nodes) if (auto* Stack=Cast<UAnimGraphNode_BlendStack>(Node); Stack && Stack->GetFName()==TEXT("AnimGraphNode_BlendStack_3"))
 { Stack->UpdateFunction.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,UpdateProjectAir),UProject_JPlayerAnimInstance::StaticClass()); bFound=true; }
 if (!bFound) return false;
 // The original rule reads the stack's previous ground asset on the entry frame.
 // Qualify completion against this episode's actual selected air player instead.
 TArray<UEdGraph*> AllGraphs; Target->GetAllGraphs(AllGraphs);
 for (UEdGraph* Rule : AllGraphs) for (UEdGraphNode* Node : Rule->Nodes)
  if (auto* Call=Cast<UK2Node_CallFunction>(Node); Call && (Call->FunctionReference.GetMemberName()==TEXT("IsAnimationAlmostComplete") || Call->FunctionReference.GetMemberName()==GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,IsProjectAirTransitionComplete)))
  {
   const auto* Transition=Cast<UAnimStateTransitionNode>(Rule->GetOuter());
   const bool bAirRule=Transition && Transition->GetPreviousState() && Transition->GetPreviousState()->GetStateName().Contains(TEXT("In Air"));
   if (bAirRule) Call->FunctionReference.SetExternalMember(GET_FUNCTION_NAME_CHECKED(UProject_JPlayerAnimInstance,IsProjectAirTransitionComplete),UProject_JPlayerAnimInstance::StaticClass());
   else Call->FunctionReference.SetSelfMember(TEXT("IsAnimationAlmostComplete"));
   Call->ReconstructNode();
  }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target); FKismetEditorUtilities::CompileBlueprint(Target);
 if (Target->Status==BS_Error) return false;
 auto* Defaults=CastChecked<UProject_JPlayerAnimInstance>(Target->GeneratedClass->GetDefaultObject());
 Defaults->AirAnimationChooser=Chooser;
 const auto* StateProperty=FindFProperty<FEnumProperty>(Target->GeneratedClass,GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance,PlayerStateMachineState));
 if(!StateProperty || StateProperty->GetEnum()!=StaticEnum<EProject_JPlayerPlaybackState>())return false;
 Target->MarkPackageDirty(); return RestoreFoundationFallOff(Target);
}

TArray<FString> UProject_JPlayerLocomotionLibrary::InspectProjectAirCoverage(UChooserTable* Chooser)
{
 TArray<FString> Result;
 for (auto Mode : {EProject_JLocomotionRotationMode::OrientToMovement,EProject_JLocomotionRotationMode::Strafe})
 for (auto Direction : {EProject_JStateControllerStrafeDirection::Forward,EProject_JStateControllerStrafeDirection::ForwardLeft,EProject_JStateControllerStrafeDirection::Left,EProject_JStateControllerStrafeDirection::BackwardLeft,EProject_JStateControllerStrafeDirection::Backward,EProject_JStateControllerStrafeDirection::BackwardRight,EProject_JStateControllerStrafeDirection::Right,EProject_JStateControllerStrafeDirection::ForwardRight})
 {
  FProject_JPlayerAirQuery Query; Query.State=EProject_JStateControllerPresentationState::TransitionToInAir; Query.RotationMode=Mode; Query.Gait=EProject_JLocomotionGaitIntent::Run; Query.Foot=EProject_JStateControllerFoot::Left; Query.Direction=Direction;
  FChooserPlayerSettings Settings; FProject_JStateControllerChooserOutput Output;
  FChooserEvaluationContext Context; Context.AddStructParam(Query);Context.AddStructParam(Settings);Context.AddStructParam(Output);
  UObject* Asset=nullptr;
  UChooserTable::EvaluateChooser(Context,Chooser,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Candidate){Asset=Candidate;return FObjectChooserBase::EIteratorStatus::Stop;}));
  Result.Add(FString::Printf(TEXT("Mode=%d Direction=%d Asset=%s Start=%.3f Blend=%.3f"),int32(Mode),int32(Direction),*GetPathNameSafe(Asset),Output.StartTime,Output.BlendTime));
 }
 return Result;
}

int32 UProject_JPlayerLocomotionLibrary::CleanImportedBlueprintReferences(UBlueprint* Source,UBlueprint* Target)
{
 if (!Target || (!Target->GetPathName().StartsWith(TEXT("/Game/GASP58Native/")) && !Target->GetPathName().StartsWith(TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master")))) return INDEX_NONE;
 Target->Modify(); int32 Changed=0;
 if(Source && Source!=Target && Source->GeneratedClass && Target->GeneratedClass)
 {
  TMap<UObject*,UObject*> Map{{Source,Target},{Source->GeneratedClass,Target->GeneratedClass}};
  if(Source->SkeletonGeneratedClass && Target->SkeletonGeneratedClass) Map.Add(Source->SkeletonGeneratedClass,Target->SkeletonGeneratedClass);
  Map.Add(Source->GeneratedClass->GetDefaultObject(),Target->GeneratedClass->GetDefaultObject());
  const auto Flags=EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef;
  FArchiveReplaceObjectRef<UObject> BlueprintRefs(Target,Map,Flags); Changed+=BlueprintRefs.GetCount();
  FArchiveReplaceObjectRef<UObject> ClassRefs(Target->GeneratedClass,Map,Flags); Changed+=ClassRefs.GetCount();
 }
 TArray<UEdGraph*> Graphs; Target->GetAllGraphs(Graphs);
 for(UEdGraph* Graph:Graphs) for(UEdGraphNode* Node:Graph->Nodes)
  if(auto* Entry=Cast<UK2Node_FunctionEntry>(Node))
  {
   TArray<FName> Unused;
   for(const auto& Var:Entry->LocalVariables) if(!FBlueprintEditorUtils::IsVariableUsed(Target,Var.VarName,Graph)) Unused.Add(Var.VarName);
   if(!Unused.IsEmpty())
   {
    Entry->Modify(); Changed+=Entry->LocalVariables.RemoveAll([&](const FBPVariableDescription& Var){return Unused.Contains(Var.VarName);});
   }
  }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target); FBlueprintEditorUtils::RefreshAllNodes(Target);
 FKismetEditorUtilities::CompileBlueprint(Target); Target->MarkPackageDirty();
 return Target->Status==BS_Error?INDEX_NONE:Changed;
}

int32 UProject_JPlayerLocomotionLibrary::RebindGameplayRetargeter(UAnimBlueprint* Target,UIKRetargeter* Previous,UIKRetargeter* Replacement)
{
 if(!Target || !Previous || !Replacement || Previous==Replacement || !Target->GetPathName().StartsWith(TEXT("/Game/Animation_Logic/ABPs/GreatSword/"))) return INDEX_NONE;
 Target->Modify(); TMap<UObject*,UObject*> Map{{Previous,Replacement}};
 const auto Flags=EArchiveReplaceObjectFlags::IgnoreOuterRef|EArchiveReplaceObjectFlags::IgnoreArchetypeRef;
 FArchiveReplaceObjectRef<UObject> GraphReferences(Target,Map,Flags);
 int32 Changed=GraphReferences.GetCount();
 if(Target->GeneratedClass)
 {
  FArchiveReplaceObjectRef<UObject> ClassReferences(Target->GeneratedClass,Map,Flags); Changed+=ClassReferences.GetCount();
  FArchiveReplaceObjectRef<UObject> Defaults(Target->GeneratedClass->GetDefaultObject(),Map,Flags); Changed+=Defaults.GetCount();
 }
 // Object-valued BP variable defaults are exported text, rather than UObject fields.
 for(auto& Var:Target->NewVariables)
  if(Var.VarType.PinCategory==UEdGraphSchema_K2::PC_Object && Var.VarType.PinSubCategoryObject.Get()==UIKRetargeter::StaticClass() && Var.DefaultValue.Contains(Previous->GetPathName()))
  { Var.DefaultValue=Var.DefaultValue.Replace(*Previous->GetPathName(),*Replacement->GetPathName()); ++Changed; }
 FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Target); FBlueprintEditorUtils::RefreshAllNodes(Target);
 FKismetEditorUtilities::CompileBlueprint(Target); Target->MarkPackageDirty();
 return Target->Status==BS_Error?INDEX_NONE:Changed;
}
