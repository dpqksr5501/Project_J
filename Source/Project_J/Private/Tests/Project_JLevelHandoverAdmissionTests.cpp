#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "CharacterClass/Project_JProgressionComponent.h"
#include "CharacterClass/Project_JCharacterClassDefinition.h"
#include "AbilitySystem/Project_JAbilitySet.h"
#include "GameplayEffect.h"
#include "Serialization/MemoryWriter.h"
#include "Components/CapsuleComponent.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Project_JGameplayTags.h"
#include "UObject/GarbageCollection.h"

namespace ProjectJLevelAdmissionTests
{
struct FWorld
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorld()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        World->InitializeActorsForPlay(FURL());
    }
    ~FWorld() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
    AProject_JPlayerState* Owner()
    {
        auto* State = World->SpawnActor<AProject_JPlayerState>();
        State->GetProjectJAbilitySystemComponent()->InitAbilityActorInfo(State, State);
        return State;
    }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProgressionLevelAdmissionTest, "ProjectJ.Continuation.ProgressionLevelAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProgressionLevelAdmissionTest::RunTest(const FString&)
{
    ProjectJLevelAdmissionTests::FWorld F;
    auto* Class = NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId=TEXT("LevelAdmission"); Class->StartingLevel=101;
    auto* P = F.Owner()->FindComponentByClass<UProject_JProgressionComponent>();
    TestFalse(TEXT("Class starting above owner cap is rejected before publication"), P->InitializeClass(Class));
    TestEqual(TEXT("Rejected class leaves the initial revision"), P->GetState().Revision, 0);
    auto* Defaults = F.Owner()->FindComponentByClass<UProject_JProgressionComponent>();
    Defaults->InitializeDefaults(Class, nullptr, 1);
    TestEqual(TEXT("Default class floor above owner cap is not published"), Defaults->GetState().Revision, 0);
    auto* Level = F.Owner()->FindComponentByClass<UProject_JProgressionComponent>();
    Level->InitializeDefaults(nullptr, nullptr, 101);
    TestEqual(TEXT("Default level above owner cap is not published"), Level->GetState().Revision, 0);
    Class->StartingLevel=1;
    auto* Valid = F.Owner()->FindComponentByClass<UProject_JProgressionComponent>();
    TestTrue(TEXT("Existing valid class initializes"), Valid->InitializeClass(Class));
    const int32 Revision=Valid->GetState().Revision;
    Class->StartingLevel=101;
    TestFalse(TEXT("Effective class floor is rechecked at level mutation"), Valid->SetLevel(10));
    TestEqual(TEXT("Rejected effective level preserves the original level"), Valid->GetState().Level, 1);
    TestEqual(TEXT("Rejected effective level preserves the original revision"), Valid->GetState().Revision, Revision);
    auto* Boundary=F.Owner()->FindComponentByClass<UProject_JProgressionComponent>(); Boundary->MaximumLevel=5;
    Class->StartingLevel=3; Boundary->InitializeDefaults(Class,nullptr,1);
    TestEqual(TEXT("Admitted defaults retain the class floor"),Boundary->GetState().Level,3);
    TestTrue(TEXT("Scalar setter retains its below-floor normalization"),Boundary->SetLevel(1));
    TestEqual(TEXT("Below-floor scalar request stays at the floor"),Boundary->GetState().Level,3);
    TestTrue(TEXT("Exact owner cap is admitted"),Boundary->SetLevel(5));
    TestFalse(TEXT("One above custom owner cap is rejected"),Boundary->SetLevel(6));
    auto* Restored=F.Owner()->FindComponentByClass<UProject_JProgressionComponent>(); Restored->MaximumLevel=5;
    TestTrue(TEXT("Every admitted cap snapshot can restore on a matching owner"),Restored->RestoreSnapshot(Boundary->CaptureSnapshot(),Class,{}));
    auto* AdvancementOwner=F.Owner()->FindComponentByClass<UProject_JProgressionComponent>(); AdvancementOwner->MaximumLevel=5;
    AdvancementOwner->InitializeDefaults(nullptr,nullptr,1);
    auto* Advancement=NewObject<UProject_JCharacterAdvancementDefinition>(); Advancement->AdvancementId=TEXT("AdoptedFloor"); Advancement->BaseClass=Class;
    TestTrue(TEXT("Class-less advancement adopts its admitted base floor"),AdvancementOwner->ApplyAdvancement(Advancement));
    TestEqual(TEXT("Adopted class level satisfies its floor"),AdvancementOwner->GetState().Level,3);
    auto* Rejected=F.Owner()->FindComponentByClass<UProject_JProgressionComponent>(); Rejected->MaximumLevel=2;
    Rejected->InitializeDefaults(nullptr,nullptr,1);
    TestFalse(TEXT("Advancement cannot introduce an over-cap base class"),Rejected->ApplyAdvancement(Advancement));
    TestTrue(TEXT("Rejected advancement acquires no history"),Rejected->GetAcquiredAdvancements().IsEmpty());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerHandoverLevelAdmissionTest, "ProjectJ.Continuation.PlayerHandoverLevelAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerHandoverLevelAdmissionTest::RunTest(const FString&)
{
    ProjectJLevelAdmissionTests::FWorld F;
    auto* State=F.Owner();
    auto* P=State->FindComponentByClass<UProject_JProgressionComponent>(); P->InitializeDefaults(nullptr,nullptr,1);
    auto* Player=F.World->SpawnActor<AProject_JGreatswordCharacter>();
    if (!TestNotNull(TEXT("Concrete native player fixture"),Player)) return false;
    Player->SetPlayerState(State);
    if (!TestEqual(TEXT("Actual player resolves persistent level authority"),Player->GetProgressionComponent(),P)) return false;
    Player->RegisterAllComponents();
    Player->GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
    if (!TestTrue(TEXT("Fixture must exercise an actual successful teleport"),
        Player->SetActorLocationAndRotation(FVector(50,20,10),FRotator(0,15,0),false,nullptr,ETeleportType::TeleportPhysics))) return false;
    const FTransform Before=Player->GetActorTransform();
    const FVector Goal=Before.GetLocation()+FVector(500,200,100); const FRotator Rotation(0,60,0);
    TArray<uint8> Data; FMemoryWriter Writer(Data,true);
    FProject_JPlayerHandoverSnapshot Snapshot;
    Snapshot.Level=101; Snapshot.Location=Goal; Snapshot.Rotation=Rotation;
    Writer<<Snapshot.Version<<Snapshot.Level<<Snapshot.Location<<Snapshot.Rotation;
    Writer.Close();
    TestFalse(TEXT("Level rejected by persistent owner cannot publish a pose-only handover"),Player->TryApplyHandoverSnapshot(Data));
    TestTrue(TEXT("Rejected level leaves original pose"),Player->GetActorTransform().Equals(Before));
    TestEqual(TEXT("Rejected level leaves original persistent level"),P->GetState().Level,1);
    const auto Encode = [](FProject_JPlayerHandoverSnapshot Value)
    {
        TArray<uint8> Bytes; FMemoryWriter Archive(Bytes,true);
        Archive<<Value.Version<<Value.Level<<Value.Location<<Value.Rotation; Archive.Close(); return Bytes;
    };
    Snapshot.Level=2;
    TestTrue(TEXT("Admitted packet restores both exact level and pose"),Player->TryApplyHandoverSnapshot(Encode(Snapshot)));
    TestEqual(TEXT("Valid packet changes persistent authority"),P->GetState().Level,2);
    TestTrue(TEXT("Valid packet reaches requested position"),Player->GetActorLocation().Equals(Goal));
    auto* Class=NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId=TEXT("ExactFloor"); Class->StartingLevel=3;
    TestTrue(TEXT("Higher admitted class floor initializes"),P->InitializeClass(Class));
    const FTransform FloorPose=Player->GetActorTransform();
    Snapshot.Level=1; Snapshot.Location=Goal+FVector(100,0,0);
    TestFalse(TEXT("Exact packet below class floor cannot be silently normalized"),Player->TryApplyHandoverSnapshot(Encode(Snapshot)));
    TestTrue(TEXT("Below-floor packet leaves pose"),Player->GetActorTransform().Equals(FloorPose));
    auto* NextOwner=F.Owner(); auto* NextP=NextOwner->FindComponentByClass<UProject_JProgressionComponent>();
    NextP->InitializeDefaults(nullptr,nullptr,1);
    const auto Changed=P->OnChanged.AddLambda([&] { Player->SetPlayerState(NextOwner); CollectGarbage(RF_NoFlags); });
    Snapshot.Level=4;
    TestFalse(TEXT("Rebound owner during real publication cannot acknowledge old handover"),Player->TryApplyHandoverSnapshot(Encode(Snapshot)));
    P->OnChanged.Remove(Changed);
    TestEqual(TEXT("Checked setter never mutates the replacement owner"),NextP->GetState().Level,1);
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStyleEffectAdmissionTest, "ProjectJ.Continuation.StyleEffectAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStyleEffectAdmissionTest::RunTest(const FString&)
{
    auto* Style=NewObject<UProject_JCombatStyleDefinition>();
    Style->CombatStyleTag=FProject_JGameplayTags::Get().State_CombatMode;
    Style->bUsesCombo=false; Style->bRequiresWeaponAnimation=false;
    FProject_JAbilitySet_GameplayEffect Entry; Entry.Effect=UGameplayEffect::StaticClass(); Entry.EffectLevel=0.5f;
    Style->InlineEffects.Add(Entry);
    TArray<FText> Errors;
    TestFalse(TEXT("Compiled style admission agrees with GAS set level >= 1"),Style->ValidateDefinition(Errors));
    TestFalse(TEXT("An execution-invalid effect cannot mark compiled style ready"),Style->IsRuntimeReady());
    TestEqual(TEXT("Invalid style publishes no compiled grants"),Style->GetRuntimeAbilitySets().Num(),0);
    Style->InlineEffects[0].EffectLevel=1.5f;
    Style->InlineEffects[0].Effect=LoadClass<UGameplayEffect>(nullptr,TEXT("/Game/GEs/BP_GE_CombatMode.BP_GE_CombatMode_C"));
    Style->InvalidateRuntimeData();
    TestTrue(TEXT("Valid fractional GAS level remains admitted"),Style->ValidateDefinition(Errors));
    TestTrue(TEXT("Valid style becomes ready after explicit invalidation"),Style->IsRuntimeReady());
    ProjectJLevelAdmissionTests::FWorld F; auto* ASC=F.Owner()->GetProjectJAbilitySystemComponent();
    FProject_JAbilitySet_GrantedHandles Handles;
    for (const UProject_JAbilitySet* Set : Style->GetRuntimeAbilitySets()) { Set->GiveToAbilitySystem(ASC,&Handles); }
    if (TestEqual(TEXT("Compiled valid effect actually executes"),Handles.GameplayEffectHandles.Num(),1))
        TestEqual(TEXT("Runtime keeps 1.5 without integer clamping"),ASC->GetActiveGameplayEffect(Handles.GameplayEffectHandles[0])->Spec.GetLevel(),1.5f);
    GetDefault<UProject_JAbilitySet>()->TakeFromAbilitySystem(ASC,&Handles);
    Style->InlineEffects.Reset(); Style->AbilitySets.Add(NewObject<UProject_JAbilitySet>()); Style->InvalidateRuntimeData();
    TestTrue(TEXT("Legacy non-compiled readiness contract remains"),Style->IsRuntimeReady());
    return !HasAnyErrors();
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProgressionRestoreRetirementTest, "ProjectJ.Continuation.ProgressionRestoreRetirement",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProgressionRestoreRetirementTest::RunTest(const FString&)
{
    ProjectJLevelAdmissionTests::FWorld F;
    auto* State=F.Owner(); auto* ASC=State->GetProjectJAbilitySystemComponent();
    auto* P=State->FindComponentByClass<UProject_JProgressionComponent>();
    auto* Class=NewObject<UProject_JCharacterClassDefinition>(); Class->ClassId=TEXT("RetiredRestore");
    auto* Set=NewObject<UProject_JAbilitySet>(); FProject_JAbilitySet_GameplayEffect Entry;
    Entry.Effect=LoadClass<UGameplayEffect>(nullptr,TEXT("/Game/GEs/BP_GE_CombatMode.BP_GE_CombatMode_C"));
    if (!TestNotNull(TEXT("Actual GE callback fixture"),Entry.Effect.Get())) return false;
    Set->GrantedEffectEntries.Add(Entry); Class->AbilitySets.Add(Set);
    FProject_JProgressionSnapshot Snapshot; Snapshot.ClassId=Class->ClassId;
    const auto Callback=ASC->OnGameplayEffectAppliedDelegateToSelf.AddLambda(
        [&](UAbilitySystemComponent*,const FGameplayEffectSpec&,FActiveGameplayEffectHandle){P->DestroyComponent();CollectGarbage(RF_NoFlags);});
    const bool Restored=P->RestoreSnapshot(Snapshot,Class,{});
    ASC->OnGameplayEffectAppliedDelegateToSelf.Remove(Callback);
    TestFalse(TEXT("Restore retired by its real grant callback reports rejection"),Restored);
    TestEqual(TEXT("Retired restore returns the original owned effect"),ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),0);
    return !HasAnyErrors();
}
#endif
