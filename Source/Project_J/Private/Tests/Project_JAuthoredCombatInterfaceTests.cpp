#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SkeletalMeshComponent.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JCombatInterface.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "CharacterClass/Project_JProgressionComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredCombatInterfaceTest,
    "ProjectJ.AssetArchitecture.AuthoredCombatInterface",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredCombatInterfaceTest::RunTest(const FString&)
{
    UClass* AuthoredClass = LoadClass<AProject_JPlayerCharacter>(nullptr,
        TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
    if (!TestNotNull(TEXT("Actual production Blueprint class"), AuthoredClass)) return false;
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    if (!TestNotNull(TEXT("Test world"), World)) return false;
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT
    {
        if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::LevelTransition);
        World->DestroyWorld(false);
        GEngine->DestroyWorldContext(World);
    };
    World->InitializeActorsForPlay(FURL());
    World->GetWorldSettings()->NotifyBeginPlay();
    World->GetWorldSettings()->NotifyMatchStarted();
    auto* State = World->SpawnActor<AProject_JPlayerState>();
    auto* Controller = World->SpawnActor<APlayerController>();
    if (!TestNotNull(TEXT("Persistent state"), State) || !TestNotNull(TEXT("Controller"), Controller)) return false;
    State->SetOwner(Controller);
    Controller->SetPlayerState(State);
    const FTransform SpawnTransform(FRotator::ZeroRotator, FVector(321, 654, 987));
    // Spawn the actual BP, including its authored defaults and construction script.
    auto* Avatar = World->SpawnActorDeferred<AProject_JPlayerCharacter>(AuthoredClass, SpawnTransform,
        nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
    if (!TestNotNull(TEXT("Authored avatar"), Avatar)) return false;
    Avatar->SetPlayerState(State);
    Avatar->FinishSpawning(SpawnTransform);
    Controller->Possess(Avatar);
    auto* ASC = State->GetProjectJAbilitySystemComponent();
    auto* Attributes = State->GetProjectJAttributeSet();
    auto* Progression = State->FindComponentByClass<UProject_JProgressionComponent>();
    if (!TestNotNull(TEXT("Persistent ASC"), ASC) || !TestNotNull(TEXT("Attributes"), Attributes)
        || !TestNotNull(TEXT("Progression"), Progression)) return false;
    if (!TestTrue(TEXT("Authority and actual persistent owners are established"), Avatar->HasAuthority()
        && Avatar->GetPlayerState() == State && Avatar->GetAbilitySystemComponent() == ASC
        && ASC->GetOwnerActor() == State && ASC->GetAvatarActor() == Avatar
        && Avatar->GetAttributeSet() == Attributes && Avatar->GetProgressionComponent() == Progression)) return false;
    if (!TestTrue(TEXT("Level change accepted"), Progression->SetLevel(12))
        || !TestTrue(TEXT("Level state published"), Progression->GetState().Revision > 0)
        || !TestEqual(TEXT("Actual progression level"), Progression->GetState().Level, 12)) return false;
    // Execute_* must dispatch through the BP. Calling _Implementation would hide stale overrides.
    TestEqual(TEXT("Reflected interface uses progression authority"), IProject_JCombatInterface::Execute_GetCharacterLevel(Avatar), 12);
    TestEqual(TEXT("Public level projection agrees"), State->GetPublicCharacterLevel(), 12);
    ASC->AddAttributeSetSubobject(Attributes);
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
    TestEqual(TEXT("Live health established"), Attributes->GetHealth(), 100.f);
    TestFalse(TEXT("Live BP is not dead"), IProject_JCombatInterface::Execute_IsDead(Avatar));
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 0.f);
    TestEqual(TEXT("Zero health established"), Attributes->GetHealth(), 0.f);
    TestTrue(TEXT("Reflected interface observes zero health"), IProject_JCombatInterface::Execute_IsDead(Avatar));
    const FName MissingSocket(TEXT("ProjectJ_Test_Missing_Combat_Socket"));
    if (!TestFalse(TEXT("Missing-socket precondition"), Avatar->GetMesh()->DoesSocketExist(MissingSocket))) return false;
    TestFalse(TEXT("Actor is away from origin"), Avatar->GetActorLocation().IsNearlyZero());
    TestTrue(TEXT("Missing socket falls back to actual actor location"),
        IProject_JCombatInterface::Execute_GetCombatSocketLocation(Avatar, MissingSocket).Equals(Avatar->GetActorLocation()));
    const TArray<FName> SocketNames = Avatar->GetMesh()->GetAllSocketNames();
    if (!TestTrue(TEXT("Authored mesh exposes a socket or bone"), !SocketNames.IsEmpty())) return false;
    TestTrue(TEXT("Existing primary-mesh socket follows mesh location"),
        IProject_JCombatInterface::Execute_GetCombatSocketLocation(Avatar, SocketNames[0]).Equals(Avatar->GetMesh()->GetSocketLocation(SocketNames[0])));
    ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
    TestFalse(TEXT("Death query has no stale duplicate state after recovery"), IProject_JCombatInterface::Execute_IsDead(Avatar));
    return true;
}
#endif
