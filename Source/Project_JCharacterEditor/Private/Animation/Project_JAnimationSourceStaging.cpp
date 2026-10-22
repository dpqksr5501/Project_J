#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimSequence.h"
#include "Editor.h"
#include "Misc/PackageName.h"
#include "PoseSearch/PoseSearchAnimNotifies.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/LinkerInstancingContext.h"
#include "UObject/Package.h"

TArray<UAnimSequence*> UProject_JPlayerLocomotionLibrary::PrepareStagedAnimationSources(
 const TArray<FString>& Packages, const TMap<FString,FString>& Remaps)
{
 check(IsInGameThread());
 if ((GEditor && GEditor->PlayWorld) || Packages.IsEmpty()) return {};
 const FString StagingRoot=TEXT("/Game/GASP58Native/Characters/UEFN_Mannequin/Animations/");
 for (const FString& Path : Packages)
  if (!Path.StartsWith(StagingRoot) || Path.Contains(TEXT("..")) || FindPackage(nullptr,*Path) ||
      !FPackageName::DoesPackageExist(Path)) return {};
 FLinkerInstancingContext Context;
 for (const auto& Pair : Remaps)
 {
  if (!FPackageName::IsValidLongPackageName(Pair.Key) || !Pair.Key.StartsWith(TEXT("/Game/")) ||
      !FPackageName::IsValidLongPackageName(Pair.Value,true) ||
      !(Pair.Value.StartsWith(TEXT("/Game/")) || Pair.Value.StartsWith(TEXT("/Temp/PlayerAnimationImport/")))) return {};
  Context.AddPackageMapping(FName(*Pair.Key),FName(*Pair.Value));
 }
 // Source BranchIn notifies must never pull in an upstream PSD and its entire
 // motion bank. Private in-memory sinks preserve linker object types until the
 // explicit registration notify is removed. Production databases are untouched.
 for (const auto& Pair : Remaps)
  if (Pair.Value.StartsWith(TEXT("/Temp/PlayerAnimationImport/")))
  {
   const FName Name(*FPackageName::GetShortName(Pair.Key));
   if (!Name.ToString().StartsWith(TEXT("PSD_"))) return {};
   UPackage* Sink=CreatePackage(*Pair.Value);
   if (!FindObject<UPoseSearchDatabase>(Sink,*Name.ToString()))
    NewObject<UPoseSearchDatabase>(Sink,Name,RF_Transient);
  }
 // Blueprint-generated notify classes can resolve their parents in PostLoad.
 // Instantiate the explicitly mapped authoring classes with this context first,
 // rather than allowing a later unscoped LoadClass to strip animation payloads.
 for (const auto& Pair : Remaps)
  if (Pair.Value.StartsWith(TEXT("/Game/GASP58Native/AnimNotifies/")) &&
      !FindPackage(nullptr,*Pair.Value) &&
      !LoadPackage(nullptr,*Pair.Value,LOAD_None,nullptr,&Context)) return {};
 TArray<UAnimSequence*> Result;
 for (const FString& Path : Packages)
 {
  UPackage* Package=LoadPackage(nullptr,*Path,LOAD_None,nullptr,&Context);
  auto* Sequence=Package ? FindObject<UAnimSequence>(Package,*FPackageName::GetShortName(Path)) : nullptr;
  if (!Sequence) return {};
  Sequence->Modify();
  Sequence->Notifies.RemoveAll([](const FAnimNotifyEvent& Event)
   {return Event.NotifyStateClass && Event.NotifyStateClass->IsA<UAnimNotifyState_PoseSearchBranchIn>();});
  Sequence->RefreshCacheData();
  Sequence->MarkPackageDirty();
  Result.Add(Sequence);
 }
 return Result;
}
