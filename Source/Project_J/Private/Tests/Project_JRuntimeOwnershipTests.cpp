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
#include "Components/Project_JCharacterMovementComponent.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Components/Project_JCombatStateComponent.h"
#include "Project_JAbilitySystemComponent.h"

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
    auto* Move = Cast<UProject_JCharacterMovementComponent>(Avatar->GetCharacterMovement());
    if (!TestNotNull(TEXT("Authored player uses prediction-aware movement"), Move)) return false;
    auto* ASC = State->GetAbilitySystemComponent();
    const auto Handle = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    const auto* Profile = Avatar->GetLocomotionProfile();
    if (!TestNotNull(TEXT("Authored movement policy"), Profile)) return false;
    TestTrue(TEXT("Sprint ability activates"), ASC->TryActivateAbility(Handle));
    Move->UpdateFromCompressedFlags(FSavedMove_Character::FLAG_Custom_0);
    Move->MoveAutonomous(0.1f, 0.016f, FSavedMove_Character::FLAG_Custom_0, FVector(1000, 0, 0));
    TestEqual(TEXT("Server applies sprint speed before the move"), Move->MaxWalkSpeed, Profile->SprintSpeed);
    TestEqual(TEXT("Sprint acceleration policy is applied"), Move->MaxAcceleration, Profile->MovementPolicy.SprintMaxAcceleration);
    auto* Data = Move->GetPredictionData_Client_Character();
    FSavedMovePtr Sprint = Data->AllocateNewMove();
    Sprint->SetMoveFor(Avatar, 0.016f, FVector(1000, 0, 0), *Data);
    TestTrue(TEXT("Sprint intent is encoded in this move"), (Sprint->GetCompressedFlags() & FSavedMove_Character::FLAG_Custom_0) != 0);
    FSavedMovePtr Walk = Data->AllocateNewMove();
    Walk->Clear();
    TestFalse(TEXT("Sprint boundary cannot be combined away"), Sprint->CanCombineWith(Walk, Avatar, 0.125f));
    Move->UpdateFromCompressedFlags(0);
    Sprint->PrepMoveFor(Avatar);
    TestTrue(TEXT("Replay restores historical sprint intent"), Move->IsSprintRequestedForMove());
    Move->MoveAutonomous(0.2f, 0.016f, 0, FVector(1000, 0, 0));
    TestEqual(TEXT("Released sprint applies walk policy even while GAS end is pending"), Move->MaxWalkSpeed, Profile->WalkSpeed);
    ASC->CancelAbilityHandle(Handle);
    Move->MoveAutonomous(0.3f, 0.016f, FSavedMove_Character::FLAG_Custom_0, FVector(1000, 0, 0));
    TestEqual(TEXT("Client flag cannot grant unauthorised sprint"), Move->MaxWalkSpeed, Profile->WalkSpeed);
    TestEqual(TEXT("Run acceleration is restored"), Move->MaxAcceleration, Profile->MovementPolicy.RunMaxAcceleration);
    Sprint->Clear();
    TestFalse(TEXT("Recycled saved move clears sprint flag"), (Sprint->GetCompressedFlags() & FSavedMove_Character::FLAG_Custom_0) != 0);
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
