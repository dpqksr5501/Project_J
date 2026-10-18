#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Backend/Project_JHandoverManager.h"
#include "Tests/Project_JHandoverContractActor.h"
#include "UObject/StrongObjectPtr.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Animation/AnimMontage.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "HAL/FileManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3HandoverAgeTest,"ProjectJ.SourceAudit3.HandoverReplayLifetime",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAudit3HandoverAgeTest::RunTest(const FString&) {
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT {if(W->HasBegunPlay())W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
    TStrongObjectPtr<UGameInstance> GI(NewObject<UGameInstance>(GEngine));
    TStrongObjectPtr<UProject_JHandoverManager> Manager(NewObject<UProject_JHandoverManager>(GI.Get()));
    auto* Actor=W->SpawnActor<AProject_JHandoverContractActor>();int32 Applied=0;
    Actor->OnApply=[&](const TArray<uint8>&){++Applied;return true;};
    FProject_JHandoverEnvelope Envelope;
    TestTrue(TEXT("Default 30 second envelope builds"),Manager->BuildEnvelope(Actor,TEXT("LocalNode"),Envelope));
    TestEqual(TEXT("First valid envelope applies once"),Manager->ApplyEnvelopeChecked(Actor,Envelope),EProject_JHandoverApplyResult::Applied);
    TestEqual(TEXT("Live receipt rejects duplicate application"),Manager->ApplyEnvelopeChecked(Actor,Envelope),EProject_JHandoverApplyResult::AlreadyApplied);
    Envelope.Timestamp-=FTimespan::FromSeconds(61);Manager->AppliedTransferIds[Envelope.TransferId].AppliedAt-=FTimespan::FromSeconds(61);Manager->PruneReplayWindow();
    TestEqual(TEXT("Expired positive-age envelope remains rejected after receipt pruning"),Manager->ApplyEnvelopeChecked(Actor,Envelope),EProject_JHandoverApplyResult::Rejected);
    Manager->MaxEnvelopeAgeSeconds=0.f;
    TestFalse(TEXT("Zero-age configuration is explicitly unsupported"),Manager->HasValidTimingConfiguration());
    TestEqual(TEXT("Zero cannot reopen a replay after 61 seconds"),Manager->ApplyEnvelopeChecked(Actor,Envelope),EProject_JHandoverApplyResult::Rejected);
    TestEqual(TEXT("No repeated side effect"),Applied,1);
    // Accepted future timestamps require receipts to cover the full validity
    // horizon, including the existing three-second clock-skew allowance.
    TStrongObjectPtr<UProject_JHandoverManager> FutureManager(NewObject<UProject_JHandoverManager>(GI.Get()));
    FutureManager->MaxEnvelopeAgeSeconds=90.f;
    FProject_JHandoverEnvelope Future;TestTrue(TEXT("Positive nondefault horizon builds"),FutureManager->BuildEnvelope(Actor,TEXT("LocalNode"),Future));
    Future.Timestamp+=FTimespan::FromSeconds(2.5);
    TestEqual(TEXT("Existing future-skew policy accepts the envelope"),FutureManager->ApplyEnvelopeChecked(Actor,Future),EProject_JHandoverApplyResult::Applied);
    Future.Timestamp-=FTimespan::FromSeconds(91);FutureManager->AppliedTransferIds[Future.TransferId].AppliedAt-=FTimespan::FromSeconds(91);FutureManager->PruneReplayWindow();
    TestEqual(TEXT("Receipt survives every still-valid future-skew envelope"),FutureManager->ApplyEnvelopeChecked(Actor,Future),EProject_JHandoverApplyResult::AlreadyApplied);
    Future.Timestamp-=FTimespan::FromSeconds(3);FutureManager->AppliedTransferIds[Future.TransferId].AppliedAt-=FTimespan::FromSeconds(3);FutureManager->PruneReplayWindow();
    TestEqual(TEXT("Pruning after the complete validity horizon cannot reopen the expired envelope"),FutureManager->ApplyEnvelopeChecked(Actor,Future),EProject_JHandoverApplyResult::Rejected);
    FutureManager->Deinitialize();Manager->Deinitialize();return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3RetainedEquipmentTest,"ProjectJ.SourceAudit3.RetainedAvatarEquipment",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAudit3RetainedEquipmentTest::RunTest(const FString&) {
    auto* W=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(W);
    ON_SCOPE_EXIT {if(W->HasBegunPlay())W->EndPlay(EEndPlayReason::Quit);W->DestroyWorld(false);GEngine->DestroyWorldContext(W);};
    W->InitializeActorsForPlay(FURL());W->GetWorldSettings()->NotifyBeginPlay();W->GetWorldSettings()->NotifyMatchStarted();
    auto* State=W->SpawnActor<AProject_JPlayerState>();auto* PC=W->SpawnActor<APlayerController>();
    auto* Old=W->SpawnActor<AProject_JGreatswordCharacter>();auto* Next=W->SpawnActor<AProject_JGreatswordCharacter>();
    State->SetOwner(PC);PC->SetPlayerState(State);Old->SetPlayerState(State);PC->Possess(Old);
    auto* ASC=State->GetProjectJAbilitySystemComponent();ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetAttackPowerAttribute(),100.f);
    auto* Definition=NewObject<UProject_JEquipmentItemDefinition>(State);Definition->ItemId=TEXT("Audit3Head");Definition->EquipmentSlot=EProject_JEquipmentSlot::Head;
    Definition->StatApplicationPolicy=EProject_JEquipmentStatApplicationPolicy::StatModifiersOnly;
    FProject_JEquipmentStatModifier Modifier;Modifier.Stat=EProject_JEquipmentStat::AttackPower;Modifier.Value=10.f;Definition->StatModifiers.Add(Modifier);
    const int32 Baseline=ASC->GetActiveEffects(FGameplayEffectQuery()).Num();
    const auto Item=State->GetInventoryComponent()->AddItemDefinition(Definition);
    TestTrue(TEXT("Persistent inventory item equips"),State->GetEquipmentManagerComponent()->TryEquipItemInstanceById(Item.InstanceId).bSucceeded);
    TestEqual(TEXT("Old avatar has exactly one bonus"),ASC->GetNumericAttribute(UProject_JAttributeSet::GetAttackPowerAttribute()),110.f);
    Next->SetPlayerState(State);PC->Possess(Next);
    TestTrue(TEXT("Old avatar really remains alive"),IsValid(Old)&&!Old->IsActorBeingDestroyed());
    TestTrue(TEXT("Actual possession assigns ASC to next avatar"),ASC->GetAvatarActor()==Next);
    TestEqual(TEXT("Retained handover does not duplicate equipment stat"),ASC->GetNumericAttribute(UProject_JAttributeSet::GetAttackPowerAttribute()),110.f);
    TestEqual(TEXT("Retained handover owns one effect"),ASC->GetActiveEffects(FGameplayEffectQuery()).Num(),Baseline+1);
    Old->Destroy();TestEqual(TEXT("Old teardown preserves the live equipment bonus"),ASC->GetNumericAttribute(UProject_JAttributeSet::GetAttackPowerAttribute()),110.f);
    State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Head);
    TestEqual(TEXT("Unequip returns the exact baseline"),ASC->GetNumericAttribute(UProject_JAttributeSet::GetAttackPowerAttribute()),100.f);
    return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3PrepareMontageTest,"ProjectJ.SourceAudit3.PrepareNetworkMontage",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAudit3PrepareMontageTest::RunTest(const FString&) {
    FString Path;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditMontage="),Path);
    if(Path.IsEmpty()){AddInfo(TEXT("Not applicable without an explicit isolated test package path."));return true;}
    if(!TestTrue(TEXT("Only a disposable source-audit package can be created"),Path.StartsWith(TEXT("/Game/__SourceAuditTests__/"))))return false;
    const FString File=FPackageName::LongPackageNameToFilename(Path,FPackageName::GetAssetPackageExtension());
    if(!TestFalse(TEXT("Never overwrite an existing test or user asset"),IFileManager::Get().FileExists(*File)))return false;
    auto* Style=LoadObject<UProject_JCombatStyleDefinition>(nullptr,TEXT("/Game/DataAssetSets/CombatStyle/DA_CombatStyle_Greatsword.DA_CombatStyle_Greatsword"));
    if(!TestNotNull(TEXT("Read-only production style"),Style)||!Style->ComboDefinition||Style->ComboDefinition->Nodes.IsEmpty())return false;
    auto* Source=Style->ComboDefinition->Nodes[0].AttackDefinition->Montage.Get();
    if(!TestNotNull(TEXT("Source montage"),Source))return false;
    auto* Package=CreatePackage(*Path);auto* Copy=DuplicateObject(Source,Package,*FPackageName::GetLongPackageAssetName(Path));Copy->SetFlags(RF_Public|RF_Standalone);
    // Keep the actual authored animation; isolate sections/notifies used by the
    // objective network rate trial from unrelated production combat events.
    Copy->Notifies.Reset();Copy->CompositeSections.Reset();
    Copy->AddAnimCompositeSection(TEXT("AuditSlow"),0.f);Copy->AddAnimCompositeSection(TEXT("AuditFast"),.3f);
    Copy->CompositeSections[0].NextSectionName=TEXT("AuditSlow");Copy->CompositeSections[1].NextSectionName=TEXT("AuditFast");
    FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
    if(!TestTrue(TEXT("Saved only a newly-created isolated network montage"),UPackage::SavePackage(Package,Copy,*File,Args)))return false;
    const FString StylePath=FPackageName::GetLongPackagePath(Path)/TEXT("DA_AuditStyle");
    const FString StyleFile=FPackageName::LongPackageNameToFilename(StylePath,FPackageName::GetAssetPackageExtension());
    if(!TestFalse(TEXT("Never overwrite an existing isolated style"),IFileManager::Get().FileExists(*StyleFile)))return false;
    auto* StylePackage=CreatePackage(*StylePath);auto* Isolated=DuplicateObject(Style,StylePackage,TEXT("DA_AuditStyle"));Isolated->SetFlags(RF_Public|RF_Standalone);
    Isolated->AbilitySets.Reset();Isolated->CommandSet=nullptr;Isolated->CombatPresentationSet=nullptr;
    Isolated->bDeriveAttackCatalog=true;Isolated->AttackSet=nullptr;Isolated->AdditionalAttacks.Reset();
    Isolated->ComboDefinition=NewObject<UProject_JComboDefinition>(Isolated,TEXT("AuditCombo"));
    auto& Slow=Isolated->ComboDefinition->Nodes.AddDefaulted_GetRef();
    Slow.NodeTag=FGameplayTag::RequestGameplayTag(TEXT("ProjectJ.Tests.AuditNetwork.NodeA"));
    Slow.StartInputTags.AddTag(FGameplayTag::RequestGameplayTag(TEXT("InputTag.SourceAudit.NetworkRate")));
    Slow.AttackDefinition=DuplicateObject(Style->ComboDefinition->Nodes[0].AttackDefinition.Get(),Isolated,TEXT("AuditSlowAttack"));
    Slow.AttackDefinition->Montage=Copy;Slow.AttackDefinition->MontageSectionName=TEXT("AuditSlow");Slow.AttackDefinition->PlayRate=1.f;
    Slow.AttackDefinition->AttackTag=Slow.NodeTag;
    Slow.AttackDefinition->RequiredOwnerTags.Reset();Slow.AttackDefinition->BlockedOwnerTags.Reset();Slow.AttackDefinition->MovementPolicy=EProject_JAttackMovementPolicy::InPlace;
    Slow.AttackDefinition->bUseFlyingMovementModeForRootMotion=false;
    auto Fast=Slow;Fast.NodeTag=FGameplayTag::RequestGameplayTag(TEXT("ProjectJ.Tests.AuditNetwork.NodeB"));Fast.StartInputTags.Reset();
    Fast.AttackDefinition=DuplicateObject(Slow.AttackDefinition.Get(),Isolated,TEXT("AuditFastAttack"));Fast.AttackDefinition->MontageSectionName=TEXT("AuditFast");Fast.AttackDefinition->PlayRate=2.f;
    Fast.AttackDefinition->AttackTag=Fast.NodeTag;
    Isolated->ComboDefinition->Nodes.Add(Fast);
    TestTrue(TEXT("Saved only the isolated network style and inner test definitions"),UPackage::SavePackage(StylePackage,Isolated,*StyleFile,Args));
    return true;
}
#endif
