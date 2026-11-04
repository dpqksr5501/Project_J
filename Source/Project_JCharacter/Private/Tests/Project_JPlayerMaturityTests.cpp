#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Misc/App.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/IConsoleManager.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "Animation/AnimNotifyQueue.h"
#include "Combat/Project_JCombatCommandSet.h"
#include "Combat/Project_JCombatConfiguration.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Animation/AnimMontage.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/Project_JMountedAnimationLayerComponent.h"
#include "Components/Project_JCombatAnimationLayerComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Project_JNPCCharacter.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "NiagaraFunctionLibrary.h"
#include "Misc/DataValidation.h"
#include <limits>

namespace
{
struct FTestWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FTestWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	~FTestWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
UProject_JAbilitySystemComponent* MakeASC(AActor* Owner)
{
	auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Owner);
	ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
	return ASC;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentGrantOwnershipTest, "ProjectJ.PlayerMaturity.Equipment.OriginalASC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentGrantOwnershipTest::RunTest(const FString&)
{
	FTestWorld W;
	auto* Rider = W.World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
	auto* OldOwner = W.World->SpawnActor<AActor>(); auto* NewOwner = W.World->SpawnActor<AActor>();
	auto* Old = MakeASC(OldOwner); auto* New = MakeASC(NewOwner);
	auto* OldStats = NewObject<UProject_JAttributeSet>(OldOwner); Old->AddAttributeSetSubobject(OldStats); OldStats->SetMaxHealth(100);
	auto* NewStats = NewObject<UProject_JAttributeSet>(NewOwner); New->AddAttributeSetSubobject(NewStats); NewStats->SetMaxHealth(200);
	Rider->CurrentASC = Old;
	auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Rider); Runtime->RegisterComponent();
	auto* Item = NewObject<UProject_JEquipmentItemDefinition>(); Item->EquipmentSlot = EProject_JEquipmentSlot::Head;
	Item->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly;
	auto& Modifier = Item->StatModifiers.AddDefaulted_GetRef(); Modifier.Stat = EProject_JEquipmentStat::MaxHealth; Modifier.Value = 50;
	Item->AbilitySet = NewObject<UProject_JAbilitySet>();
	Item->AbilitySet->GrantedAbilityEntries.AddDefaulted_GetRef().Ability = UProject_JPlayerMaturityMeleeFixture::StaticClass();
	FProject_JEquipmentRuntimeItem Ledger; Ledger.ItemDef = Item;
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, Ledger);
	TestEqual(TEXT("Equipment modifies original ASC"), OldStats->GetMaxHealth(), 150.0f);
	TestEqual(TEXT("Equipment grants an ability"), Old->GetActivatableAbilities().Num(), 1);
	Rider->CurrentASC = New;
	Runtime->RemoveEquipmentGameplay(Ledger);
	TestEqual(TEXT("Unequip after avatar swap restores original stat"), OldStats->GetMaxHealth(), 100.0f);
	TestEqual(TEXT("Replacement ASC is untouched"), NewStats->GetMaxHealth(), 200.0f);
	TestEqual(TEXT("Original grant is retired"), Old->GetActivatableAbilities().Num(), 0);
	Runtime->RemoveEquipmentGameplay(Ledger);
	TestEqual(TEXT("Repeated cleanup is idempotent"), OldStats->GetMaxHealth(), 100.0f);
	Rider->CurrentASC = Old;
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, Ledger);
	Rider->CurrentASC = nullptr;
	Runtime->RemoveEquipmentGameplay(Ledger);
	TestEqual(TEXT("Losing current ASC still retires old grants"), Old->GetActivatableAbilities().Num(), 0);
	TestEqual(TEXT("Losing current ASC still reverses fallback"), OldStats->GetMaxHealth(), 100.0f);
	Rider->CurrentASC = Old;
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, Ledger);
	auto* Buff = NewObject<UGameplayEffect>(); Buff->DurationPolicy = EGameplayEffectDurationType::Infinite;
	auto& GE = Buff->Modifiers.AddDefaulted_GetRef(); GE.Attribute = OldStats->GetMaxHealthAttribute();
	GE.ModifierOp = EGameplayModOp::Additive; GE.ModifierMagnitude = FScalableFloat(25);
	Ledger.GrantedEffectHandles.Add(Old->ApplyGameplayEffectToSelf(Buff, 1, Old->MakeEffectContext()));
	Rider->CurrentASC = New;
	Runtime->RemoveEquipmentGameplay(Ledger);
	TestEqual(TEXT("Active effects and fallback retire on original ASC together"), OldStats->GetMaxHealth(), 100.0f);
	TestEqual(TEXT("Original ASC retires every previous equipment effect"), Old->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);
	Item->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::GameplayEffectsThenStatModifiers;
	Item->EquipmentEffects.Add(UGameplayEffect::StaticClass());
	Rider->CurrentASC = Old; Runtime->ApplyEquipmentGameplay(*Rider, *Item, Ledger);
	TestEqual(TEXT("Invalid instant effect yields only one removable native stat fallback on its owning ASC"), Old->GetActiveEffects(FGameplayEffectQuery()).Num(), 1);
	TestEqual(TEXT("Invalid effect uses exactly one stat fallback"), OldStats->GetMaxHealth(), 150.0f);
	Runtime->RemoveEquipmentGameplay(Ledger);
	TestEqual(TEXT("Fallback remains reversible"), OldStats->GetMaxHealth(), 100.0f);
	TestEqual(TEXT("Cleanup retires the ASC-owned fallback effect"), Old->GetActiveEffects(FGameplayEffectQuery()).Num(), 0);

	// Source IDs are actual ASC leases. Two retained views of one manager share
	// effects, but neither a rename nor a new manager incarnation may alias them.
	const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepOld(Old), KeepNew(New);
	const TStrongObjectPtr<UProject_JEquipmentItemDefinition> KeepItem(Item);
	const TStrongObjectPtr<UProject_JEquipmentRuntimeComponent> KeepRuntime(Runtime);
	Item->AbilitySet = nullptr; Item->EquipmentEffects.Reset();
	Item->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly;
	auto* OtherRider = W.World->SpawnActor<AProject_JPlayerMaturityASCFixture>(); OtherRider->CurrentASC = Old;
	auto* OtherRuntime = NewObject<UProject_JEquipmentRuntimeComponent>(OtherRider); OtherRuntime->RegisterComponent();
	const TStrongObjectPtr<UProject_JEquipmentRuntimeComponent> KeepOtherRuntime(OtherRuntime);
	const auto NewManager = [&]()
	{
		auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(OldOwner);
		OldOwner->AddInstanceComponent(Manager); Manager->RegisterComponent();
		return Manager;
	};
	const auto SourceOf = [](const FProject_JEquipmentRuntimeItem& Row)
	{
		return Row.GrantedHandles.GrantSourceGenerations.Num() == 1
			? Row.GrantedHandles.GrantSourceGenerations.CreateConstIterator().Key() : NAME_None;
	};
	auto* FirstManager = NewManager(); auto* SecondManager = NewManager();
	Runtime->BindToEquipmentManager(FirstManager); OtherRuntime->BindToEquipmentManager(FirstManager);
	FProject_JEquipmentRuntimeItem FirstView, SecondView; FirstView.ItemDef = SecondView.ItemDef = Item;
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, FirstView);
	const FName FirstSource = SourceOf(FirstView);
	TestFalse(TEXT("Stat-only provider records one actual ASC source lease"), FirstSource.IsNone());
	TestTrue(TEXT("Manager can be renamed without changing its object incarnation"),
		FirstManager->Rename(TEXT("EquipmentGrantProviderRenamed"), nullptr, REN_DontCreateRedirectors | REN_NonTransactional));
	OtherRuntime->ApplyEquipmentGameplay(*OtherRider, *Item, SecondView);
	TestEqual(TEXT("Renamed same provider retains one source across retained avatars"), SourceOf(SecondView), FirstSource);
	TestEqual(TEXT("Retained same-provider avatars apply actual stats only once"), OldStats->GetMaxHealth(), 150.f);
	Runtime->RemoveEquipmentGameplay(FirstView);
	TestEqual(TEXT("One retained provider release keeps the other lease active"), OldStats->GetMaxHealth(), 150.f);
	FProject_JEquipmentRuntimeItem StaleSecond = SecondView;
	OtherRuntime->RemoveEquipmentGameplay(SecondView);
	TestEqual(TEXT("Final retained release retires the actual effect"), OldStats->GetMaxHealth(), 100.f);
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, FirstView);
	TestEqual(TEXT("Reacquisition by the same live manager keeps its source identity"), SourceOf(FirstView), FirstSource);
	OtherRuntime->RemoveEquipmentGameplay(StaleSecond);
	TestEqual(TEXT("Old generation cannot remove a later same-source acquisition"), OldStats->GetMaxHealth(), 150.f);
	OtherRuntime->BindToEquipmentManager(SecondManager);
	OtherRuntime->ApplyEquipmentGameplay(*OtherRider, *Item, SecondView);
	const FName SecondSource = SourceOf(SecondView);
	TestTrue(TEXT("Distinct managers keep distinct provider leases"), !SecondSource.IsNone() && SecondSource != FirstSource);
	TestTrue(TEXT("Distinct managers reuse the slot/definition comparison name"),
		SecondSource.GetComparisonIndex() == FirstSource.GetComparisonIndex() && SecondSource.GetNumber() != FirstSource.GetNumber());
	TestEqual(TEXT("Distinct providers each contribute their own actual stats"), OldStats->GetMaxHealth(), 200.f);
	Runtime->RemoveEquipmentGameplay(FirstView);
	TestEqual(TEXT("Retiring one distinct provider preserves the other effect"), OldStats->GetMaxHealth(), 150.f);
	OtherRuntime->RemoveEquipmentGameplay(SecondView);
	TestEqual(TEXT("Distinct providers retire independently"), OldStats->GetMaxHealth(), 100.f);

	// Null manager remains a supported shared source, including the original-ASC
	// test above. It is not replaced by an arbitrary per-Avatar identity.
	Runtime->BindToEquipmentManager(nullptr); OtherRuntime->BindToEquipmentManager(nullptr);
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, FirstView); OtherRuntime->ApplyEquipmentGameplay(*OtherRider, *Item, SecondView);
	TestTrue(TEXT("Null provider remains one shared number-zero source"), SourceOf(FirstView) == SourceOf(SecondView) && SourceOf(FirstView).GetNumber() == 0);
	TestEqual(TEXT("Null-provider views retain the established single effect"), OldStats->GetMaxHealth(), 150.f);
	Runtime->RemoveEquipmentGameplay(FirstView); OtherRuntime->RemoveEquipmentGameplay(SecondView);

	// Transient definitions can have only their final numeric suffix differ.
	// The manager number must not overwrite that part of the definition identity.
	const TStrongObjectPtr<UProject_JEquipmentItemDefinition> NumericA(NewObject<UProject_JEquipmentItemDefinition>(OldOwner, TEXT("ProviderItem_17")));
	const TStrongObjectPtr<UProject_JEquipmentItemDefinition> NumericB(NewObject<UProject_JEquipmentItemDefinition>(OldOwner, TEXT("ProviderItem_18")));
	for (auto* Definition : {NumericA.Get(), NumericB.Get()})
	{
		Definition->EquipmentSlot = Item->EquipmentSlot; Definition->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly;
		Definition->StatModifiers = Item->StatModifiers;
	}
	Runtime->BindToEquipmentManager(FirstManager); OtherRuntime->BindToEquipmentManager(FirstManager);
	FirstView.ItemDef = NumericA.Get(); SecondView.ItemDef = NumericB.Get();
	Runtime->ApplyEquipmentGameplay(*Rider, *NumericA, FirstView); OtherRuntime->ApplyEquipmentGameplay(*OtherRider, *NumericB, SecondView);
	TestTrue(TEXT("Numeric-suffixed definitions remain different comparison identities"),
		!SourceOf(FirstView).IsNone() && !SourceOf(SecondView).IsNone() && SourceOf(FirstView).GetComparisonIndex() != SourceOf(SecondView).GetComparisonIndex());
	TestEqual(TEXT("Different definitions retain their own effects under one manager"), OldStats->GetMaxHealth(), 200.f);
	Runtime->RemoveEquipmentGameplay(FirstView); OtherRuntime->RemoveEquipmentGameplay(SecondView);

	// Model the real unpublished local transaction: manager retirement closes the
	// bound runtime but cannot release a ledger not yet published in RuntimeItems.
	OtherRuntime->BindToEquipmentManager(nullptr); Runtime->BindToEquipmentManager(SecondManager);
	FirstView.ItemDef = SecondView.ItemDef = Item;
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, FirstView);
	const FName RetiredSource = SourceOf(FirstView);
	const TWeakObjectPtr<UProject_JEquipmentManagerComponent> RetiredManager(SecondManager);
	SecondManager->DestroyComponent(); SecondManager = nullptr;
	CollectGarbage(RF_NoFlags, true);
	TestFalse(TEXT("Normal provider destruction and GC retire its weak identity"), RetiredManager.IsValid());
	TestTrue(TEXT("Unpublished old transaction still owns its exact ASC source lease"), Old->HasAbilityGrantSource(RetiredSource));
	auto* ReplacementManager = NewManager(); Runtime->BindToEquipmentManager(ReplacementManager);
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, SecondView);
	const FName ReplacementSource = SourceOf(SecondView);
	TestTrue(TEXT("New provider incarnation never joins the still-live old record"),
		!ReplacementSource.IsNone() && ReplacementSource != RetiredSource && ReplacementSource.GetComparisonIndex() == RetiredSource.GetComparisonIndex());
	TestEqual(TEXT("Replacement remains an independent contribution while old transaction survives"), OldStats->GetMaxHealth(), 200.f);
	Runtime->RemoveEquipmentGameplay(FirstView);
	TestEqual(TEXT("Late old-ledger release preserves the replacement contribution"), OldStats->GetMaxHealth(), 150.f);
	Runtime->RemoveEquipmentGameplay(SecondView);
	TestEqual(TEXT("All provider effects retire normally"), OldStats->GetMaxHealth(), 100.f);
	TestEqual(TEXT("Identity lifecycle never modifies the other ASC"), NewStats->GetMaxHealth(), 200.f);
	Runtime->BindToEquipmentManager(nullptr); OtherRuntime->BindToEquipmentManager(nullptr);
	FirstManager->DestroyComponent(); ReplacementManager->DestroyComponent(); OtherRuntime->DestroyComponent();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMountedTagOwnershipTest, "ProjectJ.PlayerMaturity.Mount.OriginalASC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMountedTagOwnershipTest::RunTest(const FString&)
{
	FTestWorld W;
	auto* Rider = W.World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
	auto* C = NewObject<UProject_JMountComponent>(Rider); C->RegisterComponent();
	auto* Old = MakeASC(Rider); auto* New = MakeASC(Rider);
	auto* Mount = W.World->SpawnActor<AProject_JMountLifecycleFixture>();
	if (!TestNotNull(TEXT("Concrete mount fixture spawns"), Mount)) { return false; }
	const FGameplayTag Tag = FProject_JGameplayTags::Get().State_Mounted;
	Old->AddProjectJLooseGameplayTag(Tag, true); New->AddProjectJLooseGameplayTag(Tag, true);
	Rider->CurrentASC = Old;
	C->SetMountedMount(Mount);
	TestEqual(TEXT("Session adds exactly one owned tag"), Old->GetTagCount(Tag), 2);
	Rider->CurrentASC = New;
	C->SetMountedMount(nullptr);
	TestEqual(TEXT("Dismount retires the original session contribution"), Old->GetTagCount(Tag), 1);
	TestEqual(TEXT("Replacement and external tag remain"), New->GetTagCount(Tag), 1);
	C->SetMountedMount(nullptr);
	TestEqual(TEXT("Repeated clear cannot consume external tag"), Old->GetTagCount(Tag), 1);
	Rider->CurrentASC = Old; C->SetMountedMount(Mount); Rider->CurrentASC = nullptr;
	C->DestroyComponent();
	TestEqual(TEXT("Destroying a pre-BeginPlay component releases its tag"), Old->GetTagCount(Tag), 1);
	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJComboWindowOwnershipTest, "ProjectJ.PlayerMaturity.Combat.ComboWindowOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJComboWindowOwnershipTest::RunTest(const FString&)
{
	FTestWorld W;
	auto* Rider = W.World->SpawnActor<AProject_JPlayerMaturityASCFixture>();
	auto* ASC = MakeASC(Rider); Rider->CurrentASC = ASC;
	// The ability's weapon identity is captured from an actual equipped slot.
	auto* Item = NewObject<UProject_JEquipmentItemDefinition>(); Item->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
	// Use a transient base character's equipment lifecycle for its public revision.
	auto* Player = W.World->SpawnActor<AProject_JSpellbladeCharacter>();
	auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Player);
	Manager->EquipItem(Item); Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>()->BindToEquipmentManager(Manager);
	auto* Ability = NewObject<UProject_JPlayerMaturityMeleeFixture>(); Ability->SetTestActive(true);
	Ability->AttackEquipment = Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>(); Ability->AttackWeaponRevision = Ability->AttackEquipment->GetWeaponRevision();
	Ability->ActiveAttackDefinition = NewObject<UProject_JAttackDefinition>();
	const uint64 A = Ability->BeginComboWindow(), B = Ability->BeginComboWindow();
	TestTrue(TEXT("Two overlapping windows have distinct owned tokens"), A && B && A != B);
	Ability->EndComboWindow(A);
	TestTrue(TEXT("A End preserves B"), Ability->IsComboWindowCurrent(B) && Ability->bIsComboWindowOpen);
	Ability->ResetComboWindows();
	const uint64 Next = Ability->BeginComboWindow();
	TestFalse(TEXT("Old End does not apply to next node"), Ability->EndComboWindow(B));
	TestTrue(TEXT("Next node remains open"), Ability->IsComboWindowCurrent(Next));
	Ability->EndComboWindow(Next);
	TestFalse(TEXT("Last owned End closes window"), Ability->bIsComboWindowOpen);
	Ability->SetTestActive(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCueLeaseOwnershipTest, "ProjectJ.PlayerMaturity.Combat.CueLeaseOwnership",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCueLeaseOwnershipTest::RunTest(const FString&)
{
	FTestWorld W;
	auto* NPC = W.World->SpawnActor<AProject_JNPCCharacter>();
	auto* C = NewObject<UProject_JCombatPresentationComponent>(NPC); C->RegisterComponent();
	const auto Tag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
	const uint64 A = ++C->NextCueLeaseToken, B = ++C->NextCueLeaseToken;
	C->CueLeases.Add(A, Tag); C->CueLeases.Add(B, Tag);
	C->ReplicatedPresentationState.ActiveLoopingCueTags.AddTag(Tag);
	C->EndCueLease(A);
	TestTrue(TEXT("Ending A preserves overlapping B's replicated loop"), C->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag));
	C->EndCueLease(B);
	TestFalse(TEXT("Last lease retires loop recovery state"), C->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag));
	C->CueLeases.Add(++C->NextCueLeaseToken, Tag);
	const uint64 Old = C->NextCueLeaseToken;
	C->BeginAttackPresentation(Tag);
	const uint64 Next = ++C->NextCueLeaseToken; C->CueLeases.Add(Next, Tag);
	C->ReplicatedPresentationState.ActiveLoopingCueTags.AddTag(Tag);
	C->EndCueLease(Old);
	TestTrue(TEXT("Old attack End cannot stop new same-tag attack"), C->IsCueLeaseCurrent(Next) && C->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag));
	C->StartedCueTags.AddTag(Tag); C->StopCueLocal(Tag);
	TestTrue(TEXT("One-shot dedup survives Stop"), C->StartedCueTags.HasTagExact(Tag));
	C->EndAttackPresentation();

	// The same authority metadata/notify contract applies with and without a renderer.
	// Payload-admission assertions belong to the actual process capability; never
	// spoof CanEverRender or the actor's network role to make a request admissible.
	auto* Assets = W.World->GetSubsystem<UProject_JVisualAssetSubsystem>();
	if (!TestNotNull(TEXT("Cue ownership world has the real visual loader"), Assets)) { return false; }
	const uint64 AcceptedBefore = Assets->GetStats().Accepted;
	auto* Set = NewObject<UProject_JCombatPresentationSet>(C);
	auto* Profile = NewObject<UProject_JAttackPresentationProfile>(Set);
	auto& Entry = Set->AttackPresentations.AddDefaulted_GetRef(); Entry.AttackTag = Tag; Entry.Profile = Profile;
	auto& Cue = Profile->Cues.AddDefaulted_GetRef(); Cue.CueTag = Tag; Cue.bLooping = true;
	const FSoftObjectPath PayloadPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Cue.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(PayloadPath);
	C->BasePresentationSet = Set;
	C->RefreshPresentation(); C->BeginAttackPresentation(Tag);
	const uint64 MetadataLease = C->BeginCueLease(Tag);
	TestTrue(TEXT("Real notify begin keeps ownership without requiring payload residency"), MetadataLease != 0 && C->IsCueLeaseCurrent(MetadataLease));
	TestTrue(TEXT("Authority publishes the requested looping cue identity"), C->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag));
	const int32 RecoveryRevision = C->ReplicatedPresentationState.Revision;
	C->ApplyReplicatedState();
	TestEqual(TEXT("Recovery accepts the same metadata with or without local visual work"), C->LastAppliedPresentationRevision, RecoveryRevision);
	if (!FApp::CanEverRender())
	{
		TestFalse(TEXT("Headless direct cue demand is denied before shared admission"), C->EnsureVisualAsset(PayloadPath, true));
		TestEqual(TEXT("Headless preload and cue do not acquire payload leases"), C->VisualAssetLeases.Num(), 0);
		TestEqual(TEXT("Headless recovery does not retain a local pending loop"), C->PendingLoopingCues.Num(), 0);
		TestFalse(TEXT("Headless cue does not mark an unrendered local start"), C->StartedCueTags.HasTagExact(Tag));
		auto* Budget = W.World->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
		if (!TestNotNull(TEXT("Headless metadata world retains the existing apply owner"), Budget)) { return false; }
		C->QueueReadyLoopApplications(); C->ApplyBudgetedReadyLoop(C->VisualAssetRevision);
		TestEqual(TEXT("Headless direct and recovery demand never admit deferred visual applies"), Budget->GetStats().Pending, 0);
		TestEqual(TEXT("Headless ready-loop consumer performs no physical apply"), Budget->GetStats().Applied, uint64(0));
	}
	TestFalse(TEXT("Current metadata-only lease has no admitted component"), C->GetCueLeaseComponent(MetadataLease).IsValid());
	TestFalse(TEXT("Zero lease has no component"), C->GetCueLeaseComponent(0).IsValid());
	C->EndCueLease(MetadataLease);
	TestFalse(TEXT("Retired metadata lease has no component"), C->GetCueLeaseComponent(MetadataLease).IsValid());
	TestFalse(TEXT("Real notify end removes the authority loop identity"), C->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag));
	TestFalse(TEXT("Real notify end retires its ownership token"), C->IsCueLeaseCurrent(MetadataLease));
	C->RefreshPresentation(); C->EndAttackPresentation();
	if (!FApp::CanEverRender())
	{
		// An already resident system must not bypass the local-consumption guard.
		Cue.NiagaraSystem = NewObject<UNiagaraSystem>(Profile);
		C->BeginAttackPresentation(Tag);
		const uint64 ResidentLease = C->BeginCueLease(Tag);
		TestTrue(TEXT("Resident cosmetic payload still preserves notify ownership"), ResidentLease != 0);
		TestTrue(TEXT("Resident cosmetic payload still publishes remote loop recovery"), C->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag));
		TestEqual(TEXT("Resident payload does not admit local headless work"), C->VisualAssetLeases.Num(), 0);
		TestEqual(TEXT("Resident payload creates no local pending loop"), C->PendingLoopingCues.Num(), 0);
		TestEqual(TEXT("Resident payload creates no local active loop"), C->ActiveLoopingCues.Num(), 0);
		TestFalse(TEXT("Resident headless payload is not marked locally started"), C->StartedCueTags.HasTagExact(Tag));
		C->EndCueLease(ResidentLease);
		TestFalse(TEXT("Resident notify end removes recovery identity"), C->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag));
		const uint32 OldVisualRevision = C->VisualAssetRevision;
		C->RefreshPresentation();
		C->OnVisualAssetReady(PayloadPath, OldVisualRevision, nullptr);
		TestEqual(TEXT("Stale completion cannot re-admit headless payload work"), C->VisualAssetLeases.Num(), 0);
		TestEqual(TEXT("Headless consumer issues no new shared requests"), Assets->GetStats().Accepted, AcceptedBefore);
	}
	C->EndAttackPresentation(); C->DestroyComponent();
	TestEqual(TEXT("Retired cue releases all of its payload leases"), C->VisualAssetLeases.Num(), 0);
	TestEqual(TEXT("Notify begin is rejected after component retirement"), C->BeginCueLease(Tag), uint64(0));
	TestFalse(TEXT("Retired presentation exposes no earlier lease component"), C->GetCueLeaseComponent(MetadataLease).IsValid());

	// The shared notify is exercised through its real component/lease consumer.
	// Only the three reentry cases below require an actual rendering process.
	FTestWorld Cue126World;
	TStrongObjectPtr<UWorld> Cue126WorldPin(Cue126World.World);
	ON_SCOPE_EXIT { if (Cue126World.World->GetBegunPlay()) { Cue126World.World->EndPlay(EEndPlayReason::Quit); } };
	Cue126World.World->InitializeActorsForPlay(FURL());
	Cue126World.World->GetWorldSettings()->NotifyBeginPlay();
	FActorSpawnParameters Cue126Spawn;
	Cue126Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	TStrongObjectPtr<AProject_JNPCCharacter> Cue126Owner(Cue126World.World->SpawnActor<AProject_JNPCCharacter>(
		AProject_JNPCCharacter::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Cue126Spawn));
	if (!TestNotNull(TEXT("Shared cue notify owner"), Cue126Owner.Get())) { return false; }
	TStrongObjectPtr<UProject_JCombatPresentationComponent> Cue126Presentation(NewObject<UProject_JCombatPresentationComponent>(Cue126Owner.Get()));
	Cue126Owner->AddInstanceComponent(Cue126Presentation.Get()); Cue126Presentation->RegisterComponent();
	TStrongObjectPtr<UProject_JAnimNotifyState_CombatPresentationCue> Cue126Notify(NewObject<UProject_JAnimNotifyState_CombatPresentationCue>());
	Cue126Notify->CueTag = Tag;
	FAnimNotifyEventReference Cue126Event; Cue126Event.SetNotifyInstanceID(126);
	const FSoftObjectPath Cue126SystemPath(TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
	TStrongObjectPtr<UNiagaraSystem> Cue126System;
	if (FApp::CanEverRender())
	{
		Cue126System.Reset(LoadObject<UNiagaraSystem>(nullptr, *Cue126SystemPath.ToString()));
		if (!TestNotNull(TEXT("Real authored Niagara for native activation reentry"), Cue126System.Get())) { return false; }
	}
	auto* Cue126Set = NewObject<UProject_JCombatPresentationSet>(Cue126Presentation.Get());
	auto* Cue126Profile = NewObject<UProject_JAttackPresentationProfile>(Cue126Set);
	auto& Cue126Entry = Cue126Set->AttackPresentations.AddDefaulted_GetRef(); Cue126Entry.AttackTag = Tag; Cue126Entry.Profile = Cue126Profile;
	auto& Cue126Definition = Cue126Profile->Cues.AddDefaulted_GetRef();
	Cue126Definition.CueTag = Tag; Cue126Definition.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(Cue126SystemPath);
	Cue126Definition.AttachmentTarget = EProject_JCombatVFXAttachmentTarget::World;
	Cue126Definition.bLooping = true; Cue126Definition.bDestroyImmediatelyOnStop = true; Cue126Definition.bUseRemoteSpawnBudget = false;
	Cue126Presentation->BasePresentationSet = Cue126Set; Cue126Presentation->RefreshPresentation();
	Cue126Presentation->BeginAttackPresentation(Tag);

	// Real owned auxiliary components avoid destroying a character's root mesh.
	// No subsequent Begin is used to clean the dead half of the shared map.
	TArray<TWeakObjectPtr<USkeletalMeshComponent>> Cue126Meshes;
	TArray<uint64> Cue126Tokens;
	for (int32 Cue126Index = 0; Cue126Index < 32; ++Cue126Index)
	{
		auto* Cue126Mesh = NewObject<USkeletalMeshComponent>(Cue126Owner.Get());
		Cue126Owner->AddInstanceComponent(Cue126Mesh); Cue126Mesh->RegisterComponent(); Cue126Meshes.Add(Cue126Mesh);
		Cue126Notify->NotifyBegin(Cue126Mesh, nullptr, 1.0f, Cue126Event);
		Cue126Tokens.Add(Cue126Presentation->NextCueLeaseToken);
	}
	TestEqual(TEXT("Shared notify retains one mesh entry for each of 32 actual begins"), Cue126Notify->Leases.Num(), 32);
	TestEqual(TEXT("The actual presentation owns all 32 returned tokens"), Cue126Presentation->CueLeases.Num(), 32);
	for (int32 Cue126Half = 0; Cue126Half < 2; ++Cue126Half)
	{
		for (int32 Cue126DeadIndex = Cue126Half * 16; Cue126DeadIndex < (Cue126Half + 1) * 16; ++Cue126DeadIndex)
		{
			if (auto* Cue126DeadMesh = Cue126Meshes[Cue126DeadIndex].Get())
			{
				Cue126Owner->RemoveInstanceComponent(Cue126DeadMesh); Cue126DeadMesh->DestroyComponent();
			}
		}
		CollectGarbage(RF_NoFlags, true);
		const int32 Cue126ExpectedLive = Cue126Half == 0 ? 16 : 0;
		TestTrue(TEXT("GC leaves the owning actor and presentation alive"), IsValid(Cue126Owner.Get()) && IsValid(Cue126Presentation.Get()));
		TestEqual(TEXT("PostGC prunes only the dead mesh keys without another Begin"), Cue126Notify->Leases.Num(), Cue126ExpectedLive);
		TestEqual(TEXT("PostGC ends dead mesh tokens on the still-live presentation"), Cue126Presentation->CueLeases.Num(), Cue126ExpectedLive);
		for (int32 Cue126CheckIndex = 0; Cue126CheckIndex < 32; ++Cue126CheckIndex)
		{
			const bool Cue126ShouldLive = Cue126CheckIndex >= (Cue126Half + 1) * 16;
			TestEqual(TEXT("GC preserves exactly the surviving mesh tokens"), Cue126Presentation->IsCueLeaseCurrent(Cue126Tokens[Cue126CheckIndex]), Cue126ShouldLive);
			if (!Cue126ShouldLive) { TestNull(TEXT("Dead auxiliary mesh storage is fully collected"), Cue126Meshes[Cue126CheckIndex].GetEvenIfUnreachable()); }
		}
		TestEqual(TEXT("Shared loop recovery lasts exactly until the final live mesh lease ends"),
			Cue126Presentation->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(Tag), Cue126ExpectedLive != 0);
	}
	TestEqual(TEXT("Final mesh GC also retires the real local loop"), Cue126Presentation->ActiveLoopingCues.Num(), 0);
	TestEqual(TEXT("Final mesh GC leaves no desired loop continuation"), Cue126Presentation->PendingLoopingCues.Num(), 0);

	if (FApp::CanEverRender())
	{
		auto* Cue126ProjectPool = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.Pool"));
		auto* Cue126EnginePool = IConsoleManager::Get().FindConsoleVariable(TEXT("FX.NiagaraComponentPool.Enable"));
		if (!TestNotNull(TEXT("Existing presentation pool switch"), Cue126ProjectPool) ||
			!TestNotNull(TEXT("Existing native Niagara pool switch"), Cue126EnginePool)) { return false; }
		const int32 Cue126PreviousProjectPool = Cue126ProjectPool->GetInt(), Cue126PreviousEnginePool = Cue126EnginePool->GetInt();
		Cue126ProjectPool->Set(1, ECVF_SetByCode); Cue126EnginePool->Set(1, ECVF_SetByCode);
		ON_SCOPE_EXIT { Cue126ProjectPool->Set(Cue126PreviousProjectPool, ECVF_SetByCode); Cue126EnginePool->Set(Cue126PreviousEnginePool, ECVF_SetByCode); };
		TStrongObjectPtr<AProject_JPlayerMaturityASCFixture> Cue126Receiver(Cue126World.World->SpawnActor<AProject_JPlayerMaturityASCFixture>(
			AProject_JPlayerMaturityASCFixture::StaticClass(), FVector(1000, 0, 0), FRotator::ZeroRotator, Cue126Spawn));
		if (!TestNotNull(TEXT("Native activation callback receiver"), Cue126Receiver.Get())) { return false; }
		TStrongObjectPtr<USkeletalMeshComponent> Cue126ReentryMesh(NewObject<USkeletalMeshComponent>(Cue126Owner.Get()));
		Cue126Owner->AddInstanceComponent(Cue126ReentryMesh.Get()); Cue126ReentryMesh->RegisterComponent();
		TStrongObjectPtr<UNiagaraComponent> Cue126Pooled;
		int32 Cue126Activations = 0;
		const auto Cue126ArmActivation = [&](TFunction<void()> Cue126Action) -> bool
		{
			Cue126Presentation->BeginAttackPresentation(Tag);
			Cue126Pooled.Reset(UNiagaraFunctionLibrary::SpawnSystemAtLocation(Cue126World.World, Cue126System.Get(),
				FVector::ZeroVector, FRotator::ZeroRotator, FVector::OneVector, false, false, ENCPoolMethod::ManualRelease, false));
			if (!TestNotNull(TEXT("Public Niagara spawn primes the real manual-release pool"), Cue126Pooled.Get())) { return false; }
			Cue126Pooled->OnComponentActivated.AddUniqueDynamic(Cue126Receiver.Get(), &AProject_JPlayerMaturityASCFixture::HandleCueComponentActivated);
			Cue126Pooled->ReleaseToPool();
			if (!TestTrue(TEXT("Inactive prime returns through the native engine pool"), Cue126Pooled->PoolingMethod == ENCPoolMethod::FreeInPool)) { return false; }
			Cue126Receiver->OnCueComponentActivated = [&, Cue126Work = MoveTemp(Cue126Action)](UActorComponent* Cue126Activated, bool Cue126Reset) mutable
			{
				++Cue126Activations;
				TestTrue(TEXT("Production Activate(true) reuses the bound pooled component"), Cue126Activated == Cue126Pooled.Get() && Cue126Reset);
				Cue126Work();
			};
			return true;
		};

		const uint64 Cue126BeforeDuplicate = Cue126Presentation->NextCueLeaseToken;
		if (!Cue126ArmActivation([&]()
		{
			const uint64 PendingLease142 = Cue126Presentation->NextCueLeaseToken;
			TestTrue(TEXT("Activation callback observes a current lease before loop publication"), Cue126Presentation->IsCueLeaseCurrent(PendingLease142));
			TestFalse(TEXT("Current lease has no component until actual admission returns"), Cue126Presentation->GetCueLeaseComponent(PendingLease142).IsValid());
			Cue126Notify->NotifyBegin(Cue126ReentryMesh.Get(), nullptr, 1.0f, Cue126Event);
		})) { return false; }
		Cue126Notify->NotifyBegin(Cue126ReentryMesh.Get(), nullptr, 1.0f, Cue126Event);
		TestEqual(TEXT("Duplicate Begin is injected by one real activation callback"), Cue126Activations, 1);
		TestEqual(TEXT("Same-ID nested Begin cannot acquire a second presentation token"), Cue126Presentation->NextCueLeaseToken, Cue126BeforeDuplicate + 1);
		TestEqual(TEXT("Same-ID nested Begin commits one shared-notify entry"), Cue126Notify->Leases.Num(), 1);
		TestEqual(TEXT("Same-ID nested Begin leaves one live presentation lease"), Cue126Presentation->CueLeases.Num(), 1);
		const uint64 AdmittedLease142 = Cue126Presentation->NextCueLeaseToken;
		const TWeakObjectPtr<UNiagaraComponent> AdmittedComponent142 = Cue126Presentation->GetCueLeaseComponent(AdmittedLease142);
		TestTrue(TEXT("Current lease resolves the actual admitted pooled component"), AdmittedComponent142.IsValid() && AdmittedComponent142.Get() == Cue126Pooled.Get());
		Cue126Notify->NotifyEnd(Cue126ReentryMesh.Get(), nullptr, Cue126Event);
		TestEqual(TEXT("Duplicate Begin's single matching End retires the token"), Cue126Presentation->CueLeases.Num(), 0);
		TestFalse(TEXT("Ended lease cannot resolve its still-allocated pooled component"), Cue126Presentation->GetCueLeaseComponent(AdmittedLease142).IsValid());

		if (!Cue126ArmActivation([&]() { Cue126Notify->NotifyEnd(Cue126ReentryMesh.Get(), nullptr, Cue126Event); })) { return false; }
		Cue126Notify->NotifyBegin(Cue126ReentryMesh.Get(), nullptr, 1.0f, Cue126Event);
		TestEqual(TEXT("End-before-return executes in the real second activation"), Cue126Activations, 2);
		TestEqual(TEXT("End-before-return cannot resurrect a notify entry"), Cue126Notify->Leases.Num(), 0);
		TestEqual(TEXT("A token returned after its End is immediately retired"), Cue126Presentation->CueLeases.Num(), 0);
		TestEqual(TEXT("End-before-return also stops the spawned looping component"), Cue126Presentation->ActiveLoopingCues.Num(), 0);
		TestEqual(TEXT("End-before-return removes the pending loop"), Cue126Presentation->PendingLoopingCues.Num(), 0);

		const uint64 Cue126BeforeRestart = Cue126Presentation->NextCueLeaseToken;
		if (!Cue126ArmActivation([&]()
		{
			Cue126Notify->NotifyEnd(Cue126ReentryMesh.Get(), nullptr, Cue126Event);
			Cue126Presentation->BeginAttackPresentation(Tag);
			Cue126Notify->NotifyBegin(Cue126ReentryMesh.Get(), nullptr, 1.0f, Cue126Event);
			CollectGarbage(RF_NoFlags, true);
		})) { return false; }
		Cue126Notify->NotifyBegin(Cue126ReentryMesh.Get(), nullptr, 1.0f, Cue126Event);
		TestEqual(TEXT("End/restart/GC executes in the real third activation"), Cue126Activations, 3);
		TestEqual(TEXT("Restart acquires exactly one token for each actual begin"), Cue126Presentation->NextCueLeaseToken, Cue126BeforeRestart + 2);
		TestFalse(TEXT("Old attack token cannot survive the callback replacement"), Cue126Presentation->IsCueLeaseCurrent(Cue126BeforeRestart + 1));
		TestTrue(TEXT("Replacement token remains current after the old Begin returns"), Cue126Presentation->IsCueLeaseCurrent(Cue126BeforeRestart + 2));
		TestEqual(TEXT("Old completion cannot replace the same-ID new notify claim"), Cue126Notify->Leases.Num(), 1);
		TestEqual(TEXT("Only the replacement presentation token survives GC"), Cue126Presentation->CueLeases.Num(), 1);
		TestEqual(TEXT("Replacement attack retains exactly one real looping cue"), Cue126Presentation->ActiveLoopingCues.Num(), 1);
		TestFalse(TEXT("Old attack lease cannot resolve the replacement loop"), Cue126Presentation->GetCueLeaseComponent(Cue126BeforeRestart + 1).IsValid());
		const TWeakObjectPtr<UNiagaraComponent> ReplacementComponent142 = Cue126Presentation->GetCueLeaseComponent(Cue126BeforeRestart + 2);
		TestTrue(TEXT("Replacement lease resolves the one admitted loop"), ReplacementComponent142.IsValid() && ReplacementComponent142.Get() == Cue126Presentation->ActiveLoopingCues.FindChecked(Tag).Get());
		TestFalse(TEXT("Older ended lease stays null after later pool reuse"), Cue126Presentation->GetCueLeaseComponent(AdmittedLease142).IsValid());
		Cue126Notify->NotifyEnd(Cue126ReentryMesh.Get(), nullptr, Cue126Event);
		TestEqual(TEXT("Replacement's own End consumes the replacement claim"), Cue126Notify->Leases.Num(), 0);
		TestEqual(TEXT("Replacement's own End retires its token"), Cue126Presentation->CueLeases.Num(), 0);
		TestEqual(TEXT("Replacement's own End stops the replacement loop"), Cue126Presentation->ActiveLoopingCues.Num(), 0);
		TestFalse(TEXT("Replacement's own End retires component observation"), Cue126Presentation->GetCueLeaseComponent(Cue126BeforeRestart + 2).IsValid());
		Cue126Receiver->OnCueComponentActivated = nullptr;
	}
	else { AddInfo(TEXT("Native Niagara activation reentry requires an RHI run; the actual full-GC lease checks above still ran.")); }
	Cue126Presentation->EndAttackPresentation();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLayerLoadLifecycleTest, "ProjectJ.PlayerMaturity.Animation.LayerLoadLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLayerLoadLifecycleTest::RunTest(const FString&)
{
	const FSoftObjectPath Path(TEXT("/Game/Tests/Missing.Missing_C"));
	auto* Mounted = NewObject<UProject_JMountedAnimationLayerComponent>();
	Mounted->Layer.PreloadedPath = Path; Mounted->Layer.Generation = 10;
	Mounted->HandleLayerPreloadCompleted(Path, 9);
	TestFalse(TEXT("Stale mounted callback cannot poison new request"), Mounted->Layer.bFailed);
	AddExpectedError(TEXT("Layer load failed; default pose retained"), EAutomationExpectedErrorFlags::Contains, 2);
	Mounted->HandleLayerPreloadCompleted(Path, 10);
	TestTrue(TEXT("Missing mounted layer records terminal failure"), Mounted->Layer.bFailed);
	Mounted->ResetPreload();
	TestFalse(TEXT("Explicit reset creates retryable generation"), Mounted->Layer.bFailed);
	TestTrue(TEXT("Reset invalidates old same-path callback"), Mounted->Layer.Generation > 10);
	auto* Combat = NewObject<UProject_JCombatAnimationLayerComponent>();
	Combat->Layer.PreloadedPath = Path; Combat->Layer.Generation = 20;
	Combat->HandleLayerPreloadCompleted(Path, 20);
	TestTrue(TEXT("Missing combat layer has the same failure contract"), Combat->Layer.bFailed);
	Combat->bEndingPlay = true; Combat->Layer.bFailed = false;
	Combat->HandleLayerPreloadCompleted(Path, 20);
	TestFalse(TEXT("EndPlay ignores in-flight callback"), Combat->Layer.bFailed);
	auto* Early = NewObject<UProject_JCombatAnimationLayerComponent>();
	Early->Layer.PreloadedPath = Path; Early->Layer.Generation = 30;
	Early->DestroyComponent();
	Early->HandleLayerPreloadCompleted(Path, 30);
	TestFalse(TEXT("Destruction before BeginPlay rejects the old completion"), Early->Layer.bFailed);
	TestTrue(TEXT("Destruction releases the component's preload ownership"), Early->Layer.PreloadedPath.IsNull());
	FProject_JAnimationLayerLease InvalidResident;
	InvalidResident.PreparePreload(FSoftObjectPath(UObject::StaticClass()));
	TestFalse(TEXT("Resident non-animation class is rejected at completion"), InvalidResident.CompletePreload());
	TestTrue(TEXT("Invalid class failure prevents normal refresh retry"), InvalidResident.HasPreloadFor(FSoftObjectPath(UObject::StaticClass())));
	TestFalse(TEXT("Other layer owner keeps its independent request state"), Combat->Layer.bFailed);
	return true;
}



IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerVisibilityTest, "ProjectJ.PlayerMaturity.Animation.SharedVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerVisibilityTest::RunTest(const FString&)
{
	FTestWorld W;
	auto* Player = W.World->SpawnActor<AProject_JSpellbladeCharacter>();
	auto* Leader = Player->GetMesh(); Leader->SetHiddenInGame(true); Leader->SetLastRenderTime(-100);
	auto* Follower = NewObject<USkeletalMeshComponent>(Player); Follower->SetupAttachment(Leader); Follower->RegisterComponent();
	Follower->SetHiddenInGame(false); Follower->SetLastRenderTime(W.World->GetTimeSeconds());
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Leader); Leader->AnimScriptInstance = Anim; Anim->InitializeAnimation();
	auto* Locomotion = Player->GetLocomotionAnimStateComponent(); Locomotion->RefreshCachedReferences();
	TestTrue(TEXT("Follower visibility wakes AnimInstance"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	TestTrue(TEXT("Follower visibility wakes locomotion semantics"), Locomotion->WasRecentlyRendered(0.25f));
	Follower->SetLastRenderTime(-100);
	TestFalse(TEXT("No visible renderer suspends semantic presentation"), Locomotion->WasRecentlyRendered(0.25f));
	Leader->SetLastRenderTime(W.World->GetTimeSeconds());
	TestFalse(TEXT("Hidden leader shadow timestamp cannot override visual demand"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	auto* DynamicVisual = NewObject<USkeletalMeshComponent>(Player);
	DynamicVisual->SetLastRenderTime(W.World->GetTimeSeconds());
	TestFalse(TEXT("Unregistered recent skeletal component is not demand"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	DynamicVisual->RegisterComponent(); DynamicVisual->SetLastRenderTime(W.World->GetTimeSeconds());
	TestTrue(TEXT("Registration is visible immediately within the same frame"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	DynamicVisual->UnregisterComponent();
	TestFalse(TEXT("Unregistration retires demand immediately within the same frame"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	auto* StaticVisual = NewObject<UStaticMeshComponent>(Player); StaticVisual->RegisterComponent();
	StaticVisual->SetLastRenderTime(W.World->GetTimeSeconds());
	TestFalse(TEXT("Recently rendered non-skeletal component preserves the existing criterion"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	Leader->SetHiddenInGame(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCommandAuthoringBoundaryTest, "ProjectJ.PlayerMaturity.Authoring.CommandBounds",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCommandAuthoringBoundaryTest::RunTest(const FString&)
{
	auto* Set = NewObject<UProject_JCombatCommandSet>();
	auto& Command = Set->Commands.AddDefaulted_GetRef();
	const auto Input = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
	Command.CommandTag = Input; Command.ResultInputTag = Input;
	TArray<FProject_JCombatCommandInputEntry> History;
	for (int32 i=0;i<16;++i) { Command.OrderedInputSequence.Add(Input); History.Add({Input, i*0.01}); }
	TestNotNull(TEXT("Largest supported sequence matches"), Set->FindBestMatch(History, {}));
	Command.OrderedInputSequence.Add(Input); History.Add({Input, 0.16});
	TestNull(TEXT("17-input command cannot bypass runtime bound"), Set->FindBestMatch(History, {}));
#if WITH_EDITOR
	FDataValidationContext Context;
	TestTrue(TEXT("Validator rejects unexecutable command"), Set->IsDataValid(Context) == EDataValidationResult::Invalid);
#endif
	Command.OrderedInputSequence.SetNum(1);
	Command.MaxTimeBetweenInputs = std::numeric_limits<float>::quiet_NaN();
	TestNull(TEXT("Nonfinite interval cannot match"), Set->FindBestMatch(History, {}));
	Command.MaxTimeBetweenInputs = 0.5f; History.Last().TimestampSeconds = std::numeric_limits<double>::quiet_NaN();
	TestNull(TEXT("Nonfinite timestamp cannot match"), Set->FindBestMatch(History, {}));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEffectiveInputCompositionTest, "ProjectJ.PlayerMaturity.Authoring.EffectiveInputComposition",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEffectiveInputCompositionTest::RunTest(const FString&)
{
	FTestWorld W; auto* Owner = W.World->SpawnActor<AActor>(); auto* ASC = MakeASC(Owner);
	FGameplayAbilitySpec First(UProject_JPlayerMaturityMeleeFixture::StaticClass());
	const auto Tag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
	First.GetDynamicSpecSourceTags().AddTag(Tag);
	const auto A = ASC->GiveAbility(First), B = ASC->GiveAbility(First);
	TArray<FText> Conflicts; FProject_JCombatConfiguration::FindInputGrantConflicts(*ASC, Conflicts);
	TestEqual(TEXT("Final grants reveal cross-source input collision"), Conflicts.Num(), 1);
	TestEqual(TEXT("Diagnostics do not mutate intentional shared grants"), ASC->GetActivatableAbilities().Num(), 2);
	ASC->ClearAbility(B); FProject_JCombatConfiguration::FindInputGrantConflicts(*ASC, Conflicts);
	TestTrue(TEXT("Retiring the colliding grant clears diagnostic"), Conflicts.IsEmpty());
	ASC->ClearAbility(A);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJExecutionConfigurationPolicyTest, "ProjectJ.PlayerMaturity.Combat.ConfigurationPolicy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJExecutionConfigurationPolicyTest::RunTest(const FString&)
{
	auto* Started = NewObject<UProject_JCombatStyleDefinition>();
	auto* Replacement = NewObject<UProject_JCombatStyleDefinition>();
	TestFalse(TEXT("Default policy preserves the captured execution after style replacement"),
		FProject_JCombatConfiguration::ShouldCancelExecution(Started->ExecutionChangePolicy, Started, Replacement));
	TestTrue(TEXT("Opt-in policy cancels a gameplay style replacement"),
		FProject_JCombatConfiguration::ShouldCancelExecution(EProject_JCombatExecutionChangePolicy::CancelOnGameplayStyleChange, Started, Replacement));
	TestFalse(TEXT("Animation-only configuration change does not cancel gameplay"),
		FProject_JCombatConfiguration::ShouldCancelExecution(EProject_JCombatExecutionChangePolicy::CancelOnGameplayStyleChange, Started, Started));
	TestTrue(TEXT("Removing the gameplay style is a replacement boundary"),
		FProject_JCombatConfiguration::ShouldCancelExecution(EProject_JCombatExecutionChangePolicy::CancelOnGameplayStyleChange, Started, nullptr));
	return true;
}

#if WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentAndMontageAuthoringTest, "ProjectJ.PlayerMaturity.Authoring.EquipmentAndMontage",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentAndMontageAuthoringTest::RunTest(const FString&)
{
	auto* Item = NewObject<UProject_JEquipmentItemDefinition>(); Item->EquipmentSlot = EProject_JEquipmentSlot::Head;
	Item->EquipmentEffects.Add(UGameplayEffect::StaticClass());
	FDataValidationContext ItemContext;
	TestTrue(TEXT("Editor rejects instant equipment effects"), Item->IsDataValid(ItemContext) == EDataValidationResult::Invalid);
	auto* Attack = NewObject<UProject_JAttackDefinition>(); Attack->AttackTag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
	Attack->Montage = NewObject<UAnimMontage>(); Attack->Montage->AddAnimCompositeSection(TEXT("Authored"), 0);
	Attack->MontageSectionName = TEXT("Missing");
	FDataValidationContext Missing;
	TestTrue(TEXT("Editor rejects nonexistent section"), Attack->IsDataValid(Missing) == EDataValidationResult::Invalid);
	Attack->MontageSectionName = TEXT("Authored");
	FDataValidationContext Valid;
	TestTrue(TEXT("Existing section passes the same authoring contract"), Attack->IsDataValid(Valid) != EDataValidationResult::Invalid);
	return true;
}
#endif
#endif
