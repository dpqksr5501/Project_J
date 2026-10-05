#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JAttributeSet.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJResourceInvariantTest, "ProjectJ.Maturity.GAS.ResourceInvariants",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJResourceInvariantTest::RunTest(const FString&)
{
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
	auto* Owner = World->SpawnActor<AActor>();
	auto* ASC = NewObject<UAbilitySystemComponent>(Owner); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Owner, Owner);
	auto* Stats = NewObject<UProject_JAttributeSet>(Owner); ASC->AddAttributeSetSubobject(Stats);
	Stats->SetMaxHealth(100); Stats->SetHealth(90); Stats->SetMaxMana(50); Stats->SetMana(40);
	auto* Bonus = NewObject<UGameplayEffect>(); Bonus->DurationPolicy = EGameplayEffectDurationType::Infinite;
	for (const FGameplayAttribute& Maximum : {Stats->GetMaxHealthAttribute(), Stats->GetMaxManaAttribute()})
	{
		auto& Modifier = Bonus->Modifiers.AddDefaulted_GetRef(); Modifier.Attribute = Maximum;
		Modifier.ModifierOp = EGameplayModOp::Additive; Modifier.ModifierMagnitude = FScalableFloat(50);
	}
	const auto Handle = ASC->ApplyGameplayEffectToSelf(Bonus, 1, ASC->MakeEffectContext());
	TestTrue(TEXT("Infinite maximum bonus is active"), Handle.IsValid());
	Stats->SetHealth(140); Stats->SetMana(90);
	ASC->RemoveActiveGameplayEffect(Handle);
	TestEqual(TEXT("Removing a maximum buff clamps health without Execute"), Stats->GetHealth(), 100.0f);
	TestEqual(TEXT("Removing a maximum buff clamps mana without Execute"), Stats->GetMana(), 50.0f);
	Stats->SetMaxHealth(200);
	TestEqual(TEXT("Increasing maximum does not grant a free heal"), Stats->GetHealth(), 100.0f);
	Stats->SetMaxHealth(-10); Stats->SetMaxMana(-10);
	TestEqual(TEXT("Health maximum is nonnegative"), Stats->GetMaxHealth(), 0.0f);
	TestEqual(TEXT("Current health follows a zero maximum"), Stats->GetHealth(), 0.0f);
	TestEqual(TEXT("Current mana follows a zero maximum"), Stats->GetMana(), 0.0f);
	float Invalid = std::numeric_limits<float>::quiet_NaN(); Stats->PreAttributeBaseChange(Stats->GetMaxHealthAttribute(), Invalid);
	TestEqual(TEXT("Nonfinite base values are sanitized"), Invalid, 0.0f);
	return true;
}
#endif
