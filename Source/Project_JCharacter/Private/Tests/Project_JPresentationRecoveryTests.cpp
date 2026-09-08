#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Project_JNPCCharacter.h"
#include "Engine/World.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(TestAttackTag, "ProjectJ.Tests.Presentation.Attack");
UE_DEFINE_GAMEPLAY_TAG_STATIC(TestCueTag, "ProjectJ.Tests.Presentation.Cue");
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPresentationRecoveryTest, "ProjectJ.Integrated.PresentationRecovery",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJPresentationRecoveryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* NPC = World->SpawnActor<AProject_JNPCCharacter>();
	auto* C = NewObject<UProject_JCombatPresentationComponent>(NPC); C->RegisterComponent();
	C->BeginAttackPresentation(TestAttackTag);
	auto* Set = NewObject<UProject_JCombatPresentationSet>(C);
	auto* Profile = NewObject<UProject_JAttackPresentationProfile>(C);
	FProject_JCombatVFXCueDefinition Cue; Cue.CueTag = TestCueTag; Cue.AttachmentTarget = EProject_JCombatVFXAttachmentTarget::CharacterMesh;
	Profile->Cues.Add(Cue);
	FProject_JCombatAttackPresentationEntry Entry; Entry.AttackTag = TestAttackTag; Entry.Profile = Profile;
	Set->AttackPresentations.Add(Entry); C->BasePresentationSet = Set;
	TestTrue(TEXT("NPC resolves the existing set/profile/cue types"), C->ResolveCue(TestCueTag) == &Profile->Cues[0]);
	const uint32 First = C->ActiveAttackInstance;
	C->BeginAttackPresentation(TestAttackTag);
	TestTrue(TEXT("Repeated attack tag has a new instance"), C->ActiveAttackInstance != First);
	C->StartedCueTags.AddTag(TestCueTag);
	C->ReplicatedPresentationState.EventOrder = 30;
	C->ReplicatedPresentationState.Revision = 30;
	C->LastAppliedPresentationEventOrder = 40; // Later one-shot does not invalidate persistent loop recovery.
	C->LastAppliedRecoveryEventOrder = 20;
	C->ApplyReplicatedState();
	TestTrue(TEXT("Same-attack recovery preserves one-shot deduplication"), C->StartedCueTags.HasTagExact(TestCueTag));
	TestEqual(TEXT("Older useful recovery accepted after a newer one-shot"), C->LastAppliedPresentationRevision, 30);
	TestEqual(TEXT("Event ordering never moves backwards"), C->LastAppliedPresentationEventOrder, 40);
	C->LastAppliedRecoveryEventOrder = 50;
	C->ReplicatedPresentationState.Revision = 31; C->ReplicatedPresentationState.EventOrder = 45;
	C->ReplicatedPresentationState.ActiveAttackTag = {};
	C->ApplyReplicatedState();
	TestTrue(TEXT("Stale recovery cannot erase a newer attack"), C->ActiveAttackTag.IsValid());
	C->EndAttackPresentation();
	const int32 Order = C->NextPresentationEventOrder;
	C->EndAttackPresentation();
	TestEqual(TEXT("Repeated end does not emit network updates"), C->NextPresentationEventOrder, Order);
	C->DestroyComponent(); World->DestroyWorld(false);
	return true;
}
#endif
