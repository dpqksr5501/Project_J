#include "Network/Project_JMMOClientBridgeComponent.h"
#include "Backend/Project_JOnlineWorldSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Dom/JsonObject.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JGameMode.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "Project_JBaseCharacter.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Security/Project_JServerIntegritySubsystem.h"
#include "Social/Project_JSocialSubsystem.h"
#include "TimerManager.h"

struct UProject_JMMOClientBridgeComponent::FOnlineDungeonEntry
{
	FGuid Instance;
	FName Dungeon;
	TMap<FGuid, TPair<TWeakObjectPtr<APlayerController>, int64>> Party;
	double Deadline = 0.;
	bool bAutoTravel = true;
};

UProject_JMMOClientBridgeComponent::UProject_JMMOClientBridgeComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UProject_JMMOClientBridgeComponent::ClientReceiveMailInbox_Implementation(const TArray<FProject_JInboxEntry>& Entries,
	FGuid Next, bool bSucceeded)
{
	OnMailInbox.Broadcast(Entries, Next, bSucceeded);
}

void UProject_JMMOClientBridgeComponent::ServerRequestMailInbox_Implementation(FGuid Cursor)
{
	FGuid Character;
	if (!AdmitAuthoritativePlayer(false, Character) || bReadingInbox) return;
	auto* PC = CastChecked<APlayerController>(GetOwner());
	auto* Player = PC->GetPlayerState<AProject_JPlayerState>();
	auto* Mode = GetWorld()->GetAuthGameMode<AProject_JGameMode>();
	const int64 Epoch = Mode ? Mode->GetOnlineEpoch(Player) : 0;
	if (Epoch <= 0)
	{
		if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld())) { ClientReceiveMailInbox({}, {}, false); return; }
		TArray<FProject_JInboxEntry> Mail;
		if (const auto* Economy = GetWorld()->GetGameInstance()->GetSubsystem<UProject_JEconomySubsystem>())
			for (const auto& Message : Economy->GetInbox(Character))
			{
				if (Message.bClaimed || Mail.Num() >= 30) continue;
				FProject_JInboxEntry& Entry = Mail.AddDefaulted_GetRef();
				Entry.MailId = Message.MailId;
				Entry.Subject = Message.SubjectKey.ToString();
				Entry.Currency = Message.AttachedCurrency.ToString();
				Entry.Amount = Message.AttachedAmount;
			}
		ClientReceiveMailInbox(Mail, {}, true);
		return;
	}
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
	Body->SetStringField(TEXT("session_epoch"), LexToString(Epoch));
	Body->SetNumberField(TEXT("limit"), 30);
	if (Cursor.IsValid()) Body->SetStringField(TEXT("cursor"), UProject_JOnlineWorldSubsystem::GuidText(Cursor));
	const auto Lease = Player->CaptureServerSession();
	const TWeakObjectPtr<ThisClass> WeakSelf(this);
	const TWeakObjectPtr<AProject_JPlayerState> WeakPlayer(Player);
	bReadingInbox = true;
	const bool bStarted = UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("Mail"), TEXT("Inbox"), Body, {},
		[WeakSelf, WeakPlayer, Lease, Character, Epoch](const TSharedPtr<FJsonObject>& Response)
		{
			auto* Self = WeakSelf.Get();
			const auto* State = WeakPlayer.Get();
			if (!Self) return;
			Self->bReadingInbox = false;
			if (!State || !State->IsServerSessionCurrent(Lease)) return;
			Self->VisibleClaimableMail.Reset();
			Self->MailOwner = Character;
			Self->MailEpoch = Epoch;
			FString Status, Next;
			const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
			if (!Response.IsValid() || !Response->TryGetStringField(TEXT("status"), Status) || Status != TEXT("OK")
				|| !Response->TryGetArrayField(TEXT("mail"), Rows) || Rows->Num() > 30)
			{
				Self->ClientReceiveMailInbox({}, {}, false);
				return;
			}
			const auto* Definitions = State->GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
			TArray<FProject_JInboxEntry> Entries;
			for (const auto& Row : *Rows)
			{
				const TSharedPtr<FJsonObject>* Record = nullptr;
				FString Id, Amount;
				FProject_JInboxEntry Entry;
				if (!Row->TryGetObject(Record) || !Record || !(*Record)->TryGetStringField(TEXT("mail_id"), Id)
					|| !FGuid::Parse(Id, Entry.MailId) || !(*Record)->TryGetStringField(TEXT("subject_key"), Entry.Subject)) continue;
				(*Record)->TryGetStringField(TEXT("attached_currency"), Entry.Currency);
				if ((*Record)->TryGetStringField(TEXT("attached_amount"), Amount)) LexTryParseString(Entry.Amount, *Amount);
				int32 Version = 0;
				if ((*Record)->TryGetStringField(TEXT("definition_id"), Entry.Item))
				{
					(*Record)->TryGetNumberField(TEXT("definition_version"), Version);
					(*Record)->TryGetNumberField(TEXT("quantity"), Entry.Quantity);
					const auto* Definition = Definitions ? Definitions->GetItemDefinition(FName(*Entry.Item, FNAME_Find), Version) : nullptr;
					if (Definition && Entry.Quantity > 0 && Entry.Quantity <= Definition->MaxStackCount) Self->VisibleClaimableMail.Add(Entry.MailId);
				}
				else Self->VisibleClaimableMail.Add(Entry.MailId);
				Entries.Add(MoveTemp(Entry));
			}
			FGuid CursorNext;
			if (Response->TryGetStringField(TEXT("next_cursor"), Next)) FGuid::Parse(Next, CursorNext);
			Self->ClientReceiveMailInbox(Entries, CursorNext, true);
		});
	if (!bStarted) { bReadingInbox = false; ClientReceiveMailInbox({}, {}, false); }
}

bool UProject_JMMOClientBridgeComponent::AdmitAuthoritativePlayer(bool bRequireAlive, FGuid& OutCharacterId) const
{
	const APlayerController* PC = Cast<APlayerController>(GetOwner());
	if (!IsValid(PC) || !PC->HasAuthority() || !GetWorld() || GetWorld()->bIsTearingDown)
	{
		return false;
	}
	if (!UProject_JCommandBudgetComponent::AdmitClientCommand(const_cast<APlayerController*>(PC)))
	{
		return false;
	}
	const AProject_JPlayerState* PS = PC->GetPlayerState<AProject_JPlayerState>();
	if (!IsValid(PS) || !PS->CanAcceptSessionCommand())
	{
		return false;
	}
	OutCharacterId = PS->GetCharacterId().Value;
	if (!OutCharacterId.IsValid())
	{
		return false;
	}
	if (bRequireAlive)
	{
		if (const auto* Char = Cast<AProject_JBaseCharacter>(PC->GetPawn()))
		{
			if (!UProject_JDeathReviveSubsystem::CanCharacterPerformAliveAction(Char))
			{
				return false;
			}
		}
	}
	return true;
}

FString UProject_JMMOClientBridgeComponent::BuildDungeonTravelURL(const FProject_JDungeonJoinTicket& Ticket)
{
	const FString URL = FString::Printf(
		TEXT("%s?TicketInstance=%s?TicketDungeon=%s?TicketChar=%s?TicketNonce=%s?TicketEndpoint=%s?TicketIssued=%.3f?TicketExp=%.3f?TicketSig=%s"),
		*Ticket.ServerEndpoint,
		*Ticket.InstanceId.ToString(EGuidFormats::Digits),
		*Ticket.DungeonId.ToString(),
		*Ticket.CharacterId.ToString(EGuidFormats::Digits),
		*Ticket.Nonce.ToString(EGuidFormats::Digits),
		*Ticket.ServerEndpoint,
		Ticket.IssuedAtSeconds,
		Ticket.ExpiresAtSeconds,
		*Ticket.SignatureHex);
	if (!Ticket.bReturnToWorld) return URL;
	return Ticket.SiblingReturnRoute.IsEmpty() ? URL + TEXT("?TicketReturn=1")
		: URL + TEXT("?TicketReturn=1?TicketRoute=") + Ticket.SiblingReturnRoute;
}

bool UProject_JMMOClientBridgeComponent::DispatchDungeonEntryForPlayer(
	APlayerController* RequestingPC,
	EProject_JDungeonEntrySource Source,
	FName DungeonId,
	bool bAutoTravel)
{
	if (!IsValid(RequestingPC) || !RequestingPC->HasAuthority() || DungeonId.IsNone())
	{
		return false;
	}
	UWorld* World = RequestingPC->GetWorld();
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	auto* Dungeon = GI ? GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>() : nullptr;
	auto* RequestingPS = RequestingPC->GetPlayerState<AProject_JPlayerState>();
	const bool bOnline = UProject_JOnlineWorldSubsystem::UsesOnlineBackend(World);
	const auto* Mode = World ? World->GetAuthGameMode<AProject_JGameMode>() : nullptr;
	if (!Dungeon || (!bOnline && World->GetNetMode() != NM_Standalone) || !RequestingPS || !RequestingPS->CanAcceptSessionCommand()
		|| !RequestingPS->GetCharacterId().IsValid())
	{
		return false;
	}

	const FGuid RequestingCharId = RequestingPS->GetCharacterId().Value;
	TArray<FGuid> PartyIds;
	TMap<FGuid, APlayerController*> OnlineControllersByChar;

	if (const auto* Social = GI->GetSubsystem<UProject_JSocialSubsystem>())
	{
		const FName PartyId = Social->GetPartyIdForCharacter(RequestingCharId);
		FProject_JSocialGroupSnapshot PartySnap;
		if (!PartyId.IsNone() && Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, PartyId, PartySnap) && !PartySnap.Members.IsEmpty())
		{
			PartyIds = PartySnap.Members;
		}
	}
	if (PartyIds.IsEmpty())
	{
		PartyIds.Add(RequestingCharId);
	}

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* PC = It->Get();
		AProject_JPlayerState* PS = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (PS && PS->CanAcceptSessionCommand() && PS->GetCharacterId().IsValid())
		{
			OnlineControllersByChar.Add(PS->GetCharacterId().Value, PC);
		}
	}

	TMap<FGuid, FProject_JReturnAnchor> Anchors;
	const FName CurrentMapName(*World->GetMapName());
	for (const FGuid& MemberId : PartyIds)
	{
		APlayerController* MemberPC = OnlineControllersByChar.FindRef(MemberId);
		if (!MemberPC || !MemberPC->FindComponentByClass<UProject_JMMOClientBridgeComponent>()) return false;
		const APawn* MemberPawn = MemberPC->GetPawn();
		if (const auto* Character = Cast<AProject_JBaseCharacter>(MemberPawn);
			Character && !UProject_JDeathReviveSubsystem::CanCharacterPerformAliveAction(Character)) return false;
		FProject_JReturnAnchor Anchor;
		Anchor.ZoneId = CurrentMapName;
		Anchor.ChannelId = bOnline && Mode ? Mode->GetOnlineChannelId() : 1;
		Anchor.Location = MemberPawn ? MemberPawn->GetActorLocation() : FVector::ZeroVector;
		Anchor.Rotation = MemberPawn ? MemberPawn->GetActorRotation() : FRotator::ZeroRotator;
		Anchors.Add(MemberId, Anchor);
	}

	if (bOnline)
	{
		TMap<FGuid, APlayerController*> PartyControllers;
		for (const auto& Id : PartyIds) PartyControllers.Add(Id, OnlineControllersByChar.FindRef(Id));
		auto* Bridge = RequestingPC->FindComponentByClass<ThisClass>();
		return Bridge && Bridge->BeginOnlineDungeonEntry(DungeonId, PartyControllers, Anchors, bAutoTravel);
	}

	FGuid InstanceId;
	TArray<FProject_JDungeonJoinTicket> Tickets;
	if (!Dungeon->RequestDungeonEntry(Source, DungeonId, PartyIds, Anchors, UProject_JDungeonInstanceSubsystem::GetCurrentUtcEpochSeconds(), InstanceId, Tickets) || Tickets.IsEmpty())
	{
		return false;
	}

	for (const FProject_JDungeonJoinTicket& Ticket : Tickets)
	{
		if (APlayerController* TargetPC = OnlineControllersByChar.FindRef(Ticket.CharacterId))
		{
			if (auto* Bridge = TargetPC->FindComponentByClass<UProject_JMMOClientBridgeComponent>())
			{
				Bridge->ClientReceiveDungeonTicket(Ticket, bAutoTravel);
			}
		}
	}
	return true;
}

bool UProject_JMMOClientBridgeComponent::BeginOnlineDungeonEntry(FName DungeonId,
	const TMap<FGuid, APlayerController*>& Controllers, const TMap<FGuid, FProject_JReturnAnchor>& Anchors, bool bAutoTravel)
{
	if (OnlineDungeonEntry || Controllers.IsEmpty() || Controllers.Num() > 40 || Anchors.Num() != Controllers.Num()) return false;
	auto* Mode = GetWorld()->GetAuthGameMode<AProject_JGameMode>();
	auto* Allocator = GetWorld()->GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
	auto* Requester = CastChecked<APlayerController>(GetOwner())->GetPlayerState<AProject_JPlayerState>();
	const auto* Definition = Allocator->FindDungeonDefinition(DungeonId);
	if (!Mode || !Requester || !Allocator->HasNetworkSigningSecret() || !Definition
		|| Controllers.Num() < Definition->MinPartySize || Controllers.Num() > Definition->MaxPartySize
		|| !FMath::IsFinite(Definition->TimeLimitSeconds)) return false;
	const int64 Epoch = Mode->GetOnlineEpoch(Requester);
	const FGuid Character = Requester->GetCharacterId().Value;
	if (Epoch <= 0 || !Controllers.Contains(Character)) return false;

	auto Entry = MakeShared<FOnlineDungeonEntry>();
	Entry->Instance = FGuid::NewGuid();
	Entry->Dungeon = DungeonId;
	Entry->Deadline = FPlatformTime::Seconds() + 15.;
	Entry->bAutoTravel = bAutoTravel;
	auto Members = MakeShared<FJsonObject>();
	for (const auto& Pair : Controllers)
	{
		auto* State = Pair.Value->GetPlayerState<AProject_JPlayerState>();
		const int64 MemberEpoch = Mode->GetOnlineEpoch(State);
		const auto* Anchor = Anchors.Find(Pair.Key);
		if (MemberEpoch <= 0 || !Anchor || !Anchor->IsValid()) return false;
		Entry->Party.Add(Pair.Key, {Pair.Value, MemberEpoch});
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("zone_id"), Anchor->ZoneId.ToString());
		Row->SetNumberField(TEXT("channel_id"), Anchor->ChannelId);
		Row->SetStringField(TEXT("session_epoch"), LexToString(MemberEpoch));
		const auto& P = Anchor->Location;
		const auto& R = Anchor->Rotation;
		Row->SetArrayField(TEXT("location"), {MakeShared<FJsonValueNumber>(P.X), MakeShared<FJsonValueNumber>(P.Y), MakeShared<FJsonValueNumber>(P.Z)});
		Row->SetArrayField(TEXT("rotation"), {MakeShared<FJsonValueNumber>(R.Pitch), MakeShared<FJsonValueNumber>(R.Yaw), MakeShared<FJsonValueNumber>(R.Roll)});
		Members->SetObjectField(UProject_JOnlineWorldSubsystem::GuidText(Pair.Key), Row);
	}
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("instance_id"), UProject_JOnlineWorldSubsystem::GuidText(Entry->Instance));
	Body->SetStringField(TEXT("dungeon_id"), DungeonId.ToString());
	Body->SetStringField(TEXT("requester_char_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
	Body->SetStringField(TEXT("session_epoch"), LexToString(Epoch));
	Body->SetNumberField(TEXT("lifetime_seconds"), FMath::RoundToInt(FMath::Clamp(Definition->TimeLimitSeconds, 60.f, 14400.f)));
	Body->SetObjectField(TEXT("members"), Members);
	OnlineDungeonEntry = Entry;
	const bool bStarted = UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("Dungeon"), TEXT("Allocate"), Body, Entry->Instance,
		[WeakThis = TWeakObjectPtr<ThisClass>(this), Entry](const TSharedPtr<FJsonObject>& Response)
		{
			if (auto* Self = WeakThis.Get()) Self->ReadOnlineDungeonStatus(Entry, Response);
		});
	if (!bStarted) OnlineDungeonEntry.Reset();
	return bStarted;
}

void UProject_JMMOClientBridgeComponent::ReadOnlineDungeonStatus(const TSharedRef<FOnlineDungeonEntry>& Entry,
	const TSharedPtr<FJsonObject>& Response)
{
	if (OnlineDungeonEntry.Get() != &Entry.Get()) return;
	auto Reject = [this]
	{
		OnlineDungeonEntry.Reset();
		ClientNotifyMMOActionResult(TEXT("DungeonEntry"), false, TEXT("AllocationFailed"));
	};
	auto* World = GetWorld();
	auto* Mode = World ? World->GetAuthGameMode<AProject_JGameMode>() : nullptr;
	if (!Mode || !Response || FPlatformTime::Seconds() >= Entry->Deadline) { Reject(); return; }
	for (const auto& Member : Entry->Party)
	{
		const auto* PC = Member.Value.Key.Get();
		const auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (Mode->GetOnlineEpoch(State) != Member.Value.Value || State->GetCharacterId().Value != Member.Key
			|| !PC->GetPawn()) { Reject(); return; }
	}
	FString Status;
	if (!Response->TryGetStringField(TEXT("status"), Status) || (Status != TEXT("RESERVED") && Status != TEXT("READY")))
	{ Reject(); return; }
	FProject_JDungeonInstanceRecord Record;
	FString DestinationZone;
	int32 DestinationChannel = 0;
	if (Status == TEXT("READY") && Response->TryGetStringField(TEXT("server_endpoint"), Record.ServerEndpoint)
		&& Response->TryGetNumberField(TEXT("expires_at"), Record.ExpiresAtSeconds)
		&& Response->TryGetStringField(TEXT("zone"), DestinationZone) && !DestinationZone.IsEmpty()
		&& Response->TryGetNumberField(TEXT("channel"), DestinationChannel) && DestinationChannel > 0)
	{
		auto* Allocator = World->GetGameInstance()->GetSubsystem<UProject_JDungeonInstanceSubsystem>();
		Record.InstanceId = Entry->Instance;
		Record.DungeonId = Entry->Dungeon;
		for (const auto& Member : Entry->Party) Record.AllowedCharacters.Add(Member.Key);
		TArray<TPair<UProject_JMMOClientBridgeComponent*, FProject_JDungeonJoinTicket>> Deliveries;
		for (const auto& Member : Entry->Party)
		{
			auto* Bridge = Member.Value.Key->FindComponentByClass<ThisClass>();
			FProject_JDungeonJoinTicket Ticket;
			if (!Bridge || !Allocator->MakeJoinTicket(Record, Member.Key, Allocator->GetCurrentUtcEpochSeconds(), Ticket))
			{ Reject(); return; }
			Ticket.DestinationZone = FName(*DestinationZone);
			Ticket.DestinationChannel = DestinationChannel;
			Deliveries.Emplace(Bridge, MoveTemp(Ticket));
		}
		OnlineDungeonEntry.Reset();
		ClientNotifyMMOActionResult(TEXT("DungeonEntry"), true, TEXT("Allocated"));
		for (const auto& Delivery : Deliveries) Delivery.Key->ClientReceiveDungeonTicket(Delivery.Value, Entry->bAutoTravel);
		return;
	}
	World->GetTimerManager().SetTimer(OnlineDungeonPollTimer, FTimerDelegate::CreateWeakLambda(this, [this, Entry]
	{
		auto Query = MakeShared<FJsonObject>();
		Query->SetStringField(TEXT("instance_id"), UProject_JOnlineWorldSubsystem::GuidText(Entry->Instance));
		if (!UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("Dungeon"), TEXT("Status"), Query, {},
			[WeakThis = TWeakObjectPtr<ThisClass>(this), Entry](const TSharedPtr<FJsonObject>& Result)
			{
				if (auto* Self = WeakThis.Get()) Self->ReadOnlineDungeonStatus(Entry, Result);
			})) ReadOnlineDungeonStatus(Entry, nullptr);
	}), .25f, false);
}

void UProject_JMMOClientBridgeComponent::ServerRequestDungeonEntryFromMenu_Implementation(FName DungeonId)
{
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(true, CharId) || DungeonId.IsNone())
	{
		ClientNotifyMMOActionResult(TEXT("DungeonEntry"), false, TEXT("Denied"));
		return;
	}
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	const bool bOk = DispatchDungeonEntryForPlayer(PC, EProject_JDungeonEntrySource::MenuUI, DungeonId, true);
	if (!bOk || !UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
		ClientNotifyMMOActionResult(TEXT("DungeonEntry"), bOk, bOk ? TEXT("Allocated") : TEXT("AllocationFailed"));
}

void UProject_JMMOClientBridgeComponent::ClientReceiveDungeonTicket_Implementation(const FProject_JDungeonJoinTicket& Ticket, bool bAutoTravel)
{
	OnDungeonTicketReceived.Broadcast(Ticket);
	if (bAutoTravel && !GIsAutomationTesting && Ticket.IsValidFormat())
	{
		if (APlayerController* PC = Cast<APlayerController>(GetOwner()); PC && PC->IsLocalController() && PC->GetLocalPlayer() != nullptr)
		{
			const FString URL = BuildDungeonTravelURL(Ticket);
			if (auto* Online = GetWorld()->GetGameInstance()->GetSubsystem<UProject_JOnlineWorldSubsystem>();
				Ticket.DestinationChannel > 0 && Online && Online->HasOnlineClientSession())
			{
				Online->ConnectTravel(PC, Ticket.DestinationZone, Ticket.DestinationChannel, URL);
				return;
			}
			PC->ClientTravel(URL, ETravelType::TRAVEL_Absolute);
		}
	}
}

void UProject_JMMOClientBridgeComponent::ServerRequestDungeonReturn_Implementation()
{
	FGuid Character;
	if (!AdmitAuthoritativePlayer(false, Character)) return;
	auto* GI = GetWorld()->GetGameInstance();
	auto* Dungeon = GI ? GI->GetSubsystem<UProject_JDungeonInstanceSubsystem>() : nullptr;
	if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
	{
		FProject_JReturnAnchor Anchor;
		if (!Dungeon || !Dungeon->GetCharacterReturnAnchor(Character, Anchor))
		{
			ClientNotifyMMOActionResult(TEXT("DungeonReturn"), false, TEXT("ReturnUnavailable"));
			return;
		}
		const auto* Player = CastChecked<APlayerController>(GetOwner())->GetPlayerState<AProject_JPlayerState>();
		const auto Lease = Player->CaptureServerSession();
		const TWeakObjectPtr<const AProject_JPlayerState> WeakPlayer(Player);
		const TWeakObjectPtr<ThisClass> WeakThis(this);
		const TWeakObjectPtr<UProject_JDungeonInstanceSubsystem> WeakDungeon(Dungeon);
		const double Started = Dungeon->GetCurrentUtcEpochSeconds();
		auto Body = MakeShared<FJsonObject>();
		Body->SetStringField(TEXT("zone"), Anchor.ZoneId.ToString());
		UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("World"), TEXT("Channel_List"), Body, {},
			[WeakThis, WeakDungeon, WeakPlayer, Lease, Character, Zone = Anchor.ZoneId, Started](const TSharedPtr<FJsonObject>& Response)
			{
				auto* Self = WeakThis.Get();
				auto* Allocator = WeakDungeon.Get();
				const auto* State = WeakPlayer.Get();
				if (!Self || !Allocator || !State || !State->IsServerSessionCurrent(Lease)) return;
				const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
				FString Status;
				if (Response.IsValid() && Response->TryGetStringField(TEXT("status"), Status) && Status == TEXT("OK")
					&& Response->TryGetArrayField(TEXT("channels"), Rows) && Rows->Num() <= 128)
				{
					const double Now = Allocator->GetCurrentUtcEpochSeconds();
					TArray<FProject_JChannelInfo> Channels;
					for (const auto& Row : *Rows)
					{
						const TSharedPtr<FJsonObject>* Value = nullptr;
						double Remaining = 0;
						FProject_JChannelInfo Entry;
						if (!Row->TryGetObject(Value) || !Value || !(*Value)->TryGetNumberField(TEXT("channel_id"), Entry.ChannelId)
							|| !(*Value)->TryGetNumberField(TEXT("population"), Entry.CurrentCCU) || !(*Value)->TryGetNumberField(TEXT("capacity"), Entry.MaxCCU)
							|| !(*Value)->TryGetStringField(TEXT("endpoint"), Entry.ServerEndpoint) || !(*Value)->TryGetBoolField(TEXT("draining"), Entry.bDraining)
							|| !(*Value)->TryGetNumberField(TEXT("seconds_remaining"), Remaining)) continue;
						Entry.ZoneId = Zone;
						Entry.LastHeartbeatSeconds = Started - Allocator->ChannelHeartbeatTimeoutSeconds + FMath::Clamp(Remaining, 0., Allocator->ChannelHeartbeatTimeoutSeconds);
						Channels.Add(MoveTemp(Entry));
					}
					Allocator->ReplaceZoneChannels(Zone, MoveTemp(Channels));
					FProject_JDungeonJoinTicket Ticket;
					if (Allocator->TryIssueReturnTicket(Character, Now, Ticket, true))
					{
						Self->ClientReceiveDungeonTicket(Ticket, true);
						return;
					}
				}
				Self->ClientNotifyMMOActionResult(TEXT("DungeonReturn"), false, TEXT("ReturnUnavailable"));
			});
		return;
	}
	FProject_JDungeonJoinTicket Ticket;
	if (Dungeon && Dungeon->TryIssueReturnTicket(Character, Dungeon->GetCurrentUtcEpochSeconds(), Ticket))
		ClientReceiveDungeonTicket(Ticket, true);
	else
		ClientNotifyMMOActionResult(TEXT("DungeonReturn"), false, TEXT("ReturnUnavailable"));
}

void UProject_JMMOClientBridgeComponent::ServerRequestRevive_Implementation(EProject_JRevivePath Path)
{
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(false, CharId))
	{
		ClientNotifyMMOActionResult(TEXT("Revive"), false, TEXT("Denied"));
		return;
	}
	APlayerController* PC = Cast<APlayerController>(GetOwner());
	auto* Char = PC ? Cast<AProject_JBaseCharacter>(PC->GetPawn()) : nullptr;
	auto* Revive = GetWorld()->GetSubsystem<UProject_JDeathReviveSubsystem>();
	FVector ReviveLoc;
	const bool bOk = Revive && Char && Revive->ExecuteRevive(Char, Path, GetWorld()->GetTimeSeconds(), ReviveLoc);
	if (bOk)
	{
		if (auto* Integrity = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UProject_JServerIntegritySubsystem>() : nullptr)
		{
			Integrity->GrantMovementWhitelist(CharId, 2.0, GetWorld()->GetTimeSeconds());
		}
	}
	ClientNotifyMMOActionResult(TEXT("Revive"), bOk, bOk ? TEXT("Revived") : TEXT("ReviveRejected"));
}

void UProject_JMMOClientBridgeComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(OnlineDungeonPollTimer);
	}
	OnlineDungeonEntry.Reset();
	VisibleClaimableMail.Reset();
	bReadingInbox = false;
	bClaimingOnlineMail = false;
	Super::EndPlay(EndPlayReason);
}

void UProject_JMMOClientBridgeComponent::ServerCreateAuctionListing_Implementation(
	FGuid IdempotencyKey, FGuid ItemInstanceId, int64 BuyoutPrice, int64 /*ClientDepositFeeIgnored*/, float DurationSeconds)
{
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(true, CharId))
	{
		ClientNotifyMMOActionResult(TEXT("AuctionList"), false, TEXT("Denied"));
		return;
	}
	// Fail-closed in Online mode until persistent auction listing RPC is connected (prevents split memory authority).
	if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
	{
		ClientNotifyMMOActionResult(TEXT("AuctionList"), false, TEXT("OnlineRoutePending"));
		return;
	}
	auto* Econ = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UProject_JEconomySubsystem>() : nullptr;
	FGuid ListingId;
	// Always compute DepositFee server-side (5% listing fee, minimum 1) instead of trusting client input.
	const int64 ServerDepositFee = BuyoutPrice > 0 ? FMath::Max<int64>(1, BuyoutPrice / 20) : 0;
	const bool bOk = Econ && Econ->CreateAuctionListing(
		IdempotencyKey, CharId, ItemInstanceId, BuyoutPrice, ServerDepositFee,
		FMath::Clamp(static_cast<double>(DurationSeconds), 60.0, 172800.0), GetWorld()->GetTimeSeconds(), ListingId);
	ClientNotifyMMOActionResult(TEXT("AuctionList"), bOk, bOk ? TEXT("Listed") : TEXT("ListFailed"));
}

void UProject_JMMOClientBridgeComponent::ServerBuyAuctionListing_Implementation(FGuid IdempotencyKey, FGuid ListingId)
{
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(true, CharId))
	{
		ClientNotifyMMOActionResult(TEXT("AuctionBuy"), false, TEXT("Denied"));
		return;
	}
	if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
	{
		ClientNotifyMMOActionResult(TEXT("AuctionBuy"), false, TEXT("OnlineRoutePending"));
		return;
	}
	auto* Econ = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UProject_JEconomySubsystem>() : nullptr;
	const bool bOk = Econ && Econ->ExecuteAuctionBuyout(IdempotencyKey, ListingId, CharId, GetWorld()->GetTimeSeconds());
	ClientNotifyMMOActionResult(TEXT("AuctionBuy"), bOk, bOk ? TEXT("Purchased") : TEXT("PurchaseFailed"));
}

void UProject_JMMOClientBridgeComponent::ServerClaimMail_Implementation(FGuid MailId)
{
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(true, CharId))
	{
		ClientNotifyMMOActionResult(TEXT("MailClaim"), false, TEXT("Denied"));
		return;
	}
	if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
	{
		SubmitOnlineMailClaim(MailId, CharId);
		return;
	}
	auto* Econ = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UProject_JEconomySubsystem>() : nullptr;
	auto* PC = Cast<APlayerController>(GetOwner());
	auto* Player = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	const auto Lease = Player ? Player->CaptureServerSession() : ProjectJ::MMO::FServerSessionLease{};
	const TWeakObjectPtr<ThisClass> WeakSelf(this);
	const TWeakObjectPtr<AProject_JPlayerState> WeakPlayer(Player);
	if (Econ && Econ->SubmitMailClaim(MailId, Player, [WeakSelf, WeakPlayer, Lease](bool bOk)
	{
		auto* Self = WeakSelf.Get();
		auto* PS = WeakPlayer.Get();
		const auto* Controller = Self ? Cast<APlayerController>(Self->GetOwner()) : nullptr;
		if (Controller && PS && Controller->PlayerState == PS && PS->IsServerSessionCurrent(Lease))
			Self->ClientNotifyMMOActionResult(TEXT("MailClaim"), bOk, bOk ? TEXT("Claimed") : TEXT("ClaimFailed"));
	})) return;
	ClientNotifyMMOActionResult(TEXT("MailClaim"), false, TEXT("ClaimFailed"));
}

void UProject_JMMOClientBridgeComponent::SubmitOnlineMailClaim(FGuid MailId, FGuid Character)
{
	auto* Player = CastChecked<APlayerController>(GetOwner())->GetPlayerState<AProject_JPlayerState>();
	auto* Mode = GetWorld()->GetAuthGameMode<AProject_JGameMode>();
	const int64 Epoch = Mode ? Mode->GetOnlineEpoch(Player) : 0;
	if (bClaimingOnlineMail || Epoch <= 0 || MailOwner != Character || MailEpoch != Epoch || !VisibleClaimableMail.Contains(MailId))
	{
		ClientNotifyMMOActionResult(TEXT("MailClaim"), false, TEXT("Denied"));
		return;
	}
	const auto Lease = Player->CaptureServerSession();
	const TWeakObjectPtr<ThisClass> WeakSelf(this);
	const TWeakObjectPtr<AProject_JPlayerState> WeakPlayer(Player);
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(Character));
	Body->SetStringField(TEXT("session_epoch"), LexToString(Epoch));
	Body->SetStringField(TEXT("mail_id"), UProject_JOnlineWorldSubsystem::GuidText(MailId));
	bClaimingOnlineMail = true;
	const bool bStarted = UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("Mail"), TEXT("Claim"), Body, MailId,
		[WeakSelf, WeakPlayer, Lease, Body, MailId](const TSharedPtr<FJsonObject>& Response)
		{
			auto* Self = WeakSelf.Get();
			auto* State = WeakPlayer.Get();
			if (!Self) return;
			if (!State || !State->IsServerSessionCurrent(Lease)) { Self->bClaimingOnlineMail = false; return; }
			FString Status;
			if (!Response.IsValid() || !Response->TryGetStringField(TEXT("status"), Status) || (Status != TEXT("COMMITTED") && Status != TEXT("REPLAYED")))
			{
				Self->bClaimingOnlineMail = false;
				Self->ClientNotifyMMOActionResult(TEXT("MailClaim"), false, Status == TEXT("INVENTORY_FULL") ? TEXT("InventoryFull") : TEXT("ClaimFailed"));
				return;
			}
			const bool bReading = UProject_JOnlineWorldSubsystem::Submit(Self->GetWorld(), TEXT("World"), TEXT("Inventory"), Body, {},
				[WeakSelf, WeakPlayer, Lease, MailId](const TSharedPtr<FJsonObject>& Inventory)
				{
					auto* Bridge = WeakSelf.Get();
					auto* OwnerState = WeakPlayer.Get();
					if (!Bridge) return;
					Bridge->bClaimingOnlineMail = false;
					const auto Current = [WeakPlayer, Lease] { const auto* PS = WeakPlayer.Get(); return PS && PS->IsServerSessionCurrent(Lease); };
					if (!Current()) return;
					const bool bInstalled = Inventory.IsValid() && UProject_JOnlineWorldSubsystem::ApplyInventory(OwnerState, *Inventory, Current);
					if (bInstalled) Bridge->VisibleClaimableMail.Remove(MailId);
					if (Current() && WeakSelf.IsValid()) Bridge->ClientNotifyMMOActionResult(TEXT("MailClaim"), bInstalled, bInstalled ? TEXT("Claimed") : TEXT("RefreshPending"));
				});
			if (!bReading) { Self->bClaimingOnlineMail = false; Self->ClientNotifyMMOActionResult(TEXT("MailClaim"), false, TEXT("RefreshPending")); }
		});
	if (!bStarted) { bClaimingOnlineMail = false; ClientNotifyMMOActionResult(TEXT("MailClaim"), false, TEXT("ClaimFailed")); }
}

void UProject_JMMOClientBridgeComponent::ServerWithdrawGuildBank_Implementation(FGuid GuildId, int32 SlotIndex, int64 ExpectedRevision)
{
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(true, CharId))
	{
		ClientNotifyMMOActionResult(TEXT("GuildWithdraw"), false, TEXT("Denied"));
		return;
	}
	if (UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
	{
		ClientNotifyMMOActionResult(TEXT("GuildWithdraw"), false, TEXT("OnlineRoutePending"));
		return;
	}
	auto* Social = GetWorld()->GetGameInstance() ? GetWorld()->GetGameInstance()->GetSubsystem<UProject_JGuildAndSocialExtensionsSubsystem>() : nullptr;
	const FDateTime Utc = FDateTime::UtcNow();
	const int32 DayKey = Utc.GetYear() * 10000 + Utc.GetMonth() * 100 + Utc.GetDay();
	const bool bOk = Social && Social->WithdrawFromGuildBankToMail(GuildId, CharId, SlotIndex, ExpectedRevision, DayKey);
	ClientNotifyMMOActionResult(TEXT("GuildWithdraw"), bOk, bOk ? TEXT("Withdrawn") : TEXT("WithdrawRejected"));
}

void UProject_JMMOClientBridgeComponent::ServerBlockCharacter_Implementation(FGuid TargetCharacterId)
{
	SetChatBlock(TargetCharacterId, true);
}

void UProject_JMMOClientBridgeComponent::ServerUnblockCharacter_Implementation(FGuid TargetCharacterId)
{
	SetChatBlock(TargetCharacterId, false);
}

bool UProject_JMMOClientBridgeComponent::InstallBlockSnapshot(const FJsonObject& Snapshot)
{
	FString RevisionText;
	int64 Revision = 0;
	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Snapshot.TryGetStringField(TEXT("block_revision"), RevisionText) || !LexTryParseString(Revision, *RevisionText)
		|| Revision <= 0 || Revision < BlockRevision || !Snapshot.TryGetArrayField(TEXT("blocked_characters"), Rows)
		|| !Rows || Rows->Num() > 256) return false;
	TSet<FGuid> Received;
	for (const auto& Row : *Rows)
	{
		FString Text;
		FGuid Id;
		if (!Row || !Row->TryGetString(Text) || !FGuid::Parse(Text, Id) || !Id.IsValid() || Id == ChatOwner || Received.Contains(Id)) return false;
		Received.Add(Id);
	}
	BlockedSenders = MoveTemp(Received);
	BlockRevision = Revision;
	return true;
}

void UProject_JMMOClientBridgeComponent::InitializeOnlineChat(const FGuid& OwnerId, int64 Epoch, const FJsonObject& Login)
{
	ChatOwner = OwnerId;
	ChatEpoch = Epoch;
	BlockRevision = 0;
	BlockedSenders.Reset();
	UnconfirmedMute.Invalidate();
	bEditingBlock = false;
	if (!OwnerId.IsValid() || Epoch <= 0 || !InstallBlockSnapshot(Login))
		UE_LOG(LogTemp, Warning, TEXT("Online chat unavailable: login has no valid private block list."));
}

bool UProject_JMMOClientBridgeComponent::HasCurrentOnlineChat(const AProject_JGameMode* Mode, const AProject_JPlayerState* Player) const
{
	return Mode && IsValid(Player) && ChatOwner.IsValid() && ChatOwner == Player->GetCharacterId().Value
		&& ChatEpoch > 0 && ChatEpoch == Mode->GetOnlineEpoch(Player) && BlockRevision > 0;
}

void UProject_JMMOClientBridgeComponent::SetChatBlock(const FGuid& TargetCharacterId, bool bBlock)
{
	const FName Action = bBlock ? TEXT("Block") : TEXT("Unblock");
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(false, CharId) || !TargetCharacterId.IsValid() || TargetCharacterId == CharId)
	{
		ClientNotifyMMOActionResult(Action, false, TEXT("Denied"));
		return;
	}
	if (!UProject_JOnlineWorldSubsystem::UsesOnlineBackend(GetWorld()))
	{
		auto* Social = GetWorld()->GetGameInstance()->GetSubsystem<UProject_JGuildAndSocialExtensionsSubsystem>();
		const bool bOk = Social && (bBlock ? Social->BlockCharacter(CharId, TargetCharacterId) : Social->UnblockCharacter(CharId, TargetCharacterId));
		ClientNotifyMMOActionResult(Action, bOk, bOk ? (bBlock ? TEXT("Blocked") : TEXT("Unblocked")) : TEXT("Denied"));
		return;
	}
	auto* Player = CastChecked<APlayerController>(GetOwner())->GetPlayerState<AProject_JPlayerState>();
	auto* Mode = GetWorld()->GetAuthGameMode<AProject_JGameMode>();
	if (!HasCurrentOnlineChat(Mode, Player) || bEditingBlock
		|| (UnconfirmedMute.IsValid() && UnconfirmedMute != TargetCharacterId))
	{
		ClientNotifyMMOActionResult(Action, false, TEXT("Unavailable"));
		return;
	}
	auto Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("character_id"), UProject_JOnlineWorldSubsystem::GuidText(CharId));
	Body->SetStringField(TEXT("target"), UProject_JOnlineWorldSubsystem::GuidText(TargetCharacterId));
	Body->SetStringField(TEXT("session_epoch"), LexToString(ChatEpoch));
	Body->SetStringField(TEXT("block_revision"), LexToString(BlockRevision));
	Body->SetBoolField(TEXT("block"), bBlock);
	// Until a definitive reply, mute the target even if transport lost the acknowledgement.
	const FGuid PriorUnconfirmed = UnconfirmedMute;
	if (bBlock) UnconfirmedMute = TargetCharacterId;
	bEditingBlock = true;
	const auto Lease = Player->CaptureServerSession();
	const int64 SubmittedEpoch = ChatEpoch;
	const bool bSubmitted = UProject_JOnlineWorldSubsystem::Submit(GetWorld(), TEXT("World"), TEXT("Set_Chat_Block"), Body, FGuid::NewGuid(),
		[Weak = TWeakObjectPtr<ThisClass>(this), Owner = TWeakObjectPtr<AProject_JPlayerState>(Player), Lease, SubmittedEpoch, Action, TargetCharacterId, PriorUnconfirmed, bBlock](const TSharedPtr<FJsonObject>& Response)
		{
			auto* Self = Weak.Get();
			auto* PS = Owner.Get();
			auto* PC = Self ? Cast<APlayerController>(Self->GetOwner()) : nullptr;
			if (!PC || !PS || PC->PlayerState != PS || !PS->IsServerSessionCurrent(Lease)
				|| Self->ChatEpoch != SubmittedEpoch || Self->ChatOwner != Lease.CharacterId) return;
			Self->bEditingBlock = false;
			FString Status;
			if (Response) Response->TryGetStringField(TEXT("status"), Status);
			if (Status == TEXT("STALE_SESSION"))
			{
				Self->BlockRevision = 0;
				Self->BlockedSenders.Reset();
				Self->UnconfirmedMute.Invalidate();
				Self->ClientNotifyMMOActionResult(Action, false, TEXT("StaleSession"));
				return;
			}
			const bool bKnown = Response && (Status == TEXT("OK") || Status == TEXT("CONFLICT") || Status == TEXT("FULL") || Status == TEXT("INVALID"))
				&& Self->InstallBlockSnapshot(*Response);
			if (bKnown) Self->UnconfirmedMute.Invalidate();
			else if (Status == TEXT("PRE_DISPATCH_REJECTED")) Self->UnconfirmedMute = PriorUnconfirmed;
			const bool bOk = bKnown && (Status == TEXT("OK") || Status == TEXT("CONFLICT"))
				&& Self->BlockedSenders.Contains(TargetCharacterId) == bBlock;
			Self->ClientNotifyMMOActionResult(Action, bOk, bOk ? (bBlock ? TEXT("Blocked") : TEXT("Unblocked")) : TEXT("BlockNotConfirmed"));
		});
	if (!bSubmitted)
	{
		bEditingBlock = false;
		UnconfirmedMute = PriorUnconfirmed;
		ClientNotifyMMOActionResult(Action, false, TEXT("BlockNotConfirmed"));
	}
}

void UProject_JMMOClientBridgeComponent::ServerSendChatMessage_Implementation(
	FGuid OptionalRecipientCharId, EProject_JChatChannel Channel, const FString& Message)
{
	FGuid CharId;
	if (!AdmitAuthoritativePlayer(false, CharId))
	{
		ClientNotifyMMOActionResult(TEXT("Chat"), false, TEXT("Denied"));
		return;
	}
	UWorld* World = GetWorld();
	const APlayerController* SenderPC = Cast<APlayerController>(GetOwner());
	const bool bOnline = UProject_JOnlineWorldSubsystem::UsesOnlineBackend(World);
	const auto* Mode = bOnline ? World->GetAuthGameMode<AProject_JGameMode>() : nullptr;
	if (bOnline && !HasCurrentOnlineChat(Mode, SenderPC ? SenderPC->GetPlayerState<AProject_JPlayerState>() : nullptr))
	{
		ClientNotifyMMOActionResult(TEXT("Chat"), false, TEXT("Unavailable"));
		return;
	}
	UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	auto* Social = GI ? GI->GetSubsystem<UProject_JGuildAndSocialExtensionsSubsystem>() : nullptr;
	const bool bOk = Social && Social->AdmitChatMessage(CharId, OptionalRecipientCharId, Channel, Message, World->GetTimeSeconds());
	if (!bOk)
	{
		ClientNotifyMMOActionResult(TEXT("Chat"), false, TEXT("RateLimitedOrBlocked"));
		return;
	}

	// Deliver to relevant connected recipients in this world (respecting block list) instead of only echoing to sender.
	const auto* PartySub = GI ? GI->GetSubsystem<UProject_JSocialSubsystem>() : nullptr;
	const FName SenderPartyId = PartySub ? PartySub->GetPartyIdForCharacter(CharId) : NAME_None;
	const FName SenderGuildId = PartySub ? PartySub->GetGuildIdForCharacter(CharId) : NAME_None;
	const APawn* SenderPawn = SenderPC ? SenderPC->GetPawn() : nullptr;

	for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
	{
		APlayerController* TargetPC = It->Get();
		AProject_JPlayerState* TargetPS = TargetPC ? TargetPC->GetPlayerState<AProject_JPlayerState>() : nullptr;
		auto* TargetBridge = TargetPC ? TargetPC->FindComponentByClass<UProject_JMMOClientBridgeComponent>() : nullptr;
		if (!TargetPS || !TargetBridge || !TargetPS->GetCharacterId().IsValid()) continue;

		const FGuid TargetCharId = TargetPS->GetCharacterId().Value;
		if (bOnline && !TargetBridge->HasCurrentOnlineChat(Mode, TargetPS)) continue;
		if (TargetCharId != CharId && (bOnline ? (TargetBridge->BlockedSenders.Contains(CharId) || TargetBridge->UnconfirmedMute == CharId)
			: Social->IsBlockedBy(TargetCharId, CharId))) continue;

		bool bShouldDeliver = (TargetCharId == CharId);
		if (!bShouldDeliver)
		{
			switch (Channel)
			{
			case EProject_JChatChannel::Whisper:
				bShouldDeliver = OptionalRecipientCharId.IsValid() && TargetCharId == OptionalRecipientCharId;
				break;
			case EProject_JChatChannel::Party:
				bShouldDeliver = PartySub && !SenderPartyId.IsNone() && PartySub->GetPartyIdForCharacter(TargetCharId) == SenderPartyId;
				break;
			case EProject_JChatChannel::Guild:
				bShouldDeliver = PartySub && !SenderGuildId.IsNone() && PartySub->GetGuildIdForCharacter(TargetCharId) == SenderGuildId;
				break;
			case EProject_JChatChannel::Local:
				if (const APawn* TargetPawn = TargetPC->GetPawn(); SenderPawn && TargetPawn)
				{
					bShouldDeliver = FVector::DistSquared(SenderPawn->GetActorLocation(), TargetPawn->GetActorLocation()) <= FMath::Square(3500.0);
				}
				break;
			case EProject_JChatChannel::World:
				bShouldDeliver = true;
				break;
			default:
				break;
			}
		}

		if (bShouldDeliver)
		{
			TargetBridge->ClientReceiveChatMessage(CharId, Channel, Message);
		}
	}
}

void UProject_JMMOClientBridgeComponent::ClientReceiveChatMessage_Implementation(FGuid SenderCharId, EProject_JChatChannel Channel, const FString& Message)
{
	OnChatMessageReceived.Broadcast(SenderCharId, Channel, Message);
}

void UProject_JMMOClientBridgeComponent::ServerRequestGatherNode_Implementation(
	FGuid, EProject_JResourceNodeMode, int32, int32, float)
{
	FGuid CharId;
	if (AdmitAuthoritativePlayer(true, CharId))
		ClientNotifyMMOActionResult(TEXT("Gather"), false, TEXT("UseWorldInteraction"));
}

void UProject_JMMOClientBridgeComponent::ClientNotifyMMOActionResult_Implementation(FName ActionName, bool bSucceeded, FName ReasonCode)
{
	OnMMOActionResult.Broadcast(ActionName, bSucceeded, ReasonCode);
}
