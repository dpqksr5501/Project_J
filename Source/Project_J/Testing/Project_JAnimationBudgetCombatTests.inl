// Included by the editor-only probe: exercise production combat beside optional
// budgeted poses, without changing the production mesh class or authored assets.
#include "AIController.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Components/CapsuleComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Misc/App.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Project_JPlayerState.h"
#include "UObject/UnrealType.h"

namespace ProjectJAnimationBudgetProbe
{
enum class ECombatExit { Complete, Cancel, Destroy, Unequip, UnequipBeforeHit };

struct FCombatResult
{
	int32 HitEvents = 0, AppliedEffects = 0, ComboWindows = 0, CombatFrames = 0, BoneFrames = 0;
	bool bSawTrail = false, bSawRenderedTrail = false;
	FVector Displacement = FVector::ZeroVector;
};

class FCombatComparison : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	ECombatExit Exit;
	int32 Mode = 0, Ticks = 0, AttackTick = 0;
	UWorld* World = nullptr;
	TWeakObjectPtr<AProject_JGreatswordCharacter> Player;
	TWeakObjectPtr<AProject_JNPCCharacter> Target;
	TWeakObjectPtr<AProject_JPlayerState> State;
	TWeakObjectPtr<UAbilitySystemComponent> ASC, TargetASC;
	TWeakObjectPtr<UProject_JCombatPresentationComponent> Presentation;
	TWeakObjectPtr<UProject_JCombatHitValidationComponent> Hit;
	TStrongObjectPtr<UProject_JEquipmentItemDefinition> Item;
	FDelegateHandle BoneHandle, HitHandle, ComboHandle, EffectHandle;
	FGameplayTag HitTag;
	FCombatResult Results[2];
	FVector Origin = FVector(10000, 0, 1000);
	double Deadline = 0;
	bool bStarted = false, bExited = false;
	float MontageLength = 0;

	int32 ActiveCueCount() const
	{
		if (!Presentation.IsValid()) { return 0; }
		auto* Property = FindFProperty<FMapProperty>(Presentation->GetClass(), TEXT("ActiveLoopingCues"));
		return Property ? FScriptMapHelper(Property, Property->ContainerPtrToValuePtr<void>(Presentation.Get())).Num() : 0;
	}
	bool HasTrailState() const
	{
		if (!Presentation.IsValid()) { return false; }
		auto* Property = FindFProperty<FStructProperty>(Presentation->GetClass(), TEXT("ReplicatedPresentationState"));
		const auto* Value = Property ? Property->ContainerPtrToValuePtr<FProject_JReplicatedCombatPresentationState>(Presentation.Get()) : nullptr;
		return Value && Value->ActiveLoopingCueTags.HasTagExact(FGameplayTag::RequestGameplayTag(TEXT("PresentationCue.Combat.Trail")));
	}

	bool Setup()
	{
		// The test process sets CVars before creating worlds; never alter a live editor's settings.
		auto* Budget = IConsoleManager::Get().FindConsoleVariable(TEXT("a.Budget.BudgetMs"));
		if (!Budget || Budget->GetFloat() > 0.101f)
		{ Test->AddError(TEXT("Combat continuity requires a.Budget.BudgetMs 0.1 before test worlds start.")); return false; }
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(false);
		if (!Start(World, Mode, 100, 20.0f)) { Test->AddError(TEXT("Cannot start background pose workload")); return false; }
		World->GetSubsystem<UProject_JCharacterAnimationBudgetSubsystem>()->SetEnabledOverride(Mode == 1);
		Item.Reset(LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip")));
		UClass* VisualClass = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
		if (!Item || !VisualClass) { Test->AddError(TEXT("Missing authored equipment/character fixture")); return false; }
		const auto* Template = CastChecked<AProject_JPlayerCharacter>(VisualClass->GetDefaultObject());
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* Controller = World->SpawnActor<AAIController>(Params);
		State = World->SpawnActor<AProject_JPlayerState>(Params);
		Params.bDeferConstruction = true;
		Player = World->SpawnActor<AProject_JGreatswordCharacter>(AProject_JGreatswordCharacter::StaticClass(), Origin, FRotator::ZeroRotator, Params);
		if (!Controller || !State.IsValid() || !Player.IsValid()) { return false; }
		for (const FName Name : { FName(TEXT("CharacterAnimProfile")), FName(TEXT("CharacterClassDefinition")) })
		{
			auto* Property = FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), Name);
			if (!Property) { return false; }
			Property->SetObjectPropertyValue_InContainer(Player.Get(), Property->GetObjectPropertyValue_InContainer(Template));
		}
		auto* Mesh = Player->GetMesh();
		Mesh->SetSkeletalMesh(Template->GetMesh()->GetSkeletalMeshAsset());
		Mesh->SetRelativeTransform(Template->GetMesh()->GetRelativeTransform());
		Mesh->SetAnimInstanceClass(Template->GetMesh()->GetAnimClass());
		// Start with a throttled hidden mesh; production attack lifetime must protect it.
		Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
		Mesh->bEnableUpdateRateOptimizations = true;
		CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Mesh)->bBudgetTickWhenNotRendered = true;
		Player->SetActorEnableCollision(false);
		State->SetOwner(Controller); Controller->SetPlayerState(State.Get()); Player->SetPlayerState(State.Get());
		Player->FinishSpawning(FTransform(FRotator::ZeroRotator, Origin)); Controller->Possess(Player.Get());
		Player->GetCharacterMovement()->SetMovementMode(MOVE_Flying);
		ASC = Player->GetAbilitySystemComponent();
		Presentation = Player->FindComponentByClass<UProject_JCombatPresentationComponent>();
		Hit = Player->FindComponentByClass<UProject_JCombatHitValidationComponent>();
		if (!ASC.IsValid() || !Presentation.IsValid() || !Hit.IsValid() || !Mesh->GetAnimInstance()) { return false; }
		Test->TestNotNull(TEXT("Production character uses budget-capable mesh"), Cast<UProject_JBudgetedSkeletalMeshComponent>(Mesh));
		Test->TestFalse(TEXT("Production combat notifies are enabled"), Mesh->bSuppressNotifyEventDispatch);
		State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
		State->GetEquipmentManagerComponent()->EquipItem(Item.Get());
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		Player->FindComponentByClass<UProject_JWeaponPresentationComponent>()->AttachWeaponToDrawnSocket();
		Params.bDeferConstruction = false;
		Target = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), Origin + FVector(100, 0, 0), FRotator::ZeroRotator, Params);
		if (!Target.IsValid()) { return false; }
		Target->GetCharacterMovement()->DisableMovement();
		// Broad nearby stationary target: validates notify -> authority GE dispatch, not weapon geometry precision.
		Target->GetCapsuleComponent()->SetCapsuleSize(150, 180);
		Target->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Target->GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Overlap);
		TargetASC = Target->GetAbilitySystemComponent();
		if (!TargetASC.IsValid()) { return false; }
		EffectHandle = TargetASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda([this](UAbilitySystemComponent*, const FGameplayEffectSpec& Spec, FActiveGameplayEffectHandle)
		{
			if (bStarted && Spec.GetContext().GetInstigator() == Player.Get()) { ++Results[Mode].AppliedEffects; }
		});
		BoneHandle = Mesh->RegisterOnBoneTransformsFinalizedDelegate(FOnBoneTransformsFinalizedMultiCast::FDelegate::CreateLambda([this]
		{ if (bStarted && !bExited) { ++Results[Mode].BoneFrames; } }));
		ComboHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(FProject_JGameplayTags::Get().Event_Combat_ComboWindow).AddLambda([this](const FGameplayEventData* Data)
		{ if (bStarted && Data && Data->EventMagnitude > 0) { ++Results[Mode].ComboWindows; } });
		Ticks = 0; bStarted = bExited = false; Deadline = FPlatformTime::Seconds() + 90;
		return true;
	}

	void Cleanup()
	{
		if (ASC.IsValid())
		{
			ASC->GenericGameplayEventCallbacks.FindOrAdd(HitTag).Remove(HitHandle);
			ASC->GenericGameplayEventCallbacks.FindOrAdd(FProject_JGameplayTags::Get().Event_Combat_ComboWindow).Remove(ComboHandle);
			ASC->CancelAllAbilities();
		}
		if (TargetASC.IsValid()) { TargetASC->OnGameplayEffectAppliedDelegateToSelf.Remove(EffectHandle); }
		if (Player.IsValid()) { Player->GetMesh()->UnregisterOnBoneTransformsFinalizedDelegate(BoneHandle); }
		if (World)
		{
			World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
			World->DestroyWorld(false); GEngine->DestroyWorldContext(World); World = nullptr;
			if (Active) { Test->TestFalse(TEXT("Background allocator ownership released"), Active->bOwnAllocatorEnable); }
		}
		Player.Reset(); Target.Reset(); State.Reset(); ASC.Reset(); TargetASC.Reset(); Presentation.Reset(); Hit.Reset(); Item.Reset();
	}

	bool StartAttack()
	{
		Test->TestEqual(TEXT("Idle combat character participates in actual ABA only in Mode1"),
			CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh())->IsManagedByBudget(), Mode == 1);
		auto* Input = Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>();
		Input->ClearCommandInputHistory();
		// The current test equipment grants native Melee with an empty listener tag.
		// Wire only these transient granted instances to the borrowed notify. Never
		// change an ability CDO, montage, equipment definition or saved asset.
		const auto* Style = Player->GetCombatStyleDefinition();
		FGameplayTagContainer Tags; ASC->GetOwnedGameplayTags(Tags);
		const auto* Node = Style && Style->ComboDefinition ? Style->ComboDefinition->FindStartNode(FProject_JGameplayTags::Get().InputTag_Weapon_LMB, Tags) : nullptr;
		if (!Node || !Node->AttackDefinition || !Node->AttackDefinition->Montage) { return false; }
		for (const FAnimNotifyEvent& Event : Node->AttackDefinition->Montage->Notifies)
		{
			if (auto* Notify = Cast<UProject_JAnimNotifyState_MeleeHit>(Event.NotifyStateClass))
			{
				auto* Property = FindFProperty<FStructProperty>(Notify->GetClass(), TEXT("HitEventTag"));
				if (Property) { HitTag = *Property->ContainerPtrToValuePtr<FGameplayTag>(Notify); }
			}
		}
		if (!HitTag.IsValid()) { Test->AddError(TEXT("Authored melee notify has no event tag")); return false; }
		for (const auto& Spec : ASC->GetActivatableAbilities())
		{
			auto* Ability = Spec.GetPrimaryInstance();
			auto* Property = Ability ? FindFProperty<FStructProperty>(Ability->GetClass(), TEXT("MeleeHitEventTag")) : nullptr;
			if (Property && !Ability->HasAnyFlags(RF_ClassDefaultObject) && !Spec.IsActive())
			{
				UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("FixtureOnly HitListener=%s -> %s Ability=%s; authored assets unchanged"), *Property->ContainerPtrToValuePtr<FGameplayTag>(Ability)->ToString(), *HitTag.ToString(), *GetNameSafe(Ability));
				*Property->ContainerPtrToValuePtr<FGameplayTag>(Ability) = HitTag;
			}
		}
		bStarted = true; AttackTick = Ticks;
		Player->SetActorLocation(Origin, false, nullptr, ETeleportType::TeleportPhysics);
		Input->HandleInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
		const auto* Attack = Hit->GetActiveAttackDefinition();
		if (!Attack || !Attack->Montage || !Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying())
		{ Test->AddError(TEXT("Actual LMB input did not start an authored attack")); return false; }
		MontageLength = Attack->Montage->GetPlayLength();
		Test->TestTrue(TEXT("Production attack protects hidden pose updates"), Player->GetMesh()->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
		Test->TestFalse(TEXT("Production attack disables URO for mandatory notifies"), Player->GetMesh()->bEnableUpdateRateOptimizations);
		Test->TestFalse(TEXT("Attack immediately leaves allocator scheduling"), CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh())->IsManagedByBudget());
		for (const auto& Spec : ASC->GetActivatableAbilities())
		{
			if (Spec.IsActive())
			{
				auto* Ability = Spec.GetPrimaryInstance();
				auto* Property = Ability ? FindFProperty<FStructProperty>(Ability->GetClass(), TEXT("MeleeHitEventTag")) : nullptr;
				const auto* Tag = Property ? Property->ContainerPtrToValuePtr<FGameplayTag>(Ability) : nullptr;
				UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("CombatAuthoredContract Ability=%s Listener=%s Notify=%s Effect=%s"), *GetNameSafe(Ability), Tag ? *Tag->ToString() : TEXT("None"), *HitTag.ToString(), *GetNameSafe(Attack->DamageEffect.Get()));
			}
		}
		HitHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(HitTag).AddLambda([this](const FGameplayEventData*) { ++Results[Mode].HitEvents; });
		Test->TestFalse(TEXT("Hit before authored notify is rejected"), Hit->ProcessAuthorityHit(Target.Get()));
		return true;
	}

	bool EndCase()
	{
		const FCombatResult& R = Results[Mode];
		if (Exit == ECombatExit::UnequipBeforeHit)
		{
			Test->TestEqual(TEXT("Unequip before contact prevents every damage application"), R.AppliedEffects, 0);
		}
		else
		{
			Test->TestTrue(TEXT("Authored melee notify sent gameplay events"), R.HitEvents > 0);
			Test->TestEqual(TEXT("Authority applied exactly one GE to the target per swing"), R.AppliedEffects, 1);
			Test->TestTrue(TEXT("Authored trail notify opened recovery state (GPU rendering tested separately)"), R.bSawTrail);
			if (FApp::CanEverRender()) { Test->TestTrue(TEXT("Rendered run created a tracked Niagara trail"), R.bSawRenderedTrail); }
		}
		if (ASC.IsValid()) { Test->TestFalse(TEXT("ASC attack tag cleared even if avatar was destroyed"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking)); }
		if (Exit == ECombatExit::Complete) { Test->TestTrue(TEXT("Authored combo window notify delivered"), R.ComboWindows > 0); }
		if (Player.IsValid())
		{
			Test->TestFalse(TEXT("Attack tag cleared after completion/interruption"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking));
			Test->TestFalse(TEXT("Presentation identity cleared"), Presentation->GetActiveAttackTag().IsValid());
			Test->TestNull(TEXT("Hit definition cleared"), Hit->GetActiveAttackDefinition());
			Test->TestEqual(TEXT("No retained looping trail"), ActiveCueCount(), 0);
			Test->TestFalse(TEXT("Trail recovery state cleared"), HasTrailState());
			Test->TestFalse(TEXT("Late hit rejected after attack end"), Hit->ProcessAuthorityHit(Target.Get()));
			const bool bBudgeted = CastChecked<UProject_JBudgetedSkeletalMeshComponent>(Player->GetMesh())->IsManagedByBudget();
			Test->TestEqual(TEXT("Attack end restores URO or resumes allocator ownership"), bool(Player->GetMesh()->bEnableUpdateRateOptimizations), !bBudgeted);
			if (Mode == 1) { Test->TestTrue(TEXT("Idle character rejoins allocator after attack"), bBudgeted); }
			Test->TestTrue(TEXT("Attack end restores pre-attack visibility policy"), Player->GetMesh()->VisibilityBasedAnimTickOption == EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
		}
		UE_LOG(LogProjectJAnimationBudgetProbe, Display, TEXT("CombatContinuity Mode=%d Exit=%d HitEvents=%d Effects=%d ComboWindows=%d Trail=%d RenderedTrail=%d BoneFinalizations=%d CombatFrames=%d Displacement=%s"),
			Mode, int32(Exit), R.HitEvents, R.AppliedEffects, R.ComboWindows, R.bSawTrail, R.bSawRenderedTrail, R.BoneFrames, R.CombatFrames, *R.Displacement.ToString());
		Cleanup();
		if (Mode++ == 0) { return false; }
		Test->TestEqual(TEXT("Budget pressure preserves confirmed hit count"), Results[1].AppliedEffects, Results[0].AppliedEffects);
		Test->TestEqual(TEXT("Budget pressure preserves authored hit event count"), Results[1].HitEvents, Results[0].HitEvents);
		Test->TestEqual(TEXT("Budget pressure preserves combo events"), Results[1].ComboWindows, Results[0].ComboWindows);
		if (Exit != ECombatExit::UnequipBeforeHit) { Test->TestTrue(TEXT("Authored root motion produced nonzero movement"), Results[0].Displacement.Size() > 1.0); }
		Test->TestTrue(TEXT("Budget pressure preserves movement within 1cm"), Results[1].Displacement.Equals(Results[0].Displacement, 1.0));
		return true;
	}
public:
	FCombatComparison(FAutomationTestBase* InTest, ECombatExit InExit) : Test(InTest), Exit(InExit) {}
	virtual ~FCombatComparison() { if (World) { Cleanup(); } }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("Combat continuity fixture setup failed")); Cleanup(); return true; }
		if (++Ticks > 900 || FPlatformTime::Seconds() > Deadline)
		{ Test->AddError(TEXT("Combat continuity timed out")); Cleanup(); return true; }
		if (Target.IsValid() && Player.IsValid()) { Target->SetActorLocation(Player->GetActorLocation() + FVector(100, 0, 0)); }
		World->Tick(LEVELTICK_All, 1.0f / 60.0f);
		if (!bStarted)
		{
			if (Active->bMeasured && !StartAttack()) { Cleanup(); return true; }
			return false;
		}
		if (bExited) { return Ticks - AttackTick > FMath::CeilToInt((MontageLength + 0.5f) * 60) ? EndCase() : false; }
		++Results[Mode].CombatFrames;
		Results[Mode].Displacement = Player->GetActorLocation() - Origin;
		Results[Mode].bSawTrail |= HasTrailState();
		Results[Mode].bSawRenderedTrail |= ActiveCueCount() > 0;
		// Interrupt after root motion has actually started, not just at the first
		// notify frame (the authored anticipation portion is stationary).
		if (Exit == ECombatExit::UnequipBeforeHit || (Exit != ECombatExit::Complete && Results[Mode].AppliedEffects > 0 && Results[Mode].bSawTrail && Results[Mode].Displacement.Size() > 10.0))
		{
			if (Exit == ECombatExit::Cancel) { ASC->CancelAllAbilities(); }
			else if (Exit == ECombatExit::Unequip || Exit == ECombatExit::UnequipBeforeHit)
			{
				State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
				Test->TestNull(TEXT("Equipment removed during active root-motion attack"), State->GetEquipmentManagerComponent()->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon));
				Test->TestEqual(TEXT("Unequip clears local looping components immediately"), ActiveCueCount(), 0);
				Test->TestNull(TEXT("Unequip immediately closes damage definition"), Hit->GetActiveAttackDefinition());
				Test->TestFalse(TEXT("Unequip immediately ends attacking state"), ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking));
				Test->TestFalse(TEXT("Unequip rejects subsequent hits"), Hit->ProcessAuthorityHit(Target.Get()));
			}
			else
			{
				TWeakObjectPtr<AProject_JGreatswordCharacter> Destroyed = Player;
				Player->Destroy();
				Test->TestFalse(TEXT("Destroyed combat actor is invalid"), Destroyed.IsValid());
				Player.Reset();
			}
			bExited = true;
		}
		else if (Exit == ECombatExit::Complete && !Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying()) { bExited = true; }
		return false;
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationCombatComplete, "ProjectJ.GroupB.CombatContinuity.Complete", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationCombatComplete::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FCombatComparison(this, ProjectJAnimationBudgetProbe::ECombatExit::Complete)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationCombatCancel, "ProjectJ.GroupB.CombatContinuity.Cancel", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationCombatCancel::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FCombatComparison(this, ProjectJAnimationBudgetProbe::ECombatExit::Cancel)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationCombatDestroy, "ProjectJ.GroupB.CombatContinuity.Destroy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationCombatDestroy::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FCombatComparison(this, ProjectJAnimationBudgetProbe::ECombatExit::Destroy)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationCombatUnequip, "ProjectJ.GroupB.CombatContinuity.Unequip", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationCombatUnequip::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FCombatComparison(this, ProjectJAnimationBudgetProbe::ECombatExit::Unequip)); return true; }
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationCombatUnequipBeforeHit, "ProjectJ.GroupB.CombatContinuity.UnequipBeforeHit", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationCombatUnequipBeforeHit::RunTest(const FString&) { ADD_LATENT_AUTOMATION_COMMAND(ProjectJAnimationBudgetProbe::FCombatComparison(this, ProjectJAnimationBudgetProbe::ECombatExit::UnequipBeforeHit)); return true; }
