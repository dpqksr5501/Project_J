#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Tests/Project_JMoverFrameFixture.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"
#include "Movement/Project_JMoverLifecycle.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionMoverAimPublication,
    "ProjectJ.PlayerLocomotion.AnimationUsesMoverViewRotation",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerLocomotionMoverAimPublication::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    World->GetWorldSettings()->NotifyMatchStarted();
    ON_SCOPE_EXIT
    {
        World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* Player=World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* Class=LoadClass<UProject_JPlayerAnimInstance>(nullptr,TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master_C"));
    if(!TestNotNull(TEXT("Production GASP class"),Class)) return false;
    auto* Anim=NewObject<UProject_JPlayerAnimInstance>(Player->GetMesh(),Class);
    Anim->NativeInitializeAnimation();
    const auto ReadAim=[&]() { return Anim->PlayerCharacterProperties.AimingRotation; };
    TestNull(TEXT("Remote-style presentation has no controller to query"),Player->GetController());
    TestTrue(TEXT("Observer fixture publishes an actual grounded Mover frame"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Walking));
    auto* Mover=Player->GetMover();
    const auto PublishInput=[&](FRotator View)
    {
        FMoverInputCmdContext Input;
        auto& Default=Input.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
        Default.ControlRotation=View;Default.OrientationIntent=FVector::ForwardVector;
        Default.SetMoveInput(EMoveInputType::DirectionalIntent,FVector::ZeroVector);
        const auto State=Mover->GetSyncState();
        Mover->TickInterpolatedSimProxy(FMoverTimeStep(),Input,Mover,State,State,FMoverAuxStateContext());
        const auto* Consumed=Mover->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>();
        TestTrue(TEXT("The real Mover input publication retained the requested view"),Consumed && Consumed->ControlRotation.Equals(View,.001f));
        Anim->NativeUpdateAnimation(1.f/60.f);
    };
    for(const FRotator View:{FRotator(20.f,140.f,0.f),FRotator(-15.f,-179.f,0.f),FRotator(10.f,179.f,0.f)})
    {
        PublishInput(View);
        TestTrue(TEXT("Animation uses the view from the consumed Mover command, including the 180-degree seam"),ReadAim().Equals(View,.001f));
        TestFalse(TEXT("The fixture distinguishes input view from the pawn body fallback"),ReadAim().Equals(Player->GetBaseAimRotation(),.001f));
        auto& Proxy=Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();Proxy.ConsumeQueuedGameThreadData();
        TestEqual(TEXT("Common combat layer uses the same completed pitch"),Proxy.GetThreadSafeData().Aim.AimPitch,float(View.Pitch));
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionStaleFallOffEvent,
    "ProjectJ.PlayerLocomotion.RejectStaleFallOffEvent",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerLocomotionStaleFallOffEvent::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    World->GetWorldSettings()->NotifyMatchStarted();
    ON_SCOPE_EXIT
    {
        World->EndPlay(EEndPlayReason::Quit);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* Player=World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* Events=Player->FindComponentByClass<UProject_JReplicatedAnimEventComponent>();
    auto* Request=Events->FindFunction(TEXT("ServerDispatchEvent"));
    auto* State=FindFProperty<FStructProperty>(Events->GetClass(),TEXT("ReplicatedAnimEvents"));
    if(!TestNotNull(TEXT("Actual server request"),Request) || !TestNotNull(TEXT("Replicated event state"),State)) return false;
    const auto Count=[&]()
    {
        return State->ContainerPtrToValuePtr<FProject_JReplicatedAnimEventState>(Events)->FallOffStartCounter;
    };
    struct FRequest { EProject_JReplicatedAnimEventType EventType; bool bFlag; };
    const auto Send=[&]()
    {
        FRequest Parameters{EProject_JReplicatedAnimEventType::FallOffStart,false};
        Events->ProcessEvent(Request,&Parameters);
    };
    TestTrue(TEXT("Real grounded Mover frame"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Walking));
    Send();
    TestEqual(TEXT("A predicted fall arriving after touchdown cannot reopen remote air"),Count(),0);
    TestTrue(TEXT("Real airborne Mover frame"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Falling));
    Send();
    TestEqual(TEXT("A current physical fall retains its cosmetic event"),Count(),1);
    TestTrue(TEXT("Subsequent touchdown"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Walking));
    Send();
    TestEqual(TEXT("A later stale request cannot manufacture another fall"),Count(),1);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionRemoteLandingOwnership,
    "ProjectJ.PlayerLocomotion.RemoteLandingEventBeforeInterpolatedGround",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerLocomotionRemoteLandingOwnership::RunTest(const FString&)
{
    UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    World->GetWorldSettings()->NotifyMatchStarted();
    ON_SCOPE_EXIT
    {
        World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    auto* Player=World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* State=Player->GetLocomotionAnimStateComponent(); State->RefreshCachedReferences();
    TestTrue(TEXT("Presentation still holds an airborne Mover frame"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Falling,FVector(0,0,-450)));
    State->HandleReplicatedLandingStarted(1,0.f,450.f,false,false,false);
    TestEqual(TEXT("Authority opens one remote landing"),State->LandingPresentationRevision,1);
    State->UpdateState(.1f);
    TestTrue(TEXT("Older interpolated air cannot cancel the confirmed landing lease"),State->bIsLanding);
    TestTrue(TEXT("Interpolation reaches ground"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Walking));
    State->UpdateState(.016f);
    TestEqual(TEXT("Physical touchdown cannot create another remote landing writer"),State->LandingPresentationRevision,1);
    State->HandleReplicatedLandingCancelled(1);
    TestFalse(TEXT("Ordered cancellation releases the same landing"),State->bIsLanding);
    TestTrue(TEXT("Next physical air frame"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Falling,FVector(0,0,-450)));
    State->HandleConfirmedRemoteJump(2,0.f,FVector(0,0,450));
    State->UpdateState(.15f);
    TestTrue(TEXT("Ground can arrive before the next landing event"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Walking));
    State->UpdateState(.016f);
    TestEqual(TEXT("Jump completion also cannot fabricate a remote landing"),State->LandingPresentationRevision,1);
    State->HandleReplicatedLandingStarted(2,0.f,450.f,false,false,false);
    TestEqual(TEXT("The new authority event opens exactly one next landing"),State->LandingPresentationRevision,2);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJConfirmedLandingImpactTest,"ProjectJ.PlayerLocomotion.ConfirmedLandingWithoutSampling",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJConfirmedLandingImpactTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 World->InitializeActorsForPlay(FURL());World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
 ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World); };
 auto* Player=World->SpawnActor<AProject_JGreatswordCharacter>();
 TestTrue(TEXT("Grounded physical frame"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Walking));
 auto* Locomotion=Player->GetLocomotionAnimStateComponent();Locomotion->SetComponentTickEnabled(false);
 Locomotion->LastFallSpeed=Locomotion->VerticalSpeed=0.f;Locomotion->bIsJumping=true;
 auto State=Player->GetMover()->GetSyncState();auto& Life=State.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
 Life.LandingSequence=3;Life.LandingVelocity=FVector(0,0,-900);
 Player->GetMover()->OnPostFinalize.Broadcast(State,FMoverAuxStateContext());
 TestEqual(TEXT("The public landing/foley path receives confirmed impact despite no sampling"),Locomotion->LandStartFallSpeed,900.f);
 TestTrue(TEXT("Confirmed hard impact selects heavy landing"),Locomotion->bUseHeavyLand);
 const auto Revision=Locomotion->LandingPresentationRevision;
 Player->GetMover()->OnPostFinalize.Broadcast(State,FMoverAuxStateContext());
 TestEqual(TEXT("Re-finalizing one physical episode cannot duplicate the landing event"),Locomotion->LandingPresentationRevision,Revision);
 return true;
}
#endif
