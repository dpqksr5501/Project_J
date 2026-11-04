#include "Testing/Project_JGatheringPrototypeFixture.h"
#include "Gathering/Project_JGatheringNode.h"
#include "Movement/Project_JMoverComponent.h"
#include "Game/Project_JQuestComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Crafting/Project_JCraftingStation.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "Blueprint/WidgetTree.h"
#include "Components/TextBlock.h"
#include "UnrealClient.h"
#include "HAL/FileManager.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Companion/Project_JCompanionComponent.h"
#include "Companion/Project_JCompanionItemDefinition.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JNPCCharacter.h"
#include "Project_JPlayerCharacter.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JPlayerUIComponent.h"

AProject_JGatheringPrototypeFixture::AProject_JGatheringPrototypeFixture()
{
	PrimaryActorTick.bCanEverTick = true; PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = .05f;
}
void AProject_JGatheringPrototypeFixture::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJGatheringPrototype"))) return;
	Started = FPlatformTime::Seconds();
	FString Profile; FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), Profile);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJGatheringOutput="), Output);
	bCompanion = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCompanionPrototype"));
	bCompound = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCompoundQuestPrototype"));
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJCompoundQuestScreenshot="), QuestScreenshot);
	if (bCompound && bCompanion) { Finish(false, TEXT("Choose one explicit optional prototype workload")); return; }
	if (GetNetMode() != NM_Standalone || GetWorld()->GetPackage()->GetName() != TEXT("/Game/Tests/Gathering/L_GatheringPrototype") ||
		Profile != TEXT("OwnedGatheringPrototype") || Output.IsEmpty())
	{ Finish(false, TEXT("Requires the exact owned gathering map, standalone profile and explicit output")); return; }
	SetActorTickEnabled(true);
#endif
}
void AProject_JGatheringPrototypeFixture::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (bFinished) return;
	if (FPlatformTime::Seconds() - Started > 45) { Finish(false, TEXT("Bounded gathering/companion prototype timed out")); return; }
	auto* PC = GetWorld()->GetFirstPlayerController();
	auto* PS = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	auto* Avatar = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
	auto* Profiles = GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!PC || !PC->IsLocalPlayerController() || !PS || !Avatar || PS->GetPawn() != Avatar || !Profiles ||
		!Profiles->IsReady(PS) || Profiles->IsCheckpointPending(PS)) return;
	auto* Inventory = PS->GetInventoryComponent();
	auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	auto* UI = PC->FindComponentByClass<UProject_JPlayerUIComponent>();
	auto* Model = UI ? UI->GetInventoryModel() : nullptr;
    if (bCompound)
    {
        const FName QuestId(TEXT("Prototype.GatherAndCraft"));
        auto* Quests = PS->FindComponentByClass<UProject_JQuestComponent>();
        auto* Progression = PS->FindComponentByClass<UProject_JProgressionComponent>();
        const auto* Def = Quests ? Quests->FindDefinition(QuestId) : nullptr;
        if (!Def || Def->Objectives.Num() != 2 || Def->RewardExperience != 7 || !Progression)
        { Finish(false, TEXT("Canonical compound definition must resolve before profile opening")); return; }
        const auto Snapshot = Quests->CaptureSnapshot();
        const auto* State = Snapshot.States.FindByPredicate([QuestId](const auto& S) { return S.QuestId == QuestId; });
        if (Phase == 0)
        {
            if (!Inventory->GetItemInstances().IsEmpty() || State || !Quests->Accept(QuestId) || !Profiles->IsCheckpointPending(PS))
            { Finish(false, TEXT("Isolated compound acceptance must wait for the existing durable profile command")); return; }
            Phase = 10; return;
        }
        if (Phase != 15 && (!State || Snapshot.Version != 2 || State->Count != 0 || State->Objectives.Num() != 2))
        { Finish(false, TEXT("Persisted compound state or schema is invalid")); return; }
        if (Phase == 10)
        {
            int32 Nodes = 0, Benches = 0;
            for (TActorIterator<AProject_JGatheringNode> It(GetWorld()); It; ++It)
                if (It->NodeId == TEXT("Prototype.Gathering.HealthPotion.01")) { Node = *It; ++Nodes; }
            for (TActorIterator<AProject_JCraftingStation> It(GetWorld()); It; ++It)
                if (It->RecipeId == TEXT("Prototype.Alchemy.HealthToMana")) { QuestBench = *It; ++Benches; }
            Yield = Data ? Data->GetItemDefinition(TEXT("HealthPotion"), 1) : nullptr;
            QuestMana = Data ? Data->GetItemDefinition(TEXT("ManaPotion"), 1) : nullptr;
            if (Nodes != 1 || Benches != 1 || !Yield || !QuestMana || !Node->NodeVisual->GetStaticMesh() ||
                !QuestBench->BenchVisual->GetStaticMesh() || !Node->CanInteract_Implementation(Avatar) || State->bClaimed ||
                State->GetObjectiveCount(TEXT("Collect")) || State->GetObjectiveCount(TEXT("Brew")))
            { Finish(false, TEXT("Owned node/bench, canonical items or empty compound progress are invalid")); return; }
            Revision = Profiles->GetCommittedRevision(PS); InitialExperience = Progression->GetExperience();
            Avatar->TryInteract();
            if (!Profiles->IsCheckpointPending(PS) || Node->GetPhase() != EProject_JGatheringPhase::Pending)
            { Finish(false, TEXT("Actual first interaction did not enter gathering")); return; }
            Phase = 11; return;
        }
        int32 Health = 0, Mana = 0;
        for (const auto& Item : Inventory->GetItemInstances())
        {
            if (Item.ItemDef == Yield) Health += Item.StackCount;
            else if (Item.ItemDef == QuestMana) Mana += Item.StackCount;
            else { Finish(false, TEXT("Unexpected ownership in the compound prototype")); return; }
        }
        if (Phase == 11 || Phase == 13)
        {
            const int32 Expected = Phase == 11 ? 1 : 2;
            if (Health != Expected || Mana || State->GetObjectiveCount(TEXT("Collect")) != Expected ||
                State->GetObjectiveCount(TEXT("Brew")) || State->bClaimed || Profiles->GetCommittedRevision(PS) != Revision + Expected)
            { Finish(false, TEXT("Gathered item and objective were not published in the same committed revision")); return; }
            const auto* Summary = Model ? Model->FindItemSummary(Yield->ItemId) : nullptr;
            if (!Summary || Summary->Quantity != Expected) return;
            if (Phase == 11) { Phase = 12; return; }
            if (Def->IsComplete(*State) || Quests->Claim(QuestId))
            { Finish(false, TEXT("Gather-only progress incorrectly completed the AND quest")); return; }
            auto* Mover = Avatar->GetMover();
            if (!Mover || Mover->IsBackendAsync()) { Finish(false, TEXT("Owned bench placement requires the normal synchronous Mover backend")); return; }
            auto Placement = MakeShared<FTeleportEffect>();
            Placement->TargetLocation = QuestBench->GetActorLocation() + FVector(-150,0,100);
            Placement->bUseActorRotation = true;
            Mover->QueueInstantMovementEffect(Placement);
            Phase = 17; return;
        }
        if (Phase == 17)
        {
            if (Health != 2 || Mana || State->GetObjectiveCount(TEXT("Collect")) != 2 ||
                State->GetObjectiveCount(TEXT("Brew")) || State->bClaimed || Profiles->GetCommittedRevision(PS) != Revision + 2 || !IsValid(QuestBench))
            { Finish(false, TEXT("Committed gather state changed before the queued bench placement completed")); return; }
            // Observe actual movement before using the normal range/LOS interaction.
            if (!QuestBench->CanInteract_Implementation(Avatar)) return;
            Avatar->TryInteract();
            if (!Profiles->IsCheckpointPending(PS)) { Finish(false, TEXT("Actual bench interaction did not admit one atomic craft")); return; }
            Phase = 14; return;
        }
        if (Phase == 12)
        {
            if (Node->GetPhase() != EProject_JGatheringPhase::Available) return;
            Avatar->TryInteract();
            if (!Profiles->IsCheckpointPending(PS)) { Finish(false, TEXT("Second actual gathering interaction was not admitted")); return; }
            Phase = 13; return;
        }
        if (Health || Mana != 1 || State->GetObjectiveCount(TEXT("Collect")) != 2 || State->GetObjectiveCount(TEXT("Brew")) != 1 || !Def->IsComplete(*State))
        { Finish(false, TEXT("Craft materials, result and both objective counters disagree")); return; }
        if (Phase == 14)
        {
            if (Profiles->GetCommittedRevision(PS) != Revision + 3 || State->bClaimed || Progression->GetExperience() != InitialExperience)
            { Finish(false, TEXT("Craft must commit once before reward admission")); return; }
            TArray<UUserWidget*> Widgets; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UProject_JPlayerHUDWidget::StaticClass(), false);
            UProject_JPlayerHUDWidget* HUD = nullptr;
            for (auto* Widget : Widgets) if (Widget && Widget->GetOwningPlayer() == PC)
            { if (HUD) { Finish(false, TEXT("Ambiguous owner HUD")); return; } HUD = Cast<UProject_JPlayerHUDWidget>(Widget); }
            if (!HUD) return;
            HUD->ToggleWindow(TEXT("Quests"));
            bool Visible = false;
            // Journal content lives inside the separate HUDWindow UserWidget tree.
            HUD->WidgetTree->ForEachWidgetAndDescendants([&Visible](UWidget* Child)
            {
                if (const auto* Text = Cast<UTextBlock>(Child))
                { const FString Value = Text->GetText().ToString(); Visible |= Value.Contains(TEXT("2/2")) && Value.Contains(TEXT("1/1")); }
            });
            if (!Visible) { Finish(false, TEXT("Existing owner quest journal did not project both objective counters")); return; }
            if (!QuestScreenshot.IsEmpty())
            {
                if (FScreenshotRequest::IsScreenshotRequested()) { HUD->CloseWindow(TEXT("Quests")); return; }
                QuestScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddWeakLambda(this, [this]
                { bQuestScreenshotProcessed = true; });
                FScreenshotRequest::RequestScreenshot(QuestScreenshot, true, false); Phase = 16; return;
            }
            HUD->ExecuteCommand(FName(*(FString(TEXT("Claim.")) + QuestId.ToString())));
            Phase = 15; return;
        }
        if (Phase == 16)
        {
            if (!bQuestScreenshotProcessed) return;
            // Remove after Broadcast has returned; self-removal destroys the lambda's captures.
            if (QuestScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(QuestScreenshotHandle); QuestScreenshotHandle.Reset(); }
            if (IFileManager::Get().FileSize(*QuestScreenshot) <= 0) return;
            TArray<UUserWidget*> Widgets; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(this, Widgets, UProject_JPlayerHUDWidget::StaticClass(), false);
            for (auto* Widget : Widgets) if (Widget && Widget->GetOwningPlayer() == PC)
            { CastChecked<UProject_JPlayerHUDWidget>(Widget)->ExecuteCommand(FName(*(FString(TEXT("Claim.")) + QuestId.ToString()))); Phase = 15; return; }
            return;
        }
        if (Phase == 15)
        {
            if (State || Snapshot.HistoryCount != 1 || !Snapshot.HasHydratedHistory() ||
                !ProjectJ::QuestHistory::Contains(Snapshot.History, QuestId) ||
                Progression->GetExperience() != InitialExperience + 7 || Profiles->GetCommittedRevision(PS) != Revision + 4)
            { Finish(false, TEXT("Existing owner HUD command did not atomically claim XP once")); return; }
            const auto* Summary = Model ? Model->FindItemSummary(QuestMana->ItemId) : nullptr;
            if (!Summary || Summary->Quantity != 1) return;
            Finish(true, TEXT("Canonical AND quest: actual gather2/craft1, same-commit item+progress, owner journal and atomic XP claim passed")); return;
        }
        return;
    }
	if (Phase == 0)
	{
		if (!Inventory->GetItemInstances().IsEmpty()) { Finish(false, TEXT("Prototype requires an empty isolated profile")); return; }
		int32 Count = 0;
		for (TActorIterator<AProject_JGatheringNode> It(GetWorld()); It; ++It)
			if (It->NodeId == TEXT("Prototype.Gathering.HealthPotion.01")) { Node = *It; ++Count; }
		if (Count != 1 || !Node->NodeVisual->GetStaticMesh() || !Node->NodeVisual->IsVisible() ||
			Node->YieldCount != 1 || Node->CooldownSeconds != 3 || Node->GetPhase() != EProject_JGatheringPhase::Available)
		{ Finish(false, TEXT("Owned visible gathering node or declared prototype tuning is invalid")); return; }
		Yield = Data ? Data->GetItemDefinition(Node->YieldItemId, Node->YieldItemVersion) : nullptr;
		if (!Yield || !Node->CanInteract_Implementation(Avatar)) { Finish(false, TEXT("Canonical yield or real spatial eligibility is unavailable")); return; }
		Revision = Profiles->GetCommittedRevision(PS); Avatar->TryInteract();
		if (!Profiles->IsCheckpointPending(PS) || Node->GetPhase() != EProject_JGatheringPhase::Pending || Node->TryGather(Avatar))
		{ Finish(false, TEXT("Actual interaction must admit one pending gather and reject its duplicate")); return; }
		Phase = 1; return;
	}
	int32 Quantity = 0, CompanionQuantity = 0;
	for (const auto& Item : Inventory->GetItemInstances())
	{
		if (Item.ItemDef == Yield) Quantity += Item.StackCount;
		else if (Item.ItemDef == CompanionDefinition)
		{
			if (CompanionItem.IsValid() && CompanionItem != Item.InstanceId) { Finish(false, TEXT("Companion source item identity changed during runtime use")); return; }
			CompanionQuantity += Item.StackCount; CompanionItem = Item.InstanceId;
		}
		else { Finish(false, TEXT("Unexpected ownership in the isolated prototype")); return; }
	}
	const auto* Summary = Model ? Model->FindItemSummary(Yield->ItemId) : nullptr;
	if (Phase == 1 || Phase == 3)
	{
		const int32 Expected = Phase == 1 ? 1 : 2;
		const double Now = GetWorld()->GetTimeSeconds(); // Same authority clock as the node ledger.
		const double Deadline = Node->GetNextAvailableServerTime();
		if (Quantity != Expected || Profiles->GetCommittedRevision(PS) != Revision + Expected ||
			!FMath::IsFinite(Now) || !FMath::IsFinite(Deadline) || Deadline <= 0)
		{ Finish(false, TEXT("Committed yield, single revision or finite cooldown deadline disagree")); return; }
		const bool bCooldownRemaining = Now < Deadline;
		const auto ExpectedPhase = bCooldownRemaining ? EProject_JGatheringPhase::Cooldown : EProject_JGatheringPhase::Available;
		if (Node->GetPhase() != ExpectedPhase || (bCooldownRemaining && Node->TryGather(Avatar)))
		{ Finish(false, TEXT("Actual clock/deadline and cooldown reuse admission disagree")); return; }
		// RHI/UI work can outlast this prototype's3s cooldown. Expiry is valid;
		// do not invoke another gather until the explicit next operation below.
		if (!Summary || Summary->Quantity != Expected) return; // Existing next-tick UI projection.
		if (Phase == 1) { Phase = 2; return; }
		if (!bCompanion) { Finish(true, TEXT("Actual node interaction, durable yield, cooldown/reuse and existing UI agree")); return; }
		CompanionDefinition = LoadObject<UProject_JCompanionItemDefinition>(nullptr,
			TEXT("/Game/DataAssetSets/Companion/DA_Companion_Follow_NPC_Test.DA_Companion_Follow_NPC_Test"));
		if (!CompanionDefinition || !CompanionDefinition->IsFollowDefinitionValid() || !Data ||
			Data->GetItemDefinition(CompanionDefinition->ItemId, CompanionDefinition->SchemaVersion) != CompanionDefinition)
		{ Finish(false, TEXT("Canonical companion item metadata is unavailable in this build")); return; }
		Inventory->AddItemDefinition(CompanionDefinition, 1, 1);
		if (!Profiles->IsCheckpointPending(PS)) { Finish(false, TEXT("Companion item ownership was not admitted by the existing profile adapter")); return; }
		Phase = 4; return;
	}
	if (Phase == 2)
	{
		if (Node->GetPhase() != EProject_JGatheringPhase::Available) return;
		Avatar->TryInteract();
		if (!Profiles->IsCheckpointPending(PS) || Node->GetPhase() != EProject_JGatheringPhase::Pending || Quantity != 1)
		{ Finish(false, TEXT("Expired cooldown did not admit the next independent gathering operation")); return; }
		Phase = 3; return;
	}
	if (Quantity != 2 || CompanionQuantity != 1 || !CompanionItem.IsValid() || Profiles->GetCommittedRevision(PS) != Revision + 3)
	{ Finish(false, TEXT("Companion runtime use changed the committed source item, gathered yield or profile revision")); return; }
	auto* Companion = PS->GetCompanionComponent();
	if (!Companion || !Model) return;
	const UProject_JInventoryEntry* Entry = nullptr;
	for (const auto& Candidate : Model->InventoryEntries)
		if (Candidate && Candidate->Item.InstanceId == CompanionItem) { Entry = Candidate.Get(); break; }
	if (Phase == 4)
	{
		if (!Entry || Model->bPending) return;
		if (!Model->Activate(Entry) || Model->bPending || !Companion->HasPendingPreparation())
		{ Finish(false, TEXT("Existing owner inventory activation did not enter shared companion preparation")); return; }
		Phase = 5; return;
	}
	auto* NPC = Companion->GetActiveCompanion();
	if (!NPC) return;
	UProject_JNPCActionComponent* Follow = nullptr;
	TInlineComponentArray<UProject_JNPCActionComponent*> Actions(NPC);
	for (auto* Action : Actions) if (Action->GetOwnershipToken() && Action->GetIntentTarget() == Avatar) { Follow = Action; break; }
	const auto* NPCASC = NPC->GetAbilitySystemComponent();
	if (NPC->GetClass() != CompanionDefinition->CompanionClass.Get() || NPC->GetClass()->GetPathName() != TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C") ||
		NPC->GetNetConnection() || NPC->GetNetOwningPlayer() || !NPCASC || NPCASC->GetOwnerActor() != NPC || NPCASC->GetAvatarActor() != NPC ||
		!Follow || Follow->GetAttackAbilityHandle().IsValid())
	{ Finish(false, TEXT("Actual authored companion class, isolated GAS authority or owner follow consumer disagree")); return; }
	++CompanionSummons;
	if (Phase == 5)
	{
		RetiredCompanion = NPC; Companion->RequestCompanion(FGuid{}); ++CompanionDismissals;
		if (Companion->GetActiveCompanion() || !RetiredCompanion->IsActorBeingDestroyed() || !Entry || !Model->Activate(Entry))
		{ Finish(false, TEXT("Actual dismissal and same-item re-summon did not preserve the runtime lease boundary")); return; }
		Phase = 6; return;
	}
	if (NPC == RetiredCompanion) { Finish(false, TEXT("Re-summon reused the retired actor")); return; }
	Companion->RequestCompanion(FGuid{}); ++CompanionDismissals;
	if (Companion->GetActiveCompanion() || !NPC->IsActorBeingDestroyed()) { Finish(false, TEXT("Final companion dismissal left an active actor")); return; }
	Finish(true, TEXT("Gathering durable yield/UI and actual canonical item activation, authored summon/dismiss/re-summon passed"));
#endif
}
void AProject_JGatheringPrototypeFixture::Finish(bool Success, const TCHAR* Reason)
{
	if (bFinished) return;
	bFinished = true; SetActorTickEnabled(false);
	if (QuestScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(QuestScreenshotHandle); QuestScreenshotHandle.Reset(); }
	const FString Body = FString::Printf(TEXT("success=%d\nphase=%d\ncompanion=%d\ncompound_quest=%d\ncompound_screenshot=%d\ncompanion_summons=%d\ncompanion_dismissals=%d\nprototype_yield=1_health_potion_per_gather\nnav_movement_verified=0\nelapsed_s=%.3f\nreason=%s\n"),
		Success, Phase, bCompanion, bCompound, bQuestScreenshotProcessed, CompanionSummons, CompanionDismissals, FPlatformTime::Seconds() - Started, Reason);
	const bool Saved = !Output.IsEmpty() && FFileHelper::SaveStringToFile(Body, *(Output / TEXT("GatheringPrototype.txt")));
	UE_LOG(LogTemp, Display, TEXT("PROJECT_J_GATHERING_PROTOTYPE success=%d saved=%d phase=%d companion=%d reason=%s"), Success, Saved, Phase, bCompanion, Reason);
	FPlatformMisc::RequestExitWithStatus(false, Success && Saved ? 0 : 1);
}

void AProject_JGatheringPrototypeFixture::EndPlay(const EEndPlayReason::Type Reason)
{
    if (QuestScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(QuestScreenshotHandle); QuestScreenshotHandle.Reset(); }
    Super::EndPlay(Reason);
}
