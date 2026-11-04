#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Misc/App.h"
#include "GameFramework/Actor.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "UObject/StrongObjectPtr.h"
#include "Tests/Project_JClothPolicyFixture.h"
#include "Animation/Project_JBudgetedSkeletalMeshComponent.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularAuthoredPose,
	"ProjectJ.PlayerLocomotion.ModularAuthoredPoseLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJModularAuthoredPose::RunTest(const FString&)
{
	auto* Mesh = LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
	if (!TestNotNull(TEXT("Shared humanoid fixture"), Mesh)) return false;
	auto* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Owner = World->SpawnActor<AActor>();
	auto MakeSource = [&]()
	{
		auto* Source = NewObject<USkeletalMeshComponent>(Owner);
		Owner->AddInstanceComponent(Source);
		Source->SetSkeletalMeshAsset(Mesh);
		Source->RegisterComponent();
		return Source;
	};
	auto* First = MakeSource();
	auto* Second = MakeSource();
	Owner->SetRootComponent(First);
	auto* Part = NewObject<UProject_JModularMeshComponent>(Owner);
	Owner->AddInstanceComponent(Part);
	Part->bFollowAttachParentPose = true;
	Part->SetSkeletalMeshAsset(Mesh);
	Part->SetupAttachment(First);
	Part->RegisterComponent();
	TestTrue(TEXT("Authored registration binds the skeletal parent"), Part->LeaderPoseComponent.Get() == First);
	TestFalse(TEXT("Shared parts do not evaluate another graph"), Part->IsComponentTickEnabled());
	Part->Deactivate();
	Part->Activate(true);
	TestFalse(TEXT("Actor auto-activation cannot re-enable shared-pose ticking"), Part->IsComponentTickEnabled());
	Part->AttachToComponent(Second, FAttachmentTransformRules::KeepRelativeTransform);
	TestTrue(TEXT("Changing parent replaces the pose owner"), Part->LeaderPoseComponent.Get() == Second);
	Part->UnregisterComponent();
	TestNull(TEXT("Unregistration releases the authored source"), Part->LeaderPoseComponent.Get());
	TestFalse(TEXT("Releasing the leader after unregister must not recreate orphaned physics"), Part->IsPhysicsStateCreated());
	Part->RegisterComponent();
	TestTrue(TEXT("Re-registration restores the current source"), Part->LeaderPoseComponent.Get() == Second);
	Part->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
	TestNull(TEXT("Detachment cannot retain a stale source"), Part->LeaderPoseComponent.Get());
	Part->DestroyComponent();
	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularClothTick,
	"ProjectJ.PlayerLocomotion.ModularClothTickLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJModularClothTick::RunTest(const FString&)
{
	auto* Body = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Spellblade/Spellblade_Woman.Spellblade_Woman"));
	if (!TestNotNull(TEXT("Body fixture"), Body)) return false;
	// Policy fixture only: exercise admission before streamed simulation resources
	// exist, without depending on retired production cloth or Saved backups.
	TStrongObjectPtr<USkeletalMesh> ClothFixture(MakeProjectJClothPolicyFixture(Body));
	auto* Cloak = ClothFixture.Get();
	if (!TestNotNull(TEXT("Private cloth admission metadata fixture"), Cloak)) return false;
	TestFalse(TEXT("Production body data remains unchanged"), Body->HasActiveClothingAssets());
	// Preview must retain metadata admission before a solver exists, even with
	// NullRHI. A headless runtime world must instead follow creation admission.
	for (const EWorldType::Type WorldType : {EWorldType::EditorPreview, EWorldType::Game})
	{
		auto* World = UWorld::CreateWorld(WorldType, false);
		GEngine->CreateNewWorldContext(WorldType).SetCurrentWorld(World);
		auto* Owner = World->SpawnActor<AActor>();
		auto* Source = NewObject<USkeletalMeshComponent>(Owner);
		Owner->SetRootComponent(Source); Owner->AddInstanceComponent(Source);
		Source->SetSkeletalMeshAsset(Body); Source->RegisterComponent();
		auto* Part = NewObject<UProject_JModularMeshComponent>(Owner);
		Owner->AddInstanceComponent(Part); Part->SetSkeletalMeshAsset(Cloak);
		Part->AttachAndSetLeader(Source); Part->RegisterComponent();
		const bool bExpectedTick = WorldType == EWorldType::EditorPreview || IsRunningCommandlet() || FApp::CanEverRender();
		TestEqual(TEXT("Runtime-only headless creation policy preserves preview admission"), Part->GetAllowClothActors(), bExpectedTick);
		TestTrue(TEXT("Active section metadata is admitted before streamed simulation resources exist"), Project_J::Animation::UsesIndependentClothSimulation(*Part));
		TestEqual(TEXT("Shared bones tick only when cloth creation is admitted"), Part->IsComponentTickEnabled(), bExpectedTick);
		TestNull(TEXT("No second graph for bone-shared cloth"), Part->GetAnimInstance());
		Part->AttachAndSetLeader(Source);
		TestTrue(TEXT("Idempotent binding retains source and cloth admission"),
			Part->LeaderPoseComponent.Get() == Source && Part->IsComponentTickEnabled() == bExpectedTick);
		Part->Deactivate(); Part->Activate(true);
		TestEqual(TEXT("Activation preserves cloth creation admission"), Part->IsComponentTickEnabled(), bExpectedTick);
		Part->UnregisterComponent();
		TestFalse(TEXT("Cloth unregister cannot recreate rigid physics after the engine destroyed it"), Part->IsPhysicsStateCreated());
		Part->RegisterComponent();
		TestEqual(TEXT("Re-registration restores cloth policy"), Part->IsComponentTickEnabled(), bExpectedTick);
		Part->SetSkeletalMeshAsset(Body);
		TestFalse(TEXT("Changing to a non-cloth mesh removes the extra tick"), Part->IsComponentTickEnabled());
		Part->SetSkeletalMeshAsset(Cloak);
		TestEqual(TEXT("Changing back to cloth preserves creation admission"), Part->IsComponentTickEnabled(), bExpectedTick);
		Part->UnregisterComponent();
		Part->SetAllowClothActors(false);
		Part->RegisterComponent();
		TestFalse(TEXT("An authored creation opt-out stays disabled across registration"), Part->GetAllowClothActors());
		TestFalse(TEXT("Opted-out shared cloth does not retain a component tick"), Part->IsComponentTickEnabled());
		Part->Deactivate(); Part->Activate(true);
		TestFalse(TEXT("Activation cannot override cloth creation opt-out"), Part->IsComponentTickEnabled());
		Part->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
		TestFalse(TEXT("Detached automatic part cannot keep simulating a stale source"), Part->IsComponentTickEnabled());
		Part->DestroyComponent(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJModularSharedPhysicsBounds,
    "ProjectJ.PlayerLocomotion.ModularSharedPhysicsBounds",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJModularSharedPhysicsBounds::RunTest(const FString&)
{
    auto* Shared=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Bounds.SharedPhysicsCache"));
    auto* LocalCache=IConsoleManager::Get().FindConsoleVariable(TEXT("a.CacheLocalSpaceBounds"));
    if(!TestNotNull(TEXT("Shared native bounds opt-in"),Shared) || !TestNotNull(TEXT("Native local bounds policy"),LocalCache)) return false;
    if(!TestEqual(TEXT("Fixture requires the existing native local-cache mode without changing it"),LocalCache->GetInt(),1)) return false;
    auto* Hits=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Bounds.SharedPhysicsHits"));
    auto* Misses=IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Bounds.SharedPhysicsMisses"));
    if(!TestNotNull(TEXT("Read-only shared bounds hit observation"),Hits) || !TestNotNull(TEXT("Read-only shared bounds miss observation"),Misses)) return false;
    if(!TestTrue(TEXT("Shared bounds counters have headroom; saturation is unknown"),Hits->GetInt()>=0 && Misses->GetInt()>=0 && Hits->GetInt()<MAX_int32-1024 && Misses->GetInt()<MAX_int32-1024)) return false;
    const int32 SavedShared=Shared->GetInt();const EConsoleVariableFlags SavedSetBy=EConsoleVariableFlags(Shared->GetFlags() & ECVF_SetByMask);
    // Native ReplaceCurrent explicitly excludes Constructor history. An
    // explicit same-rank Set preserves the original priority, including0.
    Shared->Set(0,SavedSetBy);
    ON_SCOPE_EXIT
    {
        Shared->Set(SavedShared,SavedSetBy);
        TestEqual(TEXT("Shared opt-in value is restored"),Shared->GetInt(),SavedShared);
        TestTrue(TEXT("Shared opt-in SetBy priority is preserved"),EConsoleVariableFlags(Shared->GetFlags() & ECVF_SetByMask)==SavedSetBy);
    };
    auto* LeaderAsset=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Spellblade/Meshes/SK_Spellblade_Complete.SK_Spellblade_Complete"));
    auto* Lower=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Spellblade/Meshes/SK_Spellblade_lower.SK_Spellblade_lower"));
    auto* Shoulder=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Spellblade/Meshes/SK_Spellblade_sho.SK_Spellblade_sho"));
    auto* PA=LoadObject<UPhysicsAsset>(nullptr,TEXT("/Game/Characters/Mannequins/Rigs/PA_Mannequin.PA_Mannequin"));
    if(!TestNotNull(TEXT("Actual hidden source asset"),LeaderAsset) || !TestNotNull(TEXT("Actual lower part"),Lower) ||
       !TestNotNull(TEXT("Different actual shoulder part"),Shoulder) || !TestNotNull(TEXT("Actual shared physics asset"),PA)) return false;
    TArray<TPair<UPackage*,bool>> Packages;
    for(UObject* Asset : TArray<UObject*>{LeaderAsset,Lower,Shoulder,PA})
    {
        auto* Package=Asset->GetOutermost();bool bFound=false;for(const auto& Entry:Packages) bFound |= Entry.Key==Package;
        if(!bFound) Packages.Emplace(Package,Package->IsDirty());
        if(!TestFalse(TEXT("Loaded fixture definitions must already be clean"),Package->IsDirty())) return false;
    }
    ON_SCOPE_EXIT {for(const auto& Entry:Packages) TestEqual(TEXT("Bounds/pose fixture preserves definition dirty state"),Entry.Key->IsDirty(),Entry.Value);};
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);
    GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    auto* Owner=World->SpawnActor<AActor>();
    auto* Source=NewObject<UProject_JBudgetedSkeletalMeshComponent>(Owner);
    Owner->SetRootComponent(Source);Owner->AddInstanceComponent(Source);
    Source->SetDisablePostProcessBlueprint(true);Source->SetSkeletalMeshAsset(LeaderAsset);Source->SetPhysicsAsset(PA);
    Source->bIncludeComponentLocationIntoBounds=false;Source->bComponentUseFixedSkelBounds=false;Source->bUseAttachParentBound=false;
    Source->bUseBoundsFromLeaderPoseComponent=false;Source->bCastHiddenShadow=false;Source->bConsiderAllBodiesForBounds=false;
    Source->BoundsScale=1.f;Source->RegisterComponent();Source->RefreshBoneTransforms();Source->SetVisibility(false,false);
    auto MakePart=[&](USkeletalMesh* Asset)
    {
        auto* Part=NewObject<UProject_JModularMeshComponent>(Owner);Owner->AddInstanceComponent(Part);
        Part->SetDisablePostProcessBlueprint(true);Part->SetSkeletalMeshAsset(Asset);
        Part->bIncludeComponentLocationIntoBounds=false;Part->bUseBoundsFromLeaderPoseComponent=false;
        Part->bUseAttachParentBound=false;Part->bComponentUseFixedSkelBounds=false;Part->bConsiderAllBodiesForBounds=false;Part->BoundsScale=1.f;
        Part->AttachAndSetLeader(Source);Part->RegisterComponent();Part->SetVisibility(true,false);return Part;
    };
    auto* First=MakePart(Lower);auto* Second=MakePart(Shoulder);
    if(!TestTrue(TEXT("Registered native source has an initialized pose"),Source->IsRegistered() && Source->GetNumComponentSpaceTransforms()>0) ||
       !TestTrue(TEXT("Source remains hidden while both actual parts render"),!Source->ShouldRender() && First->ShouldRender() && Second->ShouldRender()) ||
       !TestTrue(TEXT("Different child assets retain the same actual leader"),Lower!=Shoulder && First->LeaderPoseComponent.Get()==Source && Second->LeaderPoseComponent.Get()==Source) ||
       !TestNull(TEXT("First child has no own physics asset"),First->GetPhysicsAsset()) || !TestNull(TEXT("Second child has no own physics asset"),Second->GetPhysicsAsset())) return false;
    if(!TestTrue(TEXT("Authored native PA has a nonempty bounded selected body set"),PA->BoundsBodies.Num()>0 && PA->BoundsBodies.Num()<=64)) return false;
    TestFalse(TEXT("Source fixture has no cloth simulation metadata"),LeaderAsset->HasActiveClothingAssets());
    TestFalse(TEXT("Lower fixture has no cloth simulation metadata"),Lower->HasActiveClothingAssets());
    TestFalse(TEXT("Shoulder fixture has no cloth simulation metadata"),Shoulder->HasActiveClothingAssets());
    const TArray<FTransform> PoseA=Source->GetComponentSpaceTransforms();
    auto ApplyPose=[&](const TArray<FTransform>& Pose)
    {
        Source->GetEditableComponentSpaceTransforms()=Pose;
        Source->ApplyEditedComponentSpaceTransforms(); // Public editor flip/update path; no Finalize or revision injection.
    };
    ON_SCOPE_EXIT {if(IsValid(Source) && Source->IsRegistered()) ApplyPose(PoseA);};
    const auto Same=[](const FBoxSphereBounds& A,const FBoxSphereBounds& B)
    {return A.Origin.Equals(B.Origin,1.e-5) && A.BoxExtent.Equals(B.BoxExtent,1.e-5) && FMath::IsNearlyEqual(A.SphereRadius,B.SphereRadius,1.e-5);};
    auto Native=[&](UProject_JModularMeshComponent* Part,const FTransform& Query)
    {
        Shared->Set(0,SavedSetBy);Part->InvalidateCachedBounds();const int32 BeforeHits=Hits->GetInt(),BeforeMisses=Misses->GetInt();
        const auto Result=Part->CalcBounds(Query);
        TestTrue(TEXT("Disabled shared path consumes only original native bounds"),Hits->GetInt()==BeforeHits && Misses->GetInt()==BeforeMisses);return Result;
    };
    auto Pair=[&](const TCHAR* Case,UProject_JModularMeshComponent* Part,const FTransform& Query,int32 ExpectedHit=-1,int32 ExpectedMiss=-1)
    {
        const auto Expected=Native(Part,Query);const auto ExpectedCached=Part->CalcBounds(Query);Shared->Set(1,SavedSetBy);Part->InvalidateCachedBounds();
        const int32 BeforeHits=Hits->GetInt(),BeforeMisses=Misses->GetInt();const auto Actual=Part->CalcBounds(Query);
        const int32 DeltaHit=Hits->GetInt()-BeforeHits,DeltaMiss=Misses->GetInt()-BeforeMisses;
        TestTrue(Case,Same(Expected,Actual));
        if(ExpectedHit>=0) TestEqual(FString::Printf(TEXT("%s: actual shared hits"),Case),DeltaHit,ExpectedHit);
        if(ExpectedMiss>=0) TestEqual(FString::Printf(TEXT("%s: actual shared misses"),Case),DeltaMiss,ExpectedMiss);
        if(ExpectedHit<0 && ExpectedMiss<0) TestEqual(FString::Printf(TEXT("%s: one actual shared hit or miss"),Case),DeltaHit+DeltaMiss,1);
        TestTrue(TEXT("Native result is finite with a positive radius"),!Actual.Origin.ContainsNaN() && !Actual.BoxExtent.ContainsNaN() && FMath::IsFinite(Actual.SphereRadius) && Actual.SphereRadius>0);
        const int32 CachedHits=Hits->GetInt(),CachedMisses=Misses->GetInt();
        TestTrue(TEXT("Original native child-cache result remains unchanged"),Same(ExpectedCached,Part->CalcBounds(Query)));
        TestTrue(TEXT("Original child-cache hit performs no shared lookup"),Hits->GetInt()==CachedHits && Misses->GetInt()==CachedMisses);return Actual;
    };
    const FTransform Identity=FTransform::Identity;
    const FTransform OtherQuery(FRotator(12,35,5),FVector(80,-30,25),FVector(1.25));
    const auto OriginalA=Native(First,Identity);
    TestTrue(TEXT("Different actual part assets have equivalent original native body envelopes"),Same(OriginalA,Native(Second,Identity)));
    Pair(TEXT("First child publishes its original native envelope"),First,Identity,0,1);
    Pair(TEXT("Second child reuses native local bounds under its own query transform"),Second,OtherQuery,1,0);
    const uint64 PoseFrame=GFrameCounter;
    TArray<FTransform> PoseB=PoseA;for(auto& Transform:PoseB) Transform.AddToTranslation(FVector(350,0,0));ApplyPose(PoseB);
    const auto OriginalB=Native(First,Identity);
    TestFalse(TEXT("Edited pose is a non-inert native bounds positive control"),Same(OriginalA,OriginalB));
    Pair(TEXT("Changed selected bone values cannot reuse stale pose A"),First,Identity);
    Pair(TEXT("Sibling result remains native after pose B publication"),Second,OtherQuery);
    ApplyPose(PoseA);
    TestEqual(TEXT("Pose ABA uses the same actual engine frame"),GFrameCounter,PoseFrame);
    TestTrue(TEXT("Public pose restoration returns the original native envelope"),Same(OriginalA,Native(First,Identity)));
    Pair(TEXT("Restored pose A does not consume stale pose B"),Second,OtherQuery);
    // Unsupported inputs keep the original native path; no policy or asset override is persisted.
    Second->bIncludeComponentLocationIntoBounds=true;Pair(TEXT("Include-location fallback matches native"),Second,OtherQuery,0,0);Second->bIncludeComponentLocationIntoBounds=false;
    Pair(TEXT("Nonuniform query fallback matches native"),Second,FTransform(FRotator::ZeroRotator,FVector(12,34,56),FVector(1,2,1)),0,0);
    Second->SetPhysicsAsset(PA,true);Pair(TEXT("Own-physics-asset fallback matches native"),Second,Identity,0,0);Second->SetPhysicsAsset(nullptr,true);
    Second->SetSkeletalMeshAsset(Lower);Pair(TEXT("Child asset replacement cannot retain a stale binding"),Second,OtherQuery);
    Second->SetSkeletalMeshAsset(Shoulder);Pair(TEXT("Original different child asset is reusable after replacement"),Second,OtherQuery);
    Second->UnregisterComponent();Second->RegisterComponent();Pair(TEXT("Child unregister/reregister preserves native output"),Second,OtherQuery);
    Source->UnregisterComponent();Pair(TEXT("Unregistered leader cannot serve a retained shared entry"),Second,OtherQuery,0,0);
    Source->RegisterComponent();Source->RefreshBoneTransforms();Source->SetVisibility(false,false);ApplyPose(PoseA);
    Pair(TEXT("Source registration replacement preserves native output"),Second,OtherQuery);
    Source->InitAnim(true);Source->RefreshBoneTransforms();ApplyPose(PoseA);
    Pair(TEXT("Normal public animation reinitialization preserves native output"),Second,OtherQuery);
    Source->SetPhysicsAsset(PA,true);Pair(TEXT("Physics reinitialization preserves current native output"),Second,OtherQuery);
    Second->DetachFromComponent(FDetachmentTransformRules::KeepRelativeTransform);
    TestNull(TEXT("Detach retires the production leader relationship"),Second->LeaderPoseComponent.Get());
    Pair(TEXT("Detached part retains its own native bounds fallback"),Second,OtherQuery,0,0);
    Second->AttachAndSetLeader(Source);Pair(TEXT("Rebound part consumes only its current native source"),Second,OtherQuery);
    TestFalse(TEXT("Original leader-bounds flag is never adopted"),First->bUseBoundsFromLeaderPoseComponent || Second->bUseBoundsFromLeaderPoseComponent);
    return true;
}
#endif
