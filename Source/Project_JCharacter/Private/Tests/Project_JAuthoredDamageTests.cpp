#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "GameplayEffect.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuthoredDamageTest, "ProjectJ.Combat.AuthoredDamageContract",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAuthoredDamageTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto MakeActor = [&](const FVector& Position) {
        auto* Actor = World->SpawnActor<AProject_JPlayerMaturityASCFixture>(); Actor->SetActorLocation(Position);
        Actor->SetActorEnableCollision(false);
        auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Actor);
        Actor->CurrentASC = ASC; ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Actor, Actor);
        auto* Attributes = NewObject<UProject_JAttributeSet>(Actor); ASC->AddAttributeSetSubobject(Attributes);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 100);
        ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100);
        return Actor;
    };
    auto* Attacker = MakeActor(FVector::ZeroVector); auto* Target = MakeActor(FVector(100, 0, 0));
    auto* Hit = NewObject<UProject_JCombatHitValidationComponent>(Attacker); Hit->RegisterComponent();
    const auto* Reference = LoadObject<UProject_JAttackDefinition>(nullptr,
        TEXT("/Game/DataAssetSets/AttackDefinition/GreatSword/DA_Attack_Greatsword_LMB1.DA_Attack_Greatsword_LMB1"));
    if (!TestNotNull(TEXT("Existing working prototype attack"), Reference) || !Reference->DamageEffect) { return false; }
    const auto* Effect = Reference->DamageEffect->GetDefaultObject<UGameplayEffect>();
    float HealthDelta = 0;
    for (const auto& Modifier : Effect->Modifiers)
    {
        if (Modifier.Attribute == UProject_JAttributeSet::GetHealthAttribute() && Modifier.ModifierOp == EGameplayModOp::Additive)
        { float Magnitude = 0; if (Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(1.f, Magnitude)) { HealthDelta += Magnitude; } }
    }
    if (!TestTrue(TEXT("Working reference is instant and reduces health"), Effect->DurationPolicy == EGameplayEffectDurationType::Instant && HealthDelta < 0)) { return false; }
    for (const TCHAR* Name : {TEXT("LMB1"), TEXT("LMB2"), TEXT("LMBRMB"), TEXT("LR1"), TEXT("LR2"),
        TEXT("Q1"), TEXT("Q2"), TEXT("R"), TEXT("RMB1"), TEXT("RMB2"), TEXT("T")})
    {
        const FString Path = FString::Printf(TEXT("/Game/DataAssetSets/AttackDefinition/GreatSword/DA_Attack_Greatsword_%s.DA_Attack_Greatsword_%s"), Name, Name);
        auto* Definition = LoadObject<UProject_JAttackDefinition>(nullptr, *Path);
        if (!TestNotNull(Path, Definition)) { continue; }
        // Each definition must demonstrate damage against a living target; the
        // final attack must not pass merely because earlier hits reached zero.
        Target->CurrentASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 100);
        const float Before = Target->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
        Hit->BeginAttackNode(Definition->AttackTag, Definition); Hit->SetHitWindowOpen(true);
        Hit->RecordAuthoritativeTrace(FVector(20, 0, 0), FVector(100, 0, 0));
        TestTrue(FString::Printf(TEXT("%s server hit accepted"), Name), Hit->ProcessAuthorityHit(Target));
        TestEqual(FString::Printf(TEXT("%s applies the existing authored health damage"), Name),
            Target->CurrentASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), FMath::Max(0.f, Before + HealthDelta));
        TestFalse(TEXT("Damage does not grant persistent combat mode to the victim"), Target->CurrentASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode));
        TestFalse(TEXT("Same swing cannot damage twice"), Hit->ProcessAuthorityHit(Target)); Hit->EndAttack();
    }
    return true;
}
#endif
