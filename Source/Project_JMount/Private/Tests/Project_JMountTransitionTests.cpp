#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Components/CapsuleComponent.h"
#include "Mount/Project_JMountComponent.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Project_JMountRefinementFixtures.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"

namespace ProjectJMountTransitionTests
{
struct FWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FWorld()
	{
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->GetWorldSettings()->NotifyMatchStarted();
	}
	~FWorld()
	{
		World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
	template<typename T> T* Spawn()
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		return World->SpawnActor<T>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountAttachRetiredTest, "ProjectJ.Continuation.MountAttachRetired",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountAttachRetiredTest::RunTest(const FString&)
{
	ProjectJMountTransitionTests::FWorld Scope;
	auto* Mount = Scope.Spawn<AProject_JMountLifecycleFixture>();
	auto* Rider = Scope.Spawn<AProject_JMountCallbackRiderFixture>();
	auto* Controller = Scope.Spawn<APlayerController>();
	if (!Mount || !Rider || !Controller) { AddError(TEXT("Fixture spawn failed")); return false; }
	auto* Relationship = NewObject<UProject_JMountComponent>(Rider);
	Rider->AddInstanceComponent(Relationship); Relationship->RegisterComponent();
	Controller->Possess(Rider);
	Rider->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Rider->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	bool bDismountedInCallback = false;
	Rider->OnMovementDisabled = [&] { bDismountedInCallback = Mount->DismountRider(true); };
	TestFalse(TEXT("Retired attach cannot report mount success"), Mount->TryMountRider(Rider));
	TestTrue(TEXT("Real movement callback completed forced dismount"), bDismountedInCallback);
	TestNull(TEXT("Mount no longer owns the rider"), Mount->GetRider());
	TestFalse(TEXT("Old completion cannot reinstall rider relationship"), Relationship->IsMounted());
	TestTrue(TEXT("Rider possession remains with the original controller"), Controller->GetPawn() == Rider);
	TestEqual(TEXT("Captured collision is restored"), Rider->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Captured movement is restored"), Rider->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Flying);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountReplicatedReplacementTest, "ProjectJ.Continuation.MountReplicatedReplacement",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountReplicatedReplacementTest::RunTest(const FString&)
{
	ProjectJMountTransitionTests::FWorld Scope;
	auto* Mount = Scope.Spawn<AProject_JMountLifecycleFixture>();
	auto* A = Scope.Spawn<AProject_JMountCallbackRiderFixture>();
	auto* B = Scope.Spawn<AProject_JMountCallbackRiderFixture>();
	if (!Mount || !A || !B) { AddError(TEXT("Fixture spawn failed")); return false; }
	A->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	A->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	B->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	B->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	Mount->SetTestRider(A);
	Mount->SetTestRider(B); // A -> B: the null intermediate update can be coalesced.
	TestNull(TEXT("Replaced rider is detached"), A->GetAttachParentActor());
	TestEqual(TEXT("Replaced rider restores its collision"), A->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Replaced rider restores its own mode"), A->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Flying);
	TestTrue(TEXT("New rider is attached"), B->GetAttachParentActor() == Mount);
	Mount->SetTestRider(nullptr);
	TestEqual(TEXT("New rider has an independent collision restore slot"), B->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryAndPhysics);
	TestEqual(TEXT("New rider has an independent movement restore slot"), B->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Walking);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountPhaseSupersededTest, "ProjectJ.Continuation.MountPhaseSuperseded",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountPhaseSupersededTest::RunTest(const FString&)
{
	ProjectJMountTransitionTests::FWorld Scope;
	auto* Mount = Scope.Spawn<AProject_JMountRefinementFixture>();
	auto* Rider = Scope.Spawn<AProject_JMountCallbackRiderFixture>();
	auto* Controller = Scope.Spawn<APlayerController>();
	if (!Mount || !Rider || !Controller) { AddError(TEXT("Fixture spawn failed")); return false; }
	Controller->Possess(Rider);
	if (!TestTrue(TEXT("Actual rider mounts"), Mount->TryMountRider(Rider))) { return false; }
	Mount->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
	if (!TestTrue(TEXT("Grounded occupied mount begins flight"), Mount->BeginFlight())) { return false; }
	auto* ASC = Mount->GetAbilitySystemComponent();
	const auto& Tags = FProject_JGameplayTags::Get();
	bool bReentered = false, bNestedLanding = false;
	const auto Handle = ASC->RegisterGameplayTagEvent(Tags.State_Mount_TakingOff, EGameplayTagEventType::NewOrRemoved)
		.AddLambda([&](const FGameplayTag, int32 Count)
		{
			if (Count == 0 && !bReentered) { bReentered = true; bNestedLanding = Mount->EndFlight(); }
		});
	Mount->CommitTakeOffImpulse();
	ASC->RegisterGameplayTagEvent(Tags.State_Mount_TakingOff, EGameplayTagEventType::NewOrRemoved).Remove(Handle);
	TestTrue(TEXT("TakingOff removal actually reentered public EndFlight"), bReentered && bNestedLanding);
	TestEqual(TEXT("New landing phase remains authoritative"), Mount->GetFlightState(), EProject_JMountFlightState::Landing);
	TestTrue(TEXT("New phase retains its own tag"), ASC->HasMatchingGameplayTag(Tags.State_Mount_Landing));
	TestFalse(TEXT("Old completion cannot publish AutoAscending tag"), ASC->HasMatchingGameplayTag(Tags.State_Mount_AutoAscending));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountControllerReassignedTest, "ProjectJ.Continuation.MountControllerReassigned",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountControllerReassignedTest::RunTest(const FString&)
{
	ProjectJMountTransitionTests::FWorld Scope;
	auto* Mount = Scope.Spawn<AProject_JMountLifecycleFixture>();
	auto* Rider = Scope.Spawn<AProject_JMountCallbackRiderFixture>();
	auto* Replacement = Scope.Spawn<ACharacter>();
	auto* Controller = Scope.Spawn<APlayerController>();
	if (!Mount || !Rider || !Replacement || !Controller) { AddError(TEXT("Fixture spawn failed")); return false; }
	Controller->Possess(Rider);
	Rider->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Rider->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
	Rider->OnMovementDisabled = [&] { Controller->Possess(Replacement); };
	TestFalse(TEXT("Old mount request cannot steal reassigned controller"), Mount->TryMountRider(Rider));
	TestTrue(TEXT("New possession remains authoritative"), Controller->GetPawn() == Replacement);
	TestNull(TEXT("Failed request retires its own rider"), Mount->GetRider());
	TestNull(TEXT("Failed request detaches its own rider"), Rider->GetAttachParentActor());
	TestEqual(TEXT("Failed request restores collision"), Rider->GetCapsuleComponent()->GetCollisionEnabled(), ECollisionEnabled::QueryOnly);
	TestEqual(TEXT("Failed request restores movement"), Rider->GetCharacterMovement()->MovementMode.GetValue(), MOVE_Flying);
	return true;
}
#endif
