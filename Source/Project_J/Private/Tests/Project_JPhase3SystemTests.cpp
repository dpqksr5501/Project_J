#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/ParallelFor.h"
#include "Audio/Project_JAudioBudgetSubsystem.h"
#include "Economy/Project_JEconomySubsystem.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Gameplay/Project_JProgressionAndLifeSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Security/Project_JServerIntegritySubsystem.h"
#include "Social/Project_JGuildAndSocialExtensions.h"
#include "UI/Project_JUIWindowStackSubsystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase3EconomyTest, "ProjectJ.Systems.Phase3.EconomyLedgerAuctionTradeMail",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase3EconomyTest::RunTest(const FString&)
{
	using namespace ProjectJ::MMO;
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->Init();
	auto* Econ = GI->GetSubsystem<UProject_JEconomySubsystem>();
	if (!TestNotNull(TEXT("EconomySubsystem"), Econ)) { GI->Shutdown(); return false; }

	const FGuid Seller = FGuid::NewGuid();
	const FGuid ItemId = FGuid::NewGuid();
	TestEqual(TEXT("Mint seller deposit gold"),
		Econ->GetLedger().MintCurrency(FGuid::NewGuid(), Seller, TEXT("GOLD"), 1000, TEXT("Seed")), ELedgerStatus::Committed);

	// Unbalanced currency entry must fail closed.
	TArray<FLedgerEntryInput> BadEntries;
	BadEntries.Add({ Seller, TEXT("GOLD"), 500 });
	BadEntries.Add({ FEconomyLedger::SystemSinkAccount(), TEXT("GOLD"), -400 });
	TestEqual(TEXT("Unbalanced double-entry rejected"),
		Econ->GetLedger().CommitCurrencyTransaction(FGuid::NewGuid(), TEXT("BadTx"), BadEntries), ELedgerStatus::UnbalancedEntries);

	FItemOwnershipRecord Sword{ ItemId, Seller, EItemOwnerScope::CharInv, TEXT("Blade_Epic"), 1, 8, 1 };
	TestTrue(TEXT("Unique item registered"), Econ->GetLedger().RegisterItemInstance(Sword));
	TestFalse(TEXT("Duplicate global UUID item rejected"), Econ->GetLedger().RegisterItemInstance(Sword));

	FGuid ListingId;
	TestTrue(TEXT("Auction listing created into escrow"),
		Econ->CreateAuctionListing(FGuid::NewGuid(), Seller, ItemId, 2000, 50, 3600.0, 100.0, ListingId));

	// 20 concurrent buyers race for the same auction listing -> exactly 1 winner, 19 rejected!
	TArray<FGuid> Buyers;
	Buyers.SetNum(20);
	for (int32 i = 0; i < 20; ++i)
	{
		Buyers[i] = FGuid::NewGuid();
		Econ->GetLedger().MintCurrency(FGuid::NewGuid(), Buyers[i], TEXT("GOLD"), 5000, TEXT("BuyerSeed"));
	}

	TAtomic<int32> Winners(0);
	FGuid WinnerId;
	FCriticalSection WinnerLock;
	ParallelFor(20, [&](int32 Idx)
	{
		if (Econ->ExecuteAuctionBuyout(FGuid::NewGuid(), ListingId, Buyers[Idx], 120.0))
		{
			++Winners;
			FScopeLock L(&WinnerLock);
			WinnerId = Buyers[Idx];
		}
	});
	TestEqual(TEXT("20-way concurrent buyout produces exactly 1 winner"), Winners.Load(), 1);
	TestTrue(TEXT("Global double-entry zero-sum invariant holds"), Econ->GetLedger().VerifyGlobalZeroSumInvariant());
	const auto SellerMails = Econ->GetInbox(Seller);
	const FGuid RewardMail = Econ->SendSystemMail(Seller, TEXT("TestSystemReward"), TEXT("GOLD"), 800, {});
	TestTrue(TEXT("System reward can be claimed while auction proceeds remain in escrow"),
		Econ->ClaimMailAttachments(RewardMail, Seller, true));
	if (TestEqual(TEXT("Seller received one auction mail"), SellerMails.Num(), 1))
	{
		TestTrue(TEXT("Seller can still claim the original escrow funding"), Econ->ClaimMailAttachments(SellerMails[0].MailId, Seller, true));
		TestEqual(TEXT("Deposit, reward and seller proceeds balance independently"), Econ->GetLedger().GetBalance(Seller, TEXT("GOLD")), 3650ll);
	}
	const int64 SellerBeforeRejectedTransfer = Econ->GetLedger().GetBalance(Seller, TEXT("GOLD"));
	const TArray<FLedgerEntryInput> TransferEntries{{Seller, TEXT("GOLD"), -1}, {WinnerId, TEXT("GOLD"), 1}};
	const FItemOwnershipTransfer WrongOwner{ItemId, Seller, EItemOwnerScope::MailEscrow, 3, Seller, EItemOwnerScope::CharInv};
	TestEqual(TEXT("An ownership conflict rejects the currency and item transaction"),
		Econ->GetLedger().CommitTransaction(FGuid::NewGuid(), TEXT("TestOwnershipConflict"), TransferEntries, MakeArrayView(&WrongOwner, 1)),
		ELedgerStatus::OwnershipConflict);
	TestEqual(TEXT("Rejected ownership leaves the debit unapplied"), Econ->GetLedger().GetBalance(Seller, TEXT("GOLD")), SellerBeforeRejectedTransfer);

	// Winner mail claim rolls back when inventory is full, succeeds when space available.
	const auto WinnerMails = Econ->GetInbox(WinnerId);
	if (TestEqual(TEXT("Winner received 1 auction mail"), WinnerMails.Num(), 1))
	{
		TestFalse(TEXT("Mail claim rolls back when inventory full"), Econ->ClaimMailAttachments(WinnerMails[0].MailId, WinnerId, false));
		TestTrue(TEXT("Mail claim succeeds with inventory space"), Econ->ClaimMailAttachments(WinnerMails[0].MailId, WinnerId, true));
		FItemOwnershipRecord PostClaim;
		TestTrue(TEXT("Item owned by winner in CharInv"), Econ->GetLedger().FindItem(ItemId, PostClaim)
			&& PostClaim.OwnerId == WinnerId && PostClaim.Scope == EItemOwnerScope::CharInv);
	}

	// 1:1 Direct Trade: Lock -> Edit auto-unlocks -> Re-Lock -> Confirm atomic swap.
	const FGuid Partner = FGuid::NewGuid();
	Econ->GetLedger().MintCurrency(FGuid::NewGuid(), Partner, TEXT("GOLD"), 1500, TEXT("PartnerSeed"));
	const FGuid TradeId = Econ->StartDirectTrade(WinnerId, Partner);
	TestTrue(TEXT("Winner offers sword"), Econ->UpdateTradeOffer(TradeId, WinnerId, 0, { ItemId }));
	TestTrue(TEXT("Partner offers 1000 gold"), Econ->UpdateTradeOffer(TradeId, Partner, 1000, {}));
	TestTrue(TEXT("Winner locks"), Econ->SetTradeLocked(TradeId, WinnerId, true));
	TestTrue(TEXT("Partner locks"), Econ->SetTradeLocked(TradeId, Partner, true));
	// Anti-scam edit must unlock BOTH sides!
	TestTrue(TEXT("Partner edits gold to 100"), Econ->UpdateTradeOffer(TradeId, Partner, 100, {}));
	TestFalse(TEXT("Winner automatically unlocked after partner edit"), Econ->IsTradeLocked(TradeId, WinnerId));
	TestFalse(TEXT("Partner automatically unlocked after edit"), Econ->IsTradeLocked(TradeId, Partner));
	TestFalse(TEXT("Cannot confirm unlocked trade"), Econ->ConfirmDirectTrade(TradeId, WinnerId, FGuid::NewGuid()));

	TestTrue(TEXT("Partner restores fair 1000 gold"), Econ->UpdateTradeOffer(TradeId, Partner, 1000, {}));
	Econ->SetTradeLocked(TradeId, WinnerId, true);
	Econ->SetTradeLocked(TradeId, Partner, true);
	const FGuid TradeCommitKey = FGuid::NewGuid();
	TestTrue(TEXT("Winner confirms"), Econ->ConfirmDirectTrade(TradeId, WinnerId, TradeCommitKey));
	TestTrue(TEXT("Partner confirms -> atomic swap executes"), Econ->ConfirmDirectTrade(TradeId, Partner, TradeCommitKey));
	FItemOwnershipRecord Swapped;
	TestTrue(TEXT("Partner now owns traded sword"), Econ->GetLedger().FindItem(ItemId, Swapped) && Swapped.OwnerId == Partner);
	TestTrue(TEXT("Zero-sum ledger invariant holds after trade"), Econ->GetLedger().VerifyGlobalZeroSumInvariant());

	GI->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase3SocialTest, "ProjectJ.Systems.Phase3.GuildBankFriendsBlockChat",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase3SocialTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->Init();
	auto* Social = GI->GetSubsystem<UProject_JGuildAndSocialExtensionsSubsystem>();
	if (!TestNotNull(TEXT("GuildAndSocialExtensions"), Social)) { GI->Shutdown(); return false; }

	const FGuid GuildId = FGuid::NewGuid();
	const FGuid Initiate = FGuid::NewGuid();
	const FGuid Member = FGuid::NewGuid();
	const FGuid ItemId = FGuid::NewGuid();
	Social->SetGuildMemberRank(GuildId, Initiate, EProject_JGuildRank::Initiate);
	Social->SetGuildMemberRank(GuildId, Member, EProject_JGuildRank::Member);

	TestTrue(TEXT("Member deposits item to guild bank"), Social->DepositToGuildBank(GuildId, Member, 0, ItemId, TEXT("Potion_HP")));
	FGuid OutItem;
	TestFalse(TEXT("Initiate cannot withdraw (RBAC limit 0)"), Social->WithdrawFromGuildBank(GuildId, Initiate, 0, 1, 20261123, OutItem));
	TestFalse(TEXT("Stale revision rejected"), Social->WithdrawFromGuildBank(GuildId, Member, 0, 99, 20261123, OutItem));
	TestTrue(TEXT("Member withdraws with matching revision"), Social->WithdrawFromGuildBank(GuildId, Member, 0, 1, 20261123, OutItem));
	TestEqual(TEXT("Withdrawn item matches"), OutItem, ItemId);
	TestEqual(TEXT("Guild bank audit log recorded deposit + withdraw"), Social->GetGuildBankAuditLogCount(GuildId), 2);

	const FGuid ReplacementItem = FGuid::NewGuid();
	TestTrue(TEXT("The same bank slot accepts a replacement deposit"),
		Social->DepositToGuildBank(GuildId, Member, 0, ReplacementItem, TEXT("Potion_HP")));
	TestFalse(TEXT("The old revision cannot withdraw the replacement"),
		Social->WithdrawFromGuildBankToMail(GuildId, Member, 0, 1, 20261123));
	TestFalse(TEXT("An unrecorded attachment leaves the withdrawal uncommitted"),
		Social->WithdrawFromGuildBankToMail(GuildId, Member, 0, 2, 20261123));
	auto* Econ = GI->GetSubsystem<UProject_JEconomySubsystem>();
	if (TestNotNull(TEXT("Guild withdrawal economy ledger"), Econ))
	{
		using ProjectJ::MMO::EItemOwnerScope;
		TestTrue(TEXT("The authority registers the guild-owned attachment"),
			Econ->GetLedger().RegisterItemInstance({ReplacementItem, GuildId, EItemOwnerScope::GuildBank, TEXT("Potion_HP")}));
		TestTrue(TEXT("A funded withdrawal transfers to the member's mail"),
			Social->WithdrawFromGuildBankToMail(GuildId, Member, 0, 2, 20261123));
		TestFalse(TEXT("Retry cannot withdraw the same item twice"),
			Social->WithdrawFromGuildBankToMail(GuildId, Member, 0, 2, 20261123));
		const auto Inbox = Econ->GetInbox(Member);
		if (TestEqual(TEXT("One withdrawal mail was delivered"), Inbox.Num(), 1))
			TestEqual(TEXT("Mail carries the exact guild-owned item ID"), Inbox[0].AttachedItemInstanceId, ReplacementItem);
	}

	// Friends, Block & Chat rate limiting.
	TestTrue(TEXT("Friend request sent"), Social->SendFriendRequest(Initiate, Member));
	TestTrue(TEXT("Friend request accepted"), Social->AcceptFriendRequest(Member, Initiate));
	TestTrue(TEXT("Mutual friendship active"), Social->AreFriends(Initiate, Member));
	TestTrue(TEXT("Whisper allowed before block"), Social->AdmitChatMessage(Initiate, Member, EProject_JChatChannel::Whisper, TEXT("Hi"), 10.0));
	TestTrue(TEXT("Member blocks Initiate"), Social->BlockCharacter(Member, Initiate));
	TestFalse(TEXT("Block terminates friendship"), Social->AreFriends(Initiate, Member));
	TestFalse(TEXT("Blocked whisper rejected"), Social->AdmitChatMessage(Initiate, Member, EProject_JChatChannel::Whisper, TEXT("Spam"), 20.0));
	TestTrue(TEXT("First world chat allowed"), Social->AdmitChatMessage(Member, FGuid(), EProject_JChatChannel::World, TEXT("LFG"), 100.0));
	TestFalse(TEXT("World chat within 10s rate-limited"), Social->AdmitChatMessage(Member, FGuid(), EProject_JChatChannel::World, TEXT("LFG2"), 105.0));

	GI->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase3ProgressionLifeTest, "ProjectJ.Systems.Phase3.ProgressionQuestsGathering",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase3ProgressionLifeTest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->Init();
	auto* Prog = GI->GetSubsystem<UProject_JProgressionAndLifeSubsystem>();
	if (!TestNotNull(TEXT("ProgressionAndLifeSubsystem"), Prog)) { GI->Shutdown(); return false; }

	TestTrue(TEXT("Level scales upward with XP"), UProject_JProgressionAndLifeSubsystem::ComputeLevelFromTotalXP(50000) > 1);
	const auto SafeEnh = UProject_JProgressionAndLifeSubsystem::EvaluateEnhancement(5, 0, 0.99f);
	TestTrue(TEXT("Safe zone (+5 -> +6) succeeds even on worst roll"), SafeEnh.bSucceeded && SafeEnh.NewEnhanceLevel == 6);
	const auto FailEnh = UProject_JProgressionAndLifeSubsystem::EvaluateEnhancement(10, 0, 0.99f);
	TestFalse(TEXT("Above safe zone can fail and increments pity"), FailEnh.bSucceeded);
	TestEqual(TEXT("Pity incremented"), FailEnh.NewPityCounter, 1);
	const auto PityEnh = UProject_JProgressionAndLifeSubsystem::EvaluateEnhancement(10, UProject_JProgressionAndLifeSubsystem::EnhancePityThreshold, 0.99f);
	TestTrue(TEXT("Pity threshold guarantees success"), PityEnh.bSucceeded && PityEnh.bTriggeredPity && PityEnh.NewEnhanceLevel == 11);

	const auto Sets = UProject_JProgressionAndLifeSubsystem::EvaluateEquippedSetBonuses({ TEXT("DragonSet"), TEXT("DragonSet"), TEXT("DragonSet"), TEXT("DragonSet") });
	TestTrue(TEXT("4-piece set bonus activated"), Sets.Num() == 1 && Sets[0].bTwoPieceActive && Sets[0].bFourPieceActive);

	// Chain & UTC Daily Quest claim.
	const FGuid CharA = FGuid::NewGuid();
	const FGuid CharB = FGuid::NewGuid();
	TSet<FName> Completed;
	TestFalse(TEXT("Chain quest blocked without prerequisite"), Prog->CanAcceptChainQuest(CharA, TEXT("Q2"), { TEXT("Q1") }, Completed));
	Completed.Add(TEXT("Q1"));
	TestTrue(TEXT("Chain quest unlocked after prerequisite"), Prog->CanAcceptChainQuest(CharA, TEXT("Q2"), { TEXT("Q1") }, Completed));
	const FDateTime Day1(2026, 11, 23, 10, 0, 0);
	const FDateTime Day2(2026, 11, 24, 0, 1, 0);
	TestTrue(TEXT("Daily quest claimed on Day 1"), Prog->TryClaimRepeatingQuest(CharA, TEXT("DailyBoss"), EProject_JQuestResetPolicy::DailyUtc00, Day1));
	TestFalse(TEXT("Daily quest duplicate blocked on Day 1"), Prog->TryClaimRepeatingQuest(CharA, TEXT("DailyBoss"), EProject_JQuestResetPolicy::DailyUtc00, Day1));
	TestTrue(TEXT("Daily quest available again on Day 2 UTC"), Prog->TryClaimRepeatingQuest(CharA, TEXT("DailyBoss"), EProject_JQuestResetPolicy::DailyUtc00, Day2));

	// Personal-phase vs competitive resource gathering.
	const FGuid PersonalNode = FGuid::NewGuid();
	const FGuid RareNode = FGuid::NewGuid();
	TestTrue(TEXT("CharA gathers personal-phase node"), Prog->TryGatherResourceNode(PersonalNode, EProject_JResourceNodeMode::PersonalPhase, CharA, 2, 1, 60.0, 10.0));
	TestTrue(TEXT("CharB independently gathers same personal-phase node"), Prog->TryGatherResourceNode(PersonalNode, EProject_JResourceNodeMode::PersonalPhase, CharB, 2, 1, 60.0, 11.0));
	TestFalse(TEXT("CharA blocked on personal cooldown"), Prog->TryGatherResourceNode(PersonalNode, EProject_JResourceNodeMode::PersonalPhase, CharA, 2, 1, 60.0, 20.0));
	TestTrue(TEXT("CharA claims competitive rare node"), Prog->TryGatherResourceNode(RareNode, EProject_JResourceNodeMode::CompetitiveShared, CharA, 3, 3, 120.0, 10.0));
	TestFalse(TEXT("CharB blocked by competitive respawn"), Prog->TryGatherResourceNode(RareNode, EProject_JResourceNodeMode::CompetitiveShared, CharB, 3, 3, 120.0, 20.0));

	GI->Shutdown();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPhase3IntegrityAudioUITest, "ProjectJ.Systems.Phase3.IntegrityAudioAndUIStack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPhase3IntegrityAudioUITest::RunTest(const FString&)
{
	UGameInstance* GI = NewObject<UGameInstance>(GEngine);
	GI->Init();
	auto* Integrity = GI->GetSubsystem<UProject_JServerIntegritySubsystem>();
	auto* Audio = GI->GetSubsystem<UProject_JAudioBudgetSubsystem>();
	auto* UIStack = GI->GetSubsystem<UProject_JUIWindowStackSubsystem>();
	if (!Integrity || !Audio || !UIStack) { GI->Shutdown(); return false; }

	const FGuid CharId = FGuid::NewGuid();
	// Legitimate speed passes; teleport without whitelist escalates SnapBack -> FlagAudit -> DisconnectRecommended.
	TestEqual(TEXT("Normal movement accepted"),
		Integrity->ValidateMovementStep(CharId, FVector::ZeroVector, FVector(500, 0, 0), 600.f, 1.0, 10.0),
		EProject_JIntegrityAction::Accept);

	Integrity->GrantMovementWhitelist(CharId, 2.0, 11.0);
	TestEqual(TEXT("Whitelisted portal jump accepted"),
		Integrity->ValidateMovementStep(CharId, FVector::ZeroVector, FVector(50000, 0, 0), 600.f, 0.1, 11.5),
		EProject_JIntegrityAction::Accept);

	EProject_JIntegrityAction LastAction = EProject_JIntegrityAction::Accept;
	for (int32 i = 0; i < 5; ++i)
	{
		LastAction = Integrity->ValidateMovementStep(CharId, FVector::ZeroVector, FVector(10000, 0, 0), 600.f, 0.5, 20.0 + i * 0.5);
	}
	TestEqual(TEXT("5 speed violations escalate to DisconnectRecommended"), LastAction, EProject_JIntegrityAction::DisconnectRecommended);

	// Audio 48-voice & 25m other-player cull.
	TestFalse(TEXT("Other-player SFX beyond 25m culled"), Audio->RequestVoice(EProject_JAudioGroup::OtherPlayerSFX, 3000.0).bAdmitted);
	const auto OtherOk = Audio->RequestVoice(EProject_JAudioGroup::OtherPlayerSFX, 500.0);
	TestTrue(TEXT("Nearby other-player SFX admitted with -9dB ducking"), OtherOk.bAdmitted && OtherOk.VolumeMultiplier < 0.5f);

	// UI Window Stack ESC order & accessibility clamping.
	UIStack->PushModalWindow(TEXT("Inventory"));
	UIStack->PushModalWindow(TEXT("AuctionHouse"));
	TestEqual(TEXT("ESC closes top window first"), UIStack->HandleEscapeCloseTop(), FName(TEXT("AuctionHouse")));
	TestEqual(TEXT("Remaining top window is Inventory"), UIStack->GetTopWindow(), FName(TEXT("Inventory")));
	FProject_JAccessibilitySettings Acc;
	Acc.UIScale = 5.0f;
	UIStack->ApplyAccessibilitySettings(Acc);
	TestTrue(TEXT("Accessibility UIScale clamped to 1.4"), FMath::IsNearlyEqual(UIStack->GetAccessibilitySettings().UIScale, 1.4f));

	GI->Shutdown();
	return true;
}
#endif
