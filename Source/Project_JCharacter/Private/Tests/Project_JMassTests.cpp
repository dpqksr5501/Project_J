#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Mass/Project_JMassMovementProcessor.h"
#include "Mass/Project_JMassRepresentationSubsystem.h"
#include "MassEntityManager.h"
#include "MassEntityManagerStorage.h"
#include "MassProcessingContext.h"
#include "MassExecutor.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameplayEffect.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include "ProfilingDebugging/MiscTrace.h"

namespace
{
TSharedRef<FMassEntityManager> NewManager()
{
	auto Manager = MakeShared<FMassEntityManager>();
	FMassEntityManagerStorageInitParams Params;
#if WITH_MASS_CONCURRENT_RESERVE
	Params.Emplace<FMassEntityManager_InitParams_Concurrent>(FMassEntityManager_InitParams_Concurrent{4096, 1024});
#else
PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Params.Emplace<FMassEntityManager_InitParams_SingleThreaded>();
PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
	Manager->Initialize(Params); return Manager;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMassValuesTest, "ProjectJ.GroupD.MassValues",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMassValuesTest::RunTest(const FString&)
{
	FString CSV = TEXT("count,mode,iteration,joined_step_ms\n");
	for (const int32 Count : {100, 1000, 2000})
	{
		TArray<FVector> SerialPositions;
		for (const bool bParallel : {false, true})
		{
			auto Manager = NewManager();
			const UScriptStruct* Fragments[] = {FProjectJMassMovementFragment::StaticStruct()};
			const auto Archetype = Manager->CreateArchetype(Fragments);
			TArray<FMassEntityHandle> Handles;
			for (int32 I = 0; I < Count; ++I)
			{
				const auto H = Manager->CreateEntity(Archetype); Handles.Add(H);
				auto& F = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(H);
				F.StableId = I + 1; F.Position = FVector(0, I, 0); F.Speed = 250 + I % 20;
				F.RouteCount = 3; F.Route[0] = F.Position; F.Route[1] = FVector(100, I, 0); F.Route[2] = FVector(100000, I, 0);
				F.NextPoint = 1; F.bMassOwnsMovement = I % 10 != 0; F.bRouteValid = I % 11 != 0;
			}
			auto* Processor = NewObject<UProject_JMassMovementProcessor>(); Processor->AddToRoot();
			Processor->bParallel = bParallel; Processor->CallInitialize(GetTransientPackage(), Manager);
			TRACE_BOOKMARK(TEXT("ProjectJ.MassValues Count=%d Parallel=%d"), Count, bParallel);
			for (int32 Iteration = 0; Iteration < 130; ++Iteration)
			{
				const double Start = FPlatformTime::Seconds();
				{ UE::Mass::FProcessingContext Context(Manager, 0.05f); UE::Mass::Executor::Run(*Processor, Context); }
				if (Iteration >= 10) { CSV += FString::Printf(TEXT("%d,%s,%d,%.6f\n"), Count, bParallel ? TEXT("Parallel") : TEXT("Serial"), Iteration - 10, (FPlatformTime::Seconds() - Start) * 1000); }
			}
			for (int32 I = 0; I < Count; ++I)
			{
				const auto& F = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(Handles[I]);
				if (!bParallel) { SerialPositions.Add(F.Position); }
				else { TestTrue(TEXT("Mass chunk execution matches serial values"), F.Position.Equals(SerialPositions[I], 1.e-8)); }
				if (I % 10 == 0 || I % 11 == 0) { TestEqual(TEXT("Character-owned/invalid route never moves"), F.Position.X, 0.0); }
			}
			const auto Retired = Handles.Last(); Manager->DestroyEntity(Retired); Handles.Pop();
			const auto Replacement = Manager->CreateEntity(Archetype);
			TestFalse(TEXT("Retired Mass handle cannot address reused storage"), Manager->IsEntityValid(Retired));
			Manager->DestroyEntity(Replacement);
			for (const auto H : Handles) { Manager->DestroyEntity(H); }
			Processor->RemoveFromRoot();
		}
	}
	FString Output;
	if (!FParse::Value(FCommandLine::Get(), TEXT("ProjectJMassOutput="), Output)) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/GroupD_20260910/Mass"); }
	IFileManager::Get().MakeDirectory(*Output, true);
	TestTrue(TEXT("Actual Mass serial/parallel timings written"), FFileHelper::SaveStringToFile(CSV, *(Output / TEXT("mass-values.csv"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMassHandoffTest, "ProjectJ.GroupD.MassHandoff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMassHandoffTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	TArray<AProject_JNPCCharacter*> NPCs;
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 I = 0; I < 12; ++I)
	{ NPCs.Add(World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(0, I * 100, 100), FRotator::ZeroRotator, Params)); }
	World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
	auto* Bridge = World->GetSubsystem<UProject_JMassRepresentationSubsystem>();
	Bridge->SetObserversForTest({FVector(20000, 0, 0)});
	TArray<FProjectJMassAgentToken> Tokens; TArray<FNavPathSharedPtr> Paths;
	for (auto* NPC : NPCs)
	{
		NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(73);
		NPC->GetAbilitySystemComponent()->InitAbilityActorInfo(NPC, NPC);
		Tokens.Add(Bridge->RegisterNPC(NPC));
		auto Path = MakeShared<FNavigationPath, ESPMode::ThreadSafe>();
		Path->GetPathPoints().Add(FNavPathPoint(NPC->GetNavAgentLocation()));
		Path->GetPathPoints().Add(FNavPathPoint(NPC->GetNavAgentLocation() + FVector(10000, 0, 0))); Path->MarkReady();
		Paths.Add(Path); TestTrue(TEXT("Explicit route accepted"), Bridge->SetRoute(Tokens.Last(), Path));
	}
	auto* ASC = NPCs[0]->GetAbilitySystemComponent();
	auto* Equipment = NPCs[0]->FindComponentByClass<UProject_JEquipmentManagerComponent>();
	auto* Item = NewObject<UProject_JEquipmentItemDefinition>(); Item->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
	Equipment->EquipItem(Item); const FString EquipmentBefore = Equipment->GetReplicationDiagnosticSummary();
	TestEqual(TEXT("Round-trip fixture has a real equipped item"), Equipment->GetAllEquippedItems().Num(), 1);
	auto* Effect = NewObject<UGameplayEffect>(); Effect->DurationPolicy = EGameplayEffectDurationType::Infinite;
	const auto EffectHandle = ASC->ApplyGameplayEffectSpecToSelf(FGameplayEffectSpec(Effect, ASC->MakeEffectContext(), 1));
	for (int32 I = 0; I < 3; ++I) { Bridge->Tick(0.05f); TestTrue(TEXT("Transition budget bounded"), Bridge->GetStats().LastTransitions <= 4); }
	TestFalse(TEXT("Active effect pins Character state"), Bridge->IsMassOwned(Tokens[0].Id));
	ASC->RemoveActiveGameplayEffect(EffectHandle); Bridge->Tick(0.05f);
	TestEqual(TEXT("All opt-in NPCs handed off"), Bridge->GetStats().MassOwned, 12);
	TestFalse(TEXT("CMC cannot write while Mass owns movement"), NPCs[0]->GetCharacterMovement()->IsComponentTickEnabled());
	TestFalse(TEXT("ABA cannot re-enable a suspended requested pose tick"), CastChecked<UProject_JBudgetedSkeletalMeshComponent>(NPCs[0]->GetMesh())->GetRequestedTickEnabled());
	TestFalse(TEXT("Old-generation route rejected"), Bridge->SetRoute(Tokens[0], Paths[0]));
	Bridge->SetObserversForTest({FVector(-1800, 0, 100)}); Bridge->Tick(0.0f);
	TestTrue(TEXT("Hysteresis retains far representation in the boundary band"), Bridge->IsMassOwned(Tokens[0].Id));
	const FVector BeforeInvalidation = NPCs[0]->GetActorLocation(); Paths[0]->Invalidate();
	Bridge->Tick(0.05f);
	TestTrue(TEXT("Invalid route never advances stale movement"), NPCs[0]->GetActorLocation().Equals(BeforeInvalidation));
	Bridge->SetObserversForTest({NPCs[0]->GetActorLocation()});
	for (int32 I = 0; I < 4; ++I) { Bridge->Tick(0.0f); }
	TestFalse(TEXT("Near NPC returns to Character"), Bridge->IsMassOwned(Tokens[0].Id));
	TestTrue(TEXT("CMC restored"), NPCs[0]->GetCharacterMovement()->IsComponentTickEnabled());
	TestTrue(TEXT("ASC identity preserved"), NPCs[0]->GetAbilitySystemComponent() == ASC);
	TestTrue(TEXT("Equipment authority preserved"), NPCs[0]->FindComponentByClass<UProject_JEquipmentManagerComponent>() == Equipment);
	TestEqual(TEXT("Equipped item identity and payload survive the round trip"), Equipment->GetReplicationDiagnosticSummary(), EquipmentBefore);
	TestEqual(TEXT("Health survives round trip"), NPCs[0]->GetAttributeSet()->GetHealth(), 73.f);
	NPCs.Last()->Destroy(); Bridge->Tick(0);
	TestEqual(TEXT("Destroyed actor retires Mass entity"), Bridge->GetStats().Registered, 11);
	TestTrue(TEXT("Explicit unregister with current generation"), Bridge->UnregisterNPC(Bridge->GetToken(Tokens[0].Id)));
	TestFalse(TEXT("Retired stable ID remains invalid"), Bridge->UnregisterNPC(Tokens[0]));
	auto* OtherWorld = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(OtherWorld);
	auto* OtherNPC = OtherWorld->SpawnActor<AProject_JNPCCharacter>();
	auto* OtherBridge = OtherWorld->GetSubsystem<UProject_JMassRepresentationSubsystem>();
	const auto OtherToken = OtherBridge->RegisterNPC(OtherNPC);
	TestEqual(TEXT("World-local IDs can repeat"), OtherToken.Id, Tokens[0].Id);
	TestFalse(TEXT("Old-world token cannot unregister a new-world NPC with matching ID/generation"), OtherBridge->UnregisterNPC(Tokens[0]));
	TestTrue(TEXT("New-world token remains valid"), OtherBridge->UnregisterNPC(OtherToken));
	OtherWorld->DestroyWorld(false); GEngine->DestroyWorldContext(OtherWorld);
	World->EndPlay(EEndPlayReason::LevelTransition);
	TestEqual(TEXT("World end clears all representation ownership"), Bridge->GetStats().Registered, 0);
	TestFalse(TEXT("No registration after world end"), Bridge->RegisterNPC(NPCs[0]).IsValid());
	World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdMassTest, "ProjectJ.Crowd.MassSpacing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCrowdMassTest::RunTest(const FString&)
{
	FString CSV = TEXT("count,mode,iteration,total_ms\n");
	for (const int32 Count : {100, 512, 1024, 2048})
	{
		TArray<FVector> Reference;
		for (const bool bParallel : {false, true})
		{
			auto Manager = NewManager();
			const UScriptStruct* Types[] = {FProjectJMassMovementFragment::StaticStruct()};
			const auto Archetype = Manager->CreateArchetype(Types);
			TArray<FMassEntityHandle> Handles;
			for (int32 I = 0; I < Count; ++I)
			{
				const auto H = Manager->CreateEntity(Archetype); Handles.Add(H);
				auto& F = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(H);
				F.StableId = I + 1; F.Position = FVector((I % 64) * 140, (I / 64) * 120, 0);
				F.RouteCount = 2; F.Route[0] = F.Position; F.Route[1] = F.Position + FVector(100000, 0, 0);
				F.NextPoint = 1; F.bRouteValid = F.bMassOwnsMovement = true;
			}
			auto* Processor = NewObject<UProject_JMassMovementProcessor>(); Processor->AddToRoot();
			Processor->CallInitialize(GetTransientPackage(), Manager); Processor->bParallel = bParallel; Processor->bUseCrowdSpacing = true;
			const FString Name = FString::Printf(TEXT("ProjectJ.Crowd.Mass.%d.%s"), Count, bParallel ? TEXT("Parallel") : TEXT("Serial"));
			const uint64 Region = TRACE_BEGIN_REGION_WITH_ID(*Name);
			for (int32 I = 0; I < 130; ++I)
			{
				const double Start = FPlatformTime::Seconds();
				{ UE::Mass::FProcessingContext Context(Manager, 0.05f); UE::Mass::Executor::Run(*Processor, Context); }
				if (I >= 10) { CSV += FString::Printf(TEXT("%d,%s,%d,%.6f\n"), Count, bParallel ? TEXT("Parallel") : TEXT("Serial"), I - 10, (FPlatformTime::Seconds() - Start) * 1000); }
			}
			TRACE_END_REGION_WITH_ID(Region);
			for (int32 I = 0; I < Count; ++I)
			{
				auto& F = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(Handles[I]);
				TestTrue(TEXT("Neighbor search has a hard per-agent visit bound"), F.NeighborVisits <= 9 * 32);
				TestFalse(TEXT("Regular crowd lanes do not overflow or request fallback"), F.bNeedsCharacterTraversal);
				if (!bParallel) { Reference.Add(F.Position); }
				else { TestTrue(TEXT("Whole-frame snapshot yields equal serial/parallel movement"), Reference[I].Equals(F.Position, 1.e-8)); }
				F.Position = FVector::ZeroVector; // Overfull cell: conservative stop rather than omitted blockers.
			}
			{ UE::Mass::FProcessingContext Context(Manager, 0.05f); UE::Mass::Executor::Run(*Processor, Context); }
			for (const auto H : Handles)
			{
				const auto& F = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(H);
				TestTrue(TEXT("Dense cell overflow requests Character traversal"), F.bNeedsCharacterTraversal);
				TestTrue(TEXT("Dense cell never advances an unsafe route"), F.Position.IsZero());
				Manager->DestroyEntity(H);
			}
			Processor->RemoveFromRoot();
		}
	}
	FString Output; FParse::Value(FCommandLine::Get(), TEXT("ProjectJCrowdOutput="), Output);
	if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/CrowdE_20260910/Metrics"); }
	IFileManager::Get().MakeDirectory(*Output, true);
	TestTrue(TEXT("Snapshot plus neighbor compute and join cost recorded"), FFileHelper::SaveStringToFile(CSV, *(Output / TEXT("mass-crowd.csv"))));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdMassEdgesTest, "ProjectJ.Crowd.MassSpacingEdges",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCrowdMassEdgesTest::RunTest(const FString&)
{
	for (const bool bParallel : {false, true})
	{
		auto Manager = NewManager(); const UScriptStruct* Types[] = {FProjectJMassMovementFragment::StaticStruct()};
		const auto Archetype = Manager->CreateArchetype(Types);
		const auto Left = Manager->CreateEntity(Archetype), Right = Manager->CreateEntity(Archetype);
		auto& A = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(Left);
		auto& B = Manager->GetFragmentDataChecked<FProjectJMassMovementFragment>(Right);
		A.StableId = 1; B.StableId = 2;
		A.Position = FVector(199, 0, 0); B.Position = FVector(449, 0, 0);
		A.RouteCount = B.RouteCount = 2; A.NextPoint = B.NextPoint = 1;
		A.Route[1] = FVector(1000, 0, 0); B.Route[1] = FVector(-1000, 0, 0);
		A.Speed = B.Speed = 1000; A.bMassOwnsMovement = B.bMassOwnsMovement = A.bRouteValid = B.bRouteValid = true;
		auto* P = NewObject<UProject_JMassMovementProcessor>(); P->AddToRoot(); P->CallInitialize(GetTransientPackage(), Manager);
		P->bUseCrowdSpacing = true; P->bParallel = bParallel;
		{ UE::Mass::FProcessingContext Context(Manager, .1f); UE::Mass::Executor::Run(*P, Context); }
		TestTrue(TEXT("Opposing maximum-speed agents see blockers across the old two-cell boundary"), B.Position.X - A.Position.X >= A.Radius + B.Radius + 10);
		A.Position = FVector(0, 0, 0); A.RouteCount = 3; A.NextPoint = 1;
		A.Route[1] = FVector(5, 0, 0); A.Route[2] = FVector(5, 1000, 0); B.Position = FVector(2000, 0, 0);
		{ UE::Mass::FProcessingContext Context(Manager, .1f); UE::Mass::Executor::Run(*P, Context); }
		TestTrue(TEXT("Route corner waits for a neighbor check in its new direction"), A.Position.Equals(FVector(5, 0, 0)));
		Manager->DestroyEntity(Left); Manager->DestroyEntity(Right); P->RemoveFromRoot();
	}
	return true;
}
#endif
