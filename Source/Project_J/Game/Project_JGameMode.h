// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "Project_JMMOTypes.h"
#include "MMO/Coordination.h"
#include "Project_JGameMode.generated.h"

class AProject_JPlayerState;

/**
 *  Simple GameMode for a third person game
 */
UCLASS(abstract)
class AProject_JGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	
	/** Constructor */
	AProject_JGameMode();

	virtual void InitGameState() override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	/** Native trusted-server entry, never an RPC. The adapter must already have
	 * verified Account's principal before Begin, then verify character ownership
	 * before Complete(true). Prototype PostLogin identities do not imply this.
	 * Begin must precede asynchronous ownership verification. */
	bool BeginTrustedSession(APlayerController* Player, const FProject_JAccountId& Account,
		const FProject_JCharacterId& Character, ProjectJ::MMO::FServerSessionAttempt& OutAttempt);
	bool CompleteTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt, bool bOwnershipVerified,
		ProjectJ::MMO::FServerSessionLease& OutLease);
	bool CancelTrustedSession(const ProjectJ::MMO::FServerSessionAttempt& Attempt);
	bool RevokeTrustedSession(AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& ExpectedLease);
	bool IsTrustedSessionCurrent(const AProject_JPlayerState* Player, const ProjectJ::MMO::FServerSessionLease& Lease) const;
	/** Opt-in server policy: login waits without prototype identity/Pawn until a
	 * trusted adapter completes ownership verification. No verifier is supplied.
	 * After Complete, the adapter calls normal HandleStartingNewPlayer. */
	UPROPERTY(EditDefaultsOnly, Category="MMO|Session")
	bool bRequireTrustedSessions = false;
	/** Applied once during InitGameState, before login; not a live resize control. */
	UPROPERTY(EditDefaultsOnly, Category="MMO|Session", meta=(ClampMin="1"))
	int32 MaxTrustedSessions = 128;

protected:
	virtual FProject_JWorldInstanceId CreatePrototypeWorldInstanceId() const;
	virtual void AssignPrototypeIdentity(AProject_JPlayerState& ProjectPlayerState) const;

private:
	friend class AProject_JPlayerState;
	void PlayerSessionEnded(AProject_JPlayerState* Player);
	struct FPendingSession
	{
		ProjectJ::MMO::FServerSessionAttempt Attempt;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<AProject_JPlayerState> State;
	};
	struct FBoundSession
	{
		ProjectJ::MMO::FServerSessionLease Lease;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<AProject_JPlayerState> State;
	};
	void RetireSession(const FBoundSession& Binding);
	bool IsSessionPlayer(const APlayerController* Player, const AProject_JPlayerState* State) const;
	bool CanBindSessionIdentity(const APlayerController* Player, const AProject_JPlayerState* State,
		const FGuid& Account, const FGuid& Character) const;
	ProjectJ::MMO::FServerSessionGate SessionGate;
	TMap<FGuid, FPendingSession> PendingSessions;
	TMap<FGuid, FBoundSession> BoundSessions;
	bool bSessionsClosing = false;
};



