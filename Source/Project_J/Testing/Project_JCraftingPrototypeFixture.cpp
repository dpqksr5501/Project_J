#include "Testing/Project_JCraftingPrototypeFixture.h"
#include "Backend/Project_JLocalProfileSubsystem.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JCraftingJobsComponent.h"
#include "Components/Button.h"
#include "Blueprint/WidgetBlueprintLibrary.h"
#include "UI/Project_JCraftingJobsWidget.h"
#include "UnrealClient.h"
#include "UObject/StrongObjectPtr.h"
#include "Components/StaticMeshComponent.h"
#include "Crafting/Project_JCraftingRecipeDefinition.h"
#include "Crafting/Project_JCraftingStation.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Game/Project_JPlayerState.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Project_JPlayerCharacter.h"
#include "UI/Project_JInventoryViewModel.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Inventory/Project_JItemDefinition.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Game/Project_JQuestComponent.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/MemoryWriter.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/Base64.h"
#include "Misc/Timespan.h"

#if !UE_BUILD_SHIPPING
namespace
{
constexpr int32 ReconnectMaxValues = 65536;
constexpr int32 ReconnectMaxBase64 = ((ReconnectMaxValues + 2) / 3) * 4;
constexpr int32 ReconnectMaxMarker = 131072;
const TCHAR* ReconnectProfile = TEXT("OwnedCraftingPrototype");

bool ReconnectInteger(const FString& Text, int64& Out)
{
 if (Text.IsEmpty() || Text.Len() > 19) return false;
 int64 Value = 0;
 for (TCHAR Ch : Text)
 { if (Ch < '0' || Ch > '9' || Value > (MAX_int64 - (Ch - '0')) / 10) return false; Value = Value * 10 + (Ch - '0'); }
 if (LexToString(Value) != Text) return false;
 Out = Value; return true;
}

// A comparison witness, not a profile format. Never deserialize these bytes
// into components. Names are strings; UObject identities/local FName indices,
// presentation generations and remaining-time samples deliberately stay out.
bool CaptureReconnectValues(AProject_JPlayerState* PS, TArray<uint8>& Out)
{
 auto* Progress = PS ? PS->FindComponentByClass<UProject_JProgressionComponent>() : nullptr;
 auto* Quest = PS ? PS->FindComponentByClass<UProject_JQuestComponent>() : nullptr;
 auto* Inventory = PS ? PS->GetInventoryComponent() : nullptr;
 auto* Equipment = PS ? PS->GetEquipmentManagerComponent() : nullptr;
 auto* Jobs = PS ? PS->GetCraftingJobsComponent() : nullptr;
 if (!Progress || !Quest || !Inventory || !Equipment || !Jobs) return false;
 auto P = Progress->CaptureSnapshot(); auto Q = Quest->CaptureSnapshot();
 const auto InventoryValues = Inventory->GetItemInstances();
 const auto Slots = Equipment->CapturePersistentEquipment();
 // Use the existing public validator for real slot identity/uniqueness and
 // bag+slot consistency; starter equipment is ordinary committed ownership.
 if (!Equipment->ValidatePersistentEquipment(Slots, InventoryValues, true)) return false;
 TArray<FProject_JPersistentItem> Items;
 for (const auto& Item : InventoryValues)
 {
  if (!Item.ItemDef || !Item.InstanceId.IsValid() || Items.Num() >= ProjectJ::Crafting::MaxOwnedEntries) return false;
  FProject_JPersistentItem Value; Value.Instance = Item.InstanceId; Value.Definition = Item.ItemDef->ItemId;
  Value.DefinitionVersion = Item.ItemDef->SchemaVersion; Value.Level = Item.ItemLevel;
  Value.Count = Item.StackCount; Value.Order = Item.BagOrder; Items.Add(Value);
 }
 // Match the backend CaptureItems representation: one owned item per UUID,
 // with its slot merged into a bag row or an explicit non-bag equipment row.
 for (const auto& Entry : Slots)
 {
  const auto& Item = Entry.ItemInstance;
  if (!Item.ItemDef || !Item.InstanceId.IsValid()) return false;
  if (auto* Found = Items.FindByPredicate([&](const auto& Value) { return Value.Instance == Item.InstanceId; }))
  {
   if (Found->Definition != Item.ItemDef->ItemId || Found->DefinitionVersion != Item.ItemDef->SchemaVersion ||
    Found->Level != Item.ItemLevel || Found->Count != 1 || Found->Slot != EProject_JEquipmentSlot::None) return false;
   Found->Slot = Entry.Slot;
  }
  else
  {
   if (Items.Num() >= ProjectJ::Crafting::MaxOwnedEntries) return false;
   FProject_JPersistentItem Value; Value.Instance = Item.InstanceId; Value.Definition = Item.ItemDef->ItemId;
   Value.DefinitionVersion = Item.ItemDef->SchemaVersion; Value.Level = Item.ItemLevel; Value.Count = 1;
   Value.bInBag = false; Value.Slot = Entry.Slot; Items.Add(Value);
  }
 }
 auto J = Jobs->CaptureJobs();
 if (Q.States.Num() > 128 || !Q.HasHydratedHistory() || P.AdvancementHistory.Num() > 256 || !ProjectJ::Crafting::ValidateJobState(Items, J)) return false;
 Items.Sort([](const auto& A, const auto& B) { return A.Instance.ToString() < B.Instance.ToString(); });
 Q.States.Sort([](const auto& A, const auto& B) { return A.QuestId.LexicalLess(B.QuestId); });
 J.Sort([](const auto& A, const auto& B) { return A.Id.ToString() < B.Id.ToString(); });
 TArray<uint8> Values; FMemoryWriter Writer(Values);
 const auto Put = [&Writer](auto Value) { Writer << Value; };
 const auto Name = [&Put](FName Id) { Put(Id.ToString().ToLower()); };
 const auto ItemValue = [&Put, &Name](const FProject_JPersistentItem& Item)
 { Put(Item.Instance); Name(Item.Definition); Put(Item.DefinitionVersion); Put(Item.Level); Put(Item.Count); Put(Item.Order); Put(uint8(Item.Slot)); Put(Item.bInBag); };
 Put(FString(TEXT("ProjectJ.CraftingReconnectWitness.1")));
 Put(PS->GetAccountId().Value); Put(PS->GetCharacterId().Value);
 Put(P.SchemaVersion); Put(P.Experience); Name(P.ClassId); Put(P.Level); Put(P.AdvancementHistory.Num());
 for (FName Id : P.AdvancementHistory) Name(Id); // Acquisition order is meaningful.
 Put(Q.Version); Put(Q.HistoryCount); Put(Q.HistoryRootDigest); Put(Q.States.Num());
 for (auto& State : Q.States)
 {
  if (State.Objectives.Num() > UProject_JQuestComponent::MaxObjectivesPerQuest) return false;
  State.Objectives.Sort([](const auto& A, const auto& B) { return A.ObjectiveId.LexicalLess(B.ObjectiveId); });
  Name(State.QuestId); Put(State.Count); Put(State.bClaimed); Put(State.bTracked); Put(State.Objectives.Num());
  for (const auto& Objective : State.Objectives) { Name(Objective.ObjectiveId); Put(Objective.Count); }
 }
 Put(Items.Num()); for (const auto& Item : Items) ItemValue(Item);
 Put(J.Num());
 for (auto& Job : J)
 {
  Put(Job.SchemaVersion); Put(Job.Id); Name(Job.RecipeId); Put(Job.RecipeVersion); Put(Job.Batches); Put(Job.RulesDigest);
  Put(Job.StartedUtcTicks); Put(Job.DueUtcTicks);
  Job.Escrow.Sort([](const auto& A, const auto& B) { return A.Instance.ToString() < B.Instance.ToString(); });
  Put(Job.Escrow.Num()); for (const auto& Item : Job.Escrow) ItemValue(Item);
  Put(Job.Outputs.Num()); // Preserve frozen output order.
  for (const auto& Output : Job.Outputs)
  { Name(Output.Definition); Put(Output.DefinitionVersion); Put(Output.Level); Put(Output.CountPerBatch); Put(Output.MaxStackCount); }
 }
 if (Writer.IsError() || Values.IsEmpty() || Values.Num() > ReconnectMaxValues) return false;
 Out = MoveTemp(Values); return true;
}
}
#endif

AProject_JCraftingPrototypeFixture::AProject_JCraftingPrototypeFixture()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;
	PrimaryActorTick.TickInterval = .05f;
}
void AProject_JCraftingPrototypeFixture::BeginPlay()
{
	Super::BeginPlay();
#if !UE_BUILD_SHIPPING
	FString Reconnect;
	const bool bReconnectRequested = FParse::Value(FCommandLine::Get(), TEXT("ProjectJCraftingReconnect="), Reconnect);
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJCraftingPrototype")))
	{ if (bReconnectRequested) Finish(false, TEXT("Reconnect requires the explicit owned crafting fixture")); return; }
	Started = FPlatformTime::Seconds();
	FString Profile;
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJLocalProfile="), Profile);
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJCraftingOutput="), Output);
	if (GetNetMode() != NM_Standalone || GetWorld()->GetPackage()->GetName() != TEXT("/Game/Tests/Crafting/L_CraftingPrototype") ||
		Profile != TEXT("OwnedCraftingPrototype") || Output.IsEmpty())
	{
		Finish(false, TEXT("Requires the exact owned map, standalone profile and explicit output")); return;
	}
	bTimedUI = FParse::Param(FCommandLine::Get(), TEXT("ProjectJTimedCraftingUI"));
	if (bReconnectRequested)
	{
		FString Run;
		if (Reconnect == TEXT("SeedPending")) ReconnectMode = EReconnectMode::SeedPending;
		else if (Reconnect == TEXT("RestoreCollect")) ReconnectMode = EReconnectMode::RestoreCollect;
		if (bTimedUI || ReconnectMode == EReconnectMode::None ||
			!FParse::Value(FCommandLine::Get(), TEXT("ProjectJCraftingRun="), Run) ||
			!FGuid::ParseExact(Run, EGuidFormats::DigitsWithHyphens, ReconnectRun) || !ReconnectRun.IsValid() ||
			!FParse::Value(FCommandLine::Get(), TEXT("ProjectJCraftingWitness="), ReconnectWitnessPath) ||
			ReconnectWitnessPath.IsEmpty() || FPaths::IsRelative(ReconnectWitnessPath))
		{ Finish(false, TEXT("Reconnect requires one exact mode, run GUID and absolute witness path without TimedUI")); return; }
		ReconnectWitnessPath = FPaths::ConvertRelativePathToFull(ReconnectWitnessPath);
		if ((ReconnectMode == EReconnectMode::SeedPending && FPaths::FileExists(ReconnectWitnessPath)) ||
			(ReconnectMode == EReconnectMode::RestoreCollect && !FPaths::FileExists(ReconnectWitnessPath)))
		{ Finish(false, TEXT("Preserve an existing seed witness; restore requires the first process witness")); return; }
	}
	SetActorTickEnabled(true);
#endif
}
void AProject_JCraftingPrototypeFixture::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
#if !UE_BUILD_SHIPPING
	if (bFinished) return;
	if (FPlatformTime::Seconds() - Started > 45) { Finish(false, TEXT("Bounded prototype timed out")); return; }
	auto* PC = GetWorld()->GetFirstPlayerController();
	auto* PS = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	auto* Avatar = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
	auto* Profiles = GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>();
	if (!PC || !PC->IsLocalPlayerController() || !PS || !Avatar || PS->GetPawn() != Avatar ||
		!Profiles || !Profiles->IsReady(PS) || Profiles->IsCheckpointPending(PS)) return;
	if (Player && Player != PS) { Finish(false, TEXT("Owner changed while prototype was active")); return; }
	Player = PS;
	// Restoration observes normal OpenProfile values before all fresh material,
	// transient-recipe, interaction and UI setup.
	if (ReconnectMode == EReconnectMode::RestoreCollect) { AdvanceReconnect(Avatar, Profiles); return; }

	auto* Inventory = PS->GetInventoryComponent();
	if (Phase == 0)
	{
		// The runner provides a fresh isolated UserDir. Never edit a pre-existing bag.
		if (!Inventory->GetItemInstances().IsEmpty()) { Finish(false, TEXT("Prototype requires an empty isolated profile")); return; }
		auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
		auto* Recipe = Data ? Data->GetCraftingRecipe(TEXT("Prototype.Alchemy.HealthToMana"), 1) : nullptr;
		ProjectJ::Crafting::FRecipeValues Values; FString Rules;
		if (!Recipe || !Recipe->Capture(*Data, Values, Rules) || Values.Inputs.Num() != 1 || Values.Outputs.Num() != 1 ||
			Values.Inputs[0].CountPerBatch != 2 || Values.Outputs[0].CountPerBatch != 1)
		{
			Finish(false, TEXT("Authored prototype recipe did not resolve to the declared2-to-1 balance")); return;
		}
		Health = Data->GetItemDefinition(Values.Inputs[0].Definition, Values.Inputs[0].DefinitionVersion);
		Mana = Data->GetItemDefinition(Values.Outputs[0].Definition, Values.Outputs[0].DefinitionVersion);
		int32 Benches = 0;
		for (TActorIterator<AProject_JCraftingStation> It(GetWorld()); It; ++It)
			if (It->RecipeId == Recipe->RecipeId) { Bench = *It; ++Benches; }
		if (Benches != 1 || !Bench->BenchVisual->GetStaticMesh() || !Bench->BenchVisual->IsVisible() ||
			!Bench->CanInteract_Implementation(Avatar))
		{
			Finish(false, TEXT("One visible authored bench must be in normal interaction range")); return;
		}
		if (bTimedUI || ReconnectMode == EReconnectMode::SeedPending)
		{
			OriginalRecipeSources = Data->CraftingRecipeDefinitions;
			OriginalBenchRecipe = Bench->RecipeId; OriginalBenchVersion = Bench->RecipeVersion;
			TimedRecipe = DuplicateObject<UProject_JCraftingRecipeDefinition>(Recipe, this);
			if (!TimedRecipe) { Finish(false, TEXT("Transient timed recipe allocation failed")); return; }
			TimedRecipe->RecipeId = TEXT("Prototype.Alchemy.HealthToMana.TimedUI");
			TimedRecipe->DurationSecondsPerBatch = 2;
			TimedData = Data; Data->CraftingRecipeDefinitions.Add(TSoftObjectPtr<UProject_JCraftingRecipeDefinition>(TimedRecipe)); bRecipeInjected = true;
			if (!Data->RefreshCraftingRecipes() || Data->GetCraftingRecipe(TimedRecipe->RecipeId, TimedRecipe->SchemaVersion) != TimedRecipe)
			{ Finish(false, TEXT("Transient timed recipe canonical registration failed")); return; }
			Bench->RecipeId = TimedRecipe->RecipeId; Bench->RecipeVersion = TimedRecipe->SchemaVersion;
		}
		Inventory->AddItemDefinition(Health, 6, 1);
		if (!Profiles->IsCheckpointPending(PS)) { Finish(false, TEXT("Prototype materials were not admitted by existing profile owner")); return; }
		Phase = 1; return;
	}
	int32 HealthCount = 0, ManaCount = 0;
	for (const auto& Item : Inventory->GetItemInstances())
	{
		if (Item.ItemDef == Health) HealthCount += Item.StackCount;
		else if (Item.ItemDef == Mana) ManaCount += Item.StackCount;
		else { Finish(false, TEXT("Unexpected item in isolated prototype profile")); return; }
	}
	if (ReconnectMode == EReconnectMode::SeedPending) { AdvanceReconnect(Avatar, Profiles); return; }
	if (bTimedUI) { AdvanceTimedUI(Avatar, Profiles, HealthCount, ManaCount); return; }
	if (Phase == 1)
	{
		if (HealthCount != 6 || ManaCount != 0) { Finish(false, TEXT("Materials were not committed as an ordinary inventory edit")); return; }
		Revision = Profiles->GetCommittedRevision(PS);
		Avatar->TryInteract();
		if (!Profiles->IsCheckpointPending(PS)) { Finish(false, TEXT("Actual character interaction did not select the authored bench")); return; }
		Phase = 2; return;
	}
	auto* UI = PC->FindComponentByClass<UProject_JPlayerUIComponent>();
	auto* Model = UI ? UI->GetInventoryModel() : nullptr;
	const auto* HealthSummary = Model ? Model->FindItemSummary(Health->ItemId) : nullptr;
	const auto* ManaSummary = Model ? Model->FindItemSummary(Mana->ItemId) : nullptr;
	if (HealthCount != 4 || ManaCount != 1 || Profiles->GetCommittedRevision(PS) != Revision + 1)
	{
		Finish(false, TEXT("Committed material/output counts or single revision disagree")); return;
	}
	// Owner presentation coalesces deltas until the next game tick. Observe the
	// existing events without forcing a model refresh; the overall45s bound remains.
	if (!HealthSummary || HealthSummary->Quantity != 4 || !ManaSummary || ManaSummary->Quantity != 1) return;
	Finish(true, TEXT("Authored recipe, visible bench, interaction, atomic profile and existing owner UI agree"));
#endif
}
void AProject_JCraftingPrototypeFixture::Finish(bool Success, const TCHAR* Reason)
{
	if (bFinished) return;
	bFinished = true; SetActorTickEnabled(false);
	if (TimedScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(TimedScreenshotHandle); TimedScreenshotHandle.Reset(); }
	if (bTimedUI)
	{
		if (!RestoreTimedRecipe()) { Success = false; Reason = TEXT("Transient recipe could not restore its exact runtime sources"); }
		Success &= FFileHelper::SaveStringToFile(TimedRows, *(Output / TEXT("timed-crafting-ui.csv")));
	}
	if (ReconnectMode != EReconnectMode::None)
	{
		if (!RestoreTimedRecipe()) { Success = false; Reason = TEXT("Reconnect could not restore the exact transient recipe sources"); }
		Success &= FFileHelper::SaveStringToFile(ReconnectRows, *(Output / TEXT("crafting-reconnect.csv")));
	}
	const FString Body = FString::Printf(TEXT("success=%d\nphase=%d\nelapsed_s=%.3f\nprototype_balance=2_health_to_1_mana\nreason=%s\n"),
		Success, Phase, FPlatformTime::Seconds() - Started, Reason);
	const bool Saved = !Output.IsEmpty() && FFileHelper::SaveStringToFile(Body, *(Output / TEXT("CraftingPrototype.txt")));
	UE_LOG(LogTemp, Display, TEXT("PROJECT_J_CRAFTING_PROTOTYPE success=%d saved=%d phase=%d reason=%s"), Success, Saved, Phase, Reason);
	FPlatformMisc::RequestExitWithStatus(false, Success && Saved ? 0 : 1);
}

namespace
{
UUserWidget* FindOwnedCraftingWidget(APlayerController* PC, TSubclassOf<UUserWidget> Class)
{
 TArray<UUserWidget*> Found; UWidgetBlueprintLibrary::GetAllWidgetsOfClass(PC, Found, Class, false);
 UUserWidget* Result = nullptr;
 for (auto* Widget : Found) if (IsValid(Widget) && Widget->GetOwningPlayer() == PC && Widget->GetParent())
 { if (Result) return nullptr; Result = Widget; }
 return Result;
}
UButton* FindOwnedCraftingButton(APlayerController* PC, FName Name)
{
 auto* Row = FindOwnedCraftingWidget(PC, UProject_JCraftingJobRow::StaticClass());
 return Row ? Cast<UButton>(Row->GetWidgetFromName(Name)) : nullptr;
}
}
void AProject_JCraftingPrototypeFixture::AdvanceTimedUI(AProject_JPlayerCharacter* Avatar, UProject_JLocalProfileSubsystem* Profiles, int32 HealthCount, int32 ManaCount)
{
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 auto* PC = GetWorld()->GetFirstPlayerController();
 auto* UI = PC ? PC->FindComponentByClass<UProject_JPlayerUIComponent>() : nullptr;
 auto* Jobs = Player ? Player->GetCraftingJobsComponent() : nullptr;
 if (!UI || !Jobs || UI->GetCraftingJobsComponent() != Jobs) return;
 const TStrongObjectPtr<UProject_JCraftingJobsComponent> KeepJobs(Jobs);
 if (Phase == 1)
 {
  if (HealthCount != 6 || ManaCount != 0 || !Jobs->GetJobs().IsEmpty()) { Finish(false, TEXT("Timed UI requires the isolated committed input baseline")); return; }
  if (TimedWarmAt == 0) { UI->ToggleWindow(TEXT("Crafting")); bTimedWindowOpen = true; TimedWarmAt = FPlatformTime::Seconds(); return; }
  if (!FindOwnedCraftingWidget(PC, UProject_JCraftingJobsWidget::StaticClass()) || FPlatformTime::Seconds() - TimedWarmAt < .2) return;
  UI->ToggleWindow(TEXT("Crafting")); bTimedWindowOpen = false;
  Revision = Profiles->GetCommittedRevision(Player);
  Avatar->TryInteract();
  if (!Profiles->IsCheckpointPending(Player)) { Finish(false, TEXT("Actual interaction did not start the transient timed recipe")); return; }
  Phase = 100; return;
 }
 const int32 Count = Jobs->GetJobs().Num();
 if (Phase == 100)
 {
  if (HealthCount != 4 || ManaCount != 0 || Count != 1) { Finish(false, TEXT("Timed start did not escrow inputs without granting an output")); return; }
  if (!bTimedWindowOpen) { UI->ToggleWindow(TEXT("Crafting")); bTimedWindowOpen = true; }
  if (UI->GetPresentedCraftingJobCount() != 1) return;
  TimedObservedJob = Jobs->GetJobs()[0].JobId;
  const double Remaining = Jobs->GetRemainingSeconds(TimedObservedJob);
  if (!bTimedScreenshotRequested)
  {
   if (Remaining <= 0) { Finish(false, TEXT("Missed the real waiting interval; it is not a waiting-view pass")); return; }
   TimedRows += FString::Printf(TEXT("100,%d,%d,%d,%d,%.3f,%d,0,1\n"), HealthCount, ManaCount, Count, UI->GetPresentedCraftingJobCount(), Remaining, int32(Jobs->GetRequestState().Stage));
   const TWeakObjectPtr<ThisClass> WeakThis(this);
   TimedScreenshotHandle = FScreenshotRequest::OnScreenshotRequestProcessed().AddLambda([WeakThis] { if (auto* Self = WeakThis.Get()) Self->bTimedScreenshotProcessed = true; });
   bTimedScreenshotRequested = true;
   FScreenshotRequest::RequestScreenshot(Output / TEXT("timed-crafting-jobs.png"), true, false); return;
  }
  if (!bTimedScreenshotProcessed) return;
  FScreenshotRequest::OnScreenshotRequestProcessed().Remove(TimedScreenshotHandle); TimedScreenshotHandle.Reset();
  if (!FPaths::FileExists(Output / TEXT("timed-crafting-jobs.png"))) { Finish(false, TEXT("Actual UI screenshot file was not produced")); return; }
  auto* Button = FindOwnedCraftingButton(PC, TEXT("CancelButton")); if (!Button || !Button->GetIsEnabled()) return;
  Button->OnClicked.Broadcast();
  const auto Request = Jobs->GetRequestState();
  const bool Pending = Request.Stage == EProject_JCraftingRequestStage::Pending && Request.JobId == TimedObservedJob && Request.Action == EProject_JCraftingJobAction::Cancel;
  TimedRows += FString::Printf(TEXT("101,%d,%d,%d,%d,%.3f,%d,%d,%d\n"), HealthCount, ManaCount, Count, UI->GetPresentedCraftingJobCount(), Remaining, int32(Request.Stage), Pending, Pending);
  if (!Pending) { Finish(false, TEXT("Actual cancel button did not enter the owned request lifecycle")); return; }
  Phase = 101; return;
 }
 if (Phase == 101)
 {
  if (HealthCount != 6 || ManaCount != 0 || Count != 0 || Jobs->GetRequestState().Stage != EProject_JCraftingRequestStage::Confirmed)
  { Finish(false, TEXT("Actual cancel request did not atomically refund its escrow")); return; }
  if (UI->GetPresentedCraftingJobCount() != 0) return;
  TimedRows += FString::Printf(TEXT("102,%d,%d,%d,%d,-1,%d,0,1\n"), HealthCount, ManaCount, Count, UI->GetPresentedCraftingJobCount(), int32(Jobs->GetRequestState().Stage));
  UI->ToggleWindow(TEXT("Crafting")); bTimedWindowOpen = false;
  Avatar->TryInteract();
  if (!Profiles->IsCheckpointPending(Player)) { Finish(false, TEXT("Second actual interaction did not start after refund")); return; }
  Phase = 102; return;
 }
 if (Phase == 102)
 {
  if (HealthCount != 4 || ManaCount != 0 || Count != 1) { Finish(false, TEXT("Second start did not preserve the production balance")); return; }
  if (!bTimedWindowOpen) { UI->ToggleWindow(TEXT("Crafting")); bTimedWindowOpen = true; }
  if (UI->GetPresentedCraftingJobCount() != 1) return;
  TimedObservedJob = Jobs->GetJobs()[0].JobId;
  if (!Jobs->IsJobDue(TimedObservedJob)) return;
  auto* Button = FindOwnedCraftingButton(PC, TEXT("CollectButton")); if (!Button || !Button->GetIsEnabled()) return;
  Button->OnClicked.Broadcast();
  const auto Request = Jobs->GetRequestState();
  const bool Pending = Request.Stage == EProject_JCraftingRequestStage::Pending && Request.JobId == TimedObservedJob && Request.Action == EProject_JCraftingJobAction::Collect;
  TimedRows += FString::Printf(TEXT("103,%d,%d,%d,%d,%.3f,%d,%d,%d\n"), HealthCount, ManaCount, Count, UI->GetPresentedCraftingJobCount(), Jobs->GetRemainingSeconds(TimedObservedJob), int32(Request.Stage), Pending, Pending);
  if (!Pending) { Finish(false, TEXT("Actual collect button did not enter the owned request lifecycle")); return; }
  Phase = 103; return;
 }
 if (Phase == 103)
 {
  if (HealthCount != 4 || ManaCount != 1 || Count != 0 || Jobs->GetRequestState().Stage != EProject_JCraftingRequestStage::Confirmed || Profiles->GetCommittedRevision(Player) != Revision + 4)
  { Finish(false, TEXT("Natural UTC collection did not preserve balance single commits and request confirmation")); return; }
  if (UI->GetPresentedCraftingJobCount() != 0) return;
  auto* Model = UI->GetInventoryModel(); const auto* H = Model ? Model->FindItemSummary(Health->ItemId) : nullptr; const auto* M = Model ? Model->FindItemSummary(Mana->ItemId) : nullptr;
  if (!H || H->Quantity != 4 || !M || M->Quantity != 1) return;
  TimedRows += FString::Printf(TEXT("104,%d,%d,%d,%d,-1,%d,0,1\n"), HealthCount, ManaCount, Count, UI->GetPresentedCraftingJobCount(), int32(Jobs->GetRequestState().Stage));
  Finish(true, TEXT("Real timed station interaction actual UI buttons escrow refund UTC collect and owner view agree"));
 }
}
bool AProject_JCraftingPrototypeFixture::RestoreTimedRecipe()
{
 if (!bRecipeInjected) return true;
 bool Restored = true;
 if (auto* Data = TimedData.Get())
 {
  auto Expected = OriginalRecipeSources; Expected.Add(TSoftObjectPtr<UProject_JCraftingRecipeDefinition>(TimedRecipe));
  if (Data->CraftingRecipeDefinitions == Expected)
  {
   Data->CraftingRecipeDefinitions = OriginalRecipeSources;
   if (GetWorld() && !GetWorld()->bIsTearingDown) Restored &= Data->RefreshCraftingRecipes();
  }
  else Restored = false; // Preserve an unexpected external writer instead of overwriting it.
 }
 else Restored = false;
 if (IsValid(Bench) && TimedRecipe && Bench->RecipeId == TimedRecipe->RecipeId)
 { Bench->RecipeId = OriginalBenchRecipe; Bench->RecipeVersion = OriginalBenchVersion; }
 bRecipeInjected = false; TimedData.Reset(); TimedRecipe = nullptr; OriginalRecipeSources.Reset();
 return Restored;
}
void AProject_JCraftingPrototypeFixture::EndPlay(EEndPlayReason::Type Reason)
{
 if (TimedScreenshotHandle.IsValid()) { FScreenshotRequest::OnScreenshotRequestProcessed().Remove(TimedScreenshotHandle); TimedScreenshotHandle.Reset(); }
 RestoreTimedRecipe();
 Super::EndPlay(Reason);
}


void AProject_JCraftingPrototypeFixture::AppendReconnectRow(const TCHAR* Label, int32 HealthCount, int32 ManaCount, bool bRepeatAdmitted)
{
 auto* Profiles = GetGameInstance()->GetSubsystem<UProject_JLocalProfileSubsystem>();
 auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
 auto* Jobs = Player->GetCraftingJobsComponent();
 const auto Values = Jobs->CaptureJobs();
 const bool RecipeResolved = Data && Data->GetCraftingRecipe(TEXT("Prototype.Alchemy.HealthToMana.TimedUI"), 1);
 ReconnectRows += FString::Printf(TEXT("%s,%u,%u,%s,%s,%s,%lld,%d,%d,%d,%s,%lld,%lld,%d,%d,%d,%d,%d,%d,%d,%d,1\n"),
  Label, FPlatformProcess::GetCurrentProcessId(), ReconnectSeedPid,
  *ReconnectRun.ToString(EGuidFormats::DigitsWithHyphens), *ReconnectAccount.ToString(EGuidFormats::DigitsWithHyphens),
  *ReconnectCharacter.ToString(EGuidFormats::DigitsWithHyphens), Profiles->GetCommittedRevision(Player),
  HealthCount, ManaCount, Values.Num(), *ReconnectJob.ToString(EGuidFormats::DigitsWithHyphens), ReconnectStartedUtc, ReconnectDueUtc,
  ReconnectValueBytes, int32(bReconnectRestored), int32(Profiles->IsCheckpointPending(Player)), int32(bReconnectComplete),
  int32(bReconnectConfirmed), int32(bReconnectFirst), int32(bRepeatAdmitted), int32(RecipeResolved));
}

void AProject_JCraftingPrototypeFixture::AdvanceReconnect(AProject_JPlayerCharacter* Avatar, UProject_JLocalProfileSubsystem* Profiles)
{
#if !UE_BUILD_SHIPPING
 const TStrongObjectPtr<ThisClass> KeepSelf(this);
 const TStrongObjectPtr<AProject_JPlayerState> KeepPlayer(Player);
 const TStrongObjectPtr<UProject_JCraftingJobsComponent> KeepJobs(Player ? Player->GetCraftingJobsComponent() : nullptr);
 const TStrongObjectPtr<UWorld> KeepWorld(GetWorld());
 auto* World = KeepWorld.Get();
 const TStrongObjectPtr<APlayerController> KeepPC(World ? World->GetFirstPlayerController() : nullptr);
 const TStrongObjectPtr<AProject_JPlayerCharacter> KeepAvatar(Avatar);
 const TStrongObjectPtr<UProject_JLocalProfileSubsystem> KeepProfiles(Profiles);
 auto* PC = KeepPC.Get();
 FGuid ExpectedAccount, ExpectedCharacter;
 if (!KeepPlayer || !KeepJobs || !UProject_JLocalProfileSubsystem::ResolveLocalIdentity(ReconnectProfile, ExpectedAccount, ExpectedCharacter))
 { Finish(false, TEXT("Reconnect has no current owner or exact local identity")); return; }
 const auto Current = [&]
 {
  return !bFinished && !IsActorBeingDestroyed() && IsValid(KeepPlayer.Get()) && !KeepPlayer->IsActorBeingDestroyed() &&
   Player == KeepPlayer.Get() && World && GetWorld() == World && !World->bIsTearingDown && IsValid(PC) && !PC->IsActorBeingDestroyed() &&
   IsValid(KeepAvatar.Get()) && !Avatar->IsActorBeingDestroyed() && Avatar->GetWorld() == World && KeepProfiles &&
   PC->GetPlayerState<AProject_JPlayerState>() == Player && PC->GetPawn() == Avatar && Player->GetPawn() == Avatar &&
   Player->GetCraftingJobsComponent() == KeepJobs.Get() && IsValid(KeepJobs.Get()) && !KeepJobs->IsBeingDestroyed() &&
   Player->GetAccountId().Value == ExpectedAccount && Player->GetCharacterId().Value == ExpectedCharacter && Profiles->IsReady(Player);
 };
 if (!Current()) { Finish(false, TEXT("Reconnect owner identity or possession changed")); return; }
 ReconnectAccount = ExpectedAccount; ReconnectCharacter = ExpectedCharacter;
 auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
 if (!Data) { Finish(false, TEXT("Reconnect canonical definitions unavailable")); return; }
 if (ReconnectMode == EReconnectMode::RestoreCollect && Phase == 0)
 { Health = Data->GetItemDefinition(TEXT("HealthPotion"), 1); Mana = Data->GetItemDefinition(TEXT("ManaPotion"), 1); }
 if (!Health || !Mana) { Finish(false, TEXT("Reconnect canonical potion identities unavailable")); return; }
 int32 HealthCount = 0, ManaCount = 0;
 for (const auto& Item : Player->GetInventoryComponent()->GetItemInstances())
 {
  if (Item.ItemDef == Health) HealthCount += Item.StackCount;
  else if (Item.ItemDef == Mana) ManaCount += Item.StackCount;
  else { Finish(false, TEXT("Unexpected item in isolated reconnect profile")); return; }
 }
 const auto Jobs = KeepJobs->CaptureJobs();
 if (ReconnectMode == EReconnectMode::SeedPending)
 {
  if (Phase == 1)
  {
   if (HealthCount != 6 || ManaCount != 0 || !Jobs.IsEmpty()) { Finish(false, TEXT("Pending seed requires the ordinary committed input baseline")); return; }
   Revision = Profiles->GetCommittedRevision(Player);
   Avatar->TryInteract();
   if (!Current() || !Profiles->IsCheckpointPending(Player)) { Finish(false, TEXT("Pending seed interaction did not admit the real timed start")); return; }
   Phase = 200; return;
  }
  if (Phase != 200 || HealthCount != 4 || ManaCount != 0 || Jobs.Num() != 1 || Profiles->GetCommittedRevision(Player) != Revision + 1)
  { Finish(false, TEXT("Pending seed did not commit exactly one escrow-backed start")); return; }
  const auto& Job = Jobs[0];
  if (Job.RecipeId != TEXT("Prototype.Alchemy.HealthToMana.TimedUI") || Job.RecipeVersion != 1 || Job.Batches != 1 ||
   Job.StartedUtcTicks <= 0 || Job.DueUtcTicks - Job.StartedUtcTicks != 2 * ETimespan::TicksPerSecond || Job.Escrow.Num() != 1 ||
   Job.Escrow[0].Definition != Health->ItemId || Job.Escrow[0].Count != 2 || Job.Outputs.Num() != 1 ||
   Job.Outputs[0].Definition != Mana->ItemId || Job.Outputs[0].CountPerBatch != 1)
  { Finish(false, TEXT("Pending seed frozen terms disagree with the existing2-second prototype")); return; }
  TArray<uint8> Values;
  if (!CaptureReconnectValues(Player, Values) || !Current() || Profiles->IsCheckpointPending(Player))
  { Finish(false, TEXT("Pending seed could not capture stable committed public values")); return; }
  ReconnectRevision = Profiles->GetCommittedRevision(Player); ReconnectSeedPid = FPlatformProcess::GetCurrentProcessId();
  ReconnectJob = Job.Id; ReconnectStartedUtc = Job.StartedUtcTicks; ReconnectDueUtc = Job.DueUtcTicks; ReconnectValueBytes = Values.Num();
  const auto Marker = MakeShared<FJsonObject>();
  Marker->SetNumberField(TEXT("schema"), 1); Marker->SetStringField(TEXT("run"), ReconnectRun.ToString(EGuidFormats::DigitsWithHyphens));
  Marker->SetStringField(TEXT("profile"), ReconnectProfile); Marker->SetNumberField(TEXT("seed_pid"), ReconnectSeedPid);
  Marker->SetStringField(TEXT("account"), ReconnectAccount.ToString(EGuidFormats::DigitsWithHyphens));
  Marker->SetStringField(TEXT("character"), ReconnectCharacter.ToString(EGuidFormats::DigitsWithHyphens));
  Marker->SetStringField(TEXT("revision"), LexToString(ReconnectRevision)); Marker->SetStringField(TEXT("job"), ReconnectJob.ToString(EGuidFormats::DigitsWithHyphens));
  Marker->SetStringField(TEXT("started"), LexToString(ReconnectStartedUtc)); Marker->SetStringField(TEXT("due"), LexToString(ReconnectDueUtc));
  Marker->SetStringField(TEXT("values"), FBase64::Encode(Values));
  FString Text;
  if (ReconnectRevision <= 0 || !FJsonSerializer::Serialize(Marker, TJsonWriterFactory<>::Create(&Text)) || Text.Len() > ReconnectMaxMarker ||
   FPaths::FileExists(ReconnectWitnessPath) || !FFileHelper::SaveStringToFile(Text, *ReconnectWitnessPath,
    FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM, &IFileManager::Get(), FILEWRITE_NoReplaceExisting))
  { Finish(false, TEXT("Pending seed witness failed; preserve any existing witness")); return; }
  AppendReconnectRow(TEXT("seed_committed"), HealthCount, ManaCount); Phase = 201;
  Finish(true, TEXT("Committed pending job recorded before normal process exit")); return;
 }
 if (Phase == 0)
 {
  const int64 Size = IFileManager::Get().FileSize(*ReconnectWitnessPath);
  FString Text; TSharedPtr<FJsonObject> Marker;
  if (Size <= 0 || Size > ReconnectMaxMarker || !FFileHelper::LoadFileToString(Text, *ReconnectWitnessPath) || Text.Len() > ReconnectMaxMarker ||
   !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Marker) || !Marker.IsValid() || Marker->Values.Num() != 11)
  { Finish(false, TEXT("Reconnect witness is missing oversized or not the exact comparison schema")); return; }
  double Schema = 0, SeedPid = 0; FString Run, Profile, Account, Character, RevisionText, Job, StartedText, DueText, Encoded;
  FGuid ParsedRun, ParsedAccount, ParsedCharacter;
  TArray<uint8> Expected;
  if (!Marker->TryGetNumberField(TEXT("schema"), Schema) || Schema != 1 || !Marker->TryGetStringField(TEXT("run"), Run) ||
   !FGuid::ParseExact(Run, EGuidFormats::DigitsWithHyphens, ParsedRun) || ParsedRun != ReconnectRun ||
   !Marker->TryGetStringField(TEXT("profile"), Profile) || Profile != ReconnectProfile ||
   !Marker->TryGetNumberField(TEXT("seed_pid"), SeedPid) || !FMath::IsFinite(SeedPid) || SeedPid <= 0 || SeedPid > MAX_uint32 ||
   double(uint32(SeedPid)) != SeedPid || uint32(SeedPid) == FPlatformProcess::GetCurrentProcessId() ||
   !Marker->TryGetStringField(TEXT("account"), Account) || !FGuid::ParseExact(Account, EGuidFormats::DigitsWithHyphens, ParsedAccount) || ParsedAccount != ExpectedAccount ||
   !Marker->TryGetStringField(TEXT("character"), Character) || !FGuid::ParseExact(Character, EGuidFormats::DigitsWithHyphens, ParsedCharacter) || ParsedCharacter != ExpectedCharacter ||
   !Marker->TryGetStringField(TEXT("revision"), RevisionText) || !ReconnectInteger(RevisionText, ReconnectRevision) || ReconnectRevision <= 0 ||
   !Marker->TryGetStringField(TEXT("job"), Job) || !FGuid::ParseExact(Job, EGuidFormats::DigitsWithHyphens, ReconnectJob) || !ReconnectJob.IsValid() ||
   !Marker->TryGetStringField(TEXT("started"), StartedText) || !ReconnectInteger(StartedText, ReconnectStartedUtc) || ReconnectStartedUtc <= 0 ||
   !Marker->TryGetStringField(TEXT("due"), DueText) || !ReconnectInteger(DueText, ReconnectDueUtc) || ReconnectDueUtc <= ReconnectStartedUtc ||
   !Marker->TryGetStringField(TEXT("values"), Encoded) || Encoded.IsEmpty() || Encoded.Len() > ReconnectMaxBase64 ||
   !FBase64::Decode(Encoded, Expected) || Expected.IsEmpty() || Expected.Num() > ReconnectMaxValues || FBase64::Encode(Expected) != Encoded)
  { Finish(false, TEXT("Reconnect witness fields run identity PID UTC or bounded value encoding are invalid")); return; }
  ReconnectSeedPid = uint32(SeedPid); ReconnectValueBytes = Expected.Num();
  TArray<uint8> Actual;
  if (HealthCount != 4 || ManaCount != 0 || Jobs.Num() != 1 || Jobs[0].Id != ReconnectJob ||
   Jobs[0].StartedUtcTicks != ReconnectStartedUtc || Jobs[0].DueUtcTicks != ReconnectDueUtc ||
   Jobs[0].DueUtcTicks - Jobs[0].StartedUtcTicks != 2 * ETimespan::TicksPerSecond ||
   Jobs[0].RecipeId != TEXT("Prototype.Alchemy.HealthToMana.TimedUI") || Jobs[0].RecipeVersion != 1 || Jobs[0].Batches != 1 ||
   Data->GetCraftingRecipe(Jobs[0].RecipeId, Jobs[0].RecipeVersion) || Profiles->GetCommittedRevision(Player) != ReconnectRevision ||
   !CaptureReconnectValues(Player, Actual) || Actual != Expected || !Current() || Profiles->IsCheckpointPending(Player))
  { Finish(false, TEXT("Normal new-process restoration did not preserve exact committed inventory progression quests or frozen pending work")); return; }
  bReconnectRestored = true;
  AppendReconnectRow(TEXT("restore_observed"), HealthCount, ManaCount); Phase = 210; return;
 }
 if (Phase == 210)
 {
  if (HealthCount != 4 || ManaCount != 0 || Jobs.Num() != 1 || Jobs[0].Id != ReconnectJob ||
   Jobs[0].StartedUtcTicks != ReconnectStartedUtc || Jobs[0].DueUtcTicks != ReconnectDueUtc ||
   Profiles->GetCommittedRevision(Player) != ReconnectRevision)
  { Finish(false, TEXT("Restored owner values changed before its explicit collect phase")); return; }
  if (!KeepJobs->IsJobDue(ReconnectJob)) return; // Real UTC only; no deadline refresh or quality/time override.
  Phase = 211; bReconnectComplete = bReconnectConfirmed = bReconnectFirst = false;
  const TWeakObjectPtr<ThisClass> WeakThis(this); const TWeakObjectPtr<AProject_JPlayerState> WeakPlayer(Player);
  const FGuid Account = ReconnectAccount, Character = ReconnectCharacter;
  const bool Admitted = KeepJobs->TryCollectJob(ReconnectJob, [WeakThis, WeakPlayer, Account, Character](bool Confirmed, bool First)
  {
   auto* Self = WeakThis.Get(); auto* PS = WeakPlayer.Get();
   if (Self && !Self->bFinished && !Self->IsActorBeingDestroyed() && Self->Phase == 211 && IsValid(PS) &&
    !PS->IsActorBeingDestroyed() && Self->Player == PS && PS->GetAccountId().Value == Account && PS->GetCharacterId().Value == Character &&
    Self->GetWorld() && !Self->GetWorld()->bIsTearingDown && PS->GetWorld() == Self->GetWorld())
   { Self->bReconnectComplete = true; Self->bReconnectConfirmed = Confirmed; Self->bReconnectFirst = First; }
  });
  if (!Current() || !Admitted) { Finish(false, TEXT("The restored due job did not admit a new-session public collect")); }
  return;
 }
 if (Phase == 211)
 {
  if (!bReconnectComplete) return;
  if (!bReconnectConfirmed || !bReconnectFirst || HealthCount != 4 || ManaCount != 1 || !Jobs.IsEmpty() ||
   Profiles->GetCommittedRevision(Player) != ReconnectRevision + 1 || KeepJobs->FindJob(ReconnectJob))
  { Finish(false, TEXT("Restored job collection did not produce exactly one committed output")); return; }
  AppendReconnectRow(TEXT("collect_committed"), HealthCount, ManaCount); Phase = 212;
  bReconnectComplete = bReconnectConfirmed = bReconnectFirst = false;
  const bool Repeated = KeepJobs->TryCollectJob(ReconnectJob);
  if (!Current() || Repeated || Profiles->IsCheckpointPending(Player) || Profiles->GetCommittedRevision(Player) != ReconnectRevision + 1 ||
   !KeepJobs->CaptureJobs().IsEmpty())
  { Finish(false, TEXT("An already collected job admitted another output or revision")); return; }
  int32 ActualHealth = 0, ActualMana = 0;
  for (const auto& Item : Player->GetInventoryComponent()->GetItemInstances())
  { if (Item.ItemDef == Health) ActualHealth += Item.StackCount; else if (Item.ItemDef == Mana) ActualMana += Item.StackCount; else { Finish(false, TEXT("Repeat collect changed isolated item types")); return; } }
  if (ActualHealth != 4 || ActualMana != 1) { Finish(false, TEXT("Repeat collect changed committed quantities")); return; }
  AppendReconnectRow(TEXT("repeat_absent"), ActualHealth, ActualMana, Repeated); Phase = 213;
  Finish(true, TEXT("New process restored exact committed values and collected original UTC work once"));
 }
#endif
}
