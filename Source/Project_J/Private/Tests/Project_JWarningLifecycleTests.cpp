#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Tests/Project_JWarningLifecycleTestTypes.h"
#include "Project_JAbilitySystemComponent.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Game/Project_JPlayerController.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Camera/PlayerCameraManager.h"

namespace
{
constexpr auto WarningLifecycleTestFlags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
UWorld* BeginWarningWorld()
{
	auto* World=UWorld::CreateWorld(EWorldType::Game,false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	return World;
}
void EndWarningWorld(UWorld* World)
{
	World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRegistryBootstrap,"ProjectJ.Warnings.PrimaryAssetRegistryBootstrap",WarningLifecycleTestFlags)
bool FProjectJRegistryBootstrap::RunTest(const FString&)
{
	auto* GI=NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
	auto* World=GI->GetWorld();
	ON_SCOPE_EXIT { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Registry=GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto* Class=LoadObject<UProject_JCharacterClassDefinition>(nullptr,TEXT("/Game/DataAssetSets/Class/DA_Class_Greatsword.DA_Class_Greatsword"));
	if (!TestNotNull(TEXT("Real GI registry"),Registry) || !TestNotNull(TEXT("Authored production class"),Class)) return false;
	TestEqual(TEXT("PrimaryAsset class is ready before persistence or caller refresh"),Registry->GetClassDefinition(Class->ClassId),Class);
	TArray<FText> Errors;
	TestTrue(TEXT("Validation accepts the actual effective PrimaryAsset registry"),Registry->ValidateDataRegistry(Errors));
	TestTrue(TEXT("The effective registry has no validation errors"),Errors.IsEmpty());
	TestTrue(TEXT("Explicit repeated catalog refresh remains compatible"),Registry->LoadPrimaryAssetDefinitions());
	TestEqual(TEXT("Refresh preserves the same authoritative class"),Registry->GetClassDefinition(Class->ClassId),Class);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCancelDuringTeardown,"ProjectJ.Warnings.AbilityCancellationDuringTeardown",WarningLifecycleTestFlags)
bool FProjectJCancelDuringTeardown::RunTest(const FString&)
{
	auto* World=BeginWarningWorld(); ON_SCOPE_EXIT { EndWarningWorld(World); };
	auto* Owner=World->SpawnActor<AActor>();
	auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Owner); Owner->AddInstanceComponent(ASC); ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Owner,Owner);
	const auto Handle=ASC->GiveAbility(FGameplayAbilitySpec(UProject_JWarningCancellationAbility::StaticClass(),1));
	if (!TestTrue(TEXT("Actual GAS ability activates"),ASC->TryActivateAbility(Handle))) return false;
	auto* Instance=CastChecked<UProject_JWarningCancellationAbility>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
	ASC->CancelAbilityHandle(Handle);
	TestTrue(TEXT("Ordinary gameplay still requests replicated cancellation"),Instance->bLastReplicateCancel);
	TestFalse(TEXT("Ordinary cancellation ends local ability state"),Instance->IsActive());
	if (!TestTrue(TEXT("The same ability can reactivate before shutdown"),ASC->TryActivateAbility(Handle))) return false;
	World->BeginTearingDown();
	ASC->CancelAbilityHandle(Handle);
	TestEqual(TEXT("Both paths actually execute cancellation"),Instance->CancelCount,2);
	TestFalse(TEXT("Closing worlds cannot send ClientCancelAbility for retired net objects"),Instance->bLastReplicateCancel);
	TestFalse(TEXT("Shutdown still performs local End/task/ability cleanup"),Instance->IsActive());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCameraDuringTeardown,"ProjectJ.Warnings.ViewTargetDuringTeardown",WarningLifecycleTestFlags)
bool FProjectJCameraDuringTeardown::RunTest(const FString&)
{
	auto* World=BeginWarningWorld(); ON_SCOPE_EXIT { EndWarningWorld(World); };
	auto* PC=World->SpawnActor<AProject_JWarningPlayerController>();
	if (!TestNotNull(TEXT("Actual player camera manager"),PC->PlayerCameraManager.Get())) return false;
	auto* A=World->SpawnActor<AActor>(); auto* B=World->SpawnActor<AActor>();
	PC->SetViewTarget(A);
	TestEqual(TEXT("Live camera targeting remains available"),PC->GetViewTarget(),A);
	World->BeginTearingDown();
	PC->SetViewTarget(B);
	TestEqual(TEXT("Closing worlds do not start new camera/RPC transitions"),PC->GetViewTarget(),A);
	return true;
}
#endif
