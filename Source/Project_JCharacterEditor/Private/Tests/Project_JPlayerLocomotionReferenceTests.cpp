#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/StrongObjectPtr.h"
#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Chooser.h"
#include "ChooserPropertyAccess.h"
#include "Curves/CurveFloat.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Project_JGreatswordCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJReferenceLocomotionQueries,
 "ProjectJ.PlayerLocomotion.ReferenceQueryParity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJReferenceLocomotionQueries::RunTest(const FString&)
{
 FString Text;
 TSharedPtr<FJsonObject> Reference;
 if (!TestTrue(TEXT("Independent Blueprint reference is available"),
  FFileHelper::LoadFileToString(Text, *FPaths::Combine(FPaths::ProjectDir(), TEXT("Scripts/Tests/Data/PlayerLocomotionReferenceQueries.json"))) &&
  FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Reference) && Reference.IsValid())) return false;
 const auto& Cases = Reference->GetArrayField(TEXT("cases"));
 TestEqual(TEXT("All captured boundary cases remain present"), Cases.Num(), 91);
 TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>());
 for (const auto& Value : Cases)
 {
  const auto Row = Value->AsObject();
  const FString Label = Row->GetStringField(TEXT("case"));
  TStrongObjectPtr<UProject_JPlayerAnimInstance> Anim(NewObject<UProject_JPlayerAnimInstance>(Mesh.Get()));
  const auto Inputs = Row->GetObjectField(TEXT("inputs"));
  auto Vector = [](const TSharedPtr<FJsonValue>& Input)
  {
   const auto& A = Input->AsArray();
   return FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber());
  };
  // Typed test inputs only: do not expose a runtime setter or reflect BP fields.
  for (const auto& Input : Inputs->Values)
  {
   const auto& Key = Input.Key; const auto& Data = Input.Value;
   if (Key == TEXT("Speed2D")) Anim->PlayerSpeed2D = Data->AsNumber();
   else if (Key == TEXT("FutureFacingDelta")) Anim->PlayerFutureFacingDelta = Data->AsNumber();
   else if (Key == TEXT("MovementMode")) Anim->PlayerMovementMode = static_cast<EProject_JPlayerMovementMode>(int32(Data->AsNumber()));
   else if (Key == TEXT("MovementMode_LastFrame")) Anim->PlayerMovementModeLastFrame = static_cast<EProject_JPlayerMovementMode>(int32(Data->AsNumber()));
   else if (Key == TEXT("Velocity_LastFrame")) Anim->PlayerVelocityLastFrame = Vector(Data);
   else if (Key == TEXT("OffsetRootBoneEnabled")) Anim->PlayerOffsetRootBoneEnabled = Data->AsBool();
   else if (Key == TEXT("CharacterTransform")) Anim->PlayerCharacterTransform = FTransform(Vector(Data));
   else if (Key == TEXT("CharacterTransform_LastFrame")) Anim->PlayerCharacterTransformLastFrame = FTransform(Vector(Data));
   else if (Key == TEXT("LastNonZeroVelocity")) Anim->PlayerLastNonZeroVelocity = Vector(Data);
   else if (Key == TEXT("Trj_NearFutureVelocity")) Anim->PlayerTrajectoryNearFutureVelocity = Vector(Data);
   else if (Key == TEXT("Trj_CurrentAngularVelocity")) Anim->PlayerTrajectoryCurrentAngularVelocity = Vector(Data);
   else AddError(Label + TEXT(": unknown reference input ") + Key);
  }
  const auto Expected = Row->GetObjectField(TEXT("queries"));
  auto Check = [&](const TCHAR* Name, bool Actual) { TestEqual(Label + TEXT("/") + Name, Actual, Expected->GetBoolField(Name)); };
  Check(TEXT("ShouldTurnInPlace"), Anim->EvaluateTurnInPlace());
  Check(TEXT("ShouldSpinTransition"), Anim->EvaluateSpinTransition());
  Check(TEXT("JustLanded_Light"), Anim->EvaluateLightLanding());
  Check(TEXT("JustLanded_Heavy"), Anim->EvaluateHeavyLanding());
  Check(TEXT("AllowFootPinning"), Anim->EvaluateFootPinning());
  Check(TEXT("AllowSlopeWarping"), Anim->EvaluateSlopeWarping());
  Check(TEXT("JustTeleported"), Anim->EvaluateTeleport());
  TestEqual(Label + TEXT("/root translation half life"), Anim->EvaluateRootTranslationHalfLife(), Expected->GetNumberField(TEXT("Get_OffsetRootTranslationHalfLife")));
  const FString Space = Anim->EvaluateOrientationWarpingSpace() == EOrientationWarpingSpace::RootBoneTransform ? TEXT("ROOT_BONE_TRANSFORM") : TEXT("COMPONENT_TRANSFORM");
  TestEqual(Label + TEXT("/orientation space"), Space, Expected->GetStringField(TEXT("Get_OrientationWarpingWarpingSpace")));
  const auto& A = Expected->GetArrayField(TEXT("Get_StrafeWarpDirection"));
  TestTrue(Label + TEXT("/strafe warp direction"), Anim->EvaluateStrafeWarpDirection().Equals(FVector(A[0]->AsNumber(), A[1]->AsNumber(), A[2]->AsNumber()), 1.e-5));
 }
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNonCosmeticAnimationCallbacks,
 "ProjectJ.PlayerLocomotion.NonCosmeticWeaponCallbacks", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJNonCosmeticAnimationCallbacks::RunTest(const FString&)
{
 for (const bool bDedicated : {true, false})
 {
  UWorld* World=UWorld::CreateWorld(EWorldType::PIE,false);
  GEngine->CreateNewWorldContext(EWorldType::PIE).SetCurrentWorld(World);
  World->SetPlayInEditorInitialNetMode(bDedicated?NM_DedicatedServer:NM_Standalone);
  auto* Pawn=World->SpawnActor<AProject_JGreatswordCharacter>();
  auto* Profile=NewObject<UProject_JWeaponPresentationProfile>(World);
  Profile->WeaponActorClass=AActor::StaticClass();
  if (!bDedicated) World->bIsTearingDown=true;
  Pawn->SetCurrentWeaponPresentationProfile(Profile);
  auto* Presentation=Pawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
  Presentation->AttachWeaponToDrawnSocket();
  Presentation->AttachWeaponToSheathedSocket();
  Presentation->ExitCombatPresentation();
  TestNull(TEXT("Server/teardown callbacks cannot create a cosmetic weapon"),Presentation->GetSpawnedWeapon());
  GEngine->DestroyWorldContext(World);World->DestroyWorld(false);
 }
 FAutomationTestExecutionInfo Info;GetExecutionInfo(Info);
 TestEqual(TEXT("An intentionally absent cosmetic is not a missing-socket warning"),Info.GetWarningTotal(),0);
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionSourceLifetime,
 "ProjectJ.PlayerLocomotion.SourceValidityLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLocomotionSourceLifetime::RunTest(const FString&)
{
 TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>());
 TStrongObjectPtr<UProject_JPlayerAnimInstance> Anim(NewObject<UProject_JPlayerAnimInstance>(Mesh.Get()));
 auto Seed = [&]() { Anim->PlayerHasOwningActor = Anim->PlayerHasLocomotionSource = Anim->PlayerUseThreadSafeUpdateAnimation = true; };
 auto CheckInvalid = [&]()
 {
  TestFalse(TEXT("Prior owner cannot authorize a new graph update"), Anim->PlayerHasOwningActor);
  TestFalse(TEXT("Prior Mover source does not survive its lifetime"), Anim->PlayerHasLocomotionSource);
  TestFalse(TEXT("No thread-safe calculation is admitted before a new source frame"), Anim->PlayerUseThreadSafeUpdateAnimation);
 };
 Seed(); Anim->NativeInitializeAnimation(); CheckInvalid();
 Anim->PlayerSpeed2D = 321.;
 Anim->UpdateNativeLocomotionLogic();
 TestEqual(TEXT("Invalid source cannot consume stale character properties"), Anim->PlayerSpeed2D, 321.);
 Seed(); Anim->NativeUninitializeAnimation(); CheckInvalid();
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTypedRotationOffsetChooser,
 "ProjectJ.PlayerLocomotion.TypedRotationOffsetChooser", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTypedRotationOffsetChooser::RunTest(const FString&)
{
 auto* Table=LoadObject<UChooserTable>(nullptr,TEXT("/Game/Animation_Logic/PlayerMovement/CHT_RotationOffsetCurve.CHT_RotationOffsetCurve"));
 if (!TestNotNull(TEXT("Production rotation curve Chooser"),Table)) return false;
 const auto* Context=Table->ContextData.Num()==1?Table->ContextData[0].GetPtr<FContextObjectTypeStruct>():nullptr;
 if (!TestTrue(TEXT("Runtime query is a native typed struct"),Context && Context->Struct==FProject_JPlayerRotationOffsetQuery::StaticStruct())) return false;
 // Frozen from the reviewed pre-migration table: six directional rows when
 // not sliding, then forward / knee-slide rows. Test actual Chooser execution.
 const TCHAR* Names[]={TEXT("F"),TEXT("B"),TEXT("LL"),TEXT("LR"),TEXT("RL"),TEXT("RR")};
 for (int32 M=0;M<6;++M) for (int32 D=0;D<6;++D)
 {
  FProject_JPlayerRotationOffsetQuery Query;
  Query.MovementMode=static_cast<EProject_JPlayerMovementMode>(M);
  Query.MovementDirection=static_cast<EProject_JPlayerMovementDirection>(D);
  FChooserEvaluationContext Evaluation;Evaluation.AddStructParam(Query);
  UObject* Selected=nullptr;
  UChooserTable::EvaluateChooser(Evaluation,Table,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Object)
  {Selected=Object;return Object?FObjectChooserBase::EIteratorStatus::Stop:FObjectChooserBase::EIteratorStatus::Failed;}));
  const FString Name=M==2?(D==0?TEXT("F"):TEXT("Slide_Knees")):Names[D];
  const FString Expected=FString::Printf(TEXT("/Game/Animation_Logic/PlayerMovement/Curve_RotationOffset_%s.Curve_RotationOffset_%s"),*Name,*Name);
  TestEqual(FString::Printf(TEXT("Mode %d direction %d retains its authored curve"),M,D),GetPathNameSafe(Selected),Expected);
  TestNotNull(TEXT("Result is an evaluable rotation curve"),Cast<UCurveFloat>(Selected));
 }
 const bool bWasDirty=Table->GetOutermost()->IsDirty();
 TestTrue(TEXT("Authoring migration accepts an already typed table"),UProject_JPlayerLocomotionLibrary::UseNativeRotationOffsetQuery(Table));
 TestEqual(TEXT("Reapplying migration does not dirty the table"),Table->GetOutermost()->IsDirty(),bWasDirty);
 return true;
}
#endif
