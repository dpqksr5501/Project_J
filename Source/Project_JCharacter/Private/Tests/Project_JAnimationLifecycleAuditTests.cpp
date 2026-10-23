#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Project_JGreatswordCharacter.h"
#include "Tests/Project_JMoverFrameFixture.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRetargetQualitySourceLifetime,
 "ProjectJ.Animation.RetargetQualitySourceLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRetargetQualitySourceLifetime::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 ON_SCOPE_EXIT { World->DestroyWorld(false); };
 auto* Pawn=World->SpawnActor<AProject_JMoverPawn>();
 auto* Source=Pawn->GetMesh();
 auto* Asset=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
 if(!TestNotNull(TEXT("Source mesh fixture"),Asset)) return false;
 Source->SetSkeletalMeshAsset(Asset);
 Source->SetAnimInstanceClass(UProject_JCharacterAnimInstance::StaticClass());
 auto* Native=Cast<UProject_JCharacterAnimInstance>(Source->GetAnimInstance());
 if(!TestNotNull(TEXT("Real native source"),Native)) return false;
 Native->NativeUpdateAnimation(1.f/60.f);
 auto* Visual=NewObject<USkeletalMeshComponent>(Pawn);
 Visual->SetupAttachment(Source);Visual->RegisterComponent();
 auto* Retarget=NewObject<UProject_JRetargetAnimInstance>(Visual);
 Retarget->NativeInitializeAnimation();Retarget->NativeUpdateAnimation(1.f/60.f);
 TestEqual(TEXT("Follower captures the unrendered source's policy"),Retarget->CurrentQualityTier,EProject_JAnimBudgetTier::Hidden);
 TestFalse(TEXT("Hidden source disables the follower"),Retarget->bEnableFollowerRetarget);
 Source->SetAnimInstanceClass(nullptr);
 Retarget->NativeUpdateAnimation(1.f/60.f);
 TestEqual(TEXT("A missing policy producer uses standalone quality, not the previous producer's tier"),Retarget->CurrentQualityTier,EProject_JAnimBudgetTier::Local);
 TestTrue(TEXT("Missing producer cannot strand retarget/hand/foot IK in Hidden"),Retarget->bEnableFollowerRetarget && Retarget->bTierAllowsHandIK && Retarget->bTierAllowsFootIK && Retarget->bTierAllowsRetargetIK);
 TestEqual(TEXT("Standalone retarget restores its LOD contract"),Retarget->RetargetLODThreshold,-1);
 Retarget->NativeUninitializeAnimation();
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerAnimationSourceLifetime,
 "ProjectJ.PlayerLocomotion.SourceBindingLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerAnimationSourceLifetime::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());
 World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
 ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
 auto* First=World->SpawnActor<AProject_JGreatswordCharacter>();
 auto* Next=World->SpawnActor<AProject_JGreatswordCharacter>();
 Next->SetActorLocation(FVector(10000,0,0));
 if(!TestTrue(TEXT("First source publishes"),ProjectJ::Tests::PublishMoverFrame(First,MOVE_Walking,FVector(250,0,0))) ||
    !TestTrue(TEXT("Next source publishes"),ProjectJ::Tests::PublishMoverFrame(Next,MOVE_Walking,FVector(0,400,0)))) return false;
 auto* Anim=NewObject<UProject_JPlayerAnimInstance>(First->GetMesh());
 Anim->NativeInitializeAnimation();Anim->NativeUpdateAnimation(1.f/60.f);Anim->NativeThreadSafeUpdateAnimation(1.f/60.f);
 const FVector FirstPosition=Anim->GetFoundationTrajectory().GetSampleAtTime(0.f).Position;
 // Retained instances already refresh owner references and based movement on a
 // source change. Trajectory prediction must follow that same source boundary.
 FMoverInputCmdContext Input;
 auto& Direction=Input.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
 Direction.SetMoveInput(EMoveInputType::DirectionalIntent,FVector::RightVector);
 Direction.OrientationIntent=FVector::RightVector;
 const auto State=Next->GetMover()->GetSyncState();
 Next->GetMover()->TickInterpolatedSimProxy(FMoverTimeStep(),Input,Next->GetMover(),State,State,FMoverAuxStateContext());
 Anim->Rename(nullptr,Next->GetMesh(),REN_DontCreateRedirectors | REN_NonTransactional);
 Anim->NativeUpdateAnimation(1.f/60.f);Anim->NativeThreadSafeUpdateAnimation(1.f/60.f);
 const FVector NextPosition=Anim->GetFoundationTrajectory().GetSampleAtTime(0.f).Position;
 TestTrue(TEXT("A changed source cannot keep the old trajectory origin"),FVector::DistSquared(FirstPosition,NextPosition)>FMath::Square(5000.));
 TestTrue(TEXT("Current properties and trajectory share the new source"),FVector::DistSquared(Anim->PlayerCharacterProperties.ActorTransform.GetLocation(),NextPosition)<FMath::Square(300.));
 TestTrue(TEXT("Source replacement seeds derivatives instead of inventing acceleration"),Anim->PlayerVelocityAcceleration.IsNearlyZero());
 const double ExpectedTurn=(Anim->PlayerTrajectoryFutureVelocity.Rotation()-Anim->PlayerCharacterProperties.Velocity.Rotation()).GetNormalized().Yaw;
 TestTrue(TEXT("The first new-source trajectory compares against its own velocity, never the retired source"),FMath::IsNearlyEqual(Anim->PlayerTrajectoryTurnAngle,ExpectedTurn,.001));
 Anim->NativeUninitializeAnimation();
 return true;
}
#endif
