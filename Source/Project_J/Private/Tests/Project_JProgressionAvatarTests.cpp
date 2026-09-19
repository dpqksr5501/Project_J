#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JGreatswordCharacter.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "UI/Project_JCharacterViewModel.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "UObject/UnrealType.h"
#include "Misc/ScopeExit.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProgressionAvatarTest, "ProjectJ.Extension.Progression.PlayerStateRespawn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProgressionAvatarTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT
	{
		if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::LevelTransition);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	};
	World->InitializeActorsForPlay(FURL());
	World->GetWorldSettings()->NotifyBeginPlay();
	World->GetWorldSettings()->NotifyMatchStarted();
	auto* Owner = World->SpawnActor<AProject_JPlayerState>();
	// Reuse the real locomotion contract without introducing combat/starting-equipment side effects.
	auto* FixtureClass = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	auto* ProfileProperty = FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), TEXT("CharacterAnimProfile"));
	if (!TestNotNull(TEXT("Authored character fixture"), FixtureClass) || !TestNotNull(TEXT("Animation profile property"), ProfileProperty)) return false;
	auto* AuthoredProfile = Cast<UProject_JCharacterAnimProfile>(ProfileProperty->GetObjectPropertyValue_InContainer(FixtureClass->GetDefaultObject()));
	if (!TestNotNull(TEXT("Authored animation profile"), AuthoredProfile)) return false;
	auto SpawnAvatar = [&]()
	{
		auto* Avatar = World->SpawnActorDeferred<AProject_JGreatswordCharacter>(AProject_JGreatswordCharacter::StaticClass(), FTransform::Identity,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		auto* Profile = NewObject<UProject_JCharacterAnimProfile>(Avatar);
		Profile->LocomotionProfile = AuthoredProfile->LocomotionProfile;
		ProfileProperty->SetObjectPropertyValue_InContainer(Avatar, Profile);
		Avatar->SetPlayerState(Owner);
		Avatar->FinishSpawning(FTransform::Identity);
		return Avatar;
	};
	auto* First = SpawnAvatar();
	auto* Class = NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId = TEXT("PersistentWarrior");
	auto* Set = NewObject<UProject_JAbilitySet>(); FProject_JAbilitySet_GameplayAbility Entry; Entry.Ability = UGameplayAbility::StaticClass(); Set->GrantedAbilityEntries.Add(Entry);
	Class->AbilitySets.Add(Set);
	TestTrue(TEXT("Class initialized through the existing character API"), First->InitializeCharacterClassDefinition(Class));
	auto* Advancement = NewObject<UProject_JCharacterAdvancementDefinition>(); Advancement->AdvancementId = TEXT("PersistentKnight"); Advancement->BaseClass = Class;
	TestTrue(TEXT("Advancement applied through the existing character API"), First->ApplyAdvancementDefinition(Advancement));
	TestEqual(TEXT("Public class view follows progression"), Owner->GetPublicClassId(), Class->ClassId);
	const int32 BeforeLevelRevision = First->GetCombatConfiguration().Revision;
	First->GetProgressionComponent()->SetLevel(12);
	TestEqual(TEXT("Level changes preserve combat configuration revision"), First->GetCombatConfiguration().Revision, BeforeLevelRevision);
	if (TestNotNull(TEXT("Avatar view model exists"), First->GetCharacterViewModel()))
		TestEqual(TEXT("View model level follows progression"), First->GetCharacterViewModel()->GetLevel(), 12);
	TestEqual(TEXT("Public level view follows progression"), Owner->GetPublicCharacterLevel(), 12);
	auto* ASC = Owner->GetProjectJAbilitySystemComponent(); const int32 Count = ASC->GetActivatableAbilities().Num();
	const int32 Revision = First->GetCombatConfiguration().Revision;
	TestEqual(TEXT("Unchanged configuration reuses its revision"), First->GetCombatConfiguration().Revision, Revision);
	auto* PersistentProgression = First->GetProgressionComponent();
	First->SetPlayerState(nullptr);
	TestFalse(TEXT("Detached avatar cannot mutate the former owner"), First->InitializeCharacterClassDefinition(Class));
	TestFalse(TEXT("Detached avatar releases the old progression subscription"), PersistentProgression->OnChanged.IsBoundToObject(First));
	First->Destroy();
	auto* Second = SpawnAvatar();
	TestTrue(TEXT("Second avatar resolves the same progression owner"), Second->GetProgressionComponent() == Owner->FindComponentByClass<UProject_JProgressionComponent>());
	TestEqual(TEXT("Class remains on PlayerState"), Second->GetCharacterClassId(), Class->ClassId);
	TestEqual(TEXT("Advancement remains on PlayerState"), Second->GetAdvancementId(), Advancement->AdvancementId);
	TestTrue(TEXT("Reinitialization is idempotent after respawn"), Second->InitializeCharacterClassDefinition(Class));
	TestEqual(TEXT("Respawn does not duplicate specs"), ASC->GetActivatableAbilities().Num(), Count);
	TestFalse(TEXT("Respawn does not forget acquisition history"), Second->ApplyAdvancementDefinition(Advancement));
	return true;
}
#endif
