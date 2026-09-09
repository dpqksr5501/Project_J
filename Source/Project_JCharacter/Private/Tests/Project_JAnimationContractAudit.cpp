#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSubsystem_Base.h"
#include "Animation/AnimInstance.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JPlayerCharacter.h"

// Read only, narrow authored-asset audit. Counts identify graph paths to inspect;
// they are neither a worker scheduling measurement nor proof of whole-graph FastPath.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationContractAudit, "ProjectJ.GroupB.AnimationContractAudit",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationContractAudit::RunTest(const FString&)
{
 auto* CharacterClass = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
 auto* Style = LoadObject<UProject_JCombatStyleDefinition>(nullptr, TEXT("/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword.DA_CombatStyle_Greatsword"));
 if (!TestNotNull(TEXT("Character fixture"), CharacterClass) || !TestNotNull(TEXT("Combat style fixture"), Style) || !TestNotNull(TEXT("Weapon animation profile"), Style->WeaponAnimationProfile.Get())) { return false; }
 auto* Character = CastChecked<AProject_JPlayerCharacter>(CharacterClass->GetDefaultObject());
 TArray<UClass*> Classes { Character->GetMesh()->GetAnimClass(), Style->WeaponAnimationProfile->CombatAnimationLayerClass.LoadSynchronous() };
 for (auto* Class : Classes)
 {
  if (!TestNotNull(TEXT("Authored animation class"), Class)) { continue; }
  const auto* CDO = CastChecked<UAnimInstance>(Class->GetDefaultObject());
  const auto* Interface = IAnimClassInterface::GetFromClass(Class);
  const auto* Subsystem = Interface ? Interface->FindSubsystem<FAnimSubsystem_Base>() : nullptr;
  int32 Handlers = 0, Bound = 0, Copies = 0, CopyOnly = 0;
  if (Subsystem)
  {
   for (const auto& Handler : Subsystem->GetExposedValueHandlers())
   {
    ++Handlers;
    const auto* Type = Handler.GetHandlerStruct(); const auto* Data = Handler.GetHandler();
    if (!Type || !Data) { continue; }
    bool bBound = false;
    if (Type->IsChildOf(FAnimNodeExposedValueHandler_Base::StaticStruct()))
    {
     const auto* Base = static_cast<const FAnimNodeExposedValueHandler_Base*>(Data);
     bBound = !Base->BoundFunction.IsNone(); Bound += bBound;
     if (bBound) { AddInfo(FString::Printf(TEXT("AnimAudit BoundFunction Class=%s Function=%s"), *Class->GetPathName(), *Base->BoundFunction.ToString())); }
    }
    if (Type->IsChildOf(FAnimNodeExposedValueHandler_PropertyAccess::StaticStruct()))
    {
     const auto* Access = static_cast<const FAnimNodeExposedValueHandler_PropertyAccess*>(Data);
     Copies += Access->CopyRecords.Num();
     CopyOnly += !bBound && !Access->CopyRecords.IsEmpty();
    }
   }
  }
  const auto* ThreadSafe = Class->FindFunctionByName(TEXT("BlueprintThreadSafeUpdateAnimation"));
  AddInfo(FString::Printf(TEXT("AnimAudit Class=%s MultiThreadedUpdate=%d RootMotionMode=%d Nodes=%d Handlers=%d BoundFunctions=%d PropertyCopyRecords=%d CopyOnlyHandlers=%d ThreadSafeBlueprintBytes=%d"),
   *Class->GetPathName(), CDO->bUseMultiThreadedAnimationUpdate, int32(CDO->RootMotionMode), Interface ? Interface->GetAnimNodeProperties().Num() : 0,
   Handlers, Bound, Copies, CopyOnly, ThreadSafe ? ThreadSafe->Script.Num() : 0));
 }
 return true;
}
#endif
