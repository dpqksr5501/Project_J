#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/SkeletalMeshComponent.h"



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLocomotionTagOwnershipTest, "ProjectJ.RuntimeOwnership.LocomotionTagLease",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLocomotionTagOwnershipTest::RunTest(const FString&)
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
    auto* State = Player ? Player->GetLocomotionAnimStateComponent() : nullptr;
    if (!TestNotNull(TEXT("Registered locomotion owner"), State)) { return false; }
    auto* Property = FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"));
    if (!TestNotNull(TEXT("Native ASC owner seam"), Property)) { return false; }
    auto MakeASC = [&]() {
        auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player);
        ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player); return ASC;
    };
    auto* First = MakeASC(); auto* Second = MakeASC();
    auto Bind = [&](UAbilitySystemComponent* ASC) {
        Property->SetObjectPropertyValue_InContainer(Player, ASC); State->RefreshCachedReferences();
    };
    const auto& Tags = FProject_JGameplayTags::Get();
    Bind(First);
    // Other systems own one contribution each. This component may retire only its own.
    First->AddProjectJLooseGameplayTag(Tags.State_Movement_InAir);
    Second->AddProjectJLooseGameplayTag(Tags.State_Movement_InAir);
    bool bReboundDuringAdd = false;
    const auto Handle = First->RegisterGameplayTagEvent(Tags.State_Movement_InAir, EGameplayTagEventType::AnyCountChange)
        .AddLambda([&](FGameplayTag, int32 Count) {
            if (!bReboundDuringAdd && Count == 2) { bReboundDuringAdd = true; Bind(Second); }
        });
    State->AddOwnedInAirGameplayTag();
    TestTrue(TEXT("Actual synchronous tag delegate changed the ASC"), bReboundDuringAdd);
    TestEqual(TEXT("Old ASC retains only the unrelated owner's contribution"), First->GetTagCount(Tags.State_Movement_InAir), 1);
    TestEqual(TEXT("New ASC acquires exactly one locomotion contribution"), Second->GetTagCount(Tags.State_Movement_InAir), 2);
    First->RegisterGameplayTagEvent(Tags.State_Movement_InAir, EGameplayTagEventType::AnyCountChange).Remove(Handle);
    State->RemoveOwnedInAirGameplayTag();
    TestEqual(TEXT("Release removes only the new owner's lease"), Second->GetTagCount(Tags.State_Movement_InAir), 1);

    // Demand survives a real cache gap; it must not decrement an unrelated ASC.
    Bind(First); State->AddOwnedLandingGameplayTag(); Bind(nullptr);
    TestEqual(TEXT("Null ASC gap releases the original landing contribution"), First->GetTagCount(Tags.State_Movement_Landing), 0);
    Bind(Second);
    TestEqual(TEXT("Late valid ASC acquires the outstanding landing demand once"), Second->GetTagCount(Tags.State_Movement_Landing), 1);
    State->RemoveOwnedLandingGameplayTag();
    State->bIsLanding = State->bLandingRequested = true;
    Bind(First); Bind(Second);
    TestEqual(TEXT("Cosmetic landing alone does not invent a gameplay tag demand"), Second->GetTagCount(Tags.State_Movement_Landing), 0);
    State->AddOwnedLandingGameplayTag();
    State->DestroyComponent();
    TestEqual(TEXT("Destruction before BeginPlay releases the owned landing lease"), Second->GetTagCount(Tags.State_Movement_Landing), 0);
    TestEqual(TEXT("Destruction preserves an unrelated air contribution"), Second->GetTagCount(Tags.State_Movement_InAir), 1);
    return true;
}
#endif
