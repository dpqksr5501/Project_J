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
#include "Misc/DataValidation.h"
#include <limits>

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
    Fourth->SetHiddenInGame(true);
    Coordinator->RefreshVisualBudget(1.f / 60.f);
    TestTrue(TEXT("Coordinator acquires actual external tick controls"), Fourth->IsUsingExternalTickRateControl());
    Anim->ApplyOptimizationPolicy(Policy);
    TestFalse(TEXT("Fallback cannot wake a hidden coordinator-owned mesh"), Fourth->IsComponentTickEnabled());
    Coordinator->bAllowVisualBudget = false;
    Coordinator->RefreshVisualBudget(1.f / 60.f);
    TestFalse(TEXT("Coordinator returns external controls"), Fourth->IsUsingExternalTickRateControl());
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
