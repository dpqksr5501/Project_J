#include "Tests/Project_JInputLeaseCallbackFixture.h"
#include "Game/Project_JInputLeaseSubsystem.h"
#include "UObject/GarbageCollection.h"

void UProject_JInputLeaseCallbackFixture::MappingAdded(const UInputMappingContext*)
{
#if WITH_DEV_AUTOMATION_TESTS
    if (Mode == 0) { Leases->Release(Subject.Get()); }
    else if (Mode == 1) { Leases->Deinitialize(); }
    else
    {
        CollectGarbage(RF_NoFlags);
        bObservedLiveSubject = Subject.IsValid();
    }
#endif
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/LocalPlayer.h"
#include "GameFramework/PlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "InputMappingContext.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInputLeaseCallbackTest, "ProjectJ.Continuation.InputLeaseCallback",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJInputLeaseCallbackTest::RunTest(const FString&)
{
    for (int32 Mode = 0; Mode < 3; ++Mode)
    {
        TStrongObjectPtr<UWorld> World(UWorld::CreateWorld(EWorldType::Game, false));
        GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World.Get());
        TStrongObjectPtr<ULocalPlayer> Player(NewObject<ULocalPlayer>(GEngine));
        auto* Controller = World->SpawnActor<APlayerController>();
        Controller->PlayerInput = NewObject<UEnhancedPlayerInput>(Controller);
        Controller->Player = Player.Get(); Player->PlayerController = Controller;
        Player->PlayerAdded(nullptr, 0);
        ON_SCOPE_EXIT { Player->PlayerRemoved(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World.Get()); };
        auto* Leases = Player->GetSubsystem<UProject_JInputLeaseSubsystem>();
        auto* Input = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
        if (!TestNotNull(TEXT("Actual lease service"), Leases) || !TestNotNull(TEXT("Actual mapping service"), Input)) { return false; }
        TStrongObjectPtr<UProject_JInputLeaseCallbackFixture> Receiver(NewObject<UProject_JInputLeaseCallbackFixture>());
        Receiver->Leases = Leases; Receiver->Mode = Mode;
        auto* Subject = NewObject<UInputMappingContext>(); Receiver->Subject = Subject;
        auto* Context = NewObject<UInputMappingContext>();
        Input->OnMappingContextAdded.AddDynamic(Receiver.Get(), &UProject_JInputLeaseCallbackFixture::MappingAdded);
        const bool bAcquired = Leases->Acquire(Subject, Context);
        Input->OnMappingContextAdded.RemoveDynamic(Receiver.Get(), &UProject_JInputLeaseCallbackFixture::MappingAdded);
        if (Mode < 2)
        {
            TestFalse(TEXT("Actual synchronous release/deinitialize cannot report a live acquired lease"), bAcquired);
            TestFalse(TEXT("Revoked acquire leaves no installed mapping"), Input->HasMappingContext(Context));
        }
        else
        {
            TestTrue(TEXT("An acquire participant survives GC until its mapping callback returns"), Receiver->bObservedLiveSubject);
            TestTrue(TEXT("A live acquire retains its requested mapping"), bAcquired && Input->HasMappingContext(Context));
            CollectGarbage(RF_NoFlags);
            TestFalse(TEXT("Acquire pins do not become permanent owner roots"), Receiver->Subject.IsValid());
            Leases->Release(nullptr); // Prune the expired weak owner without dereferencing it.
            TestFalse(TEXT("Expired owner pruning releases its mapping"), Input->HasMappingContext(Context));
        }
    }
    return true;
}
#endif
