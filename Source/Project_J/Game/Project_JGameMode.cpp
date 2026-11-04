// Copyright Epic Games, Inc. All Rights Reserved.

#include "Project_JGameMode.h"
#include "Game/Project_JPlayerRetirement.h"
#include "GameFramework/GameSession.h"
#include "Engine/World.h"
#include "Project_JGameState.h"
#include "Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Engine/GameInstance.h"
#include "Project_JAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "GameFramework/Pawn.h"
#include "UObject/StrongObjectPtr.h"

AProject_JGameMode::AProject_JGameMode()
{
	GameStateClass = AProject_JGameState::StaticClass();
	PlayerStateClass = AProject_JPlayerState::StaticClass();
}

void AProject_JGameMode::InitGameState()
{
	const bool bCapacityConfigured = SessionGate.SetCapacity(FMath::Max(1, MaxTrustedSessions));
	ensureMsgf(bCapacityConfigured, TEXT("Trusted session capacity must be configured before any login"));
	Super::InitGameState();

	if (AProject_JGameState* ProjectGameState = GetGameState<AProject_JGameState>())
	{
		ProjectGameState->SetWorldInstanceId(CreatePrototypeWorldInstanceId());
	}
}

void AProject_JGameMode::PostLogin(APlayerController* NewPlayer)
{
	if (bRequireTrustedSessions)
	{
		if (auto* State = IsValid(NewPlayer) ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr) State->RequireServerSession(this);
		Super::PostLogin(NewPlayer); // Normal native login; starting-player admission below holds the Pawn.
		return;
	}
	FString LocalProfile;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), LocalProfile))
	{
		auto* State = NewPlayer ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr;
		TWeakObjectPtr<AProject_JGameMode> WeakThis(this); TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);
		auto Ready = [WeakThis, WeakPlayer](bool Success)
		{
			auto* Mode = WeakThis.Get(); auto* PC = WeakPlayer.Get();
			if (!IsValid(Mode) || Mode->IsActorBeingDestroyed() || !IsValid(PC) || PC->IsActorBeingDestroyed()) return;
			if (Success) Mode->HandleStartingNewPlayer(PC);
			else
			{
				auto* Status=Mode->GetGameInstance()?Mode->GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>():nullptr;
				const FText Reason=Status?Status->GetStatusText(PC->GetPlayerState<AProject_JPlayerState>()):FText::GetEmpty();
				PC->ClientReturnToMainMenuWithTextReason(Reason.IsEmpty()?NSLOCTEXT("LocalProfile", "LoadFailed", "Local profile could not be loaded. Existing saved data was preserved."):Reason);
			}
		};
#if UE_BUILD_SHIPPING
		Ready(false); // Local names never establish production account authority.
#else
		if (Profiles && State) Profiles->OpenProfile(State, LocalProfile, MoveTemp(Ready));
		else Ready(false);
#endif
		Super::PostLogin(NewPlayer); // Starting-player override holds spawn until restoration completes.
		return;
	}
	Super::PostLogin(NewPlayer);

	AProject_JPlayerState* ProjectPlayerState = NewPlayer ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!ProjectPlayerState)
	{
		return;
	}

	if (!ProjectPlayerState->GetAccountId().IsValid() || !ProjectPlayerState->GetCharacterId().IsValid())
	{
		AssignPrototypeIdentity(*ProjectPlayerState);
	}
}

void AProject_JGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	if (bRequireTrustedSessions)
	{
		const auto* State = IsValid(NewPlayer) ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		if (!IsValid(State) || !State->CanAcceptSessionCommand()) return;
	}
	if (UProject_JLocalProfileSubsystem::IsTrustedDevelopmentEnabled())
	{
		auto* State = IsValid(NewPlayer) ? NewPlayer->GetPlayerState<AProject_JPlayerState>() : nullptr;
		auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr;
		if (!bRequireTrustedSessions || !Profiles || !IsValid(State)) return;
		if (!Profiles->IsReady(State))
		{
			if (Profiles->HasProfileBinding(State)) return; // Loading/failed: never spawn fallback data.
			const auto Lease = State->CaptureServerSession();
			TWeakObjectPtr<AProject_JGameMode> WeakMode(this);
			TWeakObjectPtr<APlayerController> WeakPlayer(NewPlayer);
			TWeakObjectPtr<AProject_JPlayerState> WeakState(State);
			Profiles->OpenTrustedDevelopmentProfile(State, [WeakMode, WeakPlayer, WeakState, Lease](bool Success)
			{
				auto* Mode = WeakMode.Get(); auto* PC = WeakPlayer.Get(); auto* PS = WeakState.Get();
				if (Success && IsValid(Mode) && !Mode->IsActorBeingDestroyed() && IsValid(PC) && !PC->IsActorBeingDestroyed()
					&& IsValid(PS) && PC->GetPlayerState<AProject_JPlayerState>() == PS && PS->IsServerSessionCurrent(Lease))
					Mode->HandleStartingNewPlayer(PC);
			});
			return;
		}
	}
	FString LocalProfile;
	if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), LocalProfile))
	{
		auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr;
		if (!Profiles || !NewPlayer || !Profiles->IsReady(NewPlayer->GetPlayerState<AProject_JPlayerState>())) return;
	}
	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
}
void AProject_JGameMode::Logout(AController* Exiting)
{
	if (auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr)
	{
		auto* State = Exiting ? Exiting->GetPlayerState<AProject_JPlayerState>() : nullptr;
		Profiles->RequestCheckpoint(State);
		Profiles->Detach(State);
	}
	TArray<ProjectJ::MMO::FServerSessionAttempt> Attempts;
	for (const auto& Pair : PendingSessions) if (Pair.Value.Controller.Get() == Exiting) Attempts.Add(Pair.Value.Attempt);
	for (const auto& Attempt : Attempts) CancelTrustedSession(Attempt);
	TArray<FBoundSession> Bindings;
	for (const auto& Pair : BoundSessions) if (Pair.Value.Controller.Get() == Exiting) Bindings.Add(Pair.Value);
	for (const auto& Binding : Bindings) RevokeTrustedSession(Binding.State.Get(), Binding.Lease);
	Super::Logout(Exiting);
}

bool AProject_JGameMode::IsSessionPlayer(const APlayerController* Player, const AProject_JPlayerState* State) const
{
	return !bSessionsClosing && IsValid(this) && !IsActorBeingDestroyed() && HasAuthority() && GetWorld() && !GetWorld()->bIsTearingDown
		&& GetWorld()->GetAuthGameMode() == this && IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->HasAuthority()
		&& Player->GetWorld() == GetWorld() && IsValid(State) && !State->IsActorBeingDestroyed() && State->HasAuthority()
		&& State->GetWorld() == GetWorld() && Player->GetPlayerState<AProject_JPlayerState>() == State && State->GetOwner() == Player;
}

bool AProject_JGameMode::CanBindSessionIdentity(const APlayerController* Player, const AProject_JPlayerState* State,
	const FGuid& Account, const FGuid& Character) const
{
	if (!IsSessionPlayer(Player, State)) return false;
	if (State->ServerSessionLease.IsValid())
		return State->ServerSessionAuthority.Get() == this && State->ServerSessionLease.AccountId == Account
			&& State->ServerSessionLease.CharacterId == Character && State->GetAccountId().Value == Account && State->GetCharacterId().Value == Character;
	// First binding never adopts an existing prototype/profile character's data.
	// A different character needs a fresh state; this API performs no migration.
	return !Player->GetPawn() && !State->GetPawn() && !State->GetAccountId().IsValid() && !State->GetCharacterId().IsValid();
}

bool AProject_JGameMode::BeginTrustedSession(APlayerController* Player, const FProject_JAccountId& Account,
	const FProject_JCharacterId& Character, ProjectJ::MMO::FServerSessionAttempt& OutAttempt)
{
	check(IsInGameThread());
	OutAttempt = {};
	auto* State = IsValid(Player) ? Player->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (!Account.IsValid() || !Character.IsValid() || !CanBindSessionIdentity(Player, State, Account.Value, Character.Value)) return false;
	// One in-flight attempt per connection. Superseded/disconnected owners do not
	// retain a lifetime-account tombstone or a physical adapter reservation here.
	TArray<ProjectJ::MMO::FServerSessionAttempt> Previous;
	for (const auto& Pair : PendingSessions)
		if (Pair.Value.Controller.Get() == Player || !Pair.Value.Controller.IsValid() || !Pair.Value.State.IsValid()) Previous.Add(Pair.Value.Attempt);
	for (const auto& Attempt : Previous) CancelTrustedSession(Attempt);
	if (SessionGate.Begin(Account.Value, Character.Value, OutAttempt) != ProjectJ::MMO::EAdmission::Accepted) return false;
	PendingSessions.Add(Account.Value, {OutAttempt, Player, State});
	State->RequireServerSession(this);
	return true;
}

bool AProject_JGameMode::CancelTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt)
{
	check(IsInGameThread());
	const auto* Found = PendingSessions.Find(Attempt.AccountId);
	if (!Found || !(Found->Attempt == Attempt)) return false;
	PendingSessions.Remove(Attempt.AccountId);
	return SessionGate.Cancel(Attempt);
}

bool AProject_JGameMode::CompleteTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt, bool bOwnershipVerified,
	ProjectJ::MMO::FServerSessionLease& OutLease)
{
	check(IsInGameThread());
	OutLease = {};
	const auto* Found = PendingSessions.Find(Attempt.AccountId);
	if (!Found || !(Found->Attempt == Attempt)) return false;
	const FPendingSession Pending = *Found;
	const TStrongObjectPtr<APlayerController> Player(Pending.Controller.Get());
	const TStrongObjectPtr<AProject_JPlayerState> State(Pending.State.Get());
	if (!bOwnershipVerified || !CanBindSessionIdentity(Player.Get(), State.Get(), Attempt.AccountId, Attempt.CharacterId)) { CancelTrustedSession(Attempt); return false; }
	const auto PreviousLease = State->CaptureServerSession();
	const TStrongObjectPtr<UProject_JLocalProfileSubsystem> Profiles(GetGameInstance()
		? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr);
	if (PreviousLease.IsValid() && Profiles && !Profiles->CanRenewTrustedProfile(State.Get()))
	{ CancelTrustedSession(Attempt); return false; } // Keep the old lease; retry after its accepted I/O drains.
	ProjectJ::MMO::FServerSessionLease Lease, Replaced;
	if (SessionGate.Accept(Attempt, Lease, Replaced) != ProjectJ::MMO::EAdmission::Accepted)
	{ CancelTrustedSession(Attempt); return false; }
	PendingSessions.Remove(Attempt.AccountId);
	TArray<FBoundSession> Retired;
	for (const auto& Pair : BoundSessions)
		if (Pair.Value.Lease == Replaced || Pair.Value.Controller == Pending.Controller || Pair.Value.State == Pending.State) Retired.Add(Pair.Value);
	for (const auto& Binding : Retired)
	{
		BoundSessions.Remove(Binding.Lease.AccountId);
		SessionGate.Release(Binding.Lease); // A same-account replacement already revoked this exact epoch.
	}
	// Publish the new native owner before callbacks. A nested acceptance can now
	// replace it; outer cleanup/install must never overwrite that newer lease.
	BoundSessions.Add(Lease.AccountId, {Lease, Pending.Controller, Pending.State});
	for (const auto& Binding : Retired) RetireSession(Binding, Binding.Controller != Pending.Controller, Binding.Controller == Pending.Controller && Binding.State == Pending.State);
	if (!IsTrustedSessionCurrent(State.Get(), Lease)) { RevokeTrustedSession(State.Get(), Lease); return false; }
	State->InstallServerSession(this, Lease);
	FProject_JAccountId Account; Account.Value = Lease.AccountId;
	FProject_JCharacterId Character; Character.Value = Lease.CharacterId;
	State->SetIdentity(Account, Character);
	if (!State->IsServerSessionCurrent(Lease)) { RevokeTrustedSession(State.Get(), Lease); return false; }
	if (PreviousLease.IsValid() && Profiles && !Profiles->RenewTrustedProfile(State.Get(), PreviousLease, Lease))
	{ RevokeTrustedSession(State.Get(), Lease); return false; }
	OutLease = Lease;
	return true;
}

bool AProject_JGameMode::IsTrustedSessionCurrent(const AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& Lease) const
{
	check(IsInGameThread());
	const auto* Found = BoundSessions.Find(Lease.AccountId);
	return Found && Found->Lease == Lease && Found->State.Get() == Player
		&& IsSessionPlayer(Found->Controller.Get(), Player) && SessionGate.IsCurrent(Lease);
}

bool AProject_JGameMode::RevokeTrustedSession(AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& ExpectedLease)
{
	check(IsInGameThread());
	if (!ExpectedLease.IsValid()) return false;
	const auto* Found = BoundSessions.Find(ExpectedLease.AccountId);
	if (!Found || !(Found->Lease == ExpectedLease) || Found->State.Get() != Player) return false;
	const FBoundSession Retired = *Found;
	BoundSessions.Remove(ExpectedLease.AccountId);
	SessionGate.Release(ExpectedLease);
	RetireSession(Retired);
	return true;
}

void AProject_JGameMode::PlayerSessionEnded(AProject_JPlayerState* Player)
{
	// Terminal owner cleanup, separate from public exact-lease logout. An old
	// empty capture cannot cancel a newer pending authentication attempt.
	TArray<ProjectJ::MMO::FServerSessionAttempt> Attempts;
	for (const auto& Pair : PendingSessions) if (Pair.Value.State.Get() == Player) Attempts.Add(Pair.Value.Attempt);
	for (const auto& Attempt : Attempts) CancelTrustedSession(Attempt);
	TArray<FBoundSession> Bindings;
	for (const auto& Pair : BoundSessions) if (Pair.Value.State.Get() == Player) Bindings.Add(Pair.Value);
	for (const auto& Binding : Bindings) RevokeTrustedSession(Player, Binding.Lease);
}

void AProject_JGameMode::RetireSession(const FBoundSession& Binding, bool bTerminateConnection, bool bPreserveProfileForRenewal)
{
	const TStrongObjectPtr<AProject_JPlayerState> State(Binding.State.Get());
	const TStrongObjectPtr<APlayerController> Player(Binding.Controller.Get());
	auto StillOwns = [&] { return IsValid(State.Get()) && State->OwnsServerSession(this, Binding.Lease); };
	if (!StillOwns()) return; // Provisional/unpublished or already replaced by a callback.
	State->bServerSessionRevoked = true;
	if (!bPreserveProfileForRenewal)
		if (auto* Profiles = GetGameInstance() ? GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>() : nullptr) Profiles->Detach(State.Get());
	if (!StillOwns()) return;
	if (bTerminateConnection && IsValid(Player.Get()))
	{
		const bool bRetired = ProjectJ::Lifecycle::RetireConnectionAvatar(Player.Get(), StillOwns);
		if (!StillOwns()) return;
		if (bRetired)
		{
			if (!GameSession || !GameSession->KickPlayer(Player.Get(), FText::FromString(TEXT("Session replaced")))) Player->Destroy();
			return;
		}
	}
	const TStrongObjectPtr<APawn> ControlledPawn(IsValid(Player.Get()) ? Player->GetPawn() : nullptr);
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(State->GetProjectJAbilitySystemComponent());
	const TStrongObjectPtr<APawn> Pawn(IsValid(ASC.Get()) ? Cast<APawn>(ASC->GetAvatarActor()) : nullptr);
	const uint64 Revision = IsValid(ASC.Get()) ? ASC->GetActorInfoRevision() : 0;
	auto SameAvatar = [&]
	{
		return StillOwns() && IsValid(ASC.Get()) && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging()
			&& ASC->GetOwnerActor() == State.Get() && ASC->GetAvatarActor() == Pawn.Get() && ASC->GetActorInfoRevision() == Revision;
	};
	TArray<FGameplayAbilitySpecHandle> Executions;
	if (IsValid(Pawn.Get()) && SameAvatar())
		for (const auto& Spec : ASC->GetActivatableAbilities()) if (Spec.IsActive()) Executions.Add(Spec.Handle);
	// Native possession is its own input authority; ordinary RPC budgets alone
	// cannot revoke Mover's controller input. UnPossessed retires owned PawnScope,
	// sprint and input bindings through the existing character lifecycle.
	if (StillOwns() && IsValid(Player.Get()) && IsValid(ControlledPawn.Get()) && Player->GetPlayerState<AProject_JPlayerState>() == State.Get()
		&& Player->GetPawn() == ControlledPawn.Get() && ControlledPawn->GetController() == Player.Get() && ControlledPawn->GetPlayerState() == State.Get()) Player->UnPossess();
	for (const auto& Handle : Executions)
	{
		if (!SameAvatar()) return;
		ASC->CancelAbilityHandle(Handle);
	}
	if (IsValid(Pawn.Get()) && SameAvatar()) ASC->ClearActorInfo();
	if (bTerminateConnection && StillOwns() && IsValid(Player.Get()))
	{
		if (!GameSession || !GameSession->KickPlayer(Player.Get(), FText::FromString(TEXT("Session replaced")))) Player->Destroy();
	}
}

void AProject_JGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	bSessionsClosing = true;
	SessionGate.Close();
	TArray<FBoundSession> Retired; BoundSessions.GenerateValueArray(Retired);
	BoundSessions.Reset(); PendingSessions.Reset();
	for (const auto& Binding : Retired) RetireSession(Binding);
	Super::EndPlay(Reason);
}

FProject_JWorldInstanceId AProject_JGameMode::CreatePrototypeWorldInstanceId() const
{
	const FString MapName = GetWorld() ? GetWorld()->GetMapName() : FString(TEXT("UnknownMap"));

	FProject_JWorldInstanceId WorldInstanceId;
	WorldInstanceId.WorldId = FName(TEXT("PrototypeWorld"));
	WorldInstanceId.ZoneId = FName(*FPaths::GetBaseFilename(MapName));
	WorldInstanceId.InstanceId = FName(*FGuid::NewGuid().ToString(EGuidFormats::DigitsWithHyphensLower));
	WorldInstanceId.ChannelId = FName(TEXT("Default"));
	return WorldInstanceId;
}

void AProject_JGameMode::AssignPrototypeIdentity(AProject_JPlayerState& ProjectPlayerState) const
{
	ProjectPlayerState.SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
}
