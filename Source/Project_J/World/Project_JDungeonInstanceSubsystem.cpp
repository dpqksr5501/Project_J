#include "World/Project_JDungeonInstanceSubsystem.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "Dom/JsonObject.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Engine/World.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JQuestComponent.h"
#include "GameFramework/PlayerController.h"
#include "Gameplay/Project_JLootSubsystem.h"
#include "HAL/PlatformMisc.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/SecureHash.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#if WITH_EDITOR
EDataValidationResult UProject_JDungeonDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Base = Super::IsDataValid(Context);
	bool bValid = (Base != EDataValidationResult::Invalid);
	if (DungeonId.IsNone())
	{
		Context.AddError(NSLOCTEXT("ProjectJDungeon", "MissingId", "UProject_JDungeonDefinition requires a non-empty DungeonId."));
		bValid = false;
	}
	if (DungeonMap.IsNull())
	{
		Context.AddError(NSLOCTEXT("ProjectJDungeon", "MissingMap", "UProject_JDungeonDefinition requires a valid DungeonMap soft world reference."));
		bValid = false;
	}
	if (MinPartySize < 1 || MaxPartySize < MinPartySize)
	{
		Context.AddError(NSLOCTEXT("ProjectJDungeon", "InvalidPartyRange", "UProject_JDungeonDefinition requires 1 <= MinPartySize <= MaxPartySize."));
		bValid = false;
	}
	if (!FMath::IsFinite(TimeLimitSeconds) || TimeLimitSeconds < 60.0f)
	{
		Context.AddError(NSLOCTEXT("ProjectJDungeon", "InvalidTimeLimit", "UProject_JDungeonDefinition requires TimeLimitSeconds >= 60."));
		bValid = false;
	}
	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif

void UProject_JDungeonInstanceSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	const FString RuntimeSecret = FPlatformMisc::GetEnvironmentVariable(TEXT("PROJECT_J_DUNGEON_TICKET_KEY"));
	if (RuntimeSecret.Len() >= 32 && RuntimeSecret.Len() <= 256) TicketSigningSecret = RuntimeSecret;
}

void UProject_JDungeonInstanceSubsystem::Deinitialize()
{
	Channels.Reset();
	WarmPoolEndpoints.Reset();
	DungeonDefinitions.Reset();
	ActiveInstances.Reset();
	CharacterToInstance.Reset();
	ConsumedTicketNonces.Reset();
	Super::Deinitialize();
}

void UProject_JDungeonInstanceSubsystem::RegisterChannelHeartbeat(const FProject_JChannelInfo& Channel)
{
	if (Channel.ZoneId.IsNone() || Channel.ChannelId <= 0 || Channel.ServerEndpoint.IsEmpty()
		|| Channel.CurrentCCU < 0 || Channel.MaxCCU <= 0 || !FMath::IsFinite(Channel.LastHeartbeatSeconds)
		|| Channel.LastHeartbeatSeconds <= 0.0 || Channel.LastHeartbeatSeconds > GetCurrentUtcEpochSeconds())
	{
		return;
	}
	const TPair<FName, int32> Key(Channel.ZoneId, Channel.ChannelId);
	const FProject_JChannelInfo* Previous = Channels.Find(Key);
	if (!Previous || Channel.LastHeartbeatSeconds >= Previous->LastHeartbeatSeconds)
	{
		Channels.Add(Key, Channel);
	}
}

void UProject_JDungeonInstanceSubsystem::ReplaceZoneChannels(FName Zone, TArray<FProject_JChannelInfo> Snapshot)
{
	for (auto It = Channels.CreateIterator(); It; ++It)
		if (It.Key().Key == Zone) It.RemoveCurrent();
	for (const auto& Channel : Snapshot)
		if (Channel.ZoneId == Zone) RegisterChannelHeartbeat(Channel);
}

bool UProject_JDungeonInstanceSubsystem::ResolveReturnChannelEndpoint(
	const FProject_JReturnAnchor& Anchor,
	FString& OutEndpoint,
	int32& OutChannelId) const
{
	if (!Anchor.IsValid())
	{
		return false;
	}
	const double NowSeconds = GetCurrentUtcEpochSeconds();
	const auto IsAvailable = [NowSeconds](const FProject_JChannelInfo& Channel)
	{
		const double AgeSeconds = NowSeconds - Channel.LastHeartbeatSeconds;
		return AgeSeconds >= 0.0 && AgeSeconds < ChannelHeartbeatTimeoutSeconds
			&& !Channel.bDraining && Channel.CurrentCCU < Channel.MaxCCU;
	};
	if (const FProject_JChannelInfo* Exact = Channels.Find(TPair<FName, int32>(Anchor.ZoneId, Anchor.ChannelId)))
	{
		if (IsAvailable(*Exact))
		{
			OutEndpoint = Exact->ServerEndpoint;
			OutChannelId = Exact->ChannelId;
			return true;
		}
	}
	// Original channel full or draining -> pick least-loaded healthy sibling channel in the same zone.
	const FProject_JChannelInfo* BestSibling = nullptr;
	for (const auto& Pair : Channels)
	{
		const FProject_JChannelInfo& Cand = Pair.Value;
		if (Cand.ZoneId == Anchor.ZoneId && IsAvailable(Cand))
		{
			if (!BestSibling || Cand.CurrentCCU < BestSibling->CurrentCCU)
			{
				BestSibling = &Cand;
			}
		}
	}
	if (BestSibling)
	{
		OutEndpoint = BestSibling->ServerEndpoint;
		OutChannelId = BestSibling->ChannelId;
		return true;
	}
	return false;
}

void UProject_JDungeonInstanceSubsystem::RegisterWarmPoolServer(const FString& ServerEndpoint)
{
	if (!ServerEndpoint.IsEmpty())
	{
		WarmPoolEndpoints.AddUnique(ServerEndpoint);
	}
}

void UProject_JDungeonInstanceSubsystem::RegisterDungeonDefinition(const UProject_JDungeonDefinition* Definition)
{
	if (Definition && !Definition->DungeonId.IsNone())
	{
		DungeonDefinitions.Add(Definition->DungeonId, Definition);
	}
}

const UProject_JDungeonDefinition* UProject_JDungeonInstanceSubsystem::FindDungeonDefinition(FName DungeonId) const
{
	if (const TWeakObjectPtr<const UProject_JDungeonDefinition>* Found = DungeonDefinitions.Find(DungeonId))
	{
		return Found->Get();
	}
	return nullptr;
}

bool UProject_JDungeonInstanceSubsystem::HasActiveDungeonInstance(const FGuid& CharacterId) const
{
	const FGuid* InstanceId = CharacterToInstance.Find(CharacterId);
	const auto* Record = InstanceId ? ActiveInstances.Find(*InstanceId) : nullptr;
	return Record && !Record->bCleared;
}

FString UProject_JDungeonInstanceSubsystem::ComputeTicketSignature(const FProject_JDungeonJoinTicket& Ticket) const
{
	FString Canonical = FString::Printf(
		TEXT("v3|%d|%s|%d:%s|%s|%s|%d:%s|%.3f|%.3f"),
		Ticket.bReturnToWorld ? 1 : 0,
		*Ticket.InstanceId.ToString(EGuidFormats::Digits),
		Ticket.DungeonId.ToString().Len(),
		*Ticket.DungeonId.ToString(),
		*Ticket.CharacterId.ToString(EGuidFormats::Digits),
		*Ticket.Nonce.ToString(EGuidFormats::Digits),
		Ticket.ServerEndpoint.Len(),
		*Ticket.ServerEndpoint,
		Ticket.IssuedAtSeconds,
		Ticket.ExpiresAtSeconds);
	if (!Ticket.SiblingReturnRoute.IsEmpty()) Canonical += TEXT("|route|") + Ticket.SiblingReturnRoute;

	FSHAHash Hash;
	FTCHARToUTF8 Utf8(*Canonical);
	const FTCHARToUTF8 Key(*TicketSigningSecret);
	FSHA1::HMACBuffer(Key.Get(), Key.Length(), Utf8.Get(), Utf8.Length(), Hash.Hash);
	return Hash.ToString();
}

bool UProject_JDungeonInstanceSubsystem::RequestDungeonEntry(
	EProject_JDungeonEntrySource EntrySource,
	FName DungeonId,
	const TArray<FGuid>& PartyCharacterIds,
	const TMap<FGuid, FProject_JReturnAnchor>& ReturnAnchors,
	double NowSeconds,
	FGuid& OutInstanceId,
	TArray<FProject_JDungeonJoinTicket>& OutTickets)
{
	OutTickets.Reset();
	OutInstanceId.Invalidate();
	if (DungeonId.IsNone() || PartyCharacterIds.IsEmpty() || PartyCharacterIds.Num() > 40 || !FMath::IsFinite(NowSeconds))
	{
		return false;
	}
	PruneExpiredState(NowSeconds);
	if (ActiveInstances.Num() >= MaxActiveInstances) return false;
	TSet<FGuid> Party;
	for (const FGuid& CharId : PartyCharacterIds)
	{
		if (!CharId.IsValid() || Party.Contains(CharId) || HasActiveDungeonInstance(CharId))
		{
			return false;
		}
		Party.Add(CharId);
	}

	const UProject_JDungeonDefinition* Definition = FindDungeonDefinition(DungeonId);
	if (Definition)
	{
		if (PartyCharacterIds.Num() < Definition->MinPartySize || PartyCharacterIds.Num() > Definition->MaxPartySize)
		{
			return false;
		}
	}

	FString Endpoint;
	if (!WarmPoolEndpoints.IsEmpty())
	{
		Endpoint = WarmPoolEndpoints.Pop();
	}
	else if (Definition && !Definition->DungeonMap.IsNull())
	{
		Endpoint = Definition->DungeonMap.GetLongPackageName();
	}
	else
	{
		const int32 Port = NextDynamicPort++;
		if (NextDynamicPort >= 60000)
		{
			NextDynamicPort = 7780;
		}
		Endpoint = FString::Printf(TEXT("127.0.0.1:%d"), Port);
	}

	FProject_JDungeonInstanceRecord Record;
	Record.InstanceId = FGuid::NewGuid();
	Record.DungeonId = DungeonId;
	Record.ServerEndpoint = Endpoint;
	const double Duration = Definition ? Definition->TimeLimitSeconds : 1800.0;
	Record.ExpiresAtSeconds = NowSeconds + FMath::Max(DefaultTicketTTLSeconds, Duration);
	Record.AllowedCharacters = MoveTemp(Party);
	for (const auto& Pair : ReturnAnchors)
		if (Record.AllowedCharacters.Contains(Pair.Key) && Pair.Value.IsValid()) Record.ReturnAnchors.Add(Pair.Key, Pair.Value);
	if (!InstallInstanceAdmission(Record, NowSeconds)) return false;

	for (const FGuid& CharId : PartyCharacterIds)
	{
		FProject_JDungeonJoinTicket Ticket;
		if (TryIssueReservedJoinTicket(Record.InstanceId, CharId, NowSeconds, Ticket)) OutTickets.Add(MoveTemp(Ticket));
	}

	OutInstanceId = Record.InstanceId;
	return true;
}

bool UProject_JDungeonInstanceSubsystem::TryIssueReservedJoinTicket(const FGuid& InstanceId, const FGuid& CharacterId,
	double NowSeconds, FProject_JDungeonJoinTicket& OutTicket) const
{
	const auto* Record = ActiveInstances.Find(InstanceId);
	if (!Record) { OutTicket = {}; return false; }
	return MakeJoinTicket(*Record, CharacterId, NowSeconds, OutTicket);
}

bool UProject_JDungeonInstanceSubsystem::MakeJoinTicket(const FProject_JDungeonInstanceRecord& Record, const FGuid& CharacterId,
	double NowSeconds, FProject_JDungeonJoinTicket& OutTicket) const
{
	OutTicket = {};
	if (Record.bCleared || !Record.AllowedCharacters.Contains(CharacterId) || Record.JoinedCharacters.Contains(CharacterId)
		|| !FMath::IsFinite(NowSeconds) || Record.ExpiresAtSeconds <= NowSeconds) return false;
	OutTicket.InstanceId = Record.InstanceId;
	OutTicket.DungeonId = Record.DungeonId;
	OutTicket.CharacterId = CharacterId;
	OutTicket.Nonce = FGuid::NewGuid();
	OutTicket.ServerEndpoint = Record.ServerEndpoint;
	OutTicket.IssuedAtSeconds = NowSeconds;
	OutTicket.ExpiresAtSeconds = FMath::Min(Record.ExpiresAtSeconds, NowSeconds + DefaultTicketTTLSeconds);
	OutTicket.SignatureHex = ComputeTicketSignature(OutTicket);
	return true;
}

bool UProject_JDungeonInstanceSubsystem::HasNetworkSigningSecret() const
{
	return TicketSigningSecret.Len() >= 32 && TicketSigningSecret != TEXT("ProjectJ_LocalDev_TicketSecret_ChangeInProd");
}

void UProject_JDungeonInstanceSubsystem::RetireInstance(FGuid Instance)
{
	FProject_JDungeonInstanceRecord Record;
	if (!ActiveInstances.RemoveAndCopyValue(Instance, Record)) return;
	for (const FGuid& Character : Record.AllowedCharacters)
		if (CharacterToInstance.FindRef(Character) == Instance) CharacterToInstance.Remove(Character);
}

void UProject_JDungeonInstanceSubsystem::PollOnlineWorker(UWorld* World, const FGuid& Worker, const FString& Endpoint, FName DungeonId)
{
	if (bPollingOnlineWorker || DungeonId.IsNone() || !HasNetworkSigningSecret()) return;
	OnlineWorkerId = Worker;
	const TWeakObjectPtr<ThisClass> WeakThis(this);
	const TWeakObjectPtr<UWorld> WeakWorld(World);
	const FString WorkerId = UProject_JOnlineWorldSubsystem::GuidText(Worker);
	auto Request = MakeShared<FJsonObject>();
	Request->SetStringField(TEXT("worker_id"), WorkerId);
	Request->SetStringField(TEXT("server_endpoint"), Endpoint);
	Request->SetStringField(TEXT("dungeon_id"), DungeonId.ToString());
	bPollingOnlineWorker = true;
	if (!UProject_JOnlineWorldSubsystem::Submit(World, TEXT("Dungeon"), TEXT("Heartbeat"), Request, {},
		[WeakThis, WeakWorld, WorkerId, DungeonId, Endpoint](const TSharedPtr<FJsonObject>& Heartbeat)
		{
			auto* Self = WeakThis.Get();
			if (!Self) return;
			bool bRegistered = false;
			if (!Heartbeat || !Heartbeat->TryGetBoolField(TEXT("ok"), bRegistered) || !bRegistered || Self->GetWorld() != WeakWorld.Get())
			{ Self->bPollingOnlineWorker = false; return; }
			auto Query = MakeShared<FJsonObject>();
			Query->SetStringField(TEXT("worker_id"), WorkerId);
			if (!UProject_JOnlineWorldSubsystem::Submit(WeakWorld.Get(), TEXT("Dungeon"), TEXT("Worker_Admission"), Query, {},
				[WeakThis, WeakWorld, WorkerId, DungeonId, Endpoint](const TSharedPtr<FJsonObject>& Admission)
				{
					auto* Allocator = WeakThis.Get();
					if (!Allocator) return;
					Allocator->bPollingOnlineWorker = false;
					if (Allocator->GetWorld() == WeakWorld.Get() && Admission)
						Allocator->InstallOnlineWorkerAdmission(*Admission, WorkerId, DungeonId, Endpoint);
				})) Self->bPollingOnlineWorker = false;
		})) bPollingOnlineWorker = false;
}

void UProject_JDungeonInstanceSubsystem::InstallOnlineWorkerAdmission(const FJsonObject& Admission, const FString& WorkerId,
	FName DungeonId, const FString& Endpoint)
{
	FString Status, Instance, AssignedDungeon, AssignedEndpoint;
	const TSharedPtr<FJsonObject>* Members = nullptr;
	FProject_JDungeonInstanceRecord Record;
	if (!Admission.TryGetStringField(TEXT("status"), Status) || (Status != TEXT("RESERVED") && Status != TEXT("READY"))
		|| !Admission.TryGetStringField(TEXT("instance_id"), Instance) || !FGuid::Parse(Instance, Record.InstanceId)
		|| !Admission.TryGetStringField(TEXT("dungeon_id"), AssignedDungeon) || AssignedDungeon != DungeonId.ToString()
		|| !Admission.TryGetStringField(TEXT("server_endpoint"), AssignedEndpoint) || AssignedEndpoint != Endpoint
		|| !Admission.TryGetNumberField(TEXT("expires_at"), Record.ExpiresAtSeconds)
		|| !Admission.TryGetObjectField(TEXT("members"), Members) || !Members || (*Members)->Values.IsEmpty()
		|| (*Members)->Values.Num() > 40) return;
	Record.DungeonId = DungeonId;
	Record.ServerEndpoint = Endpoint;
	for (const auto& Member : (*Members)->Values)
	{
		FGuid Character;
		FString Zone;
		FProject_JReturnAnchor Anchor;
		const TSharedPtr<FJsonObject>* AnchorObject = nullptr;
		const auto ReadVector = [](const FJsonObject& Object, const TCHAR* Name, FVector& Out)
		{
			const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
			return Object.TryGetArrayField(Name, Values) && Values->Num() == 3
				&& (*Values)[0]->TryGetNumber(Out.X) && (*Values)[1]->TryGetNumber(Out.Y) && (*Values)[2]->TryGetNumber(Out.Z);
		};
		FVector Rotation;
		if (!FGuid::Parse(Member.Key, Character) || !Member.Value->TryGetObject(AnchorObject) || !AnchorObject
			|| !(*AnchorObject)->TryGetStringField(TEXT("zone_id"), Zone) || Zone.IsEmpty() || Zone.Len() > 64
			|| !(*AnchorObject)->TryGetNumberField(TEXT("channel_id"), Anchor.ChannelId)
			|| !ReadVector(**AnchorObject, TEXT("location"), Anchor.Location)
			|| !ReadVector(**AnchorObject, TEXT("rotation"), Rotation)) return;
		Anchor.ZoneId = FName(*Zone);
		Anchor.Rotation = FRotator(Rotation.X, Rotation.Y, Rotation.Z);
		if (!Anchor.IsValid()) return;
		Record.AllowedCharacters.Add(Character);
		Record.ReturnAnchors.Add(Character, Anchor);
	}
	if (OnlineWorkerAdmission.IsValid() && OnlineWorkerAdmission != Record.InstanceId)
	{
		RetireInstance(OnlineWorkerAdmission);
		bOnlineClearEnqueued = false;
	}
	if (!InstallInstanceAdmission(Record, GetCurrentUtcEpochSeconds())) return;
	OnlineWorkerAdmission = Record.InstanceId;
	if (Status == TEXT("READY")) return;
	auto Ack = MakeShared<FJsonObject>();
	Ack->SetStringField(TEXT("worker_id"), WorkerId);
	Ack->SetStringField(TEXT("instance_id"), Instance);
	UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("Dungeon"), TEXT("Acknowledge"), Ack, Record.InstanceId, [](auto) {});
}

bool UProject_JDungeonInstanceSubsystem::GetInstanceAdmission(const FGuid& InstanceId, FProject_JDungeonInstanceRecord& OutRecord) const
{
	const auto* Record = ActiveInstances.Find(InstanceId);
	if (!Record || Record->bCleared) return false;
	OutRecord = {};
	OutRecord.InstanceId = Record->InstanceId;
	OutRecord.DungeonId = Record->DungeonId;
	OutRecord.ServerEndpoint = Record->ServerEndpoint;
	OutRecord.ExpiresAtSeconds = Record->ExpiresAtSeconds;
	OutRecord.AllowedCharacters = Record->AllowedCharacters;
	OutRecord.ReturnAnchors = Record->ReturnAnchors;
	return true;
}

bool UProject_JDungeonInstanceSubsystem::InstallInstanceAdmission(const FProject_JDungeonInstanceRecord& Record, double NowSeconds)
{
	if (!Record.InstanceId.IsValid() || Record.DungeonId.IsNone() || Record.DungeonId.ToString().Len() > 64
		|| Record.ServerEndpoint.IsEmpty() || Record.ServerEndpoint.Len() > 256
		|| Record.AllowedCharacters.IsEmpty() || Record.AllowedCharacters.Num() > 40
		|| !FMath::IsFinite(NowSeconds) || !FMath::IsFinite(Record.ExpiresAtSeconds) || Record.ExpiresAtSeconds <= NowSeconds) return false;
	for (const auto& Pair : Record.ReturnAnchors)
		if (!Record.AllowedCharacters.Contains(Pair.Key) || !Pair.Value.IsValid()) return false;
	if (const auto* Existing = ActiveInstances.Find(Record.InstanceId))
	{
		if (Existing->bCleared || Existing->ExpiresAtSeconds != Record.ExpiresAtSeconds || Existing->DungeonId != Record.DungeonId
			|| Existing->ServerEndpoint != Record.ServerEndpoint || Existing->AllowedCharacters.Num() != Record.AllowedCharacters.Num()) return false;
		for (const FGuid& Character : Record.AllowedCharacters)
			if (!Existing->AllowedCharacters.Contains(Character)) return false;
		if (Existing->ReturnAnchors.Num() != Record.ReturnAnchors.Num()) return false;
		for (const auto& Pair : Record.ReturnAnchors)
		{
			const auto* Anchor = Existing->ReturnAnchors.Find(Pair.Key);
			if (!Anchor || Anchor->ZoneId != Pair.Value.ZoneId || Anchor->ChannelId != Pair.Value.ChannelId
				|| Anchor->Location != Pair.Value.Location || Anchor->Rotation != Pair.Value.Rotation) return false;
		}
		return true;
	}
	if (ActiveInstances.Num() >= MaxActiveInstances) return false;
	for (const FGuid& Character : Record.AllowedCharacters)
		if (!Character.IsValid() || HasActiveDungeonInstance(Character)) return false;

	auto& Installed = ActiveInstances.Add(Record.InstanceId);
	Installed.InstanceId = Record.InstanceId;
	Installed.DungeonId = Record.DungeonId;
	Installed.ServerEndpoint = Record.ServerEndpoint;
	Installed.ExpiresAtSeconds = Record.ExpiresAtSeconds;
	Installed.AllowedCharacters = Record.AllowedCharacters;
	Installed.ReturnAnchors = Record.ReturnAnchors;
	for (const FGuid& Character : Installed.AllowedCharacters) CharacterToInstance.Add(Character, Installed.InstanceId);
	return true;
}

bool UProject_JDungeonInstanceSubsystem::ParseJoinTicketOptions(const FString& Options, FProject_JDungeonJoinTicket& OutTicket)
{
	OutTicket = {};
	if (Options.Len() > 4096) return false;
	const FString Dungeon = UGameplayStatics::ParseOption(Options, TEXT("TicketDungeon"));
	const FString Signature = UGameplayStatics::ParseOption(Options, TEXT("TicketSig"));
	const FString Endpoint = UGameplayStatics::ParseOption(Options, TEXT("TicketEndpoint"));
	if (Dungeon.IsEmpty() || Dungeon.Len() > 64 || Signature.Len() != 40 || Endpoint.IsEmpty() || Endpoint.Len() > 256) return false;
	for (TCHAR Digit : Signature)
		if (!FChar::IsHexDigit(Digit)) return false;
	for (TCHAR Character : Endpoint)
		if (!((Character <= 127 && FChar::IsAlnum(Character)) || FCString::Strchr(TEXT(".:-_/[]"), Character))) return false;

	const auto ParseTime = [&Options](const TCHAR* Key, double& OutValue)
	{
		const FString Time = UGameplayStatics::ParseOption(Options, Key);
		int32 Dot;
		if (Time.Len() > 18 || !Time.FindChar(TEXT('.'), Dot) || Dot < 1 || Dot + 4 != Time.Len()) return false;
		for (int32 Index = 0; Index < Time.Len(); ++Index)
			if (Index != Dot && (Time[Index] < TEXT('0') || Time[Index] > TEXT('9'))) return false;
		OutValue = FCString::Atod(*Time);
		return FMath::IsFinite(OutValue);
	};
	FProject_JDungeonJoinTicket Parsed;
	if (!FGuid::ParseExact(UGameplayStatics::ParseOption(Options, TEXT("TicketInstance")), EGuidFormats::Digits, Parsed.InstanceId)
		|| !FGuid::ParseExact(UGameplayStatics::ParseOption(Options, TEXT("TicketChar")), EGuidFormats::Digits, Parsed.CharacterId)
		|| !FGuid::ParseExact(UGameplayStatics::ParseOption(Options, TEXT("TicketNonce")), EGuidFormats::Digits, Parsed.Nonce)
		|| !ParseTime(TEXT("TicketIssued"), Parsed.IssuedAtSeconds)
		|| !ParseTime(TEXT("TicketExp"), Parsed.ExpiresAtSeconds)) return false;
	Parsed.DungeonId = FName(*Dungeon, FNAME_Find);
	Parsed.ServerEndpoint = Endpoint;
	Parsed.SignatureHex = Signature;
	const FString Direction = UGameplayStatics::ParseOption(Options, TEXT("TicketReturn"));
	if (!Direction.IsEmpty() && Direction != TEXT("1")) return false;
	Parsed.bReturnToWorld = Direction == TEXT("1");
	Parsed.SiblingReturnRoute = UGameplayStatics::ParseOption(Options, TEXT("TicketRoute"));
	if (Parsed.SiblingReturnRoute.Len() > 256 || (!Parsed.SiblingReturnRoute.IsEmpty() && !Parsed.bReturnToWorld)) return false;
	if (!Parsed.IsValidFormat()) return false;
	OutTicket = MoveTemp(Parsed);
	return true;
}

bool UProject_JDungeonInstanceSubsystem::CanAdmitJoinTicket(const FProject_JDungeonJoinTicket& Ticket, double NowSeconds) const
{
	if (const auto* World = GetWorld(); World && World->GetNetMode() != NM_Standalone
		&& !HasNetworkSigningSecret()) return false;
	if (!Ticket.IsValidFormat() || !FMath::IsFinite(NowSeconds) || !FMath::IsFinite(Ticket.IssuedAtSeconds)
		|| !FMath::IsFinite(Ticket.ExpiresAtSeconds) || Ticket.IssuedAtSeconds > NowSeconds + 0.001
		|| Ticket.ExpiresAtSeconds <= NowSeconds || Ticket.ExpiresAtSeconds <= Ticket.IssuedAtSeconds
		|| ConsumedTicketNonces.Contains(Ticket.Nonce))
	{
		return false;
	}
	if (ComputeTicketSignature(Ticket) != Ticket.SignatureHex) return false;
	if (Ticket.bReturnToWorld)
	{
		FProject_JReturnAnchor Anchor;
		return GetReturnPlacement(Ticket, NowSeconds, Anchor);
	}
	const FProject_JDungeonInstanceRecord* Record = ActiveInstances.Find(Ticket.InstanceId);
	if (!Record || NowSeconds >= Record->ExpiresAtSeconds
		|| Record->DungeonId != Ticket.DungeonId
		|| !Record->AllowedCharacters.Contains(Ticket.CharacterId))
	{
		return false;
	}
	if (Record->bCleared || Record->ServerEndpoint != Ticket.ServerEndpoint) return false;
	if (Record->JoinedCharacters.Contains(Ticket.CharacterId))
	{
		const auto* Reconnect = Record->PendingReconnectTickets.Find(Ticket.CharacterId);
		if (!Reconnect || Reconnect->Nonce != Ticket.Nonce) return false;
	}
	return true;
}

bool UProject_JDungeonInstanceSubsystem::VerifyAndConsumeJoinTicket(const FProject_JDungeonJoinTicket& Ticket, double NowSeconds)
{
	if (!CanAdmitJoinTicket(Ticket, NowSeconds)) return false;
	if (ConsumedTicketNonces.Num() >= 512)
	{
		for (auto It = ConsumedTicketNonces.CreateIterator(); It; ++It)
			if (NowSeconds >= It.Value()) It.RemoveCurrent();
		if (ConsumedTicketNonces.Num() >= 4096) return false;
	}
	auto* Record = ActiveInstances.Find(Ticket.InstanceId);
	ConsumedTicketNonces.Add(Ticket.Nonce, Ticket.ExpiresAtSeconds);
	if (Ticket.bReturnToWorld) return true;
	Record->JoinedCharacters.Add(Ticket.CharacterId);
	Record->PendingReconnectTickets.Remove(Ticket.CharacterId);
	Record->DisconnectGraceDeadlines.Remove(Ticket.CharacterId);
	return true;
}

double UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds()
{
	return FDateTime::UtcNow().ToUnixTimestampDecimal();
}

void UProject_JDungeonInstanceSubsystem::PruneExpiredState(double NowSeconds)
{
	for (auto It = ConsumedTicketNonces.CreateIterator(); It; ++It)
	{
		if (NowSeconds >= It.Value())
		{
			It.RemoveCurrent();
		}
	}
	for (auto It = ActiveInstances.CreateIterator(); It; ++It)
	{
		auto& Record = It.Value();
		if (NowSeconds >= Record.ExpiresAtSeconds)
		{
			for (const FGuid& Character : Record.AllowedCharacters)
			{
				if (CharacterToInstance.FindRef(Character) != Record.InstanceId) continue;
				CharacterToInstance.Remove(Character);
			}
			It.RemoveCurrent();
			continue;
		}
		for (auto Grace = Record.DisconnectGraceDeadlines.CreateIterator(); Grace; ++Grace)
		{
			if (NowSeconds < Grace.Value()) continue;
			Record.PendingReconnectTickets.Remove(Grace.Key());
			Grace.RemoveCurrent();
		}
	}
}

bool UProject_JDungeonInstanceSubsystem::RecordDisconnectInDungeon(const FGuid& CharacterId, double NowSeconds)
{
	const FGuid* InstanceId = CharacterToInstance.Find(CharacterId);
	FProject_JDungeonInstanceRecord* Record = InstanceId ? ActiveInstances.Find(*InstanceId) : nullptr;
	if (!Record || Record->bCleared || !Record->JoinedCharacters.Contains(CharacterId)
		|| !FMath::IsFinite(NowSeconds) || NowSeconds >= Record->ExpiresAtSeconds)
	{
		return false;
	}
	if (!Record->DisconnectGraceDeadlines.Contains(CharacterId))
		Record->DisconnectGraceDeadlines.Add(CharacterId, FMath::Min(Record->ExpiresAtSeconds, NowSeconds + DisconnectGraceSeconds));
	return true;
}

bool UProject_JDungeonInstanceSubsystem::TryIssueReconnectTicket(
	const FGuid& CharacterId,
	double NowSeconds,
	FProject_JDungeonJoinTicket& OutTicket)
{
	const FGuid* InstanceId = CharacterToInstance.Find(CharacterId);
	FProject_JDungeonInstanceRecord* Record = InstanceId ? ActiveInstances.Find(*InstanceId) : nullptr;
	if (!Record || Record->bCleared || !FMath::IsFinite(NowSeconds) || NowSeconds >= Record->ExpiresAtSeconds)
	{
		return false;
	}
	const double* Deadline = Record->DisconnectGraceDeadlines.Find(CharacterId);
	if (!Deadline || NowSeconds >= *Deadline)
	{
		return false;
	}
	if (const auto* Pending = Record->PendingReconnectTickets.Find(CharacterId); Pending && NowSeconds < Pending->ExpiresAtSeconds)
	{
		OutTicket = *Pending;
		return true;
	}

	OutTicket = FProject_JDungeonJoinTicket{};
	OutTicket.InstanceId = Record->InstanceId;
	OutTicket.DungeonId = Record->DungeonId;
	OutTicket.CharacterId = CharacterId;
	OutTicket.Nonce = FGuid::NewGuid();
	OutTicket.ServerEndpoint = Record->ServerEndpoint;
	OutTicket.IssuedAtSeconds = NowSeconds;
	OutTicket.ExpiresAtSeconds = FMath::Min(*Deadline, NowSeconds + DefaultTicketTTLSeconds);
	OutTicket.SignatureHex = ComputeTicketSignature(OutTicket);
	Record->PendingReconnectTickets.Add(CharacterId, OutTicket);
	return true;
}

bool UProject_JDungeonInstanceSubsystem::ClaimDungeonClearReward(const FGuid& InstanceId, const FGuid& CharacterId)
{
	FProject_JDungeonInstanceRecord* Record = ActiveInstances.Find(InstanceId);
	if (!Record || !Record->JoinedCharacters.Contains(CharacterId) || Record->ClaimedClearCharacters.Contains(CharacterId))
	{
		return false;
	}
	Record->bCleared = true;
	Record->ClaimedClearCharacters.Add(CharacterId);
	return true;
}

bool UProject_JDungeonInstanceSubsystem::IsDungeonClearedFor(const FGuid& Character) const
{
	const auto* Record = ActiveInstances.Find(GetCharacterInstanceId(Character));
	return Record && Record->bCleared && Record->JoinedCharacters.Contains(Character);
}

bool UProject_JDungeonInstanceSubsystem::ResolveFinalBossDefeat(const UProject_JDungeonDefinition& Definition)
{
	auto* World = GetWorld();
	if (!World || World->bIsTearingDown || World->GetNetMode() == NM_Client) return false;
	auto* Loot = GetGameInstance()->GetSubsystem<UProject_JLootSubsystem>();
	const auto Roll = [&](FGuid Instance, FGuid Character)
	{
		return Loot->RollPersonalLoot(Definition.ClearLootTable, Character,
			static_cast<int32>(HashCombineFast(GetTypeHash(Instance), GetTypeHash(Character))));
	};
	if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(World))
	{
		auto* Record = ActiveInstances.Find(OnlineWorkerAdmission);
		if (!Record || Record->DungeonId != Definition.DungeonId || Record->JoinedCharacters.IsEmpty()
			|| !OnlineWorkerId.IsValid() || !Loot) return false;
		if (Record->bCleared || bOnlineClearEnqueued) return true;
		auto Rewards = MakeShared<FJsonObject>();
		for (const FGuid& Character : Record->JoinedCharacters)
		{
			const auto Grant = Roll(Record->InstanceId, Character);
			Rewards->SetObjectField(UProject_JOnlineWorldSubsystem::GuidText(Character), UProject_JOnlineWorldSubsystem::WriteLoot(Grant));
		}
		auto Clear = MakeShared<FJsonObject>();
		Clear->SetStringField(TEXT("worker_id"), UProject_JOnlineWorldSubsystem::GuidText(OnlineWorkerId));
		Clear->SetStringField(TEXT("instance_id"), UProject_JOnlineWorldSubsystem::GuidText(OnlineWorkerAdmission));
		if (!Definition.QuestClearEventId.IsNone())
			Clear->SetStringField(TEXT("quest_event"), Definition.QuestClearEventId.ToString());
		Clear->SetObjectField(TEXT("rewards"), Rewards);
		return EnqueueOnlineClear(World, Clear);
	}

	auto* Economy = GetGameInstance()->GetSubsystem<UProject_JEconomySubsystem>();
	bool bCleared = false;
	for (auto It = World->GetPlayerControllerIterator(); It; ++It)
	{
		auto* Player = It->Get() ? It->Get()->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (!Player || !Player->CanAcceptSessionCommand()) continue;
		const FGuid Character = Player->GetCharacterId().Value;
		const FGuid Instance = GetCharacterInstanceId(Character);
		const auto* Record = ActiveInstances.Find(Instance);
		if (!Record || Record->DungeonId != Definition.DungeonId || !Record->JoinedCharacters.Contains(Character)
			|| Record->ClaimedClearCharacters.Contains(Character)) continue;
		if (Definition.ClearLootTable && (!Loot || !Economy || !Economy->SendBossRewardMail(Roll(Instance, Character)))) continue;
		if (!ClaimDungeonClearReward(Instance, Character)) continue;
		bCleared = true;
		if (!Definition.QuestClearEventId.IsNone())
			if (auto* Quests = Player->FindComponentByClass<UProject_JQuestComponent>())
				Quests->TryRecordObjective(Definition.QuestClearEventId);
	}
	return bCleared;
}

bool UProject_JDungeonInstanceSubsystem::EnqueueOnlineClear(UWorld* World, const TSharedRef<FJsonObject>& Clear)
{
	const FGuid Instance = OnlineWorkerAdmission;
	const FGuid Worker = OnlineWorkerId;
	bOnlineClearEnqueued = true;
	const bool bAdmitted = UProject_JOnlineWorldSubsystem::EnqueueDurableMutation(World, TEXT("Dungeon"), TEXT("Clear"),
		Clear, Instance, TEXT("dungeon completion"),
		[WeakThis = TWeakObjectPtr<ThisClass>(this), WeakWorld = TWeakObjectPtr<UWorld>(World), Instance, Worker]
		(bool bConfirmed, TSharedPtr<FJsonObject>)
		{
			auto* Dungeon = WeakThis.Get();
			auto* Origin = WeakWorld.Get();
			if (!Dungeon || !Origin || Origin->bIsTearingDown || Dungeon->GetWorld() != Origin
				|| Dungeon->OnlineWorkerAdmission != Instance || Dungeon->OnlineWorkerId != Worker) return;
			// Outbox alone owns wire retry and quarantine. A final failure may be
			// redriven by the operator under the original worker and instance fence.
			if (!bConfirmed) return;
			auto* Cleared = Dungeon->ActiveInstances.Find(Instance);
			auto* World = Dungeon->GetWorld();
			if (!Cleared || !World) return;
			Cleared->bCleared = true;
			Cleared->ClaimedClearCharacters = Cleared->JoinedCharacters;
			const auto* Mode = World->GetAuthGameMode<AProject_JGameMode>();
			for (auto It = World->GetPlayerControllerIterator(); Mode && It; ++It)
			{
				auto* PS = It->Get() ? It->Get()->GetPlayerState<AProject_JPlayerState>() : nullptr;
				const int64 Epoch = Mode->GetOnlineEpoch(PS);
				if (Epoch <= 0 || !Cleared->JoinedCharacters.Contains(PS->GetCharacterId().Value)) continue;
				auto Query = MakeShared<FJsonObject>();
				Query->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(PS->GetCharacterId().Value));
				Query->SetStringField(TEXT("session_epoch"), LexToString(Epoch));
				UProject_JOnlineWorldSubsystem::Submit(World, TEXT("World"), TEXT("Inventory"), Query, {},
					[WeakPlayer = TWeakObjectPtr<AProject_JPlayerState>(PS), Lease = PS->CaptureServerSession()](const TSharedPtr<FJsonObject>& Snapshot)
					{
						const auto Current = [WeakPlayer, Lease] { return WeakPlayer.IsValid() && WeakPlayer->IsServerSessionCurrent(Lease); };
						if (Snapshot && Current()) UProject_JOnlineWorldSubsystem::ApplyInventory(WeakPlayer.Get(), *Snapshot, Current);
					});
			}
		});
	if (!bAdmitted) bOnlineClearEnqueued = false;
	return bAdmitted;
}

bool UProject_JDungeonInstanceSubsystem::ResolveCharacterReturn(
	const FGuid& CharacterId,
	FProject_JReturnAnchor& OutAnchor,
	FString& OutEndpoint,
	int32& OutChannelId) const
{
	return GetCharacterReturnAnchor(CharacterId, OutAnchor) && ResolveReturnChannelEndpoint(OutAnchor, OutEndpoint, OutChannelId);
}

bool UProject_JDungeonInstanceSubsystem::GetCharacterReturnAnchor(const FGuid& CharacterId, FProject_JReturnAnchor& OutAnchor) const
{
	const FGuid* InstanceId = CharacterToInstance.Find(CharacterId);
	const auto* Record = InstanceId ? ActiveInstances.Find(*InstanceId) : nullptr;
	const auto* Anchor = Record ? Record->ReturnAnchors.Find(CharacterId) : nullptr;
	if (!Anchor || !Anchor->IsValid())
	{
		return false;
	}
	OutAnchor = *Anchor;
	return true;
}

bool UProject_JDungeonInstanceSubsystem::TryIssueReturnTicket(
	const FGuid& CharacterId, double NowSeconds, FProject_JDungeonJoinTicket& OutTicket, bool bEmbedReturnPlacement)
{
	OutTicket = {};
	const auto* InstanceId = CharacterToInstance.Find(CharacterId);
	auto* Record = InstanceId ? ActiveInstances.Find(*InstanceId) : nullptr;
	if (!Record || !Record->bCleared || !Record->JoinedCharacters.Contains(CharacterId)
		|| !FMath::IsFinite(NowSeconds) || NowSeconds >= Record->ExpiresAtSeconds) return false;
	if (const auto* Pending = Record->PendingReturnTickets.Find(CharacterId); Pending && NowSeconds < Pending->ExpiresAtSeconds)
	{
		OutTicket = *Pending;
		return true;
	}
	FProject_JReturnAnchor Anchor;
	FString Endpoint;
	int32 Channel = 0;
	if (!ResolveCharacterReturn(CharacterId, Anchor, Endpoint, Channel)) return false;
	OutTicket.InstanceId = *InstanceId;
	OutTicket.DungeonId = Record->DungeonId;
	OutTicket.CharacterId = CharacterId;
	OutTicket.Nonce = FGuid::NewGuid();
	OutTicket.ServerEndpoint = MoveTemp(Endpoint);
	OutTicket.IssuedAtSeconds = NowSeconds;
	OutTicket.ExpiresAtSeconds = FMath::Min(Record->ExpiresAtSeconds, NowSeconds + DefaultTicketTTLSeconds);
	OutTicket.bReturnToWorld = true;
	OutTicket.DestinationZone = Anchor.ZoneId;
	OutTicket.DestinationChannel = Channel;
	if (bEmbedReturnPlacement || Channel != Anchor.ChannelId)
	{
		OutTicket.SiblingReturnRoute = FString::Printf(TEXT("%s,%d,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f"),
			*Anchor.ZoneId.ToString(), Channel, Anchor.Location.X, Anchor.Location.Y, Anchor.Location.Z,
			Anchor.Rotation.Pitch, Anchor.Rotation.Yaw, Anchor.Rotation.Roll);
		if (OutTicket.SiblingReturnRoute.Len() > 256) return false;
	}
	OutTicket.SignatureHex = ComputeTicketSignature(OutTicket);
	Record->PendingReturnTickets.Add(CharacterId, OutTicket);
	return true;
}

bool UProject_JDungeonInstanceSubsystem::ReadSiblingReturnRoute(const FString& Route, FProject_JReturnAnchor& OutAnchor) const
{
	TArray<FString> Values;
	if (Route.Len() > 256 || Route.ParseIntoArray(Values, TEXT(","), false) != 8) return false;
	const UWorld* World = GetWorld();
	int32 ChannelId = 0;
	if (!World || Values[0] != World->GetMapName() || !LexTryParseString(ChannelId, *Values[1])
		|| ChannelId != LocalWorldChannelId || ChannelId <= 0) return false;
	double Coordinates[6];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Coordinates); ++Index)
		if (!LexTryParseString(Coordinates[Index], *Values[Index + 2]) || !FMath::IsFinite(Coordinates[Index])) return false;
	OutAnchor = {FName(*Values[0]), ChannelId, FVector(Coordinates[0], Coordinates[1], Coordinates[2]),
		FRotator(Coordinates[3], Coordinates[4], Coordinates[5])};
	return OutAnchor.IsValid();
}

bool UProject_JDungeonInstanceSubsystem::GetReturnPlacement(const FProject_JDungeonJoinTicket& Ticket, double NowSeconds,
	FProject_JReturnAnchor& OutAnchor) const
{
	const auto* World = GetWorld();
	const FURL Endpoint(nullptr, *Ticket.ServerEndpoint, TRAVEL_Absolute);
	if (!World || !Ticket.bReturnToWorld || !Endpoint.Valid || Endpoint.Host.IsEmpty() || Endpoint.Port != World->URL.Port) return false;
	if (!Ticket.SiblingReturnRoute.IsEmpty()) return ReadSiblingReturnRoute(Ticket.SiblingReturnRoute, OutAnchor);
	const auto* Record = ActiveInstances.Find(Ticket.InstanceId);
	const auto* Anchor = Record ? Record->ReturnAnchors.Find(Ticket.CharacterId) : nullptr;
	if (!Anchor || !Record->AllowedCharacters.Contains(Ticket.CharacterId) || Record->DungeonId != Ticket.DungeonId
		|| NowSeconds >= Record->ExpiresAtSeconds || !Anchor->IsValid()
		|| Anchor->ZoneId != FName(*World->GetMapName()) || Anchor->ChannelId != LocalWorldChannelId) return false;
	OutAnchor = *Anchor;
	return true;
}

bool UProject_JDungeonInstanceSubsystem::FinishCharacterReturn(const FProject_JDungeonJoinTicket& Ticket)
{
	if (!Ticket.bReturnToWorld || !ConsumedTicketNonces.Contains(Ticket.Nonce) || ComputeTicketSignature(Ticket) != Ticket.SignatureHex) return false;
	if (CharacterToInstance.FindRef(Ticket.CharacterId) != Ticket.InstanceId) return !Ticket.SiblingReturnRoute.IsEmpty();
	auto* Record = ActiveInstances.Find(Ticket.InstanceId);
	if (!Record || !Record->ReturnAnchors.Remove(Ticket.CharacterId)) return false;
	Record->AllowedCharacters.Remove(Ticket.CharacterId);
	CharacterToInstance.Remove(Ticket.CharacterId);
	if (Record->AllowedCharacters.IsEmpty()) ActiveInstances.Remove(Ticket.InstanceId);
	return true;
}
