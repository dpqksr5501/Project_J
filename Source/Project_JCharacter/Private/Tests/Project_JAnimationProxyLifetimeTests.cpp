#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationProxyLifetimeTest, "ProjectJ.RuntimeOwnership.AnimationProxyPinsQueuedAssets",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationProxyLifetimeTest::RunTest(const FString&)
{
    TStrongObjectPtr<USkeletalMeshComponent> Mesh(NewObject<USkeletalMeshComponent>());
    TStrongObjectPtr<UProject_JCharacterAnimInstance> Anim(NewObject<UProject_JCharacterAnimInstance>(Mesh.Get()));
    auto& Proxy = Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
    auto* Database = NewObject<UPoseSearchDatabase>();
    const TWeakObjectPtr<UPoseSearchDatabase> WeakDatabase(Database);
    FProject_JAnimThreadSafeData Data;
    Proxy.QueueGameThreadData(Data, Database, true, true, false);
    Database = nullptr; // The producer can replace its asset before a hidden pose consumes this snapshot.
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestTrue(TEXT("Queued proxy asset survives collection without a producer reference"), WeakDatabase.IsValid());
    Proxy.QueueGameThreadData(Data, nullptr, false, false, false);
    CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
    TestFalse(TEXT("Replaced proxy asset is not pinned indefinitely"), WeakDatabase.IsValid());
    return true;
}
#endif
