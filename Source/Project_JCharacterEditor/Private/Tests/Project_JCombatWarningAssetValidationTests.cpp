#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/DataValidation.h"
#include "Validation/Project_JCombatWarningAssetValidator.h"
#include "Animation/AnimMontage.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JGroundAttack.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "UObject/UnrealType.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatWarningAssetValidationTest,
	"ProjectJ.Integrated.GameplayWarningAssetValidation", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJCombatWarningAssetValidationTest::RunTest(const FString&)
{
	auto* Ability = NewObject<UProject_JGameplayAbility_GroundAttack>();
	FDataValidationContext Dynamic;
	TestEqual(TEXT("A spec may supply the ground attack at grant time"), Ability->IsDataValid(Dynamic), EDataValidationResult::Valid);
	auto* Radius = FindFProperty<FFloatProperty>(Ability->GetClass(), TEXT("RadiusCm"));
	if (!TestNotNull(TEXT("Ground attack radius field"), Radius)) return false;
	Radius->SetPropertyValue_InContainer(Ability, 49.f);
	FDataValidationContext BadRadius;
	TestEqual(TEXT("Invalid ground ability radius is caught before activation"), Ability->IsDataValid(BadRadius), EDataValidationResult::Invalid);
	Radius->SetPropertyValue_InContainer(Ability, 50.f);
	auto* AssignedAttack = FindFProperty<FObjectProperty>(Ability->GetClass(), TEXT("Attack"));
	if (!TestNotNull(TEXT("Ground attack definition field"), AssignedAttack)) return false;
	AssignedAttack->SetObjectPropertyValue_InContainer(Ability, NewObject<UProject_JAttackDefinition>(Ability));
	FDataValidationContext BadPolicy;
	TestEqual(TEXT("A fixed incompatible attack cannot silently cancel every cast"), Ability->IsDataValid(BadPolicy), EDataValidationResult::Invalid);
	auto* Montage = NewObject<UAnimMontage>();
	auto* Cue = NewObject<UProject_JAnimNotifyState_CombatPresentationCue>(Montage);
	auto& Event = Montage->Notifies.AddDefaulted_GetRef();
	Event.NotifyStateClass = Cue;
	Event.SetDuration(0.5f);
	auto* Purpose = FindFProperty<FEnumProperty>(Cue->GetClass(), TEXT("Purpose"));
	auto* Tag = FindFProperty<FStructProperty>(Cue->GetClass(), TEXT("CueTag"));
	if (!TestNotNull(TEXT("Authored purpose is reflected"), Purpose) || !TestNotNull(TEXT("Authored tag is reflected"), Tag)) return false;
	Purpose->GetUnderlyingProperty()->SetIntPropertyValue(Purpose->ContainerPtrToValuePtr<void>(Cue), static_cast<uint64>(EProject_JCombatCuePurpose::GameplayWarning));
	// Reuse the runtime test tag; Editor modules must never add native network tags.
	*Tag->ContainerPtrToValuePtr<FGameplayTag>(Cue) = FGameplayTag::RequestGameplayTag(FName(TEXT("ProjectJ.Tests.Presentation.Cue")));
	Event.bCanBeFilteredViaRequest = false;
	auto* Validator = NewObject<UProject_JCombatWarningAssetValidator>();
	auto* Job = NewObject<UProject_JCharacterClassDefinition>();
	auto* Granted = NewObject<UProject_JAbilitySet>(Job);
	Job->AbilitySets.Add(Granted);
	Granted->GrantedAbilityEntries.AddDefaulted_GetRef().Ability = UProject_JGameplayAbility_GroundAttack::StaticClass();
	FDataValidationContext MissingDefinition;
	TestEqual(TEXT("A class grant cannot supply a missing attack via its owner"),
		Validator->ValidateLoadedAsset(FAssetData(Job), Job, MissingDefinition), EDataValidationResult::Invalid);
	const auto Validate = [&]()
	{
		FDataValidationContext Context;
		return Validator->ValidateLoadedAsset(FAssetData(Montage), Montage, Context);
	};
	TestEqual(TEXT("Deterministic server warning is valid"), Validate(), EDataValidationResult::Valid);
	Event.bTriggerOnDedicatedServer = false;
	TestEqual(TEXT("Server-suppressed warning is rejected at its parent asset"), Validate(), EDataValidationResult::Invalid);
	Event.bTriggerOnDedicatedServer = true;
	Event.NotifyTriggerChance = 0.5f;
	TestEqual(TEXT("Probabilistic warning cannot silently disappear"), Validate(), EDataValidationResult::Invalid);
	Event.NotifyTriggerChance = 1.f;
	Event.NotifyFilterType = ENotifyFilterType::LOD;
	TestEqual(TEXT("LOD cannot suppress authority warning production"), Validate(), EDataValidationResult::Invalid);
	Event.NotifyFilterType = ENotifyFilterType::NoFiltering;
	Event.bCanBeFilteredViaRequest = true;
	TestEqual(TEXT("Request-filtered producer is rejected"), Validate(), EDataValidationResult::Invalid);
	Event.bCanBeFilteredViaRequest = false;
	Event.SetDuration(0.f);
	TestEqual(TEXT("Empty warning interval is rejected"), Validate(), EDataValidationResult::Invalid);
	Event.SetDuration(0.5f);
	*Tag->ContainerPtrToValuePtr<FGameplayTag>(Cue) = FGameplayTag();
	TestEqual(TEXT("Warning must resolve a runtime cue key"), Validate(), EDataValidationResult::Invalid);
	Purpose->GetUnderlyingProperty()->SetIntPropertyValue(Purpose->ContainerPtrToValuePtr<void>(Cue), static_cast<uint64>(EProject_JCombatCuePurpose::Cosmetic));
	TestEqual(TEXT("Cosmetic assets are outside this validator's warning contract"), Validate(), EDataValidationResult::NotValidated);
	// Exercise the shipped authoring graph on transient copies; keep the saved content untouched.
	const auto* Boss = LoadObject<UProject_JAttackDefinition>(nullptr, TEXT("/Game/DataAssetSets/MMO/DA_Attack_MMOTestBoss_Cleave.DA_Attack_MMOTestBoss_Cleave"));
	if (!TestNotNull(TEXT("B01 attack definition"), Boss) || !TestNotNull(TEXT("B01 attack montage"), Boss->Montage.Get())) return false;
	{
		// Exercise the actual saved dependency edge. Restore in memory before returning to the editor.
		TGuardValue<TArray<FAnimNotifyEvent>> RestoreNotifies(Boss->Montage->Notifies, TArray<FAnimNotifyEvent>{});
		FDataValidationContext Context;
		TestEqual(TEXT("Editing only the montage catches deletion of its last required producer"),
			Validator->ValidateLoadedAsset(FAssetData(Boss->Montage.Get()), Boss->Montage.Get(), Context), EDataValidationResult::Invalid);
	}
	auto* Attack = DuplicateObject<UProject_JAttackDefinition>(Boss, GetTransientPackage());
	Attack->Montage = DuplicateObject<UAnimMontage>(Boss->Montage.Get(), Attack);
	Attack->WarningPolicy = EProject_JAttackWarningPolicy::MontageAttention;
	Attack->RequiredWarningCue = FGameplayTag::RequestGameplayTag(TEXT("PresentationCue.Combat.BossTestCleave"));
	TestTrue(TEXT("B01 producer precedes melee in the selected attack"), Attack->GetWarningContractFailure().IsEmpty());
	const FName OriginalSection = Attack->MontageSectionName;
	Attack->MontageSectionName = TEXT("MissingEntry");
	TestFalse(TEXT("A warning elsewhere cannot satisfy a missing entry section"), Attack->GetWarningContractFailure().IsEmpty());
	Attack->MontageSectionName = OriginalSection;
	const float OriginalRate = Attack->Montage->RateScale;
	Attack->Montage->RateScale = -1.f;
	TestFalse(TEXT("Reverse playback cannot promise warning before damage"), Attack->GetWarningContractFailure().IsEmpty());
	Attack->Montage->RateScale = OriginalRate;
	const auto* ExistingWarning = Attack->Montage->Notifies.FindByPredicate([](const FAnimNotifyEvent& Notify)
	{
		const auto* Producer = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Notify.NotifyStateClass);
		return Producer && Producer->IsGameplayWarning();
	});
	if (!TestNotNull(TEXT("Authored notification to duplicate"), ExistingWarning)) return false;
	const FAnimNotifyEvent Copy = *ExistingWarning;
	Attack->Montage->Notifies.Add(Copy);
	TestFalse(TEXT("Duplicate warning fails the selected attack"), Attack->GetWarningContractFailure().IsEmpty());
	Attack->Montage->Notifies.Pop();
	Attack->Montage->Notifies.RemoveAll([](const FAnimNotifyEvent& Notify)
	{
		const auto* Producer = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Notify.NotifyStateClass);
		return Producer && Producer->IsGameplayWarning();
	});
	TestFalse(TEXT("Removing the entire producer fails the attack contract"), Attack->GetWarningContractFailure().IsEmpty());
	Attack->WarningPolicy = EProject_JAttackWarningPolicy::Legacy;
	TestTrue(TEXT("Unmigrated attacks retain their execution contract"), Attack->GetWarningContractFailure().IsEmpty());
	return true;
}
#endif
