#if WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Animation/Project_JAnimationClock.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
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
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Mount/Project_JMountComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Project_JNPCCharacter.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameplayEffect.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
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
	Runtime->RemoveEquipmentGameplay(*Rider, *Item, Ledger);
	TestEqual(TEXT("Unequip after avatar swap restores original stat"), OldStats->GetMaxHealth(), 100.0f);
	TestEqual(TEXT("Replacement ASC is untouched"), NewStats->GetMaxHealth(), 200.0f);
	TestEqual(TEXT("Original grant is retired"), Old->GetActivatableAbilities().Num(), 0);
	Runtime->RemoveEquipmentGameplay(*Rider, *Item, Ledger);
	TestEqual(TEXT("Repeated cleanup is idempotent"), OldStats->GetMaxHealth(), 100.0f);
	Rider->CurrentASC = Old;
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, Ledger);
	Rider->CurrentASC = nullptr;
	Runtime->RemoveEquipmentGameplay(*Rider, *Item, Ledger);
	TestEqual(TEXT("Losing current ASC still retires old grants"), Old->GetActivatableAbilities().Num(), 0);
	TestEqual(TEXT("Losing current ASC still reverses fallback"), OldStats->GetMaxHealth(), 100.0f);
	Rider->CurrentASC = Old;
	Runtime->ApplyEquipmentGameplay(*Rider, *Item, Ledger);
	auto* Buff = NewObject<UGameplayEffect>(); Buff->DurationPolicy = EGameplayEffectDurationType::Infinite;
	auto& GE = Buff->Modifiers.AddDefaulted_GetRef(); GE.Attribute = OldStats->GetMaxHealthAttribute();
	GE.ModifierOp = EGameplayModOp::Additive; GE.ModifierMagnitude = FScalableFloat(25);
	Ledger.GrantedEffectHandles.Add(Old->ApplyGameplayEffectToSelf(Buff, 1, Old->MakeEffectContext()));
	Rider->CurrentASC = New;
	Runtime->RemoveEquipmentGameplay(*Rider, *Item, Ledger);
	TestEqual(TEXT("Active effects and fallback retire on original ASC together"), OldStats->GetMaxHealth(), 100.0f);
	Item->StatApplicationPolicy = EProject_JEquipmentStatApplicationPolicy::GameplayEffectsThenStatModifiers;
	Item->EquipmentEffects.Add(UGameplayEffect::StaticClass());
	Rider->CurrentASC = Old; Runtime->ApplyEquipmentGameplay(*Rider, *Item, Ledger);
	TestTrue(TEXT("Instant equipment effect is never retained/applied as a removable buff"), Ledger.GrantedEffectHandles.IsEmpty());
	TestEqual(TEXT("Invalid effect uses exactly one stat fallback"), OldStats->GetMaxHealth(), 150.0f);
	Runtime->RemoveEquipmentGameplay(*Rider, *Item, Ledger);
	TestEqual(TEXT("Fallback remains reversible"), OldStats->GetMaxHealth(), 100.0f);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTrajectoryFreshnessTest, "ProjectJ.PlayerMaturity.Trajectory.Freshness",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJTrajectoryFreshnessTest::RunTest(const FString&)
{
	FTestWorld W;
	W.World->Tick(LEVELTICK_TimeOnly, 1.0f);
	auto* Player = W.World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* Controller = W.World->SpawnActor<APlayerController>(); Controller->Possess(Player);
	// NullRHI has no renderer; explicitly model visual demand for the producer.
	Player->GetMesh()->SetHiddenInGame(false);
	Player->GetMesh()->SetLastRenderTime(W.World->GetTimeSeconds());
	auto* C = Player->GetMotionMatchingTrajectoryComponent();
	C->UpdateTrajectoryState(1.0f/60.0f);
	TestTrue(TEXT("Fresh generated trajectory is usable"), C->IsTrajectoryPredictionUsable());
	FVector Velocity; float Angle;
	TestTrue(TEXT("Fresh sample query succeeds"), C->TryGetFuturePlanarVelocity(0.2f, FVector::ZeroVector, Velocity, Angle));
	C->LastGeneratedWorldTimeSeconds = W.World->GetTimeSeconds() - 0.1;
	TestTrue(TEXT("Prediction survives sparse multi-frame cadence within age budget"), C->IsTrajectoryPredictionUsable());
	C->LastGeneratedWorldTimeSeconds = W.World->GetTimeSeconds() - 1.0;
	TestFalse(TEXT("Stale sample query is rejected"), C->TryGetFuturePlanarVelocity(0.2f, FVector::ZeroVector, Velocity, Angle));
	TestTrue(TEXT("Rejected sample returns zero velocity"), Velocity.IsZero());
	C->ResetTrajectoryHistory();
	TestFalse(TEXT("Reset samples are invalid until regeneration"), C->IsTrajectoryPredictionUsable());
	C->UpdateTrajectoryState(1.0f/30.0f);
	TestTrue(TEXT("Regeneration after reset restores usability"), C->IsTrajectoryPredictionUsable());
	C->bWasTrajectoryGenerationEligible = false;
	TestFalse(TEXT("No generation demand invalidates retained samples"), C->IsTrajectoryPredictionUsable());
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
	auto* Player = W.World->SpawnActor<AProject_JGreatswordCharacter>();
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
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLayerLoadLifecycleTest, "ProjectJ.PlayerMaturity.Animation.LayerLoadLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLayerLoadLifecycleTest::RunTest(const FString&)
{
	const FSoftObjectPath Path(TEXT("/Game/Tests/Missing.Missing_C"));
	auto* Mounted = NewObject<UProject_JMountedAnimationLayerComponent>();
	Mounted->PreloadedAnimationLayerPath = Path; Mounted->LoadGeneration = 10;
	Mounted->HandleLayerPreloadCompleted(Path, 9);
	TestFalse(TEXT("Stale mounted callback cannot poison new request"), Mounted->bLoadFailed);
	AddExpectedError(TEXT("Layer load failed; default pose retained"), EAutomationExpectedErrorFlags::Contains, 2);
	Mounted->HandleLayerPreloadCompleted(Path, 10);
	TestTrue(TEXT("Missing mounted layer records terminal failure"), Mounted->bLoadFailed);
	Mounted->ResetPreload();
	TestFalse(TEXT("Explicit reset creates retryable generation"), Mounted->bLoadFailed);
	TestTrue(TEXT("Reset invalidates old same-path callback"), Mounted->LoadGeneration > 10);
	auto* Combat = NewObject<UProject_JCombatAnimationLayerComponent>();
	Combat->PreloadedAnimationLayerPath = Path; Combat->LoadGeneration = 20;
	Combat->HandleLayerPreloadCompleted(Path, 20);
	TestTrue(TEXT("Missing combat layer has the same failure contract"), Combat->bLoadFailed);
	Combat->bEndingPlay = true; Combat->bLoadFailed = false;
	Combat->HandleLayerPreloadCompleted(Path, 20);
	TestFalse(TEXT("EndPlay ignores in-flight callback"), Combat->bLoadFailed);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationClockTest, "ProjectJ.PlayerMaturity.Animation.ClockAndTIP",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationClockTest::RunTest(const FString&)
{
	for (const int32 FPS : {30, 60, 120})
	{
		FProject_JAnimationClock Clock;
		for (int32 i=0; i<FPS; ++i) { Clock.Advance(1.0f/FPS, false); }
		TestTrue(TEXT("Animation time is frame-rate independent"), FMath::IsNearlyEqual(Clock.Seconds, 1.0, 1.e-5));
		Clock.Advance(10.0f, true);
		TestTrue(TEXT("Pause does not advance animation progress"), FMath::IsNearlyEqual(Clock.Seconds, 1.0, 1.e-5));
		Clock.Advance(0.25f, false);
		TestTrue(TEXT("UE's dilated delta is applied exactly once"), FMath::IsNearlyEqual(Clock.Seconds, 1.25, 1.e-5));
		Clock.Advance(std::numeric_limits<float>::quiet_NaN(), false);
		TestTrue(TEXT("Invalid delta is ignored"), FMath::IsFinite(Clock.Seconds));
	}
	FProject_JAnimationClock Sparse; Sparse.Advance(0.2f, false);
	FProject_JAnimationClock Dense; for(int32 i=0;i<12;++i) { Dense.Advance(1.0f/60.0f, false); }
	TestTrue(TEXT("URO accumulated delta and dense updates agree"), FMath::IsNearlyEqual(Sparse.Seconds, Dense.Seconds, 1.e-5));
	FTestWorld W; auto* Player = W.World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Player->GetMesh());
	Anim->AnimationClock.Seconds = 50;
	Anim->StateControllerRuntime.BeginHold(EProject_JStateControllerPresentationState::TurnInPlace, 40);
	auto& Proxy = Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	Proxy.ThreadSafeData.OneShotPresentation.TransitionElapsedTime = 0.3f;
	TestEqual(TEXT("TIP reads the published animation clock, not the live real-time hold"), Anim->GetThreadSafeStateControllerPlaybackHoldElapsedTime(), 0.3f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerVisibilityTest, "ProjectJ.PlayerMaturity.Animation.SharedVisibility",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerVisibilityTest::RunTest(const FString&)
{
	FTestWorld W;
	auto* Player = W.World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* Leader = Player->GetMesh(); Leader->SetHiddenInGame(true); Leader->SetLastRenderTime(-100);
	auto* Follower = NewObject<USkeletalMeshComponent>(Player); Follower->SetupAttachment(Leader); Follower->RegisterComponent();
	Follower->SetHiddenInGame(false); Follower->SetLastRenderTime(W.World->GetTimeSeconds());
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Leader); Leader->AnimScriptInstance = Anim; Anim->InitializeAnimation();
	auto* Locomotion = Player->GetLocomotionAnimStateComponent(); Locomotion->RefreshCachedReferences();
	auto* Trajectory = Player->GetMotionMatchingTrajectoryComponent();
	TestTrue(TEXT("Follower visibility wakes AnimInstance"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	TestTrue(TEXT("Follower visibility wakes locomotion semantics"), Locomotion->WasRecentlyRendered(0.25f));
	TestTrue(TEXT("Follower visibility permits trajectory producer"), Trajectory->ShouldGenerateTrajectory(*Player));
	Follower->SetLastRenderTime(-100);
	TestFalse(TEXT("No visible renderer suspends trajectory"), Trajectory->ShouldGenerateTrajectory(*Player));
	TestFalse(TEXT("No visible renderer suspends semantic presentation"), Locomotion->WasRecentlyRendered(0.25f));
	Leader->SetLastRenderTime(W.World->GetTimeSeconds());
	TestFalse(TEXT("Hidden leader shadow timestamp cannot override visual demand"), Anim->WasOwnerVisualRecentlyRendered(0.25f));
	Leader->SetHiddenInGame(false);
	TestTrue(TEXT("Visible leader remains a supported renderer"), Trajectory->ShouldGenerateTrajectory(*Player));
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
