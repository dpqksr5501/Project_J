#include "Testing/Project_JLocalDungeonFixture.h"

#include "Components/BoxComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJLocalDungeonFixture, Log, All);

namespace
{
const FName LocalDungeonId(TEXT("LocalDungeonTravel"));
const FVector ReturnLocation(360.0, 220.0, 100.0);
const FRotator ReturnRotation(0.0, 90.0, 0.0);

FString Endpoint(int32 Port)
{
	return FString::Printf(TEXT("127.0.0.1:%d"), Port);
}

void ExchangeAdmission(FArchive& Ar, FProject_JDungeonInstanceRecord& Record, FGuid Character)
{
	Ar << Record.InstanceId << Record.ServerEndpoint << Record.ExpiresAtSeconds;
	auto& Anchor = Record.ReturnAnchors.FindOrAdd(Character);
	FString Zone = Ar.IsSaving() ? Anchor.ZoneId.ToString() : FString();
	Ar << Zone << Anchor.ChannelId << Anchor.Location << Anchor.Rotation;
	if (Ar.IsLoading())
	{
		Anchor.ZoneId = FName(*Zone);
		Record.DungeonId = LocalDungeonId;
		Record.AllowedCharacters.Add(Character);
	}
}
}

AProject_JLocalDungeonFixtureController::AProject_JLocalDungeonFixtureController()
{
	MMO = CreateDefaultSubobject<UProject_JMMOClientBridgeComponent>(TEXT("MMO"));
}

void AProject_JLocalDungeonFixtureController::ClientFinishDungeonFixture_Implementation()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	UE_LOG(LogProjectJLocalDungeonFixture, Display, TEXT("DUNGEON_FIXTURE_CLIENT_FINISHED"));
	FPlatformMisc::RequestExit(false);
#endif
}

void AProject_JLocalDungeonFixtureController::ClientDisconnectDungeonFixture_Implementation()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	UE_LOG(LogProjectJLocalDungeonFixture, Display, TEXT("DUNGEON_FIXTURE_CLIENT_DISCONNECTED"));
	FPlatformMisc::RequestExit(false);
#endif
}

AProject_JLocalDungeonFixture::AProject_JLocalDungeonFixture()
{
	DefaultPawnClass = ACharacter::StaticClass();
	PlayerControllerClass = AProject_JLocalDungeonFixtureController::StaticClass();
	bRequireTrustedSessions = true;
}

void AProject_JLocalDungeonFixture::BeginPlay()
{
	Super::BeginPlay();
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	const TCHAR* Cmd = FCommandLine::Get();
	FString FixtureRole, RunId;
	FGuid Run;
	bEnabled = GetNetMode() == NM_DedicatedServer
		&& FParse::Value(Cmd, TEXT("ProjectJLocalDungeonRole="), FixtureRole)
		&& (FixtureRole == TEXT("Source") || FixtureRole == TEXT("Destination") || FixtureRole == TEXT("Sibling"))
		&& FParse::Value(Cmd, TEXT("ProjectJLocalDungeonDirectory="), RunDirectory)
		&& FParse::Value(Cmd, TEXT("ProjectJLocalDungeonRun="), RunId) && FGuid::Parse(RunId, Run) && Run.IsValid()
		&& FParse::Value(Cmd, TEXT("ProjectJLocalDungeonSourcePort="), SourcePort)
		&& FParse::Value(Cmd, TEXT("ProjectJLocalDungeonDestinationPort="), DestinationPort)
		&& SourcePort > 1024 && DestinationPort > 1024 && SourcePort != DestinationPort;
	if (!bEnabled)
	{
		FPlatformMisc::RequestExitWithStatus(false, 2);
		return;
	}
	bDestination = FixtureRole == TEXT("Destination");
	bSibling = FixtureRole == TEXT("Sibling");
	FParse::Value(Cmd, TEXT("ProjectJLocalDungeonSiblingPort="), SiblingPort);
	if (bSibling && (SiblingPort <= 1024 || SiblingPort == SourcePort || SiblingPort == DestinationPort))
	{
		FPlatformMisc::RequestExitWithStatus(false, 2);
		return;
	}
	bTestReconnect = bDestination && FParse::Param(Cmd, TEXT("ProjectJLocalDungeonReconnect"));
	bRequireDungeonTicket = bDestination;
	Account = Run;
	Character = FGuid(Run.A ^ 0x72933245, Run.B, Run.C, Run.D);
	auto* Dungeon = GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	Dungeon->SetLocalFixtureSigningSecret(Run.ToString(EGuidFormats::DigitsWithHyphens));
	if (bSibling) Dungeon->SetLocalFixtureChannel(2);
	GetWorldTimerManager().SetTimer(Watchdog, this, &ThisClass::StopFixture, 90.0f, false);

	auto* Floor = GetWorld()->SpawnActor<AActor>();
	auto* Box = NewObject<UBoxComponent>(Floor);
	Floor->SetRootComponent(Box);
	Floor->AddInstanceComponent(Box);
	Box->SetBoxExtent(FVector(2000.0, 2000.0, 20.0));
	Box->SetCollisionProfileName(TEXT("BlockAll"));
	Box->SetWorldLocation(FVector(0.0, 0.0, -20.0));
	Box->RegisterComponent();
	GetWorld()->SpawnActor<APlayerStart>(FVector(0.0, 0.0, 100.0), FRotator::ZeroRotator);

	const FString AdmissionFile = RunDirectory / TEXT("admission.bin");
	const double Now = Dungeon->GetCurrentUtcEpochSeconds();
	FProject_JDungeonInstanceRecord Record;
	if (bDestination)
	{
		TArray<uint8> Bytes;
		if (!FFileHelper::LoadFileToArray(Bytes, *AdmissionFile) || Bytes.Num() > 4096) return;
		FMemoryReader Reader(Bytes);
		ExchangeAdmission(Reader, Record, Character);
		if (Reader.IsError() || !Reader.AtEnd() || !Dungeon->InstallInstanceAdmission(Record, Now)) return;
		Allocation = Record.InstanceId;
	}
	else if (!bSibling)
	{
		FProject_JReturnAnchor Anchor{FName(*GetWorld()->GetMapName()), 1, ReturnLocation, ReturnRotation};
		Dungeon->RegisterWarmPoolServer(Endpoint(DestinationPort));
		TArray<FProject_JDungeonJoinTicket> Tickets;
		if (!Dungeon->RequestDungeonEntry(EProject_JDungeonEntrySource::WorldPortal, LocalDungeonId, {Character},
			{{Character, Anchor}}, Now, Allocation, Tickets) || Tickets.Num() != 1 || !Dungeon->GetInstanceAdmission(Allocation, Record)) return;
		TArray<uint8> Bytes;
		FMemoryWriter Writer(Bytes);
		ExchangeAdmission(Writer, Record, Character);
		if (!FFileHelper::SaveArrayToFile(Bytes, *AdmissionFile)) return;
	}
	bReady = true;
	UE_LOG(LogProjectJLocalDungeonFixture, Display, TEXT("DUNGEON_FIXTURE_READY %s map=%s"), *FixtureRole, *GetWorld()->GetPackage()->GetName());
#endif
}

void AProject_JLocalDungeonFixture::PreLogin(const FString& Options, const FString& Address,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	if (!bReady || (Address != TEXT("127.0.0.1") && !Address.StartsWith(TEXT("127.0.0.1:"))))
	{
		ErrorMessage = TEXT("LocalDungeonFixtureNotReady");
		return;
	}
	if (bSibling && !UGameplayStatics::HasOption(Options, TEXT("TicketRoute")))
	{
		ErrorMessage = TEXT("SiblingReturnTicketRequired");
		return;
	}
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
}

void AProject_JLocalDungeonFixture::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	auto* Player = Cast<AProject_JLocalDungeonFixtureController>(NewPlayer);
	if (!bReady || !IsValid(Player)) return;
	auto* Dungeon = GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	FProject_JAccountId AccountIdentity;
	AccountIdentity.Value = Account;
	FProject_JCharacterId CharacterIdentity;
	CharacterIdentity.Value = Character;
	ProjectJ::MMO::FServerSessionAttempt Attempt;
	ProjectJ::MMO::FServerSessionLease Lease;
	if (!BeginTrustedSession(Player, AccountIdentity, CharacterIdentity, Attempt)
		|| !CompleteTrustedSession(Attempt, true, Lease)) return;
	HandleStartingNewPlayer(Player);
	if (!IsValid(Player->GetPawn())) return;

	if (bSibling || (!bDestination && bOutbound))
	{
		if (Dungeon->HasActiveDungeonInstance(Character) || FVector::DistSquared2D(Player->GetPawn()->GetActorLocation(), ReturnLocation) > 1.0) return;
		UE_LOG(LogProjectJLocalDungeonFixture, Display, TEXT("DUNGEON_FIXTURE_RETURNED_AT_ANCHOR %s"), bSibling ? TEXT("Sibling") : TEXT("Source"));
		Player->ClientFinishDungeonFixture();
		return;
	}
	const TWeakObjectPtr<AProject_JLocalDungeonFixtureController> WeakPlayer(Player);
	FTimerHandle TravelTimer;
	if (bDestination)
	{
		if (bTestReconnect && !bIssuedReconnect)
		{
			bAwaitingReconnect = true;
			UE_LOG(LogProjectJLocalDungeonFixture, Display, TEXT("DUNGEON_FIXTURE_DESTINATION_JOINED"));
			GetWorldTimerManager().SetTimer(TravelTimer, [WeakPlayer] {
				if (auto* Controller = WeakPlayer.Get()) Controller->ClientDisconnectDungeonFixture();
			}, 1.0f, false);
			return;
		}
		if (!Dungeon->ClaimDungeonClearReward(Allocation, Character)) return;
		FProject_JChannelInfo Channel;
		FProject_JReturnAnchor Origin;
		if (!Dungeon->GetCharacterReturnAnchor(Character, Origin)) return;
		Channel.ZoneId = Origin.ZoneId;
		Channel.ChannelId = 1;
		Channel.ServerEndpoint = Endpoint(SourcePort);
		Channel.LastHeartbeatSeconds = Dungeon->GetCurrentUtcEpochSeconds();
		Channel.bDraining = SiblingPort != 0;
		Dungeon->RegisterChannelHeartbeat(Channel);
		if (SiblingPort != 0)
		{
			Channel.bDraining = false;
			Channel.ChannelId = 2;
			Channel.ServerEndpoint = Endpoint(SiblingPort);
			Dungeon->RegisterChannelHeartbeat(Channel);
		}
		UE_LOG(LogProjectJLocalDungeonFixture, Display, TEXT("%s"),
			bIssuedReconnect ? TEXT("DUNGEON_FIXTURE_DESTINATION_RECONNECTED") : TEXT("DUNGEON_FIXTURE_DESTINATION_JOINED"));
		GetWorldTimerManager().SetTimer(TravelTimer, [WeakPlayer] {
			if (auto* Controller = WeakPlayer.Get()) Controller->MMO->ServerRequestDungeonReturn();
		}, 1.0f, false);
	}
	else
	{
		FProject_JDungeonJoinTicket Ticket;
		if (!Dungeon->TryIssueReservedJoinTicket(Allocation, Character, Dungeon->GetCurrentUtcEpochSeconds(), Ticket)) return;
		bOutbound = true;
		GetWorldTimerManager().SetTimer(TravelTimer, [WeakPlayer, Ticket] {
			if (auto* Controller = WeakPlayer.Get()) Controller->MMO->ClientReceiveDungeonTicket(Ticket, true);
		}, 1.0f, false);
	}
#endif
}

void AProject_JLocalDungeonFixture::Logout(AController* Exiting)
{
	Super::Logout(Exiting);
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (bAwaitingReconnect)
	{
		bAwaitingReconnect = false;
		auto* Dungeon = GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
		FProject_JDungeonJoinTicket Ticket;
		if (Dungeon->TryIssueReconnectTicket(Character, Dungeon->GetCurrentUtcEpochSeconds(), Ticket))
		{
			bIssuedReconnect = FFileHelper::SaveStringToFile(UProject_JMMOClientBridgeComponent::BuildDungeonTravelURL(Ticket),
				*(RunDirectory / TEXT("reconnect.url")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
			if (bIssuedReconnect)
				UE_LOG(LogProjectJLocalDungeonFixture, Display, TEXT("DUNGEON_FIXTURE_RECONNECT_TICKET_READY"));
		}
		return;
	}
	if (bDestination || bSibling || (bOutbound && (SiblingPort != 0
		|| !GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>()->HasActiveDungeonInstance(Character))))
		FPlatformMisc::RequestExit(false);
#endif
}

void AProject_JLocalDungeonFixture::StopFixture()
{
	UE_LOG(LogProjectJLocalDungeonFixture, Error, TEXT("DUNGEON_FIXTURE_TIMED_OUT"));
	FPlatformMisc::RequestExitWithStatus(false, 3);
}
