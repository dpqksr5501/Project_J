#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"
#include "GameFramework/WorldSettings.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Components/Project_JInventoryComponent.h"
#include "Mount/Project_JMountItemDefinition.h"
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
#include "Movement/Project_JMoverComponent.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "DefaultMovementSet/Settings/CommonLegacyMovementSettings.h"
#include "MoveLibrary/MovementUtils.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/CapsuleComponent.h"

namespace ProjectJRuntimeOwnershipTests
{
// Use the authored locomotion defaults, without starting equipment/BP side effects.
AProject_JGreatswordCharacter* SpawnAvatar(UWorld* World, AProject_JPlayerState* State)
{
    UClass* AuthoredClass = LoadClass<AProject_JPlayerCharacter>(nullptr,
        TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
    auto* Property = FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), TEXT("CharacterAnimProfile"));
    auto* Authored = AuthoredClass && Property
        ? Cast<UProject_JCharacterAnimProfile>(Property->GetObjectPropertyValue_InContainer(AuthoredClass->GetDefaultObject())) : nullptr;
    if (!Authored) return nullptr;
    auto* Avatar = World->SpawnActorDeferred<AProject_JGreatswordCharacter>(AProject_JGreatswordCharacter::StaticClass(),
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
    Avatar->RequestUseMountItem(Instance.InstanceId);
    auto* First = Avatar->GetSummonedMount();
    if (!TestNotNull(TEXT("First summoned mount"), First)) return false;
    TestTrue(TEXT("Destroy succeeds"), First->Destroy());
    TestEqual(TEXT("Destroyed mount rejects rider before GC"), First->GetMountEligibilityFailure(Avatar), EProject_JMountEligibilityFailure::MountUnavailable);
    Avatar->RequestUseMountItem(Instance.InstanceId);
    auto* Second = Avatar->GetSummonedMount();
    TestTrue(TEXT("A different live mount is spawned immediately"), IsValid(Second) && Second != First);
    TestTrue(TEXT("No possession transfer into destroyed mount"), Controller->GetPawn() == Avatar);
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
    TestEqual(TEXT("Actual ground solver retains the original SmoothWalking parent"),GroundMode->GetClass()->GetSuperClass()->GetFName(),FName(TEXT("SmoothWalkingMode")));
    const auto* FormerView=LoadObject<UScriptStruct>(nullptr,TEXT("/Game/Animation_Logic/PlayerMovement/S_MoverCustomInputs.S_MoverCustomInputs"));
    TestNotNull(TEXT("Former authored input schema remains available for reference"),FormerView);
    TestNull(TEXT("Native ground solver no longer adds a Blueprint input projection"),Walking.InputCmd.InputCollection.FindDataByType(FormerView));
    TestNull(TEXT("Historical input is also free of the retired projection"),Historical.InputCollection.FindDataByType(FormerView));
    TestEqual(TEXT("Unapproved sprint is capped at authored walk speed"), WalkInput->GetMoveInput().Size(), double(Profile->WalkSpeed));
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
    TestEqual(TEXT("Approved sprint respects authored speed"), Sprinting.InputCmd.InputCollection.FindDataByType<FCharacterDefaultInputs>()->GetMoveInput().Size(), double(Profile->SprintSpeed));
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
    return true;
}
#endif
