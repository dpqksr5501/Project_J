#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "GameFramework/PlayerController.h"
#include "Movement/Project_JMovementQueries.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Tests/Project_JMountLifecycleFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerMountLifecycleTest, "ProjectJ.PlayerMaturity.Mount.PossessionAndBlockedExit",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerMountLifecycleTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Controller = World->SpawnActor<APlayerController>(Params);
	auto* State = World->SpawnActor<AProject_JPlayerState>(Params);
	auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>(Params);
	auto* Mount = World->SpawnActor<AProject_JMountLifecycleFixture>(Params);
	if (!TestNotNull(TEXT("Concrete mount fixture spawns"), Mount)
		|| !TestNotNull(TEXT("Player fixture spawns"), Player)
		|| !TestNotNull(TEXT("Controller fixture spawns"), Controller)
		|| !TestNotNull(TEXT("PlayerState fixture spawns"), State)) { return false; }
	State->SetOwner(Controller); Controller->SetPlayerState(State); Player->SetPlayerState(State); Controller->Possess(Player);
	auto* ASC = State->GetAbilitySystemComponent();
	const auto MountedTag = FProject_JGameplayTags::Get().State_Mounted;
	Player->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Project_J::Movement::SetMovementMode(Player,MOVE_Flying);
	for(int32 Step=0;Step<8 && Project_J::Movement::ReadPhysicalFrame(Player).Mode!=MOVE_Flying;++Step) World->Tick(LEVELTICK_All,1.f/60.f);
	TestEqual(TEXT("Mover admits original Flying policy before mounting"),Project_J::Movement::ReadPhysicalFrame(Player).Mode,MOVE_Flying);
	TestTrue(TEXT("Actual possession transfers to mount"), Mount->TryMountRider(Player));
	TestTrue(TEXT("Controller now drives mount"), Controller->GetPawn() == Mount);
	TestTrue(TEXT("Persistent rider ASC owns mounted tag"), ASC->HasMatchingGameplayTag(MountedTag));
	for(int32 Step=0;Step<8 && Player->GetMover()->GetMovementModeName()!=TEXT("Attached");++Step) World->Tick(LEVELTICK_All,1.f/60.f);
	TestEqual(TEXT("Mounted rider enters stationary Mover simulation"),Player->GetMover()->GetMovementModeName(),FName(TEXT("Attached")));
	const FVector RiderOffset=Player->GetActorLocation()-Mount->GetActorLocation();
	Mount->SetActorLocation(Mount->GetActorLocation()+FVector(700,250,100));
	for(int32 Step=0;Step<4;++Step) World->Tick(LEVELTICK_All,1.f/60.f);
	TestTrue(TEXT("Mover preserves attachment movement without simulation pulling the rider back"),
		(Player->GetActorLocation()-Mount->GetActorLocation()).Equals(RiderOffset,.1));
	TestTrue(TEXT("Attached movement cannot accumulate falling velocity"),Player->GetVelocity().IsNearlyZero(.1));
	auto* Obstacle = World->SpawnActor<AActor>(Params);
	auto* Box = NewObject<UBoxComponent>(Obstacle); Obstacle->SetRootComponent(Box);
	Box->SetBoxExtent(FVector(2000)); Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block); Box->RegisterComponent();
	Box->SetWorldLocation(Mount->GetActorLocation());
	TestFalse(TEXT("Ordinary dismount rejects fully blocked exit"), Mount->DismountRider(false));
	TestTrue(TEXT("Forced dismount completes lifecycle despite blocked exit"), Mount->DismountRider(true));
	TestTrue(TEXT("Controller is restored to rider"), Controller->GetPawn() == Player);
	TestFalse(TEXT("Persistent mounted tag is retired"), ASC->HasMatchingGameplayTag(MountedTag));
	TestFalse(TEXT("Rider relationship is cleared"), Player->GetMountComponent()->IsMounted());
	TestEqual(TEXT("Original collision policy is restored"), Player->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	for(int32 Step=0;Step<8 && Project_J::Movement::ReadPhysicalFrame(Player).Mode!=MOVE_Flying;++Step) World->Tick(LEVELTICK_All,1.f/60.f);
	TestEqual(TEXT("Original movement mode is restored"), Project_J::Movement::ReadPhysicalFrame(Player).Mode, MOVE_Flying);
	Obstacle->Destroy();
	Player->SetActorLocation(Mount->GetActorLocation());
	TestTrue(TEXT("Rider can remount after forced cleanup"), Mount->TryMountRider(Player));
	TestTrue(TEXT("Mount has the actual begun-play lifecycle"), Mount->HasActorBegunPlay());
	TestTrue(TEXT("Mount destruction succeeds"), Mount->Destroy());
	TestTrue(TEXT("Destroying a begun mount restores possession"), Controller->GetPawn() == Player);
	TestFalse(TEXT("Destroyed mount leaves no tag"), ASC->HasMatchingGameplayTag(MountedTag));
	TestFalse(TEXT("Destroyed mount leaves no relationship"), Player->GetMountComponent()->IsMounted());
	return true;
}
#endif
