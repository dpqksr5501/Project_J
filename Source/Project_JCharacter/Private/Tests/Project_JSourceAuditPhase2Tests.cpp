#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
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
    FPhase2World()
    {
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
        ++GFrameCounter; World->GetTimerManager().Tick(0.f);
    }
    ~FPhase2World() { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
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
        FPhase2World Scope;
        auto* Player = Scope.World->SpawnActor<AProject_JGreatswordCharacter>();
        auto* State = Player->FindComponentByClass<UProject_JLocomotionAnimStateComponent>();
        Player->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
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

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit2AssetUsageTest, "ProjectJ.SourceAudit2.ReadOnlyAssetUsage", Phase2Flags)
bool FProjectJAudit2AssetUsageTest::RunTest(const FString&)
{
    UClass* Class = LoadClass<AProject_JGreatswordCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
    if (!TestNotNull(TEXT("Production character Blueprint"), Class)) return false;
    if (Class)
    {
        const auto* Player = CastChecked<AProject_JPlayerCharacter>(Class->GetDefaultObject());
        const auto* Profile = Player->GetCharacterAnimProfile();
        AddInfo(FString::Printf(TEXT("ProductionBP=%s Profile=%s Locomotion=%s AirReselection=%d OneShot=%d Chooser=%s"), *Class->GetPathName(), *GetPathNameSafe(Profile),
            *GetPathNameSafe(Profile ? Profile->LocomotionProfile.Get() : nullptr),
            Profile && Profile->LocomotionProfile ? int32(Profile->LocomotionProfile->MotionMatchingSearchPolicy.bEnableAirJumpReselection) : -1,
            Profile && Profile->LocomotionProfile ? int32(Profile->LocomotionProfile->MotionMatchingSearchPolicy.bEnableExperimentalOneShotPresentation) : -1,
            *GetPathNameSafe(Profile && Profile->LocomotionProfile ? Profile->LocomotionProfile->MotionMatchingSearchPolicy.StateControllerAnimationChooserTable.Get() : nullptr)));
    }
    auto* Style = LoadObject<UProject_JCombatStyleDefinition>(nullptr, TEXT("/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword.DA_CombatStyle_Greatsword"));
    if (!TestNotNull(TEXT("Production combat style"), Style) || !TestNotNull(TEXT("Production combo"), Style->ComboDefinition.Get())) return false;
    for (const auto& Node : Style->ComboDefinition->Nodes)
    {
        const auto* Attack = Node.AttackDefinition.Get();
        AddInfo(FString::Printf(TEXT("Node=%s Attack=%s Montage=%s Section=%s Rate=%.3f"), *Node.NodeTag.ToString(), *GetPathNameSafe(Attack),
            *GetPathNameSafe(Attack ? Attack->Montage.Get() : nullptr), Attack ? *Attack->MontageSectionName.ToString() : TEXT("None"), Attack ? Attack->PlayRate : 0));
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit2AirReselectionTest, "ProjectJ.SourceAudit2.AirReselectionCommand", Phase2Flags)
bool FProjectJAudit2AirReselectionTest::RunTest(const FString&)
{
    for (float EntryTime : {0.f, .2f})
    {
        FPhase2World Scope;
        auto* Player = Scope.World->SpawnActor<AProject_JGreatswordCharacter>();
        auto* Profile = NewObject<UProject_JCharacterAnimProfile>(Player);
        Profile->LocomotionProfile = NewObject<UProject_JLocomotionProfile>(Profile);
        FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), TEXT("CharacterAnimProfile"))->SetObjectPropertyValue_InContainer(Player, Profile);
        auto* Clip = NewObject<UAnimSequence>(Player);
        auto* Mesh = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
        if (!TestNotNull(TEXT("Transient clip skeleton source"), Mesh)) return false;
        Clip->SetSkeleton(Mesh->GetSkeleton());
        auto& Controller = Clip->GetController(); Controller.InitializeModel();
        Controller.SetFrameRate(FFrameRate(30, 1), false); Controller.SetNumberOfFrames(FFrameNumber(30), false);
        TestEqual(TEXT("Transient timing fixture is exactly one second"), Clip->GetPlayLength(), 1.f);
        auto* Chooser = NewObject<UChooserTable>(Profile);
        Chooser->FallbackResult.InitializeAs<FAssetChooser>(); Chooser->FallbackResult.GetMutable<FAssetChooser>().Asset = Clip;
        auto& Policy = Profile->LocomotionProfile->MotionMatchingSearchPolicy;
        Policy.StateControllerAnimationChooserTable = Chooser; Policy.bEnableExperimentalOneShotPresentation = true;
        Policy.bEnableAirJumpReselection = true; Policy.bUseMotionMatchForExperimentalOneShotEntry = false;
        auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Player->GetMesh());
        Anim->OwningCharacter = Player; Anim->OwningPlayerCharacter = Player;
        auto* State = Player->FindComponentByClass<UProject_JLocomotionAnimStateComponent>();
        Anim->LocomotionAnimStateComponent = State;
        State->bIsJumping = true; State->bHasMoveInput = true;
        State->AuthoritativeContext.RotationMode = EProject_JLocomotionRotationMode::OrientToMovement;
        State->AuthoritativeContext.GaitIntent = EProject_JLocomotionGaitIntent::Run;
        State->DerivedLocomotionContext.PhaseFamily = EProject_JLocomotionPhaseFamily::JumpStart;
        State->KinematicContext.bHasFutureTrajectoryVelocity = true;
        State->KinematicContext.FutureTrajectoryVelocity = FVector(0, -300, 0);
        FProject_JAnimThreadSafeData Data; Data.MotionMatchingSearchPolicy = Policy; Data.Air.bIsInAir = true;
        Data.Movement.Velocity = FVector(300, 0, 0).RotateAngleAxis(40.f, FVector::UpVector); Data.Movement.GroundSpeed = 300;
        Anim->FillLocomotionStateThreadSafeData(Data);
        TestTrue(TEXT("Future trajectory deliberately disagrees with current air velocity"), Data.Movement.FutureTrajectoryVelocity.Y < 0 && Data.Movement.Velocity.Y > 0);
        TestEqual(TEXT("OTM launch snapshot uses its authored 45 degree forward sector"), Data.OneShotPresentation.StrafeDirection, EProject_JStateControllerStrafeDirection::Forward);
        Anim->StateControllerRuntime.BeginHold(EProject_JStateControllerPresentationState::TransitionToInAir, 0.);
        Anim->CachedStateControllerSelectedAnimation = Clip;
        Anim->CachedStateControllerSelectedAnimationOutput.StartTime = EntryTime;
        Anim->CachedStateControllerPresentationState = EProject_JStateControllerPresentationState::TransitionToInAir;
        Anim->CachedStateControllerStrafeDirection = EProject_JStateControllerStrafeDirection::Forward;
        Anim->bCachedStateControllerHasSelectedAnimation = true;
        Anim->CachedStateControllerChooserTable = Chooser;
        Anim->StateControllerPresentationStateForChooser = EProject_JStateControllerPresentationState::TransitionToInAir;
        Anim->AnimationClock.Advance(.4f, false);
        Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
        TestEqual(TEXT("Approved live air direction reaches the same command snapshot"), Data.OneShotPresentation.StrafeDirection, EProject_JStateControllerStrafeDirection::ForwardRight);
        TestTrue(TEXT("Approved direction has its live angle"), Data.OneShotPresentation.bHasStrafeDirectionAngle);
        TestNearlyEqual(TEXT("Approved live angle"), Data.OneShotPresentation.StrafeDirectionAngle, 40.f);
        Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
        TestEqual(TEXT("Same-frame chooser reads the approved direction"), Anim->StateControllerStrafeDirectionForChooser, EProject_JStateControllerStrafeDirection::ForwardRight);
        TestNearlyEqual(TEXT("Reselection preserves absolute asset time, including nonzero entry"), Data.OneShotPresentation.SelectedAnimationOutput.StartTime, EntryTime + .4f);
        const int32 Revision = Anim->StateControllerChooserSelectionRevision;
        Anim->AnimationClock.Advance(.01f, false);
        Anim->FillLocomotionStateThreadSafeData(Data);
        Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
        TestEqual(TEXT("Next snapshot preserves the committed air sector"), Data.OneShotPresentation.StrafeDirection, EProject_JStateControllerStrafeDirection::ForwardRight);
        TestNearlyEqual(TEXT("Asset start offset is deducted exactly once"), Data.OneShotPresentation.TransitionTimeRemaining, 1.f - EntryTime - .4f - .01f);
        Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
        TestEqual(TEXT("Stable air direction does not emit another selection"), Anim->StateControllerChooserSelectionRevision, Revision);
        const float AdditionalWait = Data.OneShotPresentation.SelectedAnimationOutput.BlendTime * Policy.AirJumpMinBlendProgressRatio + .01f;
        Anim->AnimationClock.Advance(AdditionalWait, false);
        Data.Movement.Velocity = FVector(300, 0, 0).RotateAngleAxis(-40.f, FVector::UpVector);
        Anim->FillLocomotionStateThreadSafeData(Data);
        Anim->ResolveStateControllerPresentationStateWithPlaybackHold(Data, Data.OneShotPresentation);
        TestEqual(TEXT("A later meaningful direction change is accepted after blend cooldown"), Data.OneShotPresentation.StrafeDirection, EProject_JStateControllerStrafeDirection::ForwardLeft);
        Anim->EvaluateStateControllerAnimationChooserOnGameThread(Data);
        TestNearlyEqual(TEXT("Repeated reselection keeps cumulative asset progress"), Data.OneShotPresentation.SelectedAnimationOutput.StartTime, EntryTime + .41f + AdditionalWait);
        TestEqual(TEXT("One command per meaningful direction change"), Anim->StateControllerChooserSelectionRevision, Revision + 1);
    }
    return true;
}
#endif
