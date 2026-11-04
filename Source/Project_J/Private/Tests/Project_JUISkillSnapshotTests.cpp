#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Tests/Project_JUISkillQueryFixture.h"
#include "UI/Project_JPlayerUIComponent.h"
#include "UI/Project_JPlayerHUDWidget.h"
#include "Components/HorizontalBox.h"
#include "Components/ProgressBar.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJUISkillSnapshotTest, "ProjectJ.Foundation.UI.SkillQuerySnapshot",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJUISkillSnapshotTest::RunTest(const FString&)
{
	auto* Ability = GetMutableDefault<UProject_JUISkillQueryFixture>();
	ON_SCOPE_EXIT { Ability->OnQuery = nullptr; Ability->OnCooldown = nullptr; Ability->Queries = Ability->CooldownQueries = 0; };
	const auto Tag = FProject_JGameplayTags::Get().InputTag_Skill_Q;
	const auto OtherTag = FProject_JGameplayTags::Get().InputTag_Skill_R;
	for (int32 Mode = 0; Mode < 10; ++Mode)
	{
		auto* World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		ON_SCOPE_EXIT { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
		auto* State = World->SpawnActor<AProject_JPlayerState>();
		auto* Controller = World->SpawnActor<APlayerController>();
		auto* Avatar = World->SpawnActor<AActor>();
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		auto* ASC = State->GetProjectJAbilitySystemComponent(); ASC->InitAbilityActorInfo(State, Avatar);
		FGameplayAbilitySpec Spec(Ability); Spec.GetDynamicSpecSourceTags().AddTag(Tag); Spec.GetDynamicSpecSourceTags().AddTag(OtherTag);
		const auto Handle = ASC->GiveAbility(Spec);
		TestTrue(TEXT("Queries consume the actual post-initialization Avatar"), ASC->GetAvatarActor() == Avatar);
		auto* UI = NewObject<UProject_JPlayerUIComponent>(Controller); Controller->AddInstanceComponent(UI); UI->RegisterComponent();
		UI->Screen = NewObject<UProject_JPlayerHUDWidget>(UI);
		UI->Screen->ActionBar = NewObject<UHorizontalBox>(UI->Screen);
		UI->BoundASC = ASC; UI->BoundState = State;
		UI->PresentedSkills = {{Tag, FText::FromString(TEXT("Q"))}, {OtherTag, FText::FromString(TEXT("R"))}};
		UI->HUDPreferences.Slots.SetNum(10);
		for (int32 Index = 0; Index < 10; ++Index)
		{
			auto& Binding = UI->HUDPreferences.Slots[Index]; Binding.Kind = EProject_JQuickSlotKind::Skill; Binding.InputTag = Index & 1 ? OtherTag : Tag;
			auto* Button = NewObject<UProject_JSkillButton>(UI->Screen); Button->Configure(UI, Index);
			Button->CooldownBar = NewObject<UProgressBar>(Button);
			UI->Screen->SkillButtons.Add(Button);
		}
		const TStrongObjectPtr<UProject_JSkillButton> First(UI->Screen->SkillButtons[0]);
		Ability->Queries = Ability->CooldownQueries = 0; Ability->OnQuery = nullptr; Ability->OnCooldown = nullptr;
		if (Mode == 1) Ability->OnQuery = [UI] { UI->ClearSources(); CollectGarbage(RF_NoFlags); };
		if (Mode == 2) Ability->OnQuery = [=] { ASC->InitAbilityActorInfo(State, Controller); };
		if (Mode == 3) Ability->OnQuery = [UI] { UI->UpdateSkills(); };
		if (Mode == 4) Ability->OnCooldown = [UI] { UI->EndPlay(EEndPlayReason::RemovedFromWorld); CollectGarbage(RF_NoFlags); };
		if (Mode == 5) Ability->OnQuery = [UI] { UI->Screen = NewObject<UProject_JPlayerHUDWidget>(UI); UI->PresentedSkills.Reset(); };
		if (Mode == 6) Ability->OnQuery = [this, Avatar] { TestTrue(TEXT("Actual queried Avatar retires"), Avatar->Destroy()); CollectGarbage(RF_NoFlags); };
		if (Mode == 7) Ability->OnQuery = [=] { ASC->InitAbilityActorInfo(State, Avatar); };
		if (Mode == 8) Ability->OnQuery = [ASC] { ASC->ClearAllAbilities(); };
		if (Mode == 9) Ability->OnQuery = [=] { ASC->ClearAbility(Handle); };
		UI->UpdateSkills();
		TestEqual(FString::Printf(TEXT("One evaluation for a spec shared by ten slots and two tags (mode %d)"), Mode), Ability->Queries, 1);
		if (Mode == 0)
		{
			TestEqual(TEXT("One GAS list visit"), UI->LastSkillSpecVisits, 1);
			TestEqual(TEXT("One evaluation"), UI->LastSkillAbilityEvaluations, 1);
			TestEqual(TEXT("Cooldown queried once for all matching slots"), Ability->CooldownQueries, 1);
			for (const auto& Button : UI->Screen->SkillButtons)
			{
				TestTrue(TEXT("Both input tags preserve availability"), Button->bAvailable);
				TestEqual(TEXT("Every duplicate slot receives the same cooldown"), Button->CooldownBar->GetPercent(), .5f);
			}
			TestTrue(TEXT("A live cooldown arms its existing 0.2s timer"), World->GetTimerManager().TimerExists(UI->SkillsTimer));
			ASC->ClearAllAbilities(); UI->UpdateSkills();
			TestFalse(TEXT("Removed skills become unavailable without a persistent cache"), First->bAvailable);
			TestFalse(TEXT("No cooldown means no polling timer"), World->GetTimerManager().TimerExists(UI->SkillsTimer));
		}
		else
		{
			TestFalse(FString::Printf(TEXT("Stale/retired snapshot never publishes its available result (mode %d)"), Mode), First->bAvailable);
			TestFalse(FString::Printf(TEXT("Stale query never rearms the retired countdown (mode %d)"), Mode), World->GetTimerManager().TimerExists(UI->SkillsTimer));
			TestEqual(FString::Printf(TEXT("Source changes and GAS deferred removals are checked at their actual boundary (mode %d)"), Mode), Ability->CooldownQueries, Mode == 4 || Mode == 8 ? 1 : 0);
			if (Mode == 4) TestFalse(TEXT("EndPlay admits no deferred refresh"), UI->SkillsRefreshTimer.IsValid());
			else TestTrue(FString::Printf(TEXT("A live replacement coalesces one deferred refresh (mode %d)"), Mode), UI->SkillsRefreshTimer.IsValid());
		}
		World->GetTimerManager().ClearAllTimersForObject(UI);
	}
	AddInfo(TEXT("Real ASC/spec and HUD button consumers: ten duplicate quickslots/two input tags evaluate one shared spec once; clear/GC, ActorInfo replacement, reentry, EndPlay/GC, screen/profile replacement and destroyed Avatar reject stale publication. Not an FPS or GPU benchmark."));
	return true;
}
#endif
