#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Animation/Project_JAnimationBudgetTypes.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "CollisionShape.h"
#include "Movement/Project_JMoverPawn.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshSocket.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "GameFramework/GameStateBase.h"
#include "GameplayTagsManager.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "Components/CapsuleComponent.h"
#include "Tests/Project_JSpatialHistoryFixture.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include <limits>
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Components/BoxComponent.h"
#include "GameplayEffect.h"

class FGameStateTestAccessor : public AGameStateBase
{
public:
	using AGameStateBase::ServerWorldTimeSecondsDelta;
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCanonicalMeleeTraceTest, "ProjectJ.Combat.CanonicalMeleeTrace",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJCanonicalMeleeTraceTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { GEngine->DestroyWorldContext(World); };
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);
	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();
	LeaderMesh->SetWorldLocation(FVector(100.0, 0.0, 0.0));

	AActor* VisualWeapon = World->SpawnActor<AActor>(Params);
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(VisualWeapon);
	UStaticMeshSocket* VisualTip = NewObject<UStaticMeshSocket>(StaticMesh);
	VisualTip->SocketName = TEXT("WeaponHit_Tip");
	StaticMesh->AddSocket(VisualTip);
	UStaticMeshComponent* VisualMesh = NewObject<UStaticMeshComponent>(VisualWeapon);
	VisualMesh->SetStaticMesh(StaticMesh);
	VisualWeapon->SetRootComponent(VisualMesh);
	VisualMesh->RegisterComponent();
	VisualMesh->SetWorldLocation(FVector(1000.0, 0.0, 0.0));

	UProject_JWeaponPresentationComponent* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Character);
	Character->AddInstanceComponent(Presentation);
	Presentation->RegisterComponent();
	Presentation->SpawnedWeapon = VisualWeapon;
	FTransform VisualTransform;
	TestTrue(TEXT("Visual weapon has a tip socket"), Presentation->GetWeaponSocketTransform(TEXT("WeaponHit_Tip"), VisualTransform));

	UProject_JAnimNotifyState_MeleeHit* Notify = NewObject<UProject_JAnimNotifyState_MeleeHit>();
	Notify->bUseWeaponPresentationSocket = true; // serialized legacy option must no longer control gameplay
	Notify->SocketName = NAME_None; // component origin needs no skeletal asset in this isolated test
	const FVector TraceLocation = Notify->ResolveTraceLocation(LeaderMesh);
	TestTrue(TEXT("Gameplay trace uses the Leader socket"), TraceLocation.Equals(LeaderMesh->GetSocketLocation(Notify->SocketName)));
	TestTrue(TEXT("Gameplay trace ignores the cosmetic weapon"), FVector::DistSquared(TraceLocation, VisualTransform.GetLocation()) > FMath::Square(100.0));
	VisualMesh->SetWorldLocation(FVector(-1000.0, 0.0, 0.0));
	TestTrue(TEXT("Presentation weapon moved"), Presentation->GetWeaponSocketTransform(TEXT("WeaponHit_Tip"), VisualTransform));
	TestTrue(TEXT("Moved presentation weapon cannot change gameplay trace"), Notify->ResolveTraceLocation(LeaderMesh).Equals(TraceLocation));
	TestTrue(TEXT("Visual tip moved independently from gameplay trace"), FVector::DistSquared(TraceLocation, VisualTransform.GetLocation()) > FMath::Square(100.0));

	// Shared-notify housekeeping only: actual Begin/End with a real skeletal socket,
	// no HitValidation component, no owned attack/asset grant and therefore token0.
	{
		TStrongObjectPtr<UProject_JAnimNotifyState_MeleeHit> KeepNotify(Notify);
		const TStrongObjectPtr<USkeletalMesh> Asset(LoadObject<USkeletalMesh>(nullptr,
			TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
		if (!TestTrue(TEXT("Shared melee cleanup uses a real existing skeleton root bone"), Asset.IsValid() && Asset->GetRefSkeleton().GetNum() > 0))
		{ World->DestroyWorld(false); return false; }
		auto* RetiredCharacter = World->SpawnActor<ACharacter>(Params);
		if (!TestNotNull(TEXT("Second actual mesh participates in the shared notify"), RetiredCharacter))
		{ World->DestroyWorld(false); return false; }
		auto* RetiredMesh = RetiredCharacter->GetMesh();
		LeaderMesh->SetSkeletalMesh(Asset.Get()); RetiredMesh->SetSkeletalMesh(Asset.Get());
		Notify->SocketName = Asset->GetRefSkeleton().GetBoneName(0);
		if (!TestTrue(TEXT("Both actual meshes expose the authored test socket"), LeaderMesh->DoesSocketExist(Notify->SocketName) && RetiredMesh->DoesSocketExist(Notify->SocketName)))
		{ World->DestroyWorld(false); return false; }
		FAnimNotifyEventReference Event; Event.SetNotifyInstanceID(11401);
		Notify->NotifyBegin(LeaderMesh, nullptr, 1.f, Event);
		Notify->NotifyBegin(RetiredMesh, nullptr, 1.f, Event);
		Notify->NotifyBegin(LeaderMesh, nullptr, 1.f, Event);
		TestTrue(TEXT("Same-call Begin burst isolates meshes and preserves duplicate execution suppression"),
			Notify->TraceStates.Num() == 2 && Notify->TraceStates.FindChecked(LeaderMesh).Num() == 1 && Notify->PostGCHandle.IsValid());
		const TWeakObjectPtr<USkeletalMeshComponent> DeadMesh(RetiredMesh);
		RetiredCharacter->Destroy(); RetiredCharacter = nullptr; RetiredMesh = nullptr;
		TestEqual(TEXT("Destroyed mesh storage is weakly retained until native GC"), Notify->TraceStates.Num(), 2);
		CollectGarbage(RF_NoFlags, true);
		TestFalse(TEXT("The actual destroyed participant is no longer a valid weak mesh"), DeadMesh.IsValid());
		TestTrue(TEXT("Native post-GC returns the stale melee entry without another Begin"),
			!Notify->TraceStates.Contains(DeadMesh) && Notify->TraceStates.Num() == 1 && Notify->TraceStates.Contains(LeaderMesh));
		Notify->NotifyEnd(LeaderMesh, nullptr, Event);
		TestTrue(TEXT("Normal End retires the surviving melee execution"), Notify->TraceStates.IsEmpty());
		const TWeakObjectPtr<UProject_JAnimNotifyState_MeleeHit> DeadNotify(Notify);
		KeepNotify.Reset(); Notify = nullptr;
		CollectGarbage(RF_NoFlags, true);
		TestFalse(TEXT("Real notify collection retires its weak GC delegate safely"), DeadNotify.IsValid());
	}

	Presentation->SpawnedWeapon = nullptr;
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCanonicalBladeTrajectoryTest, "ProjectJ.Combat.CanonicalBladeTrajectory",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJCanonicalBladeTrajectoryTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);
	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();
	LeaderMesh->SetWorldLocation(FVector(100.0, 0.0, 0.0));

	UProject_JCombatHitValidationComponent* HitValidation = NewObject<UProject_JCombatHitValidationComponent>(Character);
	Character->AddInstanceComponent(HitValidation);
	HitValidation->RegisterComponent();
	UProject_JAttackDefinition* Attack = NewObject<UProject_JAttackDefinition>();
	Attack->HitSpec.bUseCanonicalBladeTipOffset = true;
	Attack->HitSpec.CanonicalBladeTipOffset = FVector(200.0, 0.0, 0.0);
	const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.CanonicalBlade.Attack"));
	HitValidation->BeginAttackNode(AttackTag, Attack, 43);

	UProject_JAnimNotifyState_MeleeHit* Notify = NewObject<UProject_JAnimNotifyState_MeleeHit>();
	Notify->SocketName = NAME_None; // Component root stands in for a Leader socket without a skeletal asset.
	const FVector Root = LeaderMesh->GetSocketLocation(Notify->SocketName);
	const FVector FirstTip = Notify->ResolveTraceLocation(LeaderMesh);
	TestTrue(TEXT("Canonical endpoint is beyond the root"), FirstTip.Equals(Root + FVector(200.0, 0.0, 0.0)));

	AActor* VisualWeapon = World->SpawnActor<AActor>(Params);
	VisualWeapon->SetActorLocation(FVector(5000.0, 0.0, 0.0));
	TestTrue(TEXT("Initial visual position cannot change the endpoint"), Notify->ResolveTraceLocation(LeaderMesh).Equals(FirstTip));
	VisualWeapon->SetActorLocation(FVector(-5000.0, 0.0, 0.0));
	TestTrue(TEXT("Moved visual weapon cannot change the endpoint"), Notify->ResolveTraceLocation(LeaderMesh).Equals(FirstTip));

	LeaderMesh->SetWorldRotation(FRotator(0.0, 90.0, 0.0));
	const FVector SecondTip = Notify->ResolveTraceLocation(LeaderMesh);
	TestTrue(TEXT("Leader pose rotates the canonical blade tip"), SecondTip.Equals(Root + FVector(0.0, 200.0, 0.0)));
	TestTrue(TEXT("Sweep ends at the tip instead of the hand"), !SecondTip.Equals(LeaderMesh->GetSocketLocation(Notify->SocketName)));

	// Exercise the real notify Tick path with an asset-free canonical socket.
	// Begin normally requires an authored mesh socket; seed only that boundary.
	FAnimNotifyEventReference EventReference;
	const uint64 WindowToken = HitValidation->BeginHitWindow();
	Notify->HitEventTag = AttackTag;
	Notify->TraceStates.FindOrAdd(LeaderMesh).Add(EventReference.GetNotifyInstanceID(), {FirstTip, HitValidation, WindowToken});
	Notify->NotifyTick(LeaderMesh, nullptr, 1.f / 60, EventReference);
	FVector HistoricalStart = FVector::ZeroVector;
	FVector HistoricalEnd = FVector::ZeroVector;
	bool bHistoricalWindowOpen = false;
	TestTrue(TEXT("Canonical sweep enters SSR history"), HitValidation->FindAuthoritativeTraceAtTime(World->GetTimeSeconds() + 0.01f, 43, AttackTag, HistoricalStart, HistoricalEnd, bHistoricalWindowOpen));
	TestTrue(TEXT("SSR records the canonical blade endpoints"), HistoricalStart.Equals(FirstTip) && HistoricalEnd.Equals(SecondTip) && bHistoricalWindowOpen);
	HitValidation->EndAttack();
	LeaderMesh->SetWorldLocation(FVector(500, 0, 0));
	Notify->NotifyTick(LeaderMesh, nullptr, 1.f / 60, EventReference);
	TestTrue(TEXT("A canceled window cannot advance its notify trace"),
		Notify->TraceStates.FindChecked(LeaderMesh).FindChecked(EventReference.GetNotifyInstanceID()).PreviousLocation.Equals(SecondTip));
	Notify->NotifyEnd(LeaderMesh, nullptr, EventReference);
	TestTrue(TEXT("Notify end releases its execution state"), Notify->TraceStates.IsEmpty());

	// Gameplay event delivery is synchronous. A hit can cancel the swing and
	// open another one before the original sweep has dispatched all candidates.
	auto* Attacker = World->SpawnActor<AProject_JNPCCharacter>(Params);
	auto* ASC = Attacker->GetAbilitySystemComponent(); ASC->InitAbilityActorInfo(Attacker,Attacker);
	auto* Validation = NewObject<UProject_JCombatHitValidationComponent>(Attacker);
	Attacker->AddInstanceComponent(Validation); Validation->RegisterComponent();
	Validation->BeginAttackNode(AttackTag,Attack,100);
	auto* AttackMesh = Attacker->GetMesh();
	const FVector Start = AttackMesh->GetSocketLocation(NAME_None);
	const FVector End = Notify->ResolveTraceLocation(AttackMesh);
	for (double Fraction : {.3,.7})
	{
		auto* Target = World->SpawnActor<ACharacter>(Params);
		Target->SetActorLocation(FMath::Lerp(Start,End,Fraction));
		Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Target->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
	}
	const uint64 OldWindow = Validation->BeginHitWindow();
	Notify->TraceStates.FindOrAdd(AttackMesh).Add(EventReference.GetNotifyInstanceID(),{Start,Validation,OldWindow});
	int32 Delivered = 0; uint64 NewWindow = 0;
	const auto Callback = ASC->GenericGameplayEventCallbacks.FindOrAdd(AttackTag).AddLambda([&](const FGameplayEventData*)
	{
		++Delivered;
		if (Delivered==1)
		{
			Validation->EndAttack();Validation->BeginAttackNode(AttackTag,Attack,101);
			NewWindow=Validation->BeginHitWindow();
		}
	});
	Notify->NotifyTick(AttackMesh,nullptr,1.f/60,EventReference);
	TestEqual(TEXT("A canceled sweep cannot dispatch its remaining candidates into a new attack"),Delivered,1);
	TestTrue(TEXT("The new attack's window survives cancellation of the old sweep"),NewWindow && Validation->IsHitWindowCurrent(NewWindow));
	Notify->NotifyEnd(AttackMesh,nullptr,EventReference);
	TestTrue(TEXT("Late notify End cannot close the replacement window"),Validation->IsHitWindowCurrent(NewWindow));
	ASC->GenericGameplayEventCallbacks.FindChecked(AttackTag).Remove(Callback);
	Validation->EndAttack();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSSRHistoricalSweepTest, "ProjectJ.Combat.SSRHistoricalSweepInterpolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJSSRHistoricalSweepTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JCombatHitValidationComponent* HitVal = NewObject<UProject_JCombatHitValidationComponent>(Character);
	Character->AddInstanceComponent(HitVal);
	HitVal->RegisterComponent();

	const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.SSR.Attack"));
	const FGameplayTag OtherAttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.SSR.OtherAttack"));
	const int32 Key = 42;

	// Populate ring buffer with 3 historical sweeps (including Close transition boundary)
	FProject_JAuthoritativeSweepRecord Record1;
	Record1.ServerTimestamp = 10.0f;
	Record1.TraceStart = FVector(0, 0, 0);
	Record1.TraceEnd = FVector(100, 0, 0);
	Record1.AttackNodeTag = AttackTag;
	Record1.PredictionKey = Key;
	Record1.bHitWindowOpen = true;
	HitVal->AppendSweepHistoryRecord(Record1);

	FProject_JAuthoritativeSweepRecord Record2;
	Record2.ServerTimestamp = 10.1f;
	Record2.TraceStart = FVector(0, 100, 0);
	Record2.TraceEnd = FVector(100, 100, 0);
	Record2.AttackNodeTag = AttackTag;
	Record2.PredictionKey = Key;
	Record2.bHitWindowOpen = true;
	HitVal->AppendSweepHistoryRecord(Record2);

	FProject_JAuthoritativeSweepRecord Record3;
	Record3.ServerTimestamp = 10.2f;
	Record3.TraceStart = FVector(0, 200, 0);
	Record3.TraceEnd = FVector(100, 200, 0);
	Record3.AttackNodeTag = AttackTag;
	Record3.PredictionKey = Key;
	Record3.bHitWindowOpen = false; // Transition boundary: HitWindow closed at 10.2f
	HitVal->AppendSweepHistoryRecord(Record3);

	// 1. Interpolation test: timestamp 10.05 is exactly halfway between Record1 (10.0) and Record2 (10.1)
	FVector InterpStart = FVector::ZeroVector;
	FVector InterpEnd = FVector::ZeroVector;
	bool bHitWindowOpen = false;
	bool bFound = HitVal->FindAuthoritativeTraceAtTime(10.05f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);

	TestTrue(TEXT("Found historical trace within valid window"), bFound);
	TestEqual(TEXT("Interpolated TraceStart Y is halfway (50)"), InterpStart.Y, 50.0);
	TestEqual(TEXT("Interpolated TraceEnd Y is halfway (50)"), InterpEnd.Y, 50.0);
	TestTrue(TEXT("Historical HitWindow was open at 10.05"), bHitWindowOpen);

	// 2. Transition boundary test: at 10.15s (between Record2 Open and Record3 Closed)
	// Because 10.15 < 10.20 (the close transition timestamp), hit window must still be open
	bFound = HitVal->FindAuthoritativeTraceAtTime(10.15f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);
	TestTrue(TEXT("Found trace at 10.15 before close boundary"), bFound);
	TestTrue(TEXT("HitWindow is open prior to close timestamp"), bHitWindowOpen);
	TestNearlyEqual(TEXT("Interpolated TraceStart Y at 10.15 is 150"), InterpStart.Y, 150.0, 0.01);

	// 3. Exact close transition timestamp test: at 10.20s
	bFound = HitVal->FindAuthoritativeTraceAtTime(10.20f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);
	TestTrue(TEXT("Found trace at 10.20"), bFound);
	TestFalse(TEXT("HitWindow is closed exactly at close timestamp"), bHitWindowOpen);

	// 4. Immediately after close transition within future tolerance: at 10.22s
	bFound = HitVal->FindAuthoritativeTraceAtTime(10.22f, Key, AttackTag, InterpStart, InterpEnd, bHitWindowOpen);
	TestTrue(TEXT("Found trace at 10.22 within tolerance"), bFound);
	TestFalse(TEXT("HitWindow remains closed after close timestamp"), bHitWindowOpen);

	// 5. Reject out-of-bounds timestamp (ancient past: 5.0)
	FVector OutStart, OutEnd;
	bool bOutWindow = false;
	TestFalse(TEXT("Rejects ancient timestamp before history bounds"),
		HitVal->FindAuthoritativeTraceAtTime(5.0f, Key, AttackTag, OutStart, OutEnd, bOutWindow));

	// 6. Reject future timestamp (15.0)
	TestFalse(TEXT("Rejects future timestamp beyond tolerance"),
		HitVal->FindAuthoritativeTraceAtTime(15.0f, Key, AttackTag, OutStart, OutEnd, bOutWindow));

	// 7. Reject prediction key mismatch
	const int32 WrongKey = 999;
	TestFalse(TEXT("Rejects prediction key mismatch"),
		HitVal->FindAuthoritativeTraceAtTime(10.05f, WrongKey, AttackTag, OutStart, OutEnd, bOutWindow));

	// 8. Reject attack node mismatch
	TestFalse(TEXT("Rejects attack node mismatch"),
		HitVal->FindAuthoritativeTraceAtTime(10.05f, Key, OtherAttackTag, OutStart, OutEnd, bOutWindow));

	// 9. Ring buffer wrap-around test: fill 135 records to exceed capacity 128
	for (int32 Index = 0; Index < 135; ++Index)
	{
		FProject_JAuthoritativeSweepRecord WrapRecord;
		WrapRecord.ServerTimestamp = 20.0f + Index * 0.01f;
		WrapRecord.TraceStart = FVector(Index, 0, 0);
		WrapRecord.TraceEnd = FVector(Index + 10, 0, 0);
		WrapRecord.AttackNodeTag = AttackTag;
		WrapRecord.PredictionKey = 100;
		WrapRecord.bHitWindowOpen = true;
		HitVal->AppendSweepHistoryRecord(WrapRecord);
	}

	TestEqual(TEXT("SweepHistoryCount clamped to MaxSweepHistoryCapacity (128)"), HitVal->SweepHistoryCount, 128);

	// Query oldest available record in wrapped buffer (record 7 is at 20.0 + 7 * 0.01 = 20.07)
	const bool bFoundWrappedOldest = HitVal->FindAuthoritativeTraceAtTime(20.07f, 100, AttackTag, OutStart, OutEnd, bOutWindow);
	TestTrue(TEXT("Found oldest available record in wrapped ring buffer"), bFoundWrappedOldest);

	// Query overwritten record prior to wrap (e.g. record 0 at 20.0) -> must reject
	const bool bFoundOverwritten = HitVal->FindAuthoritativeTraceAtTime(20.01f, 100, AttackTag, OutStart, OutEnd, bOutWindow);
	TestFalse(TEXT("Rejects overwritten record in ring buffer"), bFoundOverwritten);

	// The same component now records one complete authoritative path per native dispatch.
	HitVal->EndAttack();
	auto* PacketAttack = NewObject<UProject_JAttackDefinition>(HitVal);
	HitVal->BeginAttackNode(AttackTag, PacketAttack, Key);
	const uint64 PacketWindow = HitVal->BeginHitWindow(), PacketSpatial = HitVal->GetSpatialRevision();
	TArray<FProject_JAuthoritativeTraceSegment> Path;
	for (int32 I = 0; I < 3; ++I)
	{
		FProject_JAuthoritativeTraceSegment Piece;
		Piece.TraceStart = FVector(I * 10, I * 20, 0); Piece.TraceEnd = FVector(I * 10 + 5, I * 20 + 10, 0); Path.Add(Piece);
	}
	const uint64 FirstPacket = HitVal->RecordAuthoritativeTracePacket(Path, PacketWindow, PacketSpatial);
	TestTrue(TEXT("A completed path gets one real publication"), FirstPacket != 0);
	TestEqual(TEXT("Three pieces occupy one ring record"), HitVal->SweepHistoryCount, 1);
	TestEqual(TEXT("Retained geometry accounts for actual pieces"), HitVal->SweepHistorySegmentCount, 3);
	FProject_JAuthoritativeSweepRecord ReadPacket;
	TestTrue(TEXT("Public packet readback preserves the atomic path"), HitVal->FindAuthoritativeTracePacketAtTime(World->GetTimeSeconds(), Key, AttackTag, ReadPacket));
	TestTrue(TEXT("Atomic path has all three actual pieces"), ReadPacket.bAtomicPath && ReadPacket.Segments.Num() == 3);
	if (ReadPacket.Segments.Num() == 3)
		TestTrue(TEXT("Later piece endpoints are not a first-to-last chord"), ReadPacket.Segments[2].TraceStart == Path[2].TraceStart && ReadPacket.Segments[2].TraceEnd == Path[2].TraceEnd);
	OutStart = OutEnd = FVector(99); bOutWindow = true;
	TestFalse(TEXT("Legacy two-vector API cannot flatten an atomic path"), HitVal->FindAuthoritativeTraceAtTime(World->GetTimeSeconds(), Key, AttackTag, OutStart, OutEnd, bOutWindow));
	TestTrue(TEXT("Unavailable legacy readback initializes every output"), OutStart.IsZero() && OutEnd.IsZero() && !bOutWindow);
	TestTrue(TEXT("Contact selects its exact owned segment without another record"), HitVal->SelectAuthoritativeTraceSegment(FirstPacket, PacketWindow, PacketSpatial, 1));
	TestTrue(TEXT("Authority origin is the contact piece"), HitVal->LastAuthoritativeTraceStart == Path[1].TraceStart && HitVal->LastAuthoritativeTraceEnd == Path[1].TraceEnd);
	TestEqual(TEXT("Segment selection does not grow history"), HitVal->SweepHistoryCount, 1);
	TestFalse(TEXT("Out-of-range piece does not select another geometry"), HitVal->SelectAuthoritativeTraceSegment(FirstPacket, PacketWindow, PacketSpatial, 3));
	TestEqual(TEXT("Foreign spatial revision cannot publish"), HitVal->RecordAuthoritativeTracePacket(Path, PacketWindow, PacketSpatial + 1), uint64(0));
	Path[1].TraceStart.X = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("Nonfinite path rejects the entire publication"), HitVal->RecordAuthoritativeTracePacket(Path, PacketWindow, PacketSpatial), uint64(0));
	TestEqual(TEXT("Rejected path preserves the last valid whole record"), HitVal->SweepHistoryCount, 1);
	TestEqual(TEXT("Rejected path preserves retained geometry count"), HitVal->SweepHistorySegmentCount, 3);
	Path[1].TraceStart = FVector(10, 20, 0);
	TArray<FProject_JAuthoritativeTraceSegment> MaximumPath; MaximumPath.Init(Path[0], UProject_JCombatHitValidationComponent::MaxTracePacketSegments);
	TArray<FProject_JAuthoritativeTraceSegment> OverflowPath = MaximumPath; OverflowPath.Add(Path[0]);
	TestEqual(TEXT("Over-cap path cannot publish a shortened prefix"), HitVal->RecordAuthoritativeTracePacket(OverflowPath, PacketWindow, PacketSpatial), uint64(0));
	uint64 LatestPacket = 0;
	for (int32 I = 0; I < 4; ++I) LatestPacket = HitVal->RecordAuthoritativeTracePacket(MaximumPath, PacketWindow, PacketSpatial);
	TestTrue(TEXT("Bounded maximum path is admitted intact"), LatestPacket != 0);
	TestEqual(TEXT("Geometry cap evicts oldest whole packets"), HitVal->SweepHistorySegmentCount, 189);
	TestEqual(TEXT("Whole-packet eviction retains three complete paths"), HitVal->SweepHistoryCount, 3);
	int32 RetainedPieces = 0, AllocatedSlots = 0;
	for (const auto& Record : HitVal->AuthoritativeSweepHistory)
	{
		RetainedPieces += Record.Segments.Num();
		AllocatedSlots += Record.Segments.Max() > 0;
	}
	TestEqual(TEXT("Inactive slots retain no path array capacity"), AllocatedSlots, 3);
	TestEqual(TEXT("Physical path storage contains no evicted prefixes"), RetainedPieces, 189);
	TestFalse(TEXT("An older publication cannot select a later path"), HitVal->SelectAuthoritativeTraceSegment(FirstPacket, PacketWindow, PacketSpatial, 0));
	HitVal->EndHitWindow(PacketWindow);
	TestFalse(TEXT("A real window close revokes contact selection"), HitVal->SelectAuthoritativeTraceSegment(LatestPacket, PacketWindow, PacketSpatial, 0));
	TestTrue(TEXT("Public packet lookup retains the exact close marker"), HitVal->FindAuthoritativeTracePacketAtTime(World->GetTimeSeconds(), Key, AttackTag, ReadPacket));
	TestTrue(TEXT("Close marker has no open geometry or duplicated path allocation"), !ReadPacket.bHitWindowOpen && ReadPacket.Segments.IsEmpty());
	// Publicly permitted mixed writers at one native instant still obey the final close publication.
	HitVal->BeginAttackNode(AttackTag, PacketAttack, Key);
	const uint64 MixedWindow = HitVal->BeginHitWindow();
	HitVal->RecordAuthoritativeTrace(Path[0].TraceStart, Path[0].TraceEnd);
	TestTrue(TEXT("Mixed history admits the complete atomic path"), HitVal->RecordAuthoritativeTracePacket(Path, MixedWindow, PacketSpatial) != 0);
	HitVal->EndHitWindow(MixedWindow);
	TestTrue(TEXT("Mixed same-time packet lookup finds the final publication"), HitVal->FindAuthoritativeTracePacketAtTime(World->GetTimeSeconds(), Key, AttackTag, ReadPacket));
	TestTrue(TEXT("Final atomic close defeats an earlier same-time legacy open"), ReadPacket.bAtomicPath && !ReadPacket.bHitWindowOpen && ReadPacket.Segments.IsEmpty());
	OutStart = OutEnd = FVector(99); bOutWindow = true;
	TestFalse(TEXT("Mixed same-time history cannot masquerade as a legacy open chord"), HitVal->FindAuthoritativeTraceAtTime(World->GetTimeSeconds(), Key, AttackTag, OutStart, OutEnd, bOutWindow));
	TestTrue(TEXT("Mixed unavailable legacy outputs are cleared"), OutStart.IsZero() && OutEnd.IsZero() && !bOutWindow);
	HitVal->ResetSpatialHistory();
	TestEqual(TEXT("Spatial discontinuity retires every whole record"), HitVal->SweepHistoryCount, 0);
	TestEqual(TEXT("Spatial discontinuity releases all retained geometry"), HitVal->SweepHistorySegmentCount, 0);
	HitVal->EndAttack();
	for (const auto& Record : HitVal->AuthoritativeSweepHistory)
		TestEqual(TEXT("Attack retirement leaves no owning path capacity"), Record.Segments.Max(), 0);

	const auto VerifyPacketRPC = [&]() -> bool
	{
		auto* PacketWorld = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(PacketWorld);
		ON_SCOPE_EXIT { if (PacketWorld->GetBegunPlay()) PacketWorld->EndPlay(EEndPlayReason::Quit); PacketWorld->DestroyWorld(false); GEngine->DestroyWorldContext(PacketWorld); };
		const auto MakeRecipient = [&](const FVector& Position)
		{
			auto* Actor = PacketWorld->SpawnActor<AProject_JPlayerMaturityASCFixture>(AProject_JPlayerMaturityASCFixture::StaticClass(), Position, FRotator::ZeroRotator, Params);
			if (!Actor) return Actor;
			auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Actor); Actor->CurrentASC = ASC;
			Actor->AddInstanceComponent(ASC); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Actor, Actor);
			auto* Attributes = NewObject<UProject_JAttributeSet>(Actor); ASC->AddAttributeSetSubobject(Attributes);
			ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
			ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
			return Actor;
		};
		auto* Attacker = MakeRecipient(FVector(0, 0, 100)); auto* Victim = MakeRecipient(FVector(120, 0, 100));
		if (!TestNotNull(TEXT("Packet RPC actual authority source"), Attacker) || !TestNotNull(TEXT("Packet RPC actual recipient"), Victim)) return false;
		auto* PacketHit = NewObject<UProject_JCombatHitValidationComponent>(Attacker); Attacker->AddInstanceComponent(PacketHit); PacketHit->RegisterComponent();
		auto* Rewind = NewObject<UProject_JServerSideRewindComponent>(Victim); Victim->AddInstanceComponent(Rewind); Rewind->RegisterComponent();
		const auto* Reference = LoadObject<UProject_JAttackDefinition>(nullptr, TEXT("/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1.DA_Attack_Spellblade_LMB1"));
		if (!TestNotNull(TEXT("Existing authored damage effect for packet RPC"), Reference) || !Reference->DamageEffect) return false;
		const auto* Effect = Reference->DamageEffect->GetDefaultObject<UGameplayEffect>();
		float Delta = 0.f;
		for (const auto& Modifier : Effect->Modifiers)
			if (Modifier.Attribute == UProject_JAttributeSet::GetHealthAttribute() && Modifier.ModifierOp == EGameplayModOp::Additive)
			{ float Magnitude = 0; if (Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, Magnitude)) Delta += Magnitude; }
		if (!TestTrue(TEXT("Packet RPC uses a real instant authored health loss"), Effect->DurationPolicy == EGameplayEffectDurationType::Instant && Delta < 0)) return false;
		PacketWorld->InitializeActorsForPlay(FURL()); PacketWorld->GetWorldSettings()->NotifyBeginPlay();
		PacketWorld->Tick(LEVELTICK_All, .05f); // Native interval scheduler records the actual target capsule.
		const double CaptureTime = PacketWorld->GetTimeSeconds();
		const FVector TargetPosition = Victim->GetCapsuleComponent()->GetComponentLocation();
		TArray<FProject_JAuthoritativeTraceSegment> Pieces;
		FProject_JAuthoritativeTraceSegment Miss; Miss.TraceStart = Attacker->GetActorLocation() + FVector(0, 200, 0); Miss.TraceEnd = Miss.TraceStart + FVector(10, 0, 0); Pieces.Add(Miss);
		FProject_JAuthoritativeTraceSegment Match; Match.TraceStart = TargetPosition + FVector(0, -100, 0); Match.TraceEnd = TargetPosition + FVector(0, -80, 0); Pieces.Add(Match);
		auto* Definition = NewObject<UProject_JAttackDefinition>(PacketHit); Definition->AttackTag = AttackTag; Definition->DamageEffect = Reference->DamageEffect; Definition->HitSpec = Reference->HitSpec;
		if (!TestTrue(TEXT("Native rewind rejects the unrelated first piece"), !Rewind->ServerVerifyHit(CaptureTime, Miss.TraceStart, Miss.TraceEnd, Definition->HitSpec.TraceRadius)) ||
			!TestTrue(TEXT("Native rewind confirms the later piece against an actually recorded capsule"), Rewind->ServerVerifyHit(CaptureTime, Match.TraceStart, Match.TraceEnd, Definition->HitSpec.TraceRadius))) return false;
		PacketHit->BeginAttackNode(AttackTag, Definition, Key);
		const uint64 Window = PacketHit->BeginHitWindow();
		if (!TestTrue(TEXT("Real RPC source publishes a complete path"), PacketHit->RecordAuthoritativeTracePacket(Pieces, Window, PacketHit->GetSpatialRevision()) != 0)) return false;
		const auto MakeBlocker = [&](const FVector& Location)
		{
			auto* Blocker = PacketWorld->SpawnActor<AActor>(Params); if (!Blocker) return Blocker;
			auto* Box = NewObject<UBoxComponent>(Blocker); Blocker->AddInstanceComponent(Box); Blocker->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(5, 4, 200)); Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionResponseToAllChannels(ECR_Ignore); Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
			Box->RegisterComponent(); Blocker->SetActorLocation(Location); return Blocker;
		};
		const auto Occluder = [&](const FVector& Origin) -> AActor*
		{
			FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJPacketRPCFixture), false, Attacker); Query.AddIgnoredActor(Attacker);
			return PacketWorld->LineTraceSingleByChannel(Hit, Origin, Victim->GetActorLocation(), ECC_Visibility, Query) ? Hit.GetActor() : nullptr;
		};
		int32 Effects = 0; bool ExactSource = true;
		const FDelegateHandle Applied = Victim->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&](UAbilitySystemComponent* EffectSource, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
			{ if (Spec.Def == Effect) { ++Effects; ExactSource &= EffectSource == Attacker->CurrentASC.Get(); } });
		ON_SCOPE_EXIT { Victim->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Applied); };
		const float Before = Victim->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
		auto* BlockedMatch = MakeBlocker(TargetPosition + FVector(0, -60, 0));
		if (!TestNotNull(TEXT("Actual matched-piece visibility blocker"), BlockedMatch)) return false;
		bool Passed = TestTrue(TEXT("Native visibility query blocks the matching piece"), Occluder(Match.TraceStart) == BlockedMatch);
		auto* MissLOS = Occluder(Miss.TraceStart);
		Passed &= TestTrue(TEXT("Unrelated first piece has clear visibility"), !MissLOS || MissLOS == Victim);
		PacketHit->ServerRequestSSRHit(Victim, CaptureTime, Match.TraceStart, Match.TraceEnd, AttackTag, 1, Key);
		Passed &= TestEqual(TEXT("Blocked matching piece does not confirm a GE"), Effects, 0);
		Passed &= TestEqual(TEXT("Blocked matching piece leaves health unchanged"), Victim->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), Before);
		Passed &= TestEqual(TEXT("Blocked admitted packet still consumes its request sequence"), PacketHit->LastServerRequestSequence, 1);
		BlockedMatch->Destroy();
		auto* UnrelatedBlocker = MakeBlocker(FMath::Lerp(Miss.TraceStart, TargetPosition, .5));
		if (!TestNotNull(TEXT("Actual unrelated-piece visibility blocker"), UnrelatedBlocker)) return false;
		PacketWorld->Tick(LEVELTICK_All, .05f); // Normal scene retirement; retain the original real historical timestamp.
		Passed &= TestTrue(TEXT("Native visibility query now blocks only the unrelated first origin"), Occluder(Miss.TraceStart) == UnrelatedBlocker);
		auto* MatchLOS = Occluder(Match.TraceStart);
		Passed &= TestTrue(TEXT("Actual matching piece remains visible"), !MatchLOS || MatchLOS == Victim);
		PacketHit->ServerRequestSSRHit(Victim, CaptureTime, Match.TraceStart, Match.TraceEnd, AttackTag, 2, Key);
		Passed &= TestEqual(TEXT("Actual server RPC reaches a later packet piece and applies one GE"), Effects, 1);
		Passed &= TestTrue(TEXT("Confirmed packet effect reports the exact source ASC"), ExactSource);
		Passed &= TestEqual(TEXT("Later-piece GE applies the existing authored health delta"), Victim->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), FMath::Max(0.f, Before + Delta));
		PacketHit->ServerRequestSSRHit(Victim, CaptureTime, Match.TraceStart, Match.TraceEnd, AttackTag, 3, Key);
		Passed &= TestEqual(TEXT("Repeated packet request cannot damage the same victim twice"), Effects, 1);
		PacketHit->EndAttack(); UnrelatedBlocker->Destroy();
		if (!Passed) return false;
		// A normal current-collision recipient has no rewind owner. Set up a separate
		// stationary query scene before beginning its attack; no private pose/clock data.
		Attacker->GetCharacterMovement()->DisableMovement();
		Attacker->GetCharacterMovement()->StopMovementImmediately();
		Attacker->SetActorLocation(FVector(0, 1000, 100));
		auto* PhysicsVictim = MakeRecipient(FVector(90, 1010, 100));
		if (!TestNotNull(TEXT("Current-physics recipient"), PhysicsVictim)) return false;
		PhysicsVictim->GetCharacterMovement()->DisableMovement(); PhysicsVictim->GetCharacterMovement()->StopMovementImmediately();
		PhysicsVictim->GetCapsuleComponent()->InitCapsuleSize(10.f, 10.f);
		PhysicsVictim->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		PhysicsVictim->GetCapsuleComponent()->SetCollisionObjectType(ECC_Pawn);
		PhysicsVictim->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);
		TestNull(TEXT("The new recipient genuinely has no historical-rewind component"), PhysicsVictim->FindComponentByClass<UProject_JServerSideRewindComponent>());
		Definition->HitSpec.TraceRadius = 1.f; // Transient fixture definition, not an authored asset/CDO change.
		PacketWorld->Tick(LEVELTICK_All, .05f);
		const FVector PhysicsCenter = PhysicsVictim->GetCapsuleComponent()->GetComponentLocation();
		const FVector DiagonalStart = PhysicsCenter - FVector(90, 10, 0), DiagonalEnd = DiagonalStart + FVector(100, 100, 0);
		const FVector ContactStart = PhysicsCenter + FVector(-20, 0, 0), ContactEnd = PhysicsCenter + FVector(20, 0, 0);
		const auto NativeContact = [&](const FVector& Start, const FVector& End)
		{
			TArray<FHitResult> Hits; FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJCurrentSSRFixture), false, Attacker);
			Query.AddIgnoredActor(Attacker);
			PacketWorld->SweepMultiByObjectType(Hits, Start, End, FQuat::Identity, FCollisionObjectQueryParams(ECC_Pawn), FCollisionShape::MakeSphere(1.f), Query);
			return Hits.ContainsByPredicate([PhysicsVictim](const FHitResult& Hit) { return Hit.GetActor() == PhysicsVictim; });
		};
		const FBox DiagonalBounds = FBox(DiagonalStart.ComponentMin(DiagonalEnd), DiagonalStart.ComponentMax(DiagonalEnd)).ExpandBy(1.f);
		if (!TestTrue(TEXT("Negative control overlaps the former coarse boxes but not the native capsule/sphere sweep"),
			PhysicsVictim->GetComponentsBoundingBox().Intersect(DiagonalBounds) && FMath::PointDistToSegment(PhysicsCenter, DiagonalStart, DiagonalEnd) > 11.f
			&& !NativeContact(DiagonalStart, DiagonalEnd) && NativeContact(ContactStart, ContactEnd))) return false;
		int32 PhysicsEffects = 0; bool PhysicsExactSource = true;
		const auto PhysicsApplied = PhysicsVictim->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
			[&](UAbilitySystemComponent* Source, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
			{ if (Spec.Def == Effect) { ++PhysicsEffects; PhysicsExactSource &= Source == Attacker->CurrentASC.Get(); } });
		ON_SCOPE_EXIT { PhysicsVictim->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.Remove(PhysicsApplied); };
		const float PhysicsBefore = PhysicsVictim->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
		PacketHit->BeginAttackNode(AttackTag, Definition, Key); PacketHit->SetHitWindowOpen(true);
		PacketHit->RecordAuthoritativeTrace(DiagonalStart, DiagonalEnd);
		// A genuine contact hint cannot replace the server-owned diagonal geometry.
		PacketHit->ServerRequestSSRHit(PhysicsVictim, PacketWorld->GetTimeSeconds(), ContactStart, ContactEnd, AttackTag, 4, Key);
		Passed &= TestEqual(TEXT("Coarse AABB overlap cannot apply a current-physics GE"), PhysicsEffects, 0);
		Passed &= TestEqual(TEXT("Rejected coarse overlap preserves actual health"), PhysicsVictim->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), PhysicsBefore);
		Passed &= TestEqual(TEXT("Rejected valid geometry still consumes native sequence admission"), PacketHit->LastServerRequestSequence, 4);
		PacketWorld->Tick(LEVELTICK_All, .05f);
		Passed &= TestTrue(TEXT("The physical recipient stayed at its admitted fixture pose"), PhysicsVictim->GetCapsuleComponent()->GetComponentLocation().Equals(PhysicsCenter, 1.e-4));
		PacketHit->RecordAuthoritativeTrace(ContactStart, ContactEnd);
		const double PhysicsTime = PacketWorld->GetTimeSeconds();
		const FVector MissHint = DiagonalStart + FVector(0, 80, 0);
		PacketHit->ServerRequestSSRHit(PhysicsVictim, PhysicsTime, MissHint, MissHint + FVector(1, 0, 0), AttackTag, 5, Key);
		Passed &= TestEqual(TEXT("Actual current native contact applies one authored GE despite a nonintersecting client hint"), PhysicsEffects, 1);
		Passed &= TestTrue(TEXT("Current-physics GE uses the actual source ASC"), PhysicsExactSource);
		Passed &= TestEqual(TEXT("Current native contact applies the real authored health delta"), PhysicsVictim->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), FMath::Max(0.f, PhysicsBefore + Delta));
		PacketHit->ServerRequestSSRHit(PhysicsVictim, PhysicsTime, MissHint, MissHint + FVector(1, 0, 0), AttackTag, 6, Key);
		Passed &= TestEqual(TEXT("Current-physics confirmed target remains deduplicated"), PhysicsEffects, 1);
		PacketHit->EndAttack();
		// Legacy history must use its selected owned origin, not the newest server
		// trace origin. Visibility is current-world policy; this is not scene rewind.
		auto* PhysicsBlocker = MakeBlocker(PhysicsCenter + FVector(-20, 0, 0));
		if (!TestNotNull(TEXT("Legacy selected-origin visibility blocker"), PhysicsBlocker)) return false;
		const auto PhysicsOccluder = [&](const FVector& Start)
		{
			FHitResult Hit; FCollisionQueryParams Query(SCENE_QUERY_STAT(ProjectJCurrentSSRLOS), false, Attacker);
			Query.AddIgnoredActor(Attacker);
			return PacketWorld->LineTraceSingleByChannel(Hit, Start, PhysicsVictim->GetActorLocation(), ECC_Visibility, Query) ? Hit.GetActor() : nullptr;
		};
		const FVector BlockedStart = PhysicsCenter + FVector(-40, 0, 0), BlockedEnd = PhysicsCenter + FVector(20, 0, 0);
		const FVector ClearStart = PhysicsCenter + FVector(0, -40, 0), ClearEnd = PhysicsCenter + FVector(0, 20, 0);
		auto* ClearOccluder = PhysicsOccluder(ClearStart);
		if (!TestTrue(TEXT("Actual native contact exists for both legacy pieces; only one owned origin is obstructed"),
			NativeContact(BlockedStart, BlockedEnd) && NativeContact(ClearStart, ClearEnd) && PhysicsOccluder(BlockedStart) == PhysicsBlocker
			&& (!ClearOccluder || ClearOccluder == PhysicsVictim))) return false;
		PacketHit->BeginAttackNode(AttackTag, Definition, Key); PacketHit->SetHitWindowOpen(true);
		PacketHit->RecordAuthoritativeTrace(BlockedStart, BlockedEnd); const double BlockedTime = PacketWorld->GetTimeSeconds();
		PacketWorld->Tick(LEVELTICK_All, .05f); PacketHit->RecordAuthoritativeTrace(ClearStart, ClearEnd);
		FProject_JAuthoritativeSweepRecord SelectedLegacy;
		Passed &= TestTrue(TEXT("Actual query selects the old non-atomic owned packet"), PacketHit->FindAuthoritativeTracePacketAtTime(BlockedTime, Key, AttackTag, SelectedLegacy)
			&& !SelectedLegacy.bAtomicPath && SelectedLegacy.TraceStart.Equals(BlockedStart, 1.e-4));
		PacketHit->ServerRequestSSRHit(PhysicsVictim, BlockedTime, ClearStart, ClearEnd, AttackTag, 7, Key);
		Passed &= TestEqual(TEXT("A clear latest server origin cannot authorize a blocked selected legacy origin"), PhysicsEffects, 1);
		PacketWorld->Tick(LEVELTICK_All, .05f); PacketHit->RecordAuthoritativeTrace(ClearStart, ClearEnd);
		const double ClearTime = PacketWorld->GetTimeSeconds();
		PacketWorld->Tick(LEVELTICK_All, .05f); PacketHit->RecordAuthoritativeTrace(BlockedStart, BlockedEnd);
		PacketHit->ServerRequestSSRHit(PhysicsVictim, ClearTime, BlockedStart, BlockedEnd, AttackTag, 8, Key);
		Passed &= TestEqual(TEXT("A blocked latest server origin cannot reject a clear selected legacy origin"), PhysicsEffects, 2);
		Passed &= TestEqual(TEXT("Selected legacy contact applies exactly one further authored health delta"), PhysicsVictim->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), FMath::Max(0.f, PhysicsBefore + 2.f * Delta));
		PacketHit->ServerRequestSSRHit(PhysicsVictim, ClearTime, BlockedStart, BlockedEnd, AttackTag, 9, Key);
		Passed &= TestEqual(TEXT("Selected legacy contact retains exact target dedup"), PhysicsEffects, 2);
		PacketHit->EndAttack(); PhysicsBlocker->Destroy();
		return Passed;
	};
	TestTrue(TEXT("Standalone authority RPC consumes real multi-piece rewind and LOS"), VerifyPacketRPC());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStableGripTargetsTest, "ProjectJ.Presentation.StableGripTargetsAndAuthoredAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJStableGripTargetsTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JWeaponPresentationComponent* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Character);
	Character->AddInstanceComponent(Presentation);
	Presentation->RegisterComponent();

	// 1. Create a mock visual weapon actor with static mesh sockets
	AActor* MockWeapon = World->SpawnActor<AActor>(Params);
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MockWeapon);

	UStaticMeshSocket* SocketR = NewObject<UStaticMeshSocket>(StaticMesh);
	SocketR->SocketName = TEXT("WeaponGrip_R");
	SocketR->RelativeLocation = FVector(10.0, 20.0, 30.0);
	StaticMesh->AddSocket(SocketR);

	UStaticMeshSocket* SocketL = NewObject<UStaticMeshSocket>(StaticMesh);
	SocketL->SocketName = TEXT("WeaponGrip_L");
	SocketL->RelativeLocation = FVector(-10.0, -20.0, 30.0);
	StaticMesh->AddSocket(SocketL);

	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(MockWeapon);
	MeshComp->SetStaticMesh(StaticMesh);
	MockWeapon->SetRootComponent(MeshComp);
	MeshComp->RegisterComponent();

	// 2. Setup WeaponPresentationProfile with authored alphas
	auto* SourceAsset = LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("Authored return-attachment fixture"),SourceAsset))
	{ GEngine->DestroyWorldContext(World);World->DestroyWorld(false);return false; }
	Character->GetMesh()->SetSkeletalMeshAsset(SourceAsset);
	Character->GetMesh()->SetWorldTransform(FTransform::Identity);
	Character->GetMesh()->RefreshBoneTransforms();
	UProject_JWeaponPresentationProfile* Profile = NewObject<UProject_JWeaponPresentationProfile>(Character);
	Profile->MotionPresentation.PrimaryGripSocketName = TEXT("WeaponGrip_R");
	Profile->MotionPresentation.SecondaryGripSocketName = TEXT("WeaponGrip_L");
	Profile->MotionPresentation.DefaultDrawnPrimaryIKAlpha = 1.0f;
	Profile->MotionPresentation.DefaultDrawnSecondaryIKAlpha = 0.85f;
	Profile->MotionPresentation.DefaultSheathedPrimaryIKAlpha = 0.5f;
	Profile->MotionPresentation.DefaultSheathedSecondaryIKAlpha = 0.0f;
	Profile->DrawnSocketName = TEXT("root"); // Preserve the mock weapon's identity transform on motion return.

	Presentation->AppliedProfile = Profile;
	Presentation->SpawnedWeapon = MockWeapon;
	Presentation->UpdateSocketComponentCache();

	// Verify cached components
	TestEqual(TEXT("Cached primary grip component"), Presentation->FindWeaponSocketComponent(TEXT("WeaponGrip_R")), static_cast<USceneComponent*>(MeshComp));
	TestEqual(TEXT("Cached secondary grip component"), Presentation->FindWeaponSocketComponent(TEXT("WeaponGrip_L")), static_cast<USceneComponent*>(MeshComp));
	TestNull(TEXT("Absent probe socket"), Presentation->FindWeaponSocketComponent(TEXT("MissingProbe")));
	TestTrue(TEXT("Absent socket cached"), Presentation->MissingSocketNames.Contains(TEXT("MissingProbe")));

	// 3. Test Drawn State
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Drawn;
	Presentation->UpdateGripTargets();
	FProject_JWeaponGripTargets Targets = Presentation->GetWeaponGripTargets();

	TestTrue(TEXT("Drawn: has primary grip"), Targets.bHasPrimaryGrip);
	TestTrue(TEXT("Drawn: has secondary grip"), Targets.bHasSecondaryGrip);
	TestEqual(TEXT("Drawn: Primary IK Alpha matches profile (1.0)"), Targets.PrimaryIKAlpha, 1.0f);
	TestEqual(TEXT("Drawn: Secondary IK Alpha matches profile (0.85)"), Targets.SecondaryIKAlpha, 0.85f);
	TestEqual(TEXT("Drawn: Primary grip X location matches socket (10.0)"), Targets.PrimaryGripWorldTransform.GetLocation().X, 10.0);
	TestEqual(TEXT("Drawn: Secondary grip X location matches socket (-10.0)"), Targets.SecondaryGripWorldTransform.GetLocation().X, -10.0);

	// A weapon attached to the rendered hand must not pull that same hand back
	// toward its own socket. Secondary grip authoring remains independent.
	USkeletalMeshComponent* VisualFollower = NewObject<USkeletalMeshComponent>(Character);
	VisualFollower->SetupAttachment(Character->GetMesh());
	VisualFollower->RegisterComponent();
	MockWeapon->AttachToComponent(VisualFollower, FAttachmentTransformRules::KeepWorldTransform);
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Visual hand attachment suppresses primary feedback"), Targets.PrimaryIKAlpha, 0.0f);
	TestEqual(TEXT("Visual hand attachment identifies primary-hand drive"), Targets.DriveMode, EProject_JWeaponGripDriveMode::PrimaryHand);
	TestTrue(TEXT("Visual hand attachment blocks primary IK curves"), Targets.bPrimaryIKSuppressedByAttachment);
	TestEqual(TEXT("Visual hand attachment preserves secondary grip"), Targets.SecondaryIKAlpha, 0.85f);
	Profile->MotionPresentation.bAllowPrimaryIKOnVisualAttachment = true;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Authored primary IK exception remains available"), Targets.PrimaryIKAlpha, 1.0f);
	TestFalse(TEXT("Authored exception permits primary IK curves"), Targets.bPrimaryIKSuppressedByAttachment);
	Profile->MotionPresentation.bAllowPrimaryIKOnVisualAttachment = false;
	MockWeapon->AttachToComponent(Character->GetMesh(), FAttachmentTransformRules::KeepWorldTransform);

	// 4. Test Sheathed State
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Sheathed;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();

	TestEqual(TEXT("Sheathed: Primary IK Alpha matches profile (0.5)"), Targets.PrimaryIKAlpha, 0.5f);
	TestEqual(TEXT("Sheathed: Secondary IK Alpha matches profile (0.0)"), Targets.SecondaryIKAlpha, 0.0f);

	// 5. Test Independent Motion override
	Presentation->bIndependentMotionActive = true;
	Presentation->ActivePrimaryGripIKAlpha = 0.7f;
	Presentation->ActiveSecondaryGripIKAlpha = 0.3f;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();

	TestEqual(TEXT("Independent Motion: Primary IK Alpha overridden to 0.7"), Targets.PrimaryIKAlpha, 0.7f);
	TestEqual(TEXT("Independent Motion: Secondary IK Alpha overridden to 0.3"), Targets.SecondaryIKAlpha, 0.3f);

	// 6. Test End Independent Motion -> restores default drawn alpha
	Presentation->EndIndependentMotion();
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();

	TestEqual(TEXT("End Motion: Primary IK Alpha restored to drawn (1.0)"), Targets.PrimaryIKAlpha, 1.0f);
	TestEqual(TEXT("End Motion: Secondary IK Alpha restored to drawn (0.85)"), Targets.SecondaryIKAlpha, 0.85f);

	// A late notify can change motion intent after a direct post-process pull.
	// Both the actual transform and every animation consumer keep that published
	// pose until the next frame; no second motion clock or pose evaluation.
	Profile->MotionPresentation.bSupportsIndependentMotion=true;
	const bool ContactHandoff=Profile->MotionPresentation.bUseContactHandoff;
	Profile->MotionPresentation.bUseContactHandoff=false;
	TArray<FProject_JWeaponMotionKey> Keys;Keys.SetNum(2);
	Keys[0].NormalizedTime=0.f;Keys[1].NormalizedTime=1.f;Keys[1].RelativeTransform=FTransform(FVector(8,0,0));
	Presentation->AnimationGripTargetsFrame=MAX_uint64;
	const uint64 MotionToken=Presentation->BeginNotifyIndependentMotion(Keys,.7f,.3f,1.f,0.f,0.f);
	TestTrue(TEXT("Frame publication fixture starts a real motion lease"),MotionToken!=0);
	Presentation->SetIndependentMotionPosition(.25f);
	const auto Published=Presentation->GetWeaponGripTargetsForAnimation(1.f/60.f);
	const FTransform PublishedRoot=MeshComp->GetComponentTransform();
	Presentation->SetIndependentMotionPosition(.75f);
	Presentation->TickComponent(1.f/60.f,LEVELTICK_All,nullptr);
	TestTrue(TEXT("A late motion notify cannot move a weapon after its pose targets were consumed"),MeshComp->GetComponentTransform().Equals(PublishedRoot,.00001));
	TestTrue(TEXT("Every consumer receives the same immutable target packet"),Presentation->GetWeaponGripTargetsForAnimation(1.f/60.f).PrimaryGripWorldTransform.Equals(Published.PrimaryGripWorldTransform,.00001));
	Presentation->AnimationGripTargetsFrame=GFrameCounter-1;Presentation->LastMotionEvaluationFrame=GFrameCounter-1;
	Presentation->GetWeaponGripTargetsForAnimation(1.f/60.f);
	TestFalse(TEXT("The following frame consumes the latest notify intent"),MeshComp->GetComponentTransform().Equals(PublishedRoot,.00001));
	Presentation->EndNotifyIndependentMotion(MotionToken);
	Profile->MotionPresentation.bUseContactHandoff=ContactHandoff;

	// A profile socket edit invalidates both positive and negative lookups.
	UStaticMeshSocket* Replacement = NewObject<UStaticMeshSocket>(StaticMesh);
	Replacement->SocketName = TEXT("WeaponGrip_R_Alternate");
	Replacement->RelativeLocation = FVector(42.0, 0.0, 0.0);
	StaticMesh->AddSocket(Replacement);
	Profile->MotionPresentation.PrimaryGripSocketName = Replacement->SocketName;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Profile socket change refreshes primary grip"), Targets.PrimaryGripWorldTransform.GetLocation().X, 42.0);
	TestEqual(TEXT("VFX socket shares the component cache"), Presentation->GetWeaponVFXAttachmentComponent(Replacement->SocketName), static_cast<USceneComponent*>(MeshComp));

	// 7. Test Weapon Destroy / Teardown
	Presentation->DestroyWeaponPresentation();
	Targets = Presentation->GetWeaponGripTargets();

	TestFalse(TEXT("Teardown: no primary grip"), Targets.bHasPrimaryGrip);
	TestFalse(TEXT("Teardown: no secondary grip"), Targets.bHasSecondaryGrip);
	TestEqual(TEXT("Teardown: Primary Alpha reset to 0.0"), Targets.PrimaryIKAlpha, 0.0f);
	TestEqual(TEXT("Teardown: Secondary Alpha reset to 0.0"), Targets.SecondaryIKAlpha, 0.0f);
	TestTrue(TEXT("Teardown: socket component cache invalidated"), Presentation->CachedSocketComponents.IsEmpty());
	TestTrue(TEXT("Teardown: missing socket cache invalidated"), Presentation->MissingSocketNames.IsEmpty());
	auto* Retarget = NewObject<UProject_JRetargetAnimInstance>(VisualFollower);
	Retarget->NativeInitializeAnimation();
	TestTrue(TEXT("Initialized retarget subscribes to its presentation owner"), Presentation->RegisteredRetargetAnimInstances.Contains(Retarget));
	Retarget->NativePostEvaluateAnimation();
	Retarget->NativeUninitializeAnimation();
	TestFalse(TEXT("Uninitialize releases subscription before garbage collection"), Presentation->RegisteredRetargetAnimInstances.Contains(Retarget));
	TestEqual(TEXT("An uninitialized body cannot report an evaluated grip pose"), Retarget->GetGripPoseEvaluationFrame(), MAX_uint64);
	Retarget->NativeInitializeAnimation();
	TestTrue(TEXT("A retained body can subscribe again"), Presentation->RegisteredRetargetAnimInstances.Contains(Retarget));
	auto* NextOwner = World->SpawnActor<ACharacter>();
	auto* NextPresentation = NewObject<UProject_JWeaponPresentationComponent>(NextOwner);
	NextPresentation->RegisterComponent();
	Retarget->Rename(nullptr, NextOwner->GetMesh(), REN_DontCreateRedirectors | REN_NonTransactional);
	Retarget->NativeUpdateAnimation(1.f / 60.f);
	TestFalse(TEXT("A retained body releases the previous live presentation owner"), Presentation->RegisteredRetargetAnimInstances.Contains(Retarget));
	TestTrue(TEXT("A changed owner establishes the same subscription as initialization"), NextPresentation->RegisteredRetargetAnimInstances.Contains(Retarget));
	Retarget->NativeUninitializeAnimation();
	TestFalse(TEXT("Uninitialize releases the replacement owner"), NextPresentation->RegisteredRetargetAnimInstances.Contains(Retarget));

	GEngine->DestroyWorldContext(World);
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationQualityTierTest, "ProjectJ.Animation.QualityTierPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJAnimationQualityTierTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(Character->GetMesh());

	// 1. Verify default values of UProject_JRetargetAnimInstance
	TestTrue(TEXT("Default: allows Follower Retarget"), RetargetAnim->bEnableFollowerRetarget);
	TestTrue(TEXT("Default: allows Hand IK"), RetargetAnim->bTierAllowsHandIK);
	TestTrue(TEXT("Default: allows Foot IK"), RetargetAnim->bTierAllowsFootIK);
	TestTrue(TEXT("Default: allows Retarget IK"), RetargetAnim->bTierAllowsRetargetIK);
	TestEqual(TEXT("Default: RetargetLODThreshold is -1"), RetargetAnim->RetargetLODThreshold, -1);
	TestEqual(TEXT("Default: Quality Tier is Local"), RetargetAnim->CurrentQualityTier, EProject_JAnimBudgetTier::Local);

	// 2. Verify Policy structures for each tier
	FProject_JAnimOptimizationPolicy PolicyLocal;
	PolicyLocal.Tier = EProject_JAnimBudgetTier::Local;
	PolicyLocal.bEnableHandIK = true;
	PolicyLocal.bEnableFootIK = true;
	PolicyLocal.bEnableRetargetIK = true;
	PolicyLocal.bEnableFollowerRetarget = true;
	PolicyLocal.RetargetIKLODThreshold = 0;
	PolicyLocal.RetargetLODThreshold = -1;
	TestTrue(TEXT("Local policy: Hand IK on"), PolicyLocal.bEnableHandIK);
	TestTrue(TEXT("Local policy: Follower retarget on"), PolicyLocal.bEnableFollowerRetarget);
	TestEqual(TEXT("Local policy: RetargetLODThreshold is -1"), PolicyLocal.RetargetLODThreshold, -1);

	FProject_JAnimOptimizationPolicy PolicyNear;
	PolicyNear.Tier = EProject_JAnimBudgetTier::Near;
	PolicyNear.bEnableHandIK = true;
	PolicyNear.bEnableFootIK = true;
	PolicyNear.bEnableRetargetIK = true;
	PolicyNear.bEnableFollowerRetarget = true;
	PolicyNear.RetargetIKLODThreshold = 1;
	PolicyNear.RetargetLODThreshold = -1;
	TestTrue(TEXT("Near policy: Retarget IK on"), PolicyNear.bEnableRetargetIK);
	TestEqual(TEXT("Near policy: RetargetLODThreshold is -1"), PolicyNear.RetargetLODThreshold, -1);

	FProject_JAnimOptimizationPolicy PolicyMid;
	PolicyMid.Tier = EProject_JAnimBudgetTier::Mid;
	PolicyMid.bEnableHandIK = false;
	PolicyMid.bEnableFootIK = true;
	PolicyMid.bEnableRetargetIK = false;
	PolicyMid.bEnableFollowerRetarget = true;
	PolicyMid.RetargetIKLODThreshold = 0;
	PolicyMid.RetargetLODThreshold = -1;
	TestFalse(TEXT("Mid policy: Hand IK off"), PolicyMid.bEnableHandIK);
	TestFalse(TEXT("Mid policy: Retarget IK off"), PolicyMid.bEnableRetargetIK);
	TestTrue(TEXT("Mid policy: Follower retarget on"), PolicyMid.bEnableFollowerRetarget);
	TestEqual(TEXT("Mid policy: RetargetLODThreshold is -1"), PolicyMid.RetargetLODThreshold, -1);

	FProject_JAnimOptimizationPolicy PolicyFar;
	PolicyFar.Tier = EProject_JAnimBudgetTier::Far;
	PolicyFar.bEnableHandIK = false;
	PolicyFar.bEnableFootIK = false;
	PolicyFar.bEnableRetargetIK = false;
	PolicyFar.bEnableFollowerRetarget = true;
	PolicyFar.RetargetIKLODThreshold = 0;
	PolicyFar.RetargetLODThreshold = 2;
	TestFalse(TEXT("Far policy: Foot IK off"), PolicyFar.bEnableFootIK);
	TestFalse(TEXT("Far policy: Hand IK off"), PolicyFar.bEnableHandIK);
	TestEqual(TEXT("Far policy: RetargetLODThreshold is 2"), PolicyFar.RetargetLODThreshold, 2);

	FProject_JAnimOptimizationPolicy PolicyHidden;
	PolicyHidden.Tier = EProject_JAnimBudgetTier::Hidden;
	PolicyHidden.bEnableHandIK = false;
	PolicyHidden.bEnableFootIK = false;
	PolicyHidden.bEnableRetargetIK = false;
	PolicyHidden.bEnableFollowerRetarget = false;
	PolicyHidden.RetargetIKLODThreshold = 0;
	PolicyHidden.RetargetLODThreshold = 0;
	TestFalse(TEXT("Hidden policy: Follower retarget off"), PolicyHidden.bEnableFollowerRetarget);
	TestFalse(TEXT("Hidden policy: Hand IK off"), PolicyHidden.bEnableHandIK);
	TestEqual(TEXT("Hidden policy: RetargetLODThreshold is 0"), PolicyHidden.RetargetLODThreshold, 0);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHiddenLeaderVisibleFollowerTest, "ProjectJ.Animation.HiddenLeaderVisibleFollower",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHiddenLeaderVisibleFollowerTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AProject_JMoverPawn* Character = World->SpawnActor<AProject_JMoverPawn>(Params);

	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();
	LeaderMesh->SetHiddenInGame(true);

	// Create FollowerMesh as child of LeaderMesh with RetargetAnimInstance
	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character, TEXT("FollowerVisualMesh"));
	FollowerMesh->SetupAttachment(LeaderMesh);
	Character->AddInstanceComponent(FollowerMesh);
	FollowerMesh->RegisterComponent();
	FollowerMesh->SetHiddenInGame(false);

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;

	UProject_JCharacterAnimInstance* LeaderAnim = NewObject<UProject_JCharacterAnimInstance>(LeaderMesh);
	LeaderMesh->AnimScriptInstance = LeaderAnim;
	LeaderAnim->InitializeAnimation();

	// 1. Verify FollowerMesh is properly identified by LeaderAnim
	TestEqual(TEXT("FollowerMesh identified by GetRuntimeRetargetFollowerMesh"),
		LeaderAnim->GetRuntimeRetargetFollowerMesh(), FollowerMesh);

	// 2. Mark FollowerMesh as recently rendered
	const float Now = World->GetTimeSeconds();
	FollowerMesh->SetLastRenderTime(Now);

	// LeaderMesh is bHiddenInGame = true, but FollowerMesh is rendered -> WasOwnerVisualRecentlyRendered must be true!
	TestTrue(TEXT("WasOwnerVisualRecentlyRendered is true when Follower is rendered"),
		LeaderAnim->WasOwnerVisualRecentlyRendered(1.0f));

	FProject_JAnimOptimizationPolicy Policy = LeaderAnim->BuildOptimizationPolicy();
	TestNotEqual(TEXT("Tier is NOT Hidden when Follower is rendered"),
		Policy.Tier, EProject_JAnimBudgetTier::Hidden);
	TestTrue(TEXT("Policy enables Follower Retarget"), Policy.bEnableFollowerRetarget);

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJHiddenToVisibleFollowerRestoreTest, "ProjectJ.Animation.HiddenToVisibleFollowerRestore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJHiddenToVisibleFollowerRestoreTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AProject_JMoverPawn* Character = World->SpawnActor<AProject_JMoverPawn>(Params);

	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();
	LeaderMesh->SetHiddenInGame(true);

	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character, TEXT("FollowerVisualMesh"));
	FollowerMesh->SetupAttachment(LeaderMesh);
	Character->AddInstanceComponent(FollowerMesh);
	FollowerMesh->RegisterComponent();
	FollowerMesh->SetHiddenInGame(false);

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;

	UProject_JCharacterAnimInstance* LeaderAnim = NewObject<UProject_JCharacterAnimInstance>(LeaderMesh);
	LeaderMesh->AnimScriptInstance = LeaderAnim;
	FollowerMesh->SetLastRenderTime(World->GetTimeSeconds());
	LeaderAnim->InitializeAnimation();
	FollowerMesh->SetComponentTickEnabled(true);

	TestTrue(TEXT("Initial: Follower tick enabled"), FollowerMesh->IsComponentTickEnabled());

	// 1. Transition to Hidden: Follower not rendered (far past time)
	FollowerMesh->SetLastRenderTime(-100.0f);

	TestFalse(TEXT("WasOwnerVisualRecentlyRendered is false when off-screen"),
		LeaderAnim->WasOwnerVisualRecentlyRendered(1.0f));

	FProject_JAnimOptimizationPolicy HiddenPolicy = LeaderAnim->BuildOptimizationPolicy();
	TestEqual(TEXT("Tier becomes Hidden"), HiddenPolicy.Tier, EProject_JAnimBudgetTier::Hidden);
	TestFalse(TEXT("HiddenPolicy disables follower retarget"), HiddenPolicy.bEnableFollowerRetarget);

	LeaderAnim->ApplyOptimizationPolicy(HiddenPolicy);
	TestFalse(TEXT("Follower tick disabled after entering Hidden tier"), FollowerMesh->IsComponentTickEnabled());

	// 2. Transition back to Visible: camera looks at character, Follower becomes rendered
	const float Now = World->GetTimeSeconds();
	FollowerMesh->SetLastRenderTime(Now);

	TestTrue(TEXT("WasOwnerVisualRecentlyRendered recovers to true"),
		LeaderAnim->WasOwnerVisualRecentlyRendered(1.0f));

	FProject_JAnimOptimizationPolicy RestoredPolicy = LeaderAnim->BuildOptimizationPolicy();
	TestNotEqual(TEXT("Restored tier is not Hidden"), RestoredPolicy.Tier, EProject_JAnimBudgetTier::Hidden);
	TestTrue(TEXT("Restored policy enables follower retarget"), RestoredPolicy.bEnableFollowerRetarget);

	LeaderAnim->ApplyOptimizationPolicy(RestoredPolicy);
	TestTrue(TEXT("Follower tick restored after exiting Hidden tier"), FollowerMesh->IsComponentTickEnabled());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularMeshIsolationTest, "ProjectJ.Animation.ModularMeshIsolation",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJModularMeshIsolationTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AProject_JMoverPawn* Character = World->SpawnActor<AProject_JMoverPawn>(Params);

	USkeletalMeshComponent* LeaderMesh = Character->GetMesh();

	// 1. Create Follower Mesh
	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character, TEXT("FollowerVisualMesh"));
	FollowerMesh->SetupAttachment(LeaderMesh);
	Character->AddInstanceComponent(FollowerMesh);
	FollowerMesh->RegisterComponent();
	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;

	// 2. Create Modular Mesh (e.g. Armor/Helmet)
	UProject_JModularMeshComponent* ModularArmorMesh = NewObject<UProject_JModularMeshComponent>(Character, TEXT("ModularArmorMesh"));
	ModularArmorMesh->SetupAttachment(LeaderMesh);
	Character->AddInstanceComponent(ModularArmorMesh);
	ModularArmorMesh->RegisterComponent();

	UProject_JCharacterAnimInstance* LeaderAnim = NewObject<UProject_JCharacterAnimInstance>(LeaderMesh);
	LeaderMesh->AnimScriptInstance = LeaderAnim;
	LeaderAnim->InitializeAnimation();

	ModularArmorMesh->PrimaryComponentTick.bCanEverTick = true;
	FollowerMesh->SetComponentTickEnabled(true);
	ModularArmorMesh->SetComponentTickEnabled(true);
	TestTrue(TEXT("Initial: FollowerMesh tick enabled"), FollowerMesh->IsComponentTickEnabled());
	TestTrue(TEXT("Initial: ModularArmorMesh tick enabled"), ModularArmorMesh->IsComponentTickEnabled());

	// 3. Apply Hidden policy (bEnableFollowerRetarget = false)
	FProject_JAnimOptimizationPolicy HiddenPolicy;
	HiddenPolicy.Tier = EProject_JAnimBudgetTier::Hidden;
	HiddenPolicy.bEnableFollowerRetarget = false;
	LeaderAnim->ApplyOptimizationPolicy(HiddenPolicy);

	// FollowerMesh must be disabled, BUT ModularArmorMesh must NOT be disabled!
	TestFalse(TEXT("FollowerMesh tick disabled in Hidden tier"), FollowerMesh->IsComponentTickEnabled());
	TestTrue(TEXT("ModularArmorMesh tick preserved in Hidden tier (isolated)"), ModularArmorMesh->IsComponentTickEnabled());

	// 4. Restore to Visible policy
	FProject_JAnimOptimizationPolicy VisiblePolicy;
	VisiblePolicy.Tier = EProject_JAnimBudgetTier::Near;
	VisiblePolicy.bEnableFollowerRetarget = true;
	LeaderAnim->ApplyOptimizationPolicy(VisiblePolicy);

	TestTrue(TEXT("FollowerMesh tick restored in Visible tier"), FollowerMesh->IsComponentTickEnabled());
	TestTrue(TEXT("ModularArmorMesh tick still enabled"), ModularArmorMesh->IsComponentTickEnabled());

	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSSRHitWindowProductionTransitionTest, "ProjectJ.Combat.SSRHitWindowProductionTransition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJSSRHitWindowProductionTransitionTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>(Params);
	World->SetGameState(GameState);
	auto* GameStateAccess = static_cast<FGameStateTestAccessor*>(GameState);

	UProject_JCombatHitValidationComponent* HitVal = NewObject<UProject_JCombatHitValidationComponent>(Character);
	Character->AddInstanceComponent(HitVal);
	HitVal->RegisterComponent();

	const FGameplayTag AttackTag = UGameplayTagsManager::Get().AddNativeGameplayTag(TEXT("ProjectJ.Tests.SSR.ProductionAttack"));
	const int32 Key = 101;
	const float WorldTime = World->GetTimeSeconds();

	// 1. Begin Attack Node at 10.0s
	GameStateAccess->ServerWorldTimeSecondsDelta = 10.0f - WorldTime;
	HitVal->BeginAttackNode(AttackTag, nullptr, Key);

	// 2. Open HitWindow at 10.0s (records open transition boundary)
	HitVal->SetHitWindowOpen(true);

	// 3. Record authoritative traces within hit window
	GameStateAccess->ServerWorldTimeSecondsDelta = 10.1f - WorldTime;
	HitVal->RecordAuthoritativeTrace(FVector(0, 0, 0), FVector(100, 0, 0));

	GameStateAccess->ServerWorldTimeSecondsDelta = 10.2f - WorldTime;
	HitVal->RecordAuthoritativeTrace(FVector(0, 100, 0), FVector(100, 100, 0));

	// 4. Close HitWindow at 10.3s (records close transition boundary)
	GameStateAccess->ServerWorldTimeSecondsDelta = 10.3f - WorldTime;
	HitVal->SetHitWindowOpen(false);

	// 5. Test SSR verification via FindAuthoritativeTraceAtTime
	FVector OutStart = FVector::ZeroVector;
	FVector OutEnd = FVector::ZeroVector;
	bool bOutHitWindowOpen = false;

	// Sub-frame inside hit window between Trace1 (10.1) and Trace2 (10.2) at 10.15s
	const bool bValidMid = HitVal->FindAuthoritativeTraceAtTime(10.15f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestTrue(TEXT("Production path: Sub-frame inside hit window is valid"), bValidMid);
	TestTrue(TEXT("Production path: HitWindow was open"), bOutHitWindowOpen);
	TestNearlyEqual(TEXT("Production path: Interpolated Y is 50"), OutStart.Y, 50.0, 0.1);

	// Timestamp before hit window opened (9.95s)
	const bool bBeforeOpen = HitVal->FindAuthoritativeTraceAtTime(9.95f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestFalse(TEXT("Production path: Sample before open boundary is rejected"), bBeforeOpen);

	// Timestamp after hit window closed (10.32s within tolerance): trace found, but window is closed
	const bool bAfterClose = HitVal->FindAuthoritativeTraceAtTime(10.32f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestTrue(TEXT("Production path: Trace after close found within tolerance"), bAfterClose);
	TestFalse(TEXT("Production path: HitWindow is closed after close boundary"), bOutHitWindowOpen);

	// Timestamp beyond future tolerance (10.45s) is rejected
	const bool bBeyondFuture = HitVal->FindAuthoritativeTraceAtTime(10.45f, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestFalse(TEXT("Production path: Sample beyond future tolerance is rejected"), bBeyondFuture);

	// Mismatched PredictionKey
	const bool bWrongKey = HitVal->FindAuthoritativeTraceAtTime(10.15f, 9999, AttackTag, OutStart, OutEnd, bOutHitWindowOpen);
	TestFalse(TEXT("Production path: Mismatched key rejected"), bWrongKey);

	// Active attack convenience overload succeeds during active combo node
	TestTrue(TEXT("Production path: Active attack convenience overload succeeds"),
		HitVal->FindAuthoritativeTraceAtTime(10.15f, OutStart, OutEnd));

	// 6. Calling EndAttack() clears active attack context (Option A active attack context)
	HitVal->EndAttack();
	const bool bAfterEndAttack = HitVal->FindAuthoritativeTraceAtTime(10.15f, OutStart, OutEnd);
	TestFalse(TEXT("Production path: Convenience overload after EndAttack is rejected (Option A active attack context)"), bAfterEndAttack);
	TestFalse(TEXT("Production path: Active tag is cleared"), HitVal->GetActiveAttackNodeTag().IsValid());

	FProject_JCombatHitRequest Request;
	Request.PredictionKey = Key;
	Request.AttackNodeTag = AttackTag;
	TestEqual(TEXT("Production path: ValidateActiveAttack rejects with NoActiveAttack after EndAttack"),
		HitVal->ValidateActiveAttack(Request), EProject_JCombatHitValidationFailure::NoActiveAttack);

	// Exercise the actual producer clock, open/close transitions and spatial
	// cutoff at a large absolute time, rather than only constructing records.
	constexpr double LongClock = 1'048'576.0;
	auto* AbsoluteClock = World->SpawnActor<AProject_JSpatialHistoryClock>(Params);
	World->SetGameState(AbsoluteClock);
	const auto SetClock = [&](double Time) { AbsoluteClock->TestServerTime = Time; };
	SetClock(LongClock + .010); HitVal->BeginAttackNode(AttackTag, nullptr, Key); HitVal->SetHitWindowOpen(true);
	HitVal->RecordAuthoritativeTrace(FVector::ZeroVector, FVector(100, 0, 0));
	SetClock(LongClock + .030); HitVal->RecordAuthoritativeTrace(FVector(0, 100, 0), FVector(100, 100, 0));
	SetClock(LongClock + .040); HitVal->SetHitWindowOpen(false);
	TestTrue(TEXT("Large producer clock retains sub-frame sweep history"), HitVal->FindAuthoritativeTraceAtTime(LongClock + .020, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen));
	TestNearlyEqual(TEXT("Large producer clock interpolates the correct geometry"), OutStart.Y, 50.0, .001);
	TestTrue(TEXT("Large producer clock keeps a 35 ms request before the close boundary"), HitVal->FindAuthoritativeTraceAtTime(LongClock + .035, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen) && bOutHitWindowOpen);
	TestTrue(TEXT("Large producer clock finds the exact close boundary"), HitVal->FindAuthoritativeTraceAtTime(LongClock + .040, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen));
	TestFalse(TEXT("Large producer clock closes at the exact authored time"), bOutHitWindowOpen);
	TestTrue(TEXT("Large producer clock preserves the existing future endpoint tolerance"), HitVal->FindAuthoritativeTraceAtTime(LongClock + .089, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen));
	TestFalse(TEXT("Large producer clock rejects beyond the existing endpoint tolerance"), HitVal->FindAuthoritativeTraceAtTime(LongClock + .091, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen));
	SetClock(LongClock + .050); HitVal->ResetSpatialHistory();
	SetClock(LongClock + .060); HitVal->SetHitWindowOpen(true); HitVal->RecordAuthoritativeTrace(FVector(200, 0, 0), FVector(210, 0, 0));
	TestFalse(TEXT("Large clock cutoff rejects geometry from before the discontinuity"), HitVal->FindAuthoritativeTraceAtTime(LongClock + .049, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen));
	TestTrue(TEXT("Large clock cutoff permits the new physical epoch"), HitVal->FindAuthoritativeTraceAtTime(LongClock + .060, Key, AttackTag, OutStart, OutEnd, bOutHitWindowOpen) && bOutHitWindowOpen);
	HitVal->EndAttack();

	World->DestroyWorld(false);
	return true;
}

#include "Animation/Project_JAnimNotifyState_TwoHandIK.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTwoHandIKTransitionAndCurveTest, "ProjectJ.Animation.TwoHandIKTransitionAndCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJTwoHandIKTransitionAndCurveTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACharacter* Character = World->SpawnActor<ACharacter>(Params);

	UProject_JWeaponPresentationComponent* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Character);
	Character->AddInstanceComponent(Presentation);
	Presentation->RegisterComponent();

	// 1. Mock Weapon Actor with Right and Left Grip Sockets
	AActor* MockWeapon = World->SpawnActor<AActor>(Params);
	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(MockWeapon);
	UStaticMeshSocket* SocketR = NewObject<UStaticMeshSocket>(StaticMesh);
	SocketR->SocketName = TEXT("WeaponGrip_R");
	SocketR->RelativeLocation = FVector(10.0, 20.0, 30.0);
	StaticMesh->AddSocket(SocketR);

	UStaticMeshSocket* SocketL = NewObject<UStaticMeshSocket>(StaticMesh);
	SocketL->SocketName = TEXT("WeaponGrip_L");
	SocketL->RelativeLocation = FVector(-10.0, -20.0, 30.0);
	StaticMesh->AddSocket(SocketL);

	UStaticMeshComponent* MeshComp = NewObject<UStaticMeshComponent>(MockWeapon);
	MeshComp->SetStaticMesh(StaticMesh);
	MockWeapon->SetRootComponent(MeshComp);
	MeshComp->RegisterComponent();

	// 2. Setup WeaponPresentationProfile with Greatsword settings (Secondary = 0 in drawn idle)
	UProject_JWeaponPresentationProfile* Profile = NewObject<UProject_JWeaponPresentationProfile>(Character);
	Profile->MotionPresentation.PrimaryGripSocketName = TEXT("WeaponGrip_R");
	Profile->MotionPresentation.SecondaryGripSocketName = TEXT("WeaponGrip_L");
	Profile->MotionPresentation.DefaultDrawnPrimaryIKAlpha = 1.0f;
	Profile->MotionPresentation.DefaultDrawnSecondaryIKAlpha = 0.0f; // Greatsword default: right hand only
	Profile->MotionPresentation.DefaultSheathedPrimaryIKAlpha = 0.0f;
	Profile->MotionPresentation.DefaultSheathedSecondaryIKAlpha = 0.0f;
	auto* SourceAsset = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("Manny attachment fixture"), SourceAsset)) { World->DestroyWorld(false); return false; }
	Character->GetMesh()->SetSkeletalMesh(SourceAsset);
	Profile->SheathedSocketName = SourceAsset->GetRefSkeleton().GetBoneName(0);

	Presentation->AppliedProfile = Profile;
	Presentation->SpawnedWeapon = MockWeapon;
	Presentation->UpdateSocketComponentCache();

	// 3. Setup Follower Mesh and UProject_JRetargetAnimInstance
	USkeletalMeshComponent* FollowerMesh = NewObject<USkeletalMeshComponent>(Character);
	FollowerMesh->SetupAttachment(Character->GetMesh());
	FollowerMesh->RegisterComponent();

	UProject_JRetargetAnimInstance* RetargetAnim = NewObject<UProject_JRetargetAnimInstance>(FollowerMesh);
	FollowerMesh->AnimScriptInstance = RetargetAnim;
	Presentation->RegisterRetargetAnimInstance(RetargetAnim);

	// Test A: Sheathed / Non-combat
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Sheathed;
	Presentation->UpdateGripTargets();
	FProject_JWeaponGripTargets Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Sheathed: Primary IK Alpha is 0.0"), Targets.PrimaryIKAlpha, 0.0f);
	TestEqual(TEXT("Sheathed: Secondary IK Alpha is 0.0"), Targets.SecondaryIKAlpha, 0.0f);

	// Test B: Drawn Combat Stance (Idle/Run outside attack) -> Right only
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Drawn;
	Presentation->UpdateGripTargets();
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Combat Idle: Primary IK Alpha is 1.0"), Targets.PrimaryIKAlpha, 1.0f);
	TestEqual(TEXT("Combat Idle: Secondary IK Alpha is 0.0 (Left hand free)"), Targets.SecondaryIKAlpha, 0.0f);

	// Test C: Begin TwoHandGrip (Attack swing starts) -> Left hand engages
	Presentation->BeginTwoHandGrip(1.0f);
	TestTrue(TEXT("TwoHandGrip is active"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Attack Swing: Secondary IK Alpha is 1.0 (Left hand grips hilt)"), Targets.SecondaryIKAlpha, 1.0f);
	TestEqual(TEXT("Attack Swing: Primary IK Alpha remains 1.0"), Targets.PrimaryIKAlpha, 1.0f);

	// Test D: Combo overlap (Second attack begins before first ends) -> Ref count is 2
	Presentation->BeginTwoHandGrip(1.0f);
	TestTrue(TEXT("TwoHandGrip remains active during combo"), Presentation->IsTwoHandGripActive());
	Presentation->EndTwoHandGrip(); // First attack notify ends
	TestTrue(TEXT("TwoHandGrip still active after first notify ends (ref-count > 0)"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Combo transition: Secondary IK Alpha stays 1.0 without popping"), Targets.SecondaryIKAlpha, 1.0f);

	Presentation->EndTwoHandGrip(); // Second attack notify ends
	TestFalse(TEXT("TwoHandGrip inactive after second notify ends"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Combo finished: Secondary IK Alpha returns to 0.0"), Targets.SecondaryIKAlpha, 0.0f);

	// Test E: UProject_JAnimNotifyState_TwoHandIK triggers presentation methods
	UProject_JAnimNotifyState_TwoHandIK* NotifyState = NewObject<UProject_JAnimNotifyState_TwoHandIK>();
	NotifyState->SecondaryIKAlpha = 0.9f;
	FAnimNotifyEventReference EventRef;
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, EventRef);
	TestTrue(TEXT("NotifyBegin activated TwoHandGrip"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("Notify target alpha applied (0.9)"), Targets.SecondaryIKAlpha, 0.9f);

	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, EventRef);
	TestFalse(TEXT("NotifyEnd deactivated TwoHandGrip"), Presentation->IsTwoHandGripActive());
	Targets = Presentation->GetWeaponGripTargets();
	TestEqual(TEXT("NotifyEnd restored Secondary Alpha to 0.0"), Targets.SecondaryIKAlpha, 0.0f);
	// The shared notify UObject carries authoring data, not per-character state.
	// Engine playback IDs pair each window even when endings arrive out of order.
	FAnimNotifyEventReference OuterRef, InnerRef;
	OuterRef.SetNotifyInstanceID(11001); InnerRef.SetNotifyInstanceID(11002);
	NotifyState->SecondaryIKAlpha = 0.6f;
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, OuterRef);
	NotifyState->SecondaryIKAlpha = 0.9f;
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, InnerRef);
	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, InnerRef);
	TestEqual(TEXT("Ending a nested window restores the surviving outer alpha"), Presentation->GetWeaponGripTargets().SecondaryIKAlpha, 0.6f);
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, InnerRef);
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, InnerRef);
	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, OuterRef);
	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, OuterRef);
	TestEqual(TEXT("Duplicate and out-of-order ends cannot consume the remaining playback"), Presentation->GetWeaponGripTargets().SecondaryIKAlpha, 0.9f);
	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, InnerRef);
	TestFalse(TEXT("Duplicate begin does not leak a second keyed request"), Presentation->IsTwoHandGripActive());
	NotifyState->SecondaryIKAlpha = 0.6f;
	Presentation->BeginTwoHandGrip(0.3f);
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, OuterRef);
	Presentation->EndTwoHandGrip();
	TestEqual(TEXT("A legacy caller cannot consume a keyed animation window"), Presentation->GetWeaponGripTargets().SecondaryIKAlpha, 0.6f);
	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, OuterRef);

	// Test F: ExitCombatPresentation clears TwoHandGrip count
	Presentation->BeginTwoHandGrip(1.0f);
	TestTrue(TEXT("Active before ExitCombat"), Presentation->IsTwoHandGripActive());
	Presentation->ExitCombatPresentation();
	TestFalse(TEXT("ExitCombatPresentation reset TwoHandGrip state count"), Presentation->IsTwoHandGripActive());
	Presentation->CurrentPresentationSocket = EProject_JWeaponPresentationSocket::Drawn;
	NotifyState->NotifyBegin(Character->GetMesh(), nullptr, 1.0f, InnerRef);
	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, OuterRef);
	TestTrue(TEXT("A stale end after reset cannot close a new attack's keyed window"), Presentation->IsTwoHandGripActive());
	NotifyState->NotifyEnd(Character->GetMesh(), nullptr, InnerRef);

	World->DestroyWorld(false);
	return true;
}

namespace
{
class FProjectJSSRExecutionClockLatent : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    UWorld* W = nullptr;
    TStrongObjectPtr<UWorld> KeepWorld;
    AProject_JPlayerMaturityASCFixture *Source = nullptr, *A = nullptr, *B = nullptr;
    AActor* EarlierOwner = nullptr;
    UProject_JCombatHitValidationComponent *Hit = nullptr, *Earlier = nullptr;
    UProject_JServerSideRewindComponent *HistoryA = nullptr, *HistoryB = nullptr;
    TStrongObjectPtr<UProject_JAttackDefinition> Definition;
    const UGameplayEffect* Effect = nullptr;
    FGameplayTag Node;
    static constexpr int32 Key = 167;
    double EarlyStart = 0, TargetTime = 0, ServerStart = 0, PacketTime = 0, Start2 = 0, Time2 = 0, Start3 = 0, Time3 = 0;
    double PreviousA = -1, PreviousB = -1, Started = FPlatformTime::Seconds();
    int32 Visit1 = 0, Visit2 = 0, Visit3 = 0, EffectsA = 0, EffectsB = 0, Sequence = 0, Step = 0;
    uint64 Window1 = 0, Window2 = 0, Window3 = 0, LastFrame = MAX_uint64;
    float Delta = 0, BeforeA = 0, BeforeB = 0;
    bool ExactSource = true;
    FVector CapturedA = FVector::ZeroVector, CapturedB = FVector::ZeroVector;
    TArray<FProject_JAuthoritativeTraceSegment> Pieces;
    FProject_JAuthoritativeSweepRecord Packet;
    FProject_JCombatHitRequest Request, Invalid, Second, Current;
    FDelegateHandle HandleA, HandleB;
    bool TestTrue(const TCHAR* Message, bool Value) { return Test->TestTrue(Message, Value); }
    bool TestFalse(const TCHAR* Message, bool Value) { return Test->TestFalse(Message, Value); }
    template<typename T> bool TestNotNull(const TCHAR* Message, T* Value) { return Test->TestNotNull(Message, Value); }
    template<typename T> bool TestEqual(const TCHAR* Message, const T& Value, const T& Expected) { return Test->TestEqual(Message, Value, Expected); }
    static float Health(const AProject_JPlayerMaturityASCFixture* Target) { return Target->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()); }
    void Send(FProject_JCombatHitRequest Value) { Value.RequestSequence = ++Sequence; Hit->ServerRequestPredictedHit(Value); }
    void BindEffects()
    {
        HandleA = A->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([this](UAbilitySystemComponent* ASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        { if (Spec.Def == Effect) { ++EffectsA; ExactSource &= ASC == Source->CurrentASC.Get(); } });
        HandleB = B->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([this](UAbilitySystemComponent* ASC, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
        { if (Spec.Def == Effect) { ++EffectsB; ExactSource &= ASC == Source->CurrentASC.Get(); } });
    }
    bool ObserveHistory()
    {
        double OldA = 0, NewA = 0, OldB = 0, NewB = 0;
        const bool HasA = HistoryA->TryGetHistoryTimeBounds(OldA, NewA), HasB = HistoryB->TryGetHistoryTimeBounds(OldB, NewB);
        const auto* CapsuleA = A->GetCapsuleComponent(); const auto* CapsuleB = B->GetCapsuleComponent();
        Test->AddInfo(FString::Printf(TEXT("CLOCK_HISTORY167 step=%d frame=%llu now=%.17g A(has=%d,begun=%d,tick=%d,old=%.17g,new=%.17g,advanced=%d,pos=%s,r=%.6f,hh=%.6f) B(has=%d,begun=%d,tick=%d,old=%.17g,new=%.17g,advanced=%d,pos=%s,r=%.6f,hh=%.6f)"),
            Step, uint64(GFrameCounter), W->GetTimeSeconds(), HasA, HistoryA->HasBegunPlay(), HistoryA->IsComponentTickEnabled(), OldA, NewA, HasA && NewA > PreviousA,
            *CapsuleA->GetComponentLocation().ToString(), CapsuleA->GetScaledCapsuleRadius(), CapsuleA->GetScaledCapsuleHalfHeight(),
            HasB, HistoryB->HasBegunPlay(), HistoryB->IsComponentTickEnabled(), OldB, NewB, HasB && NewB > PreviousB,
            *CapsuleB->GetComponentLocation().ToString(), CapsuleB->GetScaledCapsuleRadius(), CapsuleB->GetScaledCapsuleHalfHeight()));
        const bool Ready = HasA && HasB && NewA > PreviousA && NewB > PreviousB && NewA == W->GetTimeSeconds() && NewB == W->GetTimeSeconds();
        PreviousA = NewA; PreviousB = NewB;
        return TestTrue(TEXT("Natural distinct-frame native captures advance both real history bounds"), Ready);
    }
    bool Setup()
    {
        // Native dispatch only: real authority actors and public execution clocks;
        // autonomous input/socket delivery remains the separate wire proof.
        W = UWorld::CreateWorld(EWorldType::Game, false); KeepWorld.Reset(W);
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
        FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        const auto Recipient = [&](const FVector& Position)
        {
            auto* Actor = W->SpawnActor<AProject_JPlayerMaturityASCFixture>(AProject_JPlayerMaturityASCFixture::StaticClass(), Position, FRotator::ZeroRotator, Spawn);
            if (!Actor) return Actor;
            auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Actor); Actor->CurrentASC = ASC;
            Actor->AddInstanceComponent(ASC); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Actor, Actor);
            ASC->AddAttributeSetSubobject(NewObject<UProject_JAttributeSet>(Actor));
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100.f);
            ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100.f);
            return Actor;
        };
        Source = Recipient(FVector(0, 0, 100));
        A = Recipient(FVector(120, 0, 100)); B = Recipient(FVector(120, 150, 100));
        if (!TestNotNull(TEXT("Actual clock source"), Source) || !TestNotNull(TEXT("First history target"), A) || !TestNotNull(TEXT("Independent second target"), B)) return false;
        Hit = NewObject<UProject_JCombatHitValidationComponent>(Source); Source->AddInstanceComponent(Hit); Hit->RegisterComponent();
        EarlierOwner = W->SpawnActor<AActor>(Spawn);
        if (!TestNotNull(TEXT("Independent earlier native clock owner"), EarlierOwner)) return false;
        Earlier = NewObject<UProject_JCombatHitValidationComponent>(EarlierOwner); EarlierOwner->AddInstanceComponent(Earlier); Earlier->RegisterComponent();
        HistoryA = NewObject<UProject_JServerSideRewindComponent>(A); A->AddInstanceComponent(HistoryA); HistoryA->RegisterComponent();
        HistoryB = NewObject<UProject_JServerSideRewindComponent>(B); B->AddInstanceComponent(HistoryB); HistoryB->RegisterComponent();
        Definition.Reset(LoadObject<UProject_JAttackDefinition>(nullptr,
            TEXT("/Game/DataAssetSets/AttackDefinition/Spellblade/DA_Attack_Spellblade_LMB1_Canonical.DA_Attack_Spellblade_LMB1_Canonical")));
        if (!TestTrue(TEXT("Unchanged selected canonical definition"), Definition.IsValid() && Definition->MeleeTrajectory.bEnabled && Definition->DamageEffect && Definition->AttackTag.IsValid())) return false;
        Effect = Definition->DamageEffect->GetDefaultObject<UGameplayEffect>();
        Delta = 0.f;
        for (const auto& M : Effect->Modifiers)
            if (M.Attribute == UProject_JAttributeSet::GetHealthAttribute() && M.ModifierOp == EGameplayModOp::Additive)
            { float Magnitude = 0; if (M.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, Magnitude)) Delta += Magnitude; }
        if (!TestTrue(TEXT("Real instant authored health loss"), Effect->DurationPolicy == EGameplayEffectDurationType::Instant && Delta < 0.f)) return false;
        W->InitializeActorsForPlay(FURL()); W->GetWorldSettings()->NotifyBeginPlay();
        // Post-initialization/possession may restore the native default movement mode.
        // Establish this stationary fixture after that lifecycle, before the first real tick.
        for (auto* Actor : {Source, A, B})
        {
            Actor->GetCharacterMovement()->DisableMovement(); Actor->GetCharacterMovement()->StopMovementImmediately();
        }
        return true;

    }
public:
    explicit FProjectJSSRExecutionClockLatent(FAutomationTestBase* InTest) : Test(InTest) {}
    ~FProjectJSSRExecutionClockLatent() override
    {
        if (IsValid(A) && A->CurrentASC) A->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.Remove(HandleA);
        if (IsValid(B) && B->CurrentASC) B->CurrentASC->OnGameplayEffectAppliedDelegateToSelf.Remove(HandleB);
        if (W) { if (W->GetBegunPlay()) W->EndPlay(EEndPlayReason::Quit); W->DestroyWorld(false); GEngine->DestroyWorldContext(W); }
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 10.) { Test->AddError(TEXT("Natural-frame clock fixture timed out")); return true; }
        if (LastFrame == GFrameCounter) return false; // Read only; never advance or replace the engine frame clock.
        LastFrame = GFrameCounter;
        switch (Step)
        {
        case 0:
        {
            if (!Setup()) return true;
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;

            Node = Definition->AttackTag;
            Earlier->BeginAttackNode(Node, Definition.Get(), Key);
            EarlyStart = Earlier->GetActiveAttackStartTimestamp(); Visit1 = Earlier->GetActiveNodeVisitOrdinal();
            ++Step; return false;
        }
        case 1:
        {
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;
            TargetTime = W->GetTimeSeconds();
            CapturedA = A->GetCapsuleComponent()->GetComponentLocation();
            CapturedB = B->GetCapsuleComponent()->GetComponentLocation();
            ++Step; return false;
        }
        case 2:
        {
            W->Tick(LEVELTICK_All, .10f);
            if (!ObserveHistory()) return true;
            Hit->BeginAttackNode(Node, Definition.Get(), Key);
            ServerStart = Hit->GetActiveAttackStartTimestamp(); Window1 = Hit->BeginHitWindow();
            ++Step; return false;
        }
        case 3:
        {
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;
            PacketTime = W->GetTimeSeconds();
            if (!TestTrue(TEXT("Stationary fixture capsules match their actual target-time samples"), A->GetCapsuleComponent()->GetComponentLocation().Equals(CapturedA) && B->GetCapsuleComponent()->GetComponentLocation().Equals(CapturedB))) return true;
            for (auto* Target : {A, B})
            {
                const FVector P = Target->GetCapsuleComponent()->GetComponentLocation();
                auto& Piece = Pieces.AddDefaulted_GetRef(); Piece.TraceStart = P + FVector(0, -10, 0); Piece.TraceEnd = P + FVector(0, 10, 0);
            }
            if (!TestTrue(TEXT("Actual Begin clocks map the same elapsed interval"), Visit1 == 1 && Hit->GetActiveNodeVisitOrdinal() == Visit1 && EarlyStart < TargetTime && TargetTime < ServerStart && FMath::IsNearlyEqual(ServerStart + TargetTime - EarlyStart, PacketTime, 1.e-8)) ||
                !TestTrue(TEXT("Actual first history overlaps its server piece"), HistoryA->ServerVerifyHit(TargetTime, Pieces[0].TraceStart, Pieces[0].TraceEnd, Definition->HitSpec.TraceRadius)) ||
                !TestTrue(TEXT("Actual second history overlaps its server piece"), HistoryB->ServerVerifyHit(TargetTime, Pieces[1].TraceStart, Pieces[1].TraceEnd, Definition->HitSpec.TraceRadius)) ||
                !TestTrue(TEXT("Authority publishes actual current packet"), Hit->RecordAuthoritativeTracePacket(Pieces, Window1, Hit->GetSpatialRevision()) != 0)) return true;
            TestFalse(TEXT("Legacy world time cannot find a packet before server node Begin"), Hit->FindAuthoritativeTracePacketAtTime(TargetTime, Key, Node, Packet));
            if (!TestTrue(TEXT("Mapped source time selects current open atomic server packet"), Hit->FindAuthoritativeTracePacketAtTime(ServerStart + TargetTime - EarlyStart, Key, Node, Packet) && Packet.bAtomicPath && Packet.bHitWindowOpen && Packet.Segments.Num() == 2)) return true;
            BindEffects();
            BeforeA = Health(A); BeforeB = Health(B);
            Request.Target = A; Request.ClientTimestamp = TargetTime; Request.TraceStart = Pieces[0].TraceStart; Request.TraceEnd = Pieces[0].TraceEnd;
            Request.AttackNodeTag = Node; Request.PredictionKey = Key; Request.NodeClock.bPresent = true;
            Request.NodeClock.ClientNodeStartTimestamp = EarlyStart; Request.NodeClock.NodeVisitOrdinal = Visit1;
            Hit->ServerRequestSSRHit(A, TargetTime, Request.TraceStart, Request.TraceEnd, Node, ++Sequence, Key);
            TestEqual(TEXT("Missing canonical clock cannot fall back to legacy RPC"), EffectsA, 0);
            Invalid = Request; Invalid.NodeClock.ClientNodeStartTimestamp = TargetTime + .01; Send(Invalid);
            Invalid = Request; Invalid.NodeClock.ClientNodeStartTimestamp = std::numeric_limits<double>::quiet_NaN(); Send(Invalid);
            Invalid = Request; Invalid.ClientTimestamp = W->GetTimeSeconds() + 1.; Send(Invalid);
            Invalid = Request; ++Invalid.NodeClock.NodeVisitOrdinal; Send(Invalid);
            TestTrue(TEXT("Wrong/NaN/future contexts change no GE/health or first valid anchor"), EffectsA == 0 && EffectsB == 0 && Health(A) == BeforeA && Health(B) == BeforeB);
            Send(Request);
            TestTrue(TEXT("Mapped source packet and real target history apply the authored GE exactly once"), EffectsA == 1 && EffectsB == 0 && ExactSource && Health(A) == FMath::Max(0.f, BeforeA + Delta));
            Second = Request; Second.Target = B; Second.TraceStart = Pieces[1].TraceStart; Second.TraceEnd = Pieces[1].TraceEnd;
            Invalid = Second; Invalid.NodeClock.ClientNodeStartTimestamp += .01; Invalid.ClientTimestamp += .01; Send(Invalid);
            TestTrue(TEXT("Changed bound anchor rejects on fresh target despite unchanged elapsed/matching packet"), EffectsB == 0 && Health(B) == BeforeB);
            Send(Second);
            TestTrue(TEXT("Original immutable anchor still admits the independent target"), EffectsB == 1 && Health(B) == FMath::Max(0.f, BeforeB + Delta));
            Send(Request); TestEqual(TEXT("Clock mapping preserves per-attack dedup"), EffectsA, 1);
            ++Step; return false;
        }
        case 4:
        {
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;

            Earlier->BeginAttackNode(Node, Definition.Get(), Key); Hit->BeginAttackNode(Node, Definition.Get(), Key);
            Start2 = Earlier->GetActiveAttackStartTimestamp(); Visit2 = Earlier->GetActiveNodeVisitOrdinal();
            Window2 = Hit->BeginHitWindow();
            ++Step; return false;
        }
        case 5:
        {
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;
            Time2 = W->GetTimeSeconds();
            if (!TestTrue(TEXT("Next actual node advances visit on the same activation key"), Visit2 == Visit1 + 1 && Hit->GetActiveNodeVisitOrdinal() == Visit2) ||
                !TestTrue(TEXT("Next visit publishes complete native geometry"), Hit->RecordAuthoritativeTracePacket(Pieces, Window2, Hit->GetSpatialRevision()) != 0) ||
                !TestTrue(TEXT("Old context would otherwise select the new packet"), Hit->FindAuthoritativeTracePacketAtTime(Hit->GetActiveAttackStartTimestamp() + TargetTime - EarlyStart, Key, Node, Packet))) return true;
            Send(Second); TestEqual(TEXT("Old visit cannot bind the same-key replacement node"), EffectsB, 1);
            Current = Request; Current.ClientTimestamp = Time2; Current.NodeClock.ClientNodeStartTimestamp = Start2; Current.NodeClock.NodeVisitOrdinal = Visit2;
            Send(Current);
            TestTrue(TEXT("Fresh visit remains usable after stale-context rejection"), EffectsA == 2 && Health(A) == FMath::Max(0.f, BeforeA + 2.f * Delta));
            Hit->ResetSpatialHistory();
            if (!TestTrue(TEXT("Spatial retirement permits native publication but does not restore clock"), Hit->RecordAuthoritativeTracePacket(Pieces, Window2, Hit->GetSpatialRevision()) != 0)) return true;
            ++Step; return false;
        }
        case 6:
        {
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;

            if (!TestTrue(TEXT("Second actual post-spatial publication is available"), Hit->RecordAuthoritativeTracePacket(Pieces, Window2, Hit->GetSpatialRevision()) != 0)) return true;
            Current.Target = B; Current.TraceStart = Pieces[1].TraceStart; Current.TraceEnd = Pieces[1].TraceEnd;
            if (!TestTrue(TEXT("Stale same-node clock still has native packet and actual historical overlap"),
            Hit->FindAuthoritativeTracePacketAtTime(Hit->GetActiveAttackStartTimestamp() + Current.ClientTimestamp - Current.NodeClock.ClientNodeStartTimestamp, Key, Node, Packet) &&
            HistoryB->ServerVerifyHit(Current.ClientTimestamp, Current.TraceStart, Current.TraceEnd, Definition->HitSpec.TraceRadius))) return true;
            Send(Current); TestEqual(TEXT("Spatially retired same-key clock cannot damage despite fresh packets"), EffectsB, 1);
            Hit->EndAttack(); Earlier->EndAttack();
            TestEqual(TEXT("End exposes no active node visit"), Hit->GetActiveNodeVisitOrdinal(), 0);
            Send(Current); TestEqual(TEXT("Ended request cannot resurrect damage"), EffectsB, 1);
            ++Step; return false;
        }
        case 7:
        {
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;

            Earlier->BeginAttackNode(Node, Definition.Get(), Key); Hit->BeginAttackNode(Node, Definition.Get(), Key);
            Start3 = Earlier->GetActiveAttackStartTimestamp(); Visit3 = Earlier->GetActiveNodeVisitOrdinal();
            Window3 = Hit->BeginHitWindow();
            ++Step; return false;
        }
        case 8:
        {
            W->Tick(LEVELTICK_All, .05f);
            if (!ObserveHistory()) return true;
            Time3 = W->GetTimeSeconds();
            if (!TestTrue(TEXT("Same-key restart after End preserves visit provenance"), Visit3 == Visit2 + 1 && Hit->GetActiveNodeVisitOrdinal() == Visit3) ||
                !TestTrue(TEXT("Fresh restart owns actual geometry"), Hit->RecordAuthoritativeTracePacket(Pieces, Window3, Hit->GetSpatialRevision()) != 0) ||
                !TestTrue(TEXT("Old ended clock would otherwise select new geometry"), Hit->FindAuthoritativeTracePacketAtTime(Hit->GetActiveAttackStartTimestamp() + Current.ClientTimestamp - Current.NodeClock.ClientNodeStartTimestamp, Key, Node, Packet))) return true;
            Send(Current); TestEqual(TEXT("Old ended same-key clock cannot become the new immutable binding"), EffectsB, 1);
            Current.ClientTimestamp = Time3; Current.NodeClock.ClientNodeStartTimestamp = Start3; Current.NodeClock.NodeVisitOrdinal = Visit3;
            Send(Current);
            TestTrue(TEXT("Actual fresh restart recovers without stale-clock damage or poisoned binding"), EffectsA == 2 && EffectsB == 2 && ExactSource && Health(B) == FMath::Max(0.f, BeforeB + 2.f * Delta));
            Hit->EndAttack(); Earlier->EndAttack();
            return true;
        }
        default: return true;
        }
    }
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSSRExecutionClockTest, "ProjectJ.Combat.SSRExecutionClockMapping",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSSRExecutionClockTest::RunTest(const FString&)
{
    ADD_LATENT_AUTOMATION_COMMAND(FProjectJSSRExecutionClockLatent(this));
    return true;
}

#endif
