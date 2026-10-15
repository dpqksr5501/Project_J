#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Game/Project_JPlayerState.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JCombatStateComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUISkillReleaseOwnershipTest,
	"ProjectJ.Continuation.UISkillReleaseOwnership", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUISkillReleaseOwnershipTest::RunTest(const FString&)
{
	for (int32 Mode : {0, 1, 2})
	{
		const bool bReleaseAll = Mode != 0;
		const bool bRetiring = Mode == 2;
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
		auto* State = World->SpawnActor<AProject_JPlayerState>();
		auto* Controller = World->SpawnActor<APlayerController>();
		auto* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
		if (!State || !Controller || !Player) { AddError(TEXT("Fixture spawn failed")); return false; }
		State->SetOwner(Controller); Controller->SetPlayerState(State); Player->SetPlayerState(State);
		Controller->SetAsLocalPlayerController(); // LocalPredicted input; no LocalPlayer/settings session.
		Controller->Possess(Player);
		auto* ASC = State->GetProjectJAbilitySystemComponent();
		ASC->InitAbilityActorInfo(State, Player);
		Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>()->Initialize(Player);
		Player->FindComponentByClass<UProject_JCombatStateComponent>()->BindToAbilitySystem(ASC);
		if (!TestTrue(TEXT("Persistent PlayerState owns current input ASC"),
			Player->GetAbilitySystemComponent() == ASC && ASC->GetAvatarActor() == Player)) { return false; }
		auto* UI = NewObject<UProject_JPlayerUIComponent>(Controller);
		Controller->AddInstanceComponent(UI); // No Screen/LocalPlayer or settings writes.
		// Establish the engine component lifecycle without screen/profile setup.
		UI->RegisterComponent(); UI->RegisterAllComponentTickFunctions(true);
		UI->UActorComponent::BeginPlay();
		const auto& Tags = FProject_JGameplayTags::Get();
		const FGameplayTag Inputs[] = {Tags.InputTag_Weapon_LMB, Tags.InputTag_Weapon_RMB, Tags.InputTag_Skill_Q};
		TArray<FGameplayAbilitySpecHandle> Handles;
		for (FGameplayTag Tag : Inputs)
		{
			FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Sprint::StaticClass(), 1);
			Spec.GetDynamicSpecSourceTags().AddTag(Tag); Handles.Add(ASC->GiveAbility(Spec));
			FProject_JUISkillSlot Slot; Slot.InputTag = Tag; UI->PresentedSkills.Add(Slot);
		}
		UI->HUDPreferences.Slots.SetNum(2);
		for (int32 Index = 0; Index < 2; ++Index)
		{
			UI->HUDPreferences.Slots[Index].Kind = EProject_JQuickSlotKind::Skill;
			UI->HUDPreferences.Slots[Index].InputTag = Inputs[Index];
		}
		bool bReentered = false;
		const FDelegateHandle Callback = ASC->AbilityEndedCallbacks.AddLambda([&](UGameplayAbility*)
		{
			if (bReentered) { return; }
			bReentered = true;
			UI->HUDPreferences.Slots[0].InputTag = Inputs[2];
			UI->PressSkill(0); // Same slot, new tag, after a real native GAS end.
		});
		ON_SCOPE_EXIT { ASC->AbilityEndedCallbacks.Remove(Callback); UI->ReleaseAllSkills(); ASC->CancelAllAbilities(); if (UI->HasBegunPlay()) { UI->EndPlay(EEndPlayReason::Destroyed); } };
		UI->PressSkill(0);
		if (bReleaseAll)
		{
			UI->PressSkill(1);
			if (!TestTrue(TEXT("Second old press is active before bulk release"),
				ASC->FindAbilitySpecFromHandle(Handles[1])->IsActive() && ASC->FindAbilitySpecFromHandle(Handles[1])->InputPressed)) { return false; }
		}
		if (!TestTrue(TEXT("Old press activates actual native ability"), ASC->FindAbilitySpecFromHandle(Handles[0])->IsActive())) { return false; }
		if (bRetiring) { UI->EndPlay(EEndPlayReason::Destroyed); }
		else if (bReleaseAll) { UI->ReleaseAllSkills(); } else { UI->ReleaseSkill(0); }
		TestTrue(TEXT("Release reaches actual GAS end callback"), bReentered);
		TestFalse(TEXT("Original tag is released"), ASC->FindAbilitySpecFromHandle(Handles[0])->InputPressed);
		if (bReleaseAll) { TestFalse(TEXT("All remaining original tags are released"), ASC->FindAbilitySpecFromHandle(Handles[1])->InputPressed); }
		if (bRetiring)
		{
			TestTrue(TEXT("Actual EndPlay closes admission"), UI->bEnding);
			TestTrue(TEXT("Actual EndPlay leaves no new receipt"), UI->PressedSkills.IsEmpty());
			TestFalse(TEXT("EndPlay callback cannot leave a new active input"),
				ASC->FindAbilitySpecFromHandle(Handles[2])->InputPressed || ASC->FindAbilitySpecFromHandle(Handles[2])->IsActive());
			continue;
		}
		TestTrue(TEXT("New same-slot receipt survives old release"), UI->PressedSkills.FindRef(0) == Inputs[2]);
		TestTrue(TEXT("New receipt retains its pawn"), UI->PressedPawn.Get() == Player);
		TestTrue(TEXT("New press reaches the current ASC"), ASC->FindAbilitySpecFromHandle(Handles[2])->InputPressed);
		TestTrue(TEXT("New native ability is active"), ASC->FindAbilitySpecFromHandle(Handles[2])->IsActive());
		UI->ReleaseSkill(0);
		TestFalse(TEXT("Independent later release consumes new input"), ASC->FindAbilitySpecFromHandle(Handles[2])->InputPressed);
		TestFalse(TEXT("Independent later release consumes its receipt"), UI->PressedSkills.Contains(0));
	}
	return true;
}
#endif
