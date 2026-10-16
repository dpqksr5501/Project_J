#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Components/Project_JAnimationUpdateCoordinatorComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "Equipment/Project_JWeaponMotionTypes.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Animation/Project_JAnimNotifyState_WeaponMotion.h"
#include "Animation/Project_JOneShotInputResponse.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Misc/DataValidation.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJVisualAssetAdmissions,
    "ProjectJ.Continuation.VisualAssetAdmissions", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJVisualAssetAdmissions::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Character=World->SpawnActor<AProject_JCombatFacingFixture>();
    auto* Source=Character->GetMesh();
    auto* Coordinator=Character->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
    auto* Quinn=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
    auto* Manny=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    if(!TestNotNull(TEXT("Quinn asset"),Quinn)||!TestNotNull(TEXT("Distinct Manny asset"),Manny)) return false;
    Source->SetSkeletalMeshAsset(Quinn);
    Source->AnimScriptInstance=NewObject<UProject_JCharacterAnimInstance>(Source);
    Source->GetAnimInstance()->NativeInitializeAnimation();
    auto MakePart=[&](USkeletalMesh* Asset)
    {
        auto* Part=NewObject<USkeletalMeshComponent>(Character);
        Part->SetupAttachment(Source); Part->SetSkeletalMeshAsset(Asset);
        Character->AddInstanceComponent(Part); Part->RegisterComponent();
        Part->AnimScriptInstance=NewObject<UAnimInstance>(Part);
        Part->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered;
        Part->SetLastRenderTime(0);
        return Part;
    };
    auto* Part=MakePart(Quinn);
    Coordinator->RefreshVisualBudget(.5f);
    TestTrue(TEXT("Generic animated part is eligible without asset-name or retarget-class matching"),Part->IsUsingExternalTickRateControl());
    Part->AnimScriptInstance=nullptr;
    Coordinator->RefreshVisualBudget(1.f/60);
    TestFalse(TEXT("Removing the last animation instance releases previous external ownership"),Part->IsUsingExternalTickRateControl());
    TestEqual(TEXT("Released part receives its original visibility policy"),Part->VisibilityBasedAnimTickOption,EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
    Part->SetSkeletalMeshAsset(Manny);
    Part->AnimScriptInstance=NewObject<UProject_JRetargetAnimInstance>(Part);
    Source->SetVisibility(false,false);
    Coordinator->RefreshVisualBudget(.5f);
    TestTrue(TEXT("Different mesh and animation class can acquire the existing coordinator"),Part->IsUsingExternalTickRateControl());
    TestFalse(TEXT("Visible render body is not pinned to an invisible pose source's LOD"),Part->bSyncAttachParentLOD);
    auto* Local=World->SpawnActor<APlayerController>(); Local->SetAsLocalPlayerController(); Local->Possess(Character);
    Coordinator->RefreshVisualBudget(1.f/60);
    TestTrue(TEXT("Local retains its original parent LOD policy"),Part->bSyncAttachParentLOD);
    TestEqual(TEXT("Local never overwrites forced render or streaming LOD policy"),Part->GetForcedLOD(),0);
    Local->UnPossess(); Coordinator->RefreshVisualBudget(1.f/60);
    TestEqual(TEXT("Remote body regains engine screen-size LOD after local ownership ends"),Part->GetForcedLOD(),0);
    Part->SetForcedLOD(2); Local->Possess(Character); Coordinator->RefreshVisualBudget(1.f/60);
    TestEqual(TEXT("Explicit authored forced LOD is preserved"),Part->GetForcedLOD(),2);
    Local->UnPossess(); Part->SetForcedLOD(0); Coordinator->RefreshVisualBudget(1.f/60);
    auto* AdditionalRetarget=MakePart(Quinn);
    AdditionalRetarget->AnimScriptInstance=NewObject<UProject_JRetargetAnimInstance>(AdditionalRetarget);
    TestNull(TEXT("Two implicit retarget parts cannot select an arbitrary body for hand/equipment consumers"),Project_J::Animation::FindVisualFollower(*Character));
    Part->ComponentTags.Add(TEXT("VisualFollower"));
    TestEqual(TEXT("Explicit body role disambiguates additional retargeted garments"),Project_J::Animation::FindVisualFollower(*Character),Part);
    Part->SetSkeletalMeshAsset(nullptr);
    TestNull(TEXT("A body waiting for its asset cannot transfer its role to a loaded retargeted garment"),Project_J::Animation::FindVisualFollower(*Character));
    TestEqual(TEXT("Role-only consumers still resolve the explicitly declared unloaded body"),Project_J::Animation::FindVisualFollower(*Character,false),Part);
    Part->SetSkeletalMeshAsset(Manny); Part->AnimScriptInstance=NewObject<UProject_JRetargetAnimInstance>(Part);
    AdditionalRetarget->ComponentTags.Add(TEXT("RetargetFollower"));
    TestNull(TEXT("Conflicting explicit body roles also fail closed"),Project_J::Animation::FindVisualFollower(*Character));
    AdditionalRetarget->DestroyComponent();
    auto* OtherOwner=World->SpawnActor<AActor>();
    auto* OtherPart=NewObject<USkeletalMeshComponent>(OtherOwner);
    OtherOwner->AddInstanceComponent(OtherPart); OtherPart->SetupAttachment(Source);
    OtherPart->ComponentTags.Add(TEXT("VisualFollower")); OtherPart->SetSkeletalMeshAsset(Quinn); OtherPart->RegisterComponent();
    TestEqual(TEXT("Another actor's attached mesh cannot claim this actor's body role"),Project_J::Animation::FindVisualFollower(*Character),Part);
    OtherPart->DestroyComponent(); OtherOwner->Destroy();
    auto* Placeholder=MakePart(Quinn); Placeholder->ComponentTags.Add(TEXT("VisualFollower")); Placeholder->UnregisterComponent();
    TestEqual(TEXT("Unregistered placeholder cannot claim the active body role"),Project_J::Animation::FindVisualFollower(*Character),Part);
    Placeholder->DestroyComponent();
    auto* Foreign=MakePart(Quinn);
    Foreign->EnableExternalTickRateControl(true); Foreign->SetExternalTickRate(9);
    Coordinator->RefreshVisualBudget(.5f);
    TestEqual(TEXT("Pre-existing external owner is not added to this group"),Coordinator->GetManagedVisualCount(),1);
    TestEqual(TEXT("Pre-existing external cadence is preserved"),int32(Foreign->GetExternalTickRate()),9);
    Part->GetAnimInstance()->RootMotionMode=ERootMotionMode::RootMotionFromEverything;
    Coordinator->RefreshVisualBudget(1.f/60);
    TestFalse(TEXT("Changed graph root-motion ownership releases the visual lease"),Part->IsUsingExternalTickRateControl());
    TestTrue(TEXT("Lease release restores the authored parent LOD synchronization"),Part->bSyncAttachParentLOD);
    Part->GetAnimInstance()->RootMotionMode=ERootMotionMode::RootMotionFromMontagesOnly;
    Coordinator->RefreshVisualBudget(.5f);
    TestTrue(TEXT("Eligible replacement graph can rejoin"),Part->IsUsingExternalTickRateControl());
    Part->SetLeaderPoseComponent(Source);
    Coordinator->RefreshVisualBudget(1.f/60);
    TestFalse(TEXT("Switching to engine LeaderPose releases independent graph budgeting"),Part->IsUsingExternalTickRateControl());
    Part->SetLeaderPoseComponent(nullptr);
    auto* Clothed=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/GreatSword/Clothes/cloak.cloak"));
    if(!TestNotNull(TEXT("Real mesh with independent cloth bindings"),Clothed)) return false;
    Part->SetSkeletalMeshAsset(Clothed);
    Part->AnimScriptInstance=NewObject<UProject_JRetargetAnimInstance>(Part);
    TestTrue(TEXT("Independent cloth bindings or resources are recognized"),Project_J::Animation::UsesIndependentClothSimulation(*Part));
    Coordinator->RefreshVisualBudget(.5f);
    TestFalse(TEXT("Cloth retains engine tick and visibility ownership"),Part->IsUsingExternalTickRateControl());
    Coordinator->bAllowVisualBudget=false;
    Coordinator->RefreshVisualBudget(1.f/60);
    TestTrue(TEXT("Disabling the project coordinator cannot release a foreign allocator"),Foreign->IsUsingExternalTickRateControl());
    TestTrue(TEXT("Restore reports ordinary full update permission"),Coordinator->DidRequestVisualEvaluation());
    Foreign->EnableExternalTickRateControl(false);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJFollowerPolicyLifetimeTest,
    "ProjectJ.Continuation.FollowerPolicyLifetime", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJFollowerPolicyLifetimeTest::RunTest(const FString&)
{
    auto* World = UWorld::CreateWorld(EWorldType::Game, false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); };
    auto* Character = World->SpawnActor<AProject_JCombatFacingFixture>();
    auto* Source = Character->GetMesh();
    auto* Coordinator = Character->FindComponentByClass<UProject_JAnimationUpdateCoordinatorComponent>();
    if (!TestNotNull(TEXT("Character owns the real coordinator"), Coordinator)) return false;
    Coordinator->bAllowVisualBudget = false;
    auto MakeFollower = [&]()
    {
        auto* Mesh = NewObject<USkeletalMeshComponent>(Character);
        Mesh->SetupAttachment(Source);
        Character->AddInstanceComponent(Mesh);
        Mesh->RegisterComponent();
        Mesh->AnimScriptInstance = NewObject<UProject_JRetargetAnimInstance>(Mesh);
        Mesh->SetComponentTickEnabled(false);
        return Mesh;
    };
    auto* First = MakeFollower();
    auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Source);
    Source->AnimScriptInstance = Anim;
    Anim->InitializeAnimation();
    FProject_JAnimOptimizationPolicy Policy;
    Policy.bEnableFollowerRetarget = true;
    Anim->ApplyOptimizationPolicy(Policy);
    TestTrue(TEXT("Initial enabled policy applies to an initially disabled follower"), First->IsComponentTickEnabled());
    First->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    auto* Second = MakeFollower();
    Anim->ApplyOptimizationPolicy(Policy);
    TestTrue(TEXT("Identical policy applies to the replacement follower"), Second->IsComponentTickEnabled());
    TestFalse(TEXT("Detached follower receives its authored tick control back"), First->IsComponentTickEnabled());
    Second->SetComponentTickEnabled(false);
    Anim->ApplyOptimizationPolicy(Policy);
    TestFalse(TEXT("Unchanged target and policy do not rewrite tick every frame"), Second->IsComponentTickEnabled());
    Anim->NativeInitializeAnimation();
    Anim->ApplyOptimizationPolicy(Policy);
    TestTrue(TEXT("A new animation lifetime reapplies the same policy"), Second->IsComponentTickEnabled());
    Policy.bEnableFollowerRetarget = false;
    Anim->ApplyOptimizationPolicy(Policy);
    TestFalse(TEXT("Policy transitions still disable the current follower"), Second->IsComponentTickEnabled());
    Second->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    auto* Third = MakeFollower(); Third->SetComponentTickEnabled(true);
    Anim->ApplyOptimizationPolicy(Policy);
    TestFalse(TEXT("Identical hidden policy applies to a replacement enabled follower"), Third->IsComponentTickEnabled());
    Third->EnableExternalTickRateControl(true);
    Third->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
    CastChecked<UProject_JRetargetAnimInstance>(Third->GetAnimInstance())->NativeInitializeAnimation();
    TestEqual(TEXT("Retarget initialization respects any existing external allocator, not just this coordinator"),
        Third->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered);
    Policy.bEnableFollowerRetarget = true;
    Anim->ApplyOptimizationPolicy(Policy);
    TestFalse(TEXT("An external allocator retains tick ownership"), Third->IsComponentTickEnabled());
    Third->EnableExternalTickRateControl(false);
    Anim->ApplyOptimizationPolicy(Policy);
    TestTrue(TEXT("Allocator release hands the same policy back to fallback"), Third->IsComponentTickEnabled());
    Coordinator->bAllowVisualBudget = true;
    Third->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    auto* Fourth = MakeFollower();
    Anim->ApplyOptimizationPolicy(Policy);
    TestTrue(TEXT("Enabled coordinator does not own an undiscovered follower"), Fourth->IsComponentTickEnabled());
    auto* Asset = LoadObject<USkeletalMesh>(nullptr,
        TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple"));
    if (!TestNotNull(TEXT("Real visual asset for coordinator acquisition"), Asset)) return false;
    Source->SetSkeletalMesh(Asset);
    Source->AnimScriptInstance = Anim;
    Fourth->SetSkeletalMesh(Asset);
    Fourth->AnimScriptInstance = NewObject<UProject_JRetargetAnimInstance>(Fourth);
    Fourth->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::OnlyTickMontagesWhenNotRendered;
    const auto AuthoredVisibility = Fourth->VisibilityBasedAnimTickOption;
    Fourth->SetHiddenInGame(true);
    Coordinator->RefreshVisualBudget(1.f / 60.f);
    TestTrue(TEXT("Coordinator acquires actual external tick controls"), Fourth->IsUsingExternalTickRateControl());
    CastChecked<UProject_JRetargetAnimInstance>(Fourth->GetAnimInstance())->NativeInitializeAnimation();
    TestEqual(TEXT("Retarget reinitialization preserves the active visual allocator's visibility ownership"),
        Fourth->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones);
    Anim->ApplyOptimizationPolicy(Policy);
    TestFalse(TEXT("Fallback cannot wake a hidden coordinator-owned mesh"), Fourth->IsComponentTickEnabled());
    Coordinator->bAllowVisualBudget = false;
    Coordinator->RefreshVisualBudget(1.f / 60.f);
    TestFalse(TEXT("Coordinator returns external controls"), Fourth->IsUsingExternalTickRateControl());
    TestEqual(TEXT("Allocator release restores the original authored visibility after retarget reinitialization"),
        Fourth->VisibilityBasedAnimTickOption, AuthoredVisibility);
    CastChecked<UProject_JRetargetAnimInstance>(Fourth->GetAnimInstance())->NativeInitializeAnimation();
    TestEqual(TEXT("Standalone retarget initialization retains its original visibility default"),
        Fourth->VisibilityBasedAnimTickOption, EVisibilityBasedAnimTickOption::OnlyTickPoseWhenRendered);
    Anim->ApplyOptimizationPolicy(Policy);
    TestTrue(TEXT("Identical policy is applied after coordinator release"), Fourth->IsComponentTickEnabled());
    Anim->NativeUninitializeAnimation();
    TestFalse(TEXT("Uninitialize returns the originally disabled tick control"), Fourth->IsComponentTickEnabled());
    Fourth->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
    auto* Excluded = MakeFollower();
    Excluded->ComponentTags.Add(TEXT("NoVisualBudget"));
    Anim->NativeInitializeAnimation();
    Anim->ApplyOptimizationPolicy(Policy);
    TestFalse(TEXT("Excluded follower retains its authored tick control"), Excluded->IsComponentTickEnabled());
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponMotionNumericAdmissionTest,
    "ProjectJ.Continuation.WeaponMotionNumericAdmission", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponMotionNumericAdmissionTest::RunTest(const FString&)
{
    using namespace Project_J::WeaponMotion;
    const float Bad = std::numeric_limits<float>::quiet_NaN();
    TArray<FProject_JWeaponMotionKey> Keys;
    TestTrue(TEXT("Empty motion and zero duration remain valid source-socket data"), GetValidationFailure(Keys, 0, 1, 0, 0, 0).IsNone());
    Keys.AddDefaulted(); Keys[0].NormalizedTime = Bad;
    TestEqual(TEXT("A single NaN key fails admission without an ordering comparison"), GetValidationFailure(Keys, 0, 1, 1, .08f, .08f), FName(TEXT("KeyTime")));
    Keys[0].NormalizedTime = 0;
    Keys[0].RelativeTransform.SetRotation(FQuat(0, 0, 0, 0));
    TestEqual(TEXT("Finite non-unit quaternion is rejected"), GetValidationFailure(Keys, 0, 1, 1, 0, 0), FName(TEXT("KeyTransform")));
    Keys[0].RelativeTransform = FTransform::Identity;
    const auto Duplicate = Keys[0];
    Keys.Add(Duplicate);
    TestEqual(TEXT("Duplicate times are rejected before interpolation"), GetValidationFailure(Keys, 0, 1, 1, 0, 0), FName(TEXT("KeyOrder")));
    Keys[1].NormalizedTime = 1;
    TestTrue(TEXT("Corrected ordered keys are accepted"), GetValidationFailure(Keys, 0, 1, 1, 0, 0).IsNone());
    TestEqual(TEXT("Infinite blend is rejected"), GetValidationFailure(Keys, 0, 1, 1, std::numeric_limits<float>::infinity(), 0), FName(TEXT("EntryBlend")));
    auto* Presentation = NewObject<UProject_JWeaponPresentationComponent>();
    // Exercise active-state publication independently of weapon spawning/render quality.
    Presentation->bIndependentMotionActive = true;
    Presentation->ActiveMotionKeys = Keys;
    Presentation->ActiveMotionDurationSeconds = 1.f;
    Presentation->ActiveMotionNormalizedTime = .4f;
    Presentation->ActivePrimaryGripIKAlpha = .25f;
    auto Invalid = Keys; Invalid[0].NormalizedTime = Bad;
    AddExpectedError(TEXT("Invalid Weapon Motion KeyTime"), EAutomationExpectedErrorFlags::Contains, 1);
    TestFalse(TEXT("Begin rejects invalid replacement of an active state"), Presentation->BeginIndependentMotion(Invalid, .9f, 1, 2, 0, 0));
    Presentation->RefreshIndependentMotionKeys(Invalid, .9f, 1, 2, 0, 0);
    Presentation->SetIndependentMotionPosition(Bad);
    TestEqual(TEXT("Invalid refresh does not partially publish duration"), Presentation->ActiveMotionDurationSeconds, 1.f);
    TestEqual(TEXT("Invalid refresh does not partially publish IK"), Presentation->ActivePrimaryGripIKAlpha, .25f);
    TestEqual(TEXT("Invalid position retains the last finite source time"), Presentation->ActiveMotionNormalizedTime, .4f);
    Presentation->RefreshIndependentMotionKeys(Keys, .9f, 1, 2, 0, 0);
    TestEqual(TEXT("Valid tuple resumes publication"), Presentation->ActiveMotionDurationSeconds, 2.f);
    Presentation->bIndependentMotionActive = false;
    auto* Notify = NewObject<UProject_JAnimNotifyState_WeaponMotion>(); Notify->MotionKeys = Invalid;
    FDataValidationContext Context;
    TestEqual(TEXT("Notify authoring uses the runtime numeric contract"), Notify->IsDataValid(Context), EDataValidationResult::Invalid);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJOneShotMalformedInputTest,
    "ProjectJ.Continuation.OneShotMalformedInput", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJOneShotMalformedInputTest::RunTest(const FString&)
{
    using P = FProject_JOneShotInputResponse;
    P::FInput Valid; Valid.bCanSteer = Valid.bHasMoveReference = Valid.bHasMoveInput = true;
    TestFalse(TEXT("Default compatible input preserves the authored owner"), P::Resolve(Valid).bRelease);
    for (int32 Field = 0; Field < 5; ++Field)
    {
        auto Input = Valid;
        float* Values[] = { &Input.EntryControlYaw, &Input.EntryMoveYaw, &Input.MouseCancelAngle, &Input.MoveCancelAngle, &Input.SteeringLimit };
        *Values[Field] = std::numeric_limits<float>::quiet_NaN();
        const auto Result = P::Resolve(Input);
        TestTrue(TEXT("Malformed entry/tuning cannot retain ownership"), Result.bRelease);
        TestEqual(TEXT("Malformed entry/tuning has a typed cause"), FString(Result.Reason), FString(TEXT("InvalidInputSample")));
    }
    Valid.MouseCancelAngle = -1;
    TestTrue(TEXT("Negative authored cancellation threshold is invalid"), P::Resolve(Valid).bRelease);
    auto* Profile = NewObject<UProject_JLocomotionProfile>();
    Profile->TransitionPolicy.StartMouseTurnCancelAngle = std::numeric_limits<float>::quiet_NaN();
    FDataValidationContext Context;
    TestEqual(TEXT("Profile authoring rejects the same malformed cancellation tuning"), Profile->IsDataValid(Context), EDataValidationResult::Invalid);
    return true;
}
#endif
