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
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJGASSharedCommandBudgetTest, "ProjectJ.MMO.CommandBudget.GASClientEntry",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJGASSharedCommandBudgetTest::RunTest(const FString&)
{
    const TStrongObjectPtr<UGameInstance> BudgetInstance(NewObject<UGameInstance>(GEngine));
    BudgetInstance->InitializeStandalone();
    UWorld* BudgetWorld = BudgetInstance->GetWorld();
    ON_SCOPE_EXIT
    {
        BudgetWorld->EndPlay(EEndPlayReason::LevelTransition);
        BudgetInstance->Shutdown(); BudgetWorld->DestroyWorld(false); GEngine->DestroyWorldContext(BudgetWorld);
    };
    FURL BudgetURL; BudgetURL.AddOption(TEXT("game=/Script/Project_J.Project_JNetworkLoadGameMode"));
    if (!TestTrue(TEXT("Use native authority installation"), BudgetWorld->SetGameMode(BudgetURL))) return false;
    BudgetWorld->InitializeActorsForPlay(BudgetURL); BudgetWorld->GetWorldSettings()->NotifyBeginPlay();
    auto* BudgetMode = Cast<AProject_JNetworkLoadGameMode>(BudgetWorld->GetAuthGameMode());
    if (!TestNotNull(TEXT("Existing concrete mode"), BudgetMode)) return false;
    BudgetMode->bRequireTrustedSessions = true;

    auto PrepareBudgetOwner = [&](bool bLocalController, int32 Limit, AProject_JPlayerState*& OutState,
        UProject_JAbilitySystemComponent*& OutASC, APawn*& OutAvatar, FGameplayAbilitySpecHandle& OutGrant)
    {
        auto* BudgetPC = BudgetWorld->SpawnActor<APlayerController>();
        if (!BudgetPC) return false;
        // Native controller classification only: no roles, CDOs, net-driver or end-permission flags are changed.
        BudgetPC->SetPlayer(NewObject<UPlayer>(BudgetInstance.Get()));
        if (bLocalController) BudgetPC->SetAsLocalPlayerController();
        if (!BudgetPC->GetPlayerState<AProject_JPlayerState>()) BudgetPC->InitPlayerState();
        BudgetMode->PostLogin(BudgetPC);
        OutState = BudgetPC->GetPlayerState<AProject_JPlayerState>();
        if (!OutState || BudgetPC->GetPawn()) return false;
        ProjectJ::MMO::FServerSessionAttempt BudgetAttempt;
        ProjectJ::MMO::FServerSessionLease BudgetLease;
        if (!BudgetMode->BeginTrustedSession(BudgetPC, FProject_JAccountId::NewId(), FProject_JCharacterId::NewId(), BudgetAttempt)
            || !BudgetMode->CompleteTrustedSession(BudgetAttempt, true, BudgetLease)) return false;
        OutAvatar = BudgetWorld->SpawnActor<APawn>();
        if (!OutAvatar) return false;
        BudgetPC->Possess(OutAvatar);
        OutASC = OutState->GetProjectJAbilitySystemComponent();
        if (!OutASC) return false;
        OutASC->InitAbilityActorInfo(OutState, OutAvatar);
        auto* OwnerBudget = OutState->FindComponentByClass<UProject_JCommandBudgetComponent>();
        if (!OwnerBudget || BudgetPC->IsLocalController() != bLocalController || !OutState->CanAcceptSessionCommand()) return false;
        OwnerBudget->CommandsPerSecond = Limit; // Per-fixture instance, never a CDO/global policy or a window reset.
        FGameplayAbilitySpec BudgetSpec(UProject_JGameplayAbility_Sprint::StaticClass(), 1);
        BudgetSpec.GetDynamicSpecSourceTags().AddTag(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
        OutGrant = OutASC->GiveAbility(BudgetSpec);
        return OutGrant.IsValid();
    };
    auto IsBudgetAbilityActive = [](UProject_JAbilitySystemComponent* InASC, FGameplayAbilitySpecHandle InGrant)
    { const auto* BudgetSpec = InASC->FindAbilitySpecFromHandle(InGrant); return BudgetSpec && BudgetSpec->IsActive(); };
    auto MakeBudgetKey = [](int16 Value) { FPredictionKey BudgetKey; BudgetKey.Current = Value; return BudgetKey; };

    {
        AProject_JPlayerState* SharedState = nullptr; UProject_JAbilitySystemComponent* SharedASC = nullptr;
        APawn* SharedAvatar = nullptr; FGameplayAbilitySpecHandle SharedGrant;
        if (!TestTrue(TEXT("Prepare accepted native local owner"), PrepareBudgetOwner(true, 3, SharedState, SharedASC, SharedAvatar, SharedGrant))) return false;
        ON_SCOPE_EXIT { SharedASC->CancelAbilityHandle(SharedGrant); };
        auto* SharedBudget = SharedState->FindComponentByClass<UProject_JCommandBudgetComponent>();
        TestTrue(TEXT("Another client command consumes the same owner budget"), UProject_JCommandBudgetComponent::AdmitClientCommand(SharedState));
        SharedASC->CallServerTryActivateAbility(SharedGrant, true, MakeBudgetKey(101));
        if (!TestTrue(TEXT("Native standalone activation consumes one credit and runs Sprint"), IsBudgetAbilityActive(SharedASC, SharedGrant))) return false;
        SharedASC->CancelAbilityHandle(SharedGrant);
        // This ordinary intent RPC must be accounted even when the native APawn has no combat receiver.
        // Successful skill/30-per-avatar routing remains covered by the existing input tests.
        SharedASC->ServerSendCombatInputEvent(SharedAvatar, FProject_JGameplayTags::Get().InputTag_Weapon_LMB, 0.0, 1);
        SharedASC->CallServerTryActivateAbility(SharedGrant, true, MakeBudgetKey(102));
        TestFalse(TEXT("Activation cannot bypass the shared command and intent credits"), IsBudgetAbilityActive(SharedASC, SharedGrant));
        TestEqual(TEXT("Exactly the fourth request is over the per-instance cap"), SharedBudget->GetRejectedCount(), int64(1));
        TestTrue(TEXT("Trusted direct local activation remains outside client accounting"), SharedASC->TryActivateAbility(SharedGrant));
        SharedASC->AbilityInputTagReleased(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
        TestFalse(TEXT("Real Sprint input release is never dropped for exhausted client budget"), IsBudgetAbilityActive(SharedASC, SharedGrant));
        TestEqual(TEXT("Trusted activation and release do not consume client credits"), SharedBudget->GetRejectedCount(), int64(1));
    }
    {
        AProject_JPlayerState* ReentryState = nullptr; UProject_JAbilitySystemComponent* ReentryASC = nullptr;
        APawn* ReentryAvatar = nullptr; FGameplayAbilitySpecHandle ReentryGrant;
        if (!TestTrue(TEXT("Prepare native non-local controller for predicted request"), PrepareBudgetOwner(false, 1, ReentryState, ReentryASC, ReentryAvatar, ReentryGrant))) return false;
        const FPredictionKey ReentryKey = MakeBudgetKey(201);
        int32 ReentryActivations = 0; bool bReenteredBudgetRequest = false; FDelegateHandle ReentryHandle;
        ON_SCOPE_EXIT { ReentryASC->AbilityActivatedCallbacks.Remove(ReentryHandle); ReentryASC->CancelAbilityHandle(ReentryGrant); };
        ReentryHandle = ReentryASC->AbilityActivatedCallbacks.AddLambda([&](UGameplayAbility* Activated)
        {
            if (!Activated || Activated->GetCurrentAbilitySpecHandle() != ReentryGrant) return;
            ++ReentryActivations;
            if (!bReenteredBudgetRequest)
            {
                bReenteredBudgetRequest = true;
                ReentryASC->CallServerTryActivateAbility(ReentryGrant, true, ReentryKey);
            }
        });
        FServerAbilityRPCBatch ReentryBatch; ReentryBatch.AbilitySpecHandle = ReentryGrant; ReentryBatch.PredictionKey = ReentryKey;
        ReentryBatch.Started = true; ReentryBatch.InputPressed = true;
        ReentryASC->ServerAbilityRPCBatch(ReentryBatch);
        TestTrue(TEXT("The real activation callback reentered the exact handle and key"), bReenteredBudgetRequest);
        TestEqual(TEXT("One outer batch credit reaches exactly one native activation"), ReentryActivations, 1);
        TestEqual(TEXT("The exact reentrant request cannot reuse the already consumed voucher"),
            ReentryState->FindComponentByClass<UProject_JCommandBudgetComponent>()->GetRejectedCount(), int64(1));
        // Standalone client rejection can synchronously end the original instance; its active state is not a wire claim.
    }
    {
        AProject_JPlayerState* EndingState = nullptr; UProject_JAbilitySystemComponent* EndingASC = nullptr;
        APawn* EndingAvatar = nullptr; FGameplayAbilitySpecHandle EndingGrant;
        if (!TestTrue(TEXT("Prepare native predicted owner for actual remote-end validation"), PrepareBudgetOwner(false, 1, EndingState, EndingASC, EndingAvatar, EndingGrant))) return false;
        const FPredictionKey EndingKey = MakeBudgetKey(301);
        int32 EndingActivations = 0, EndingData = 0, EndingEnds = 0; bool bEndingWasCancelled = false;
        FDelegateHandle EndingActivationHandle, EndingDataHandle, EndingEndHandle;
        ON_SCOPE_EXIT
        {
            EndingASC->AbilityActivatedCallbacks.Remove(EndingActivationHandle);
            EndingASC->AbilityTargetDataSetDelegate(EndingGrant, EndingKey).Remove(EndingDataHandle);
            EndingASC->OnAbilityEnded.Remove(EndingEndHandle); EndingASC->CancelAbilityHandle(EndingGrant);
        };
        EndingActivationHandle = EndingASC->AbilityActivatedCallbacks.AddLambda([&](UGameplayAbility* Activated)
        { if (Activated && Activated->GetCurrentAbilitySpecHandle() == EndingGrant) ++EndingActivations; });
        EndingDataHandle = EndingASC->AbilityTargetDataSetDelegate(EndingGrant, EndingKey).AddLambda(
            [&](const FGameplayAbilityTargetDataHandle&, FGameplayTag) { ++EndingData; });
        EndingEndHandle = EndingASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Ended)
        { if (Ended.AbilitySpecHandle == EndingGrant) { ++EndingEnds; bEndingWasCancelled = Ended.bWasCancelled; } });
        FServerAbilityRPCBatch EndingBatch; EndingBatch.AbilitySpecHandle = EndingGrant; EndingBatch.PredictionKey = EndingKey;
        EndingBatch.Started = true; EndingBatch.InputPressed = true;
        EndingASC->ServerAbilityRPCBatch(EndingBatch);
        if (!TestTrue(TEXT("One-credit batch runs the actual native Sprint"), IsBudgetAbilityActive(EndingASC, EndingGrant))) return false;
        const auto* EndingSpec = EndingASC->FindAbilitySpecFromHandle(EndingGrant);
        const auto* EndingAbility = EndingSpec ? EndingSpec->GetPrimaryInstance() : nullptr;
        if (!TestTrue(TEXT("Native predicted activation sets real remote-end permission"), EndingAbility && EndingAbility->GetCurrentActivationInfo().bCanBeEndedByOtherInstance)) return false;
        TestEqual(TEXT("The admitted native batch forwards its target data once"), EndingData, 1);
        TestEqual(TEXT("Batch activation is not charged again in its nested entry"),
            EndingState->FindComponentByClass<UProject_JCommandBudgetComponent>()->GetRejectedCount(), int64(0));
        FServerAbilityRPCBatch WrongEnd = EndingBatch; WrongEnd.PredictionKey = MakeBudgetKey(302); WrongEnd.Ended = true;
        EndingASC->ServerAbilityRPCBatch(WrongEnd);
        TestTrue(TEXT("Native End with a different key cannot end the current execution"), IsBudgetAbilityActive(EndingASC, EndingGrant));
        EndingBatch.Ended = true;
        EndingASC->ServerAbilityRPCBatch(EndingBatch);
        TestFalse(TEXT("Exhausted-budget exact native End still ends Sprint"), IsBudgetAbilityActive(EndingASC, EndingGrant));
        TestEqual(TEXT("Rejected batches cannot start another execution"), EndingActivations, 1);
        TestEqual(TEXT("Rejected batches cannot forward even empty target data"), EndingData, 1);
        TestTrue(TEXT("The actual native End callback happens once and is not cancellation"), EndingEnds == 1 && !bEndingWasCancelled);
    }
    {
        AProject_JPlayerState* EpochState = nullptr; UProject_JAbilitySystemComponent* EpochASC = nullptr;
        APawn* EpochAvatar = nullptr; FGameplayAbilitySpecHandle EpochGrant;
        if (!TestTrue(TEXT("Prepare callback recipient ownership case"), PrepareBudgetOwner(false, 1, EpochState, EpochASC, EpochAvatar, EpochGrant))) return false;
        const TStrongObjectPtr<APawn> EpochReplacement(BudgetWorld->SpawnActor<APawn>());
        if (!TestNotNull(TEXT("Actual replacement recipient"), EpochReplacement.Get())) return false;
        const FPredictionKey EpochKey = MakeBudgetKey(401);
        int32 EpochData = 0, EpochEnds = 0; bool bReboundEpoch = false;
        FDelegateHandle EpochActivationHandle, EpochDataHandle, EpochEndHandle;
        ON_SCOPE_EXIT
        {
            EpochASC->AbilityActivatedCallbacks.Remove(EpochActivationHandle);
            EpochASC->AbilityTargetDataSetDelegate(EpochGrant, EpochKey).Remove(EpochDataHandle);
            EpochASC->OnAbilityEnded.Remove(EpochEndHandle); EpochASC->CancelAbilityHandle(EpochGrant);
        };
        EpochActivationHandle = EpochASC->AbilityActivatedCallbacks.AddLambda([&](UGameplayAbility* Activated)
        {
            if (Activated && Activated->GetCurrentAbilitySpecHandle() == EpochGrant && !bReboundEpoch)
            { bReboundEpoch = true; EpochASC->InitAbilityActorInfo(EpochState, EpochReplacement.Get()); }
        });
        EpochDataHandle = EpochASC->AbilityTargetDataSetDelegate(EpochGrant, EpochKey).AddLambda(
            [&](const FGameplayAbilityTargetDataHandle&, FGameplayTag) { ++EpochData; });
        EpochEndHandle = EpochASC->OnAbilityEnded.AddLambda([&](const FAbilityEndedData& Ended)
        { if (Ended.AbilitySpecHandle == EpochGrant) ++EpochEnds; });
        FServerAbilityRPCBatch EpochBatch; EpochBatch.AbilitySpecHandle = EpochGrant; EpochBatch.PredictionKey = EpochKey;
        EpochBatch.Started = true; EpochBatch.InputPressed = true; EpochBatch.Ended = true;
        EpochASC->ServerAbilityRPCBatch(EpochBatch);
        TestTrue(TEXT("Actual activation callback publishes a newer native actor-info recipient"), bReboundEpoch && EpochASC->GetAvatarActor() == EpochReplacement.Get());
        TestEqual(TEXT("Old batch target data is not delivered to the new recipient"), EpochData, 0);
        TestEqual(TEXT("Old batch End does not end the newer recipient"), EpochEnds, 0);
        TestTrue(TEXT("Newer recipient's native execution survives the old batch"), IsBudgetAbilityActive(EpochASC, EpochGrant));
        FGameplayAbilityActivationInfo EpochEndInfo; EpochEndInfo.ServerSetActivationPredictionKey(EpochKey);
        EpochASC->CallServerEndAbility(EpochGrant, EpochEndInfo, EpochKey);
        TestTrue(TEXT("Ordinary exact native End outside the old batch is not budget dropped"), EpochEnds == 1 && !IsBudgetAbilityActive(EpochASC, EpochGrant));
    }
    return true; // Standalone native RPC dispatch/real GAS callbacks, not a socket or throughput claim.
}
#endif
