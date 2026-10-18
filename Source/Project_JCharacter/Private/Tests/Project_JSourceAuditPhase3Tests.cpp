#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Project_JNPCCharacter.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Tests/Project_JSourceAuditPhase3Receiver.h"
#include "Mount/Project_JMountComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimInstance.h"
#include "Engine/SkeletalMesh.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"

namespace {
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FWorld {
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FWorld(bool Begun=false) {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        if (Begun) { World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted(); }
        ++GFrameCounter; World->GetTimerManager().Tick(0.f);
    }
    ~FWorld() { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
UProject_JAbilitySystemComponent* InstallASC(AProject_JPlayerMaturityASCFixture* Rider) {
    auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Rider); ASC->RegisterComponent();
    ASC->InitAbilityActorInfo(Rider,Rider); Rider->CurrentASC=ASC; return ASC;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3DismountPossessionTest,"ProjectJ.SourceAudit3.DismountPossession",Flags)
bool FProjectJAudit3DismountPossessionTest::RunTest(const FString&) {
    for (bool TagCallback : {false,true}) {
        FWorld Scope; auto* W=Scope.World;
        auto* Rider=W->SpawnActor<AProject_JPlayerMaturityASCFixture>();
        auto* Other=W->SpawnActor<APawn>(); auto* Controller=W->SpawnActor<APlayerController>(); Controller->Possess(Rider);
        auto* ASC=InstallASC(Rider);
        auto* Component=NewObject<UProject_JMountComponent>(Rider); Component->RegisterComponent();
        auto* Mount=W->SpawnActor<AProject_JMountLifecycleFixture>();
        if (!TestTrue(TEXT("Real mount transfers possession"),Component->RequestMount(Mount))) return false;
        bool Called=false; auto TakeOther=[&]{if(!Called){Called=true;Controller->Possess(Other);}};
        auto* Receiver=NewObject<UProject_JSourceAuditPhase3Receiver>(Rider);
        Receiver->OnDismounted=TakeOther;
        FDelegateHandle TagHandle;
        const auto Tag=FProject_JGameplayTags::Get().State_Mounted;
        if (TagCallback) TagHandle=ASC->RegisterGameplayTagEvent(Tag,EGameplayTagEventType::NewOrRemoved).AddLambda([&](FGameplayTag,int32 Count){if(!Count)TakeOther();});
        else Component->OnMountChanged.AddDynamic(Receiver,&UProject_JSourceAuditPhase3Receiver::MountChanged);
        TestTrue(TEXT("Dismount cleanup succeeds"),Mount->DismountRider(true));
        ASC->RegisterGameplayTagEvent(Tag,EGameplayTagEventType::NewOrRemoved).Remove(TagHandle);
        Component->OnMountChanged.RemoveAll(Receiver);
        TestTrue(TEXT("Actual synchronous dismount callback selected another pawn"),Called);
        TestTrue(TEXT("Older dismount cannot overwrite latest possession"),Controller->GetPawn()==Other);
        TestFalse(TEXT("Previous rider relationship still cleans up"),Component->IsMounted());
        TestEqual(TEXT("Previous mounted contribution cleans up"),ASC->GetTagCount(Tag),0);
    } return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3BegunMountDestroyTest,"ProjectJ.SourceAudit3.BegunMountComponentDestroy",Flags)
bool FProjectJAudit3BegunMountDestroyTest::RunTest(const FString&) {
    FWorld Scope(true); auto* W=Scope.World;
    auto* Rider=W->SpawnActor<AProject_JPlayerMaturityASCFixture>();
    auto* Controller=W->SpawnActor<APlayerController>(); Controller->Possess(Rider); auto* ASC=InstallASC(Rider);
    auto* Component=NewObject<UProject_JMountComponent>(Rider); Component->RegisterComponent();
    auto* Mount=W->SpawnActor<AProject_JMountLifecycleFixture>();
    if(!TestTrue(TEXT("Component actually began play"),Component->HasBegunPlay()) || !TestTrue(TEXT("Mount succeeded"),Component->RequestMount(Mount)))return false;
    Component->DestroyComponent();
    TestFalse(TEXT("Destroyed component clears rider relation"),Component->IsMounted());
    TestEqual(TEXT("Destroyed component releases tag"),ASC->GetTagCount(FProject_JGameplayTags::Get().State_Mounted),0);
    TestTrue(TEXT("Live rider regains possession when its component alone is destroyed"),Controller->GetPawn()==Rider);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3MontageRateTest,"ProjectJ.SourceAudit3.SharedMontageRate",Flags)
bool FProjectJAudit3MontageRateTest::RunTest(const FString&) {
    FWorld Scope; auto* W=Scope.World; auto* Player=W->SpawnActor<AProject_JGreatswordCharacter>();
    auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Player);ASC->RegisterComponent();ASC->InitAbilityActorInfo(Player,Player);
    FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(),TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player,ASC);
    auto* Source=LoadObject<UProject_JCombatStyleDefinition>(nullptr,TEXT("/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword.DA_CombatStyle_Greatsword"));
    if(!TestNotNull(TEXT("Authored style fixture"),Source))return false;
    auto* Style=DuplicateObject(Source,Player); Style->AbilitySets.Reset();Style->CommandSet=nullptr;
    Style->ComboDefinition=DuplicateObject(Source->ComboDefinition.Get(),Style);
    auto& First=Style->ComboDefinition->Nodes[0]; auto Second=First;
    auto* Attack=DuplicateObject(First.AttackDefinition.Get(),Style);
    auto* Montage=DuplicateObject(Attack->Montage.Get(),Style);
    Montage->AddAnimCompositeSection(TEXT("AuditSlow"),0.f); Montage->AddAnimCompositeSection(TEXT("AuditFast"),.3f);
    Attack->Montage=Montage;Attack->MontageSectionName=TEXT("AuditSlow");Attack->PlayRate=1.f; First.AttackDefinition=Attack;
    Second.AttackDefinition=DuplicateObject(Attack,Style);Second.AttackDefinition->MontageSectionName=TEXT("AuditFast");Second.AttackDefinition->PlayRate=2.f;
    auto* Equipment=NewObject<UProject_JEquipmentManagerComponent>(Player);Equipment->RegisterComponent();
    Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>()->BindToEquipmentManager(Equipment);
    auto* Item=NewObject<UProject_JEquipmentItemDefinition>(Player);Item->EquipmentSlot=EProject_JEquipmentSlot::Weapon;Item->CombatStyleDefinition=Style;Equipment->EquipItem(Item);
    Player->GetMesh()->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
    Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
    const auto InputTag=First.StartInputTags.First();ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
    FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Melee::StaticClass());Spec.GetDynamicSpecSourceTags().AddTag(InputTag);const auto Handle=ASC->GiveAbility(Spec);
    auto* Input=Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>();Input->Initialize(Player);Input->HandleInputTagPressed(InputTag);
    auto* Ability=Cast<UProject_JGameplayAbility_Melee>(ASC->FindAbilitySpecFromHandle(Handle)->GetPrimaryInstance());
    if(!TestNotNull(TEXT("Real GAS montage activation"),Ability) || !TestTrue(TEXT("Ability is active"),Ability->IsActive()))return false;
    auto* Task=Ability->MontageTask; Ability->StartComboNode(Second);
    TestTrue(TEXT("Existing task/notify ownership is preserved"),Ability->MontageTask==Task);
    TestEqual(TEXT("Reused montage adopts next node rate"),Player->GetMesh()->GetAnimInstance()->Montage_GetPlayRate(Montage),2.f);
    Input->HandleInputTagReleased(InputTag);ASC->CancelAllAbilities();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3BudgetAuthoringTest,"ProjectJ.SourceAudit3.BudgetAuthoringContract",Flags)
bool FProjectJAudit3BudgetAuthoringTest::RunTest(const FString&) {
    auto* Profile=NewObject<UProject_JLocomotionProfile>();
    TestFalse(TEXT("Ineffective nested budget must not be editable"),FindFProperty<FProperty>(Profile->GetClass(),TEXT("AnimationBudget"))->HasAnyPropertyFlags(CPF_Edit));
    Profile->AnimationBudget.NearDistance=99999.f;Profile->NearMotionMatchingDistance=1234.f;
    Profile->MidMotionMatchingDistance=5678.f;Profile->FarMotionMatchingDistance=9000.f;
    Profile->MidMotionMatchingUpdateInterval=.04f;Profile->FarMotionMatchingUpdateInterval=.12f;Profile->AnimInstanceHiddenRemoteUpdateInterval=.2f;Profile->bDisableMotionMatchingBeyondFarDistance=true;
    auto* Copy=DuplicateObject(Profile,GetTransientPackage());const auto R=Copy->GetResolvedAnimationBudgetSettings();
    TestEqual(TEXT("Saved flat near precedence is preserved"),R.NearDistance,1234.f);TestEqual(TEXT("Mid authoring is effective"),R.MidDistance,5678.f);TestEqual(TEXT("Far authoring is effective"),R.FarDistance,9000.f);
    TestEqual(TEXT("Mid interval"),R.MidUpdateInterval,.04f);TestEqual(TEXT("Far interval"),R.FarUpdateInterval,.12f);TestEqual(TEXT("Hidden interval"),R.HiddenUpdateInterval,.2f);TestTrue(TEXT("Far disable"),R.bDisableMotionMatchingBeyondFarDistance);
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3MovementOwnerTest,"ProjectJ.SourceAudit3.MovementOwnerAndTimers",Flags)
bool FProjectJAudit3MovementOwnerTest::RunTest(const FString&) {
    for(int32 Mode=0;Mode<3;++Mode){
        FWorld Scope;auto* W=Scope.World;auto* Player=W->SpawnActor<AProject_JGreatswordCharacter>();auto* Next=W->SpawnActor<AProject_JGreatswordCharacter>();
        auto* ASC=NewObject<UProject_JAbilitySystemComponent>(Player);ASC->RegisterComponent();ASC->InitAbilityActorInfo(Player,Player);
        FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(),TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player,ASC);
        auto* State=Player->FindComponentByClass<UProject_JLocomotionAnimStateComponent>();State->RefreshCachedReferences();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);State->StartFallOffStart(false);
        const auto Tag=FProject_JGameplayTags::Get().State_Movement_InAir;
        TestEqual(TEXT("Fall-off owns one local tag contribution"),ASC->GetTagCount(Tag),1);
        if(Mode==0){ASC->InitAbilityActorInfo(Player,Next);State->AddOwnedInAirGameplayTag();TestEqual(TEXT("Retained old owner cannot maintain a tag on reassigned ASC"),ASC->GetTagCount(Tag),0);}
        if(Mode==1){State->HandleReplicatedLandingStarted(1,0.f,100.f,false,false,false);State->FinishLanding(true);TestEqual(TEXT("Remote landing releases its fall-off tag demand"),ASC->GetTagCount(Tag),0);}
        State->DestroyComponent();TestEqual(TEXT("Retirement releases its contribution"),ASC->GetTagCount(Tag),0);
        ASC->AddLooseGameplayTag(Tag);++GFrameCounter;W->GetTimerManager().Tick(6.f);
        TestEqual(TEXT("Retired timers cannot modify a subsequent owner's contribution"),ASC->GetTagCount(Tag),1);ASC->RemoveLooseGameplayTag(Tag);
    }return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3CueDeltaRecoveryTest,"ProjectJ.SourceAudit3.CueDeltaRecovery",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAudit3CueDeltaRecoveryTest::RunTest(const FString&) {
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);
    auto* NPC=W->SpawnActor<AProject_JNPCCharacter>();
    auto* C=NewObject<UProject_JCombatPresentationComponent>(NPC); C->RegisterComponent();
    NPC->SetRole(ROLE_SimulatedProxy);
    const auto Attack=FProject_JGameplayTags::Get().InputTag_Skill_R;
    const auto Cue=FProject_JGameplayTags::Get().InputTag_Weapon_RMB;
    FProject_JReplicatedCombatPresentationState Snapshot;
    Snapshot.ActiveAttackTag=Attack;Snapshot.AttackInstance=1;Snapshot.Revision=3;Snapshot.EventOrder=4;
    C->MulticastPlayPresentationCue_Implementation(Attack,Cue,true,5,1,Snapshot);
    C->ReplicatedPresentationState=Snapshot;
    C->ApplyReplicatedState();
    TestEqual(TEXT("Later cue delta cannot suppress a useful complete snapshot"),C->LastAppliedPresentationRevision,3);
    Snapshot.Revision=4;Snapshot.EventOrder=7;
    C->MulticastPlayPresentationCue_Implementation(Attack,Cue,false,8,1,Snapshot);
    C->ReplicatedPresentationState=Snapshot;
    C->ApplyReplicatedState();
    TestEqual(TEXT("Stop delta cannot suppress other loops' complete snapshot"),C->LastAppliedPresentationRevision,4);
    C->MulticastEndAttackPresentation_Implementation(10);
    C->ReplicatedPresentationState.Revision=5;C->ReplicatedPresentationState.EventOrder=9;C->ReplicatedPresentationState.ActiveAttackTag=Attack;
    C->ApplyReplicatedState();TestFalse(TEXT("Completed attack cannot be resurrected by older recovery"),C->GetActiveAttackTag().IsValid());
    W->DestroyWorld(false);return true;
}
#endif
