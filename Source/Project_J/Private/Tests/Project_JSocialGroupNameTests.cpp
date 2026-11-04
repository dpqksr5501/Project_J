#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Game/Project_JPlayerState.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Social/Project_JSocialSubsystem.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJSocialGroupNameTest, "ProjectJ.MMO.Social.GroupNameLifetime",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJSocialGroupNameTest::RunTest(const FString&)
{
	TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UProject_JSocialSubsystem> Social(NewObject<UProject_JSocialSubsystem>(GI.Get()));
	TStrongObjectPtr<AProject_JPlayerState> A(NewObject<AProject_JPlayerState>()), B(NewObject<AProject_JPlayerState>());
	A->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
	B->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
	const auto First = Social->CreateParty(A.Get());
	const auto Second = Social->CreateParty(B.Get());
	if (!TestTrue(TEXT("Real create paths issue two distinct groups"), First.bSucceeded && Second.bSucceeded && First.GroupId != Second.GroupId)) return false;
	TestTrue(TEXT("Same kind shares one interned namespace string"), First.GroupId.GetComparisonIndex() == Second.GroupId.GetComparisonIndex());
	TestTrue(TEXT("Each real group owns a distinct positive internal FName number"), First.GroupId.GetNumber() == 1 && Second.GroupId.GetNumber() == 2);
	TestTrue(TEXT("Opaque ID string round-trips through existing FName serialization form"), FName(*First.GroupId.ToString()) == First.GroupId && FName(*Second.GroupId.ToString()) == Second.GroupId);

	FProject_JSocialGroupSnapshot Original, Retired;
	if (!TestTrue(TEXT("The original revisioned group snapshot is readable"), Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, First.GroupId, Original))) return false;
	TestTrue(TEXT("Identical restore is still idempotent"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Original));
	TestTrue(TEXT("Last member may leave through the real public path"), Social->LeaveParty(A.Get()).bSucceeded);
	TestTrue(TEXT("Existing tombstone/revision contract remains readable"),
		Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, First.GroupId, Retired) && Retired.bDeleted && Retired.Members.IsEmpty() && Retired.Revision > Original.Revision);
	TestFalse(TEXT("Old live snapshot cannot resurrect a deleted group"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Original));
	TestTrue(TEXT("Identical tombstone restore stays idempotent"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Retired));
	const auto Third = Social->CreateParty(A.Get());
	TestTrue(TEXT("A later group never reuses the retired ID"), Third.bSucceeded && Third.GroupId != First.GroupId && Third.GroupId.GetNumber() == 3 && Third.GroupId.GetComparisonIndex() == First.GroupId.GetComparisonIndex());

	const auto Guild = Social->CreateGuild(A.Get());
	TestTrue(TEXT("Kinds retain independent namespaces and sequences"), Guild.bSucceeded && Guild.GroupId.GetNumber() == 1 && Guild.GroupId.GetComparisonIndex() != First.GroupId.GetComparisonIndex());
	TStrongObjectPtr<UGameInstance> OtherGI(NewObject<UGameInstance>(GEngine));
	TStrongObjectPtr<UProject_JSocialSubsystem> OtherSocial(NewObject<UProject_JSocialSubsystem>(OtherGI.Get()));
	TStrongObjectPtr<AProject_JPlayerState> OtherLeader(NewObject<AProject_JPlayerState>());
	OtherLeader->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
	const auto Other = OtherSocial->CreateParty(OtherLeader.Get());
	TestTrue(TEXT("A different GI never restarts the same group namespace"), Other.bSucceeded && Other.GroupId.GetNumber() == 1 && Other.GroupId.GetComparisonIndex() != First.GroupId.GetComparisonIndex());

	Social->LeaveParty(A.Get());
	const int32 GroupsBeforeChurn = Social->Parties.Num();
	int32 PreviousNumber = Third.GroupId.GetNumber();
	for (int32 Index = 0; Index < 32; ++Index)
	{
		const auto Created = Social->CreateParty(A.Get());
		if (!TestTrue(TEXT("Repeated real create reuses only the namespace string, never a numbered identity"),
			Created.bSucceeded && Created.GroupId.GetComparisonIndex() == First.GroupId.GetComparisonIndex() && Created.GroupId.GetNumber() == PreviousNumber + 1 && FName(*Created.GroupId.ToString()) == Created.GroupId)) return false;
		PreviousNumber = Created.GroupId.GetNumber();
		if (!TestTrue(TEXT("Repeated last-leave uses the existing authoritative path"), Social->LeaveParty(A.Get()).bSucceeded)) return false;
	}
	TestEqual(TEXT("This change deliberately retains all revision tombstones"), Social->Parties.Num(), GroupsBeforeChurn + 32);
	TestTrue(TEXT("Inactive owner leaves no character membership"), Social->GetPartyIdForCharacter(A->GetCharacterId().Value).IsNone());

	// A trusted snapshot can reserve an as-yet unissued number in our namespace.
	// Reserving its high-water mark prevents a future local create from replacing it.
	FProject_JSocialGroupSnapshot Future;
	Future.GroupId = FName(First.GroupId, 512);
	Future.LeaderCharacterId = FGuid::NewGuid();
	Future.Members.Add(Future.LeaderCharacterId);
	if (!TestTrue(TEXT("Trusted future-number snapshot restores through the normal API"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Future))) return false;
	const auto AfterRestore = Social->CreateParty(A.Get());
	TestTrue(TEXT("Local issuance advances past restored names in the same namespace"), AfterRestore.bSucceeded && AfterRestore.GroupId.GetNumber() == 513 && AfterRestore.GroupId != Future.GroupId);
	FProject_JSocialGroupSnapshot Preserved;
	TestTrue(TEXT("A local create never overwrites the trusted restored group"), Social->GetGroupSnapshot(EProject_JSocialGroupKind::Party, Future.GroupId, Preserved) && Preserved.Members == Future.Members && Preserved.LeaderCharacterId == Future.LeaderCharacterId);
	auto InvalidFuture = Future;
	InvalidFuture.GroupId = FName(First.GroupId, 700);
	InvalidFuture.Members.Add(InvalidFuture.LeaderCharacterId);
	TestFalse(TEXT("Invalid restoration cannot reserve a future sequence number"), Social->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, InvalidFuture));
	TestEqual(TEXT("Rejected input leaves the issuance high-water mark unchanged"), Social->PartyIds.LastIssued, 513);
	Social->LeaveParty(A.Get());

	// The final legal internal number is emitted once. Exhaustion must not wrap,
	// rotate namespace, or mutate existing group/membership state.
	Social->PartyIds.LastIssued = MAX_int32 - 1;
	const auto Last = Social->CreateParty(A.Get());
	TestTrue(TEXT("The final valid internal number is usable and string-round-trippable"), Last.bSucceeded && Last.GroupId.GetNumber() == MAX_int32 && FName(*Last.GroupId.ToString()) == Last.GroupId);
	Social->LeaveParty(A.Get());
	const int32 GroupsAtExhaustion = Social->Parties.Num();
	const auto Exhausted = Social->CreateParty(A.Get());
	TestTrue(TEXT("Identity sequence exhaustion uses the existing fail-closed monotonic-state result"),
		!Exhausted.bSucceeded && Exhausted.Failure == EProject_JSocialOperationFailure::RevisionExhausted && Exhausted.GroupId.IsNone());
	TestTrue(TEXT("Exhaustion neither wraps nor partially creates a group"), Social->PartyIds.LastIssued == MAX_int32 && Social->Parties.Num() == GroupsAtExhaustion && Social->GetPartyIdForCharacter(A->GetCharacterId().Value).IsNone());

	// Keep all original namespace/churn/exhaustion assertions above. A separate
	// tiny per-instance ceiling exercises admission without a large churn run.
	{
		TStrongObjectPtr<UGameInstance> CapacityGI(NewObject<UGameInstance>(GEngine));
		TStrongObjectPtr<UProject_JSocialSubsystem> Bounded(NewObject<UProject_JSocialSubsystem>(CapacityGI.Get()));
		Bounded->MaxTrackedGroupsPerKind = 2;
		TStrongObjectPtr<AProject_JPlayerState> CA(NewObject<AProject_JPlayerState>()), CB(NewObject<AProject_JPlayerState>()), CC(NewObject<AProject_JPlayerState>());
		CA->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
		CB->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
		CC->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
		int32 Notifications = 0;
		const auto NotifyA = CA->OnPartyRosterChanged.AddLambda([&] { ++Notifications; });
		const auto NotifyB = CB->OnPartyRosterChanged.AddLambda([&] { ++Notifications; });
		const auto NotifyC = CC->OnPartyRosterChanged.AddLambda([&] { ++Notifications; });
		ON_SCOPE_EXIT { CA->OnPartyRosterChanged.Remove(NotifyA); CB->OnPartyRosterChanged.Remove(NotifyB); CC->OnPartyRosterChanged.Remove(NotifyC); };
		const auto FirstBounded = Bounded->CreateParty(CA.Get());
		FProject_JSocialGroupSnapshot BeforeDelete, Tombstone;
		if (!TestTrue(TEXT("Tiny-cap first real party exposes its original snapshot"), FirstBounded.bSucceeded && Bounded->GetGroupSnapshot(EProject_JSocialGroupKind::Party, FirstBounded.GroupId, BeforeDelete))) return false;
		TestTrue(TEXT("Last leave consumes history rather than returning admission capacity"), Bounded->LeaveParty(CA.Get()).bSucceeded);
		const auto SecondBounded = Bounded->CreateParty(CB.Get());
		if (!TestTrue(TEXT("One tombstone plus one live group fills the per-kind limit"), SecondBounded.bSucceeded && Bounded->Parties.Num() == 2)) return false;
		FProject_JSocialGroupSnapshot FullBefore, FullAfter;
		Bounded->GetGroupSnapshot(EProject_JSocialGroupKind::Party, SecondBounded.GroupId, FullBefore);
		const FName NamespaceBefore = Bounded->PartyIds.Namespace;
		const int32 IssuedBefore = Bounded->PartyIds.LastIssued, OnlineBefore = Bounded->OnlinePlayers.Num(), NotifyBefore = Notifications;
		const auto Refused = Bounded->CreateParty(CC.Get());
		TestTrue(TEXT("Full history returns the appended typed admission failure"), !Refused.bSucceeded && Refused.Failure == EProject_JSocialOperationFailure::HistoryCapacity && Refused.GroupId.IsNone());
		TestTrue(TEXT("Failed create leaves sequence namespace maps projection and notifications unchanged"),
			Bounded->PartyIds.Namespace == NamespaceBefore && Bounded->PartyIds.LastIssued == IssuedBefore && Bounded->Parties.Num() == 2 &&
			Bounded->CharacterParties.Num() == 1 && Bounded->OnlinePlayers.Num() == OnlineBefore && CC->GetPartyId_Implementation().IsNone() &&
			CB->GetPartyId_Implementation() == SecondBounded.GroupId && CB->GetPartyRoster().Num() == 1 && Notifications == NotifyBefore);
		TestTrue(TEXT("Existing validation still precedes the history admission gate"), Bounded->CreateParty(CB.Get()).Failure == EProject_JSocialOperationFailure::AlreadyMember);
		FProject_JSocialGroupSnapshot Incoming;
		Incoming.GroupId = TEXT("HistoryCapacityForeign"); Incoming.LeaderCharacterId = CC->GetCharacterId().Value; Incoming.Members = {Incoming.LeaderCharacterId};
		TestFalse(TEXT("New foreign snapshot cannot bypass a full history budget"), Bounded->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Incoming));
		FProject_JSocialGroupSnapshot Missing;
		TestFalse(TEXT("Refused foreign ID was not published"), Bounded->GetGroupSnapshot(EProject_JSocialGroupKind::Party, Incoming.GroupId, Missing));
		Incoming.GroupId = FName(FirstBounded.GroupId, 512);
		TestFalse(TEXT("New same-namespace future snapshot is refused at capacity"), Bounded->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Incoming));
		TestTrue(TEXT("Refused restore cannot reserve a future number or publish callbacks"),
			Bounded->PartyIds.Namespace == NamespaceBefore && Bounded->PartyIds.LastIssued == IssuedBefore && Bounded->Parties.Num() == 2 &&
			Bounded->CharacterParties.Num() == 1 && Bounded->OnlinePlayers.Num() == OnlineBefore && Notifications == NotifyBefore);
		TestTrue(TEXT("Refused new IDs leave the complete existing snapshot unchanged"),
			Bounded->GetGroupSnapshot(EProject_JSocialGroupKind::Party, SecondBounded.GroupId, FullAfter) && FullAfter.GroupId == FullBefore.GroupId &&
			FullAfter.Version == FullBefore.Version && FullAfter.Revision == FullBefore.Revision && FullAfter.bDeleted == FullBefore.bDeleted && FullAfter.LeaderCharacterId == FullBefore.LeaderCharacterId && FullAfter.Members == FullBefore.Members);
		const int32 BeforeJoin = Notifications;
		TestTrue(TEXT("Existing live group still admits members when history is full"), Bounded->JoinParty(CC.Get(), SecondBounded.GroupId).bSucceeded && CC->GetPartyId_Implementation() == SecondBounded.GroupId && CC->GetPartyRoster().Num() == 2 && Notifications > BeforeJoin);
		TestTrue(TEXT("Existing member can leave at capacity"), Bounded->LeaveParty(CC.Get()).bSucceeded && CC->GetPartyId_Implementation().IsNone() && CC->GetPartyRoster().IsEmpty());
		TestTrue(TEXT("The second group can also become a tombstone at capacity"), Bounded->LeaveParty(CB.Get()).bSucceeded && Bounded->Parties.Num() == 2);
		TestTrue(TEXT("Exact tombstone remains queryable without eviction"),
			Bounded->GetGroupSnapshot(EProject_JSocialGroupKind::Party, FirstBounded.GroupId, Tombstone) && Tombstone.Version == BeforeDelete.Version &&
			Tombstone.GroupId == FirstBounded.GroupId && Tombstone.Revision == BeforeDelete.Revision + 1 && Tombstone.bDeleted && Tombstone.Members.IsEmpty() && !Tombstone.LeaderCharacterId.IsValid());
		const int32 BeforeReplay = Notifications;
		TestTrue(TEXT("Identical tombstone replay stays idempotent at capacity"), Bounded->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Tombstone));
		TestFalse(TEXT("Old live snapshot remains stale at capacity"), Bounded->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, BeforeDelete));
		TestEqual(TEXT("Idempotent and stale restores do not broadcast roster changes"), Notifications, BeforeReplay);
		auto Revived = BeforeDelete; Revived.Revision = Tombstone.Revision + 1;
		TestTrue(TEXT("Higher revision can resurrect the same ID at capacity"), Bounded->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Revived) && CA->GetPartyId_Implementation() == FirstBounded.GroupId && CA->GetPartyRoster().Num() == 1 && Notifications > BeforeReplay);
		const auto IndependentGuild = Bounded->CreateGuild(CB.Get());
		TestTrue(TEXT("Party history does not consume the independent guild budget"), IndependentGuild.bSucceeded && Bounded->Guilds.Num() == 1 && CB->GetGuildId_Implementation() == IndependentGuild.GroupId);
		Bounded->MaxTrackedGroupsPerKind = 1;
		const auto ShrunkCreate = Bounded->CreateParty(CB.Get());
		TestTrue(TEXT("Shrink closes new party IDs without evicting either existing identity"), !ShrunkCreate.bSucceeded && ShrunkCreate.Failure == EProject_JSocialOperationFailure::HistoryCapacity && Bounded->Parties.Num() == 2 && Bounded->GetGroupSnapshot(EProject_JSocialGroupKind::Party, SecondBounded.GroupId, Missing) && Missing.bDeleted);
		TestTrue(TEXT("Shrink preserves existing membership mutation"), Bounded->JoinParty(CC.Get(), FirstBounded.GroupId).bSucceeded && Bounded->LeaveParty(CC.Get()).bSucceeded);
		Bounded->MaxTrackedGroupsPerKind = 0;
		TestTrue(TEXT("Zero also preserves existing membership mutation"), Bounded->JoinParty(CC.Get(), FirstBounded.GroupId).bSucceeded && Bounded->LeaveParty(CC.Get()).bSucceeded);
		FProject_JSocialGroupSnapshot SecondTombstone;
		Bounded->GetGroupSnapshot(EProject_JSocialGroupKind::Party, SecondBounded.GroupId, SecondTombstone);
		SecondTombstone.bDeleted = false; ++SecondTombstone.Revision;
		SecondTombstone.LeaderCharacterId = CC->GetCharacterId().Value; SecondTombstone.Members = {SecondTombstone.LeaderCharacterId};
		TestTrue(TEXT("Zero preserves higher-revision restore of an existing identity"), Bounded->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, SecondTombstone) && CC->GetPartyId_Implementation() == SecondBounded.GroupId && Bounded->Parties.Num() == 2 && Bounded->PartyIds.LastIssued == IssuedBefore);
		TStrongObjectPtr<UProject_JSocialSubsystem> Closed(NewObject<UProject_JSocialSubsystem>(CapacityGI.Get()));
		Closed->MaxTrackedGroupsPerKind = 0;
		TStrongObjectPtr<AProject_JPlayerState> ZeroPlayer(NewObject<AProject_JPlayerState>());
		ZeroPlayer->SetIdentity(FProject_JAccountId::NewId(), FProject_JCharacterId::NewId());
		const auto ZeroNotify = ZeroPlayer->OnPartyRosterChanged.AddLambda([&] { ++Notifications; });
		ON_SCOPE_EXIT { ZeroPlayer->OnPartyRosterChanged.Remove(ZeroNotify); };
		const int32 BeforeZero = Notifications;
		const auto ClosedParty = Closed->CreateParty(ZeroPlayer.Get()), ClosedGuild = Closed->CreateGuild(ZeroPlayer.Get());
		TestTrue(TEXT("A fresh zero budget rejects both kinds with typed capacity"), ClosedParty.Failure == EProject_JSocialOperationFailure::HistoryCapacity && ClosedGuild.Failure == EProject_JSocialOperationFailure::HistoryCapacity && !ClosedParty.bSucceeded && !ClosedGuild.bSucceeded);
		Incoming.GroupId = TEXT("HistoryCapacityZeroForeign"); Incoming.LeaderCharacterId = ZeroPlayer->GetCharacterId().Value; Incoming.Members = {Incoming.LeaderCharacterId};
		TestFalse(TEXT("A fresh zero budget also refuses a valid new trusted snapshot"), Closed->RestoreGroupSnapshot(EProject_JSocialGroupKind::Party, Incoming));
		TestTrue(TEXT("Zero rejection allocates no namespace identity state projection or notification"),
			Closed->PartyIds.Namespace.IsNone() && Closed->GuildIds.Namespace.IsNone() && Closed->PartyIds.LastIssued == 0 && Closed->GuildIds.LastIssued == 0 &&
			Closed->Parties.IsEmpty() && Closed->Guilds.IsEmpty() && Closed->CharacterParties.IsEmpty() && Closed->CharacterGuilds.IsEmpty() && Closed->OnlinePlayers.IsEmpty() &&
			ZeroPlayer->GetPartyId_Implementation().IsNone() && ZeroPlayer->GetGuildId_Implementation().IsNone() && ZeroPlayer->GetPartyRoster().IsEmpty() && Notifications == BeforeZero);
		Closed->Deinitialize(); Bounded->Deinitialize();
	}
	Social->Deinitialize(); OtherSocial->Deinitialize();
	return true;
}
#endif
