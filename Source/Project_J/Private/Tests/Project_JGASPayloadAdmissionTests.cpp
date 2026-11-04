#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/Player.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Game/Project_JGameMode.h"
#include "Game/Project_JPlayerState.h"
#include "Testing/Project_JNetworkLoadFixture.h"
#include "Components/Project_JCommandBudgetComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Abilities/GameplayAbilityTargetTypes.h"
#include "Abilities/Tasks/AbilityTask_WaitInputRelease.h"
#include "Project_JGASPayloadAdmissionFixture.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FPayloadReleaseState157
{
    bool bEnabled = true;
    int32 Calls = 0;
    TFunction<void()> OnRelease;
};
// Native standalone shared ownership only: no serialization or custom target-data wire type is claimed.
struct FPayloadReleaseData157 final : FGameplayAbilityTargetData_LocationInfo
{
    TSharedPtr<FPayloadReleaseState157> ReleaseState;
    virtual ~FPayloadReleaseData157() override
    {
        const auto State = ReleaseState;
        if (State.IsValid() && State->bEnabled)
        {
            State->bEnabled = false; ++State->Calls;
            if (State->OnRelease) State->OnRelease();
        }
    }
};
struct FPayloadOwner157
{
    APlayerController* Controller = nullptr;
    AProject_JPlayerState* State = nullptr;
    UProject_JAbilitySystemComponent* ASC = nullptr;
    APawn* Avatar = nullptr;
    FGameplayAbilitySpecHandle Grant;
};

// Reuses156's native standalone session/controller/GAS setup. It is not a socket or private-cache fixture.
struct FPayloadWorld157
{
    TStrongObjectPtr<UGameInstance> Instance;
    UWorld* World = nullptr;
    AProject_JNetworkLoadGameMode* Mode = nullptr;
    bool bReady = false;

    FPayloadWorld157() : Instance(NewObject<UGameInstance>(GEngine))
    {
        Instance->InitializeStandalone(); World = Instance->GetWorld();
        FURL URL; URL.AddOption(TEXT("game=/Script/Project_J.Project_JNetworkLoadGameMode"));
        if (!World || !World->SetGameMode(URL)) return;
        World->InitializeActorsForPlay(URL); World->GetWorldSettings()->NotifyBeginPlay();
        Mode = Cast<AProject_JNetworkLoadGameMode>(World->GetAuthGameMode());
        if (Mode) { Mode->bRequireTrustedSessions = true; bReady = true; }
    }
    ~FPayloadWorld157()
    {
        if (!World) return;
        if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::LevelTransition);
        Instance->Shutdown(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
    }
    bool Prepare(FPayloadOwner157& Out, int32 Commands = 1000, int32 Capacity = 2, bool bLocalController = false)
    {
        if (!bReady) return false;
        Out.Controller = World->SpawnActor<APlayerController>();
        if (!Out.Controller) return false;
        Out.Controller->SetPlayer(NewObject<UPlayer>(Instance.Get()));
        if (bLocalController) Out.Controller->SetAsLocalPlayerController();
        if (!Out.Controller->GetPlayerState<AProject_JPlayerState>()) Out.Controller->InitPlayerState();
        Mode->PostLogin(Out.Controller); Out.State = Out.Controller->GetPlayerState<AProject_JPlayerState>();
        if (!Out.State || Out.Controller->IsLocalController() != bLocalController || Out.Controller->GetPawn()) return false;
        ProjectJ::MMO::FServerSessionAttempt Attempt; ProjectJ::MMO::FServerSessionLease Lease;
        if (!Mode->BeginTrustedSession(Out.Controller, FProject_JAccountId::NewId(), FProject_JCharacterId::NewId(), Attempt)
            || !Mode->CompleteTrustedSession(Attempt, true, Lease)) return false;
        Out.Avatar = World->SpawnActor<APawn>();
        if (!Out.Avatar) return false;
        Out.Controller->Possess(Out.Avatar); Out.ASC = Out.State->GetProjectJAbilitySystemComponent();
        if (!Out.ASC) return false;
        Out.ASC->InitAbilityActorInfo(Out.State, Out.Avatar);
        auto* Budget = Out.State->FindComponentByClass<UProject_JCommandBudgetComponent>();
        if (!Budget || !Out.State->CanAcceptSessionCommand()) return false;
        // Per-fixture startup settings before any client activation; no CDO, clock/window or registry mutation.
        Budget->CommandsPerSecond = Commands; Out.ASC->ClientPayloadExecutionLimit = Capacity;
        Out.Grant = Out.ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
        return Out.Grant.IsValid();
    }
};
FPredictionKey Key157(int16 Value) { FPredictionKey Key; Key.Current = Value; return Key; }
bool Active157(UProject_JAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Grant)
{
    const auto* Spec = ASC->FindAbilitySpecFromHandle(Grant); return Spec && Spec->IsActive();
}
UGameplayAbility* Ability157(UProject_JAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Grant)
{
    const auto* Spec = ASC->FindAbilitySpecFromHandle(Grant); return Spec ? Spec->GetPrimaryInstance() : nullptr;
}
FGameplayAbilityTargetDataHandle Payload157(double Value, TWeakPtr<FGameplayAbilityTargetData>& Weak)
{
    auto* Data = new FGameplayAbilityTargetData_LocationInfo;
    Data->SourceLocation.LocationType = EGameplayAbilityTargetingLocationType::LiteralTransform;
    Data->SourceLocation.LiteralTransform = FTransform(FVector(Value, 0, 0));
    FGameplayAbilityTargetDataHandle Handle; Handle.Add(Data); Weak = Handle.Data[0]; return Handle;
}
bool PayloadValue157(const FGameplayAbilityTargetDataHandle& Handle, double Value)
{
    const auto* Data = Handle.Get(0);
    return Handle.Num() == 1 && Data && Data->GetScriptStruct() == FGameplayAbilityTargetData_LocationInfo::StaticStruct()
        && static_cast<const FGameplayAbilityTargetData_LocationInfo*>(Data)->SourceLocation.LiteralTransform.GetLocation().Equals(FVector(Value, 0, 0));
}
void SendPayload157(UProject_JAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Grant, FPredictionKey Key,
    double Value, TWeakPtr<FGameplayAbilityTargetData>& Weak)
{
    auto Payload = Payload157(Value, Weak);
    ASC->ServerSetReplicatedTargetData(Grant, Key, Payload, FProject_JGameplayTags::Get().InputTag_Weapon_LMB, Key);
}
bool NoData157(UProject_JAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Grant, FPredictionKey Key)
{
    // Both APIs Find only. Never bind a negative tuple: the native delegate getters FindOrAdd.
    return !ASC->CallReplicatedTargetDataDelegatesIfSet(Grant, Key)
        && !ASC->GetReplicatedDataOfGenericReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Grant, Key).bTriggered
        && !ASC->GetReplicatedDataOfGenericReplicatedEvent(EAbilityGenericReplicatedEvent::GenericConfirm, Grant, Key).bTriggered;
}
bool Negative157(UProject_JAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Grant, FPredictionKey Key, int32 Route)
{
    TWeakPtr<FGameplayAbilityTargetData> Weak;
    switch (Route)
    {
    case 0: SendPayload157(ASC, Grant, Key, 99, Weak); break;
    case 1: ASC->ServerSetReplicatedTargetDataCancelled(Grant, Key, Key); break;
    case 2: ASC->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Grant, Key, Key); break;
    default: ASC->ServerSetReplicatedEventWithPayload(EAbilityGenericReplicatedEvent::GenericConfirm, Grant, Key, Key, FVector_NetQuantize100(FVector(17, 19, 23))); break;
    }
    return !Weak.IsValid() && NoData157(ASC, Grant, Key);
}
void End157(UProject_JAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Grant)
{
    if (auto* Ability = Ability157(ASC, Grant))
    {
        const auto Info = Ability->GetCurrentActivationInfo();
        ASC->CallServerEndAbility(Grant, Info, Info.GetActivationPredictionKey());
    }
}
UGameplayAbility* Execution157(UProject_JAbilitySystemComponent* ASC, FGameplayAbilitySpecHandle Grant, FPredictionKey Key)
{
    const auto* Spec = ASC->FindAbilitySpecFromHandle(Grant);
    if (!Spec) return nullptr;
    const TArray<UGameplayAbility*> Instances = Spec->GetAbilityInstances();
    for (UGameplayAbility* Instance : Instances)
    {
        if (IsValid(Instance) && Instance->IsActive() && Instance->GetCurrentAbilitySpecHandle() == Grant
            && Instance->GetCurrentActivationInfo().GetActivationPredictionKey() == Key) return Instance;
    }
    return nullptr;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGASPayloadActiveAdmissionTest, "ProjectJ.MMO.CommandBudget.GASPayload.ActiveAdmission",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGASPayloadActiveAdmissionTest::RunTest(const FString&)
{
    FPayloadWorld157 Fixture; FPayloadOwner157 Owner;
    if (!TestTrue(TEXT("Native session and nonlocal GAS owner"), Fixture.Prepare(Owner, 1000, 1))) return false;
    auto* ASC = Owner.ASC; const auto Key = Key157(501);
    const auto Removed = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    ASC->ClearAbility(Removed);
    for (int32 Index = 0; Index < 32; ++Index)
    {
        const auto Grant = Index % 3 == 0 ? FGameplayAbilitySpecHandle() : (Index % 3 == 1 ? Removed : Owner.Grant);
        TestTrue(TEXT("Finite unknown/removed/inactive/future key storm retains no native payload or event"),
            Negative157(ASC, Grant, Key157(int16(1000 + Index)), Index % 4));
    }
    TestTrue(TEXT("An inactive key is refused without poisoning its future native activation"), Negative157(ASC, Owner.Grant, Key, 0));
    bool bPreCountActive = false; TWeakPtr<FGameplayAbilityTargetData> First;
    const auto Activation = ASC->AbilityActivatedCallbacks.AddLambda([&](UGameplayAbility* Ability)
    {
        if (!Ability || Ability->GetCurrentAbilitySpecHandle() != Owner.Grant) return;
        const auto* Spec = ASC->FindAbilitySpecFromHandle(Owner.Grant);
        bPreCountActive = Ability->IsActive() && Spec && !Spec->IsActive();
        SendPayload157(ASC, Owner.Grant, Key, 111, First);
    });
    ON_SCOPE_EXIT { ASC->AbilityActivatedCallbacks.Remove(Activation); ASC->CancelAbilityHandle(Owner.Grant); };
    ASC->CallServerTryActivateAbility(Owner.Grant, true, Key);
    if (!TestTrue(TEXT("Actual instance is active before native Spec.ActiveCount increments"), bPreCountActive && Active157(ASC, Owner.Grant))) return false;
    ASC->AbilityActivatedCallbacks.Remove(Activation);
    TestTrue(TEXT("Payload sent by the real activation callback is retained before a listener exists"), First.IsValid());
    TestTrue(TEXT("Native target flag is visible through Find-only replay"), ASC->CallReplicatedTargetDataDelegatesIfSet(Owner.Grant, Key));
    int32 Deliveries = 0; double Expected = 111; bool bValues = true;
    auto& DataDelegate = ASC->AbilityTargetDataSetDelegate(Owner.Grant, Key); // Only after actual admission.
    const auto DataHandle = DataDelegate.AddLambda([&](const FGameplayAbilityTargetDataHandle& Data, FGameplayTag Tag)
    { ++Deliveries; bValues &= PayloadValue157(Data, Expected) && Tag == FProject_JGameplayTags::Get().InputTag_Weapon_LMB; });
    bool bBound = true;
    ON_SCOPE_EXIT { if (bBound) DataDelegate.Remove(DataHandle); };
    ASC->CallReplicatedTargetDataDelegatesIfSet(Owner.Grant, Key);
    Expected = 222; TWeakPtr<FGameplayAbilityTargetData> Second;
    SendPayload157(ASC, Owner.Grant, Key, Expected, Second);
    TestTrue(TEXT("Native overwrite releases the previous shared payload"), !First.IsValid() && Second.IsValid());
    FPredictionKey Alias = Key; Alias.Base = Key.Current - 1;
    Expected = 333; TWeakPtr<FGameplayAbilityTargetData> Third;
    SendPayload157(ASC, Owner.Grant, Alias, Expected, Third);
    TestTrue(TEXT("Same native Current key preserves ordered payloads without a second execution slot"), bValues && Deliveries == 3 && !Second.IsValid() && Third.IsValid());
    ASC->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Owner.Grant, Key, Key);
    ASC->ServerSetReplicatedEventWithPayload(EAbilityGenericReplicatedEvent::GenericConfirm, Owner.Grant, Key, Key, FVector_NetQuantize100(FVector(12, 34, 56)));
    const auto Generic = ASC->GetReplicatedDataOfGenericReplicatedEvent(EAbilityGenericReplicatedEvent::GenericConfirm, Owner.Grant, Key);
    TestTrue(TEXT("Valid active generic and vector events retain their actual native values"),
        ASC->GetReplicatedDataOfGenericReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Owner.Grant, Key).bTriggered
        && Generic.bTriggered && Generic.VectorPayload.Equals(FVector(12, 34, 56)));
    ASC->ConsumeAllReplicatedData(Owner.Grant, Key);
    TestTrue(TEXT("Supported ConsumeAll releases payload values and flags"), !Third.IsValid() && NoData157(ASC, Owner.Grant, Key));
    DataDelegate.Remove(DataHandle); bBound = false; // Before native End removes/reuses the cache object.
    End157(ASC, Owner.Grant);
    TestFalse(TEXT("Exact native End finishes Sprint"), Active157(ASC, Owner.Grant));
    for (int32 Route = 0; Route < 4; ++Route) TestTrue(TEXT("Ended tuples cannot be resurrected"), Negative157(ASC, Owner.Grant, Key, Route));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGASPayloadCleanupTest, "ProjectJ.MMO.CommandBudget.GASPayload.Cleanup",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGASPayloadCleanupTest::RunTest(const FString&)
{
    FPayloadWorld157 Fixture; FPayloadOwner157 Owner;
    if (!TestTrue(TEXT("Native owner with finite new-work credits"), Fixture.Prepare(Owner, 4))) return false;
    auto* ASC = Owner.ASC; const auto Key = Key157(601);
    ASC->CallServerTryActivateAbility(Owner.Grant, true, Key);
    auto* Ability = Ability157(ASC, Owner.Grant);
    if (!TestTrue(TEXT("Actual native predicted Sprint has native remote-end permission"), Active157(ASC, Owner.Grant)
        && Ability && Ability->GetCurrentActivationInfo().bCanBeEndedByOtherInstance)) return false;
    TWeakPtr<FGameplayAbilityTargetData> Payload;
    SendPayload157(ASC, Owner.Grant, Key, 444, Payload);
    TestTrue(TEXT("Native cache holds the real shared payload"), Payload.IsValid());
    auto* Budget = Owner.State->FindComponentByClass<UProject_JCommandBudgetComponent>();
    for (int32 Index = 0; Index < 4; ++Index) Budget->TryConsume();
    TestTrue(TEXT("Actual shared new-work credits are exhausted"), Budget->GetRejectedCount() > 0);
    const TStrongObjectPtr<UAbilityTask_WaitInputRelease> Release(UAbilityTask_WaitInputRelease::WaitInputRelease(Ability, false));
    Release->ReadyForActivation();
    ASC->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Owner.Grant, Key, Key);
    TestTrue(TEXT("Real native waiting release task remains deliverable at saturation"), Release->GetState() == EGameplayTaskState::Finished);
    TestFalse(TEXT("Native task consumes its release flag"), ASC->GetReplicatedDataOfGenericReplicatedEvent(EAbilityGenericReplicatedEvent::InputReleased, Owner.Grant, Key).bTriggered);
    int32 Cancels = 0;
    auto& CancelDelegate = ASC->AbilityTargetDataCancelledDelegate(Owner.Grant, Key);
    const auto CancelHandle = CancelDelegate.AddLambda([&] { ++Cancels; ASC->ConsumeClientReplicatedTargetData(Owner.Grant, Key); });
    ASC->ServerSetReplicatedTargetDataCancelled(Owner.Grant, Key, Key);
    CancelDelegate.Remove(CancelHandle);
    TestTrue(TEXT("Exact admitted cancellation delivers and releases payload under exhausted credits"), Cancels == 1 && !Payload.IsValid());
    TWeakPtr<FGameplayAbilityTargetData> EndingPayload;
    SendPayload157(ASC, Owner.Grant, Key, 555, EndingPayload);
    TestTrue(TEXT("An already admitted execution keeps accepting ordered data without new-work drops"), EndingPayload.IsValid());
    bool bEndCallback = false, bStillActiveAtCallback = false, bLateDropped = false;
    const auto EndHandle = Ability->OnGameplayAbilityEnded.AddLambda([&](UGameplayAbility* Ending)
    {
        bEndCallback = true; bStillActiveAtCallback = Ending && Ending->IsActive();
        ASC->ServerSetReplicatedEventWithPayload(EAbilityGenericReplicatedEvent::GenericConfirm, Owner.Grant, Key, Key, FVector_NetQuantize100(FVector(77, 88, 99)));
        bLateDropped = !ASC->GetReplicatedDataOfGenericReplicatedEvent(EAbilityGenericReplicatedEvent::GenericConfirm, Owner.Grant, Key).bTriggered;
    });
    ON_SCOPE_EXIT { Ability->OnGameplayAbilityEnded.Remove(EndHandle); ASC->CancelAbilityHandle(Owner.Grant); };
    End157(ASC, Owner.Grant);
    TestTrue(TEXT("Actual early End callback cannot recreate payload while native instance still reports active"), bEndCallback && bStillActiveAtCallback && bLateDropped);
    TestTrue(TEXT("Native End releases actual payload, flags and Sprint tag"), !EndingPayload.IsValid() && !Active157(ASC, Owner.Grant)
        && ASC->GetTagCount(FProject_JGameplayTags::Get().State_Movement_Sprinting) == 0 && NoData157(ASC, Owner.Grant, Key));
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGASPayloadCapacityReentryTest, "ProjectJ.MMO.CommandBudget.GASPayload.CapacityReentry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGASPayloadCapacityReentryTest::RunTest(const FString&)
{
    FPayloadWorld157 Fixture; FPayloadOwner157 Owner;
    if (!TestTrue(TEXT("Native client execution capacity setup"), Fixture.Prepare(Owner, 1000, 2, true))) return false;
    auto* ASC = Owner.ASC;
    const auto Second = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    const auto Third = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1));
    const auto Trusted = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGASPayloadTrustedAbility::StaticClass(), 1));
    const auto FirstKey = Key157(701), SecondKey = Key157(702), ThirdKey = Key157(703);
    int32 ThirdStarts = 0; bool bNested = false;
    const auto Activated = ASC->AbilityActivatedCallbacks.AddLambda([&](UGameplayAbility* Ability)
    {
        if (!Ability) return;
        if (Ability->GetCurrentAbilitySpecHandle() == Third) ++ThirdStarts;
        if (Ability->GetCurrentAbilitySpecHandle() == Owner.Grant && !bNested)
        {
            bNested = true;
            ASC->CallServerTryActivateAbility(Second, true, SecondKey);
            ASC->CallServerTryActivateAbility(Third, true, ThirdKey);
        }
    });
    ON_SCOPE_EXIT { ASC->AbilityActivatedCallbacks.Remove(Activated); };
    ASC->CallServerTryActivateAbility(Owner.Grant, true, FirstKey);
    if (!TestTrue(TEXT("Activation callback reservation bounds actual nested client activations"), bNested && Active157(ASC, Owner.Grant) && Active157(ASC, Second)
        && !Active157(ASC, Third) && ThirdStarts == 0)) return false;
    if (!TestTrue(TEXT("Trusted native server activation stays outside the client-origin capacity"), ASC->TryActivateAbility(Trusted) && Active157(ASC, Trusted))) return false;
    auto* TrustedAbility = Ability157(ASC, Trusted);
    if (!TestNotNull(TEXT("Actual trusted instance"), TrustedAbility)) return false;
    const auto TrustedKey = TrustedAbility->GetCurrentActivationInfo().GetActivationPredictionKey();
    TWeakPtr<FGameplayAbilityTargetData> TrustedPayload;
    SendPayload157(ASC, Trusted, TrustedKey, 666, TrustedPayload);
    TestTrue(TEXT("Actual trusted execution still has exact payload admission at full client capacity"), TrustedPayload.IsValid());
    ASC->CancelAbilityHandle(Trusted);
    TestTrue(TEXT("Trusted execution retirement releases only its payload"), !TrustedPayload.IsValid() && Active157(ASC, Owner.Grant) && Active157(ASC, Second));
    auto* FirstAbility = Ability157(ASC, Owner.Grant);
    if (!TestNotNull(TEXT("Actual first client instance"), FirstAbility)) return false;
    bool bEndingAttempt = false;
    const auto Ending = FirstAbility->OnGameplayAbilityEnded.AddLambda([&](UGameplayAbility*)
    { bEndingAttempt = true; ASC->CallServerTryActivateAbility(Third, true, ThirdKey); });
    ASC->CancelAbilityHandle(Owner.Grant);
    FirstAbility->OnGameplayAbilityEnded.Remove(Ending);
    TestTrue(TEXT("Ending execution retains its capacity until native NotifyAbilityEnded"), bEndingAttempt && ThirdStarts == 0 && !Active157(ASC, Third));
    ASC->CallServerTryActivateAbility(Third, true, Key157(704));
    TestTrue(TEXT("Completed native End permits a fresh client activation without any clock reset"), ThirdStarts == 1 && Active157(ASC, Third));

    // Separate current owner: callback rebind and exact native key reuse cannot let old cleanup erase the new row.
    FPayloadOwner157 Reentry;
    if (!TestTrue(TEXT("Separate callback epoch owner"), Fixture.Prepare(Reentry))) return false;
    auto* R = Reentry.ASC; const auto Key = Key157(801);
    R->CallServerTryActivateAbility(Reentry.Grant, true, Key);
    const TStrongObjectPtr<APawn> Replacement(Fixture.World->SpawnActor<APawn>());
    if (!TestNotNull(TEXT("Real replacement Pawn"), Replacement.Get())) return false;
    TWeakPtr<FGameplayAbilityTargetData> OldPayload, NewPayload; bool bRebound = false;
    auto& DataDelegate = R->AbilityTargetDataSetDelegate(Reentry.Grant, Key);
    FDelegateHandle DataHandle; bool bDataBound = true;
    ON_SCOPE_EXIT { if (bDataBound) DataDelegate.Remove(DataHandle); };
    DataHandle = DataDelegate.AddLambda([&](const FGameplayAbilityTargetDataHandle&, FGameplayTag)
    {
        if (bRebound) return;
        DataDelegate.Remove(DataHandle); bDataBound = false; // Detach before retirement/reuse.
        bRebound = true; R->CancelAbilityHandle(Reentry.Grant);
        Reentry.Controller->Possess(Replacement.Get()); R->InitAbilityActorInfo(Reentry.State, Replacement.Get());
        R->CallServerTryActivateAbility(Reentry.Grant, true, Key);
        SendPayload157(R, Reentry.Grant, Key, 888, NewPayload);
    });
    SendPayload157(R, Reentry.Grant, Key, 777, OldPayload);
    TestTrue(TEXT("Actual callback changes recipient and starts its new native execution"), bRebound && R->GetAvatarActor() == Replacement.Get() && Active157(R, Reentry.Grant));
    TestTrue(TEXT("Old exact cleanup releases old payload without clearing the new same-key row"), !OldPayload.IsValid() && NewPayload.IsValid());
    if (bDataBound) { DataDelegate.Remove(DataHandle); bDataBound = false; }
    R->CancelAbilityHandle(Reentry.Grant);
    TestTrue(TEXT("New incarnation's own retirement releases its payload"), !NewPayload.IsValid() && NoData157(R, Reentry.Grant, Key));

    // Retiring the native shared payload can itself install a newer actor-info tuple.
    FPayloadOwner157 DestructorOwner;
    if (!TestTrue(TEXT("Actual payload destructor reentry owner"), Fixture.Prepare(DestructorOwner, 1000, 2, true))) return false;
    auto* D = DestructorOwner.ASC;
    const auto ReplacementGrant = D->GiveAbility(FGameplayAbilitySpec(UProject_JGASPayloadTrustedAbility::StaticClass(), 1));
    const auto DestructorKey = Key157(851);
    D->CallServerTryActivateAbility(DestructorOwner.Grant, true, DestructorKey);
    const TStrongObjectPtr<APawn> DestructorReplacement(Fixture.World->SpawnActor<APawn>());
    if (!TestNotNull(TEXT("Destructor callback's actual replacement Pawn"), DestructorReplacement.Get())) return false;
    const auto ReleaseState = MakeShared<FPayloadReleaseState157>();
    ON_SCOPE_EXIT { ReleaseState->bEnabled = false; ReleaseState->OnRelease = nullptr; };
    bool bTrustedReplacement = false;
    ReleaseState->OnRelease = [&]
    {
        D->CancelAbilityHandle(DestructorOwner.Grant);
        DestructorOwner.Controller->Possess(DestructorReplacement.Get());
        D->InitAbilityActorInfo(DestructorOwner.State, DestructorReplacement.Get());
        bTrustedReplacement = D->TryActivateAbility(ReplacementGrant);
    };
    TWeakPtr<FGameplayAbilityTargetData> ReleasedData;
    {
        auto* Data = new FPayloadReleaseData157;
        Data->ReleaseState = ReleaseState;
        FGameplayAbilityTargetDataHandle Handle; Handle.Add(Data); ReleasedData = Handle.Data[0];
        D->ServerSetReplicatedTargetData(DestructorOwner.Grant, DestructorKey, Handle, FGameplayTag(), DestructorKey);
    }
    if (!TestTrue(TEXT("Native cache is the last shared payload owner before outer Clear"), ReleasedData.IsValid())) return false;
    D->ClearActorInfo();
    if (!TestTrue(TEXT("Payload destruction installs a real newer tuple that outer Clear must preserve"), ReleaseState->Calls == 1
        && !ReleasedData.IsValid() && bTrustedReplacement && D->GetOwnerActor() == DestructorOwner.State
        && D->GetAvatarActor() == DestructorReplacement.Get() && Active157(D, ReplacementGrant))) return false;
    auto* ReplacementAbility = Ability157(D, ReplacementGrant);
    if (!TestNotNull(TEXT("New trusted execution published during retirement survives"), ReplacementAbility)) return false;
    const auto ReplacementKey = ReplacementAbility->GetCurrentActivationInfo().GetActivationPredictionKey();
    TWeakPtr<FGameplayAbilityTargetData> ReplacementPayload;
    SendPayload157(D, ReplacementGrant, ReplacementKey, 889, ReplacementPayload);
    TestTrue(TEXT("New exact execution still admits payload after outer Clear returns"), ReplacementPayload.IsValid()
        && D->CallReplicatedTargetDataDelegatesIfSet(ReplacementGrant, ReplacementKey));
    D->CancelAbilityHandle(ReplacementGrant);
    TestTrue(TEXT("New tuple's own normal End releases its payload"), !ReplacementPayload.IsValid()
        && NoData157(D, ReplacementGrant, ReplacementKey));

    FPayloadOwner157 Immediate;
    if (!TestTrue(TEXT("Native synchronous-end batch owner"), Fixture.Prepare(Immediate))) return false;
    const auto ImmediateGrant = Immediate.ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGASPayloadImmediateAbility::StaticClass(), 1));
    const auto ImmediateKey = Key157(901);
    FServerAbilityRPCBatch Batch; Batch.AbilitySpecHandle = ImmediateGrant; Batch.PredictionKey = ImmediateKey; Batch.Started = true; Batch.InputPressed = true;
    Immediate.ASC->ServerAbilityRPCBatch(Batch);
    TestTrue(TEXT("A real immediate End during activation leaves no batch target cache resurrection"), !Active157(Immediate.ASC, ImmediateGrant)
        && NoData157(Immediate.ASC, ImmediateGrant, ImmediateKey));
    return true; // Native dispatch/resource ownership only;156 wire compatibility is reused, not a new serialization proof.
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGASPayloadConcurrentInstanceTest, "ProjectJ.MMO.CommandBudget.GASPayload.ConcurrentInstances",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGASPayloadConcurrentInstanceTest::RunTest(const FString&)
{
    FPayloadWorld157 Fixture; FPayloadOwner157 Owner;
    if (!TestTrue(TEXT("Native per-execution owner at client cap2"), Fixture.Prepare(Owner, 1000, 2))) return false;
    auto* ASC = Owner.ASC;
    const auto Grant = ASC->GiveAbility(FGameplayAbilitySpec(UProject_JGASPayloadConcurrentAbility::StaticClass(), 1));
    const auto FirstKey = Key157(1101), SecondKey = Key157(1102), RefusedKey = Key157(1103);
    ASC->CallServerTryActivateAbility(Grant, true, FirstKey);
    ASC->CallServerTryActivateAbility(Grant, true, SecondKey);
    const TStrongObjectPtr<UGameplayAbility> First(Execution157(ASC, Grant, FirstKey));
    const TStrongObjectPtr<UGameplayAbility> Second(Execution157(ASC, Grant, SecondKey));
    if (!TestTrue(TEXT("One native spec has two distinct actual active instances/keys"), First.IsValid() && Second.IsValid() && First.Get() != Second.Get())) return false;
    ASC->CallServerTryActivateAbility(Grant, true, RefusedKey);
    TestNull(TEXT("Third client key is denied without substituting another primary instance"), Execution157(ASC, Grant, RefusedKey));
    TWeakPtr<FGameplayAbilityTargetData> FirstData, SecondData;
    SendPayload157(ASC, Grant, FirstKey, 1101, FirstData);
    SendPayload157(ASC, Grant, SecondKey, 1102, SecondData);
    TestTrue(TEXT("Both exact active keys retain their separate native payloads"), FirstData.IsValid() && SecondData.IsValid()
        && ASC->CallReplicatedTargetDataDelegatesIfSet(Grant, FirstKey) && ASC->CallReplicatedTargetDataDelegatesIfSet(Grant, SecondKey));
    bool bFirstEnding = false, bLateDropped = false;
    const auto EndHandle = First->OnGameplayAbilityEnded.AddLambda([&](UGameplayAbility* Ending)
    {
        bFirstEnding = Ending && Ending->IsActive();
        ASC->ServerSetReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Grant, FirstKey, FirstKey);
        bLateDropped = !ASC->GetReplicatedDataOfGenericReplicatedEvent(EAbilityGenericReplicatedEvent::InputPressed, Grant, FirstKey).bTriggered;
    });
    ON_SCOPE_EXIT { First->OnGameplayAbilityEnded.Remove(EndHandle); };
    const auto FirstInfo = First->GetCurrentActivationInfo();
    if (!TestTrue(TEXT("First instance has real native end permission"), FirstInfo.bCanBeEndedByOtherInstance)) return false;
    ASC->CallServerEndAbility(Grant, FirstInfo, FirstKey);
    TestTrue(TEXT("Ending first live instance blocks its late callback and releases only its exact key"), bFirstEnding && bLateDropped
        && !FirstData.IsValid() && NoData157(ASC, Grant, FirstKey) && !Execution157(ASC, Grant, FirstKey)
        && SecondData.IsValid() && Execution157(ASC, Grant, SecondKey) == Second.Get());
    const auto SecondInfo = Second->GetCurrentActivationInfo();
    ASC->CallServerEndAbility(Grant, SecondInfo, SecondKey);
    TestTrue(TEXT("Second instance's own native End releases its remaining exact payload"), !SecondData.IsValid()
        && !Execution157(ASC, Grant, SecondKey) && NoData157(ASC, Grant, SecondKey));
    return true;
}
#endif
