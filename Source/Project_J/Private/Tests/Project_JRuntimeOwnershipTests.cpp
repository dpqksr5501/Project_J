#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "GameFramework/WorldSettings.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Mount/Project_JMountItemDefinition.h"
#include "Mount/Project_JMountComponent.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "UObject/UnrealType.h"
#include "Movement/Project_JMoverInputs.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/MemoryReader.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include <limits>
#include "Components/Project_JCombatStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Tests/Project_JAvatarPublicationFixture.h"
#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/MovementUtils.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/CapsuleComponent.h"
#include "Project_JAttributeSet.h"
#include "DefaultMovementSet/Modes/SimpleWalkingMode.h"
#include "Mount/Project_JFlyingMountCharacter.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "System/Project_JCharacterAnimationBudgetSubsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "MoverNativeFlyingTrajectory.h"
#include "DefaultMovementSet/Modes/FlyingMode.h"
#include "MoveLibrary/ConstrainedMoveUtils.h"
#include "InputContainerStruct.h"

#include "Movement/Project_JGroundMovement.h"
#include "MoveLibrary/FloorQueryUtils.h"
#include "MoveLibrary/MoverBlackboard.h"
#include "UObject/StrongObjectPtr.h"
#include "MoveLibrary/RollbackBlackboardLibrary.h"
#include "MoverTypes.h"
#include "Movement/Project_JGroundTrajectoryPrediction.h"
#include "Async/Async.h"
#include "UObject/GarbageCollection.h"

namespace ProjectJRuntimeOwnershipTests
{
// Use the authored locomotion defaults, without starting equipment/BP side effects.
AProject_JSpellbladeCharacter* SpawnAvatar(UWorld* World, AProject_JPlayerState* State)
{
    UClass* AuthoredClass = LoadClass<AProject_JPlayerCharacter>(nullptr,
        TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C"));
    auto* Property = FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), TEXT("CharacterAnimProfile"));
    auto* Authored = AuthoredClass && Property
        ? Cast<UProject_JCharacterAnimProfile>(Property->GetObjectPropertyValue_InContainer(AuthoredClass->GetDefaultObject())) : nullptr;
    if (!Authored) return nullptr;
    auto* Avatar = World->SpawnActorDeferred<AProject_JSpellbladeCharacter>(AProject_JSpellbladeCharacter::StaticClass(),
        FTransform::Identity, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!Avatar) return nullptr;
    auto* Profile = NewObject<UProject_JCharacterAnimProfile>(Avatar);
    Profile->LocomotionProfile = Authored->LocomotionProfile;
    Property->SetObjectPropertyValue_InContainer(Avatar, Profile);
    Avatar->SetPlayerState(State);
    auto* TagProperty = FindFProperty<FStructProperty>(AProject_JPlayerCharacter::StaticClass(), TEXT("SprintAbilityTag"));
    *TagProperty->ContainerPtrToValuePtr<FGameplayTag>(Avatar) = FProject_JGameplayTags::Get().State_Movement_Sprinting;
    Avatar->FinishSpawning(FTransform::Identity);
    return Avatar;
}
void Begin(UWorld* World)
{
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    World->GetWorldSettings()->NotifyMatchStarted();
}
void End(UWorld* World)
{
    World->EndPlay(EEndPlayReason::LevelTransition);
    World->DestroyWorld(false);
    GEngine->DestroyWorldContext(World);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRetiredAvatarSprintTest, "ProjectJ.RuntimeOwnership.RetiredAvatarCannotCancelSprint",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRetiredAvatarSprintTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = World->SpawnActor<AAIController>();
    State->SetOwner(Controller); Controller->SetPlayerState(State);
    auto* First = SpawnAvatar(World, State);
    auto* Second = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("First avatar"), First) || !TestNotNull(TEXT("Second avatar"), Second)) return false;
    Controller->Possess(First);
    auto* ASC = State->GetAbilitySystemComponent();
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    TestTrue(TEXT("Sprint starts on first avatar"), ASC->TryActivateAbility(Handle));
    Controller->UnPossess();
    TestFalse(TEXT("Unpossession retires avatar-owned sprint"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Sprinting));
    ASC->CancelAbilityHandle(Handle);
    Controller->Possess(Second);
    TestTrue(TEXT("New avatar becomes the persistent ASC avatar"), ASC->GetAvatarActor() == Second);
    TestTrue(TEXT("Sprint starts on replacement avatar"), ASC->TryActivateAbility(Handle));
    TestTrue(TEXT("Replacement sprint is active locally"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Sprinting));
    First->StopSprint(); // A delayed old input-release must not affect the replacement.
    TestTrue(TEXT("Old input release preserves new avatar sprint"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Sprinting));
    if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Sprinting))
        TestTrue(TEXT("Restart for independent EndPlay check"), ASC->TryActivateAbility(Handle));
    First->Destroy();
    TestTrue(TEXT("Old EndPlay preserves new avatar sprint"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Sprinting));
    Second->StopSprint();
    TestFalse(TEXT("Current avatar can still stop sprint"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Sprinting));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJDestroyedSummonedMountTest, "ProjectJ.RuntimeOwnership.DestroyedMountCanBeResummonedBeforeGC",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJDestroyedSummonedMountTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = World->SpawnActor<APlayerController>();
    State->SetOwner(Controller); Controller->SetPlayerState(State);
    auto* Avatar = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Avatar"), Avatar)) return false;
    Controller->Possess(Avatar);
    auto* Item = NewObject<UProject_JMountItemDefinition>(State);
    Item->MountClass = AProject_JMountLifecycleFixture::StaticClass();
    Item->bAutoMountAfterSpawn = false;
    Item->SpawnDistance = 400.0f;
    const auto Instance = State->GetInventoryComponent()->AddItemDefinition(Item);
    TestTrue(TEXT("Mount item is owned"), Instance.InstanceId.IsValid());
    auto* Budget = State->FindComponentByClass<UProject_JCommandBudgetComponent>();
    auto* RequestFunction = Avatar->FindFunction(TEXT("ServerRequestUseMountItem"));
    if (!TestNotNull(TEXT("Persistent session admission"), Budget)
        || !TestNotNull(TEXT("Actual mount-item RPC"), RequestFunction)) return false;
    TestTrue(TEXT("Fixture invokes the reliable server entry"),
        RequestFunction->HasAllFunctionFlags(FUNC_Net | FUNC_NetServer | FUNC_NetReliable));
    struct FMountRequest { FGuid ItemInstanceId; } Request{Instance.InstanceId};
    Budget->CommandsPerSecond = 1;
    TestTrue(TEXT("Another command exhausts this session"), Budget->TryConsume());
    Avatar->ProcessEvent(RequestFunction, &Request);
    TestNull(TEXT("Exhausted client command cannot spawn a mount"), Avatar->GetSummonedMount());
    TestEqual(TEXT("Mount RPC consumes the same session rejection accounting"), Budget->GetRejectedCount(), int64(1));
    Avatar->RequestUseMountItem(Instance.InstanceId);
    TestEqual(TEXT("Trusted server work does not pay client quota"), Budget->GetRejectedCount(), int64(1));
    auto* First = Avatar->GetSummonedMount();
    if (!TestNotNull(TEXT("First summoned mount"), First)) return false;
    TestTrue(TEXT("Destroy succeeds"), First->Destroy());
    TestEqual(TEXT("Destroyed mount rejects rider before GC"), First->GetMountEligibilityFailure(Avatar), EProject_JMountEligibilityFailure::MountUnavailable);
    Avatar->RequestUseMountItem(Instance.InstanceId);
    auto* Second = Avatar->GetSummonedMount();
    TestTrue(TEXT("A different live mount is spawned immediately"), IsValid(Second) && Second != First);
    TestTrue(TEXT("No possession transfer into destroyed mount"), Controller->GetPawn() == Avatar);
    if (!TestNotNull(TEXT("Live resummoned mount"), Second)) return false;
    TestTrue(TEXT("Retire before independent domain check"), Second->Destroy());
    Budget->CommandsPerSecond = 8;
    Request.ItemInstanceId.Invalidate();
    Avatar->ProcessEvent(RequestFunction, &Request);
    Request.ItemInstanceId = Instance.InstanceId;
    Avatar->ProcessEvent(RequestFunction, &Request);
    TestNull(TEXT("A synchronous client burst is rejected before a valid spawn"), Avatar->GetSummonedMount());
    TestEqual(TEXT("Burst rejection uses the domain while shared capacity remains"), Budget->GetRejectedCount(), int64(1));
    Avatar->RequestUseMountItem(Instance.InstanceId);
    TestNotNull(TEXT("Trusted server continuation bypasses client domain admission"), Avatar->GetSummonedMount());
    TestEqual(TEXT("Trusted continuation preserves session accounting"), Budget->GetRejectedCount(), int64(1));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRetiredAvatarActivationTest, "ProjectJ.RuntimeOwnership.RetiredAvatarCannotActivateOrObserve",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRetiredAvatarActivationTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = World->SpawnActor<AAIController>();
    State->SetOwner(Controller); Controller->SetPlayerState(State);
    auto* First = SpawnAvatar(World, State);
    auto* Second = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Old avatar"), First) || !TestNotNull(TEXT("Replacement avatar"), Second)) return false;
    Controller->Possess(First);
    auto* ASC = State->GetAbilitySystemComponent();
    FGameplayAbilitySpec SprintSpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1);
    SprintSpec.GetDynamicSpecSourceTags().AddTag(FProject_JGameplayTags::Get().InputTag_Weapon_RMB);
    const auto Handle = ASC->GiveAbility(SprintSpec);
    Controller->Possess(Second);
    const auto Tag = FProject_JGameplayTags::Get().State_Movement_Sprinting;
    auto* FirstCombat = First->FindComponentByClass<UProject_JCombatStateComponent>();
    auto* SecondCombat = Second->FindComponentByClass<UProject_JCombatStateComponent>();
    TestFalse(TEXT("Delayed old-avatar activation cannot start a new-avatar ability"), FirstCombat->TryActivateAbilityByTag(Tag));
    TestFalse(TEXT("Old request leaves the replacement sprint inactive"), ASC->HasMatchingGameplayTag(Tag));
    const auto InputTag = FProject_JGameplayTags::Get().InputTag_Weapon_RMB;
    int32 Received = 0;
    const auto EventHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(InputTag).AddLambda([&](const FGameplayEventData*) { ++Received; });
    auto* ProjectASC = CastChecked<UProject_JAbilitySystemComponent>(ASC);
    ProjectASC->ServerSendCombatInputEvent_Implementation(First, InputTag, 0.f, 1u);
    TestEqual(TEXT("An old-avatar reliable packet cannot enter the replacement's stream"), Received, 0);
    ProjectASC->ServerSendCombatInputEvent_Implementation(Second, InputTag, 0.f, 1u);
    TestEqual(TEXT("Current-avatar authority input remains available"), Received, 1);
    ASC->GenericGameplayEventCallbacks.FindChecked(InputTag).Remove(EventHandle);
    ASC->CancelAbilityHandle(Handle);
    First->HandleSkillInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_RMB);
    TestFalse(TEXT("Retired skill-input component cannot activate another avatar's spec"), ASC->HasMatchingGameplayTag(Tag));
    ASC->CancelAbilityHandle(Handle);
    TestTrue(TEXT("Current avatar retains normal activation"), SecondCombat->TryActivateAbilityByTag(Tag));
    TestFalse(TEXT("Old-avatar state lookup cannot observe another avatar's state"), FirstCombat->HasStateTag(Tag));
    TestTrue(TEXT("Current-avatar state lookup sees its own sprint"), SecondCombat->HasStateTag(Tag));
    Second->HandleSkillInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_RMB);
    First->HandleSkillInputTagReleased(FProject_JGameplayTags::Get().InputTag_Weapon_RMB);
    TestTrue(TEXT("Retired raw release cannot release the replacement's held intent"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    Second->HandleSkillInputTagReleased(FProject_JGameplayTags::Get().InputTag_Weapon_RMB);
    TestFalse(TEXT("Current raw release remains effective"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
    Second->StopSprint();
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSprintSavedMoveTest, "ProjectJ.RuntimeOwnership.SprintMovePrediction",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSprintSavedMoveTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = World->SpawnActor<AAIController>();
    State->SetOwner(Controller); Controller->SetPlayerState(State);
    auto* Avatar = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Avatar"), Avatar)) return false;
    Controller->Possess(Avatar);
    auto* ASC = State->GetAbilitySystemComponent();
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    const auto* Profile = Avatar->GetLocomotionProfile();
    if (!TestNotNull(TEXT("Authored movement policy"), Profile)) return false;
    TestTrue(TEXT("Sprint ability activates"), ASC->TryActivateAbility(Handle));
    FMoverInputCmdContext Sprint;
    Avatar->ProduceInput_Implementation(0, Sprint);
    const auto* Intent = Sprint.InputCollection.FindDataByType<FProject_JMoverInputs>();
    if (!TestNotNull(TEXT("Mover command contains predicted presentation intent"), Intent)) return false;
    TestTrue(TEXT("Authorized sprint is captured in the command"), Intent->bSprint);
    FProject_JMoverInputs Historical = *Intent;
    ASC->CancelAbilityHandle(Handle);
    FMoverInputCmdContext Released;
    Avatar->ProduceInput_Implementation(16, Released);
    const auto* ReleasedIntent = Released.InputCollection.FindDataByType<FProject_JMoverInputs>();
    if (!TestNotNull(TEXT("Released command"), ReleasedIntent)) return false;
    TestFalse(TEXT("Released command ends sprint"), ReleasedIntent->bSprint);
    TestTrue(TEXT("Historical command retains sprint across replay"), Historical.bSprint);
    TestTrue(TEXT("A changed sprint boundary reconciles"), Historical.ShouldReconcile(*ReleasedIntent));
    Historical.bWalk=true;Historical.MovementDirection=5;Historical.RotationOffset=-179.25f;Historical.ControlRotationRate=21599.8f;Historical.JumpRequestId=41;
    TArray<uint8> Bytes;bool bSuccess=false;FMemoryWriter Writer(Bytes);
    TestTrue(TEXT("Ground intent serializes"),Historical.NetSerialize(Writer,nullptr,bSuccess) && bSuccess);
    FProject_JMoverInputs Restored;FMemoryReader Reader(Bytes);
    TestTrue(TEXT("Ground intent deserializes"),Restored.NetSerialize(Reader,nullptr,bSuccess) && bSuccess);
    TestFalse(TEXT("Gait, direction, turn rate and jump identity survive prediction transport"),Historical.ShouldReconcile(Restored));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverCommandAdmissionTest, "ProjectJ.Mover.CommandAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoverCommandAdmissionTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = World->SpawnActor<AAIController>();
    State->SetOwner(Controller); Controller->SetPlayerState(State);
    auto* Avatar = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Avatar"), Avatar)) return false;
    Controller->Possess(Avatar);
    auto* Mover = CastChecked<UProject_JMoverComponent>(Avatar->GetMover());
    auto* ASC = State->GetAbilitySystemComponent();
    const auto* Profile = Avatar->GetLocomotionProfile();
    if (!TestNotNull(TEXT("Authored movement policy"), Profile)) return false;
    FMoverInputCmdContext Historical;
    auto& Requested = Historical.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
    Requested.SetMoveInput(EMoveInputType::Velocity, FVector(5000, 0, 0));
    Requested.SuggestedMovementMode = DefaultModeNames::Flying;
    Requested.bIsJumpPressed = Requested.bIsJumpJustPressed = true;
    auto& Intent = Historical.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>();
    Intent.bSprint = Intent.bStrafe = true;
    Intent.JumpRequestId = 41;
    FMoverTimeStep Step;
    auto Admit = [&]()
    {
        FMoverTickStartData Working;
        Working.InputCmd = Historical;
        Mover->OnPreSimulate(Step, Working);
        return Working;
    };
    const auto Walking = Admit();
    const auto* WalkInput = Walking.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    const auto* WalkIntent = Walking.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
    const auto* GroundMode=Mover->MovementModes.FindRef(DefaultModeNames::Walking).Get();
    if(!TestNotNull(TEXT("Authored GASP walking mode"),GroundMode)) return false;
    TestTrue(TEXT("Authored ground mode inherits the project native walking adapter"),GroundMode->GetClass()->GetSuperClass()==UProject_JGroundWalkingMode::StaticClass());
    TestTrue(TEXT("Native walking retains the engine SmoothWalking solver ancestry"),GroundMode->IsA<USmoothWalkingMode>());
    const auto* GroundHook=GroundMode->FindFunction(TEXT("GenerateWalkMove"));
    TestTrue(TEXT("Authored walking resolves the generated native event branch"),GroundHook && GroundHook->GetOwnerClass()->HasAnyClassFlags(CLASS_Native));
    const auto* FormerView=LoadObject<UScriptStruct>(nullptr,TEXT("/Game/Animation_Logic/PlayerMovement/S_MoverCustomInputs.S_MoverCustomInputs"));
    TestNotNull(TEXT("Former authored input schema remains available for reference"),FormerView);
    TestNull(TEXT("Native ground solver no longer adds a Blueprint input projection"),Walking.InputCmd.InputCollection.FindDataByType(FormerView));
    TestNull(TEXT("Historical input is also free of the retired projection"),Historical.InputCollection.FindDataByType(FormerView));
    TestTrue(TEXT("Player velocity input is rejected into the directional solver"), WalkInput->GetMoveInputType()==EMoveInputType::DirectionalIntent && WalkInput->GetMoveInput().IsZero());
    TestFalse(TEXT("Client cannot enable sprint without GAS"), WalkIntent->bSprint);
    TestFalse(TEXT("Client cannot enable combat Strafe"), WalkIntent->bStrafe);
    TestTrue(TEXT("Client cannot request Flying"), WalkInput->SuggestedMovementMode.IsNone());
    TestEqual(TEXT("Working-copy admission preserves historical velocity"), Requested.GetMoveInput().X, 5000.);
    TestTrue(TEXT("Historical intent is retained for replay"), Intent.bSprint && Intent.bStrafe);
    TestEqual(TEXT("Admission preserves jump request identity"), WalkIntent->JumpRequestId, uint32(41));
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    TestTrue(TEXT("Sprint ability activates"), ASC->TryActivateAbility(Handle));
    const auto Sprinting = Admit();
    TestTrue(TEXT("GAS-approved sprint is admitted"), Sprinting.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>()->bSprint);
    TestEqual(TEXT("Approved sprint respects authored solver speed"), Mover->FindSharedSettings<UCommonLegacyMovementSettings>()->MaxSpeed, Profile->SprintSpeed);
    for (const FVector Forged : {FVector::ZeroVector,FVector(-500,0,0)})
    {
        Requested.SetMoveInput(EMoveInputType::Velocity,Forged);
        const auto Rejected=Admit();
        const auto* Accepted=Rejected.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
        TestTrue(TEXT("Zero and reverse Velocity cannot select the velocity air branch"),Accepted->GetMoveInputType()==EMoveInputType::DirectionalIntent && Accepted->GetMoveInput().IsZero());
    }
    Requested.SetMoveInput(static_cast<EMoveInputType>(255),FVector::ForwardVector);
    const auto Invalid=Admit();
    TestTrue(TEXT("Unknown input enum is rejected"),Invalid.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>()->GetMoveInput().IsZero());
    ASC->CancelAbilityHandle(Handle);
    ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_Attacking);
    const auto Attacking = Admit();
    const auto* AttackInput = Attacking.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    TestFalse(TEXT("Attack blocks held jump at simulation boundary"), AttackInput->bIsJumpPressed);
    TestFalse(TEXT("Attack blocks jump edge at simulation boundary"), AttackInput->bIsJumpJustPressed);
    ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_Attacking);
    Requested.SetMoveInput(EMoveInputType::DirectionalIntent, FVector(10, 0, 0));
    const auto Directional = Admit();
    const auto* DirectionInput = Directional.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    TestTrue(TEXT("Directional commands preserve the acceleration/braking contract"), DirectionInput->GetMoveInputType() == EMoveInputType::DirectionalIntent);
    TestEqual(TEXT("Directional commands cannot amplify analog input"), DirectionInput->GetMoveInput().Size(), 1.);
    TestEqual(TEXT("Directional solver uses approved authored walk speed"), Mover->FindSharedSettings<UCommonLegacyMovementSettings>()->MaxSpeed, Profile->WalkSpeed);
    auto* WalkingMode=CastChecked<USimpleWalkingMode>(Mover->MovementModes.FindRef(DefaultModeNames::Walking));
    Intent.bWalk=true; Admit(); const float WalkLimit=WalkingMode->MaxSpeedOverride;
    Intent.bWalk=false; Admit(); const float RunLimit=WalkingMode->MaxSpeedOverride;
    WalkingMode->MaxSpeedOverride=1.f; Admit();
    TestTrue(TEXT("Walk to Run increases the limit before desired velocity generation"),RunLimit>WalkLimit);
    TestEqual(TEXT("Replaying a command cannot inherit a previous gait override"),WalkingMode->MaxSpeedOverride,RunLimit);
    TestTrue(TEXT("Admission preserves historical directional input"), Requested.GetMoveInput().Equals(FVector(10,0,0)));
    Avatar->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(0,1));
    FMoverInputCmdContext Produced;
    Avatar->ProduceInput_Implementation(16,Produced);
    const auto* PlayerInput=Produced.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    TestTrue(TEXT("Real player producer submits directional intent"), PlayerInput && PlayerInput->GetMoveInputType()==EMoveInputType::DirectionalIntent);
    if (!PlayerInput) return false;
    TestEqual(TEXT("Real player input retains unit magnitude"),PlayerInput->GetMoveInput().Size(),1.);

    // Exercise the engine consumer with the admitted command type. A velocity
    // command would stop instantly and replace jump momentum with 40% speed.
    FComputeVelocityParams Params;
    const auto* Settings=Mover->FindSharedSettings<UCommonLegacyMovementSettings>();
    Params.MoveInputType=DirectionInput->GetMoveInputType();
    Params.InitialVelocity=FVector(Profile->WalkSpeed,0,0);
    Params.DeltaSeconds=.016f;
    Params.MaxSpeed=Settings->MaxSpeed;
    Params.Acceleration=Settings->Acceleration;
    Params.Deceleration=Settings->Deceleration;
    Params.Friction=Settings->GroundFriction;
    Params.MoveInput=Params.MoveDirectionIntent=FVector::ZeroVector;
    const FVector Braking=UMovementUtils::ComputeVelocity(Params);
    TestTrue(TEXT("Released player input produces a real braking interval"),Braking.X>0. && Braking.X<Params.InitialVelocity.X);
    Params.MoveInput=Params.MoveDirectionIntent=FVector(.4,0,0);
    Params.Deceleration=200.f; Params.Friction=0.f;
    const FVector AirVelocity=UMovementUtils::ComputeVelocity(Params);
    TestTrue(TEXT("Air control cannot instantly replace launch momentum"),AirVelocity.X>Params.InitialVelocity.X*.9);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverStrafeSprintAdmissionTest, "ProjectJ.Mover.StrafeSprintCommandAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoverStrafeSprintAdmissionTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State=World->SpawnActor<AProject_JPlayerState>();
    auto* Controller=World->SpawnActor<AAIController>();
    State->SetOwner(Controller); Controller->SetPlayerState(State);
    auto* Avatar=SpawnAvatar(World,State);
    if (!TestNotNull(TEXT("Authored avatar fixture"),Avatar)) return false;
    Controller->Possess(Avatar);
    auto* ProfileProperty=FindFProperty<FObjectPropertyBase>(Avatar->GetClass(),TEXT("CharacterAnimProfile"));
    auto* Profile=CastChecked<UProject_JCharacterAnimProfile>(ProfileProperty->GetObjectPropertyValue_InContainer(Avatar));
    Profile->CombatAnimProfile=NewObject<UProject_JCombatAnimProfile>(Profile);
    Profile->CombatAnimProfile->bAllowSprintInCombat=true;
    Profile->CombatAnimProfile->bPlayIntroMontageWhenEnteringCombat=false;
    auto* ASC=State->GetAbilitySystemComponent();
    ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
    const auto Ability=ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(),1));
    TestTrue(TEXT("Sprint has real GAS permission"),ASC->TryActivateAbility(Ability));
    TestTrue(TEXT("Combat is established"),Avatar->IsCombatModeActive());
    auto* Mover=CastChecked<UProject_JMoverComponent>(Avatar->GetMover());
    const auto* Locomotion=Avatar->GetLocomotionProfile();
    const FVector Directions[]={FVector(1,0,0),FVector(1,1,0),FVector(1,-1,0),FVector(0,1,0),FVector(0,-1,0),FVector(-1,0,0),FVector(-1,1,0),FVector(-1,-1,0),FVector::ZeroVector};
    for (const double Yaw:{0.,90.,179.}) for (int32 Index=0;Index<UE_ARRAY_COUNT(Directions);++Index)
    {
        // Admission must consume the recorded command, not this controller's
        // current rotation, a held-key cache or the client-provided direction enum.
        Controller->SetControlRotation(FRotator(0,Yaw+180.,0));
        FMoverTickStartData Start;
        auto& Input=Start.InputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
        Input.ControlRotation=FRotator(0,Yaw,0);
        Input.SetMoveInput(EMoveInputType::DirectionalIntent,Input.ControlRotation.RotateVector(Directions[Index].GetSafeNormal()));
        auto& Intent=Start.InputCmd.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>();
        Intent.bStrafe=Intent.bSprint=true; Intent.MovementDirection=0;
        // FMoverDataCollection's assignment performs the engine's deep copy;
        // its implicit copy constructor would alias the mutable data blocks.
        FMoverInputCmdContext Historical; Historical=Start.InputCmd;
        Mover->OnPreSimulate(FMoverTimeStep(),Start);
        const auto* Accepted=Start.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
        TestTrue(TEXT("Combat remains Strafe"),Accepted->bStrafe);
        const bool Expected=Index<3;
        TestEqual(TEXT("Simulation admits only forward three Strafe sprint sectors"),Accepted->bSprint,Expected);
        TestEqual(TEXT("Rejected sprint also loses the Sprint physical speed cap"),Mover->FindSharedSettings<UCommonLegacyMovementSettings>()->MaxSpeed,Expected?Locomotion->SprintSpeed:Locomotion->WalkSpeed);
        TestTrue(TEXT("Historical request remains available for prediction replay"),Historical.InputCollection.FindDataByType<FProject_JMoverInputs>()->bSprint);
    }
    TestTrue(TEXT("Directional admission does not cancel the gameplay-owned ability"),ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Movement_Sprinting));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverNumericAdmissionTest, "ProjectJ.Mover.CommandNumericAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoverNumericAdmissionTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* Avatar=World->SpawnActor<AProject_JMoverPawn>();
    auto* Mover=CastChecked<UProject_JMoverComponent>(Avatar->GetMover());
    for (int32 Case=0;Case<3;++Case)
    {
        FMoverTickStartData Start;
        auto& Input=Start.InputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
        Input.SetMoveInput(EMoveInputType::DirectionalIntent,FVector::ForwardVector);
        auto& Intent=Start.InputCmd.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>();
        Intent.bStrafe=true;
        if (Case==0) Intent.RotationOffset=std::numeric_limits<float>::quiet_NaN();
        if (Case==1) Intent.ControlRotationRate=std::numeric_limits<float>::infinity();
        if (Case==2) Intent.MovementDirection=7;
        auto Wire=Intent;
        TArray<uint8> Bytes; bool Success=true;
        FMemoryWriter Writer(Bytes); Wire.NetSerialize(Writer,nullptr,Success);
        FProject_JMoverInputs Received; FMemoryReader Reader(Bytes); Received.NetSerialize(Reader,nullptr,Success);
        TestFalse(TEXT("Malformed custom command is rejected by real transport deserialization"),Success);
        // Local producers and trajectory prediction do not require a wire round
        // trip. The simulation boundary must protect those commands as well.
        Mover->OnPreSimulate(FMoverTimeStep(),Start);
        const auto* Accepted=Start.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>();
        TestTrue(TEXT("Falling/Walking receive finite custom rotation input"),FMath::IsFinite(Accepted->RotationOffset) && FMath::IsFinite(Accepted->ControlRotationRate));
        TestTrue(TEXT("Only the authored movement-direction range reaches consumers"),Accepted->MovementDirection<6);
    }
    FMoverTickStartData Start;
    auto& Intent=Start.InputCmd.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>();
    Intent.RotationOffset=1000.f;
    Mover->OnPreSimulate(FMoverTimeStep(),Start);
    TestEqual(TEXT("Ground and air share the same finite rotation-offset range"),Start.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>()->RotationOffset,180.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverControllerlessInputTest, "ProjectJ.Mover.ControllerlessIdleInput",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoverControllerlessInputTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* Avatar=World->SpawnActor<AProject_JMoverPawn>();
    Avatar->Jump();
    FMoverInputCmdContext Idle;
    Avatar->ProduceInput_Implementation(0,Idle);
    const auto* Input=Idle.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    TestTrue(TEXT("An unpossessed simulated pawn still produces a supported neutral command"),Input && Input->GetMoveInputType()==EMoveInputType::DirectionalIntent && Input->GetMoveInput().IsNearlyZero());
    auto* Controller=World->SpawnActor<AAIController>(); Controller->Possess(Avatar);
    FMoverInputCmdContext Repossessed;
    Avatar->ProduceInput_Implementation(16,Repossessed);
    const auto* Current=Repossessed.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    TestFalse(TEXT("The next controller does not inherit an old held jump"),Current->bIsJumpPressed || Current->bIsJumpJustPressed);
    Avatar->Jump();
    Avatar->ProduceInput_Implementation(32,Repossessed);
    TestTrue(TEXT("A fresh jump edge after possession remains functional"),Repossessed.InputCollection.FindDataByType<FCharacterDefaultInputs>()->bIsJumpJustPressed);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverJumpIdentityTest, "ProjectJ.Mover.JumpIdentityAfterPredictionRejection",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMoverJumpIdentityTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* Avatar = World->SpawnActor<AProject_JMoverPawn>();
    auto* Controller = World->SpawnActor<AAIController>();
    Controller->Possess(Avatar);
    Avatar->Jump();
    FMoverInputCmdContext Rejected;
    Avatar->ProduceInput_Implementation(0, Rejected);
    const uint32 RejectedId = Rejected.InputCollection.FindDataByType<FProject_JMoverInputs>()->JumpRequestId;
    Avatar->StopJumping();
    Avatar->Jump();
    FMoverTickStartData Start;
    Avatar->ProduceInput_Implementation(16, Start.InputCmd);
    const uint32 AcceptedId = Start.InputCmd.InputCollection.FindDataByType<FProject_JMoverInputs>()->JumpRequestId;
    TestTrue(TEXT("A new edge cannot reuse a rejected request identity"), int32(AcceptedId - RejectedId) > 0);
    Start.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().SetTransforms_WorldSpace(
        FVector::ZeroVector, FRotator::ZeroRotator, FVector(123, 0, -20), FVector::ZeroVector);
    // Authoritative rollback rejected the prior prediction; no successful jump is recorded.
    Start.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
    FMoverSyncState Output; Output=Start.SyncState;
    FMoverTimeStep Step;
    FApplyMovementEffectParams Params;
    Params.UpdatedComponent = Avatar->GetRootComponent();
    Params.UpdatedPrimitive = Avatar->GetCapsuleComponent();
    Params.MoverComp = Avatar->GetMover();
    Params.StartState = &Start;
    Params.TimeStep = &Step;
    FProject_JJumpImpulseEffect Effect;
    Effect.UpwardsSpeed = 500.f;
    TestTrue(TEXT("Actual engine jump impulse applies"), Effect.ApplyMovementEffect(Params, Output));
    const auto* Life = Output.SyncStateCollection.FindDataByType<FProject_JMoverLifecycle>();
    TestEqual(TEXT("Confirmed jump retains command identity after rollback"), Life->JumpSequence, AcceptedId);
    TestTrue(TEXT("Successful jump opens a physical jump episode"), Life->bJumpEpisode);
    TestTrue(TEXT("New episode differs from rejected prediction"), Life->AirSequence != ((uint64(RejectedId) << 1) | 1));
    const auto* Physical = Output.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>();
    TestEqual(TEXT("Lifecycle tracking preserves engine horizontal momentum"), Physical->GetVelocity_WorldSpace().X, 123.);
    TestEqual(TEXT("Lifecycle tracking preserves engine jump impulse"), Physical->GetVelocity_WorldSpace().Z, 500.);
    TestTrue(TEXT("Applying the effect cannot mutate the archived start frame"),Start.SyncState.SyncStateCollection.FindDataByType<FMoverDefaultSyncState>()->GetVelocity_WorldSpace().Equals(FVector(123,0,-20)));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLateControllerActorInfoTest, "ProjectJ.RuntimeOwnership.LateControllerActorInfo",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLateControllerActorInfoTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Avatar = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Avatar fixture"), Avatar)) return false;
    auto* ASC = State->GetProjectJAbilitySystemComponent();
    TestNull(TEXT("ASC can initialize before PlayerState owner is replicated"), ASC->AbilityActorInfo->PlayerController.Get());
    auto* Controller = World->SpawnActor<APlayerController>();
    const int32 OriginalSpecs = ASC->GetActivatableAbilities().Num();
    State->SetOwner(Controller);
    TestNull(TEXT("Changing owner does not itself refresh cached actor info"), ASC->AbilityActorInfo->PlayerController.Get());
    State->OnRep_Owner();
    TestTrue(TEXT("Late owner replication publishes the current controller to GAS"), ASC->AbilityActorInfo->PlayerController.Get() == Controller);
    TestTrue(TEXT("Metadata refresh preserves current avatar"), ASC->GetAvatarActor() == Avatar);
    TestEqual(TEXT("Metadata refresh neither grants nor removes specs"), ASC->GetActivatableAbilities().Num(), OriginalSpecs);
    ASC->AbilityActorInfo->PlayerController.Reset();
    Avatar->ProcessEvent(Avatar->FindFunctionChecked(TEXT("OnRep_Controller")), nullptr);
    TestTrue(TEXT("Late controller replication refreshes the same ASC metadata"), ASC->AbilityActorInfo->PlayerController.Get() == Controller);
    auto* Replacement = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Replacement avatar"), Replacement)) return false;
    ASC->AbilityActorInfo->PlayerController.Reset();
    Avatar->ProcessEvent(Avatar->FindFunctionChecked(TEXT("OnRep_Controller")), nullptr);
    TestNull(TEXT("Retired avatar callback cannot refresh the replacement's actor info"), ASC->AbilityActorInfo->PlayerController.Get());
    TestTrue(TEXT("Current persistent owner can still refresh metadata"), ASC->RefreshActorInfoForCurrentActor(State));
    TestTrue(TEXT("Replacement controller is resolved"), ASC->AbilityActorInfo->PlayerController.Get() == Controller);
    const uint64 ReplacementEpoch = ASC->GetActorInfoRevision();
    Avatar->ProcessEvent(Avatar->FindFunctionChecked(TEXT("OnRep_PlayerState")), nullptr);
    TestTrue(TEXT("Retained old authority callback cannot rewrite the State back-reference"), State->GetPawn() == Replacement);
    TestTrue(TEXT("Retained old authority callback cannot replace the recipient"), ASC->GetAvatarActor() == Replacement && ASC->GetActorInfoRevision() == ReplacementEpoch);
    TestNull(TEXT("Retained old authority Pawn cannot lend persistent attributes"), Avatar->GetAttributeSet());
    return !HasAnyErrors();
}

// Synthetic native order injection. Actual-client callback probing is separately
// required in the existing socket Avatar-lifetime cohort.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAvatarPublicationOrderTest, "ProjectJ.RuntimeOwnership.AvatarPublicationOrder",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAvatarPublicationOrderTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false); Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Old = SpawnAvatar(World, State);
    auto* Next = SpawnAvatar(World, nullptr);
    if (!TestNotNull(TEXT("Old avatar"), Old) || !TestNotNull(TEXT("Next avatar"), Next)) return false;
    auto* ASC = State->GetProjectJAbilitySystemComponent();
    // Public role API selects the client branch in a synthetic native world;
    // this is not wire-order or real client-role evidence.
    Old->SetRole(ROLE_SimulatedProxy); Next->SetRole(ROLE_SimulatedProxy); State->SetRole(ROLE_SimulatedProxy);
    auto* PSProperty = FindFProperty<FObjectPropertyBase>(APawn::StaticClass(), TEXT("PlayerState"));
    if (!TestNotNull(TEXT("Replicated Pawn.PlayerState field"), PSProperty)) return false;
    auto Notify = [](AProject_JPlayerCharacter* Pawn) { Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_PlayerState")), nullptr); };
    Notify(Old); // Observe the actual State ASC before replacing it.
    const uint64 Epoch = ASC->GetActorInfoRevision();
    PSProperty->SetObjectPropertyValue_InContainer(Next, State);
    Notify(Next);
    TestTrue(TEXT("PS-first waits without stealing local back-reference"), State->GetPawn() == Old);
    TestTrue(TEXT("PS-first cannot start a client recipient epoch"), ASC->GetAvatarActor() == Old && ASC->GetActorInfoRevision() == Epoch);
    TestNull(TEXT("Unpublished new Pawn cannot lend the old ASC"), Next->GetAbilitySystemComponent());
    ASC->SetAvatarActor_Direct(Next);
    ASC->OnRep_OwningActor(); // The actual GAS publication boundary, not local Pawn Init.
    TestTrue(TEXT("Published new recipient wakes without polling"), Next->GetAbilitySystemComponent() == ASC && State->GetPawn() == Next);
    const uint64 PublishedEpoch = ASC->GetActorInfoRevision();
    Notify(Old);
    TestTrue(TEXT("Late old OnRep cannot rewrite State.Pawn"), State->GetPawn() == Next);
    TestTrue(TEXT("Late old OnRep cannot replace the current GAS tuple"), ASC->GetAvatarActor() == Next && ASC->GetActorInfoRevision() == PublishedEpoch);
    TestNull(TEXT("Retired Pawn cannot read persistent ASC"), Old->GetAbilitySystemComponent());
    TestNull(TEXT("Retired Pawn cannot read persistent attributes"), Old->GetAttributeSet());
    ASC->ClearActorInfo();
    TestNull(TEXT("Clear revokes current consumer"), Next->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(State, Next);
    TestTrue(TEXT("Weak observation survives Clear and wakes on re-publication"), Next->GetAbilitySystemComponent() == ASC);
    auto* Third = SpawnAvatar(World, nullptr);
    if (!TestNotNull(TEXT("ASC-first avatar"), Third)) return false;
    Third->SetRole(ROLE_SimulatedProxy);
    ASC->SetAvatarActor_Direct(Third); ASC->OnRep_OwningActor();
    PSProperty->SetObjectPropertyValue_InContainer(Third, State); Notify(Third);
    TestTrue(TEXT("ASC-first resolves immediately when PS arrives"), Third->GetAbilitySystemComponent() == ASC && State->GetPawn() == Third);
    auto* Mount = World->SpawnActor<AProject_JMountLifecycleFixture>();
    auto* RiderProperty = FindFProperty<FObjectPropertyBase>(AProject_JMountCharacter::StaticClass(), TEXT("Rider"));
    auto* MountedProperty = FindFProperty<FObjectPropertyBase>(UProject_JMountComponent::StaticClass(), TEXT("MountedMount"));
    auto* MountStateProperty = FindFProperty<FProperty>(AProject_JMountCharacter::StaticClass(), TEXT("MountState"));
    if (!TestNotNull(TEXT("Mount topology fixture"), Mount) || !TestNotNull(TEXT("Rider property"), RiderProperty)
        || !TestNotNull(TEXT("MountedMount property"), MountedProperty) || !TestNotNull(TEXT("MountState property"), MountStateProperty)) return false;
    const auto NotifyMountPS = [&]() { Mount->ProcessEvent(Mount->FindFunctionChecked(TEXT("OnRep_PlayerState")), nullptr); };
    // Derived-cache retry only: this standalone role-injection fixture does not
    // claim a valid foreign mounted session. The actual MountOnly socket cohort
    // remains the authority/owner/SP proof for unchanged mounted ownership.
    for (bool PlayerStateFirst : {true, false})
    {
        const uint64 DismountEpoch = ASC->GetActorInfoRevision();
        PSProperty->SetObjectPropertyValue_InContainer(Third, nullptr);
        RiderProperty->SetObjectPropertyValue_InContainer(Mount, Third);
        MountedProperty->SetObjectPropertyValue_InContainer(Third->GetMountComponent(), Mount);
        *MountStateProperty->ContainerPtrToValuePtr<EProject_JMountState>(Mount) = EProject_JMountState::Mounted;
        PSProperty->SetObjectPropertyValue_InContainer(Mount, State); NotifyMountPS();
        TestTrue(TEXT("Native mount PS OnRep publishes the controlled mount back-reference"), State->GetPawn() == Mount);
        if (PlayerStateFirst)
        {
            PSProperty->SetObjectPropertyValue_InContainer(Third, State); Notify(Third);
            TestTrue(TEXT("Early rider PS cannot overwrite a still-published mounted rider"), State->GetPawn() == Mount);
        }
        RiderProperty->SetObjectPropertyValue_InContainer(Mount, nullptr);
        APawn* PreviousRider = Third;
        Mount->ProcessEvent(Mount->FindFunctionChecked(TEXT("OnRep_Rider")), &PreviousRider);
        if (!PlayerStateFirst) { PSProperty->SetObjectPropertyValue_InContainer(Third, State); Notify(Third); }
        TestTrue(TEXT("Both PS-first and topology-first converge through real OnRep boundaries"), State->GetPawn() == Third && Third->GetAbilitySystemComponent() == ASC);
        TestEqual(TEXT("Topology retry is a same-pair ensure, not a recipient reset"), ASC->GetActorInfoRevision(), DismountEpoch);
        MountedProperty->SetObjectPropertyValue_InContainer(Third->GetMountComponent(), nullptr);
        AProject_JMountCharacter* PreviousMount = Mount;
        Third->GetMountComponent()->ProcessEvent(Third->GetMountComponent()->FindFunctionChecked(TEXT("OnRep_MountedMount")), &PreviousMount);
        *MountStateProperty->ContainerPtrToValuePtr<EProject_JMountState>(Mount) = EProject_JMountState::Unmounted;
        PSProperty->SetObjectPropertyValue_InContainer(Mount, nullptr); NotifyMountPS();
    }
    auto* NewRecipient = World->SpawnActor<AActor>();
    ASC->InitAbilityActorInfo(State, NewRecipient);
    const uint64 NewRecipientEpoch = ASC->GetActorInfoRevision();
    Third->GetMountComponent()->NotifyRuntimeOwnerPrerequisitesChanged();
    TestTrue(TEXT("Late old topology notification cannot replace a newer recipient"), ASC->GetAvatarActor() == NewRecipient && ASC->GetActorInfoRevision() == NewRecipientEpoch);
    TestNull(TEXT("Late old topology notification cannot lend newer recipient attributes"), Third->GetAttributeSet());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJActorInfoSettlementTest, "ProjectJ.RuntimeOwnership.ActorInfoSettlement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJActorInfoSettlementTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false); Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* A = World->SpawnActor<AActor>(); auto* B = World->SpawnActor<AActor>();
    auto* ASC = State->GetProjectJAbilitySystemComponent(); ASC->InitAbilityActorInfo(State, B);
    int32 Notifications = 0; AActor* Last = nullptr; bool Consistent = true;
    const auto Settled = ASC->OnActorInfoSettled.AddLambda([&](UProject_JAbilitySystemComponent* Source)
    {
        ++Notifications; Last = Source->GetAvatarActor();
        Consistent &= !Source->IsActorInfoChanging() && Source->GetAvatarActor_Direct() == Last
            && Source->AbilityActorInfo->OwnerActor.Get() == Source->GetOwnerActor();
    });
    ON_SCOPE_EXIT { ASC->OnActorInfoSettled.Remove(Settled); };
    for (bool Clear : {false, true})
    {
        bool Armed = true;
        const auto Changing = ASC->OnActorInfoChanging.AddLambda([&](AActor*, AActor* Avatar)
        {
            if (Armed && Avatar == A) { Armed = false; if (Clear) ASC->ClearActorInfo(); else ASC->InitAbilityActorInfo(State, B); }
        });
        Notifications = 0; ASC->InitAbilityActorInfo(State, A); ASC->OnActorInfoChanging.Remove(Changing);
        TestEqual(TEXT("Nested prechange has one outermost settled notification"), Notifications, 1);
        TestTrue(TEXT("Notification reports latest committed recipient, including null Clear"), Last == (Clear ? nullptr : B));
    }
    ASC->InitAbilityActorInfo(State, B);
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JAvatarPublicationFixture::StaticClass(), 1));
    auto* Spec = ASC->FindAbilitySpecFromHandle(Handle);
    auto* Ability = Spec ? Cast<UProject_JAvatarPublicationFixture>(Spec->GetPrimaryInstance()) : nullptr;
    if (!TestNotNull(TEXT("Actual per-actor GAS OnAvatarSet fixture"), Ability)) return false;
    for (bool Clear : {false, true})
    {
        ASC->InitAbilityActorInfo(State, B);
        Ability->OnPublication = [&, Clear]() { if (Clear) ASC->ClearActorInfo(); else ASC->InitAbilityActorInfo(State, B); };
        Notifications = 0; ASC->InitAbilityActorInfo(State, A);
        TestEqual(TEXT("Nested OnAvatarSet publishes once after the outer guard unwinds"), Notifications, 1);
        TestTrue(TEXT("OnAvatarSet replacement/Clear supersedes outer request"), Last == (Clear ? nullptr : B));
    }
    TestTrue(TEXT("All notifications expose a consistent settled pair"), Consistent);
    ASC->ClearAbility(Handle);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRepossessAttributeDefaultsTest,"ProjectJ.Mover.RepossessPreservesAttributes",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJRepossessAttributeDefaultsTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false); Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State=World->SpawnActor<AProject_JPlayerState>();
    auto* Controller=World->SpawnActor<AAIController>(); State->SetOwner(Controller);Controller->SetPlayerState(State);
    auto* Avatar=SpawnAvatar(World,State); if(!TestNotNull(TEXT("Avatar"),Avatar)) return false;
    Controller->Possess(Avatar); auto* Attributes=Avatar->GetAttributeSet();
    for(float Mana:{0.f,1.f,45.f})
    {
        Attributes->SetMana(Mana);Attributes->SetHealth(0.f);Attributes->SetAttackPower(0.f);Attributes->SetDefense(0.f);
        Controller->UnPossess();Controller->Possess(Avatar);
        TestEqual(TEXT("Repossession preserves depleted mana"),Attributes->GetMana(),Mana);
        TestEqual(TEXT("Repossession is not a resurrection"),Attributes->GetHealth(),0.f);
        TestEqual(TEXT("Zero attack remains gameplay state"),Attributes->GetAttackPower(),0.f);
        TestEqual(TEXT("Zero defense remains gameplay state"),Attributes->GetDefense(),0.f);
    }
    Avatar->InitializeDefaultAttributes(true);
    TestTrue(TEXT("Explicit reset still restores health and mana"),Attributes->GetHealth()>0 && Attributes->GetMana()>0);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountedPersistentOwnerTest, "ProjectJ.RuntimeOwnership.MountedPersistentOwner",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountedPersistentOwnerTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World = UWorld::CreateWorld(EWorldType::Game, false); Begin(World); ON_SCOPE_EXIT { End(World); };
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = World->SpawnActor<APlayerController>();
    State->SetOwner(Controller); Controller->SetPlayerState(State);
    auto* Avatar = SpawnAvatar(World, State);
    if (!TestNotNull(TEXT("Existing native playable fixture"), Avatar)) return false;
    Controller->Possess(Avatar);
    auto* ASC = State->GetProjectJAbilitySystemComponent();
    auto* Attributes = State->GetProjectJAttributeSet();
    auto* Progression = Avatar->GetProgressionComponent();
    const auto Epoch = ASC->GetActorInfoRevision();
    const auto SpecCount = ASC->GetActivatableAbilities().Num();
    Attributes->SetHealth(FMath::Min(73.f, Attributes->GetMaxHealth()));
    Attributes->SetMana(0.f);
    const float Health = Attributes->GetHealth();
    auto* Mount = World->SpawnActor<AProject_JMountLifecycleFixture>();
    if (!TestNotNull(TEXT("Existing concrete native mount fixture"), Mount)) return false;
    for (int32 Cycle = 0; Cycle < 2; ++Cycle)
    {
        Mount->SetActorLocation(Avatar->GetActorLocation());
        if (!TestTrue(TEXT("Actual public mount transition"), Mount->TryMountRider(Avatar))) return false;
        TestNull(TEXT("Engine detached rider has no Pawn.PlayerState"), Avatar->GetPlayerState());
        TestTrue(TEXT("PlayerState.Pawn remains the actually possessed mount"), State->GetPawn() == Mount);
        TestTrue(TEXT("Reciprocal current mount resolves the persistent runtime owner"), Avatar->GetMountComponent()->ResolveRuntimeOwner() == State);
        TestTrue(TEXT("Detached rider still provides its real persistent ASC"), Avatar->GetAbilitySystemComponent() == ASC);
        TestTrue(TEXT("Mounted rider retains persistent attributes"), Avatar->GetAttributeSet() == Attributes);
        TestTrue(TEXT("Mounted rider retains progression owner"), Avatar->GetProgressionComponent() == Progression);
        TestTrue(TEXT("Mount movement/health ASC stays separate"), Mount->GetAbilitySystemComponent() != ASC);
        TestTrue(TEXT("Persistent combat Avatar stays the humanoid rider"), ASC->GetAvatarActor() == Avatar);
        TestEqual(TEXT("Normal mount preserves the source epoch"), ASC->GetActorInfoRevision(), Epoch);
        Avatar->ProcessEvent(Avatar->FindFunctionChecked(TEXT("OnRep_Controller")), nullptr);
        if (!TestTrue(TEXT("Actual public dismount transition"), Mount->DismountRider(true))) return false;
        TestTrue(TEXT("Rider recovers actual possession"), Controller->GetPawn() == Avatar && State->GetPawn() == Avatar);
        TestTrue(TEXT("Normal dismount retains the same persistent ASC"), Avatar->GetAbilitySystemComponent() == ASC);
        TestEqual(TEXT("Dismount metadata reconnect preserves source epoch"), ASC->GetActorInfoRevision(), Epoch);
        TestEqual(TEXT("Mount lifecycle never heals the persistent character"), Attributes->GetHealth(), Health);
        TestEqual(TEXT("Mount lifecycle never refills depleted mana"), Attributes->GetMana(), 0.f);
        TestEqual(TEXT("Repeated reconnect never grants duplicate abilities"), ASC->GetActivatableAbilities().Num(), SpecCount);
        Avatar->ProcessEvent(Avatar->FindFunctionChecked(TEXT("OnRep_PlayerState")), nullptr);
        TestEqual(TEXT("Same current replicated owner is an ensure"), ASC->GetActorInfoRevision(), Epoch);
    }
    Mount->SetActorLocation(Avatar->GetActorLocation());
    if (!TestTrue(TEXT("Mount for recipient replacement boundary"), Mount->TryMountRider(Avatar))) return false;
    auto* Replacement = World->SpawnActor<AActor>();
    ASC->InitAbilityActorInfo(State, Replacement);
    TestTrue(TEXT("Explicit recipient replacement advances epoch"), ASC->GetActorInfoRevision() > Epoch);
    TestNull(TEXT("Old attached rider cannot resolve a replacement recipient"), Avatar->GetAbilitySystemComponent());
    ASC->InitAbilityActorInfo(State, Avatar);
    const auto ExplicitEpoch = ASC->GetActorInfoRevision();
    ASC->InitAbilityActorInfo(State, Avatar);
    TestTrue(TEXT("Explicit same-pair Init remains a real new epoch"), ASC->GetActorInfoRevision() > ExplicitEpoch);
    const auto CurrentEpoch = ASC->GetActorInfoRevision();
    TestTrue(TEXT("Current explicit recipient can again resolve mounted provider"), Avatar->GetAbilitySystemComponent() == ASC);
    TestTrue(TEXT("Dismount after explicit reset"), Mount->DismountRider(true));
    TestEqual(TEXT("Metadata ensure does not overwrite an explicit epoch"), ASC->GetActorInfoRevision(), CurrentEpoch);

    Mount->SetActorLocation(Avatar->GetActorLocation());
    if (!TestTrue(TEXT("Mount for actual possession theft boundary"), Mount->TryMountRider(Avatar))) return false;
    auto* ForeignState = World->SpawnActor<AProject_JPlayerState>();
    auto* ForeignController = World->SpawnActor<APlayerController>();
    ForeignState->SetOwner(ForeignController); ForeignController->SetPlayerState(ForeignState);
    ForeignController->Possess(Mount);
    TestNull(TEXT("Foreign controller cannot lend its owner to the old rider"), Avatar->GetMountComponent()->ResolveRuntimeOwner());
    TestNull(TEXT("Stale attached rider provides neither foreign nor former ASC"), Avatar->GetAbilitySystemComponent());
    TestTrue(TEXT("Retire stale mount without stealing possession back"), Mount->DismountRider(true, false));
    TestTrue(TEXT("Foreign controller remains in control of its current pawn"), ForeignController->GetPawn() == Mount);
    Controller->Possess(Avatar);
    auto* NewState = World->SpawnActor<AProject_JPlayerState>(); NewState->SetOwner(Controller);
    Controller->SetPlayerState(NewState); Controller->UnPossess(); Controller->Possess(Avatar);
    auto* NewASC = NewState->GetProjectJAbilitySystemComponent();
    TestTrue(TEXT("Real PlayerState replacement establishes the new ASC tuple"), Avatar->GetAbilitySystemComponent() == NewASC
        && NewASC->GetOwnerActor() == NewState && NewASC->GetAvatarActor() == Avatar && NewASC->GetActorInfoRevision() > 0);
    TestTrue(TEXT("The former source proof no longer matches the actual provider"), Avatar->GetAbilitySystemComponent() != ASC);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJFlightSolverReplicationTest,"ProjectJ.Mover.FlightSolverReplicationOrder",
    EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJFlightSolverReplicationTest::RunTest(const FString&)
{
    using namespace ProjectJRuntimeOwnershipTests;
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);Begin(World);ON_SCOPE_EXIT { End(World); };
    auto* Class=LoadClass<AProject_JFlyingMountCharacter>(nullptr,TEXT("/Script/Project_JMount.Project_JMountRefinementFixture"));
    if(!TestNotNull(TEXT("Concrete flight fixture"),Class)) return false;
    auto* Server=World->SpawnActor<AProject_JFlyingMountCharacter>(Class);auto* Owner=World->SpawnActor<AProject_JFlyingMountCharacter>(Class);
    if(!TestNotNull(TEXT("Authority fixture"),Server) || !TestNotNull(TEXT("Owner fixture"),Owner)) return false;
    auto* State=FindFProperty<FProperty>(Server->GetClass(),TEXT("FlightState"));
    auto* Glide=FindFProperty<FBoolProperty>(Server->GetClass(),TEXT("bIsGliding"));
    const float GroundBrake=Owner->GetCharacterMovement()->BrakingDecelerationFlying;
    for(bool GlideFirst:{false,true})
    {
        *State->ContainerPtrToValuePtr<EProject_JMountFlightState>(Server)=EProject_JMountFlightState::Flying;
        Server->SetGliding(true);
        auto ApplyGlide=[&]() { Glide->SetPropertyValue_InContainer(Owner,true);Owner->ProcessEvent(Owner->FindFunctionChecked(TEXT("OnRep_IsGliding")),nullptr); };
        auto ApplyState=[&]() { auto Previous=EProject_JMountFlightState::Grounded;*State->ContainerPtrToValuePtr<EProject_JMountFlightState>(Owner)=EProject_JMountFlightState::Flying;Owner->ProcessEvent(Owner->FindFunctionChecked(TEXT("OnRep_FlightState")),&Previous); };
        if(GlideFirst){ApplyGlide();ApplyState();}else{ApplyState();ApplyGlide();}
        TestEqual(TEXT("Owner solver receives glide speed"),Owner->GetCharacterMovement()->MaxFlySpeed,Server->GetCharacterMovement()->MaxFlySpeed);
        TestEqual(TEXT("Owner solver receives glide braking"),Owner->GetCharacterMovement()->BrakingDecelerationFlying,Server->GetCharacterMovement()->BrakingDecelerationFlying);
        TestEqual(TEXT("Owner solver receives glide friction"),Owner->GetCharacterMovement()->BrakingFriction,Server->GetCharacterMovement()->BrakingFriction);
        *State->ContainerPtrToValuePtr<EProject_JMountFlightState>(Owner)=EProject_JMountFlightState::Grounded;
        auto Previous=EProject_JMountFlightState::Flying;Owner->ProcessEvent(Owner->FindFunctionChecked(TEXT("OnRep_FlightState")),&Previous);
        TestEqual(TEXT("Grounded replication restores authored braking"),Owner->GetCharacterMovement()->BrakingDecelerationFlying,GroundBrake);
        Glide->SetPropertyValue_InContainer(Owner,false);Owner->ProcessEvent(Owner->FindFunctionChecked(TEXT("OnRep_IsGliding")),nullptr);
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMoverPresentationQualityTest,"ProjectJ.Mover.PresentationQualityAdmission",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJMoverPresentationQualityTest::RunTest(const FString&)
{
 using namespace ProjectJRuntimeOwnershipTests;
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);Begin(World);ON_SCOPE_EXIT { End(World); };
 auto* Floor=World->SpawnActor<AActor>();auto* Box=NewObject<UBoxComponent>(Floor);Floor->SetRootComponent(Box);
 Box->SetBoxExtent(FVector(100000,100000,50));Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
 Box->SetCollisionObjectType(ECC_WorldStatic);Box->SetCollisionResponseToAllChannels(ECR_Block);Box->RegisterComponent();Box->SetWorldLocation(FVector(0,0,-50));
 auto* AI=World->SpawnActor<AAIController>();auto* State=World->SpawnActor<AProject_JPlayerState>();AI->SetPlayerState(State);State->SetOwner(AI);
 auto* Player=ProjectJAuthoredAnimationFixture::Spawn(World,AI,State,FVector(0,0,100),true);
 if(!TestNotNull(TEXT("Actual authored moving player"),Player)) return false;
 auto* Viewer=World->SpawnActor<APlayerController>();Viewer->SetAsLocalPlayerController();
 auto* Camera=World->SpawnActor<ACameraActor>();Camera->SetActorLocation(FVector(50000,0,100));Viewer->SetViewTarget(Camera);
 Viewer->PlayerCameraManager->UpdateCamera(0.f);
 auto* Rate=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.PlayerTrajectory.RemotePredictionHz"));
 const int32 Previous=Rate->GetInt();Rate->Set(30,ECVF_SetByCode);ON_SCOPE_EXIT { Rate->Set(Previous,ECVF_SetByCode); };
 auto Tick=[&](int32 Count)
 {
  for(int32 N=0;N<Count;++N)
  {
   TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);for(auto* Mesh:Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
   World->Tick(LEVELTICK_All,1.f/60.f);
  }
 };
 Tick(150);
 auto* Anim=Cast<UProject_JPlayerAnimInstance>(Player->GetMesh()->GetAnimInstance());if(!TestNotNull(TEXT("Native production master"),Anim)) return false;
 TestFalse(TEXT("Real floor simulation has settled"),Player->GetMover()->IsAirborne());
 Anim->NativeUpdateAnimation(1.f/60.f);
 TestFalse(TEXT("A settled visual-root trajectory cannot be a capsule-offset sharp turn"),Anim->NeedsFullRateVisualUpdate());
 TestEqual(TEXT("Actual AI source publishes Far rather than default Local"),Anim->GetCurrentOptimizationPolicy().Tier,EProject_JAnimBudgetTier::Far);
 TestEqual(TEXT("Settled remote uses measured prediction density"),Anim->GetTrajectoryPredictionRate(),30);
 Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(0,1));Tick(2);Anim->NativeUpdateAnimation(1.f/60.f);
 TestEqual(TEXT("An accelerating remote retains full precision"),Anim->GetTrajectoryPredictionRate(),60);
 AI->SetControlRotation(FRotator(0,180,0));Tick(2);Anim->NativeUpdateAnimation(1.f/60.f);
 TestEqual(TEXT("A turning remote retains full precision"),Anim->GetTrajectoryPredictionRate(),60);
 AI->SetPlayerState(nullptr);State->SetOwner(Viewer);Viewer->SetPlayerState(State);Viewer->Possess(Player);Tick(2);Anim->NativeUpdateAnimation(1.f/60.f);
 TestTrue(TEXT("Actual possession establishes local ownership"),Player->GetController()==Viewer && Player->IsLocallyControlled());
 TestEqual(TEXT("Local source quality remains full"),Anim->GetCurrentOptimizationPolicy().Tier,EProject_JAnimBudgetTier::Local);
 TestEqual(TEXT("Local trajectory remains full precision"),Anim->GetTrajectoryPredictionRate(),60);
 Player->GetLocomotionAnimStateComponent()->ClearMoveInput();
 FActorSpawnParameters MountParams;MountParams.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Mount=World->SpawnActor<AProject_JMountLifecycleFixture>(Player->GetActorLocation()+FVector(150,0,0),FRotator::ZeroRotator,MountParams);
 if(!TestTrue(TEXT("Actual local rider mounts"),Mount && Mount->TryMountRider(Player))) return false;
 Tick(2);Anim->NativeUpdateAnimation(1.f/60.f);
 TestNull(TEXT("Possession moved away from the rider"),Player->GetController());
 TestEqual(TEXT("Mounted local rider keeps full source quality"),Anim->GetCurrentOptimizationPolicy().Tier,EProject_JAnimBudgetTier::Local);
 TestEqual(TEXT("Mounted local rider keeps full prediction precision"),Anim->GetTrajectoryPredictionRate(),60);
 TestFalse(TEXT("Mounted local rider cannot enter ABA"),CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh())->CanUseBudget());
 TestTrue(TEXT("Forced exit restores the local rider"),Mount->DismountRider(true));
 return true;
}

// Value-equivalence fixture for the opt-in query path; not collision or socket proof.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNativeFlyingTrajectoryParamsTest,"ProjectJ.Mover.NativeFlyingTrajectoryParams",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJNativeFlyingTrajectoryParamsTest::RunTest(const FString&)
{
 using namespace ProjectJRuntimeOwnershipTests;
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);Begin(World);ON_SCOPE_EXIT {End(World);};
 auto* State=World->SpawnActor<AProject_JPlayerState>();auto* Controller=World->SpawnActor<AAIController>();
 if(!TestNotNull(TEXT("Persistent fixture state"),State) || !TestNotNull(TEXT("Fixture controller"),Controller)) return false;
 State->SetOwner(Controller);Controller->SetPlayerState(State);
 auto* Avatar=SpawnAvatar(World,State);if(!TestNotNull(TEXT("Existing native avatar fixture"),Avatar)) return false;
 Controller->Possess(Avatar);
 auto* Mover=CastChecked<UProject_JMoverComponent>(Avatar->GetMover());
 auto* FlyingMode=Mover->MovementModes.FindRef(DefaultModeNames::Flying).Get();
 auto* PredictionSettings=Mover->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
 auto* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("mover.Prediction.ReuseFlyingParams"));
 auto* QueryCounter=IConsoleManager::Get().FindConsoleVariable(TEXT("mover.Prediction.NativeFlyingParamsQueries"));
 if(!TestNotNull(TEXT("Opt-in setting"),Enabled) || !TestNotNull(TEXT("Actual branch diagnostic"),QueryCounter) ||
  !TestNotNull(TEXT("Cached mode settings"),PredictionSettings) || !TestNotNull(TEXT("Registered Flying mode"),FlyingMode)) return false;
 if(!TestTrue(TEXT("Exact native Flying is the test precondition"),FlyingMode->GetClass()==UFlyingMode::StaticClass()) ||
  !TestFalse(TEXT("Existing fixture uses synchronous backend"),Mover->IsBackendAsync())) return false;
 const int32 PreviousEnabled=Enabled->GetInt();ON_SCOPE_EXIT {Enabled->Set(PreviousEnabled,ECVF_SetByCode);};
 auto* PredictionBase=NewObject<UBoxComponent>(Avatar);Avatar->AddInstanceComponent(PredictionBase);
 PredictionBase->SetCollisionEnabled(ECollisionEnabled::NoCollision);PredictionBase->RegisterComponent();
 auto* SpringType=LoadObject<UScriptStruct>(nullptr,TEXT("/Script/Mover.SmoothWalkingState"));
 if(!TestNotNull(TEXT("Existing private reflected spring schema"),SpringType)) return false;

 TFunction<bool(const FMoverDataCollection&,const FMoverDataCollection&)> SameValues;
 SameValues=[&](const FMoverDataCollection& Left,const FMoverDataCollection& Right)
 {
  const auto& L=Left.GetDataArray();const auto& R=Right.GetDataArray();if(L.Num()!=R.Num()) return false;
  for(int32 I=0;I<L.Num();++I)
  {
   if(!L[I] || !R[I] || L[I]->GetScriptStruct()!=R[I]->GetScriptStruct()) return false;
   const auto* Type=L[I]->GetScriptStruct();
   if(Type==FCharacterDefaultInputs::StaticStruct())
   {
    if(*static_cast<const FCharacterDefaultInputs*>(L[I].Get())!=*static_cast<const FCharacterDefaultInputs*>(R[I].Get())) return false;
   }
   else if(Type==FProject_JMoverInputs::StaticStruct())
   {
    const auto& A=*static_cast<const FProject_JMoverInputs*>(L[I].Get());const auto& B=*static_cast<const FProject_JMoverInputs*>(R[I].Get());
    if(A.bStrafe!=B.bStrafe || A.bSprint!=B.bSprint || A.bWalk!=B.bWalk || A.MovementDirection!=B.MovementDirection ||
     A.RotationOffset!=B.RotationOffset || A.ControlRotationRate!=B.ControlRotationRate || A.JumpRequestId!=B.JumpRequestId) return false;
   }
   else if(Type==FProject_JMoverLifecycle::StaticStruct())
   {
    const auto& A=*static_cast<const FProject_JMoverLifecycle*>(L[I].Get());const auto& B=*static_cast<const FProject_JMoverLifecycle*>(R[I].Get());
    if(A.AirSequence!=B.AirSequence || A.JumpSequence!=B.JumpSequence || A.LandingSequence!=B.LandingSequence || A.bJumpEpisode!=B.bJumpEpisode ||
     A.bStrafe!=B.bStrafe || A.bSprint!=B.bSprint || A.LandingBrakingTimeMs!=B.LandingBrakingTimeMs || A.LandingVelocity!=B.LandingVelocity) return false;
   }
   else if(Type==FMoverInputContainerDataStruct::StaticStruct())
   {
    if(!SameValues(static_cast<const FMoverInputContainerDataStruct*>(L[I].Get())->InputCollection,
     static_cast<const FMoverInputContainerDataStruct*>(R[I].Get())->InputCollection)) return false;
   }
   else if(!Type->CompareScriptStruct(L[I].Get(),R[I].Get(),PPF_None)) return false;
  }
  return true;
 };
 // The engine wrapper Clone copy-constructs its unreflected nested collection.
 // Detach that known wrapper explicitly in this fixture; collection assignment alone is not enough.
 TFunction<void(FMoverDataCollection&,const FMoverDataCollection&)> CopyValues;
 CopyValues=[&](FMoverDataCollection& Destination,const FMoverDataCollection& Source)
 {
  Destination.Empty();Destination=Source;
  const auto& D=Destination.GetDataArray();const auto& S=Source.GetDataArray();
  TestEqual(TEXT("Value snapshot retains payload count"),D.Num(),S.Num());
  for(int32 I=0;I<D.Num() && I<S.Num();++I)
  {
   const bool Independent=D[I] && S[I] && D[I].Get()!=S[I].Get();
   if(!TestTrue(TEXT("Snapshot payload storage is independent before value comparison"),Independent)) continue;
   if(D[I]->GetScriptStruct()==FMoverInputContainerDataStruct::StaticStruct())
   {
    auto* To=static_cast<FMoverInputContainerDataStruct*>(D[I].Get());
    const auto* From=static_cast<const FMoverInputContainerDataStruct*>(S[I].Get());
    CopyValues(To->InputCollection,From->InputCollection);
   }
  }
 };
 auto Publish=[&](const FMoverInputCmdContext& CaseInput,FName Mode,UPrimitiveComponent* SyncBase=nullptr)
 {
  FMoverTickStartData Start;CopyValues(Start.InputCmd.InputCollection,CaseInput.InputCollection);Start.SyncState=Mover->GetSyncState();
  CopyValues(Start.SyncState.SyncStateCollection,Mover->GetSyncState().SyncStateCollection);
  Start.SyncState.MovementMode=Mode;
  Start.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().SetTransforms_WorldSpace(
   FVector(100,200,300),FRotator(12,25,7),FVector(80,-30,20),FVector(0,0,25),SyncBase);
  Start.SyncState.SyncStateCollection.FindOrAddDataByType(SpringType);
  auto& Life=Start.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
  Life.AirSequence=11;Life.JumpSequence=5;Life.LandingSequence=3;Life.LandingBrakingTimeMs=123.25f;Life.LandingVelocity=FVector(10,20,-37);
  FMoverTickEndData EndData;EndData.SyncState=Start.SyncState;EndData.AuxState=Start.AuxState;
  CopyValues(EndData.SyncState.SyncStateCollection,Start.SyncState.SyncStateCollection);
  FMoverTimeStep Step;Step.ServerFrame=17;Step.BaseSimTimeMs=250.;Step.StepMs=1000.f/60.f;
  Mover->OnPostSimulate(Step,Start,EndData);Mover->FinalizeFrame(&EndData.SyncState,&EndData.AuxState);
 };
 //135: one batch of owning forecasts survives later setting/input/state changes.
 TArray<FMoverNativeFlyingTrajectory> OwnedFlying135;
 TArray<TArray<FTrajectorySampleInfo>> OwnedReference135;
 const auto SameFlying135=[](const TArray<FTrajectorySampleInfo>& Left,const TArray<FTrajectorySampleInfo>& Right)
 {
  if(Left.Num()!=Right.Num() || Left.IsEmpty()) return false;
  for(int32 Index=0;Index<Left.Num();++Index)
  {
   const auto& A=Left[Index];const auto& B=Right[Index];
   if(B.Transform.ContainsNaN() || B.LinearVelocity.ContainsNaN() || B.InstantaneousAcceleration.ContainsNaN() || B.AngularVelocity.ContainsNaN() || !FMath::IsFinite(B.SimTimeMs) ||
    !A.Transform.Equals(B.Transform,1.e-6) || !A.LinearVelocity.Equals(B.LinearVelocity,1.e-6) || !A.InstantaneousAcceleration.Equals(B.InstantaneousAcceleration,1.e-6) ||
    !A.AngularVelocity.Equals(B.AngularVelocity,1.e-6) || A.SimTimeMs!=B.SimTimeMs) return false;
  }
  return true;
 };
 FMoverPredictTrajectoryParams Query;Query.NumPredictionSamples=92;Query.SecondsPerSample=1.f/60.f;Query.bUseVisualComponentRoot=true;
 auto Compare=[&](const TCHAR* CaseName,const FMoverInputCmdContext& CaseInput,FName Mode,const FMoverPredictTrajectoryParams& CaseQuery,
  bool ExpectReuse,UPrimitiveComponent* SyncBase=nullptr,bool CompareOutput=true,int32 CandidateLevel=1)
 {
  Publish(CaseInput,Mode,SyncBase);
  FMoverInputCmdContext BeforeInput;CopyValues(BeforeInput.InputCollection,Mover->GetLastInputCmd().InputCollection);
  FMoverSyncState BeforeSync;BeforeSync=Mover->GetSyncState();CopyValues(BeforeSync.SyncStateCollection,Mover->GetSyncState().SyncStateCollection);
  FMoverInputCmdContext CallerInput;CopyValues(CallerInput.InputCollection,CaseInput.InputCollection);
  TArray<FMoverInputCmdContext> CallerOptionalInputs;CallerOptionalInputs.SetNum(CaseQuery.OptionalInputCmds.Num());
  for(int32 I=0;I<CallerOptionalInputs.Num();++I) CopyValues(CallerOptionalInputs[I].InputCollection,CaseQuery.OptionalInputCmds[I].InputCollection);
  FMoverSyncState CallerOptionalSync;FMoverAuxStateContext CallerOptionalAux;
  if(CaseQuery.OptionalStartSyncState.IsSet())
  {
   CallerOptionalSync=CaseQuery.OptionalStartSyncState.GetValue();
   CopyValues(CallerOptionalSync.SyncStateCollection,CaseQuery.OptionalStartSyncState.GetValue().SyncStateCollection);
  }
  if(CaseQuery.OptionalStartAuxState.IsSet()) CopyValues(CallerOptionalAux.AuxStateCollection,CaseQuery.OptionalStartAuxState.GetValue().AuxStateCollection);
  const int32 CounterBefore=QueryCounter->GetInt();
  if(!TestTrue(TEXT("Counter has headroom; saturation cannot hide branch assertions"),CounterBefore<MAX_int32-4)) return false;
  Enabled->Set(0,ECVF_SetByCode);const auto Reference=Mover->GetPredictedTrajectory(CaseQuery);
  TestEqual(FString::Printf(TEXT("%s: disabled query stays on ordinary path"),CaseName),QueryCounter->GetInt(),CounterBefore);
  FMoverNativeFlyingTrajectory Disabled135;Disabled135.NumPredictionSamples=777;
  TestFalse(TEXT("CVar0 rejects owning Flying preparation"),Mover->TryPrepareNativeFlyingTrajectory(CaseQuery,Disabled135));
  TestEqual(TEXT("Disabled owning capture leaves caller output unchanged"),Disabled135.NumPredictionSamples,777);
  Enabled->Set(CandidateLevel,ECVF_SetByCode);const auto Candidate=Mover->GetPredictedTrajectory(CaseQuery);
  bool Same=Reference.Num()==92 && Candidate.Num()==92;
  for(int32 I=0;Same && I<92;++I)
  {
   const auto& A=Reference[I];const auto& B=Candidate[I];
   Same=!A.Transform.ContainsNaN() && !B.Transform.ContainsNaN() && !A.LinearVelocity.ContainsNaN() && !B.LinearVelocity.ContainsNaN() &&
    !A.InstantaneousAcceleration.ContainsNaN() && !B.InstantaneousAcceleration.ContainsNaN() && !A.AngularVelocity.ContainsNaN() && !B.AngularVelocity.ContainsNaN() &&
    FMath::IsFinite(A.SimTimeMs) && FMath::IsFinite(B.SimTimeMs);
   if(CompareOutput) Same=Same && A.Transform.Equals(B.Transform,1.e-6) && A.LinearVelocity.Equals(B.LinearVelocity,1.e-6) &&
    A.InstantaneousAcceleration.Equals(B.InstantaneousAcceleration,1.e-6) && A.AngularVelocity.Equals(B.AngularVelocity,1.e-6) && A.SimTimeMs==B.SimTimeMs;
  }
  TestTrue(FString::Printf(TEXT("%s: all 92 sample transforms, velocities, derivatives and times are finite%s"),CaseName,
   CompareOutput?TEXT(" and equivalent"):TEXT("; mutable custom-mode tuning is not treated as immutable")),Same);
  TestEqual(FString::Printf(TEXT("%s: actual branch consumption"),CaseName),QueryCounter->GetInt(),CounterBefore+(ExpectReuse?1:0));
  FMoverNativeFlyingTrajectory Owned135;Owned135.NumPredictionSamples=777;Owned135.Location=FVector(123,456,789);
  const bool Prepared135=Mover->TryPrepareNativeFlyingTrajectory(CaseQuery,Owned135);
  TestEqual(FString::Printf(TEXT("%s: owning flight admission matches existing synchronous eligibility"),CaseName),Prepared135,ExpectReuse);
  if(Prepared135)
  {
   TArray<FTrajectorySampleInfo> Pure135;
   TestTrue(FString::Printf(TEXT("%s: pure owned Flying forecast computes on GT"),CaseName),UE::MoverUtils::ComputeNativeFlyingTrajectory(Owned135,Pure135));
   TestTrue(FString::Printf(TEXT("%s: owned raw values match canonical GenerateMove with policy0"),CaseName),SameFlying135(Reference,Pure135));
   OwnedFlying135.Add(Owned135);OwnedReference135.Add(Reference);
  }
  else TestTrue(FString::Printf(TEXT("%s: rejected capture does not publish partial output"),CaseName),Owned135.NumPredictionSamples==777 && Owned135.Location==FVector(123,456,789));
  TestEqual(TEXT("Owning capture/compute does not charge the synchronous reuse counter"),QueryCounter->GetInt(),CounterBefore+(ExpectReuse?1:0));
  TestTrue(FString::Printf(TEXT("%s: published input values remain unchanged"),CaseName),SameValues(BeforeInput.InputCollection,Mover->GetLastInputCmd().InputCollection));
  TestTrue(FString::Printf(TEXT("%s: original physical and private spring values remain unchanged"),CaseName),
   BeforeSync.MovementMode==Mover->GetSyncState().MovementMode && SameValues(BeforeSync.SyncStateCollection,Mover->GetSyncState().SyncStateCollection));
  TestTrue(FString::Printf(TEXT("%s: caller input remains independent"),CaseName),SameValues(CallerInput.InputCollection,CaseInput.InputCollection));
  for(int32 I=0;I<CallerOptionalInputs.Num();++I)
   TestTrue(TEXT("Optional caller command values remain unchanged"),SameValues(CallerOptionalInputs[I].InputCollection,CaseQuery.OptionalInputCmds[I].InputCollection));
  if(CaseQuery.OptionalStartSyncState.IsSet())
   TestTrue(TEXT("Optional caller sync values remain unchanged"),CallerOptionalSync.MovementMode==CaseQuery.OptionalStartSyncState.GetValue().MovementMode &&
    SameValues(CallerOptionalSync.SyncStateCollection,CaseQuery.OptionalStartSyncState.GetValue().SyncStateCollection));
  if(CaseQuery.OptionalStartAuxState.IsSet())
   TestTrue(TEXT("Optional caller aux values remain unchanged"),SameValues(CallerOptionalAux.AuxStateCollection,CaseQuery.OptionalStartAuxState.GetValue().AuxStateCollection));
  return Same;
 };
 FMoverInputCmdContext Fixed;
 auto& FixedInput=Fixed.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
 FixedInput.SetMoveInput(EMoveInputType::DirectionalIntent,FVector(.6,-.2,.1));FixedInput.OrientationIntent=FVector(.3,.9,.2).GetSafeNormal();
 auto& FixedIntent=Fixed.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>();
 FixedIntent.bWalk=true;FixedIntent.RotationOffset=-27.5f;FixedIntent.ControlRotationRate=180.f;FixedIntent.JumpRequestId=15;
 PredictionSettings->MaxSpeed=675.f;PredictionSettings->Acceleration=1300.f;PredictionSettings->Deceleration=850.f;
 PredictionSettings->TurningBoost=4.f;PredictionSettings->TurningRate=235.f;PredictionSettings->bShouldRemainVertical=true;
 Mover->SetGravityOverride(true,FVector(0,-700,-700));FPlanarConstraint Constraint;
 Constraint.bConstrainToPlane=true;Constraint.PlaneConstraintNormal=FVector::RightVector;Mover->SetPlanarConstraint(Constraint);
 Compare(TEXT("Directional tilted-gravity planar"),Fixed,DefaultModeNames::Flying,Query,true);
 FixedInput.SetMoveInput(EMoveInputType::Velocity,FVector(140,-60,25));PredictionSettings->bShouldRemainVertical=false;
 PredictionSettings->MaxSpeed=420.f;PredictionSettings->Acceleration=777.f;
 Compare(TEXT("Velocity with fresh tuning next query"),Fixed,DefaultModeNames::Flying,Query,true);
 FMoverInputCmdContext Zero;Zero=Fixed;Zero.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>()->OrientationIntent=FVector::ZeroVector;
 Compare(TEXT("Zero orientation fallback"),Zero,DefaultModeNames::Flying,Query,false); // Existing level1 policy.
 // Tilted gravity and the nonvertical initial sync pose require facing to evolve
 // across samples. Reusing the first sample's zero-intent facing is not equivalent.
 PredictionSettings->bShouldRemainVertical=true;
 Compare(TEXT("Level2 zero orientation uses evolving sync facing with tilted gravity"),Zero,DefaultModeNames::Flying,Query,true,nullptr,true,2);
 FMoverInputCmdContext Tiny;Tiny=Zero;
 Tiny.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>()->OrientationIntent=FVector(UE_KINDA_SMALL_NUMBER*.5f,0,0);
 Compare(TEXT("Level1 nearly-zero orientation retains fallback"),Tiny,DefaultModeNames::Flying,Query,false);
 Compare(TEXT("Level2 nearly-zero orientation follows native tolerance"),Tiny,DefaultModeNames::Flying,Query,true,nullptr,true,2);
 Tiny.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>()->OrientationIntent=FVector(UE_KINDA_SMALL_NUMBER*2.f,0,0);
 Compare(TEXT("Above zero tolerance retains level1 fixed input facing"),Tiny,DefaultModeNames::Flying,Query,true);
 PredictionSettings->bShouldRemainVertical=false;
 Compare(TEXT("Level2 zero orientation with unconstrained facing"),Zero,DefaultModeNames::Flying,Query,true,nullptr,true,2);
 FMoverInputCmdContext Empty;Compare(TEXT("Empty input fallback"),Empty,DefaultModeNames::Flying,Query,false);
 FMoverInputCmdContext Based;Based=Fixed;auto* BasedInput=Based.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>();
 BasedInput->bUsingMovementBase=true;BasedInput->MovementBase=PredictionBase;
 Compare(TEXT("Based input fallback"),Based,DefaultModeNames::Flying,Query,false);
 Compare(TEXT("Based sync fallback"),Fixed,DefaultModeNames::Flying,Query,false,PredictionBase);
 auto Optional=Query;Optional.OptionalInputCmds.SetNum(1);CopyValues(Optional.OptionalInputCmds[0].InputCollection,Fixed.InputCollection);
 Compare(TEXT("Optional input fallback"),Fixed,DefaultModeNames::Flying,Optional,false);
 auto OptionalStart=Query;OptionalStart.OptionalStartTime=FMoverTime();
 Compare(TEXT("Optional start fallback"),Fixed,DefaultModeNames::Flying,OptionalStart,false);
 auto OptionalSync=Query;OptionalSync.OptionalStartSyncState.Emplace();OptionalSync.OptionalStartSyncState.GetValue()=Mover->GetSyncState();
 CopyValues(OptionalSync.OptionalStartSyncState.GetValue().SyncStateCollection,Mover->GetSyncState().SyncStateCollection);
 Compare(TEXT("Optional sync fallback"),Fixed,DefaultModeNames::Flying,OptionalSync,false);
 auto OptionalAux=Query;OptionalAux.OptionalStartAuxState.Emplace();
 auto& AuxValue=OptionalAux.OptionalStartAuxState.GetValue().AuxStateCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>();
 AuxValue.RotationOffset=13.5f;AuxValue.ControlRotationRate=37.f;
 Compare(TEXT("Optional aux fallback"),Fixed,DefaultModeNames::Flying,OptionalAux,false);
 Compare(TEXT("Existing custom Attached mode fallback"),Fixed,TEXT("Attached"),Query,false);
 // Real authored Walking has mutable tuning callbacks; verify fallback/finite samples and original payloads,
 // without assuming two sequential custom queries have identical mode-owned tuning.
 Compare(TEXT("Existing authored Walking mode fallback"),Fixed,DefaultModeNames::Walking,Query,false,nullptr,false);
 TestTrue(TEXT("Owning cases include changed input, tuning and zero-facing policy2"),OwnedFlying135.Num()>=5 && OwnedReference135.Num()==OwnedFlying135.Num());
 const FMoverNativeFlyingTrajectory ValidPacket135=OwnedFlying135.IsEmpty()?FMoverNativeFlyingTrajectory{}:OwnedFlying135[0];
 if(OwnedFlying135.Num()>=2)
 {
  TestFalse(TEXT("Fresh queries consume changed input/tuning rather than replay the first forecast"),SameFlying135(OwnedReference135[0],OwnedReference135[1]));
  // The fixture has since published different settings/input and even a different movement mode.
  // Only native value packets enter this single worker job; no test callback/actor/mode/collection capture.
  struct FOwnedFlyingBatch135 { TArray<TArray<FTrajectorySampleInfo>> Samples; bool bWorker=false,bSuccess=true; };
  auto Work135=Async(EAsyncExecution::ThreadPool,[Packets=MoveTemp(OwnedFlying135)]
  {
   FOwnedFlyingBatch135 Result;Result.bWorker=!IsInGameThread();Result.Samples.SetNum(Packets.Num());
   for(int32 Index=0;Index<Packets.Num();++Index) Result.bSuccess &= UE::MoverUtils::ComputeNativeFlyingTrajectory(Packets[Index],Result.Samples[Index]);
   return Result;
  });
  const auto Result135=Work135.Get();TestTrue(TEXT("Owned Flying batch executes off GT"),Result135.bWorker);TestTrue(TEXT("Every detached Flying packet computes"),Result135.bSuccess);
  TestEqual(TEXT("Detached batch retains all captured cases"),Result135.Samples.Num(),OwnedReference135.Num());
  for(int32 Index=0;Index<Result135.Samples.Num() && Index<OwnedReference135.Num();++Index)
   TestTrue(TEXT("Detached owned packets remain canonical after source input/settings/state change"),SameFlying135(OwnedReference135[Index],Result135.Samples[Index]));
 }
 // Pure-kernel failure is explicit and clears output; do not route malformed values through ensure-based animation admission.
 FMoverNativeFlyingTrajectory Invalid135;Invalid135.NumPredictionSamples=0;TArray<FTrajectorySampleInfo> InvalidOutput135;InvalidOutput135.SetNum(1);
 TestFalse(TEXT("Malformed zero-count Flying packet is rejected"),UE::MoverUtils::ComputeNativeFlyingTrajectory(Invalid135,InvalidOutput135));TestTrue(TEXT("Zero-count rejection clears output"),InvalidOutput135.IsEmpty());
 Invalid135=ValidPacket135;TestTrue(TEXT("Valid owned Flying packet supplies the malformed-state positive control"),UE::MoverUtils::ComputeNativeFlyingTrajectory(Invalid135,InvalidOutput135));
 Invalid135.Location.X=std::numeric_limits<double>::quiet_NaN();
 TestFalse(TEXT("Malformed NaN Flying packet is rejected without publishing samples"),UE::MoverUtils::ComputeNativeFlyingTrajectory(Invalid135,InvalidOutput135));TestTrue(TEXT("NaN rejection clears output"),InvalidOutput135.IsEmpty());
 AddInfo(TEXT("Same-state opt-in numerical comparison only: 92 samples, actual branch counter and explicit original values. No collision/socket/performance claim."));
 return !HasAnyErrors();
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPreparedGroundPredictionTest,"ProjectJ.Mover.PreparedGroundPrediction",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJPreparedGroundPredictionTest::RunTest(const FString&)
{
 using namespace ProjectJRuntimeOwnershipTests;
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);Begin(World);ON_SCOPE_EXIT {End(World);};
 auto* State=World->SpawnActor<AProject_JPlayerState>();auto* Controller=World->SpawnActor<AAIController>();
 if(!TestNotNull(TEXT("Persistent ground fixture state"),State) || !TestNotNull(TEXT("Ground fixture controller"),Controller)) return false;
 State->SetOwner(Controller);Controller->SetPlayerState(State);
 auto* Avatar=SpawnAvatar(World,State);if(!TestNotNull(TEXT("Existing authored avatar fixture"),Avatar)) return false;
 Controller->Possess(Avatar);
 auto* Mover=CastChecked<UProject_JMoverComponent>(Avatar->GetMover());
 auto* Ground=Cast<UProject_JGroundWalkingMode>(Mover->MovementModes.FindRef(DefaultModeNames::Walking).Get());
 auto* Settings=Mover->FindSharedSettings_Mutable<UCommonLegacyMovementSettings>();
 auto* Enabled=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Mover.PreparedGroundPrediction"));
 auto* Counter=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Mover.PreparedGroundQueries"));
 auto* SpringType=LoadObject<UScriptStruct>(nullptr,TEXT("/Script/Mover.SmoothWalkingState"));
 if(!TestNotNull(TEXT("Actual native ground mode"),Ground) || !TestNotNull(TEXT("Shared ground policy"),Settings) ||
  !TestNotNull(TEXT("Prepared prediction switch"),Enabled) || !TestNotNull(TEXT("Successful query counter"),Counter) ||
  !TestNotNull(TEXT("Private reflected physical spring schema"),SpringType)) return false;
 if(!TestFalse(TEXT("Synchronous backend is required"),Mover->IsBackendAsync())) return false;
 for(const FName Name:{FName(TEXT("GenerateMove")),FName(TEXT("GenerateWalkMove"))})
 {
  const UFunction* Function=Ground->FindFunction(Name);
  if(!TestTrue(TEXT("Authored mode resolves both native generation events"),Function && Function->GetOwnerClass()->HasAnyClassFlags(CLASS_Native))) return false;
 }
 const int32 OldEnabled=Enabled->GetInt();ON_SCOPE_EXIT {Enabled->Set(OldEnabled,ECVF_SetByCode);};
 const FName FieldNames[]={TEXT("MaxSpeedOverride"),TEXT("Acceleration"),TEXT("Deceleration"),TEXT("TurningStrength"),TEXT("FacingSmoothingTime")};
 FFloatProperty* Fields[5]={};
 for(int32 I=0;I<5;++I)
 {
  Fields[I]=FindFProperty<FFloatProperty>(Ground->GetClass(),FieldNames[I]);
  if(!TestNotNull(TEXT("Native final writer retains its float schema"),Fields[I])) return false;
 }
 struct FModeValues { float Values[5]; };
 auto ReadMode=[&](){FModeValues Result;for(int32 I=0;I<5;++I) Result.Values[I]=Fields[I]->GetPropertyValue_InContainer(Ground);return Result;};
 auto WriteMode=[&](const FModeValues& Values){for(int32 I=0;I<5;++I) Fields[I]->SetPropertyValue_InContainer(Ground,Values.Values[I]);};
 const FModeValues OriginalMode=ReadMode();const TWeakObjectPtr<UProject_JGroundWalkingMode> WeakGround125(Ground);ON_SCOPE_EXIT {if(auto* LiveGround125=WeakGround125.Get()) for(int32 RestoreField125=0;RestoreField125<5;++RestoreField125) Fields[RestoreField125]->SetPropertyValue_InContainer(LiveGround125,OriginalMode.Values[RestoreField125]);};
 const FModeValues QueryStart={{225.f,611.f,733.f,9.f,.37f}};
 const float OriginalPolicyCap=Settings->MaxSpeed;const TWeakObjectPtr<UCommonLegacyMovementSettings> WeakSettings125(Settings);ON_SCOPE_EXIT {if(auto* LiveSettings125=WeakSettings125.Get()) LiveSettings125->MaxSpeed=OriginalPolicyCap;};
 Settings->MaxSpeed=1000.f;
 Mover->GroundMovementSettings.RunSpeed=500.f;Mover->GroundMovementSettings.SprintSpeed=750.f;
 Mover->GroundMovementSettings.bSlopeRelativeVelocity=true;
 auto* Base=NewObject<UBoxComponent>(Avatar);Avatar->AddInstanceComponent(Base);
 Base->SetCollisionEnabled(ECollisionEnabled::NoCollision);Base->RegisterComponent();
 auto* Blackboard=Mover->GetSimBlackboard_Mutable();if(!TestNotNull(TEXT("Native floor cache"),Blackboard)) return false;
 FFloorCheckResult Floor;Floor.bBlockingHit=true;Floor.bWalkableFloor=true;
 Floor.HitResult.Normal=FVector(.12,.22,.97).GetSafeNormal();Floor.HitResult.ImpactNormal=Floor.HitResult.Normal;
 Blackboard->Set(CommonBlackboard::LastFloorResult,Floor);
 if(!TestFalse(TEXT("Root-motion cohort starts without an inherited root-motion tag"),Mover->HasGameplayTag(Mover_AnimRootMotion,false))) return false;

 TFunction<bool(const FMoverDataCollection&,const FMoverDataCollection&)> SameValues;
 SameValues=[&](const FMoverDataCollection& Left,const FMoverDataCollection& Right)
 {
  const auto& L=Left.GetDataArray();const auto& R=Right.GetDataArray();if(L.Num()!=R.Num()) return false;
  for(int32 I=0;I<L.Num();++I)
  {
   if(!L[I] || !R[I] || L[I]->GetScriptStruct()!=R[I]->GetScriptStruct()) return false;
   const auto* Type=L[I]->GetScriptStruct();
   if(Type==FCharacterDefaultInputs::StaticStruct())
   {
    if(*static_cast<const FCharacterDefaultInputs*>(L[I].Get())!=*static_cast<const FCharacterDefaultInputs*>(R[I].Get())) return false;
   }
   else if(Type==FMoverDefaultSyncState::StaticStruct())
   {
    const auto& A=*static_cast<const FMoverDefaultSyncState*>(L[I].Get());const auto& B=*static_cast<const FMoverDefaultSyncState*>(R[I].Get());
    if(A.GetLocation_WorldSpace()!=B.GetLocation_WorldSpace() || A.GetOrientation_WorldSpace()!=B.GetOrientation_WorldSpace() ||
     A.GetVelocity_WorldSpace()!=B.GetVelocity_WorldSpace() || A.GetAngularVelocityDegrees_WorldSpace()!=B.GetAngularVelocityDegrees_WorldSpace() ||
     A.GetIntent_WorldSpace()!=B.GetIntent_WorldSpace() || A.GetMovementBase()!=B.GetMovementBase() || A.GetMovementBaseBoneName()!=B.GetMovementBaseBoneName()) return false;
   }
   else if(Type==FProject_JMoverInputs::StaticStruct())
   {
    const auto& A=*static_cast<const FProject_JMoverInputs*>(L[I].Get());const auto& B=*static_cast<const FProject_JMoverInputs*>(R[I].Get());
    if(A.bStrafe!=B.bStrafe || A.bSprint!=B.bSprint || A.bWalk!=B.bWalk || A.MovementDirection!=B.MovementDirection ||
     A.RotationOffset!=B.RotationOffset || A.ControlRotationRate!=B.ControlRotationRate || A.JumpRequestId!=B.JumpRequestId) return false;
   }
   else if(Type==FProject_JMoverLifecycle::StaticStruct())
   {
    const auto& A=*static_cast<const FProject_JMoverLifecycle*>(L[I].Get());const auto& B=*static_cast<const FProject_JMoverLifecycle*>(R[I].Get());
    if(A.AirSequence!=B.AirSequence || A.JumpSequence!=B.JumpSequence || A.LandingSequence!=B.LandingSequence || A.bJumpEpisode!=B.bJumpEpisode ||
     A.bStrafe!=B.bStrafe || A.bSprint!=B.bSprint || A.LandingBrakingTimeMs!=B.LandingBrakingTimeMs || A.LandingVelocity!=B.LandingVelocity) return false;
   }
   else if(Type==FMoverInputContainerDataStruct::StaticStruct())
   {
    if(!SameValues(static_cast<const FMoverInputContainerDataStruct*>(L[I].Get())->InputCollection,
     static_cast<const FMoverInputContainerDataStruct*>(R[I].Get())->InputCollection)) return false;
   }
   else if(!Type->CompareScriptStruct(L[I].Get(),R[I].Get(),PPF_None)) return false;
  }
  return true;
 };
 // The engine wrapper Clone copy-constructs its unreflected nested collection.
 // Detach that known wrapper explicitly in this fixture; collection assignment alone is not enough.
 TFunction<void(FMoverDataCollection&,const FMoverDataCollection&)> CopyValues;
 CopyValues=[&](FMoverDataCollection& Destination,const FMoverDataCollection& Source)
 {
  Destination.Empty();Destination=Source;
  const auto& D=Destination.GetDataArray();const auto& S=Source.GetDataArray();
  TestEqual(TEXT("Value snapshot retains payload count"),D.Num(),S.Num());
  for(int32 I=0;I<D.Num() && I<S.Num();++I)
  {
   const bool Independent=D[I] && S[I] && D[I].Get()!=S[I].Get();
   if(!TestTrue(TEXT("Snapshot payload storage is independent before value comparison"),Independent)) continue;
   if(D[I]->GetScriptStruct()==FMoverInputContainerDataStruct::StaticStruct())
   {
    auto* To=static_cast<FMoverInputContainerDataStruct*>(D[I].Get());
    const auto* From=static_cast<const FMoverInputContainerDataStruct*>(S[I].Get());
    CopyValues(To->InputCollection,From->InputCollection);
   }
  }
 };

 bool bRequireRealSimClock=false;
 bool bObservePredictionIsolation=false;
 bool bObserveOwnedPacket125=false;
 TOptional<FProject_JGroundTrajectoryPrediction> LastOwnedPacket125;
 TArray<FTrajectorySampleInfo> LastOwnedReference125;
 struct FOwnedWorkerResult125 { TArray<FTrajectorySampleInfo> Samples; bool bWorker=false; };
 const auto ComputeOwned125=[](FProject_JGroundTrajectoryPrediction Values125)
 {
  return Async(EAsyncExecution::ThreadPool,[WorkerValues125=MoveTemp(Values125)]
  {
   FOwnedWorkerResult125 Result125;Result125.bWorker=!IsInGameThread();
   Project_J::Movement::ComputePreparedGroundTrajectory(WorkerValues125,Result125.Samples);
   return Result125;
  });
 };
 const auto SameOwnedSamples125=[](const TArray<FTrajectorySampleInfo>& Left125,const TArray<FTrajectorySampleInfo>& Right125)
 {
  if(Left125.Num()!=Right125.Num() || Left125.IsEmpty()) return false;
  for(int32 Index125=0;Index125<Left125.Num();++Index125)
  {
   const auto& SampleLeft125=Left125[Index125];const auto& SampleRight125=Right125[Index125];
   if(SampleRight125.Transform.ContainsNaN() || SampleRight125.LinearVelocity.ContainsNaN() || SampleRight125.InstantaneousAcceleration.ContainsNaN() ||
    SampleRight125.AngularVelocity.ContainsNaN() || !FMath::IsFinite(SampleRight125.SimTimeMs) ||
    !SampleLeft125.Transform.Equals(SampleRight125.Transform,1.e-6) || !SampleLeft125.LinearVelocity.Equals(SampleRight125.LinearVelocity,1.e-6) ||
    !SampleLeft125.InstantaneousAcceleration.Equals(SampleRight125.InstantaneousAcceleration,1.e-6) ||
    !SampleLeft125.AngularVelocity.Equals(SampleRight125.AngularVelocity,1.e-6) || SampleLeft125.SimTimeMs!=SampleRight125.SimTimeMs) return false;
  }
  return true;
 };
 auto Compare=[&](const TCHAR* Name,const FMoverInputCmdContext& Input,const FMoverPredictTrajectoryParams& Query,bool ExpectedPrepared,
  FVector Prior=FVector(160,-30,10),FRotator Facing=FRotator(0,20,0),float LandingMs=0.f,
  FName ModeName=DefaultModeNames::Walking,UPrimitiveComponent* SyncBase=nullptr,bool RootMotion=false,bool MoveBaseAfterPublication=false)
 {
  if(RootMotion) Mover->AddGameplayTag(Mover_AnimRootMotion);
  ON_SCOPE_EXIT {if(RootMotion) Mover->RemoveGameplayTag(Mover_AnimRootMotion);};
  FMoverSyncState Seed;Seed=Mover->GetSyncState();CopyValues(Seed.SyncStateCollection,Mover->GetSyncState().SyncStateCollection);
  Seed.MovementMode=ModeName;
  const FTransform SeedBaseTransform=SyncBase?SyncBase->GetComponentTransform():FTransform::Identity;
  Seed.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().MoveDirectionIntent=FVector(.3,.8,0);
  Seed.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().SetTransforms_WorldSpace(
   FVector(100,200,300),Facing,Prior,FVector(0,0,17),SyncBase);
  auto& Life=Seed.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>();
  Life.LandingBrakingTimeMs=LandingMs;Life.LandingVelocity=FVector(10,20,-37);
  FMoverDataStructBase* Spring=Seed.SyncStateCollection.FindOrAddDataByType(SpringType);
  const FName VectorNames[]={TEXT("SpringVelocity"),TEXT("SpringAcceleration"),TEXT("IntermediateVelocity"),TEXT("IntermediateAngularVelocity")};
  const FVector SpringValues[]={FVector(137,-21,4),FVector(7,3,-1),FVector(130,-18,2),FVector(0,0,13)};
  for(int32 I=0;I<4;++I)
  {
   const auto* Property=FindFProperty<FStructProperty>(SpringType,VectorNames[I]);
   if(!TestNotNull(TEXT("Existing reflected spring vector"),Property)) return false;
   *Property->ContainerPtrToValuePtr<FVector>(Spring)=SpringValues[I];
  }
  const auto* SpringFacing=FindFProperty<FStructProperty>(SpringType,TEXT("IntermediateFacing"));
  if(!TestNotNull(TEXT("Existing reflected spring facing"),SpringFacing)) return false;
  *SpringFacing->ContainerPtrToValuePtr<FQuat>(Spring)=FRotator(0,33,0).Quaternion();
  FMoverAuxStateContext SeedAux; // Explicitly identical empty aux state for both public publications.
  auto Publish=[&]()
  {
   if(SyncBase) SyncBase->SetWorldTransform(SeedBaseTransform);
   FMoverTickStartData Start;CopyValues(Start.InputCmd.InputCollection,Input.InputCollection);
   Start.SyncState=Seed;CopyValues(Start.SyncState.SyncStateCollection,Seed.SyncStateCollection);Start.AuxState=SeedAux;
   FMoverTickEndData EndData;EndData.SyncState=Start.SyncState;CopyValues(EndData.SyncState.SyncStateCollection,Start.SyncState.SyncStateCollection);
   EndData.AuxState=SeedAux;
   FMoverTimeStep Time;Time.ServerFrame=17;Time.BaseSimTimeMs=250.;Time.StepMs=1000.f/60.f;
   Mover->OnPostSimulate(Time,Start,EndData);Mover->FinalizeFrame(&EndData.SyncState,&EndData.AuxState);
   // Restore after publication callbacks, before either query captures live tuning.
   WriteMode(QueryStart);
   if(SyncBase && MoveBaseAfterPublication)
    SyncBase->SetWorldTransform(FTransform(FRotator(-8,73,11),FVector(190,-70,80),FVector(1.3,.8,1.1)));
  };
  auto Unchanged=[&](const FMoverSyncState& Before,const FMoverInputCmdContext& BeforeInput)
  {
   TestTrue(FString::Printf(TEXT("%s: query does not publish predicted physical/private spring values"),Name),
    Before.MovementMode==Mover->GetSyncState().MovementMode && SameValues(Before.SyncStateCollection,Mover->GetSyncState().SyncStateCollection));
   TestTrue(FString::Printf(TEXT("%s: query does not change published last input"),Name),SameValues(BeforeInput.InputCollection,Mover->GetLastInputCmd().InputCollection));
  };
  FMoverInputCmdContext Caller;CopyValues(Caller.InputCollection,Input.InputCollection);
  TArray<FMoverInputCmdContext> CallerOptional;CallerOptional.SetNum(Query.OptionalInputCmds.Num());
  for(int32 I=0;I<CallerOptional.Num();++I) CopyValues(CallerOptional[I].InputCollection,Query.OptionalInputCmds[I].InputCollection);
  FMoverSyncState CallerSync;FMoverAuxStateContext CallerAux;
  if(Query.OptionalStartSyncState.IsSet())
  {
   CallerSync=Query.OptionalStartSyncState.GetValue();CopyValues(CallerSync.SyncStateCollection,Query.OptionalStartSyncState.GetValue().SyncStateCollection);
  }
  if(Query.OptionalStartAuxState.IsSet()) CopyValues(CallerAux.AuxStateCollection,Query.OptionalStartAuxState.GetValue().AuxStateCollection);
  Publish();
  FMoverSyncState Before;Before=Mover->GetSyncState();CopyValues(Before.SyncStateCollection,Mover->GetSyncState().SyncStateCollection);
  FMoverInputCmdContext BeforeInput;CopyValues(BeforeInput.InputCollection,Mover->GetLastInputCmd().InputCollection);
  const int32 Count=Counter->GetInt();if(!TestTrue(TEXT("Counter headroom cannot mask admission"),Count<MAX_int32-4)) return false;
  Enabled->Set(0,ECVF_SetByCode);const auto Reference=Mover->GetPredictedTrajectory(Query);const FModeValues ReferenceFinal=ReadMode();
  TestEqual(FString::Printf(TEXT("%s: CVar0 never consumes prepared branch"),Name),Counter->GetInt(),Count);Unchanged(Before,BeforeInput);
  Publish();
  FMoverSyncState CandidateStart;CandidateStart=Mover->GetSyncState();CopyValues(CandidateStart.SyncStateCollection,Mover->GetSyncState().SyncStateCollection);
  TestTrue(FString::Printf(TEXT("%s: paired publications start at identical owned sync values"),Name),SameValues(Before.SyncStateCollection,CandidateStart.SyncStateCollection));
  TestTrue(FString::Printf(TEXT("%s: paired publications start with identical input"),Name),SameValues(BeforeInput.InputCollection,Mover->GetLastInputCmd().InputCollection));
  const FModeValues Restored=ReadMode();for(int32 I=0;I<5;++I) TestEqual(TEXT("Each paired query starts from the original five mode writers"),Restored.Values[I],QueryStart.Values[I]);
  Enabled->Set(1,ECVF_SetByCode);const auto Candidate=Mover->GetPredictedTrajectory(Query);const FModeValues CandidateFinal=ReadMode();
  TestEqual(FString::Printf(TEXT("%s: actual successful prepared query count"),Name),Counter->GetInt(),Count+(ExpectedPrepared?1:0));
  TestEqual(FString::Printf(TEXT("%s: legacy sample cardinality"),Name),Reference.Num(),Query.NumPredictionSamples);
  TestEqual(FString::Printf(TEXT("%s: prepared/fallback sample cardinality"),Name),Candidate.Num(),Query.NumPredictionSamples);
  if(bRequireRealSimClock)
   TestTrue(TEXT("The real backend has advanced simulation time before this paired query"),!Reference.IsEmpty() && !Candidate.IsEmpty() && Reference[0].SimTimeMs>0. && Candidate[0].SimTimeMs>0.);
  bool Same=Reference.Num()==Query.NumPredictionSamples && Candidate.Num()==Query.NumPredictionSamples;
  int32 FirstDifferent=INDEX_NONE;
  for(int32 I=0;Same && I<Query.NumPredictionSamples;++I)
  {
   const auto& A=Reference[I];const auto& B=Candidate[I];
   Same=!A.Transform.ContainsNaN() && !B.Transform.ContainsNaN() && !A.LinearVelocity.ContainsNaN() && !B.LinearVelocity.ContainsNaN() &&
    !A.InstantaneousAcceleration.ContainsNaN() && !B.InstantaneousAcceleration.ContainsNaN() && !A.AngularVelocity.ContainsNaN() && !B.AngularVelocity.ContainsNaN() &&
    FMath::IsFinite(A.SimTimeMs) && FMath::IsFinite(B.SimTimeMs) && A.Transform.Equals(B.Transform,1.e-6) &&
    A.LinearVelocity.Equals(B.LinearVelocity,1.e-6) && A.InstantaneousAcceleration.Equals(B.InstantaneousAcceleration,1.e-6) &&
    A.AngularVelocity.Equals(B.AngularVelocity,1.e-6) && A.SimTimeMs==B.SimTimeMs;
   if(!Same) FirstDifferent=I;
  }
  TestTrue(FString::Printf(TEXT("%s: every transform/velocity/acceleration/angular/time is finite and equivalent (first mismatch %d)"),Name,FirstDifferent),Same);
  if(!Same && Reference.IsValidIndex(FirstDifferent) && Candidate.IsValidIndex(FirstDifferent))
  {
   const auto& A=Reference[FirstDifferent];const auto& B=Candidate[FirstDifferent];
   AddInfo(FString::Printf(TEXT("%s sample%d positionDelta=%.12g velocityDelta=%.12g accelerationDelta=%.12g angularDelta=%.12g time=%.12g/%.12g"),Name,
    FirstDifferent,FVector::Distance(A.Transform.GetLocation(),B.Transform.GetLocation()),FVector::Distance(A.LinearVelocity,B.LinearVelocity),
    FVector::Distance(A.InstantaneousAcceleration,B.InstantaneousAcceleration),(A.AngularVelocity-B.AngularVelocity).Euler().Size(),A.SimTimeMs,B.SimTimeMs));
  }
  for(int32 I=0;I<5;++I) TestEqual(FString::Printf(TEXT("%s: %s mode writer %s"),Name,ExpectedPrepared?TEXT("admitted native query preserves"):TEXT("fallback retains legacy"),*FieldNames[I].ToString()),
   CandidateFinal.Values[I],ExpectedPrepared?QueryStart.Values[I]:ReferenceFinal.Values[I]);
  Unchanged(CandidateStart,BeforeInput);
  const auto* CharacterInput=Input.InputCollection.FindDataByType<FCharacterDefaultInputs>();
  if(ModeName==DefaultModeNames::Walking && CharacterInput && CharacterInput->GetMoveInputType()==EMoveInputType::Velocity)
   for(int32 I=0;I<5;++I) TestEqual(TEXT("Velocity bypass preserves every original mode field"),CandidateFinal.Values[I],QueryStart.Values[I]);
  if(ModeName==DefaultModeNames::Walking && !Query.OptionalStartSyncState.IsSet() && (!CharacterInput || CharacterInput->GetMoveInput().IsNearlyZero()) && LandingMs>0.f)
   TestEqual(TEXT("Legacy landing prediction retains actual 20000 braking writer inside its solver"),ReferenceFinal.Values[2],20000.f);
  TestTrue(TEXT("Caller command remains unchanged"),SameValues(Caller.InputCollection,Input.InputCollection));
  for(int32 I=0;I<CallerOptional.Num();++I) TestTrue(TEXT("Optional caller command remains unchanged"),SameValues(CallerOptional[I].InputCollection,Query.OptionalInputCmds[I].InputCollection));
  if(Query.OptionalStartSyncState.IsSet()) TestTrue(TEXT("Optional caller sync remains unchanged"),CallerSync.MovementMode==Query.OptionalStartSyncState.GetValue().MovementMode && SameValues(CallerSync.SyncStateCollection,Query.OptionalStartSyncState.GetValue().SyncStateCollection));
  if(Query.OptionalStartAuxState.IsSet()) TestTrue(TEXT("Optional caller aux remains unchanged"),SameValues(CallerAux.AuxStateCollection,Query.OptionalStartAuxState.GetValue().AuxStateCollection));
  if(bObserveOwnedPacket125 && ExpectedPrepared)
  {
   auto PacketQuery125=Query;PacketQuery125.bDisableGravity=true;
   const FVector GravityBefore125=Mover->GetGravityAcceleration();
   const auto ModeBefore125=ReadMode();FFloorCheckResult FloorBefore125;
   const bool HadFloor125=Blackboard->TryGet(CommonBlackboard::LastFloorResult,FloorBefore125);
   const auto Synchronous125=Mover->GetPredictedTrajectory(PacketQuery125);
   FProject_JGroundTrajectoryPrediction Packet125;
   if(!TestTrue(FString::Printf(TEXT("%s: GT capture admits a detached packet"),Name),Mover->TryPrepareGroundTrajectory(PacketQuery125,Packet125))) return false;
   auto WorkerFuture125=ComputeOwned125(Packet125);const auto Worker125=WorkerFuture125.Get();
   TestTrue(TEXT("Owned packet calculation actually executes on ThreadPool"),Worker125.bWorker);
   TestTrue(TEXT("Every owned worker transform/velocity/acceleration/angular/time matches synchronous query"),SameOwnedSamples125(Synchronous125,Worker125.Samples));
   TestEqual(TEXT("Owning packet retains requested sample cardinality"),Packet125.NumPredictionSamples,PacketQuery125.NumPredictionSamples);
   TestTrue(TEXT("Capture and worker leave actual gravity override unchanged"),Mover->GetGravityAcceleration().Equals(GravityBefore125,1.e-6));
   const auto ModeAfter125=ReadMode();for(int32 Field125=0;Field125<5;++Field125)
    TestEqual(TEXT("GT capture/worker cannot write live movement mode tuning"),ModeAfter125.Values[Field125],ModeBefore125.Values[Field125]);
   FFloorCheckResult FloorAfter125;TestEqual(TEXT("Prediction preserves physical floor cache presence"),Blackboard->TryGet(CommonBlackboard::LastFloorResult,FloorAfter125),HadFloor125);
   TestTrue(TEXT("Prediction preserves cached physical floor values"),!HadFloor125 ||
    (FloorBefore125.bBlockingHit==FloorAfter125.bBlockingHit && FloorBefore125.bWalkableFloor==FloorAfter125.bWalkableFloor && FloorBefore125.bLineTrace==FloorAfter125.bLineTrace &&
     FloorBefore125.LineDist==FloorAfter125.LineDist && FloorBefore125.FloorDist==FloorAfter125.FloorDist &&
     FHitResult::StaticStruct()->CompareScriptStruct(&FloorBefore125.HitResult,&FloorAfter125.HitResult,PPF_None)));
   Unchanged(CandidateStart,BeforeInput);
   LastOwnedPacket125=MoveTemp(Packet125);LastOwnedReference125=Synchronous125;
  }
  if(bObservePredictionIsolation)
  {
   struct FPhysicalObservation { FProposedMove Raw,Admitted;FModeValues QueryFinal,AdmittedFinal; } Observed[3];
   const int32 QueryCounts[]={0,1,3};
   if(!TestTrue(TEXT("Isolation probe has native counter headroom for all real queries"),Counter->GetInt()<MAX_int32-8)) return false;
   const auto Generate=[&](bool WithAdmission)
   {
    // Reuse the same real native mode and physical seed. Each arm owns an empty
    // normal simulation receipt, just as the first native generation in119 does.
    const TStrongObjectPtr<URollbackBlackboard> Receipt(NewObject<URollbackBlackboard>(Mover));
    FMoverSimContext Context;Context.SimulationOwner=Mover;Context.Blackboard=FRollbackBlackboardSimWrapper(*Receipt);
    auto EntrySettings=URollbackBlackboardLibrary::MakeSingleFrameEntrySettings();
    EntrySettings.PersistencePolicy=EBlackboardPersistencePolicy::ThroughNextFrame;
    TestTrue(TEXT("Physical generation has an independently owned native receipt"),Context.Blackboard.CreateEntry<bool>(TEXT("DidGenerateMove"),EntrySettings));
    FMoverTickStartData Physical;CopyValues(Physical.InputCmd.InputCollection,Input.InputCollection);
    Physical.SyncState=Seed;CopyValues(Physical.SyncState.SyncStateCollection,Seed.SyncStateCollection);Physical.AuxState=SeedAux;
    FMoverTimeStep PhysicalStep;PhysicalStep.ServerFrame=17;PhysicalStep.BaseSimTimeMs=250.;PhysicalStep.StepMs=1000.f/60.f;
    if(WithAdmission) Mover->OnPreSimulate(PhysicalStep,Physical);
    Receipt->BeginSimulationFrame(PhysicalStep);
    FProposedMove Move;Ground->GenerateMove(Context,Physical,PhysicalStep,Move);
    Receipt->EndSimulationFrame();
    return Move;
   };
   for(int32 Arm=0;Arm<3;++Arm)
   {
    Settings->MaxSpeed=1000.f;Publish();Enabled->Set(1,ECVF_SetByCode);
    const int32 BeforeCount=Counter->GetInt();
    for(int32 Invocation=0;Invocation<QueryCounts[Arm];++Invocation)
    {
     const auto Future=Mover->GetPredictedTrajectory(Query);
     TestEqual(TEXT("Isolation probe keeps the original native future sample count"),Future.Num(),Query.NumPredictionSamples);
     bool SameFuture=Future.Num()==Candidate.Num();int32 FutureFirstDifferent=INDEX_NONE;
     for(int32 SampleIndex=0;SameFuture&&SampleIndex<Future.Num();++SampleIndex)
     {
      const auto& A=Candidate[SampleIndex];const auto& B=Future[SampleIndex];
      SameFuture=!B.Transform.ContainsNaN()&&!B.LinearVelocity.ContainsNaN()&&!B.InstantaneousAcceleration.ContainsNaN()&&!B.AngularVelocity.ContainsNaN()&&FMath::IsFinite(B.SimTimeMs)&&
       A.Transform.Equals(B.Transform,1.e-6)&&A.LinearVelocity.Equals(B.LinearVelocity,1.e-6)&&A.InstantaneousAcceleration.Equals(B.InstantaneousAcceleration,1.e-6)&&
       A.AngularVelocity.Equals(B.AngularVelocity,1.e-6)&&A.SimTimeMs==B.SimTimeMs;
      if(!SameFuture)FutureFirstDifferent=SampleIndex;
     }
     TestTrue(FString::Printf(TEXT("Same physical state/input/time gives the same native future for count%d invocation%d (first mismatch%d)"),QueryCounts[Arm],Invocation,FutureFirstDifferent),SameFuture);
     Unchanged(Before,BeforeInput);
    }
    TestEqual(TEXT("Isolation probe observes every real native prediction admission"),Counter->GetInt(),BeforeCount+QueryCounts[Arm]);
    Observed[Arm].QueryFinal=ReadMode();
    for(int32 Field=0;Field<5;++Field)
     TestEqual(FString::Printf(TEXT("Prediction count%d leaves live native mode writer %s unchanged"),QueryCounts[Arm],*FieldNames[Field].ToString()),Observed[Arm].QueryFinal.Values[Field],QueryStart.Values[Field]);
    // The raw direct call is a diagnostic positive control, not the authoritative
    // path: production NP/Standalone call OnPreSimulate before real generation.
    Observed[Arm].Raw=Generate(false);WriteMode(Observed[Arm].QueryFinal);
    Observed[Arm].Admitted=Generate(true);Observed[Arm].AdmittedFinal=ReadMode();
    const auto& Move=Observed[Arm].Admitted;
    TestTrue(TEXT("Admitted next physical generation is finite"),!Move.LinearVelocity.ContainsNaN()&&!Move.AngularVelocityDegrees.ContainsNaN()&&!Move.DirectionIntent.ContainsNaN());
    AddInfo(FString::Printf(TEXT("PredictionIsolation %s calls=%d mode_after_query=[%.9g,%.9g,%.9g,%.9g,%.9g] raw_velocity=%s admitted_velocity=%s admitted_angular=%s admitted_max=%.9g"),
     Name,QueryCounts[Arm],Observed[Arm].QueryFinal.Values[0],Observed[Arm].QueryFinal.Values[1],Observed[Arm].QueryFinal.Values[2],Observed[Arm].QueryFinal.Values[3],Observed[Arm].QueryFinal.Values[4],
     *Observed[Arm].Raw.LinearVelocity.ToString(),*Move.LinearVelocity.ToString(),*Move.AngularVelocityDegrees.ToString(),Observed[Arm].AdmittedFinal.Values[0]));
   }
   for(int32 Arm=1;Arm<3;++Arm)
   {
    const auto& A=Observed[0].Admitted;const auto& B=Observed[Arm].Admitted;
    TestTrue(FString::Printf(TEXT("%s: 0 versus%d animation queries cannot steer the next admitted native physical generation"),Name,QueryCounts[Arm]),
     A.LinearVelocity.Equals(B.LinearVelocity,1.e-6)&&A.AngularVelocityDegrees.Equals(B.AngularVelocityDegrees,1.e-6)&&
     A.DirectionIntent.Equals(B.DirectionIntent,1.e-6)&&A.bHasDirIntent==B.bHasDirIntent);
    for(int32 Field=0;Field<5;++Field)
     TestEqual(FString::Printf(TEXT("%s: admitted physical writer %s is independent of query count%d"),Name,*FieldNames[Field].ToString(),QueryCounts[Arm]),Observed[Arm].AdmittedFinal.Values[Field],Observed[0].AdmittedFinal.Values[Field]);
    AddInfo(FString::Printf(TEXT("PredictionIsolation %s calls=%d raw_without_admission_delta=%.12g admitted_delta=%.12g"),Name,QueryCounts[Arm],
     FVector::Distance(Observed[0].Raw.LinearVelocity,Observed[Arm].Raw.LinearVelocity),FVector::Distance(A.LinearVelocity,B.LinearVelocity)));
   }
   Settings->MaxSpeed=1000.f;Publish();WriteMode(CandidateFinal);
  }
  AddInfo(FString::Printf(TEXT("%s samples=%d interval=%.9g prepared=%d firstSimTime=%.12g finalMax=%.9g"),Name,Query.NumPredictionSamples,Query.SecondsPerSample,
   ExpectedPrepared,Reference.IsEmpty()?-1.:Reference[0].SimTimeMs,CandidateFinal.Values[0]));
  return Same;
 };
 FMoverPredictTrajectoryParams Query;Query.NumPredictionSamples=92;Query.SecondsPerSample=1.f/60.f;Query.bUseVisualComponentRoot=true;
 FMoverInputCmdContext Run;auto& RunInput=Run.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>();
 RunInput.SetMoveInput(EMoveInputType::DirectionalIntent,FVector(.8,.2,0));RunInput.OrientationIntent=FVector(.6,.8,0);
 auto& RunIntent=Run.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>();RunIntent.ControlRotationRate=230.f;
 bObserveOwnedPacket125=true;
 Compare(TEXT("Directional run reads old225 cap before new500 writer"),Run,Query,true);
 bObserveOwnedPacket125=false;
 TestEqual(TEXT("Run native prediction preserves the prequery cap"),ReadMode().Values[0],QueryStart.Values[0]);
 FMoverInputCmdContext Walk;CopyValues(Walk.InputCollection,Run.InputCollection);
 Walk.InputCollection.FindMutableDataByType<FProject_JMoverInputs>()->bWalk=true;
 TestTrue(TEXT("Walk isolation has distinct prior and authored speed caps"),!FMath::IsNearlyEqual(QueryStart.Values[0],Mover->GroundMovementSettings.WalkSpeed));
 bObservePredictionIsolation=true;
 Compare(TEXT("Walk query isolation with nonzero physical velocity"),Walk,Query,true,FVector(160,30,0),FRotator(0,20,0));
 bObservePredictionIsolation=false;
 FMoverInputCmdContext Sprint;CopyValues(Sprint.InputCollection,Run.InputCollection);
 auto* SprintIntent=Sprint.InputCollection.FindMutableDataByType<FProject_JMoverInputs>();SprintIntent->bSprint=true;SprintIntent->bStrafe=true;
 SprintIntent->RotationOffset=-175.f;SprintIntent->ControlRotationRate=-120.f;
 Sprint.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>()->OrientationIntent=FVector::ForwardVector;
 Compare(TEXT("Sprint strafe speed boundary and large facing turn"),Sprint,Query,true,FVector(500.01,0,0),FRotator(0,175,0));
 TestEqual(TEXT("Sprint native prediction preserves the prequery cap"),ReadMode().Values[0],QueryStart.Values[0]);
 FMoverInputCmdContext Idle;CopyValues(Idle.InputCollection,Run.InputCollection);
 Idle.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>()->SetMoveInput(EMoveInputType::DirectionalIntent,FVector::ZeroVector);
 Compare(TEXT("Idle landing braking"),Idle,Query,true,FVector(210,20,0),FRotator(0,25,0),140.f);
 FMoverInputCmdContext Velocity;CopyValues(Velocity.InputCollection,Run.InputCollection);
 Velocity.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>()->SetMoveInput(EMoveInputType::Velocity,FVector(600,-40,0));
 Compare(TEXT("Native NPC velocity bypass"),Velocity,Query,true);
 Compare(TEXT("Velocity with actual external root-motion tag"),Velocity,Query,true,FVector(170,15,0),FRotator(0,10,0),0.f,DefaultModeNames::Walking,nullptr,true);
 TestFalse(TEXT("Actual root-motion tag is released after its cohort"),Mover->HasGameplayTag(Mover_AnimRootMotion,false));
 FMoverInputCmdContext Empty;Compare(TEXT("No input evolves sync-facing fallback"),Empty,Query,true,FVector(150,10,0),FRotator(0,110,0));
 Mover->SetGravityOverride(true,FVector(0,-700,-700));
 bObserveOwnedPacket125=true;
 Compare(TEXT("Tilted gravity and native cached slope"),Run,Query,true,FVector(120,30,10),FRotator(12,40,7));
 bObserveOwnedPacket125=false;
 Mover->SetGravityOverride(false);
 auto Sparse=Query;Sparse.NumPredictionSamples=13;Sparse.SecondsPerSample=1.f/37.f;Sparse.bUseVisualComponentRoot=false;
 Compare(TEXT("Variable sample count/rate"),Sprint,Sparse,true,FVector(501,0,0),FRotator(0,-150,0));
 FMoverInputCmdContext Based;CopyValues(Based.InputCollection,Run.InputCollection);
 auto* BasedInput=Based.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>();BasedInput->bUsingMovementBase=true;BasedInput->MovementBase=Base;
 Compare(TEXT("Concrete native input base is captured once"),Based,Query,true);
 Base->SetWorldTransform(FTransform(FRotator(12,-31,7),FVector(30,40,-10),FVector(.8,1.4,1.1)));
 Compare(TEXT("Rotated concrete input base uses current world directions"),Based,Query,true);
 Compare(TEXT("Input and sync bases retain independent cached/current poses"),Based,Query,true,FVector(100,0,0),FRotator(12,40,7),0.f,DefaultModeNames::Walking,Base,false,true);
 Compare(TEXT("Concrete native sync base retains local/world roundtrips"),Run,Query,true,FVector(100,0,0),FRotator(0,20,0),0.f,DefaultModeNames::Walking,Base);
 Compare(TEXT("Cached initial base differs from current component pose"),Run,Query,true,FVector(100,0,0),FRotator(12,40,7),0.f,DefaultModeNames::Walking,Base,false,true);
 Base->SetWorldTransform(FTransform(FRotator(9,-28,4),FVector(30,50,-10)));
 Compare(TEXT("No-input based intent switches from cached to current base pose"),Empty,Query,true,FVector(130,10,0),FRotator(0,110,0),0.f,DefaultModeNames::Walking,Base,false,true);
 auto* UnsupportedBase=NewObject<USkeletalMeshComponent>(Avatar);Avatar->AddInstanceComponent(UnsupportedBase);
 UnsupportedBase->SetCollisionEnabled(ECollisionEnabled::NoCollision);UnsupportedBase->RegisterComponent();
 Compare(TEXT("Skeletal/custom sync base retains generic fallback"),Run,Query,false,FVector(100,0,0),FRotator(0,20,0),0.f,DefaultModeNames::Walking,UnsupportedBase);
 FMoverInputCmdContext UnsupportedInput;CopyValues(UnsupportedInput.InputCollection,Based.InputCollection);
 UnsupportedInput.InputCollection.FindMutableDataByType<FCharacterDefaultInputs>()->MovementBase=UnsupportedBase;
 Compare(TEXT("Skeletal/custom input base retains generic fallback"),UnsupportedInput,Query,false);
 auto Optional=Query;Optional.OptionalInputCmds.SetNum(1);CopyValues(Optional.OptionalInputCmds[0].InputCollection,Run.InputCollection);
 Compare(TEXT("Optional command retains generic fallback"),Run,Optional,false);
 auto OptionalTime=Query;OptionalTime.OptionalStartTime=FMoverTime();Compare(TEXT("Optional time retains generic fallback"),Run,OptionalTime,false);
 auto OptionalSync=Query;OptionalSync.OptionalStartSyncState.Emplace();
 OptionalSync.OptionalStartSyncState.GetValue().MovementMode=DefaultModeNames::Walking;
 OptionalSync.OptionalStartSyncState.GetValue().SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().SetTransforms_WorldSpace(
  FVector(10,20,30),FRotator(0,45,0),FVector(110,20,0),FVector(0,0,13));
 OptionalSync.OptionalStartSyncState.GetValue().SyncStateCollection.FindOrAddDataByType(SpringType);
 Compare(TEXT("Optional sync retains generic fallback and caller spring"),Run,OptionalSync,false);
 auto OptionalAux=Query;OptionalAux.OptionalStartAuxState.Emplace();
 OptionalAux.OptionalStartAuxState.GetValue().AuxStateCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>().RotationOffset=19.f;
 Compare(TEXT("Optional aux retains generic fallback"),Run,OptionalAux,false);
 Compare(TEXT("Existing Flying mode is not prepared ground"),Run,Query,false,FVector(100,0,0),FRotator(0,20,0),0.f,DefaultModeNames::Flying);
 Compare(TEXT("Existing Attached mode is not prepared ground"),Run,Query,false,FVector(100,0,0),FRotator(0,20,0),0.f,TEXT("Attached"));
 // Also cover readable predictive receipts after actual backend simulation,
 // without replacing timestamps or calling private blackboard frame APIs.
 // The earlier query-only publications did not write the backend pending frame.
 // Resume physics through the existing authority handover before real ticking.
 if(!TestTrue(TEXT("Resume the backend through its public authority handover"),Mover->ApplyAuthorityHandoverTransform(Avatar->GetActorTransform()))) return false;
 for(int32 I=0;I<3;++I) World->Tick(LEVELTICK_All,1.f/60.f);
 bRequireRealSimClock=true;
 Compare(TEXT("Real simulation clock retains readable spring receipts"),Run,Query,true);
 Compare(TEXT("Real simulation clock with sprint turn"),Sprint,Sparse,true,FVector(501,0,0),FRotator(0,-150,0));
 // Only the owned value packet survives this actual source retirement; the worker captures no UObject/blackboard/test pointers.
 if(!TestTrue(TEXT("Representative queries retained an owned packet"),LastOwnedPacket125.IsSet())) return false;
 const TWeakObjectPtr<AProject_JSpellbladeCharacter> RetiredAvatar125(Avatar);const TWeakObjectPtr<UProject_JMoverComponent> RetiredMover125(Mover);
 TestTrue(TEXT("Actual packet source retires through normal Actor destruction"),Avatar->Destroy());
 Avatar=nullptr;Mover=nullptr;Ground=nullptr;Settings=nullptr;
 CollectGarbage(RF_NoFlags,true);
 TestFalse(TEXT("Source Actor is invalid after real GC"),RetiredAvatar125.IsValid());
 TestFalse(TEXT("Source Mover is invalid after real GC"),RetiredMover125.IsValid());
 auto RetiredFuture125=ComputeOwned125(LastOwnedPacket125.GetValue());const auto RetiredResult125=RetiredFuture125.Get();
 TestTrue(TEXT("Detached prediction still executes on ThreadPool after source GC"),RetiredResult125.bWorker);
 TestTrue(TEXT("Source GC cannot change any owned prediction output sample"),SameOwnedSamples125(LastOwnedReference125,RetiredResult125.Samples));
 AddInfo(TEXT("Same-state legacy/prepared futures and native read-only mode isolation; unsupported fallback retains legacy writers. Next native generation includes actual OnPreSimulate admission. No collision/socket/performance or worker scheduling claim."));
 return !HasAnyErrors();
}

#endif
