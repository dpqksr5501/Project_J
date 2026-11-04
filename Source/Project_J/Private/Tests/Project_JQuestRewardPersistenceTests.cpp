#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Async/Async.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "Backend/Project_JQuestHistoryStorage.h"
#include "Backend/Project_JSQLiteRepository.h"
#include "Backend/Project_JWorldEventClaimStorage.h"
#include "World/Project_JWorldEncounter.h"
#include "World/Project_JWorldEventRegistry.h"
#include "Project_JNPCCharacter.h"
#include "Project_JAttributeSet.h"
#include "GameplayEffect.h"
#include "GameFramework/GameStateBase.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JInventoryComponent.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Game/Project_JGameMode.h"
#include "GameFramework/PlayerController.h"
#include "Engine/Player.h"
#include "Misc/CommandLine.h"
#include "Game/Project_JQuestComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Inventory/Project_JConsumableDefinition.h"
#include "Inventory/Project_JPersistentEdit.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Project_JAbilitySystemComponent.h"
#include "Tests/Project_JMMOArchitectureReceiver.h"
#include "Tests/Project_JSourceAuditPhase2UIReceiver.h"
#include "UObject/StrongObjectPtr.h"
#include <atomic>

// Test-only access selects an isolated database and seeds a server event entitlement.
// It never alters profile command/binding phase state.
struct FProjectJQuestRewardProfileTestAccess
{
    static bool SeedWorldEvent(AProject_JWorldEncounter& Encounter, FGuid Round, FGuid Character, int64 XP)
    {
        Encounter.FrozenDevelopmentRewardXP.Add(Round, XP);
        return Encounter.Contributions.Begin(Round) && Encounter.Contributions.Record(Round, Character, 1) &&
            Encounter.Contributions.Close(Round, true);
    }
    // Start after asset/ground spawn admission, with an EMPTY ledger. Damage, defeat,
    // eligibility and payout below must all pass through the production callbacks.
    // This is not an async streaming/spawn-admission test.
    static bool AttachActiveNPC(AProject_JWorldEncounter& Encounter, AProject_JNPCCharacter& NPC, int64 XP)
    {
        auto* Attributes = NPC.GetAttributeSet();
        auto* Registry = Encounter.GetWorld()->GetSubsystem<UProject_JWorldEventRegistry>();
        if (!Attributes || !Attributes->CanBeginHealthGEObservation() || !Registry) return false;
        Encounter.EncounterId = FName(*FGuid::NewGuid().ToString());
        Encounter.NPCClass = AProject_JNPCCharacter::StaticClass();
        Encounter.Phase = EProject_JEncounterPhase::Active;
        if (!Registry->Register(Encounter.EncounterId, &Encounter)) return false;
        Encounter.bRegistered = true;
        Encounter.NPC = &NPC;
        Encounter.bTrackContributions = true;
        Encounter.DevelopmentRewardExperience = XP;
        Encounter.ContributionRound = FGuid::NewGuid();
        if (!Encounter.Contributions.Begin(Encounter.ContributionRound)) return false;
        Encounter.FrozenDevelopmentRewardXP.Add(Encounter.ContributionRound, XP);
        Encounter.ContributionAttributes = Attributes;
        Encounter.ContributionLossHandle = Attributes->OnConfirmedHealthGELoss.AddUObject(&Encounter, &AProject_JWorldEncounter::OnContributionLoss);
        Encounter.ContributionFailureHandle = Attributes->OnHealthGEObservationFailed.AddUObject(&Encounter, &AProject_JWorldEncounter::OnContributionObservationFailed);
        NPC.OnDestroyed.AddDynamic(&Encounter, &AProject_JWorldEncounter::OnNPCDestroyed);
        Encounter.Phase = EProject_JEncounterPhase::Active;
        ++Encounter.SpawnRevision;
        return true;
    }
    static void PollEncounter(AProject_JWorldEncounter& Encounter) { Encounter.UpdateEncounter(); }
    static void SetDatabasePath(UProject_JLocalProfileSubsystem& Profiles, const FString& Path)
    {
        Profiles.DatabasePath = Path;
    }
};

namespace
{
struct FRewardReply
{
    bool Done = false, OK = false, First = false, Durable = false;
};
struct FRewardProbe
{
    std::atomic<int32> Plans{0};
    std::atomic<int32> Packed{0}, EncodeRejected{0};
    bool Observe = false, Observed = false, AllInstalled = false;
    bool Retired = false;
    int32 ItemEvents = 0;
};
int64 CountItem(const AProject_JPlayerState* Player, FName Id)
{
    int64 Count = 0;
    for (const auto& Item : Player->GetInventoryComponent()->GetItemInstances()) if (Item.ItemDef && Item.ItemDef->ItemId == Id) Count += Item.StackCount;
    return Count;
}
bool SameItems(const AProject_JPlayerState* Player, const FProject_JLocalProfileSnapshot& Snapshot)
{
    const auto Items = Player->GetInventoryComponent()->GetItemInstances();
    if (Items.Num() != Snapshot.Items.Num()) return false;
    for (const auto& Expected : Snapshot.Items)
    {
        const auto* Found = Items.FindByPredicate([&](const auto& Item) { return Item.InstanceId == Expected.Instance; });
        if (!Found || !Found->ItemDef || Found->ItemDef->ItemId != Expected.Definition || Found->ItemDef->SchemaVersion != Expected.DefinitionVersion ||
            Found->StackCount != Expected.Count || Found->ItemLevel != Expected.Level || Found->BagOrder != Expected.Order) return false;
    }
    return true;
}
class FQuestRewardProfile : public IAutomationLatentCommand
{
    FAutomationTestBase* Test;
    TStrongObjectPtr<UGameInstance> GI;
    UWorld* World;
    UProject_JLocalProfileSubsystem* Profiles;
    TWeakObjectPtr<AProject_JPlayerState> Player;
    TStrongObjectPtr<UProject_JQuestDefinition> SuccessQuest, FullQuest, ByteQuest;
    UProject_JConsumableDefinition* Health;
    UProject_JConsumableDefinition* Mana;
    FString Database;
    int32 Step = 0;
    bool bTrustedDevelopment = false;
    bool bWorldEventIntegration = false;
    TFuture<bool> IntegrationDisk;
    FString OriginalCommandLine;
    FProject_JAccountId TrustedAccount = FProject_JAccountId::NewId();
    FProject_JCharacterId TrustedCharacter = FProject_JCharacterId::NewId();
    TWeakObjectPtr<APlayerController> TrustedController;
    int32 RetirementMode = 0; // 0: original durable flow; 1: actual getter retirement; 2: item callback retirement.
    TSharedPtr<FRewardReply, ESPMode::ThreadSafe> OpenReply;
    double Started = FPlatformTime::Seconds();
    int64 RejectedRevision = 0, ExpectedHealth = 0;
    FGuid ExistingHealth;
    TWeakObjectPtr<AProject_JWorldEncounter> RewardEncounter;
    FGuid RewardRound;
    TFuture<bool> SeedClaims;
    FProject_JPersistentEdit PaidIntent;
    FProject_JQuestSnapshot PristineLoadQuests;
    TArray<FProject_JItemInstanceData> PristineLoadItems;
    int32 PristineLoadProgressionRevision = 0;
    int64 PristineLoadExperience = 0;
    TSharedPtr<FRewardReply, ESPMode::ThreadSafe> Reply;
    TSharedPtr<FRewardProbe, ESPMode::ThreadSafe> Probe = MakeShared<FRewardProbe, ESPMode::ThreadSafe>();
    TFuture<ProjectJ::LocalProfileHistory::FProfileValues> Disk;
    ProjectJ::LocalProfileHistory::FProfileValues PaidValues;
    FDelegateHandle ProgressHandle, QuestHandle;
    TStrongObjectPtr<UProject_JSourceAuditPhase2UIReceiver> InventoryReceiver, RetirementReceiver;

    bool Check(bool Condition, const TCHAR* Message) { return Test->TestTrue(Message, Condition); }
    void ArmGetterRetirement(AProject_JPlayerState* State)
    {
        const TWeakObjectPtr<AProject_JQuestRewardQueryFixture> Weak = Cast<AProject_JQuestRewardQueryFixture>(State);
        const auto Counts = Probe;
        CastChecked<AProject_JQuestRewardQueryFixture>(State)->OnASCQuery = [Weak, Counts]
        {
            if (auto* Current = Weak.Get())
            {
                Counts->Retired = true;
                Current->SetIdentity(FProject_JAccountId{FGuid::NewGuid()}, FProject_JCharacterId{FGuid::NewGuid()});
            }
        };
    }
    void Open(bool RetireDuringLoad = false)
    {
        AProject_JPlayerState* State = nullptr;
        if (bTrustedDevelopment)
        {
            auto* PC = World->SpawnActor<APlayerController>();
            TrustedController = PC;
            PC->SetAsLocalPlayerController();
            if (!PC->GetPlayerState<AProject_JPlayerState>()) PC->InitPlayerState();
            PC->SetPlayer(NewObject<UPlayer>(GI.Get()));
            State = PC->GetPlayerState<AProject_JPlayerState>();
            const auto RejectedUnverified = MakeShared<FRewardReply, ESPMode::ThreadSafe>();
            Profiles->OpenTrustedDevelopmentProfile(State, [RejectedUnverified](bool OK) { RejectedUnverified->Done = true; RejectedUnverified->OK = OK; });
            Check(RejectedUnverified->Done && !RejectedUnverified->OK && !Profiles->HasProfileBinding(State), TEXT("Development flag alone never authenticates a PlayerState"));
            auto* Mode = CastChecked<AProject_JGameMode>(World->GetAuthGameMode());
            ProjectJ::MMO::FServerSessionAttempt Attempt; ProjectJ::MMO::FServerSessionLease Lease;
            if (!Check(Mode->BeginTrustedSession(PC, TrustedAccount, TrustedCharacter, Attempt)
                && Mode->CompleteTrustedSession(Attempt, true, Lease), TEXT("Development fixture supplies verified native session before profile restore")))
            { OpenReply = MakeShared<FRewardReply, ESPMode::ThreadSafe>(); OpenReply->Done = true; return; }
        }
        else State = RetirementMode ? World->SpawnActor<AProject_JQuestRewardQueryFixture>() : World->SpawnActor<AProject_JPlayerState>();
        Player = State;
        auto* Quests = State->FindComponentByClass<UProject_JQuestComponent>();
        Quests->Definitions.Add(SuccessQuest.Get()); Quests->Definitions.Add(FullQuest.Get()); Quests->Definitions.Add(ByteQuest.Get());
        if (RetireDuringLoad)
        {
            // BeginPlay legitimately accepts configured default quests before profile loading.
            // Preserve the actual pristine state rather than assuming an empty project configuration.
            PristineLoadQuests = Quests->CaptureSnapshot();
            PristineLoadItems = State->GetInventoryComponent()->GetItemInstances();
            const auto* Progress = State->FindComponentByClass<UProject_JProgressionComponent>();
            PristineLoadProgressionRevision = Progress->GetState().Revision;
            PristineLoadExperience = Progress->GetExperience();
            Test->AddInfo(FString::Printf(TEXT("Load-retirement baseline: current=%d history=%d revision=%d items=%d"),
                PristineLoadQuests.States.Num(), PristineLoadQuests.HistoryCount, PristineLoadProgressionRevision, PristineLoadItems.Num()));
        }
        OpenReply = MakeShared<FRewardReply, ESPMode::ThreadSafe>(); const auto Out = OpenReply;
        auto Ready = [Out](bool OK) { Out->OK = OK; Out->Done = true; };
        if (bTrustedDevelopment)
        {
            const auto RejectedLocalName = MakeShared<FRewardReply, ESPMode::ThreadSafe>();
            Profiles->OpenProfile(State, TEXT("QuestItemRewards"), [RejectedLocalName](bool OK) { RejectedLocalName->Done = true; RejectedLocalName->OK = OK; });
            Check(RejectedLocalName->Done && !RejectedLocalName->OK && State->GetCharacterId().Value == TrustedCharacter.Value,
                TEXT("Local-name path cannot replace an authenticated character identity"));
            Profiles->OpenTrustedDevelopmentProfile(State, MoveTemp(Ready));
        }
        else Profiles->OpenProfile(State, TEXT("QuestItemRewards"), MoveTemp(Ready));
        if (RetireDuringLoad) ArmGetterRetirement(State);
    }
    bool Submit(UProject_JQuestDefinition* Definition, bool ReusePaid = false)
    {
        FProject_JPersistentEdit Edit;
        if (ReusePaid) Edit = PaidIntent;
        else { Edit.Kind = EProject_JPersistentEdit::Claim; Edit.Quest = Definition->QuestId; Edit.Request = Profiles->NewRequestId(Player.Get()); }
        if (Definition == SuccessQuest.Get() && !ReusePaid) PaidIntent = Edit;
        Reply = MakeShared<FRewardReply, ESPMode::ThreadSafe>();
        const auto Out = Reply;
        return Check(Profiles->SubmitEdit(Player.Get(), Edit,
            [Out](bool OK, bool First) { Out->Done = true; Out->OK = OK; Out->First = First; },
            [Out](const FProject_JProfileDurableResult& Result) { Out->Durable = Result.Outcome == EProject_JProfileDurableOutcome::Committed; }),
            TEXT("Real public profile port admits the prepared quest claim"));
    }
    bool SubmitEvent()
    {
        Reply = MakeShared<FRewardReply, ESPMode::ThreadSafe>(); const auto Out = Reply;
        return Check(Profiles->SubmitWorldEventReward(Player.Get(), RewardEncounter.Get(), RewardRound,
            [Out](bool OK, bool First) { Out->Done = true; Out->OK = OK; Out->First = First; },
            [Out](const FProject_JProfileDurableResult& R) { Out->Durable = R.Outcome == EProject_JProfileDurableOutcome::Committed; }),
            TEXT("Actual encounter-authorized profile consumer admits event reward"));
    }
    void ObserveFirst()
    {
        const TWeakObjectPtr<AProject_JPlayerState> Weak = Player;
        const auto State = Probe;
        const FName QuestId = SuccessQuest->QuestId, HealthId = Health->ItemId, ManaId = Mana->ItemId;
        const int64 HealthCount = ExpectedHealth;
        const auto Observe = [Weak, State, QuestId, HealthId, ManaId, HealthCount]
        {
            if (!State->Observe || State->Observed) return;
            auto* Owner = Weak.Get(); if (!Owner || Owner->IsActorBeingDestroyed()) return;
            State->Observed = true;
            State->AllInstalled = Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 17 &&
                Owner->FindComponentByClass<UProject_JQuestComponent>()->HasCompleted(QuestId) && CountItem(Owner, HealthId) == HealthCount && CountItem(Owner, ManaId) == 2;
        };
        auto* Owner = Player.Get();
        ProgressHandle = Owner->FindComponentByClass<UProject_JProgressionComponent>()->OnChanged.AddLambda(Observe);
        QuestHandle = Owner->FindComponentByClass<UProject_JQuestComponent>()->OnChanged.AddLambda(Observe);
        InventoryReceiver.Reset(NewObject<UProject_JSourceAuditPhase2UIReceiver>());
        const TWeakObjectPtr<UProject_JSourceAuditPhase2UIReceiver> WeakReceiver = InventoryReceiver.Get();
        InventoryReceiver->OnConsumption = [Observe, WeakReceiver]
        {
            if (auto* Current = WeakReceiver.Get()) Current->bConsumedCallback = false;
            Observe();
        };
        Owner->GetInventoryComponent()->OnItemAdded.AddDynamic(InventoryReceiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
    }
    void Unobserve()
    {
        if (auto* Owner = Player.Get())
        {
            Owner->FindComponentByClass<UProject_JProgressionComponent>()->OnChanged.Remove(ProgressHandle);
            Owner->FindComponentByClass<UProject_JQuestComponent>()->OnChanged.Remove(QuestHandle);
            if (InventoryReceiver.IsValid())
                Owner->GetInventoryComponent()->OnItemAdded.RemoveDynamic(InventoryReceiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
            if (RetirementReceiver.IsValid())
                Owner->GetInventoryComponent()->OnItemAdded.RemoveDynamic(RetirementReceiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
            if (auto* Query = Cast<AProject_JQuestRewardQueryFixture>(Owner)) Query->OnASCQuery = {};
        }
        if (InventoryReceiver.IsValid()) InventoryReceiver->OnConsumption = {};
        if (RetirementReceiver.IsValid()) RetirementReceiver->OnConsumption = {};
        InventoryReceiver.Reset(); RetirementReceiver.Reset();
    }
    bool RejectedUnchanged(UProject_JQuestDefinition* Definition)
    {
        auto* Owner = Player.Get(); auto* Q = Owner->FindComponentByClass<UProject_JQuestComponent>();
        return !Reply->OK && !Reply->Durable && Profiles->IsReady(Owner) && Profiles->GetCommittedRevision(Owner) == RejectedRevision &&
            Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 17 && !Q->HasCompleted(Definition->QuestId) &&
            Q->States.ContainsByPredicate([&](const auto& State) { return State.QuestId == Definition->QuestId && State.Count == 1 && !State.bClaimed; }) && SameItems(Owner, *PaidValues);
    }
public:
    FQuestRewardProfile(FAutomationTestBase* InTest, UGameInstance* InGI, UProject_JConsumableDefinition* InHealth, UProject_JConsumableDefinition* InMana, FString InDatabase, int32 InRetirementMode = 0, bool InTrustedDevelopment = false, bool InWorldEventIntegration = false)
        : Test(InTest), GI(InGI), World(InGI->GetWorld()), Profiles(InGI->GetSubsystem<UProject_JLocalProfileSubsystem>()),
          Health(InHealth), Mana(InMana), Database(MoveTemp(InDatabase)), RetirementMode(InRetirementMode)
    {
        bTrustedDevelopment = InTrustedDevelopment;
        bWorldEventIntegration = InWorldEventIntegration;
        if (bTrustedDevelopment)
        {
            OriginalCommandLine = FCommandLine::Get();
            FCommandLine::Set(*(OriginalCommandLine + TEXT(" -ProjectJDevelopmentServerProfiles")));
        }
        auto Make = [&](const TCHAR* Id, int32 Count, bool WithMana)
        {
            auto* Definition = NewObject<UProject_JQuestDefinition>(InGI);
            Definition->QuestId = Id; Definition->ObjectiveEvent = FName(*(FString(Id) + TEXT(".Event")));
            Definition->RequiredCount = 1; Definition->RewardExperience = 17;
            FProject_JQuestRewardItem Line; Line.Definition = Health->ItemId; Line.DefinitionVersion = Health->SchemaVersion; Line.Count = Count;
            Definition->RewardItems.Add(Line);
            if (WithMana) { Line.Definition = Mana->ItemId; Line.DefinitionVersion = Mana->SchemaVersion; Line.Count = 2; Definition->RewardItems.Add(Line); }
            return Definition;
        };
        SuccessQuest.Reset(Make(TEXT("Reward.Success"), Health->MaxStackCount * 2, true));
        FullQuest.Reset(Make(TEXT("Reward.Full"), 1000000, false));
        ByteQuest.Reset(Make(TEXT("Reward.Bytes"), Health->MaxStackCount * 1000, false));
        ExpectedHealth = 3 + int64(Health->MaxStackCount) * 2;
    }
    ~FQuestRewardProfile()
    {
        Unobserve();
        if (Player.IsValid() && Profiles->IsReady(Player.Get()) && !Profiles->IsCheckpointPending(Player.Get()))
            Profiles->SetCommandStageHookForTest(Player.Get(), {});
        Profiles->HoldCommandCompletionsForTest(false);
        if (Disk.IsValid()) Disk.Wait();
        if (SeedClaims.IsValid()) SeedClaims.Wait();
        if (IntegrationDisk.IsValid()) IntegrationDisk.Wait();
        if (World->HasBegunPlay()) World->EndPlay(EEndPlayReason::Quit);
        GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
        if (bTrustedDevelopment) FCommandLine::Set(*OriginalCommandLine);
    }
    bool Update() override
    {
        if (FPlatformTime::Seconds() - Started > 60) { Test->AddError(TEXT("Quest item reward profile fixture timed out")); return true; }
        if (Step == 0) { Open(); Step = 1; return false; }
        if (Step == 1)
        {
            if (!OpenReply->Done) return false;
            if (!Check(OpenReply->OK && Profiles->IsReady(Player.Get()), TEXT("The actual profile opens with canonical reward definitions"))) return true;
            Player->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(Player.Get(), nullptr);
            Player->FindComponentByClass<UProject_JProgressionComponent>()->InitializeDefaults(nullptr, nullptr, 1);
            Step = bWorldEventIntegration ? 25 : 2; return false;
        }
        if (Step != 6 && Step != 7 && Step != 16 && Step != 19 && Step != 20 && !(RetirementMode && (Step == 8 || Step == 17)) && Profiles->IsCheckpointPending(Player.Get())) return false;
        auto* Owner = Player.Get();
        auto* Q = Owner ? Owner->FindComponentByClass<UProject_JQuestComponent>() : nullptr;
        if (Step == 25)
        {
            if (!Check(Owner && World->GetGameState() && World->GetGameState()->PlayerArray.Contains(Owner),
                TEXT("Verified current player is visible to the real bounded automatic reward sweep"))) return true;
            FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
            auto* Target = World->SpawnActor<AProject_JNPCCharacter>(Params);
            auto* Other = World->SpawnActor<AProject_JNPCCharacter>(Params);
            auto* Avatar = World->SpawnActor<AActor>();
            RewardEncounter = World->SpawnActor<AProject_JWorldEncounter>();
            if (!Check(Target && Other && Avatar && RewardEncounter.IsValid(), TEXT("Native event damage actors spawned"))) return true;
            auto* SourceASC = Owner->GetProjectJAbilitySystemComponent();
            SourceASC->InitAbilityActorInfo(Owner, Avatar);
            Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
            Other->GetAbilitySystemComponent()->InitAbilityActorInfo(Other, Other);
            for (auto* NPC : {Target, Other}) { NPC->GetAttributeSet()->InitMaxHealth(100); NPC->GetAttributeSet()->InitHealth(100); }
            if (!Check(FProjectJQuestRewardProfileTestAccess::AttachActiveNPC(*RewardEncounter.Get(), *Target, 25),
                TEXT("Active encounter starts empty native observation, not a seeded completed entitlement"))) return true;
            RewardRound = RewardEncounter->GetContributionRound();
            auto Apply = [](UAbilitySystemComponent* Source, AActor* Instigator, AActor* Causer, UAbilitySystemComponent* Destination, float Amount)
            {
                TStrongObjectPtr<UGameplayEffect> Effect(NewObject<UGameplayEffect>());
                Effect->DurationPolicy = EGameplayEffectDurationType::Instant;
                auto& Mod = Effect->Modifiers.AddDefaulted_GetRef(); Mod.Attribute = UProject_JAttributeSet::GetHealthAttribute();
                Mod.ModifierOp = EGameplayModOp::Additive; Mod.ModifierMagnitude = FScalableFloat(Amount);
                auto Context = Source->MakeEffectContext(); Context.AddInstigator(Instigator, Causer);
                FGameplayEffectSpec Spec(Effect.Get(), Context, 1); Source->ApplyGameplayEffectSpecToTarget(Spec, Destination);
            };
            // Damage elsewhere and non-player damage to the event must not become this character's contribution.
            Apply(SourceASC, Owner, Avatar, Other->GetAbilitySystemComponent(), -10);
            Apply(Other->GetAbilitySystemComponent(), Other, Other, Target->GetAbilitySystemComponent(), -10);
            Check(RewardEncounter->GetContributionDamage(RewardRound, TrustedCharacter.Value) == 0,
                TEXT("Different target and non-session source produce no player event credit"));
            // A distinct character with a genuinely revoked session cannot contribute.
            auto* RevokedPC = World->SpawnActor<APlayerController>();
            if (!Check(RevokedPC != nullptr, TEXT("Revoked-session source controller spawned"))) return true;
            RevokedPC->SetAsLocalPlayerController();
            if (!RevokedPC->GetPlayerState<AProject_JPlayerState>()) RevokedPC->InitPlayerState();
            RevokedPC->SetPlayer(NewObject<UPlayer>(GI.Get()));
            auto* RevokedSource = RevokedPC->GetPlayerState<AProject_JPlayerState>();
            const auto OtherAccount = FProject_JAccountId::NewId(); const auto OtherCharacter = FProject_JCharacterId::NewId();
            if (!Check(RevokedSource != nullptr, TEXT("Revoked-session PlayerState exists"))) return true;
            ProjectJ::MMO::FServerSessionAttempt OtherAttempt; ProjectJ::MMO::FServerSessionLease RevokedLease;
            auto* Mode = CastChecked<AProject_JGameMode>(World->GetAuthGameMode());
            if (!Check(Mode->BeginTrustedSession(RevokedPC, OtherAccount, OtherCharacter, OtherAttempt) &&
                Mode->CompleteTrustedSession(OtherAttempt, true, RevokedLease) && Mode->RevokeTrustedSession(RevokedSource, RevokedLease) &&
                RevokedSource->GetCharacterId().Value == OtherCharacter.Value && !RevokedSource->CanAcceptSessionCommand(),
                TEXT("Revoked source retains character identity but has no current server session"))) return true;
            RevokedSource->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(RevokedSource, Avatar);
            Apply(RevokedSource->GetProjectJAbilitySystemComponent(), RevokedSource, Avatar, Target->GetAbilitySystemComponent(), -5);
            Check(RewardEncounter->GetContributionDamage(RewardRound, OtherCharacter.Value) == 0,
                TEXT("Real GE from a revoked distinct session cannot earn event contribution"));
            RevokedPC->Destroy();
            if (IsValid(RevokedSource)) RevokedSource->Destroy();
            Apply(SourceASC, Owner, Avatar, Target->GetAbilitySystemComponent(), -20);
            Check(RewardEncounter->GetContributionDamage(RewardRound, TrustedCharacter.Value) == 20,
                TEXT("Actual server GE records effective nonlethal player damage"));
            FProjectJQuestRewardProfileTestAccess::PollEncounter(*RewardEncounter.Get());
            Check(RewardEncounter->Phase == EProject_JEncounterPhase::Active &&
                !RewardEncounter->IsContributionEligible(RewardRound, TrustedCharacter.Value) && !Profiles->IsCheckpointPending(Owner),
                TEXT("Live target cannot close the round or start a reward write"));
            Apply(SourceASC, Owner, Avatar, Target->GetAbilitySystemComponent(), -1000);
            if (!Check(Target->GetAttributeSet()->GetHealth() == 0 &&
                RewardEncounter->GetContributionDamage(RewardRound, TrustedCharacter.Value) == 85,
                TEXT("Killing GE counts only remaining 65 Health, not 1000 attempted damage"))) return true;
            // No explicit SubmitWorldEventReward call: production defeat polling invokes the automatic sweep.
            FProjectJQuestRewardProfileTestAccess::PollEncounter(*RewardEncounter.Get());
            if (!Check(RewardEncounter->Phase == EProject_JEncounterPhase::Cooldown &&
                RewardEncounter->IsContributionEligible(RewardRound, TrustedCharacter.Value),
                TEXT("Real death query closes eligible round after GE observation completes"))) return true;
            if (!Check(Profiles->IsCheckpointPending(Owner) || Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 25,
                TEXT("Defeat automatically admits the reward transaction without explicit claim ingress"))) return true;
            Other->Destroy();
            Step = 26; return false;
        }
        if (Step == 26)
        {
            if (Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() != 25) return false;
            RejectedRevision = Profiles->GetCommittedRevision(Owner);
            FProjectJQuestRewardProfileTestAccess::PollEncounter(*RewardEncounter.Get());
            World->GetSubsystem<UProject_JWorldEventRegistry>()->NotifyTrustedProfileReady(Owner);
            if (!Check(!Profiles->IsCheckpointPending(Owner) && Profiles->GetCommittedRevision(Owner) == RejectedRevision,
                TEXT("Automatic repeated poll and ready notification do not pay a confirmed round twice"))) return true;
            const FString File = Database; const FGuid Character = TrustedCharacter.Value, Round = RewardRound;
            IntegrationDisk = Async(EAsyncExecution::ThreadPool, [File, Character, Round]
            {
                FProject_JSQLiteRepository DB(File); ProjectJ::MMO::FRecord Core, Marker;
                FProject_JLocalProfileSnapshot Snapshot; TArray<ProjectJ::WorldEventClaims::FClaim> Claims;
                return DB.Read({TEXT("LocalProfile"), Character}, Core) && FProject_JLocalProfileSnapshot::Decode(Core.Payload, Snapshot) &&
                    Snapshot.Progression.Experience == 25 && DB.Read(ProjectJ::WorldEventClaims::Key(Character), Marker) &&
                    ProjectJ::WorldEventClaims::Decode(Marker, Claims) && Claims.Num() == 1 && Claims[0].Round == Round && Claims[0].Experience == 25;
            });
            Step = 27; return false;
        }
        if (Step == 27)
        {
            if (!IntegrationDisk.IsReady()) return false;
            Check(IntegrationDisk.Get(), TEXT("Automatic GE-derived reward persisted XP and matching single round marker atomically"));
            return true;
        }
        if (Step == 2)
        {
            // InitializeDefaults above queues a real checkpoint; the existing pending
            // guard must settle before this readonly diagnostic hook can be installed.
            const auto Counts = Probe;
            Profiles->SetCommandStageHookForTest(Player.Get(), [Counts](const TCHAR* Stage)
            {
                if (!FCString::Strcmp(Stage, TEXT("BeforeQuestRewardPlan"))) Counts->Plans.fetch_add(1, std::memory_order_relaxed);
                if (!FCString::Strcmp(Stage, TEXT("AfterQuestRewardPlan"))) Counts->Packed.fetch_add(1, std::memory_order_relaxed);
                if (!FCString::Strcmp(Stage, TEXT("QuestRewardEncodeRejected"))) Counts->EncodeRejected.fetch_add(1, std::memory_order_relaxed);
            });
            if (!Check(Q->Accept(SuccessQuest->QuestId), TEXT("Public quest acceptance admits a valid bounded bundle"))) return true;
            Step = 3; return false;
        }
        if (Step == 3) { Owner->GetInventoryComponent()->AddItemDefinition(Health, 3); Step = 4; return false; }
        if (Step == 4)
        {
            const auto Items = Owner->GetInventoryComponent()->GetItemInstances();
            const auto* Item = Items.FindByPredicate([&](const auto& Value) { return Value.ItemDef == Health; });
            if (!Check(Item && Item->StackCount == 3, TEXT("Canonical initial stack committed"))) return true;
            ExistingHealth = Item->InstanceId; Q->RecordObjective(SuccessQuest->ObjectiveEvent, 1); Step = 5; return false;
        }
        if (Step == 5)
        {
            if (!Check(Q->States.ContainsByPredicate([&](const auto& State) { return State.QuestId == SuccessQuest->QuestId && State.Count == 1; }), TEXT("Actual quest objective progress committed before claim"))) return true;
            ObserveFirst(); Probe->Observe = true;
            Profiles->LoseNextCommitAcknowledgementForTest(Owner); Profiles->HoldCommandCompletionsForTest(true);
            if (!Submit(SuccessQuest.Get())) return true; Step = 6; return false;
        }
        if (Step == 6)
        {
            if (!Profiles->GetHeldCompletionCountForTest()) return false;
            Check(Reply->Durable && !Reply->Done && !Probe->Observed && CountItem(Owner, Health->ItemId) == 3 && CountItem(Owner, Mana->ItemId) == 0 &&
                Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 0 && !Q->HasCompleted(SuccessQuest->QuestId),
                TEXT("No live partial reward is published before durable confirmation"));

            if (bTrustedDevelopment)
            {
                auto* Mode = CastChecked<AProject_JGameMode>(World->GetAuthGameMode());
                const auto OldLease = Owner->CaptureServerSession();
                ProjectJ::MMO::FServerSessionAttempt Attempt; ProjectJ::MMO::FServerSessionLease Rejected;
                Check(Mode->BeginTrustedSession(TrustedController.Get(), TrustedAccount, TrustedCharacter, Attempt)
                    && !Mode->CompleteTrustedSession(Attempt, true, Rejected) && Owner->IsServerSessionCurrent(OldLease),
                    TEXT("Epoch renewal cannot retire a profile with unpublished committed work; old session remains current"));
            }
            Disk = Async(EAsyncExecution::ThreadPool, [File = Database, Character = Owner->GetCharacterId().Value]
            {
                FProject_JSQLiteRepository Repository(File); ProjectJ::MMO::FRecord Core;
                auto Values = Repository.Read({TEXT("LocalProfile"), Character}, Core)
                    ? ProjectJ::LocalProfileHistory::ReadProfileValues(Repository, Character, Core.Payload, {}, Core.Revision) : ProjectJ::LocalProfileHistory::FProfileValues();
                Repository.Close(); return Values;
            });
            Step = 7; return false;
        }
        if (Step == 7)
        {
            if (!Disk.IsReady()) return false;
            PaidValues = Disk.Get();
            if (!Check(PaidValues && PaidValues->Format == 4 && PaidValues->Progression.Experience == 17 &&
                ProjectJ::QuestHistory::Contains(PaidValues->Quests.History, SuccessQuest->QuestId), TEXT("Actual disk core, item ownership and completion page commit together"))) return true;
            int64 HealthTotal = 0, ManaTotal = 0;
            for (const auto& Item : PaidValues->Items) { if (Item.Definition == Health->ItemId) HealthTotal += Item.Count; if (Item.Definition == Mana->ItemId) ManaTotal += Item.Count; }
            Check(HealthTotal == ExpectedHealth && ManaTotal == 2 && PaidValues->Items.ContainsByPredicate([&](const auto& Item) { return Item.Instance == ExistingHealth && Item.Count == Health->MaxStackCount; }),
                TEXT("Actual saved bundle fills the old stack and owns every remainder instance"));
            if (bTrustedDevelopment)
            {
                // SQLite committed, but the original gameplay acknowledgement has not run.
                Unobserve(); Profiles->Detach(Owner);
                if (TrustedController.IsValid()) TrustedController->Destroy();
                if (IsValid(Owner)) Owner->Destroy();
                Player.Reset();
                Profiles->HoldCommandCompletionsForTest(false);
                Open(); Step = 16; return false;
            }
            if (RetirementMode == 1) ArmGetterRetirement(Owner);
            if (RetirementMode == 2)
            {
                const TWeakObjectPtr<AProject_JPlayerState> Weak = Owner;
                const auto Counts = Probe;
                RetirementReceiver.Reset(NewObject<UProject_JSourceAuditPhase2UIReceiver>());
                const TWeakObjectPtr<UProject_JSourceAuditPhase2UIReceiver> WeakReceiver = RetirementReceiver.Get();
                RetirementReceiver->OnConsumption = [Weak, Counts, WeakReceiver]
                {
                    // Let every native dispatch through: the receiver must not hide a broken publication fence.
                    if (auto* Current = WeakReceiver.Get()) Current->bConsumedCallback = false;
                    ++Counts->ItemEvents;
                    if (!Counts->Retired)
                    {
                        Counts->Retired = true;
                        if (auto* Current = Weak.Get())
                            Current->SetIdentity(FProject_JAccountId{FGuid::NewGuid()}, FProject_JCharacterId{FGuid::NewGuid()});
                    }
                };
                Owner->GetInventoryComponent()->OnItemAdded.AddDynamic(RetirementReceiver.Get(), &UProject_JSourceAuditPhase2UIReceiver::OnItem);
            }
            Profiles->HoldCommandCompletionsForTest(false); Step = 8; return false;
        }
        if (Step == 8)
        {
            if (RetirementMode)
            {
                if (!Probe->Retired) return false;
                const auto* Progress = Owner->FindComponentByClass<UProject_JProgressionComponent>();
                if (RetirementMode == 1)
                    Check(!Profiles->IsReady(Owner) && Progress->GetExperience() == 0 && !Q->HasCompleted(SuccessQuest->QuestId) &&
                        CountItem(Owner, Health->ItemId) == 3 && CountItem(Owner, Mana->ItemId) == 0 && !Probe->Observed,
                        TEXT("Actual ASC getter identity retirement installs and publishes none of the already-paid projection"));
                else
                    Check(!Profiles->IsReady(Owner) && Probe->Observed && Probe->AllInstalled && Probe->ItemEvents == 1 && SameItems(Owner, *PaidValues),
                        TEXT("First item callback retires the exact source after all values install; no remaining old-owner item events publish"));
                Unobserve(); Profiles->Detach(Owner); Owner->Destroy(); Player.Reset();
                Probe->Retired = false;
                Open(RetirementMode == 1); Step = RetirementMode == 1 ? 17 : 16; return false;
            }
            if (!Reply->Done) return false;
            if (!Check(Reply->OK && Reply->First && Probe->Observed && Probe->AllInstalled && SameItems(Owner, *PaidValues),
                TEXT("First actual notification sees XP, items and completion already installed"))) return true;
            if (!Submit(SuccessQuest.Get(), true)) return true; Step = 9; return false;
        }
        if (Step == 9)
        {
            if (!Reply->Done) return false;
            Check(Reply->OK && !Reply->First && Probe->Plans.load(std::memory_order_relaxed) == 1 && SameItems(Owner, *PaidValues) &&
                Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 17,
                TEXT("Known receipt skips grant planning/GUID allocation after the current claim plan became invalid"));
            if (!Check(Q->Accept(FullQuest->QuestId), TEXT("Oversized output bundle can be accepted as a bounded authored contract"))) return true; Step = 10; return false;
        }
        if (Step == 10) { Q->RecordObjective(FullQuest->ObjectiveEvent, 1); Step = 11; return false; }
        if (Step == 11) { RejectedRevision = Profiles->GetCommittedRevision(Owner); if (!Submit(FullQuest.Get())) return true; Step = 12; return false; }
        if (Step == 12)
        {
            if (!Reply->Done) return false;
            Check(RejectedUnchanged(FullQuest.Get()), TEXT("Whole-output capacity rejection preserves XP/items/detail/history and revision"));
            if (!Check(Q->Accept(ByteQuest->QuestId), TEXT("Byte-limit reward control accepts"))) return true; Step = 13; return false;
        }
        if (Step == 13) { Q->RecordObjective(ByteQuest->ObjectiveEvent, 1); Step = 14; return false; }
        if (Step == 14) { RejectedRevision = Profiles->GetCommittedRevision(Owner); if (!Submit(ByteQuest.Get())) return true; Step = 15; return false; }
        if (Step == 15)
        {
            if (!Reply->Done) return false;
            Check(RejectedUnchanged(ByteQuest.Get()) && Probe->Plans.load(std::memory_order_relaxed) == 3 &&
                Probe->Packed.load(std::memory_order_relaxed) == 2 && Probe->EncodeRejected.load(std::memory_order_relaxed) == 1,
                TEXT("A plan fitting row count but exceeding hot bytes commits no XP/archive/items"));
            Unobserve(); Profiles->SetCommandStageHookForTest(Owner, {}); Profiles->Detach(Owner); Owner->Destroy(); Player.Reset();
            Open(); Step = 16; return false;
        }
        if (Step == 16)
        {
            if (!OpenReply->Done || Profiles->IsCheckpointPending(Player.Get())) return false;
            if (!Check(OpenReply->OK, TEXT("Reconnect profile ready callback succeeds")))
            { Test->AddInfo(Profiles->GetStatusText(Owner).ToString()); return true; }
            bool Restored = Check(Profiles->IsReady(Owner), TEXT("Reconnect binding remains ready"));
            Restored &= Check(Q->HasCompleted(SuccessQuest->QuestId), TEXT("Reconnect restores successful quest completion"));
            Restored &= Check(!Q->HasCompleted(FullQuest->QuestId), TEXT("Reconnect does not complete rejected full quest"));
            Restored &= Check(!Q->HasCompleted(ByteQuest->QuestId), TEXT("Reconnect does not complete rejected oversized quest"));
            Restored &= Check(SameItems(Owner, *PaidValues), TEXT("Reconnect restores exact paid item identities and counts"));
            Restored &= Check(Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 17,
                TEXT("Reconnect restores exactly 17 XP"));
            if (!Restored) return true;
            const int64 Revision = Profiles->GetCommittedRevision(Owner);
            int64 ProofRevision = 0;
            Check(Profiles->QueryCommittedQuest(Owner, SuccessQuest->QuestId, ProofRevision) && ProofRevision == Revision,
                TEXT("Character/quest lookup after reconnect reports committed result without replaying an old session command"));

            if (bTrustedDevelopment)
            {
                auto* Mode = CastChecked<AProject_JGameMode>(World->GetAuthGameMode());
                const auto OldLease = Owner->CaptureServerSession();
                FProject_JPersistentEdit Stale; Stale.Kind = EProject_JPersistentEdit::Claim;
                Stale.Quest = SuccessQuest->QuestId; Stale.Request = Profiles->NewRequestId(Owner);
                ProjectJ::MMO::FServerSessionAttempt Attempt; ProjectJ::MMO::FServerSessionLease Renewed;
                Check(Mode->BeginTrustedSession(TrustedController.Get(), TrustedAccount, TrustedCharacter, Attempt)
                    && Mode->CompleteTrustedSession(Attempt, true, Renewed) && !(Renewed == OldLease)
                    && Profiles->IsReady(Owner) && SameItems(Owner, *PaidValues)
                    && Profiles->GetCommittedRevision(Owner) == Revision,
                    TEXT("Idle same-controller epoch renewal preserves the exact committed projection and writer"));
                Check(!Owner->IsServerSessionCurrent(OldLease) && !Profiles->SubmitEdit(Owner, Stale),
                    TEXT("Prior runtime lease and prior issued requests cannot cross the renewal boundary"));
            }
            Check(Q->Claim(SuccessQuest->QuestId) && !Profiles->IsCheckpointPending(Owner) && Profiles->GetCommittedRevision(Owner) == Revision && SameItems(Owner, *PaidValues),
                TEXT("Reopened completed quest cannot pay or allocate again through its public Claim"));
            if (bTrustedDevelopment) { Step = 18; return false; }
            return true;
        }
        if (Step == 18)
        {
            RewardEncounter = World->SpawnActor<AProject_JWorldEncounter>(); RewardRound = FGuid::NewGuid();
            if (!Check(RewardEncounter.IsValid(), TEXT("Native reward encounter spawned"))) return true;
            if (!Check(FProjectJQuestRewardProfileTestAccess::SeedWorldEvent(*RewardEncounter.Get(), RewardRound, TrustedCharacter.Value, 1),
                TEXT("Persistence fixture seeds server-confirmed completed entitlement"))) return true;
            FProject_JPersistentEdit Forged; Forged.Kind = EProject_JPersistentEdit::WorldEventExperience;
            Forged.Request = Profiles->NewRequestId(Owner); Forged.Instance = RewardRound; Forged.Amount = 999;
            Check(!Profiles->SubmitEdit(Owner, Forged), TEXT("Ordinary edit ingress cannot manufacture event XP"));
            Check(!Profiles->SubmitWorldEventReward(Owner, RewardEncounter.Get(), FGuid::NewGuid()), TEXT("Unknown round cannot pay"));
            Profiles->HoldCommandCompletionsForTest(true); Profiles->LoseNextCommitAcknowledgementForTest(Owner);
            if (!SubmitEvent()) return true; Step = 19; return false;
        }
        if (Step == 19)
        {
            if (!Reply->Durable || Profiles->GetHeldCompletionCountForTest() == 0) return false;
            Check(!Reply->Done && Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 17,
                TEXT("Event commit is durable while old projection acknowledgement is held"));
            Profiles->Detach(Owner); if (TrustedController.IsValid()) TrustedController->Destroy();
            if (IsValid(Owner)) Owner->Destroy(); Player.Reset(); Profiles->HoldCommandCompletionsForTest(false);
            Open(); Step = 20; return false;
        }
        if (Step == 20)
        {
            if (!OpenReply->Done || Profiles->IsCheckpointPending(Owner)) return false;
            if (!Check(OpenReply->OK && Profiles->IsReady(Owner) &&
                Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 18,
                TEXT("New trusted epoch restores exactly one event XP after commit-response loss"))) return true;
            if (!SubmitEvent()) return true; Step = 21; return false;
        }
        if (Step == 21)
        {
            if (!Reply->Done) return false;
            if (!Check(Reply->OK && !Reply->First && Reply->Durable &&
                Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 18,
                TEXT("Fresh request after reconnect consults persisted marker, not expired receipt; no second XP"))) return true;
            const FString File = Database; const FGuid Character = TrustedCharacter.Value;
            SeedClaims = Async(EAsyncExecution::ThreadPool, [File, Character]
            {
                FProject_JSQLiteRepository DB(File); ProjectJ::MMO::FRecord Marker;
                TArray<ProjectJ::WorldEventClaims::FClaim> Claims;
                if (!DB.Read(ProjectJ::WorldEventClaims::Key(Character), Marker) || !ProjectJ::WorldEventClaims::Decode(Marker, Claims)) return false;
                while (Claims.Num() < ProjectJ::WorldEventClaims::MaxClaims) Claims.Add({FGuid::NewGuid(), 1});
                TArray<uint8> Bytes; if (!ProjectJ::WorldEventClaims::Encode(Claims, Bytes)) return false;
                return DB.Commit({FGuid::NewGuid(), TEXT("TestOnly.SeedEventCapacity"),
                    {{ProjectJ::WorldEventClaims::Key(Character), Marker.Revision, 1, Bytes, false}}}).Status == ProjectJ::MMO::ECommitStatus::Committed;
            });
            Step = 22; return false;
        }
        if (Step == 22)
        {
            if (!SeedClaims.IsReady()) return false;
            if (!Check(SeedClaims.Get(), TEXT("Test-only ledger capacity fixture stored"))) return true;
            RewardRound = FGuid::NewGuid();
            if (!Check(FProjectJQuestRewardProfileTestAccess::SeedWorldEvent(*RewardEncounter.Get(), RewardRound, TrustedCharacter.Value, 1), TEXT("Second completed entitlement seeded"))) return true;
            if (!SubmitEvent()) return true; Step = 23; return false;
        }
        if (Step == 23)
        {
            if (!Reply->Done) return false;
            Check(!Reply->OK && !Reply->Durable && Profiles->IsReady(Owner), TEXT("Full event ledger refuses only new rewards, not the whole profile"));
            FProject_JPersistentEdit Ordinary; Ordinary.Kind = EProject_JPersistentEdit::Experience;
            Ordinary.Request = Profiles->NewRequestId(Owner); Ordinary.Amount = 1;
            Reply = MakeShared<FRewardReply, ESPMode::ThreadSafe>(); const auto Out = Reply;
            if (!Check(Profiles->SubmitEdit(Owner, Ordinary, [Out](bool OK, bool First) { Out->Done = true; Out->OK = OK; }),
                TEXT("Ordinary progression remains admissible after event capacity rejection"))) return true;
            Step = 24; return false;
        }
        if (Step == 24)
        {
            if (!Reply->Done) return false;
            Check(Reply->OK && Profiles->IsReady(Owner) && Owner->FindComponentByClass<UProject_JProgressionComponent>()->GetExperience() == 19,
                TEXT("Ordinary progression commits after event ledger is full"));
            return true;
        }
        if (Step == 17)
        {
            if (!Probe->Retired) return false;
            const auto* Progress = Owner->FindComponentByClass<UProject_JProgressionComponent>();
            Check(!Profiles->IsReady(Owner) && Progress->GetState().Revision == PristineLoadProgressionRevision &&
                Progress->GetExperience() == PristineLoadExperience && Owner->GetInventoryComponent()->GetItemInstances() == PristineLoadItems &&
                Q->CaptureSnapshot().Version == PristineLoadQuests.Version && Q->States == PristineLoadQuests.States &&
                Q->GetCompletedCount() == PristineLoadQuests.HistoryCount && !Q->HasCompleted(SuccessQuest->QuestId),
                TEXT("Loaded profile getter retirement preserves the new owner's pristine components; paid storage is not redirected"));
            Unobserve(); Profiles->Detach(Owner); Owner->Destroy(); Player.Reset();
            Open(); Step = 16; return false;
        }
        return false;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestRewardPersistenceTest, "ProjectJ.MMO.Persistence.QuestItemRewards",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestRewardPersistenceTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
    auto* World = GI->GetWorld();
    auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
    auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
    auto* Mana = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJManaPotion.DA_ProjectJManaPotion"));
    if (!TestTrue(TEXT("Native reward fixture uses actual canonical registered consumables"), Data && Health && Mana && Data->LoadPrimaryAssetDefinitions() &&
        Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health && Data->GetItemDefinition(Mana->ItemId, Mana->SchemaVersion) == Mana &&
        Health->MaxStackCount > 2 && Health->MaxStackCount <= 200))
    {
        GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;
    }
    const FString Database = FPaths::ProjectSavedDir() / TEXT("Validation/QuestItemRewards165") / FGuid::NewGuid().ToString() / TEXT("Profile.sqlite");
    FProjectJQuestRewardProfileTestAccess::SetDatabasePath(*GI->GetSubsystem<UProject_JLocalProfileSubsystem>(), Database);
    World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    ADD_LATENT_AUTOMATION_COMMAND(FQuestRewardProfile(this, GI, Health, Mana, Database));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTrustedQuestRewardPersistenceTest, "ProjectJ.MMO.Persistence.TrustedDevelopmentQuestReconnect",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTrustedQuestRewardPersistenceTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
    auto* World = GI->GetWorld();
    auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
    auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
    auto* Mana = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJManaPotion.DA_ProjectJManaPotion"));
    FURL URL; URL.AddOption(TEXT("game=/Script/Project_J.Project_JNetworkLoadGameMode"));
    if (!TestTrue(TEXT("Canonical content and native GameMode available"), Data && Health && Mana && Data->LoadPrimaryAssetDefinitions()
        && Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health
        && Data->GetItemDefinition(Mana->ItemId, Mana->SchemaVersion) == Mana && World->SetGameMode(URL)))
    { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    auto* Mode = CastChecked<AProject_JGameMode>(World->GetAuthGameMode());
    Mode->bRequireTrustedSessions = true;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Validation/TrustedQuestReconnect") / FGuid::NewGuid().ToString();
    FProjectJQuestRewardProfileTestAccess::SetDatabasePath(*GI->GetSubsystem<UProject_JLocalProfileSubsystem>(), Folder / TEXT("Local.sqlite"));
    World->InitializeActorsForPlay(URL); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    ADD_LATENT_AUTOMATION_COMMAND(FQuestRewardProfile(this, GI, Health, Mana, Folder / TEXT("DevelopmentServerProfiles.sqlite"), 0, true));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWorldEventGEAutomaticRewardTest, "ProjectJ.MMO.Persistence.WorldEventGEAutomaticReward",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWorldEventGEAutomaticRewardTest::RunTest(const FString&)
{
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
    auto* World = GI->GetWorld();
    auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
    auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
    auto* Mana = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJManaPotion.DA_ProjectJManaPotion"));
    FURL URL; URL.AddOption(TEXT("game=/Script/Project_J.Project_JNetworkLoadGameMode"));
    if (!TestTrue(TEXT("Canonical content and native GameMode available"), Data && Health && Mana && Data->LoadPrimaryAssetDefinitions()
        && Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health
        && Data->GetItemDefinition(Mana->ItemId, Mana->SchemaVersion) == Mana && World->SetGameMode(URL)))
    { GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
    auto* Mode = CastChecked<AProject_JGameMode>(World->GetAuthGameMode());
    Mode->bRequireTrustedSessions = true;
    const FString Folder = FPaths::ProjectSavedDir() / TEXT("Validation/WorldEventGEAutomaticReward") / FGuid::NewGuid().ToString();
    FProjectJQuestRewardProfileTestAccess::SetDatabasePath(*GI->GetSubsystem<UProject_JLocalProfileSubsystem>(), Folder / TEXT("Local.sqlite"));
    World->InitializeActorsForPlay(URL); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    ADD_LATENT_AUTOMATION_COMMAND(FQuestRewardProfile(this, GI, Health, Mana, Folder / TEXT("DevelopmentServerProfiles.sqlite"), 0, true, true));
    return true;
}

namespace
{
bool RunRetiredQuestRewardProfile(FAutomationTestBase* Test, int32 Mode)
{
    auto* GI = NewObject<UGameInstance>(GEngine); GI->InitializeStandalone();
    auto* World = GI->GetWorld();
    auto* Data = GI->GetSubsystem<UProject_JCharacterDataSubsystem>();
    auto* Health = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJHealthPotion.DA_ProjectJHealthPotion"));
    auto* Mana = LoadObject<UProject_JConsumableDefinition>(nullptr, TEXT("/Game/UI/Gameplay/DA_ProjectJManaPotion.DA_ProjectJManaPotion"));
    if (!Test->TestTrue(TEXT("Retirement controls use actual canonical registered consumables"), Data && Health && Mana && Data->LoadPrimaryAssetDefinitions() &&
        Data->GetItemDefinition(Health->ItemId, Health->SchemaVersion) == Health && Data->GetItemDefinition(Mana->ItemId, Mana->SchemaVersion) == Mana &&
        Health->MaxStackCount > 2 && Health->MaxStackCount <= 200))
    {
        GI->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false;
    }
    const FString Database = FPaths::ProjectSavedDir() / TEXT("Validation/QuestItemRewardRetirement165") / FGuid::NewGuid().ToString() / TEXT("Profile.sqlite");
    FProjectJQuestRewardProfileTestAccess::SetDatabasePath(*GI->GetSubsystem<UProject_JLocalProfileSubsystem>(), Database);
    World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
    ADD_LATENT_AUTOMATION_COMMAND(FQuestRewardProfile(Test, GI, Health, Mana, Database, Mode));
    return true;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestRewardRetirementTest, "ProjectJ.MMO.Persistence.QuestItemRewardRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestRewardRetirementTest::RunTest(const FString&)
{
    return RunRetiredQuestRewardProfile(this, 1);
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJQuestRewardPublicationRetirementTest, "ProjectJ.MMO.Persistence.QuestItemRewardPublicationRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJQuestRewardPublicationRetirementTest::RunTest(const FString&)
{
    return RunRetiredQuestRewardProfile(this, 2);
}
#endif
