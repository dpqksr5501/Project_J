#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Project_JNPCCharacter.h"
#include "AbilitySystemComponent.h"
#include "GameplayEffect.h"
#include "Engine/World.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(AuthorityOriginAttack, "ProjectJ.Tests.AuthorityHit.Origin");
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthorityHitOriginTest, "ProjectJ.Combat.AuthorityHitWorldOrigin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthorityHitOriginTest::RunTest(const FString&)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Attacker = World->SpawnActor<AProject_JNPCCharacter>(Params);
	auto* Target = World->SpawnActor<AProject_JNPCCharacter>(Params);
	Attacker->SetActorEnableCollision(false); Target->SetActorEnableCollision(false);
	Attacker->GetAbilitySystemComponent()->InitAbilityActorInfo(Attacker, Attacker);
	Target->GetAbilitySystemComponent()->InitAbilityActorInfo(Target, Target);
	auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(Attacker); Hit->RegisterComponent();
	auto* Definition = NewObject<UProject_JAttackDefinition>(Hit);
	Definition->AttackTag = AuthorityOriginAttack;
	// Empty instant effect still traverses the real authority outgoing-spec/apply path.
	Definition->DamageEffect = UGameplayEffect::StaticClass();
	int32 Applied = 0;
	const auto Handle = Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
		[&Applied](UAbilitySystemComponent*, const FGameplayEffectSpec&, FActiveGameplayEffectHandle) { ++Applied; });
	for (const FVector Origin : { FVector::ZeroVector, FVector(10000, -20000, 1000), FVector(-10000, 20000, -1000) })
	{
		Attacker->SetActorLocation(Origin); Target->SetActorLocation(Origin + FVector(100, 0, 0));
		Hit->BeginAttackNode(AuthorityOriginAttack, Definition); Hit->SetHitWindowOpen(true);
		TestFalse(TEXT("No recorded authority trace rejects"), Hit->ProcessAuthorityHit(Target));
		Hit->RecordAuthoritativeTrace(Origin + FVector(1000, 0, 0), Origin + FVector(1100, 0, 0));
		TestFalse(TEXT("A recorded trace too far from attacker still rejects"), Hit->ProcessAuthorityHit(Target));
		Hit->RecordAuthoritativeTrace(Origin + FVector(20, 0, 0), Origin + FVector(100, 0, 0));
		TestFalse(TEXT("Self hit rejects"), Hit->ProcessAuthorityHit(Attacker));
		TestTrue(TEXT("Nearby authority hit works at any world origin"), Hit->ProcessAuthorityHit(Target));
		TestFalse(TEXT("Duplicate authority hit rejects"), Hit->ProcessAuthorityHit(Target));
		Hit->EndAttack();
		TestFalse(TEXT("Late authority hit after end rejects"), Hit->ProcessAuthorityHit(Target));
	}
	TestEqual(TEXT("Exactly one real effect application per swing"), Applied, 3);
	Target->GetAbilitySystemComponent()->OnGameplayEffectAppliedDelegateToSelf.Remove(Handle);
	World->DestroyWorld(false);
	return true;
}
#endif
