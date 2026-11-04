#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Tests/Project_JSpatialHistoryFixture.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Kismet/KismetSystemLibrary.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "DefaultMovementSet/InstantMovementEffects/BasicInstantMovementEffects.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "GameplayTagsManager.h"
#include "Misc/ScopeExit.h"
#include "UObject/UnrealType.h"
#include <limits>

namespace
{
constexpr auto SpatialTestFlags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FSpatialHistoryWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FSpatialHistoryWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	void BeginPlay()
	{
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay();
		World->GetWorldSettings()->NotifyMatchStarted();
	}
	~FSpatialHistoryWorld()
	{
		if (World->GetBegunPlay()) { World->EndPlay(EEndPlayReason::LevelTransition); }
		World->DestroyWorld(false);
		GEngine->DestroyWorldContext(World);
	}
};
class FSpatialGameStateAccess : public AGameStateBase
{
public:
	using AGameStateBase::ServerWorldTimeSecondsDelta;
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialDiscontinuityTest, "ProjectJ.Combat.SpatialDiscontinuity", SpatialTestFlags)
bool FProjectJSpatialDiscontinuityTest::RunTest(const FString&)
{
	FSpatialHistoryWorld Scope;
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* NPC = Scope.World->SpawnActor<AProject_JNPCCharacter>(Spawn);
	NPC->SetActorEnableCollision(false);
	auto* Rewind = NPC->FindComponentByClass<UProject_JServerSideRewindComponent>();
	if (!TestNotNull(TEXT("Production NPC owns rewind history without fixture attachment"), Rewind)) return false;
	auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(NPC);
	NPC->AddInstanceComponent(Hit); Hit->RegisterComponent();
	auto* GameState = Scope.World->SpawnActor<AGameStateBase>(Spawn); Scope.World->SetGameState(GameState);
	Scope.BeginPlay();
	const auto SetClock = [&](float Time)
	{ static_cast<FSpatialGameStateAccess*>(GameState)->ServerWorldTimeSecondsDelta = Time - Scope.World->GetTimeSeconds(); };
	SetClock(9.9f);
	auto* Attack = NewObject<UProject_JAttackDefinition>(NPC);
	Attack->AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.Spatial.Attack"));
	Hit->BeginAttackNode(Attack->AttackTag, Attack, 103);
	const uint64 Window = Hit->BeginHitWindow();
	Hit->RecordAuthoritativeTrace(FVector::ZeroVector, FVector(100, 0, 0));
	FProject_JPoseHistoryBuffer Record, A, B; float Alpha = 0;
	Record.Timestamp = 9.9f; Record.CapsuleRadius = 20; Record.CapsuleHalfHeight = 90;
	Rewind->AppendPoseHistoryRecord(Record);
	TestTrue(TEXT("Fixture has a real retained capsule sample"), Rewind->GetPosesForTime(9.9f, A, B, Alpha));
	const uint64 Before = Hit->GetSpatialRevision();
	SetClock(10);
	TestTrue(TEXT("Ordinary Actor teleport succeeds"), NPC->TeleportTo(FVector(2000, 0, 500), FRotator::ZeroRotator, false, true));
	TestEqual(TEXT("One Actor teleport advances the spatial epoch once"), Hit->GetSpatialRevision(), Before + 1);
	TestEqual(TEXT("Teleport retires capsule history"), Rewind->PoseHistoryCount, 0);
	TestTrue(TEXT("Teleport retains the active authored hit window"), Hit->IsHitWindowCurrent(Window));
	TestTrue(TEXT("Teleport retains the active attack definition"), Hit->GetActiveAttackDefinition() == Attack);
	FVector Start, End; bool bOpen = false;
	TestFalse(TEXT("Teleport retires weapon history"), Hit->FindAuthoritativeTraceAtTime(9.9f, 103, Attack->AttackTag, Start, End, bOpen));
	SetClock(10.01f);
	Hit->RecordAuthoritativeTrace(FVector(2000, 0, 500), FVector(2010, 0, 500));
	TestFalse(TEXT("Endpoint tolerance cannot read a pre-teleport request from a new epoch"),
		Hit->FindAuthoritativeTraceAtTime(9.99f, 103, Attack->AttackTag, Start, End, bOpen));
	TestTrue(TEXT("Fresh post-teleport geometry remains queryable"),
		Hit->FindAuthoritativeTraceAtTime(10.01f, 103, Attack->AttackTag, Start, End, bOpen));
	const uint64 After = Hit->GetSpatialRevision();
	const FVector Invalid(std::numeric_limits<double>::quiet_NaN(), 0, 0);
	TestFalse(TEXT("Invalid teleport fails at the Actor boundary"), NPC->TeleportTo(Invalid, FRotator::ZeroRotator, false, true));
	TestEqual(TEXT("Failed teleport does not retire the current epoch"), Hit->GetSpatialRevision(), After);
	TestTrue(TEXT("Placement-test teleport can succeed"), NPC->TeleportTo(FVector(2100, 0, 500), FRotator::ZeroRotator, true, true));
	TestEqual(TEXT("Placement tests do not retire gameplay geometry"), Hit->GetSpatialRevision(), After);

	FTeleportEffect Effect; Effect.TargetLocation = FVector(4000, 0, 500);
	FMoverTickStartData TickStart; FMoverTimeStep Step; FMoverSyncState Output;
	FApplyMovementEffectParams Params{};
	Params.UpdatedComponent = NPC->GetCapsuleComponent(); Params.UpdatedPrimitive = NPC->GetCapsuleComponent();
	Params.MoverComp = NPC->GetMover(); Params.StartState = &TickStart; Params.TimeStep = &Step;
	TestTrue(TEXT("Real synchronous Mover teleport effect succeeds"), Effect.ApplyMovementEffect(Params, Output));
	TestEqual(TEXT("Mover's Actor success advances the epoch once"), Hit->GetSpatialRevision(), After + 1);
	TestEqual(TEXT("Mover emits one successful simulation event"), Params.OutputEvents.Num(), 1);
	const uint64 SynchronousRevision = Hit->GetSpatialRevision();
	NPC->GetMover()->OnTeleportSucceeded.Broadcast(FVector(2100, 0, 500), FQuat::Identity, Effect.TargetLocation, FQuat::Identity);
	TestEqual(TEXT("Synchronous event delivery cannot repeat Actor success"), Hit->GetSpatialRevision(), SynchronousRevision);

	// Swap only the interface used by the public async predicate. No async worker
	// or physics simulation is started by this deterministic delivery-boundary test.
	auto* BackendProperty = FindFProperty<FInterfaceProperty>(UMoverComponent::StaticClass(), TEXT("BackendLiaisonComp"));
	if (!TestNotNull(TEXT("Mover's reflected backend contract exists"), BackendProperty)) { return false; }
	auto* Backend = BackendProperty->ContainerPtrToValuePtr<TScriptInterface<IMoverBackendLiaisonInterface>>(NPC->GetMover());
	const auto Original = *Backend;
	ON_SCOPE_EXIT { if (!NPC->IsActorBeingDestroyed()) { *Backend = Original; } };
	auto* Async = NewObject<UProject_JSpatialHistoryAsyncLiaison>(NPC);
	Backend->SetObject(Async); Backend->SetInterface(Async);
	NPC->GetMover()->OnTeleportSucceeded.Broadcast(Effect.TargetLocation, FQuat::Identity, FVector(6000, 0, 500), FQuat::Identity);
	TestEqual(TEXT("Async success delivery retires geometry on GT"), Hit->GetSpatialRevision(), SynchronousRevision + 1);
	const uint64 AsyncRevision = Hit->GetSpatialRevision();
	NPC->GetMover()->OnTeleportFailed.Broadcast(Effect.TargetLocation, FQuat::Identity, FVector(7000, 0, 500), FQuat::Identity, ETeleportFailureReason::Reason_NotAvailable);
	TestEqual(TEXT("Async failure delivery preserves current geometry"), Hit->GetSpatialRevision(), AsyncRevision);
	*Backend = Original;
	NPC->Destroy();
	TestFalse(TEXT("EndPlay detaches the success subscription"),
		NPC->GetMover()->OnTeleportSucceeded.Contains(NPC, FName(TEXT("OnMoverTeleportSucceeded"))));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSpatialDiscontinuityTraceTest, "ProjectJ.Combat.SpatialDiscontinuityTrace", SpatialTestFlags)
bool FProjectJSpatialDiscontinuityTraceTest::RunTest(const FString&)
{
	// Publish the shared material before the new solver captures its initial
	// query-material snapshot. No private flush, physics tick or timing wait.
	auto* QueryMaterial = GEngine->DefaultPhysMaterial.Get();
	if (!TestNotNull(TEXT("Engine physical material is prepared for the comparison query"), QueryMaterial)) return false;
	QueryMaterial->GetPhysicsMaterial();
	FSpatialHistoryWorld Scope;
	FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* NPC = Scope.World->SpawnActor<AProject_JNPCCharacter>(Spawn); NPC->SetActorEnableCollision(false);
	auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(NPC); NPC->AddInstanceComponent(Hit); Hit->RegisterComponent();
	auto* Attack = NewObject<UProject_JAttackDefinition>(NPC);
	Attack->AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.Spatial.Trace"));
	Scope.BeginPlay();
	Hit->BeginAttackNode(Attack->AttackTag, Attack, 104);
	const uint64 Window = Hit->BeginHitWindow();
	auto* Mesh = NPC->GetMesh();
	auto* Notify = NewObject<UProject_JAnimNotifyState_MeleeHit>(); Notify->SocketName = NAME_None; Notify->HitEventTag = Attack->AttackTag;
	FAnimNotifyEventReference Event;
	const FVector Old = Mesh->GetSocketLocation(NAME_None);
	// Seed only the authored-socket Begin boundary; exercise the real notify Tick,
	// Actor teleport, collision query, GAS event callback and End implementations.
	Notify->TraceStates.FindOrAdd(Mesh).Add(Event.GetNotifyInstanceID(), {Old, Hit, Window, Hit->GetSpatialRevision()});
	TestTrue(TEXT("Teleport during the active notify succeeds"), NPC->TeleportTo(FVector(10000, 0, 500), FRotator::ZeroRotator, false, true));
	Notify->NotifyTick(Mesh, nullptr, 1.f / 60, Event);
	const FVector Rebased = Mesh->GetSocketLocation(NAME_None);
	TestTrue(TEXT("First post-teleport tick rebases the existing execution"),
		Notify->TraceStates.FindChecked(Mesh).FindChecked(Event.GetNotifyInstanceID()).PreviousLocation.Equals(Rebased));
	FVector Start, End;
	TestFalse(TEXT("Rebase does not publish an invented teleport sweep"), Hit->FindAuthoritativeTraceAtTime(Scope.World->GetTimeSeconds(), Start, End));
	TestTrue(TEXT("The existing notify window survives the rebase"), Hit->IsHitWindowCurrent(Window));
	const uint64 Revision = Hit->GetSpatialRevision();
	NPC->SetActorLocation(NPC->GetActorLocation() + FVector(10000, 0, 0));
	Notify->NotifyTick(Mesh, nullptr, 1.f / 60, Event);
	TestEqual(TEXT("Continuous movement is never classified by an arbitrary distance threshold"), Hit->GetSpatialRevision(), Revision);
	TestTrue(TEXT("Next real segment records normally"), Hit->FindAuthoritativeTraceAtTime(Scope.World->GetTimeSeconds(), Start, End));
	TestTrue(TEXT("Continuous segment starts at the post-teleport endpoint"), Start.Equals(Rebased) && End.Equals(Mesh->GetSocketLocation(NAME_None)));

	// Both collision candidates belong to this one sweep. The first event can
	// teleport synchronously without ending the ability or closing its window.
	Notify->NotifyEnd(Mesh, nullptr, Event);
	Hit->EndAttack(); Hit->BeginAttackNode(Attack->AttackTag, Attack, 105);
	const uint64 ComparisonWindow = Hit->BeginHitWindow();
	const FVector SweepStart = Mesh->GetSocketLocation(NAME_None), SweepEnd = SweepStart + FVector(400, 0, 0);
	for (double Fraction : {.25, .75})
	{
		auto* Target = Scope.World->SpawnActor<ACharacter>(Spawn);
		Target->SetActorLocation(FMath::Lerp(SweepStart, SweepEnd, Fraction));
		Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
		Target->GetCapsuleComponent()->SetPhysMaterialOverride(QueryMaterial);
	}
	// Compare the actual notify's GAS hit payload with the previous public
	// wrapper in this same collision scene, including self and non-Pawn shapes.
	NPC->SetActorEnableCollision(true);
	NPC->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	NPC->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
	auto* NonPawn = Scope.World->SpawnActor<AActor>(Spawn);
	auto* NonPawnShape = NewObject<UCapsuleComponent>(NonPawn);
	NonPawn->AddInstanceComponent(NonPawnShape); NonPawn->SetRootComponent(NonPawnShape);
	NonPawnShape->InitCapsuleSize(42, 96);
	NonPawnShape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	NonPawnShape->SetCollisionObjectType(ECC_WorldDynamic);
	NonPawnShape->SetCollisionResponseToAllChannels(ECR_Block);
	NonPawnShape->RegisterComponent();
	NonPawn->SetActorLocation(FMath::Lerp(SweepStart, SweepEnd, .5));
	Notify->TraceStates.FindOrAdd(Mesh).Add(Event.GetNotifyInstanceID(), {SweepStart, Hit, ComparisonWindow, Hit->GetSpatialRevision()});
	NPC->SetActorLocation(NPC->GetActorLocation() + FVector(400, 0, 0));
	TArray<FHitResult> Expected, Observed;
	TArray<AActor*> Ignored = {NPC};
	TestTrue(TEXT("Previous public sphere wrapper finds the two Pawn candidates"),
		UKismetSystemLibrary::SphereTraceMultiForObjects(Scope.World, SweepStart, SweepEnd, Attack->HitSpec.TraceRadius,
			{UEngineTypes::ConvertToObjectType(ECC_Pawn)}, false, Ignored, EDrawDebugTrace::None, Expected, true));
	TestEqual(TEXT("Self and non-Pawn collision are excluded by the comparison query"), Expected.Num(), 2);
	auto* ASC = NPC->GetAbilitySystemComponent();
	const auto Comparison = ASC->GenericGameplayEventCallbacks.FindOrAdd(Attack->AttackTag).AddLambda([&](const FGameplayEventData* Payload)
	{
		const auto* Data = Payload ? Payload->TargetData.Get(0) : nullptr;
		const auto* Result = Data ? Data->GetHitResult() : nullptr;
		if (Result) Observed.Add(*Result);
	});
	Notify->NotifyTick(Mesh, nullptr, 1.f / 60, Event);
	ASC->GenericGameplayEventCallbacks.FindChecked(Attack->AttackTag).Remove(Comparison);
	TestEqual(TEXT("Actual notify delivery preserves wrapper hit cardinality"), Observed.Num(), Expected.Num());
	for (int32 Index = 0; Index < Observed.Num() && Index < Expected.Num(); ++Index)
	{
		const auto& Actual = Observed[Index]; const auto& Before = Expected[Index];
		TestTrue(TEXT("Comparison query exercises a real prepared physical material"), Before.PhysMaterial.Get() == QueryMaterial);
		const bool bMetadataMatches = Actual.GetActor() == Before.GetActor() && Actual.Component == Before.Component
			&& Actual.PhysMaterial.IsValid() && Actual.PhysMaterial == Before.PhysMaterial
			&& Actual.FaceIndex == Before.FaceIndex && Actual.Item == Before.Item && Actual.ElementIndex == Before.ElementIndex
			&& Actual.BoneName == Before.BoneName && Actual.bBlockingHit == Before.bBlockingHit && Actual.bStartPenetrating == Before.bStartPenetrating
			&& FMath::IsNearlyEqual(Actual.Time, Before.Time) && FMath::IsNearlyEqual(Actual.Distance, Before.Distance)
			&& FMath::IsNearlyEqual(Actual.PenetrationDepth, Before.PenetrationDepth)
			&& Actual.TraceStart.Equals(Before.TraceStart) && Actual.TraceEnd.Equals(Before.TraceEnd)
			&& Actual.Location.Equals(Before.Location) && Actual.ImpactPoint.Equals(Before.ImpactPoint)
			&& Actual.Normal.Equals(Before.Normal) && Actual.ImpactNormal.Equals(Before.ImpactNormal);
		if (!bMetadataMatches)
			AddInfo(FString::Printf(TEXT("Hit[%d] actorEqual=%d componentEqual=%d materialValid=%d/%d materialEqual=%d face=%d/%d item=%d/%d element=%u/%u boneEqual=%d blocking=%d/%d penetrating=%d/%d time=%g/%g distance=%g/%g depth=%g/%g startDelta=%g endDelta=%g locationDelta=%g impactDelta=%g normalDelta=%g impactNormalDelta=%g"),
				Index, Actual.GetActor() == Before.GetActor(), Actual.Component == Before.Component, Actual.PhysMaterial.IsValid(), Before.PhysMaterial.IsValid(), Actual.PhysMaterial == Before.PhysMaterial,
				Actual.FaceIndex, Before.FaceIndex, Actual.Item, Before.Item, Actual.ElementIndex, Before.ElementIndex, Actual.BoneName == Before.BoneName,
				Actual.bBlockingHit, Before.bBlockingHit, Actual.bStartPenetrating, Before.bStartPenetrating, Actual.Time, Before.Time, Actual.Distance, Before.Distance,
				Actual.PenetrationDepth, Before.PenetrationDepth, FVector::Dist(Actual.TraceStart, Before.TraceStart), FVector::Dist(Actual.TraceEnd, Before.TraceEnd),
				FVector::Dist(Actual.Location, Before.Location), FVector::Dist(Actual.ImpactPoint, Before.ImpactPoint), FVector::Dist(Actual.Normal, Before.Normal), FVector::Dist(Actual.ImpactNormal, Before.ImpactNormal)));
		TestTrue(TEXT("Actual notify preserves ordered actor/component/material/face and collision metadata"), bMetadataMatches);
		TestTrue(TEXT("Actual notify ignores its owner and the non-Pawn collider"), Actual.GetActor() != NPC && Actual.GetActor() != NonPawn);
	}
	Notify->NotifyEnd(Mesh, nullptr, Event);
	const uint64 CallbackWindow = Hit->BeginHitWindow();
	NPC->SetActorLocation(NPC->GetActorLocation() - FVector(400, 0, 0));
	Notify->TraceStates.FindOrAdd(Mesh).Add(Event.GetNotifyInstanceID(), {SweepStart, Hit, CallbackWindow, Hit->GetSpatialRevision()});
	NPC->SetActorLocation(NPC->GetActorLocation() + FVector(400, 0, 0));
	int32 Delivered = 0;
	const auto Callback = ASC->GenericGameplayEventCallbacks.FindOrAdd(Attack->AttackTag).AddLambda([&](const FGameplayEventData*)
	{
		++Delivered;
		if (Delivered == 1) { NPC->TeleportTo(FVector(30000, 0, 500), FRotator::ZeroRotator, false, true); }
	});
	Notify->NotifyTick(Mesh, nullptr, 1.f / 60, Event);
	TestEqual(TEXT("Teleport in a hit callback retires the remaining old sweep candidates"), Delivered, 1);
	TestTrue(TEXT("Callback teleport retains the active notify window"), Hit->IsHitWindowCurrent(CallbackWindow));
	ASC->GenericGameplayEventCallbacks.FindChecked(Attack->AttackTag).Remove(Callback);
	Notify->NotifyTick(Mesh, nullptr, 1.f / 60, Event);
	TestTrue(TEXT("The callback teleport rebases on the next notify tick"),
		Notify->TraceStates.FindChecked(Mesh).FindChecked(Event.GetNotifyInstanceID()).PreviousLocation.Equals(Mesh->GetSocketLocation(NAME_None)));
	Notify->NotifyEnd(Mesh, nullptr, Event);
	TestFalse(TEXT("Normal notify End still releases its window"), Hit->IsHitWindowCurrent(CallbackWindow));
	TestTrue(TEXT("Normal notify End releases execution storage"), Notify->TraceStates.IsEmpty());
	return true;
}
#endif
