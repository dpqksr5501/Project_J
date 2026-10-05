#include "Testing/Project_JNetworkLoadFixture.h"
#include "Game/Project_JPlayerState.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Components/SceneComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "Engine/NetDriver.h"
#include "Engine/NetConnection.h"
#include "Engine/Engine.h"
#include "HAL/IConsoleManager.h"
#include "EngineUtils.h"
#include "TimerManager.h"
#include "Net/UnrealNetwork.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformMisc.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"
#include "ProfilingDebugging/MiscTrace.h"
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "UObject/UnrealType.h"
#include "Components/BoxComponent.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJDNetwork, Log, All);
namespace
{
bool Enabled()
{
#if UE_BUILD_SHIPPING
	return false;
#else
	return FParse::Param(FCommandLine::Get(), TEXT("ProjectJNetworkFixture"));
#endif
}
int32 FixtureClients() { int32 N = 2; FParse::Value(FCommandLine::Get(), TEXT("ProjectJFixtureClients="), N); return FMath::Clamp(N, 2, 8); }
bool AnimationEnabled() { return Enabled() && FParse::Param(FCommandLine::Get(), TEXT("ProjectJAnimationFixture")); }
void EnsureAnimationFloor(UWorld* World)
{
	if (!AnimationEnabled() || !World || !World->IsGameWorld()) { return; }
	for (TActorIterator<AActor> It(World); It; ++It) { if (It->ActorHasTag(TEXT("ProjectJAnimationFixtureFloor"))) { return; } }
	// Entry has no ground. A real collision floor on each process prevents the
	// autonomous pawn from legitimately generating FallOff RPCs during setup.
	FActorSpawnParameters Params; Params.ObjectFlags |= RF_Transient;
	auto* Floor = World->SpawnActor<AActor>(Params);
	if (!Floor) { return; }
	Floor->Tags.Add(TEXT("ProjectJAnimationFixtureFloor"));
	auto* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box); Floor->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(50000, 50000, 100));
	Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Box->SetCollisionObjectType(ECC_WorldStatic); Box->SetCollisionResponseToAllChannels(ECR_Block);
	Box->RegisterComponent(); Floor->SetActorLocation(FVector(0, 0, -100));
}
FProject_JReplicatedAnimEventState ReadAnimationEvents(AProject_JPlayerCharacter* Player)
{
	auto* Events = Player ? Player->FindComponentByClass<UProject_JReplicatedAnimEventComponent>() : nullptr;
	auto* Property = Events ? FindFProperty<FStructProperty>(Events->GetClass(), TEXT("ReplicatedAnimEvents")) : nullptr;
	return Property ? *Property->ContainerPtrToValuePtr<FProject_JReplicatedAnimEventState>(Events) : FProject_JReplicatedAnimEventState();
}
int32 FixtureNPCs() { int32 N = 100; FParse::Value(FCommandLine::Get(), TEXT("ProjectJFixtureNPCs="), N); return FMath::Clamp(N / 2 * 2, 100, 2048); }
FString OutputRoot()
{
	FString Root; FParse::Value(FCommandLine::Get(), TEXT("ProjectJDOutput="), Root);
	return Root.IsEmpty() ? FPaths::ProjectSavedDir() / TEXT("Validation/GroupD_20260910/Network") : Root;
}
void WriteResult(const TCHAR* Name, bool bSuccess, const FString& Reason, int32 Slot = INDEX_NONE)
{
	auto Object = MakeShared<FJsonObject>(); Object->SetBoolField(TEXT("success"), bSuccess);
	Object->SetStringField(TEXT("reason"), Reason); Object->SetNumberField(TEXT("slot"), Slot);
	FString Json; FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Json));
	IFileManager::Get().MakeDirectory(*OutputRoot(), true);
	FFileHelper::SaveStringToFile(Json, *(OutputRoot() / Name));
}
}

void UProject_JNetworkFixtureLifetime::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	if (!Enabled()) { return; }
	if (AnimationEnabled())
	{
		FloorWorldInit = FWorldDelegates::OnPostWorldInitialization.AddLambda([](UWorld* World, const UWorld::InitializationValues)
		{ EnsureAnimationFloor(World); });
		EnsureAnimationFloor(GetWorld());
		// Run after net dispatch and before actor/component ticks: the first
		// autonomous CMC tick must also use the controlled fixture kinematics.
		AnimationKinematicsTick = FWorldDelegates::OnWorldPreActorTick.AddLambda([](UWorld* World, ELevelTick, float)
		{
			if (!World || !World->IsGameWorld()) { return; }
			for (TActorIterator<AProject_JPlayerCharacter> It(World); It; ++It)
			{
				auto* Movement = It->GetCharacterMovement();
				Movement->SetComponentTickEnabled(false);
				Movement->SetMovementMode(MOVE_Walking);
			}
		});
	}
	const double Started = FPlatformTime::Seconds();
	Timeout = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Started](float)
	{
		if (FPlatformTime::Seconds() - Started < 180) { return true; }
		WriteResult(TEXT("timeout.json"), false, TEXT("Bounded fixture lifetime expired, including connection failure"));
		FPlatformMisc::RequestExit(false); return false;
	}));
}
void UProject_JNetworkFixtureLifetime::Deinitialize()
{
	FWorldDelegates::OnPostWorldInitialization.Remove(FloorWorldInit);
	FWorldDelegates::OnWorldPreActorTick.Remove(AnimationKinematicsTick);
	FTSTicker::GetCoreTicker().RemoveTicker(Timeout);
	Super::Deinitialize();
}

AProject_JNetworkViewPawn::AProject_JNetworkViewPawn()
{
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("ViewRoot")));
	bReplicates = true; bOnlyRelevantToOwner = true; SetReplicateMovement(true);
}
AProject_JNetworkProbeNPC::AProject_JNetworkProbeNPC()
{ bReplicates = true; bAlwaysRelevant = false; PrimaryActorTick.bCanEverTick = false; }
AProject_JAlwaysRelevantProbeNPC::AProject_JAlwaysRelevantProbeNPC() { bAlwaysRelevant = true; }
void AProject_JNetworkProbeNPC::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{ Super::GetLifetimeReplicatedProps(OutLifetimeProps); DOREPLIFETIME(AProject_JNetworkProbeNPC, ProbeId); DOREPLIFETIME(AProject_JNetworkProbeNPC, Pulse); }

void AProject_JNetworkLoadController::ClientConfigure_Implementation(int32 InStage, int32 Slot, const TArray<int32>& Expected, FGuid OwnItem, bool bExpectItems, int32 MinimumPulse)
{
	if (!Enabled()) { return; }
	CurrentStage = InStage; LocalSlot = Slot; ExpectedIds = Expected; ItemId = OwnItem;
	bItemsExpected = bExpectItems; RequiredPulse = MinimumPulse; NextObservation = 0;
}
void AProject_JNetworkLoadController::PlayerTick(float DeltaSeconds)
{
	Super::PlayerTick(DeltaSeconds);
	if (!Enabled() || HasAuthority() || CurrentStage == INDEX_NONE || FPlatformTime::Seconds() < NextObservation) { return; }
	NextObservation = FPlatformTime::Seconds() + 0.25;
	TArray<int32> Seen; int32 MinPulse = MAX_int32;
	for (TActorIterator<AProject_JNetworkProbeNPC> It(GetWorld()); It; ++It)
	{ if (It->ProbeId > 0) { Seen.Add(It->ProbeId); MinPulse = FMath::Min(MinPulse, It->Pulse); } }
	Seen.Sort();
	bool bOwn = false, bForeignEmpty = true, bEquipment = true; int32 States = 0;
	for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
	{
		if (!It->GetPublicClassId().ToString().StartsWith(TEXT("DClient"))) { continue; }
		++States;
		const bool bMine = *It == PlayerState;
		const FString Inventory = It->GetInventoryComponent()->GetReplicationDiagnosticSummary();
		if (bMine) { bOwn = It->GetInventoryComponent()->HasItemInstance(ItemId) == bItemsExpected; }
		else { bForeignEmpty &= Inventory.Contains(TEXT("Items=0")); }
		bEquipment &= It->GetEquipmentManagerComponent()->GetAllEquippedItems().Num() == (bItemsExpected ? 1 : 0);
	}
	ServerObserve(CurrentStage, Seen, bOwn, bForeignEmpty && States == FixtureClients(), bEquipment && States == FixtureClients(), MinPulse, ObserveAnimation());
}
void AProject_JNetworkLoadController::ClientAnimationExpect_Implementation(int32 InStage, const TArray<AProject_JPlayerCharacter*>& Players, const TArray<FProject_JReplicatedAnimEventState>& States)
{
	if (!AnimationEnabled()) { return; }
	AnimationStage = InStage; AnimationPlayers.Reset(); AnimationStates = States;
	for (auto* ObservedCharacter : Players) { AnimationPlayers.Add(ObservedCharacter); }
}
bool AProject_JNetworkLoadController::ObserveAnimation()
{
	if (!AnimationEnabled()) { return true; }
	if (AnimationStage != CurrentStage || AnimationPlayers.Num() != FixtureClients() || AnimationStates.Num() != AnimationPlayers.Num())
	{
		AnimationRows += FString::Printf(TEXT("%d,NotReady_ExpectedStage%d_Players%d_States%d\n"), CurrentStage, AnimationStage, AnimationPlayers.Num(), AnimationStates.Num()); return false;
	}
	int32 RemoteCount = 0; bool bReady = !bAnimationViolation;
	for (int32 I = 0; I < AnimationPlayers.Num(); ++I)
	{
		auto* ObservedCharacter = AnimationPlayers[I].Get();
		if (!ObservedCharacter)
		{
			// The reliable controller RPC can arrive before a separate actor channel
			// resolves its reference. Re-resolve by stable PlayerState identity instead
			// of retaining that initial null as a permanent failed observation.
			const FName ClassId(*FString::Printf(TEXT("DClient%d"), I));
			for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
			{
				const auto* PS = It->GetPlayerState<AProject_JPlayerState>();
				if (PS && PS->GetPublicClassId() == ClassId) { ObservedCharacter = *It; AnimationPlayers[I] = *It; break; }
			}
		}
		if (!ObservedCharacter) { AnimationRows += FString::Printf(TEXT("%d,NotReady_NullActor%d\n"), CurrentStage, I); bReady = false; continue; }
		// Only this opt-in fixture supplies kinematics/visibility. No asset is saved.
		ObservedCharacter->GetCharacterMovement()->SetComponentTickEnabled(false);
		TInlineComponentArray<USkeletalMeshComponent*> Visuals(ObservedCharacter);
		for (auto* Mesh : Visuals) { Mesh->SetLastRenderTime(CurrentStage == 4 ? -1000.0f : GetWorld()->GetTimeSeconds()); }
		if (ObservedCharacter->IsLocallyControlled()) { continue; } // SkipOwner is a production contract.
		++RemoteCount;
		auto* Anim = Cast<UProject_JCharacterAnimInstance>(ObservedCharacter->GetMesh()->GetAnimInstance());
		auto* Locomotion = ObservedCharacter->GetLocomotionAnimStateComponent();
		if (!Anim || !Locomotion || ObservedCharacter->GetLocalRole() != ROLE_SimulatedProxy)
		{
			AnimationRows += FString::Printf(TEXT("%d,NotReady_%s_Role%d_Anim%s_Class%s_State%s\n"), CurrentStage, *ObservedCharacter->GetName(), int32(ObservedCharacter->GetLocalRole()),
				*GetNameSafe(ObservedCharacter->GetMesh()->GetAnimInstance()), *GetNameSafe(ObservedCharacter->GetMesh()->GetAnimClass()), *GetNameSafe(Locomotion)); bReady = false; continue;
		}
		const auto State = ReadAnimationEvents(ObservedCharacter); const auto& Expected = AnimationStates[I];
		bReady &= State.MoveSequence == Expected.MoveSequence && State.LandingRevision == Expected.LandingRevision
			&& State.FallOffStartCounter == Expected.FallOffStartCounter && State.bLandingActive == Expected.bLandingActive;
		const bool bHasPose = Anim->GetThreadSafeStateControllerHasSelectedAnimation() || !Anim->GetThreadSafeMotionMatchingSelectedAnimation().IsNone();
		if (bHasPose) { ObservedPose.Add(ObservedCharacter); }
		if (CurrentStage == 3 && State.LandingRevision == Expected.LandingRevision && Locomotion->bIsLanding
			&& Anim->GetThreadSafeStateControllerHasSelectedAnimation() && Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching()
			&& Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TransitionToLand)
		{ ObservedLandingPose.Add(ObservedCharacter); }
		if (CurrentStage == 3) { bReady &= ObservedLandingPose.Contains(ObservedCharacter); }
		// Reliable RPC + unreliable multicast + replicated fallback must not manufacture
		// multiple physical landing epochs for one authoritative landing identity.
		if (State.LandingRevision == Expected.LandingRevision && Locomotion->LandingPresentationRevision > Expected.LandingSequence)
		{ bAnimationViolation = true; bReady = false; }
		if (CurrentStage >= 4) { bReady &= !Locomotion->bIsLanding && ObservedPose.Contains(ObservedCharacter); }
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		bReady &= FMath::IsFinite(MM.PostSelection.SelectedAnimationTime);
		AnimationRows += FString::Printf(TEXT("%d,%s,%d,%d,%d,%d,%d,%d,%s,%s,ExpectedMove%d_Land%d_Fall%d_ActualFall%d_Ready%d\n"), CurrentStage, *ObservedCharacter->GetName(), int32(ObservedCharacter->GetLocalRole()),
			State.MoveSequence, State.LandingSequence, State.LandingRevision, Locomotion->LandingPresentationRevision,
			Anim->GetThreadSafeStateControllerSelectionRevision(), *GetNameSafe(Anim->GetThreadSafeStateControllerSelectedAnimation()), *Anim->GetThreadSafeMotionMatchingSelectedAnimation().ToString(),
			Expected.MoveSequence, Expected.LandingRevision, Expected.FallOffStartCounter, State.FallOffStartCounter, bReady);
	}
	return bReady && RemoteCount == FixtureClients() - 1;
}
void AProject_JNetworkLoadController::ServerObserve_Implementation(int32 InStage, const TArray<int32>& Seen, bool bOwnInventory, bool bForeignInventoryEmpty, bool bEquipment, int32 MinPulse, bool bAnimation)
{
	if (!Enabled() || Seen.Num() > FixtureNPCs()) { return; }
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JNetworkLoadGameMode>())
	{
		LastObservation = FString::Printf(TEXT("stage=%d seen=%d own=%d foreignEmpty=%d equipment=%d pulse=%d animation=%d"), InStage, Seen.Num(), bOwnInventory, bForeignInventoryEmpty, bEquipment, MinPulse, bAnimation);
		if (Mode->ValidateObservation(this, InStage, Seen, bOwnInventory, bForeignInventoryEmpty, bEquipment, MinPulse, bAnimation)) { ConfirmedStage = InStage; }
	}
}
void AProject_JNetworkLoadController::ClientFinish_Implementation(bool bSuccess)
{
	if (!Enabled()) { return; }
	WriteResult(TEXT("client.json"), bSuccess, TEXT("Server completed connection/AOI/FastArray assertions"), LocalSlot);
	if (AnimationEnabled()) { FFileHelper::SaveStringToFile(AnimationRows, *(OutputRoot() / TEXT("animation-observations.csv"))); }
	FPlatformMisc::RequestExit(false);
}

AProject_JNetworkLoadGameMode::AProject_JNetworkLoadGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	PlayerControllerClass = AProject_JNetworkLoadController::StaticClass();
	DefaultPawnClass = AProject_JNetworkViewPawn::StaticClass();
}
void AProject_JNetworkLoadGameMode::BeginPlay()
{
	Super::BeginPlay(); bFixture = Enabled(); Started = StageStarted = FPlatformTime::Seconds();
	if (!bFixture) { SetActorTickEnabled(false); return; }
	bAllRelevant = FParse::Param(FCommandLine::Get(), TEXT("ProjectJDAllRelevant"));
	auto* Driver = GetWorld()->GetNetDriver();
	if (!Driver || !Driver->GetReplicationSystem()) { Finish(false, TEXT("Iris replication system missing")); return; }
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	for (int32 I = 0; I < FixtureNPCs(); ++I)
	{
		UClass* Class = bAllRelevant ? AProject_JAlwaysRelevantProbeNPC::StaticClass() : AProject_JNetworkProbeNPC::StaticClass();
		auto* NPC = GetWorld()->SpawnActor<AProject_JNetworkProbeNPC>(Class, FVector((I < FixtureNPCs() / 2 ? 0 : 20000) + (I % 32) * 20, ((I % (FixtureNPCs() / 2)) / 32) * 20, 100), FRotator::ZeroRotator, Params);
		NPC->ProbeId = I + 1; NPC->GetCharacterMovement()->SetComponentTickEnabled(false);
		NPC->SetActorEnableCollision(false); NPCs.Add(NPC);
	}
	UE_LOG(LogProjectJDNetwork, Display, TEXT("READY Iris=1 AllRelevant=%d NPCs=%d Clients=%d"), bAllRelevant, FixtureNPCs(), FixtureClients());
	const auto* IrisConfig = GEngine->GetIrisNetDriverConfig(NAME_GameNetDriver, NAME_GameNetDriver);
	const auto* ParallelCVar = IConsoleManager::Get().FindConsoleVariable(TEXT("net.iris.AllowParallelNetTick"));
	UE_LOG(LogProjectJDNetwork, Display, TEXT("ParallelSettings DriverAllowed=%d TasksAllowed=%d MinimumConnections=%d CVar=%d"),
		IrisConfig && IrisConfig->bCanUseParallelNetConnectionTick, Driver->ReplicationSystemConfigServer.bAllowParallelTasks,
		Driver->ReplicationSystemConfigServer.MinConnectionsForParallelTick, ParallelCVar ? ParallelCVar->GetInt() : -1);
}
void AProject_JNetworkLoadGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	if (!Enabled()) { return; }
	auto* PC = CastChecked<AProject_JNetworkLoadController>(NewPlayer);
	if (Clients.Num() >= FixtureClients()) { Finish(false, TEXT("Unexpected extra connection")); return; }
	PC->FixtureSlot = Clients.Num(); Clients.Add(PC);
	auto* PS = PC->GetPlayerState<AProject_JPlayerState>();
	PS->SetPublicCharacterSnapshot(FName(*FString::Printf(TEXT("DClient%d"), PC->FixtureSlot)), 1);
	auto* Definition = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
	if (!Definition) { Finish(false, TEXT("Read-only equipment fixture asset unavailable")); return; }
	const auto Item = PS->GetInventoryComponent()->AddItemDefinition(Definition);
	Items.Add(Item.InstanceId);
	PS->GetEquipmentManagerComponent()->TryEquipItemInstanceById(Item.InstanceId);
}
TArray<int32> AProject_JNetworkLoadGameMode::ExpectedIds(int32 Slot) const
{
	TArray<int32> Result;
	const int32 Cluster = Stage == 2 ? 1 - Slot % 2 : Slot % 2;
	for (int32 Id = 1; Id <= FixtureNPCs(); ++Id)
	{
		if (Stage >= 4 && Id <= 10) { continue; }
		if (bAllRelevant || (Id <= FixtureNPCs() / 2 ? 0 : 1) == Cluster) { Result.Add(Id); }
	}
	return Result;
}
void AProject_JNetworkLoadGameMode::StartStage(int32 NewStage)
{
	if (RegionId) { TRACE_END_REGION_WITH_ID(RegionId); }
	const FString Region = FString::Printf(TEXT("ProjectJ.Network.Stage%d"), NewStage);
	RegionId = TRACE_BEGIN_REGION_WITH_ID(*Region);
	Stage = NewStage; StageStarted = FPlatformTime::Seconds(); RequiredPulse = Pulse + 1;
	auto* Driver = GetWorld()->GetNetDriver(); StageOutBytes = Driver ? Driver->OutTotalBytes : 0;
	if (Stage == 4) { for (int32 I = 0; I < 10; ++I) { NPCs[I]->Destroy(); NPCs[I] = nullptr; } }
	if (Stage == 5)
	{
		for (int32 I = 0; I < Clients.Num(); ++I)
		{
			auto* PS = Clients[I]->GetPlayerState<AProject_JPlayerState>();
			for (auto* Item : PS->GetEquipmentManagerComponent()->GetAllEquippedItems()) { PS->GetEquipmentManagerComponent()->UnequipItem(Item); }
			PS->GetInventoryComponent()->RemoveItemInstance(Items[I]);
		}
	}
	for (AProject_JNetworkLoadController* PC : Clients)
	{
		const int32 Slot = PC->FixtureSlot;
		if (APawn* Pawn = PC->GetPawn())
		{
			Pawn->SetActorLocation(FVector((Stage == 2 ? 1 - Slot % 2 : Slot % 2) * 20000, 0, 100)); Pawn->ForceNetUpdate();
			PC->SetViewTarget(Pawn);
		}
		PC->ConfirmedStage = INDEX_NONE;
		PC->ClientConfigure(Stage, Slot, ExpectedIds(Slot), Items[Slot], Stage < 5, RequiredPulse);
	}
	if (AnimationEnabled())
	{
		TArray<AProject_JPlayerCharacter*> Players; TArray<FProject_JReplicatedAnimEventState> States;
		for (AProject_JNetworkLoadController* PC : Clients)
		{
			auto* Player = CastChecked<AProject_JPlayerCharacter>(PC->GetPawn());
			auto* Events = Player->FindComponentByClass<UProject_JReplicatedAnimEventComponent>();
			if (Stage == 1) { Player->GetCharacterMovement()->Velocity = FVector(200, 0, 0); Events->DispatchMoveStarted(false); }
			if (Stage == 2) { Player->GetCharacterMovement()->Velocity = FVector::ZeroVector; Events->DispatchMoveStopped(false); }
			if (Stage == 3) { Events->DispatchLandingStarted(650, false, false, false); }
			if (Stage == 4) { Events->DispatchLandingCancelled(); }
			if (Stage == 5) { Events->DispatchMoveStarted(false); Events->DispatchLandingStarted(650, false, false, false); Events->DispatchLandingCancelled(); Events->DispatchMoveStopped(false); }
			Player->ForceNetUpdate(); Players.Add(Player); States.Add(ReadAnimationEvents(Player));
		}
		for (AProject_JNetworkLoadController* PC : Clients) { PC->ClientAnimationExpect(Stage, Players, States); }
	}
	TRACE_BOOKMARK(TEXT("ProjectJ.Network Stage=%d AllRelevant=%d Connections=%d"), Stage, bAllRelevant, Clients.Num());
}
bool AProject_JNetworkLoadGameMode::ValidateObservation(AProject_JNetworkLoadController* PC, int32 InStage, const TArray<int32>& Seen, bool bOwn, bool bForeignEmpty, bool bEquipment, int32 MinPulse, bool bAnimation)
{
	return bFixture && !bFinished && Clients.Contains(PC) && InStage == Stage && Stage > 0
		&& Seen == ExpectedIds(PC->FixtureSlot) && bOwn && bForeignEmpty && bEquipment && MinPulse >= RequiredPulse && bAnimation;
}
void AProject_JNetworkLoadGameMode::Logout(AController* Exiting)
{
	if (Stage == 6 && Cast<AProject_JNetworkLoadController>(Exiting) && Cast<AProject_JNetworkLoadController>(Exiting)->FixtureSlot == 1) { bSawDisconnect = true; }
	Super::Logout(Exiting);
}
void AProject_JNetworkLoadGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds); if (!bFixture || bFinished) { return; }
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > 120 || (Stage > 0 && Now - StageStarted > 25))
	{
		FString Details; for (AProject_JNetworkLoadController* PC : Clients) { if (IsValid(PC)) { Details += PC->LastObservation + TEXT("; "); } }
		Finish(false, FString::Printf(TEXT("Timeout stage=%d %s"), Stage, *Details)); return;
	}
	if (Now >= NextPulse)
	{
		NextPulse = Now + 0.1; ++Pulse;
		for (AProject_JNetworkProbeNPC* NPC : NPCs) { if (IsValid(NPC)) { NPC->Pulse = Pulse; } }
	}
	if (Stage == 0 && Clients.Num() == FixtureClients() && !Clients.ContainsByPredicate([](const auto& PC) { return !IsValid(PC) || !PC->GetPawn(); }))
	{
		if (AnimationEnabled())
		{
			for (AProject_JNetworkLoadController* PC : Clients)
			{
				APawn* OldPawn = PC->GetPawn(); auto* PS = PC->GetPlayerState<AProject_JPlayerState>();
				auto* Player = ProjectJAuthoredAnimationFixture::Spawn(GetWorld(), PC, PS, OldPawn->GetActorLocation());
				if (!Player) { Finish(false, TEXT("Authored animation fixture spawn failed")); return; }
				OldPawn->Destroy();
				PS->SetPublicCharacterSnapshot(FName(*FString::Printf(TEXT("DClient%d"), PC->FixtureSlot)), 1);
			}
		}
		StartStage(1); return;
	}
	if (Stage >= 1 && Stage <= 5 && Now - StageStarted >= 3
		&& !Clients.ContainsByPredicate([this](const auto& PC) { return !IsValid(PC) || PC->ConfirmedStage != Stage; }))
	{
		auto* Driver = GetWorld()->GetNetDriver();
		Rows += FString::Printf(TEXT("%d,%.3f,%d,%u\n"), Stage, Now - StageStarted, Driver->ClientConnections.Num(), Driver->OutTotalBytes - StageOutBytes);
		UE_LOG(LogProjectJDNetwork, Display, TEXT("PASS stage=%d connections=%d bytes=%u"), Stage, Driver->ClientConnections.Num(), Driver->OutTotalBytes - StageOutBytes);
		if (Stage < 5) { StartStage(Stage + 1); }
		else { TRACE_END_REGION_WITH_ID(RegionId); RegionId = 0; Stage = 6; StageStarted = Now; Clients[1]->ClientFinish(true); }
	}
	if (Stage == 6 && bSawDisconnect && GetWorld()->GetNetDriver()->ClientConnections.Num() == FixtureClients() - 1) { Finish(true, TEXT("Five AOI/FastArray stages and graceful disconnect passed")); }
}
void AProject_JNetworkLoadGameMode::Finish(bool bSuccess, const FString& Reason)
{
	if (bFinished) { return; } bFinished = true;
	if (RegionId) { TRACE_END_REGION_WITH_ID(RegionId); RegionId = 0; }
	WriteResult(TEXT("server.json"), bSuccess, Reason);
	FFileHelper::SaveStringToFile(Rows, *(OutputRoot() / TEXT("network-phases.csv")));
	UE_LOG(LogProjectJDNetwork, Display, TEXT("FINISH success=%d %s"), bSuccess, *Reason);
	for (AProject_JNetworkLoadController* PC : Clients) { if (IsValid(PC) && PC->GetNetConnection()) { PC->ClientFinish(bSuccess); } }
	// Allow the reliable completion RPC to flush; all processes exit through the normal engine path.
	FTimerHandle Handle; GetWorldTimerManager().SetTimer(Handle, [] { FPlatformMisc::RequestExit(false); }, 1.0f, false);
}
