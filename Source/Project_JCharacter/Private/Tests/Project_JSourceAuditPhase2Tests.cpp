#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Tests/Project_JMoverFrameFixture.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h"
#include "UObject/UnrealType.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "ObjectChooser_Asset.h"
#include "Chooser.h"
#include "Animation/Project_JCharacterAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"

namespace
{
constexpr auto Phase2Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
struct FPhase2World
{
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
    FPhase2World(bool bBegun=false)
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        if(bBegun) { World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted(); }
        ++GFrameCounter; World->GetTimerManager().Tick(0.f);
    }
    ~FPhase2World() { World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit2RejectedStartTest, "ProjectJ.SourceAudit2.RejectedComboStart", Phase2Flags)
bool FProjectJAudit2RejectedStartTest::RunTest(const FString&)
{
    for (bool bTransitionOnly : {false, true})
    {
        FPhase2World Scope;
        auto* Player = Scope.World->SpawnActor<AProject_JGreatswordCharacter>();
        auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player); ASC->RegisterComponent(); ASC->InitAbilityActorInfo(Player, Player);
        FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"))->SetObjectPropertyValue_InContainer(Player, ASC);
        auto* Source = LoadObject<UProject_JCombatStyleDefinition>(nullptr, TEXT("/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword.DA_CombatStyle_Greatsword"));
        if (!TestNotNull(TEXT("Production style fixture"), Source)) return false;
        auto* Style = DuplicateObject(Source, Player); Style->AbilitySets.Reset(); Style->CommandSet = nullptr;
        Style->ComboDefinition = DuplicateObject(Source->ComboDefinition.Get(), Style);
        const auto& Tags = FProject_JGameplayTags::Get();
        for (auto& Node : Style->ComboDefinition->Nodes)
        {
            if (bTransitionOnly) Node.StartInputTags.Reset();
            else Node.RequiredOwnerTags.AddTag(Tags.State_Mounted);
        }
        if (bTransitionOnly)
        {
            auto& Node = Style->ComboDefinition->Nodes[0];
            Node.StartInputTags.AddTag(Tags.InputTag_Weapon_LMB);
            FProject_JComboTransition Edge; Edge.InputTag = Tags.InputTag_Skill_Q; Edge.TargetNodeTag = Node.NodeTag;
            Node.Transitions.Add(Edge);
        }
        auto* Equipment = NewObject<UProject_JEquipmentManagerComponent>(Player); Equipment->RegisterComponent();
        Player->FindComponentByClass<UProject_JEquipmentRuntimeComponent>()->BindToEquipmentManager(Equipment);
        auto* Item = NewObject<UProject_JEquipmentItemDefinition>(Player); Item->EquipmentSlot = EProject_JEquipmentSlot::Weapon; Item->CombatStyleDefinition = Style;
        Equipment->EquipItem(Item);
        Player->GetMesh()->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
        Player->GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
        ASC->AddLooseGameplayTag(Tags.State_CombatMode);
        const auto InputTag = bTransitionOnly ? Tags.InputTag_Skill_Q : Tags.InputTag_Weapon_LMB;
        FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Melee::StaticClass()); Spec.GetDynamicSpecSourceTags().AddTag(InputTag);
        const auto Handle = ASC->GiveAbility(Spec);
        auto* Input = Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>(); Input->Initialize(Player);
        Input->HandleInputTagPressed(InputTag); Input->HandleInputTagReleased(InputTag);
        TestFalse(TEXT("Rejected first input ends the actual GAS activation"), ASC->FindAbilitySpecFromHandle(Handle)->IsActive());
        TestFalse(TEXT("Rejected first input leaves no attacking tag"), ASC->HasMatchingGameplayTag(Tags.State_Attacking));
        TestFalse(TEXT("Rejected first input plays no montage"), Player->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying());
        TestNull(TEXT("Rejected first input leaves no hit definition"), Player->FindComponentByClass<UProject_JCombatHitValidationComponent>()->GetActiveAttackDefinition());
        TestFalse(TEXT("Rejected first input leaves no presentation"), Player->FindComponentByClass<UProject_JCombatPresentationComponent>()->GetActiveAttackTag().IsValid());
        ASC->CancelAllAbilities();
    }
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit2RemoteJumpTest, "ProjectJ.SourceAudit2.RemoteJumpDeadline", Phase2Flags)
bool FProjectJAudit2RemoteJumpTest::RunTest(const FString&)
{
    for (float Age : {0.f, .1f}) for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        FPhase2World Scope(true);
        auto* Player = Scope.World->SpawnActor<AProject_JGreatswordCharacter>();
        auto* State = Player->FindComponentByClass<UProject_JLocomotionAnimStateComponent>();
        TestTrue(TEXT("Mover falling fixture published"),ProjectJ::Tests::PublishMoverFrame(Player,MOVE_Falling));
        State->RefreshCachedReferences();
        if (Mode != 0)
        {
            State->VerticalSpeed = 400.f;
            TestTrue(TEXT("Movement evidence predicts the remote jump"), State->TryPredictRemoteJumpStart(true));
        }
        if (Mode != 1) TestTrue(TEXT("Remote confirmation accepted"), State->HandleConfirmedRemoteJump(1, Age, FVector(0, 0, 400)));
        else
        {
            State->AdvanceStateClocks(Age);
            ++GFrameCounter; Scope.World->GetTimerManager().Tick(Age);
        }
        const float Remaining = State->ReplicatedJumpStartDuration - Age;
        State->AdvanceStateClocks(Remaining + .01f);
        ++GFrameCounter; Scope.World->GetTimerManager().Tick(Remaining + .02f);
        TestFalse(TEXT("Remote timer ends JumpStart at its short deadline while airborne"), State->bIsJumping);
        TestTrue(TEXT("Remote timeout preserves airborne presentation"), State->bIsInAir);
    }
    FPhase2World Scope;
    auto* State = Scope.World->SpawnActor<AProject_JGreatswordCharacter>()->FindComponentByClass<UProject_JLocomotionAnimStateComponent>();
    State->BeginJumpStartState(); State->ScheduleJumpStartTimeout(State->JumpStartMaxDuration);
    State->AdvanceStateClocks(.4f); State->OnJumpTimerFinished();
    TestTrue(TEXT("Local watchdog retains its longer deadline"), State->bIsJumping);
    State->AdvanceStateClocks(2.f); ++GFrameCounter; Scope.World->GetTimerManager().Tick(2.1f);
    TestFalse(TEXT("Local watchdog still completes"), State->bIsJumping);
    TestTrue(TEXT("Late fresh remote confirmation is accepted"), State->HandleConfirmedRemoteJump(7, .8f, FVector(0, 0, 400)));
    TestFalse(TEXT("Late confirmation cannot restart expired JumpStart"), State->bIsJumping);
    return true;
}


// Current air selection and playback are covered by ProjectJ.PlayerLocomotion and the PIE fixtures.
#endif
