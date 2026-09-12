#include "Tests/Project_JMessageTestReceiver.h"
#include "System/Project_JMessageSubsystem.h"
#include "Engine/GameInstance.h"

void UProject_JMessageTestReceiver::ChangeSubscriptions(FGameplayTag Channel, UObject*)
{
    ++FirstCount;
    Router->ClearChannel(Channel);
    Router->GetChannelDelegate(OtherChannel).AddDynamic(this, &UProject_JMessageTestReceiver::Count);
}
void UProject_JMessageTestReceiver::Count(FGameplayTag, UObject*) { ++SecondCount; }

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
UE_DEFINE_GAMEPLAY_TAG_STATIC(MessageFixtureA, "ProjectJ.Tests.Message.A");
UE_DEFINE_GAMEPLAY_TAG_STATIC(MessageFixtureB, "ProjectJ.Tests.Message.B");
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMessageRouterTest, "ProjectJ.MMO.Messaging.ReentrantSubscriptions",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJMessageRouterTest::RunTest(const FString&)
{
    auto* GameInstance = NewObject<UGameInstance>();
    auto* Router = NewObject<UProject_JMessageSubsystem>(GameInstance);
    auto* Receiver = NewObject<UProject_JMessageTestReceiver>();
    Receiver->Router = Router; Receiver->OtherChannel = MessageFixtureB;
    Router->GetChannelDelegate(MessageFixtureA).AddDynamic(Receiver, &UProject_JMessageTestReceiver::ChangeSubscriptions);
    Router->GetChannelDelegate(MessageFixtureA).AddDynamic(Receiver, &UProject_JMessageTestReceiver::Count);
    Router->BroadcastMessage(MessageFixtureA, nullptr);
    TestEqual(TEXT("Original subscriber snapshot delivered"), Receiver->SecondCount, 1);
    Router->BroadcastMessage(MessageFixtureA, nullptr);
    TestEqual(TEXT("Clear applies to following broadcasts"), Receiver->FirstCount, 1);
    Router->BroadcastMessage(MessageFixtureB, nullptr);
    TestEqual(TEXT("New channel usable after reentrant registration"), Receiver->SecondCount, 2);
    return true;
}
#endif
