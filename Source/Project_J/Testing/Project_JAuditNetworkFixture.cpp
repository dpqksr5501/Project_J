#include "Testing/Project_JAuditNetworkFixture.h"
#include "GameFramework/Character.h"
#include "Movement/Project_JMovementQueries.h"
#include "DefaultMovementSet/CharacterMoverComponent.h"
#include "AbilitySystemComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Game/Project_JPlayerState.h"
#include "Components/Project_JInventoryComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "Engine/NetDriver.h"
#include "Engine/World.h"
#include "NativeGameplayTags.h"
#include "NiagaraSystem.h"
#include "NiagaraComponent.h"
#include "Components/Project_JGameplayWarningMarkerComponent.h"
#include "Components/Project_JNPCActivationComponent.h"
#include "Components/Project_JNPCActionComponent.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "TimerManager.h"
#include "AbilitySystemInterface.h"
#include "EngineUtils.h"
#include "Project_JGameplayTags.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Components/Project_JReplicatedAnimEventComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Combat/Project_JGameplayAbility_Sprint.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/PackageName.h"
#include "Tests/Project_JMountLifecycleFixture.h"
#include "Mount/Project_JMountComponent.h"
#include "Network/Project_JOwnedPawnScopeComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Net/Iris/ReplicationSystem/ReplicationSystemUtil.h"
#include "Net/Iris/ReplicationSystem/EngineReplicationBridge.h"
#include "Engine/NetConnection.h"
#include "Camera/CameraActor.h"
#include "Camera/PlayerCameraManager.h"
#include "HAL/IConsoleManager.h"
#include "Iris/ReplicationSystem/ReplicationSystem.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditNodeA,"ProjectJ.Tests.AuditNetwork.NodeA");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditNodeB,"ProjectJ.Tests.AuditNetwork.NodeB");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditCueA,"ProjectJ.Tests.AuditNetwork.CueA");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditCueB,"ProjectJ.Tests.AuditNetwork.CueB");
UE_DEFINE_GAMEPLAY_TAG_STATIC(AuditRateInput,"InputTag.SourceAudit.NetworkRate");
DEFINE_LOG_CATEGORY_STATIC(LogProjectJAuditNetwork,Log,All);
namespace {
bool AuditEnabled() {
#if UE_BUILD_SHIPPING
 return false;
#else
 return FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditNetwork"));
#endif
}
// Login Options are the canonical client request, unlike NetConnection.URL
// (the connection transport URL). Fixture identity is explicit, never arrival order.
int32 AuditSlotFromLoginOptions(const FString& Options)
{
 TArray<FString> Parts; Options.ParseIntoArray(Parts,TEXT("?"),true);
 int32 Requested=INDEX_NONE,Matches=0;
 for(const auto& Part:Parts)
 {
  if(!Part.StartsWith(TEXT("ProjectJAuditSlot="),ESearchCase::IgnoreCase) && !Part.Equals(TEXT("ProjectJAuditSlot"),ESearchCase::IgnoreCase)) continue;
  ++Matches;
  if(Part==TEXT("ProjectJAuditSlot=0")) Requested=0;
  else if(Part==TEXT("ProjectJAuditSlot=1")) Requested=1;
  else return INDEX_NONE;
 }
 return Matches==1 ? Requested : INDEX_NONE;
}
bool AuditQuestHistoryEnabled() { return AuditEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditQuestHistoryOnly")); }
bool AuditNPCAssemblyEnabled() { return AuditEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditNPCAssemblyOnly")); }
bool AuditWarningEnabled() { return AuditEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditWarningOnly")); }
bool AuditSessionEnabled() { return AuditEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditTrustedSessionOnly")); }
bool AuditMountEnabled() { return AuditEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditMountOnly")); }
bool AuditMountReplicationRestartEnabled() { return AuditMountEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditMountReplicationRestart")); }
bool AuditMountSpatialEnabled() { return AuditMountEnabled() && FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditMountSpatialReentry")); }
int32 AuditMountActiveSlot(int32 InCase) { return (InCase == 43 || InCase == 44) ? 0 : (InCase == 40 ? INDEX_NONE : InCase - 41); }
FString AuditOutput() { FString P;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditOutput="),P);return P; }
void WriteAudit(const FString& Name,const FString& Body) {
 const auto Root=AuditOutput();if(Root.IsEmpty())return;
 IFileManager::Get().MakeDirectory(*Root,true);FFileHelper::SaveStringToFile(Body,*(Root/Name));
}
float Health(AActor* Actor) { const auto* I=Cast<IAbilitySystemInterface>(Actor);const auto* ASC=I?I->GetAbilitySystemComponent():nullptr;return ASC?ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()):-999.f; }
FVector TraceStart(AActor* A){return A->GetActorLocation()+FVector(20,0,0);}
FVector TraceEnd(AActor* A){return A->GetActorLocation()+FVector(140,0,0);}
void FreezeAuditKinematics(APawn* C){Project_J::Movement::SetMovementMode(C,MOVE_None);}
}
namespace
{
uint64 AuditMountPublicId(const AActor* InActor)
{
    const auto* ObservedBridge = InActor ? UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(InActor) : nullptr;
    return ObservedBridge ? ObservedBridge->GetReplicatedRefHandle(InActor).GetId() : 0;
}
bool CaptureAuditMountIdentity(AProject_JAuditNetworkPlayer* InRider, FProjectJAuditMountIdentity& Out)
{
    auto* ObservedState = IsValid(InRider) ? InRider->GetPlayerState<AProject_JPlayerState>() : nullptr;
    auto* ObservedASC = ObservedState ? ObservedState->GetProjectJAbilitySystemComponent() : nullptr;
    auto* ObservedAttributes = ObservedState ? ObservedState->GetProjectJAttributeSet() : nullptr;
    if (!IsValid(InRider) || !IsValid(ObservedState) || !IsValid(ObservedASC) || !IsValid(ObservedAttributes)
        || InRider->GetAbilitySystemComponent() != ObservedASC || InRider->GetAttributeSet() != ObservedAttributes
        || ObservedASC->GetOwnerActor() != ObservedState || ObservedASC->GetAvatarActor() != InRider
        || ObservedASC->IsActorInfoChanging() || !ObservedASC->GetActorInfoRevision()) return false;
    Out.Rider = InRider; Out.State = ObservedState; Out.ASC = ObservedASC; Out.Attributes = ObservedAttributes;
    Out.Epoch = ObservedASC->GetActorInfoRevision(); return true;
}
bool ReadAuditMountAudience(const FProjectJAuditMountIdentity* InIdentities, int32 InCase, int32 InStep,
    AProject_JMountCharacter* InMount, AActor* InForeignAvatar, APlayerController* InObserver, int32 InObserverSlot,
    bool bInRecorded, bool bInDismountRequested, FString& OutRows, FString& OutDetail, uint64* OutEpochs)
{
    const bool bAuthority = !InObserver;
    const int32 ObservedActiveSlot = AuditMountActiveSlot(InCase);
    const bool bObservedMounted = ObservedActiveSlot != INDEX_NONE && InStep < 3;
    const bool bObservedWrong = bObservedMounted && InStep == 1 && InCase != 44;
    const bool bObservedMustRequest = !bAuthority && InCase > 40 && InStep == 3 && InObserverSlot == ObservedActiveSlot;
    if (bInDismountRequested != bObservedMustRequest) { OutDetail = TEXT("Actual owning client has not issued the production dismount RPC"); return false; }
    const uint64 ObservedMountId = AuditMountPublicId(InMount);
    if (!IsValid(InMount) || InMount->IsActorBeingDestroyed() || !ObservedMountId
        || !IsValid(InForeignAvatar) || !AuditMountPublicId(InForeignAvatar)) { OutDetail = TEXT("Public mount/probe replicas unresolved"); return false; }
    for (int32 ObservedSlot = 0; ObservedSlot < 2; ++ObservedSlot)
    {
        const auto& ObservedIdentity = InIdentities[ObservedSlot];
        auto* ObservedRider = ObservedIdentity.Rider.Get(); auto* ObservedState = ObservedIdentity.State.Get();
        auto* ObservedASC = ObservedIdentity.ASC.Get(); auto* ObservedAttributes = ObservedIdentity.Attributes.Get();
        auto* ObservedComponent = ObservedRider ? ObservedRider->GetMountComponent() : nullptr;
        const bool bObservedActive = bObservedMounted && ObservedSlot == ObservedActiveSlot;
        const bool bObservedReject = bObservedActive && bObservedWrong;
        const bool bObservedMine = InObserver && InObserver->GetPlayerState<AProject_JPlayerState>() == ObservedState;
        auto* ObservedExpectedAvatar = bObservedReject ? InForeignAvatar : static_cast<AActor*>(ObservedRider);
        const bool bObservedReset = InCase != 43 && InCase != 44 && ObservedSlot == ObservedActiveSlot && (InStep == 1 || InStep == 2);
        if (!IsValid(ObservedRider) || ObservedRider->IsActorBeingDestroyed() || !IsValid(ObservedState)
            || ObservedState->IsActorBeingDestroyed() || !IsValid(ObservedASC) || !ObservedASC->IsRegistered()
            || ObservedASC->IsBeingDestroyed() || ObservedASC->IsActorInfoChanging() || !IsValid(ObservedAttributes)
            || !ObservedComponent || ObservedRider->GetWorld() != InMount->GetWorld() || ObservedState->GetWorld() != InMount->GetWorld()
            || ObservedASC->GetWorld() != InMount->GetWorld() || ObservedASC->GetOwner() != ObservedState
            || ObservedASC->GetOwnerActor() != ObservedState || ObservedASC->GetAvatarActor() != ObservedExpectedAvatar
            || ObservedState->GetProjectJAbilitySystemComponent() != ObservedASC || ObservedState->GetProjectJAttributeSet() != ObservedAttributes)
        {
            OutDetail = FString::Printf(TEXT("Canonical unresolved rider=%d ASC=%s expectedAvatar=%s actualAvatar=%s"), ObservedSlot,
                *GetNameSafe(ObservedASC), *GetNameSafe(ObservedExpectedAvatar), *GetNameSafe(IsValid(ObservedASC) ? ObservedASC->GetAvatarActor() : nullptr));
            return false;
        }
        const uint64 ObservedRiderId = AuditMountPublicId(ObservedRider), ObservedStateId = AuditMountPublicId(ObservedState);
        const uint64 ObservedEpoch = ObservedASC->GetActorInfoRevision();
        const bool bObservedEpoch = InCase == 44 && InStep == 3 && ObservedSlot == 0
            ? ObservedEpoch >= ObservedIdentity.Epoch
            : (bObservedReset && !bInRecorded ? ObservedEpoch > ObservedIdentity.Epoch : ObservedEpoch == ObservedIdentity.Epoch);
        if (!ObservedRiderId || !ObservedStateId || !bObservedEpoch || (!bAuthority && bObservedMine != (ObservedSlot == InObserverSlot)))
        { OutDetail = FString::Printf(TEXT("Identity/epoch rider=%d id=%llu state=%llu epoch=%llu prior=%llu reset=%d recorded=%d mine=%d observer=%d"), ObservedSlot, ObservedRiderId, ObservedStateId, ObservedEpoch, ObservedIdentity.Epoch, bObservedReset, bInRecorded, bObservedMine, InObserverSlot); return false; }
        auto* ObservedGetterASC = ObservedRider->GetAbilitySystemComponent();
        auto* ObservedGetterAttributes = ObservedRider->GetAttributeSet();
        if ((bObservedReject && (ObservedGetterASC || ObservedGetterAttributes))
            || (!bObservedReject && (ObservedGetterASC != ObservedASC || ObservedGetterAttributes != ObservedAttributes))
            || ObservedASC == InMount->GetAbilitySystemComponent())
        {
            APawn* ObservedObserverPawn = InObserver ? InObserver->GetPawn() : nullptr;
            OutDetail = FString::Printf(TEXT("Getter mismatch rider=%d reject=%d ASC=%s Attr=%s componentMount=%s statePawn=%s observerPawn=%s mount=%s mountPS=%s mountRider=%s mountController=%s riderController=%s riderPS=%s roles=%d/%d"),
                ObservedSlot, bObservedReject, *GetNameSafe(ObservedGetterASC), *GetNameSafe(ObservedGetterAttributes),
                *GetNameSafe(ObservedComponent->GetMountedMount()), *GetNameSafe(ObservedState->GetPawn()),
                *GetNameSafe(ObservedObserverPawn), *GetNameSafe(InMount),
                *GetNameSafe(InMount->GetPlayerState()), *GetNameSafe(InMount->GetRider()), *GetNameSafe(InMount->GetController()),
                *GetNameSafe(ObservedRider->GetController()), *GetNameSafe(ObservedRider->GetPlayerState()),
                int32(ObservedRider->GetLocalRole()), int32(InMount->GetLocalRole()));
            return false;
        }
        const bool bObservedTopology = bObservedActive
            ? !ObservedRider->GetController() && !ObservedRider->GetPlayerState() && ObservedComponent->GetMountedMount() == InMount
                && InMount->GetRider() == ObservedRider && InMount->GetMountState() == EProject_JMountState::Mounted
                && InMount->GetPlayerState() == ObservedState && ObservedState->GetPawn() == InMount
            : ObservedComponent->GetMountedMount() == nullptr && ObservedRider->GetPlayerState() == ObservedState
                && ObservedState->GetPawn() == ObservedRider;
        if (!bObservedTopology)
        { OutDetail = FString::Printf(TEXT("Reciprocal mismatch rider=%d riderPS=%s statePawn=%s mountPS=%s mountRider=%s"), ObservedSlot, *GetNameSafe(ObservedRider->GetPlayerState()), *GetNameSafe(ObservedState->GetPawn()), *GetNameSafe(InMount->GetPlayerState()), *GetNameSafe(InMount->GetRider())); return false; }
        if (!bObservedMounted && (InMount->GetRider() || InMount->GetMountState() != EProject_JMountState::Unmounted || InMount->GetPlayerState()))
        { OutDetail = TEXT("Dismount relationship still live"); return false; }
        const auto ObservedRiderRole = ObservedRider->GetLocalRole(), ObservedMountRole = InMount->GetLocalRole();
        if (bAuthority)
        {
            auto* ObservedController = Cast<AController>(ObservedState->GetOwner());
            if (!ObservedController || ObservedRiderRole != ROLE_Authority || ObservedMountRole != ROLE_Authority
                || (bObservedActive ? InMount->GetController() != ObservedController : ObservedRider->GetController() != ObservedController))
            { OutDetail = TEXT("Authority current controller/session mismatch"); return false; }
        }
        else if (InObserver->GetNetMode() != NM_Client || ObservedRiderRole == ROLE_Authority
            || (!bObservedMine && (ObservedRiderRole != ROLE_SimulatedProxy || ObservedRider->GetController() || ObservedState->GetOwner()))
            || (bObservedActive && (bObservedMine
                ? InMount->GetController() != InObserver || InMount->GetLocalRole() != ROLE_AutonomousProxy || InObserver->GetPawn() != InMount
                : InMount->GetController() != nullptr || InMount->GetLocalRole() != ROLE_SimulatedProxy))
            || (!bObservedActive && bObservedMine && (ObservedRiderRole != ROLE_AutonomousProxy || ObservedRider->GetController() != InObserver || InObserver->GetPawn() != ObservedRider)))
        { OutDetail = FString::Printf(TEXT("Role topology rider=%d mine=%d roles=%d/%d controllers=%d/%d stateOwner=%d"), ObservedSlot, bObservedMine, int32(ObservedRiderRole), int32(ObservedMountRole), ObservedRider->GetController() != nullptr, InMount->GetController() != nullptr, ObservedState->GetOwner() != nullptr); return false; }
        const float ObservedHealth = ObservedAttributes->GetHealth(), ObservedMax = ObservedAttributes->GetMaxHealth();
        if (!FMath::IsFinite(ObservedHealth) || !FMath::IsFinite(ObservedMax) || ObservedHealth <= 0 || ObservedMax < ObservedHealth
            || !FMath::IsNearlyEqual(ObservedHealth, ObservedIdentity.Health) || !FMath::IsNearlyEqual(ObservedMax, ObservedIdentity.MaxHealth)
            || !FMath::IsNearlyEqual(ObservedASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()), ObservedHealth))
        { OutDetail = FString::Printf(TEXT("Public resources rider=%d actual=%.9g/%.9g expected=%.9g/%.9g"), ObservedSlot, ObservedHealth, ObservedMax, ObservedIdentity.Health, ObservedIdentity.MaxHealth); return false; }
        const int32 ObservedItems = ObservedState->GetInventoryComponent()->GetItemInstances().Num();
        const int32 ObservedPrivateEquipment = ObservedState->GetEquipmentManagerComponent()->GetPrivateEquipmentCount();
        const int32 ObservedPrivateExpected = bAuthority || bObservedMine ? 1 : 0;
        if (ObservedItems != ObservedPrivateExpected || ObservedPrivateEquipment != ObservedPrivateExpected)
        { OutDetail = FString::Printf(TEXT("Privacy rider=%d items=%d equipment=%d expected=%d"), ObservedSlot, ObservedItems, ObservedPrivateEquipment, ObservedPrivateExpected); return false; }
        OutEpochs[ObservedSlot] = ObservedEpoch;
        if (!bInRecorded) OutRows += FString::Printf(TEXT("%d,%d,%d,%d,%llu,%llu,%llu,%d,%d,%d,%d,%d,%d,1,%llu,%llu,%llu,%s,%.9g,%.9g,%d,%d,%d,%d,%d,%d,1\n"),
            InCase, InStep, InObserverSlot, ObservedSlot, ObservedRiderId, ObservedStateId, ObservedMountId,
            int32(ObservedRiderRole), int32(ObservedMountRole), ObservedRider->GetController() != nullptr, InMount->GetController() != nullptr,
            ObservedGetterASC != nullptr, ObservedGetterAttributes != nullptr, AuditMountPublicId(ObservedExpectedAvatar), AuditMountPublicId(ObservedASC->GetAvatarActor()),
            ObservedEpoch, bObservedReset ? TEXT("advance") : TEXT("stable"), ObservedHealth, ObservedMax,
            ObservedItems, ObservedPrivateEquipment, ObservedPrivateExpected, bObservedActive, bObservedReject, bInDismountRequested);
    }
    OutDetail = TEXT("Exact public tuple, actual roles, resources, epoch and owner-only privacy observed"); return true;
}
}

namespace
{
const TCHAR* MountSpatialHeader123=TEXT("case,step,observer_slot,server_mount_id,server_rider_id,mount_id,rider_id,old_mount_object_id,old_rider_object_id,mount_object_id,rider_object_id,old_mount_retired,old_rider_retired,live_mounts,live_riders,mount_mapped,rider_mapped,new_mount,new_rider,mount_always,rider_always,mount_owner_only,rider_owner_only,mount_use_owner,rider_use_owner,mount_cull_sq,rider_cull_sq,remap_dynamic,raw_camera_time,raw_camera_x,raw_camera_y,raw_camera_z,raw_view_target_id,raw_connection_view_target_id,child_connections,far_beyond2cull,has_scope,scope_ready,scope_identity,scope_member,scope_group,scope_created,scope_allow_requested,scope_failed,canonical,private,passed\n");
bool MountSpatialRemap123()
{
 const auto* Value=IConsoleManager::Get().FindConsoleVariable(TEXT("net.Iris.RemapDynamicObjects"));
 return Value && Value->GetInt()!=0;
}
bool MountSpatialFarPrivacy123(const FProjectJAuditMountIdentity* Identities,APlayerController* Observer)
{
 if(!IsValid(Observer))return false;
 for(int32 Slot=0;Slot<2;++Slot)
 {
  const auto& I=Identities[Slot];auto* PS=I.State.Get();auto* ASC=I.ASC.Get();auto* Attributes=I.Attributes.Get();
  const bool Mine=Slot==1;
  if(!IsValid(PS)||PS->IsActorBeingDestroyed()||!IsValid(ASC)||ASC->IsBeingDestroyed()||!ASC->IsRegistered()||ASC->IsActorInfoChanging()||
   !IsValid(Attributes)||PS->GetProjectJAbilitySystemComponent()!=ASC||PS->GetProjectJAttributeSet()!=Attributes||
   ASC->GetOwnerActor()!=PS||ASC->GetOwner()!=PS||ASC->GetActorInfoRevision()!=I.Epoch||
   !FMath::IsNearlyEqual(ASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute()),I.Health)||
   !FMath::IsNearlyEqual(ASC->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute()),I.MaxHealth)||
   PS->GetInventoryComponent()->GetItemInstances().Num()!=(Mine?1:0)||PS->GetEquipmentManagerComponent()->GetPrivateEquipmentCount()!=(Mine?1:0)||
   (Mine ? Observer->PlayerState!=PS||PS->GetOwner()!=Observer : PS->GetOwner()!=nullptr))return false;
  if(Mine)
  {
   auto* Rider=I.Rider.Get();if(!IsValid(Rider)||Rider->IsActorBeingDestroyed()||Observer->GetPawn()!=Rider||Rider->GetController()!=Observer||
    Rider->GetLocalRole()!=ROLE_AutonomousProxy||PS->GetPawn()!=Rider||ASC->GetAvatarActor()!=Rider||
    Rider->GetAbilitySystemComponent()!=ASC||Rider->GetAttributeSet()!=Attributes)return false;
  }
 }
 return true;
}
bool ReadMountSpatial123(UWorld* World,AProject_JAuditNetworkController* ViewPC,AProject_JAuditNetworkController* ScopePC,
 int32 ObserverSlot,int32 Step,uint64 MountId,uint64 RiderId,FProjectJAuditMountIdentity* Identities,
 FProjectJAuditMountSpatial& State,AActor* Probe,bool DismountRequested,FString& Row,FString& Detail)
{
 Detail=TEXT("host: waiting for live world/view PC and native dynamic remapping");
 if(!World||World->bIsTearingDown||!IsValid(ViewPC)||!MountSpatialRemap123())return false;
 auto* Driver=World->GetNetDriver();auto* System=Driver?Driver->GetReplicationSystem():nullptr;
 auto* Bridge=UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ViewPC);
 if(!System||!Bridge||System->GetReplicationBridge()!=Bridge){Detail=TEXT("bridge: native system/bridge is absent or differs");return false;}
 AProject_JMountCharacter* Mount=nullptr;AProject_JAuditNetworkPlayer* Rider=nullptr;int32 MountCount=0,RiderCount=0;
 for(TActorIterator<AProject_JMountLifecycleFixture> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&AuditMountPublicId(*It)==MountId){Mount=*It;++MountCount;}
 for(TActorIterator<AProject_JAuditNetworkPlayer> It(World);It;++It)if(!It->IsActorBeingDestroyed()&&AuditMountPublicId(*It)==RiderId){Rider=*It;++RiderCount;}
 const auto* DiagnosticCamera=ViewPC->PlayerCameraManager.Get();
 const auto* DiagnosticView=IsValid(DiagnosticCamera)?DiagnosticCamera->ViewTarget.Target.Get():nullptr;
 const FVector DiagnosticPosition=IsValid(DiagnosticCamera)?DiagnosticCamera->APlayerCameraManager::GetCameraCacheView().Location:FVector::ZeroVector;
 auto* DiagnosticConnection=ViewPC->GetNetConnection();if(!DiagnosticConnection)DiagnosticConnection=Driver->ServerConnection;
 AActor* DiagnosticConnectionView=DiagnosticConnection?DiagnosticConnection->ViewTarget.Get():nullptr;
 const auto* OldMount=State.OldMount.Get();const auto* OldRider=State.OldRider.Get();
 const auto* WarmMount=State.MountHandle.IsCompleteHandle()?Bridge->GetReplicatedObject(State.MountHandle):nullptr;
 const auto* WarmRider=State.RiderHandle.IsCompleteHandle()?Bridge->GetReplicatedObject(State.RiderHandle):nullptr;
 FString RawState=FString::Printf(TEXT("live=%d/%d ended=%d/%d weak_valid=%d/%d weak_destroying=%d/%d warm_map=%s/%s camera=%s cache_ready=%d cache_pos=(%.0f,%.0f,%.0f) raw_target=%s:%llu connection_target=%s:%llu children=%d"),
  MountCount,RiderCount,int32(State.bMountDestroyed),int32(State.bRiderDestroyed),int32(State.OldMount.IsValid()),int32(State.OldRider.IsValid()),
  int32(OldMount&&OldMount->IsActorBeingDestroyed()),int32(OldRider&&OldRider->IsActorBeingDestroyed()),*GetNameSafe(WarmMount),*GetNameSafe(WarmRider),
  *GetNameSafe(DiagnosticCamera),int32(IsValid(DiagnosticCamera)&&DiagnosticCamera->GetCameraCacheTime()>0),DiagnosticPosition.X,DiagnosticPosition.Y,DiagnosticPosition.Z,
  *GetNameSafe(DiagnosticView),AuditMountPublicId(DiagnosticView),*GetNameSafe(DiagnosticConnectionView),AuditMountPublicId(DiagnosticConnectionView),DiagnosticConnection?DiagnosticConnection->Children.Num():-1);
 for(int32 DiagnosticSlot=0;DiagnosticSlot<2;++DiagnosticSlot)
 {
  const auto* ASC=Identities[DiagnosticSlot].ASC.Get();
  RawState+=FString::Printf(TEXT(" asc%d=%s epoch=%llu/%llu changing=%d owner=%s avatar=%s"),DiagnosticSlot,*GetNameSafe(ASC),
   IsValid(ASC)?ASC->GetActorInfoRevision():0,Identities[DiagnosticSlot].Epoch,int32(IsValid(ASC)&&ASC->IsActorInfoChanging()),
   *GetNameSafe(IsValid(ASC)?ASC->GetOwnerActor():nullptr),*GetNameSafe(IsValid(ASC)?ASC->GetAvatarActor():nullptr));
 }
 const auto Wait=[&](const TCHAR* Stage){Detail=FString::Printf(TEXT("%s %s"),Stage,*RawState);return false;};
 const bool ForeignFar=ObserverSlot==1&&Step==1;
 if((ForeignFar&&(MountCount||RiderCount))||(!ForeignFar&&(MountCount!=1||RiderCount!=1)))return Wait(TEXT("actor_count"));
 const auto MountHandle=Mount?Bridge->GetReplicatedRefHandle(Mount):UE::Net::FNetRefHandle();
 const auto RiderHandle=Rider?Bridge->GetReplicatedRefHandle(Rider):UE::Net::FNetRefHandle();
 const bool MountMapped=MountHandle.IsCompleteHandle()&&Bridge->GetReplicatedObject(MountHandle)==Mount&&System->GetReplicationProtocol(MountHandle);
 const bool RiderMapped=RiderHandle.IsCompleteHandle()&&Bridge->GetReplicatedObject(RiderHandle)==Rider&&System->GetReplicationProtocol(RiderHandle);
 if(!ForeignFar&&(!MountMapped||!RiderMapped))return Wait(TEXT("current_native_mapping"));
 if(!State.bWarm)
 {
  if(Step!=0||!Mount||!Rider)return Wait(TEXT("warm_subjects"));
  State.OldMount=Mount;State.OldRider=Rider;State.MountHandle=MountHandle;State.RiderHandle=RiderHandle;
  State.OldMountObject=Mount->GetUniqueID();State.OldRiderObject=Rider->GetUniqueID();State.Position=Mount->GetActorLocation();
  State.MountCull=Mount->GetNetCullDistanceSquared();State.RiderCull=Rider->GetNetCullDistanceSquared();
  State.Flags[0]=Mount->bAlwaysRelevant;State.Flags[1]=Rider->bAlwaysRelevant;
  State.Flags[2]=Mount->bOnlyRelevantToOwner;State.Flags[3]=Rider->bOnlyRelevantToOwner;
  State.Flags[4]=Mount->bNetUseOwnerRelevancy;State.Flags[5]=Rider->bNetUseOwnerRelevancy;
  for(bool Flag:State.Flags)if(Flag)return Wait(TEXT("warm_spatial_flags"));
  if(!FMath::IsFinite(State.MountCull)||!FMath::IsFinite(State.RiderCull)||State.MountCull<=0||State.RiderCull<=0)return Wait(TEXT("warm_cull_distance"));
  State.bWarm=true;
 }
 const bool MountEnded=State.bMountDestroyed&&(!State.OldMount.IsValid()||State.OldMount->IsActorBeingDestroyed());
 const bool RiderEnded=State.bRiderDestroyed&&(!State.OldRider.IsValid()||State.OldRider->IsActorBeingDestroyed());
 const bool NewMount=Mount&&!State.OldMount.HasSameIndexAndSerialNumber(TWeakObjectPtr<AProject_JMountCharacter>(Mount));
 const bool NewRider=Rider&&!State.OldRider.HasSameIndexAndSerialNumber(TWeakObjectPtr<AProject_JAuditNetworkPlayer>(Rider));
 if(ForeignFar)
 {
  if(!MountEnded||!RiderEnded||Bridge->GetReplicatedObject(State.MountHandle)||Bridge->GetReplicatedObject(State.RiderHandle))return Wait(TEXT("native_retirement"));
 }
 else if(ObserverSlot==1&&Step>=2)
 {
  if(!State.bRetired||!MountEnded||!RiderEnded||!NewMount||!NewRider||
   !UE::Net::FNetRefHandle::FullCompare(MountHandle,State.MountHandle)||!UE::Net::FNetRefHandle::FullCompare(RiderHandle,State.RiderHandle))return Wait(TEXT("native_reentry"));
  // Refresh only this fixture's weak observation after actual native remapping.
  // Preserve PS/ASC/attribute identities; accept only the coherent native new-avatar epoch.
  Identities[0].Rider=Rider;
  if(!State.bReentryBound)
  {
   auto* ASC=Identities[0].ASC.Get();
   if(!IsValid(ASC)||ASC->IsActorInfoChanging()||ASC->GetOwnerActor()!=Identities[0].State.Get()||ASC->GetAvatarActor()!=Rider||
    !ASC->GetActorInfoRevision()||ASC->GetActorInfoRevision()<Identities[0].Epoch)return Wait(TEXT("reentry_canonical_avatar"));
   Identities[0].Epoch=ASC->GetActorInfoRevision();State.bReentryBound=true;
  }
 }
 else if(NewMount||NewRider||!UE::Net::FNetRefHandle::FullCompare(MountHandle,State.MountHandle)||
  !UE::Net::FNetRefHandle::FullCompare(RiderHandle,State.RiderHandle))return Wait(TEXT("stable_native_identity"));
 if(Mount&&(Mount->bAlwaysRelevant!=State.Flags[0]||Mount->bOnlyRelevantToOwner!=State.Flags[2]||Mount->bNetUseOwnerRelevancy!=State.Flags[4]||Mount->GetNetCullDistanceSquared()!=State.MountCull))return Wait(TEXT("mount_flags_or_cull_changed"));
 if(Rider&&(Rider->bAlwaysRelevant!=State.Flags[1]||Rider->bOnlyRelevantToOwner!=State.Flags[3]||Rider->bNetUseOwnerRelevancy!=State.Flags[5]||Rider->GetNetCullDistanceSquared()!=State.RiderCull))return Wait(TEXT("rider_flags_or_cull_changed"));
 auto* Camera=ViewPC->PlayerCameraManager.Get();if(!IsValid(Camera)||Camera->GetCameraCacheTime()<=0)return Wait(TEXT("camera_cache_unready"));
 const FVector CameraPosition=Camera->APlayerCameraManager::GetCameraCacheView().Location;
 if(CameraPosition.ContainsNaN())return Wait(TEXT("camera_cache_nonfinite"));
 const double DoubleCull=2.*FMath::Sqrt(FMath::Max(State.MountCull,State.RiderCull));
 const bool Far=FVector::DistSquared(CameraPosition,State.Position)>DoubleCull*DoubleCull;
 if(Far!=(Step==1&&ObserverSlot!=0))return Wait(TEXT("camera_cache_distance"));
 auto* Connection=ViewPC->GetNetConnection();if(!Connection)Connection=Driver->ServerConnection;
 if(!Connection||Connection->Children.Num()!=0)return Wait(TEXT("camera_connection_or_children"));
 auto* RawView=Camera->ViewTarget.Target.Get();AActor* RawConnectionView=Connection->ViewTarget;
 if(!IsValid(RawView)||!AuditMountPublicId(RawView))return Wait(TEXT("camera_view_target_unresolved"));
 if(ObserverSlot<0&&RawConnectionView!=RawView)return Wait(TEXT("authority_connection_view_target"));
 if(Step!=1||ObserverSlot==0)if(RawView!=ViewPC->GetPawn())return Wait(TEXT("near_view_target_not_current_pawn"));
 if(Step==1&&ObserverSlot!=0)
  if(!RawView->IsA<ACameraActor>()||FVector::DistSquared(RawView->GetActorLocation(),State.Position)<=DoubleCull*DoubleCull)return Wait(TEXT("far_camera_target_or_distance"));
 bool Canonical=false,Private=false;
 if(ForeignFar)Canonical=Private=MountSpatialFarPrivacy123(Identities,ViewPC);
 else
 {
  FString Ignored,Why;uint64 Epochs[2]={};
  Canonical=Private=ReadAuditMountAudience(Identities,44,Step,Mount,Probe,ObserverSlot<0?nullptr:ViewPC,ObserverSlot,
   false,DismountRequested,Ignored,Why,Epochs);
  if(!Canonical){Detail=FString::Printf(TEXT("canonical: %s %s"),*Why,*RawState);return false;}
 }
 if(!Canonical||!Private)return Wait(TEXT("far_privacy_or_canonical"));
 uint64 Group=0,Member=0;uint32 Created=0;bool HasScope=false,Ready=false,Identity=false,InGroup=false,Allow=false,Failed=false;
 if(ObserverSlot<0)
 {
  const auto* Scope=ScopePC?ScopePC->FindComponentByClass<UProject_JOwnedPawnScopeComponent>():nullptr;if(!Scope)return Wait(TEXT("authority_scope_absent"));
  const auto Snapshot=Scope->Query();HasScope=true;Ready=Snapshot.bLogicalReady;Identity=Snapshot.bIdentityMatches;
  InGroup=Snapshot.bMemberPresent;Allow=Snapshot.bAllowRequested;Failed=Snapshot.bAdmissionFailed;
  Group=Snapshot.GroupToken;Created=Snapshot.GroupsCreated;Member=Snapshot.PawnHandle.GetId();
  if(!Ready||!Identity||!InGroup||!Allow||Failed||Member!=(Step==3?RiderId:MountId))return Wait(TEXT("authority_scope_unready_or_member"));
  if(!State.Group){State.Group=Group;State.GroupsCreated=Created;}
  if(!Group||Group!=State.Group||Created!=State.GroupsCreated)return Wait(TEXT("authority_scope_group_changed"));
 }
 if(ForeignFar)State.bRetired=true;
 TArray<FString> Cells;Cells.Reserve(47);
 const auto Number=[&](auto Value){Cells.Add(LexToString(Value));};
 Number(44);Number(Step);Number(ObserverSlot);Number(MountId);Number(RiderId);Number(AuditMountPublicId(Mount));Number(AuditMountPublicId(Rider));
 Number(State.OldMountObject);Number(State.OldRiderObject);Number(Mount?Mount->GetUniqueID():0);Number(Rider?Rider->GetUniqueID():0);
 Number(int32(State.bRetired));Number(int32(State.bRetired));Number(MountCount);Number(RiderCount);Number(int32(MountMapped));Number(int32(RiderMapped));Number(int32(NewMount));Number(int32(NewRider));
 for(bool Flag:State.Flags)Number(int32(Flag));Number(State.MountCull);Number(State.RiderCull);Number(1);
 Number(Camera->GetCameraCacheTime());Number(CameraPosition.X);Number(CameraPosition.Y);Number(CameraPosition.Z);
 Number(AuditMountPublicId(RawView));Number(AuditMountPublicId(RawConnectionView));Number(Connection->Children.Num());Number(int32(Far));
 Number(int32(HasScope));Number(int32(Ready));Number(int32(Identity));Number(int32(InGroup));Number(Group);Number(Created);Number(int32(Allow));Number(int32(Failed));
 Number(int32(Canonical));Number(int32(Private));Number(1);
 check(Cells.Num()==47);Row=FString::Join(Cells,TEXT(","))+TEXT("\n");Detail=TEXT("Native spatial lifetime and normal public/private session observed");return true;
}
}

void AProject_JAuditNetworkController::OnMountSpatialEnd(AActor* Actor,EEndPlayReason::Type Reason)
{
 if(!AuditMountSpatialEnabled()||Case!=44||!MountSpatial.bWarm||Reason!=EEndPlayReason::Destroyed)return;
 if(MountSpatial.OldMount.HasSameIndexAndSerialNumber(TWeakObjectPtr<AProject_JMountCharacter>(Cast<AProject_JMountCharacter>(Actor))))MountSpatial.bMountDestroyed=true;
 if(MountSpatial.OldRider.HasSameIndexAndSerialNumber(TWeakObjectPtr<AProject_JAuditNetworkPlayer>(Cast<AProject_JAuditNetworkPlayer>(Actor))))MountSpatial.bRiderDestroyed=true;
}
void AProject_JAuditNetworkController::ObserveMountSpatial()
{
 const double Now=FPlatformTime::Seconds();if(Now<NextMountObserve||(MountRecordedCase==44&&MountRecordedStep==MountStep))return;
 NextMountObserve=Now+.05;if(Slot<0||Slot>1||MountExpectedIds.Num()!=3||MountPublicResources.Num()!=4)return;
 AProject_JMountCharacter* Mount=nullptr;
 for(TActorIterator<AProject_JMountLifecycleFixture> It(GetWorld());It;++It)if(!It->IsActorBeingDestroyed()&&AuditMountPublicId(*It)==MountExpectedIds[2]){Mount=*It;break;}
 if(MountStep==3&&Slot==0&&!bMountDismountRequested&&IsValid(Mount)&&Mount->GetController()==this&&GetPawn()==Mount&&
  Mount->GetLocalRole()==ROLE_AutonomousProxy&&Mount->GetRider()==MountIdentities[0].Rider.Get())
 {bMountDismountRequested=true;Mount->ServerRequestDismount();}
 FString Row,Detail;
 if(!ReadMountSpatial123(GetWorld(),this,nullptr,Slot,MountStep,MountExpectedIds[2],MountExpectedIds[0],MountIdentities,
  MountSpatial,Probe,bMountDismountRequested,Row,Detail))
 {if(MountLastDetail!=Detail){MountLastDetail=Detail;UE_LOG(LogProjectJAuditNetwork,Display,TEXT("SPATIAL_WAIT observer=%d step=%d %s"),Slot,MountStep,*Detail);}return;}
 if(MountStep==0)
 {
  if(auto* Old=MountSpatial.OldMount.Get())Old->OnEndPlay.AddUniqueDynamic(this,&ThisClass::OnMountSpatialEnd);
  if(auto* Old=MountSpatial.OldRider.Get())Old->OnEndPlay.AddUniqueDynamic(this,&ThisClass::OnMountSpatialEnd);
 }
 if(MountSpatialRows.IsEmpty())MountSpatialRows=MountSpatialHeader123;
 MountSpatialRows+=Row;MountRecordedCase=44;MountRecordedStep=MountStep;
 ServerObserve(44,MountStep,true,Detail);
}
void AProject_JAuditNetworkGameMode::UpdateMountSpatial()
{
 if(Clients.Num()!=2||!MountActor){Finish(false,TEXT("Spatial cohort lost authority participants"));return;}
 FString Row,Detail;
 if(!ReadMountSpatial123(GetWorld(),Clients[1],Clients[0],-1,Step,AuditMountPublicId(MountActor),
  AuditMountPublicId(MountIdentities[0].Rider.Get()),MountIdentities,MountSpatial,Probe,false,Row,Detail))
 {if(MountLastDetail!=Detail){MountLastDetail=Detail;UE_LOG(LogProjectJAuditNetwork,Display,TEXT("SPATIAL_WAIT_AUTH step=%d %s"),Step,*Detail);}return;}
 if(MountRecordedCase!=44||MountRecordedStep!=Step)
 {
  if(MountSpatialRows.IsEmpty())MountSpatialRows=MountSpatialHeader123;
  MountSpatialRows+=Row;MountRecordedCase=44;MountRecordedStep=Step;
 }
 if(!AllConfirmed(Step))return;
 if(Step==0)
 {
  const double Cull=FMath::Sqrt(FMath::Max(MountSpatial.MountCull,MountSpatial.RiderCull));
  const FVector Far=MountSpatial.Position+FVector(4.*Cull,0,0);
  auto* Camera=GetWorld()->SpawnActorDeferred<AProject_JAuditNetworkCamera>(AProject_JAuditNetworkCamera::StaticClass(),FTransform(FRotator::ZeroRotator,Far),
   nullptr,nullptr,ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
  if(!Camera){Finish(false,TEXT("Spatial fixture camera allocation failed"));return;}
  MountSpatialCamera=Camera;
  Camera->FinishSpawning(FTransform(FRotator::ZeroRotator,Far));
  if(bFinished||!IsValid(Camera)||Camera->IsActorBeingDestroyed()||Camera->GetWorld()!=GetWorld()||!IsValid(Clients[1]))
  {Finish(false,TEXT("Spatial camera lifecycle changed during spawn"));return;}
  Clients[1]->SetViewTarget(Camera);StartMountStep(44,1);
 }
 else if(Step==1)
 {Clients[1]->SetViewTarget(Clients[1]->GetPawn());StartMountStep(44,2);}
 else if(Step==2)StartMountStep(44,3);
 else Finish(true,TEXT("Normal spatial mount/rider retirement, native reentry, owner scope/privacy and production owner dismount observed"));
}

AProject_JAuditNetworkCamera::AProject_JAuditNetworkCamera(){bReplicates=true;bAlwaysRelevant=true;}

UProject_JAuditNetworkDamage::UProject_JAuditNetworkDamage() {
 DurationPolicy=EGameplayEffectDurationType::Instant;
 auto& M=Modifiers.AddDefaulted_GetRef();M.Attribute=UProject_JAttributeSet::GetHealthAttribute();M.ModifierOp=EGameplayModOp::Additive;M.ModifierMagnitude=FScalableFloat(-10.f);
}
void UProject_JAuditNetworkHit::ServerRequestSSRHit_Implementation(AActor* T,double Time,FVector Start,FVector End,FGameplayTag Node,int32 Sequence,int32 Key) {
 if(!AuditEnabled())return;
 const float Before=Health(T);const auto Current=GetActiveAttackNodeTag();
 // This is the actual inherited Server RPC validation/apply path, not a direct
 // ValidateServerHitRequest/history-query substitute for network damage.
 Super::ServerRequestSSRHit_Implementation(T,Time,Start,End,Node,Sequence,Key);
 if(auto* M=GetWorld()->GetAuthGameMode<AProject_JAuditNetworkGameMode>())M->RecordHit(Sequence,Time,Before,Health(T),Node,Current);
}
AProject_JAuditNetworkPlayer::AProject_JAuditNetworkPlayer(){AuditHit=CreateDefaultSubobject<UProject_JAuditNetworkHit>(TEXT("AuditSSR"));bAlwaysRelevant=!AuditMountSpatialEnabled();}
void AProject_JAuditNetworkPlayer::BeginPlay(){
 Super::BeginPlay();if(!AuditEnabled())return;FreezeAuditKinematics(this);SetActorTickEnabled(false);
 GetMesh()->SetSkeletalMeshAsset(LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
 GetMesh()->SetAnimInstanceClass(UAnimInstance::StaticClass());
 GetMesh()->VisibilityBasedAnimTickOption=EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
 GetMesh()->bEnableUpdateRateOptimizations=false;
}
AProject_JAuditNetworkTarget::AProject_JAuditNetworkTarget(){bAlwaysRelevant=true;}
void AProject_JAuditNetworkTarget::BeginPlay(){Super::BeginPlay();if(AuditEnabled()){FreezeAuditKinematics(this);SetNetUpdateFrequency(30.f);}}
AProject_JAuditCueProbe::AProject_JAuditCueProbe(){Presentation=CreateDefaultSubobject<UProject_JCombatPresentationComponent>(TEXT("AuditPresentation"));bAlwaysRelevant=true;}
void AProject_JAuditCueProbe::BeginPlay(){
 Super::BeginPlay();if(!AuditEnabled())return;FreezeAuditKinematics(this);SetNetUpdateFrequency(30.f);
 if(AuditWarningEnabled()){Presentation->BasePresentationSet=nullptr;return;}
 auto* Set=NewObject<UProject_JCombatPresentationSet>(this);auto* Profile=NewObject<UProject_JAttackPresentationProfile>(Set);
 auto* System=LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
 for(const FGameplayTag Tag:{FGameplayTag(AuditCueA),FGameplayTag(AuditCueB)}){auto& Cue=Profile->Cues.AddDefaulted_GetRef();Cue.CueTag=Tag;Cue.NiagaraSystem=System;Cue.bLooping=true;Cue.bDestroyImmediatelyOnStop=true;Cue.AttachmentTarget=EProject_JCombatVFXAttachmentTarget::World;}
 auto& Entry=Set->AttackPresentations.AddDefaulted_GetRef();Entry.AttackTag=AuditNodeA;Entry.Profile=Profile;Presentation->BasePresentationSet=Set;
}
void AProject_JAuditNetworkController::ClientPrepare_Implementation(int32 InCase,AProject_JAuditNetworkPlayer* A,AProject_JAuditNetworkTarget* T,AProject_JAuditCueProbe* P){
 if(!AuditEnabled())return;Case=InCase;Attacker=A;Target=T;Probe=P;bPrepared=false;bCheckHealth=false;bObserveCue=false;Remaining=0;MovementExpectation=INDEX_NONE;RatePhase=0;
 FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditSlot="),Slot);
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("CLIENT_PREPARE case=%d A=%s T=%s P=%s"),Case,*GetNameSafe(A),*GetNameSafe(T),*GetNameSafe(P));
}
void AProject_JAuditNetworkController::ClientMountStep_Implementation(int32 InCase, int32 InStep,
    const TArray<uint64>& InPublicIds, const TArray<float>& InPublicResources)
{
    if (!AuditMountEnabled() || InPublicIds.Num() != 3 || InPublicResources.Num() != 4
        || !((InCase == 40 && InStep == 0) || ((InCase == 41 || InCase == 42) && InStep >= 0 && InStep <= 3)
            || (AuditMountReplicationRestartEnabled() && InCase == 43 && (InStep == 0 || InStep == 2 || InStep == 3))
            || (AuditMountSpatialEnabled() && InCase == 44 && InStep >= 0 && InStep <= 3))) return;
    Case = InCase; MountStep = InStep; MountExpectedIds = InPublicIds; MountPublicResources = InPublicResources;
    MountRecordedCase = MountRecordedStep = INDEX_NONE; bMountDismountRequested = false; MountLastDetail.Reset(); NextMountObserve = 0;
    FParse::Value(FCommandLine::Get(), TEXT("ProjectJAuditSlot="), Slot);
}
void AProject_JAuditNetworkController::ObserveMountAudit()
{
    if(AuditMountSpatialEnabled() && Case==44){ObserveMountSpatial();return;}
    const double ObservedNow = FPlatformTime::Seconds();
    if (ObservedNow < NextMountObserve || (MountRecordedCase == Case && MountRecordedStep == MountStep)) return;
    NextMountObserve = ObservedNow + .05;
    if (Slot < 0 || Slot > 1 || MountExpectedIds.Num() != 3 || MountPublicResources.Num() != 4) return;
    AProject_JAuditNetworkPlayer* ObservedRiders[2] = {};
    int32 ObservedRiderCount = 0;
    for (TActorIterator<AProject_JAuditNetworkPlayer> It(GetWorld()); It; ++It)
    {
        if (It->IsActorBeingDestroyed()) continue; ++ObservedRiderCount;
        const uint64 ObservedId = AuditMountPublicId(*It);
        for (int32 ObservedSlot = 0; ObservedSlot < 2; ++ObservedSlot)
            if (ObservedId == MountExpectedIds[ObservedSlot]) ObservedRiders[ObservedSlot] = *It;
    }
    AProject_JMountCharacter* ObservedMount = nullptr;
    for (TActorIterator<AProject_JMountLifecycleFixture> It(GetWorld()); It; ++It)
        if (!It->IsActorBeingDestroyed() && AuditMountPublicId(*It) == MountExpectedIds[2]) { ObservedMount = *It; break; }
    if (ObservedRiderCount != 2 || !ObservedRiders[0] || !ObservedRiders[1] || !ObservedMount || !Probe) return;
    if (ObservedMount->GetController() == this && GetPawn() == ObservedMount)
    {
        // The normal ClientRestart also restores Walking. Stabilize only this
        // actual owned fixture mount; remote-SP reads do not issue commands.
        ObservedMount->GetCharacterMovement()->SetMovementMode(MOVE_None);
        ObservedMount->GetCharacterMovement()->StopMovementImmediately();
    }
    if (!MountIdentities[0].ASC.IsValid() || !MountIdentities[1].ASC.IsValid())
    {
        if (Case != 40) return;
        FProjectJAuditMountIdentity ObservedCaptured[2];
        for (int32 ObservedSlot = 0; ObservedSlot < 2; ++ObservedSlot)
        {
            if (!CaptureAuditMountIdentity(ObservedRiders[ObservedSlot], ObservedCaptured[ObservedSlot])) return;
            ObservedCaptured[ObservedSlot].Health = MountPublicResources[ObservedSlot * 2];
            ObservedCaptured[ObservedSlot].MaxHealth = MountPublicResources[ObservedSlot * 2 + 1];
        }
        if (ObservedCaptured[0].State == ObservedCaptured[1].State) return;
        MountIdentities[0] = ObservedCaptured[0]; MountIdentities[1] = ObservedCaptured[1];
    }
    if (Case > 40 && MountStep == 3 && Slot == AuditMountActiveSlot(Case) && !bMountDismountRequested
        && ObservedMount->GetController() == this && GetPawn() == ObservedMount && ObservedMount->GetLocalRole() == ROLE_AutonomousProxy
        && ObservedMount->GetRider() == MountIdentities[Slot].Rider.Get())
    {
        bMountDismountRequested = true;
        // Real production owner RPC, once. No fixture RPC performs dismount.
        ObservedMount->ServerRequestDismount();
    }
    FString ObservedRows, ObservedDetail; uint64 ObservedEpochs[2] = {};
    if (!ReadAuditMountAudience(MountIdentities, Case, MountStep, ObservedMount, Probe, this, Slot, false, bMountDismountRequested,
        ObservedRows, ObservedDetail, ObservedEpochs))
    {
        if (MountLastDetail != ObservedDetail)
        {
            MountLastDetail = ObservedDetail;
            UE_LOG(LogProjectJAuditNetwork, Display, TEXT("MOUNT_WAIT observer=%d case=%d step=%d requested=%d %s"), Slot, Case, MountStep, bMountDismountRequested, *ObservedDetail);
        }
        return;
    }
    for (int32 ObservedSlot = 0; ObservedSlot < 2; ++ObservedSlot) MountIdentities[ObservedSlot].Epoch = ObservedEpochs[ObservedSlot];
    MountRows += ObservedRows; MountRecordedCase = Case; MountRecordedStep = MountStep;
    ServerObserve(Case, MountStep, true, ObservedDetail);
}

void AProject_JAuditNetworkController::ClientSendHit_Implementation(int32 InCase,double Time){if(Case!=InCase)return;HitTime=Time;Sequence=InCase*100;Remaining=6;NextSend=0;}
void AProject_JAuditNetworkController::ClientCheckHealth_Implementation(int32 InCase,float Expected){if(Case!=InCase)return;ExpectedHealth=Expected;bCheckHealth=true;ObserveStarted=FPlatformTime::Seconds();}
void AProject_JAuditNetworkController::ClientObserveCue_Implementation(int32 InCase,int32 Order,bool A,bool B,bool Revision){if(Case!=InCase)return;CueOrder=Order;bExpectA=A;bExpectB=B;bRequireRevision=Revision;bObserveCue=true;ObserveStarted=FPlatformTime::Seconds();}
void AProject_JAuditNetworkController::UpdateCameraManager(float DeltaSeconds){
 if(AuditMountSpatialEnabled())Project_J::Movement::RequestMoverCameraUpdate(this);
 Super::UpdateCameraManager(DeltaSeconds);
}
void AProject_JAuditNetworkController::PlayerTick(float Delta){
 Super::PlayerTick(Delta);if(!AuditEnabled()||HasAuthority()||Case==INDEX_NONE)return;
 if(AuditQuestHistoryEnabled()){ObserveQuestHistoryAudit();return;}
 if(AuditNPCAssemblyEnabled()){ObserveNPCAssemblyAudit();return;}
 if(AuditSessionEnabled()){ObserveSessionAudit();return;}
 if(AuditWarningEnabled()){ObserveWarningAudit();return;}
 if(AuditMountEnabled()){ObserveMountAudit();return;}
 // Reliable control references can precede the corresponding actor channel.
 // Resolve the fixture's unique probe types once their real replicas exist.
 if(!Target){for(TActorIterator<AProject_JAuditNetworkTarget> It(GetWorld());It;++It){Target=*It;break;}}
 if(!Probe){for(TActorIterator<AProject_JAuditCueProbe> It(GetWorld());It;++It){Probe=*It;break;}}
 if(!bPrepared&&FMath::Fmod(GetWorld()->GetTimeSeconds(),1.f)<Delta){
  UE_LOG(LogProjectJAuditNetwork,Display,TEXT("CLIENT_WAIT case=%d A=%s T=%s P=%s ASC=%d TargetASC=%d Set=%d TBegin=%d PBegin=%d"),Case,*GetNameSafe(Attacker),*GetNameSafe(Target),*GetNameSafe(Probe),Attacker&&Attacker->GetAbilitySystemComponent()!=nullptr,Target&&Target->GetAbilitySystemComponent()!=nullptr,Probe&&Probe->Presentation->BasePresentationSet!=nullptr,Target&&Target->HasActorBegunPlay(),Probe&&Probe->HasActorBegunPlay());
 }
 if(!bPrepared&&Attacker&&Target&&Probe&&Attacker->GetAbilitySystemComponent()&&Target->GetAbilitySystemComponent()&&Probe->Presentation->BasePresentationSet){
  bPrepared=true;ServerObserve(Case,0,true,FString::Printf(TEXT("netmode=%d ownrole=%d attackerrole=%d owner=%d"),int32(GetNetMode()),int32(GetPawn()?GetPawn()->GetLocalRole():ROLE_None),int32(Attacker->GetLocalRole()),GetPawn()==Attacker));
 }
 const double Now=FPlatformTime::Seconds();
 if(Remaining>0&&Attacker&&Now>=NextSend){
  if(GetPawn()!=Attacker){Remaining=0;return;}
  Attacker->AuditHit->ServerRequestSSRHit(Target,HitTime,TraceStart(Attacker),TraceEnd(Attacker),AuditNodeA,++Sequence,17);
  --Remaining;NextSend=Now+.035;
 }
 if(bCheckHealth&&Target&&(FMath::IsNearlyEqual(Health(Target),ExpectedHealth)||Now-ObserveStarted>8)){
  bCheckHealth=false;ServerObserve(Case,2,FMath::IsNearlyEqual(Health(Target),ExpectedHealth),FString::Printf(TEXT("health=%.1f expected=%.1f attackerrole=%d"),Health(Target),ExpectedHealth,int32(Attacker->GetLocalRole())));
 }
 if(bObserveCue&&Probe){auto* C=Probe->Presentation.Get();
  const bool Event=C->LastAppliedPresentationEventOrder>=CueOrder;
  const bool A=C->ActiveLoopingCues.Contains(AuditCueA),B=C->ActiveLoopingCues.Contains(AuditCueB);
  const bool Complete=Event&&(!bRequireRevision||(A==bExpectA&&B==bExpectB&&C->LastAppliedPresentationRevision>=Case*10+3));
  if(Complete||Now-ObserveStarted>8){bObserveCue=false;
   ServerObserve(Case,bRequireRevision?2:1,Complete,FString::Printf(TEXT("event=%d revision=%d recovery=%d A=%d B=%d mode=%d"),C->LastAppliedPresentationEventOrder,C->LastAppliedPresentationRevision,C->LastAppliedRecoveryEventOrder,A,B,int32(GetNetMode())));
  }
 }
 if(MovementExpectation!=INDEX_NONE&&Attacker){
  auto* ASC=Attacker->GetAbilitySystemComponent();const int32 Count=ASC?ASC->GetTagCount(FProject_JGameplayTags::Get().State_Movement_InAir):-1;
  auto* State=Attacker->GetLocomotionAnimStateComponent();
  const bool Semantic=GetPawn()==Attacker||(State&&(MovementExpectation?State->bIsInAir:State->LastConfirmedRemoteLandingSequence>=1&&!State->bInAirGameplayTagRequested));
  const bool Match=Semantic&&(MovementExpectation?Count>0:Count==0);
  if(Match||Now-ObserveStarted>8){
   const int32 Expected=MovementExpectation;MovementExpectation=INDEX_NONE;
   ServerObserve(Case,Expected?1:2,Match,FString::Printf(TEXT("movement role=%d InAir=%d expected_falling=%d"),int32(Attacker->GetLocalRole()),Count,Expected));
  }
 }
 if(RatePhase>0&&Attacker){
  if(!RateStyle&&!ConfigureRate(Attacker))return;
  if(GetPawn()==Attacker){
   auto* ASC=Cast<UProject_JAbilitySystemComponent>(Attacker->GetAbilitySystemComponent());
   if(RatePhase==1&&!bRateStarted&&ASC&&ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_CombatMode)){
    for(auto& Spec:ASC->GetActivatableAbilities())if(Spec.GetDynamicSpecSourceTags().HasTagExact(AuditRateInput)){
     if(auto* Input=Attacker->FindComponentByClass<UProject_JSkillInputExecutionComponent>()){Input->HandleInputTagPressed(AuditRateInput);bRateStarted=true;}break;
    }
   }
   if(RatePhase==2&&!bRateAdvanced){bRateAdvanced=AdvanceRate(Attacker);}
  }
  const float Rate=ReadRate(Attacker);const bool Match=FMath::IsNearlyEqual(Rate,float(RatePhase));
  if(Match||Now-ObserveStarted>8){const int32 Phase=RatePhase;RatePhase=0;
   ServerObserve(Case,Phase,Match,FString::Printf(TEXT("montage role=%d owner=%d rate=%.3f expected=%d advanced=%d"),int32(Attacker->GetLocalRole()),GetPawn()==Attacker,Rate,Phase,bRateAdvanced));
  }
 }
}
void AProject_JAuditNetworkController::ClientObserveMovement_Implementation(int32 InCase,bool bInFalling){
 if(Case!=InCase)return;MovementExpectation=bInFalling?1:0;ObserveStarted=FPlatformTime::Seconds();
}
void AProject_JAuditNetworkController::ClientRate_Implementation(int32 InCase,bool bInAdvance){
 if(Case!=InCase)return;RatePhase=bInAdvance?2:1;ObserveStarted=FPlatformTime::Seconds();
}
bool AProject_JAuditNetworkController::ConfigureRate(AProject_JAuditNetworkPlayer* InPlayer){
 if(!InPlayer||!InPlayer->GetAbilitySystemComponent())return false;
 FString MontagePath;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditMontage="),MontagePath);
 if(MontagePath.IsEmpty())return false;
 const FString StylePackage=FPackageName::GetLongPackagePath(MontagePath)/TEXT("DA_AuditStyle");
 RateStyle=LoadObject<UProject_JCombatStyleDefinition>(nullptr,*(StylePackage+TEXT(".DA_AuditStyle")));
 if(!RateStyle||!RateStyle->ComboDefinition||RateStyle->ComboDefinition->Nodes.Num()!=2)return false;
 InPlayer->SetCurrentEquipmentConfiguration(RateStyle,InPlayer->GetCurrentWeaponPresentationProfile());
 auto* ASC=Cast<UProject_JAbilitySystemComponent>(InPlayer->GetAbilitySystemComponent());
 if(InPlayer->HasAuthority()){
  ASC->CancelAllAbilities();
  ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
  FGameplayAbilitySpec Spec(UProject_JGameplayAbility_Melee::StaticClass());Spec.GetDynamicSpecSourceTags().AddTag(AuditRateInput);ASC->GiveAbility(Spec);
 }
 return true;
}
float AProject_JAuditNetworkController::ReadRate(AProject_JAuditNetworkPlayer* InPlayer)const{
 auto* Anim=InPlayer?InPlayer->GetMesh()->GetAnimInstance():nullptr;
 auto* Montage=RateStyle&&RateStyle->ComboDefinition?RateStyle->ComboDefinition->Nodes[0].AttackDefinition->Montage.Get():nullptr;
 return Anim&&Montage&&Anim->Montage_IsPlaying(Montage)?Anim->Montage_GetPlayRate(Montage):-1.f;
}
bool AProject_JAuditNetworkController::AdvanceRate(AProject_JAuditNetworkPlayer* InPlayer){
 auto* ASC=InPlayer?InPlayer->GetAbilitySystemComponent():nullptr;if(!ASC||!RateStyle)return false;
 for(auto& Spec:ASC->GetActivatableAbilities()){
  if(!Spec.GetDynamicSpecSourceTags().HasTagExact(AuditRateInput))continue;
  auto* Ability=Cast<UProject_JGameplayAbility_Melee>(Spec.GetPrimaryInstance());if(!Ability||!Ability->IsActive())return false;
  auto* Task=Ability->MontageTask;Ability->StartComboNode(RateStyle->ComboDefinition->Nodes[1]);
  const bool Preserved=Task&&Ability->MontageTask==Task;
  UE_LOG(LogProjectJAuditNetwork,Display,TEXT("RATE_ADVANCE role=%d authority=%d task_preserved=%d rate=%.3f"),int32(InPlayer->GetLocalRole()),InPlayer->HasAuthority(),Preserved,ReadRate(InPlayer));
  return Preserved&&FMath::IsNearlyEqual(ReadRate(InPlayer),2.f);
 }return false;
}
void AProject_JAuditNetworkController::ServerObserve_Implementation(int32 InCase,int32 Step,bool Success,const FString& Detail){if(auto* M=GetWorld()->GetAuthGameMode<AProject_JAuditNetworkGameMode>())M->Observe(this,InCase,Step,Success,Detail);}
void AProject_JAuditNetworkController::ClientFinish_Implementation(bool Success){
 if(AuditQuestHistoryEnabled())
 {
  if(auto* State=QuestHistoryOwnState.Get())if(auto* Component=State->FindComponentByClass<UProject_JQuestComponent>())Component->CancelHistoryPageRequest(QuestHistoryReturnedNonce);
  WriteAudit(FString::Printf(TEXT("quest-history-client_%d.csv"),Slot),QuestHistoryRows);
  WriteAudit(FString::Printf(TEXT("quest-history-cleanup_%d.txt"),Slot),FString::Printf(TEXT("cleanup_passed=%d step=%d normal_finish=1\n"),Success&&QuestHistoryStep==5,QuestHistoryStep));
  QuestHistoryStep=INDEX_NONE;bQuestHistoryRecorded=true;
 }
 if(AuditNPCAssemblyEnabled())
 {
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(NPCAssemblyRows,*(Root/FString::Printf(TEXT("npc-assembly-client_%d.csv"),Slot)))&&Success;
 }
 if(AuditSessionEnabled())
 {
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(SessionRows,*(Root/FString::Printf(TEXT("session-client_%d.csv"),Slot)))&&Success;
 }
 if(AuditWarningEnabled())
 {
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(WarningRows,*(Root/FString::Printf(TEXT("warning-client_%d.csv"),Slot)))&&Success;
 }
 if(AuditMountSpatialEnabled())
 {
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(MountSpatialRows,*(Root/FString::Printf(TEXT("mount-spatial-client_%d.csv"),Slot)))&&Success;
  if(auto* Old=MountSpatial.OldMount.Get())Old->OnEndPlay.RemoveDynamic(this,&ThisClass::OnMountSpatialEnd);
  if(auto* Old=MountSpatial.OldRider.Get())Old->OnEndPlay.RemoveDynamic(this,&ThisClass::OnMountSpatialEnd);
 }
 if(AuditMountEnabled())WriteAudit(FString::Printf(TEXT("mount-client_%d.csv"),Slot),MountRows);
 WriteAudit(FString::Printf(TEXT("client_%d.txt"),Slot),FString::Printf(TEXT("success=%d netmode=%d local_role=%d\n"),Success,int32(GetNetMode()),int32(GetPawn()?GetPawn()->GetLocalRole():ROLE_None)));
 FPlatformMisc::RequestExitWithStatus(false,Success?0:1);
}
void AProject_JAuditNetworkController::SendCueDelta(AProject_JAuditCueProbe* P,bool bStart,int32 Order,uint32 Instance,const FProject_JReplicatedCombatPresentationState& Snapshot){
 // Deliberately send the real multicast before the full
 // replicated property. The omitted A delta models one lost cue independently
 // of the packet simulator's additional actual lag/loss/reordering.
 P->Presentation->MulticastPlayPresentationCue(AuditNodeA,AuditCueB,bStart,Order,Instance,Snapshot);
}
void AProject_JAuditNetworkController::PublishCueState(AProject_JAuditCueProbe* P,const FProject_JReplicatedCombatPresentationState& State){P->Presentation->ReplicatedPresentationState=State;P->ForceNetUpdate();}
AProject_JAuditNetworkGameMode::AProject_JAuditNetworkGameMode(){PrimaryActorTick.bCanEverTick=true;PlayerControllerClass=AProject_JAuditNetworkController::StaticClass();DefaultPawnClass=nullptr;bRequireTrustedSessions=AuditSessionEnabled();}
void AProject_JAuditNetworkGameMode::BeginPlay(){
 if(FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditMountSpatialReentry"))&&
  (!AuditMountSpatialEnabled()||AuditMountReplicationRestartEnabled()||!MountSpatialRemap123()))
 {Finish(false,TEXT("Spatial reentry requires MountOnly, native dynamic remapping and no registration-restart option"));return;}
 if(FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditQuestHistoryOnly")))
 {
  FString MontageOption;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditMontage="),MontageOption);
  if(!AuditQuestHistoryEnabled()||AuditNPCAssemblyEnabled()||AuditSessionEnabled()||AuditWarningEnabled()||AuditMountEnabled()||FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditSSROnly"))||!MontageOption.IsEmpty())
  {Finish(false,TEXT("QuestHistoryOnly is an isolated owner-page/foreign-privacy cohort"));return;}
 }
 Super::BeginPlay();Started=FPlatformTime::Seconds();if(!AuditEnabled()){Finish(false,TEXT("Explicit fixture flag required"));return;}
 if(FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditMountReplicationRestart"))&&!AuditMountEnabled()){Finish(false,TEXT("Mount replication restart requires MountOnly"));return;}
 if(AuditMountEnabled()&&FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditSSROnly"))){Finish(false,TEXT("MountOnly and SSROnly are separate cohorts"));return;}
 if(FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditWarningOnly"))&&
  (!AuditWarningEnabled()||AuditMountEnabled()||FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditSSROnly"))))
 {Finish(false,TEXT("WarningOnly is an isolated network cohort"));return;}
 auto* Driver=GetWorld()->GetNetDriver();
#if DO_ENABLE_NET_TEST
 const auto& Sim=Driver->PacketSimulationSettings;
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("PACKETS lag=%d variance=%d loss=%d order=%d duplicate=%d netmode=%d"),Sim.PktLag,Sim.PktLagVariance,Sim.PktLoss,Sim.PktOrder,Sim.PktDup,int32(GetNetMode()));
#endif
 if(AuditQuestHistoryEnabled())return; // Actual native PlayerStates, no Pawn/equipment/combat assembly.
 if(AuditNPCAssemblyEnabled())
 {
  FString MontageOption;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditMontage="),MontageOption);
  if(AuditSessionEnabled()||AuditWarningEnabled()||AuditMountEnabled()||FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditSSROnly"))||!MontageOption.IsEmpty())
  {Finish(false,TEXT("NPCAssemblyOnly is an isolated component/recovery cohort"));return;}
  FActorSpawnParameters Spawn;Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;Spawn.ObjectFlags|=RF_Transient;
  NPCAssemblyActor=GetWorld()->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(),FVector(500,0,100),FRotator::ZeroRotator,Spawn);
  if(!IsValid(NPCAssemblyActor)){Finish(false,TEXT("Native NPC assembly actor spawn failed"));return;}
  FreezeAuditKinematics(NPCAssemblyActor);
  NPCAssemblyASC=Cast<UProject_JAbilitySystemComponent>(NPCAssemblyActor->GetAbilitySystemComponent());
  NPCAssemblyEpoch=NPCAssemblyASC.IsValid()?NPCAssemblyASC->GetActorInfoRevision():0;
  bNPCAssemblyOwnsController=NPCAssemblyActor->GetController()==nullptr;
  NPCAssemblyDefinition=NewObject<UProject_JNPCActivationDefinition>(this); // No attack/asset/visual-combat claim.
  NPCAssemblyRows=TEXT("case,step,mode,role,npc_id,owner,frame,npc_present,hit_count,presentation_count,hit_id,presentation_id,registered,begun,replicated,instance,revision,order,warning_count,lease,helper_owned,grant,profile_null,pending_loops,active_niagara,passed\n");
  return;
 }
 if(AuditSessionEnabled())
 {
  FString MontageOption;FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditMontage="),MontageOption);
  if(AuditWarningEnabled()||AuditMountEnabled()||FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditSSROnly"))||!MontageOption.IsEmpty())
   Finish(false,TEXT("TrustedSessionOnly is an isolated in-process trusted diagnostic"));
  return;
 }
 if(AuditWarningEnabled())
 {
  WarningRows=TEXT("case,step,transition,owner,mode,role,frame,instance,revision,order,warning_count,lease,profile_null,passed\n");
  FActorSpawnParameters WarningSpawn;WarningSpawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;WarningSpawn.ObjectFlags|=RF_Transient;
  Probe=GetWorld()->SpawnActor<AProject_JAuditCueProbe>(AProject_JAuditCueProbe::StaticClass(),FVector(500,0,100),FRotator::ZeroRotator,WarningSpawn);
  if(!IsValid(Probe)||!IsValid(Probe->Presentation)||Probe->Presentation->BasePresentationSet)
   Finish(false,TEXT("Warning probe must have a native presentation component and genuinely absent profile"));
  return;
 }
 FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.ObjectFlags|=RF_Transient;
 Target=GetWorld()->SpawnActor<AProject_JAuditNetworkTarget>(AProject_JAuditNetworkTarget::StaticClass(),FVector(120,0,100),FRotator::ZeroRotator,P);
 Probe=GetWorld()->SpawnActor<AProject_JAuditCueProbe>(AProject_JAuditCueProbe::StaticClass(),FVector(500,0,100),FRotator::ZeroRotator,P);
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(),100.f);
 Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),100.f);
 Attack=NewObject<UProject_JAttackDefinition>(this);Attack->AttackTag=AuditNodeA;Attack->DamageEffect=UProject_JAuditNetworkDamage::StaticClass();Attack->HitSpec.TraceRadius=20.f;

}
FString AProject_JAuditNetworkGameMode::InitNewPlayer(APlayerController* NewPlayerController,const FUniqueNetIdRepl& UniqueId,const FString& Options,const FString& Portal)
{
 const FString Error=Super::InitNewPlayer(NewPlayerController,UniqueId,Options,Portal);
 if(!Error.IsEmpty() || !AuditEnabled()) return Error;
 auto* Incoming=Cast<AProject_JAuditNetworkController>(NewPlayerController);
 const int32 Requested=AuditSlotFromLoginOptions(Options);
 if(!Incoming || Requested==INDEX_NONE || Clients.Num()>=2 || Clients.ContainsByPredicate([Requested](const auto& Registered){return Registered && Registered->Slot==Requested;}))
 {
  const FString Rejection=TEXT("Audit requires one unique ProjectJAuditSlot URL option with exact value 0 or 1");
  UE_LOG(LogProjectJAuditNetwork,Error,TEXT("SLOT_REJECTED requested=%d registered=%d"),Requested,Clients.Num());
  Finish(false,Rejection);
  return Rejection; // GameModeBase.Login destroys this unadmitted PC; native join closes normally.
 }
 if(AuditNPCAssemblyEnabled()&&((Requested==0&&Clients.Num()!=0)||(Requested==1&&!bNPCAssemblyLateJoinReady)))
 {Finish(false,TEXT("NPC assembly requires slot0 first and slot1 only after the native late-join barrier"));return TEXT("NPC assembly admission order");}
 Incoming->Slot=Requested;
 return FString();
}
void AProject_JAuditNetworkGameMode::PostLogin(APlayerController* New){
 Super::PostLogin(New);if(!AuditEnabled())return;
 auto* PC=CastChecked<AProject_JAuditNetworkController>(New);
 if(PC->Slot<0 || PC->Slot>1 || Clients.Num()>=2 || Clients.ContainsByPredicate([PC](const auto& Registered){return Registered && Registered->Slot==PC->Slot;}))
 {Finish(false,TEXT("Audit PostLogin rejected missing/duplicate explicit slot"));return;}
 Clients.Add(PC);
 Clients.Sort([](const AProject_JAuditNetworkController& Left,const AProject_JAuditNetworkController& Right){return Left.Slot<Right.Slot;});
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("SLOT_REGISTERED slot=%d count=%d source=LoginOptions"),PC->Slot,Clients.Num());
 if(AuditQuestHistoryEnabled())return; // Real PostLogin PlayerState and its native quest component only.
 if(AuditNPCAssemblyEnabled())return; // Same native empty-Pawn observer connections as WarningOnly.
 if(AuditSessionEnabled())return; // Native trusted PostLogin held identity/Pawn; assemble only after verified fixture acceptance.
 if(AuditWarningEnabled())return; // No equipment/player/SSR/mount preparation in this isolated cohort.
 FActorSpawnParameters P;P.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;P.Owner=PC;P.ObjectFlags|=RF_Transient;
 auto* Player=GetWorld()->SpawnActor<AProject_JAuditNetworkPlayer>(AProject_JAuditNetworkPlayer::StaticClass(),FVector(0,PC->Slot*2000,100),FRotator::ZeroRotator,P);
 PC->Possess(Player);if(PC->Slot==0)Attacker=Player;
 auto* PS=PC->GetPlayerState<AProject_JPlayerState>();
 auto* Item=LoadObject<UProject_JEquipmentItemDefinition>(nullptr,TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
 if(!PS||!Item){Finish(false,TEXT("Production persistent equipment fixture missing"));return;}
 const auto Added=PS->GetInventoryComponent()->AddItemDefinition(Item);
 if(!PS->GetEquipmentManagerComponent()->TryEquipItemInstanceById(Added.InstanceId).bSucceeded){Finish(false,TEXT("Weapon equip failed"));}
}
bool AProject_JAuditNetworkGameMode::PrepareMountAudit()
{
    if (Clients.Num() != 2 || Clients.ContainsByPredicate([](const auto& InPC) { return !IsValid(InPC.Get()) || !InPC->GetNetConnection(); })) return false;
    if (!MountActor)
    {
        for (int32 ObservedSlot = 0; ObservedSlot < 2; ++ObservedSlot)
        {
            auto* ObservedRider = Cast<AProject_JAuditNetworkPlayer>(Clients[ObservedSlot]->GetPawn());
            if (!CaptureAuditMountIdentity(ObservedRider, MountIdentities[ObservedSlot])
                || !Project_J::Movement::ReadPhysicalFrame(ObservedRider).bValid) return false;
            auto* ObservedASC = MountIdentities[ObservedSlot].ASC.Get();
            ObservedASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 200.f + 10.f * ObservedSlot);
            ObservedASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 120.f + 10.f * ObservedSlot);
            MountIdentities[ObservedSlot].Health = ObservedASC->GetNumericAttribute(UProject_JAttributeSet::GetHealthAttribute());
            MountIdentities[ObservedSlot].MaxHealth = ObservedASC->GetNumericAttribute(UProject_JAttributeSet::GetMaxHealthAttribute());
        }
        FActorSpawnParameters ObservedParams; ObservedParams.ObjectFlags |= RF_Transient;
        ObservedParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
        auto* ObservedMount = GetWorld()->SpawnActor<AProject_JMountLifecycleFixture>(AProject_JMountLifecycleFixture::StaticClass(),
            MountIdentities[0].Rider->GetActorLocation(), FRotator::ZeroRotator, ObservedParams);
        if (!ObservedMount) { Finish(false, TEXT("Native mount audience fixture spawn failed")); return false; }
        MountActor = ObservedMount; ObservedMount->bAlwaysRelevant = !AuditMountSpatialEnabled();
        // Entry has no gameplay terrain. Preserve the existing audit's stable
        // kinematics using the normal movement API; never manufacture NetRoles.
        ObservedMount->GetCharacterMovement()->SetMovementMode(MOVE_None);
        ObservedMount->GetCharacterMovement()->StopMovementImmediately();
    }
    return AuditMountPublicId(MountActor) && AuditMountPublicId(Probe)
        && AuditMountPublicId(MountIdentities[0].Rider.Get()) && AuditMountPublicId(MountIdentities[1].Rider.Get());
}
void AProject_JAuditNetworkGameMode::StartMountStep(int32 InCase, int32 InStep)
{
    const TStrongObjectPtr<AProject_JAuditNetworkGameMode> KeepSelf(this);
    const TStrongObjectPtr<AProject_JMountCharacter> KeepMount(MountActor);
    const TStrongObjectPtr<AProject_JAuditCueProbe> KeepProbe(Probe);
    const bool bObservedNewCase = Case != InCase;
    Case = InCase; Step = InStep; StepStarted = FPlatformTime::Seconds(); if (bObservedNewCase) CaseStarted = StepStarted;
    const uint64 ObservedRevision = ++MountRevision;
    MountRecordedCase = MountRecordedStep = INDEX_NONE; MountLastDetail.Reset();
    for (const auto& InPC : Clients) { InPC->ConfirmedCase = InPC->ConfirmedStep = INDEX_NONE; }
    if (Case > 40)
    {
        const int32 ObservedSlot = AuditMountActiveSlot(Case);
        const TStrongObjectPtr<AProject_JAuditNetworkPlayer> KeepRider(MountIdentities[ObservedSlot].Rider.Get());
        const TStrongObjectPtr<AProject_JPlayerState> KeepState(MountIdentities[ObservedSlot].State.Get());
        const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepASC(MountIdentities[ObservedSlot].ASC.Get());
        if (!KeepRider || !KeepState || !KeepASC || !KeepMount || !KeepProbe || KeepRider->IsActorBeingDestroyed()
            || KeepASC->GetOwnerActor() != KeepState.Get() || KeepASC->IsActorInfoChanging())
        { Finish(false, TEXT("Mount step lost its original canonical participants")); return; }
        if (Step == 0)
        {
            if (Clients[ObservedSlot]->GetPawn() != KeepRider.Get() || KeepASC->GetAvatarActor() != KeepRider.Get()
                || KeepMount->GetRider() || KeepMount->GetMountState() != EProject_JMountState::Unmounted)
            { Finish(false, TEXT("Mount start no longer owns the rider/session")); return; }
            KeepMount->SetActorLocation(KeepRider->GetActorLocation());
            KeepMount->GetCharacterMovement()->SetMovementMode(MOVE_None);
            if (!KeepMount->TryMountRider(KeepRider.Get())) { Finish(false, TEXT("Actual public mount request failed")); return; }
            // Character::Restart during real possession restores Walking.
            // This terrain-free fixture must reapply its declared stable mode.
            KeepMount->GetCharacterMovement()->SetMovementMode(MOVE_None);
            KeepMount->GetCharacterMovement()->StopMovementImmediately();
        }
        else if (Case != 43 && Case != 44 && (Step == 1 || Step == 2))
        {
            auto* ObservedExpectedAvatar = Step == 1 ? static_cast<AActor*>(KeepRider.Get()) : static_cast<AActor*>(KeepProbe.Get());
            if (KeepMount->GetRider() != KeepRider.Get() || Clients[ObservedSlot]->GetPawn() != KeepMount.Get()
                || KeepASC->GetAvatarActor() != ObservedExpectedAvatar || (Step == 2
                    && KeepASC->GetActorInfoRevision() != MountIdentities[ObservedSlot].TestReplacementEpoch))
            { Finish(false, TEXT("Explicit recipient test would overwrite a newer tuple/session")); return; }
            const uint64 ObservedBeforeEpoch = KeepASC->GetActorInfoRevision();
            KeepASC->InitAbilityActorInfo(KeepState.Get(), Step == 1 ? static_cast<AActor*>(KeepProbe.Get()) : static_cast<AActor*>(KeepRider.Get()));
            if (bFinished || MountRevision != ObservedRevision) return;
            auto* ObservedNextAvatar = Step == 1 ? static_cast<AActor*>(KeepProbe.Get()) : static_cast<AActor*>(KeepRider.Get());
            if (KeepASC->GetActorInfoRevision() != ObservedBeforeEpoch + 1 || KeepASC->GetOwnerActor() != KeepState.Get()
                || KeepASC->GetAvatarActor() != ObservedNextAvatar)
            { Finish(false, TEXT("A newer explicit recipient change superseded the fixture operation")); return; }
            MountIdentities[ObservedSlot].TestReplacementEpoch = Step == 1 ? KeepASC->GetActorInfoRevision() : 0;
            KeepState->ForceNetUpdate(); KeepRider->ForceNetUpdate(); KeepMount->ForceNetUpdate(); KeepProbe->ForceNetUpdate();
        }
        // Step3 deliberately performs no authority dismount. The real owning
        // client must issue the existing production mount RPC itself.
    }
    if (bFinished || MountRevision != ObservedRevision) return;
    if (Clients.Num() != 2 || Clients.ContainsByPredicate([](const auto& InPC) { return !IsValid(InPC.Get()) || !InPC->GetNetConnection(); }))
    { Finish(false, TEXT("Mount step callback retired an actual socket participant")); return; }
    TArray<uint64> ObservedPublicIds; TArray<float> ObservedPublicResources;
    for (int32 ObservedSlot = 0; ObservedSlot < 2; ++ObservedSlot)
    {
        ObservedPublicIds.Add(AuditMountPublicId(MountIdentities[ObservedSlot].Rider.Get()));
        ObservedPublicResources.Add(MountIdentities[ObservedSlot].Health); ObservedPublicResources.Add(MountIdentities[ObservedSlot].MaxHealth);
    }
    ObservedPublicIds.Add(AuditMountPublicId(MountActor));
    for (const auto& InPC : Clients)
    {
        if (bObservedNewCase) InPC->ClientPrepare(Case, Attacker, Target, Probe);
        InPC->ClientMountStep(Case, Step, ObservedPublicIds, ObservedPublicResources);
    }
    UE_LOG(LogProjectJAuditNetwork, Display, TEXT("MOUNT_BEGIN case=%d step=%d"), Case, Step);
}
void AProject_JAuditNetworkGameMode::UpdateMountAudit()
{
    const double ObservedNow = FPlatformTime::Seconds();
    if (Clients.ContainsByPredicate([](const auto& InPC) { return !IsValid(InPC.Get()) || !InPC->GetNetConnection(); }))
    { Finish(false, TEXT("Actual mount socket connection retired")); return; }
    if (Case == INDEX_NONE) { if (PrepareMountAudit()) StartMountStep(40, 0); return; }
    if (ObservedNow - StepStarted > 20)
    {
        if (Case == 43) InspectMountScopeRegistration(TEXT("timeout"), MountRegistrationPhase == 1, Step == 3, false);
        Finish(false, FString::Printf(TEXT("Mount deadline case=%d step=%d %s"), Case, Step, *MountLastDetail)); return;
    }
    if (AuditMountSpatialEnabled() && Case == 44) {UpdateMountSpatial();return;}
    if (Case == 43 && (MountRegistrationPhase == 1 || MountRegistrationPhase == 2))
    { UpdateMountRegistrationRestart(); return; }
    FString ObservedRows, ObservedDetail; uint64 ObservedEpochs[2] = {};
    const bool bObservedRecorded = MountRecordedCase == Case && MountRecordedStep == Step;
    if (!ReadAuditMountAudience(MountIdentities, Case, Step, MountActor, Probe, nullptr, -1, bObservedRecorded, false,
        ObservedRows, ObservedDetail, ObservedEpochs)) { MountLastDetail = ObservedDetail; return; }
    if (!bObservedRecorded)
    {
        MountRows += ObservedRows; MountRecordedCase = Case; MountRecordedStep = Step;
        for (int32 ObservedSlot = 0; ObservedSlot < 2; ++ObservedSlot) MountIdentities[ObservedSlot].Epoch = ObservedEpochs[ObservedSlot];
    }
    if (!AllConfirmed(Step)) return;
    if (Case == 43)
    {
        if (Step == 0)
        {
            if (!InspectMountScopeRegistration(TEXT("warm"), false, false, true)) return;
            MountRegistrationPhase = 1; bMountRegistrationActorPhaseObserved = false;
            MountRegistrationPhaseObserver = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &ThisClass::ObserveMountRegistrationActorPhase);
            StepStarted = ObservedNow;
            // Public Iris registration stop/start preserves bReplicates and
            // RemoteRole; SetReplicates would also reset the autonomous role.
            // No fixture Wake/Flush, possession, tuple, role or location repair.
            const TStrongObjectPtr<AProject_JAuditNetworkGameMode> KeepSelf(this);
            const TStrongObjectPtr<AProject_JMountCharacter> KeepMount(MountActor);
            UE::Net::FReplicationSystemUtil::StopReplicatingActor(KeepMount.Get(), FStopReplicatingActorParams(EEndPlayReason::RemovedFromWorld));
        }
        else if (Step == 2) StartMountStep(43, 3);
        else if (Step == 3 && InspectMountScopeRegistration(TEXT("dismounted"), false, true, true))
            Finish(true, TEXT("Original mount cohort and same-possession replication stop/restart, owner scope reuse, public/private restoration and production dismount passed"));
        return;
    }
    if (Case == 40) StartMountStep(41, 0);
    else if (Step < 3) StartMountStep(Case, Step + 1);
    else if (Case == 41) StartMountStep(42, 0);
    else if (Case == 42 && AuditMountReplicationRestartEnabled()) StartMountStep(43, 0);
    else if (Case == 42 && AuditMountSpatialEnabled()) StartMountStep(44, 0);
    else Finish(true, TEXT("Two actual mount owners and foreign SP public reads, replicated tuple rejection/recovery, production dismount RPCs and owner-only privacy passed"));
}
bool AProject_JAuditNetworkGameMode::InspectMountScopeRegistration(const TCHAR* PhaseName,
    bool bStopped, bool bDismounted, bool bRecord)
{
    auto* ObservedPC = Clients.IsValidIndex(0) ? Clients[0].Get() : nullptr;
    auto* ObservedMount = MountActor.Get();
    auto* ObservedRider = MountIdentities[0].Rider.Get();
    auto* ObservedState = MountIdentities[0].State.Get();
    auto* ObservedASC = MountIdentities[0].ASC.Get();
    APawn* ObservedPawn = IsValid(ObservedPC) ? ObservedPC->GetPawn() : nullptr;
    const auto* ObservedScope = IsValid(ObservedPC) ? ObservedPC->FindComponentByClass<UProject_JOwnedPawnScopeComponent>() : nullptr;
    const auto ObservedSnapshot = ObservedScope ? ObservedScope->Query() : FProjectJOwnedPawnScopeSnapshot{};
    const auto* ObservedBridge = IsValid(ObservedPawn) ? UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ObservedPawn) : nullptr;
    const auto ObservedCurrent = IsValid(ObservedBridge) ? ObservedBridge->GetReplicatedRefHandle(ObservedPawn) : UE::Net::FNetRefHandle{};
    const auto* ObservedMountBridge = IsValid(ObservedMount) ? UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(ObservedMount) : nullptr;
    const auto ObservedMountHandle = IsValid(ObservedMountBridge) ? ObservedMountBridge->GetReplicatedRefHandle(ObservedMount) : UE::Net::FNetRefHandle{};
    const bool bObservedFullMatch = ObservedCurrent.IsCompleteHandle() && ObservedSnapshot.PawnHandle.IsCompleteHandle()
        && UE::Net::FNetRefHandle::FullCompare(ObservedCurrent, ObservedSnapshot.PawnHandle);
    const bool bObservedRoundtrip = ObservedCurrent.IsCompleteHandle() && ObservedBridge
        && ObservedBridge->GetReplicatedObject(ObservedCurrent) == ObservedPawn;
    const bool bObservedSamePC = !MountRegistrationGroup || MountRegistrationController.Get() == ObservedPC;
    const bool bObservedSameMount = !MountRegistrationGroup || MountRegistrationPawn.Get() == ObservedMount;
    const bool bObservedParticipants = IsValid(ObservedPC) && !ObservedPC->IsActorBeingDestroyed() && ObservedPC->GetNetConnection()
        && IsValid(ObservedMount) && !ObservedMount->IsActorBeingDestroyed() && ObservedMount->HasAuthority()
        && IsValid(ObservedRider) && !ObservedRider->IsActorBeingDestroyed() && IsValid(ObservedState)
        && !ObservedState->IsActorBeingDestroyed() && ObservedState->GetOwner() == ObservedPC
        && ObservedPC->GetPlayerState<AProject_JPlayerState>() == ObservedState;
    const bool bObservedTuple = bObservedParticipants && IsValid(ObservedASC) && !ObservedASC->IsActorInfoChanging()
        && ObservedState->GetProjectJAbilitySystemComponent() == ObservedASC && ObservedASC->GetOwnerActor() == ObservedState
        && ObservedASC->GetAvatarActor() == ObservedRider && ObservedASC->GetActorInfoRevision() == MountIdentities[0].Epoch;
    const bool bObservedPossession = bObservedParticipants && (bDismounted
        ? ObservedPawn == ObservedRider && ObservedRider->GetController() == ObservedPC && ObservedState->GetPawn() == ObservedRider
            && !ObservedMount->GetRider() && ObservedMount->GetMountState() == EProject_JMountState::Unmounted
        : ObservedPawn == ObservedMount && ObservedMount->GetController() == ObservedPC && ObservedState->GetPawn() == ObservedMount
            && ObservedMount->GetPlayerState() == ObservedState && ObservedMount->GetRider() == ObservedRider
            && ObservedMount->GetMountState() == EProject_JMountState::Mounted);
    const bool bObservedLease = ObservedScope && ObservedSnapshot.bGroupValid && ObservedSnapshot.GroupToken
        && ObservedSnapshot.GroupsCreated && !ObservedSnapshot.bAdmissionFailed && !ObservedSnapshot.bClosed
        && (!MountRegistrationGroup || (ObservedSnapshot.GroupToken == MountRegistrationGroup
            && ObservedSnapshot.GroupsCreated == MountRegistrationGroupsCreated));
    const bool bObservedReplicated = IsValid(ObservedMount) && ObservedMount->GetIsReplicated();
    const bool bObservedBoundary = bStopped
        ? bObservedReplicated && !ObservedCurrent.IsValid() && !ObservedMountHandle.IsValid()
            && !ObservedSnapshot.bLogicalReady && !ObservedSnapshot.bIdentityMatches && !ObservedSnapshot.bMemberPresent && !bObservedFullMatch
        : bObservedReplicated && bObservedRoundtrip && bObservedFullMatch && ObservedSnapshot.bLogicalReady
            && ObservedSnapshot.bIdentityMatches && ObservedSnapshot.bMemberPresent && ObservedSnapshot.bAllowRequested
            && !ObservedSnapshot.bMutationPending;
    const bool bObservedValid = bObservedParticipants && bObservedSamePC && bObservedSameMount
        && bObservedTuple && bObservedPossession && bObservedLease && bObservedBoundary;
    const uint64 ObservedPawnId = AuditMountPublicId(ObservedPawn), ObservedRiderId = AuditMountPublicId(ObservedRider);
    const FString ObservedDetail = FString::Printf(TEXT("phase=%s actor_phase_observed=%d scope=%d same_pc=%d same_mount=%d same_possession=%d tuple_same=%d replicated=%d current_complete=%d current_system=%u current_id=%llu member_complete=%d member_system=%u member_id=%llu full_match=%d current_roundtrip=%d group=%llu groups=%u ready=%d identity=%d group_valid=%d member_present=%d allow=%d pending=%d admission_failed=%d pc_pawn_id=%llu mount_id=%llu rider_id=%llu"),
        PhaseName, bMountRegistrationActorPhaseObserved, ObservedScope != nullptr, bObservedSamePC, bObservedSameMount,
        bObservedPossession, bObservedTuple, bObservedReplicated, ObservedCurrent.IsCompleteHandle(),
        ObservedCurrent.IsCompleteHandle() ? ObservedCurrent.GetReplicationSystemId() : 0, ObservedCurrent.GetId(),
        ObservedSnapshot.PawnHandle.IsCompleteHandle(), ObservedSnapshot.PawnHandle.IsCompleteHandle() ? ObservedSnapshot.PawnHandle.GetReplicationSystemId() : 0,
        ObservedSnapshot.PawnHandle.GetId(), bObservedFullMatch, bObservedRoundtrip, ObservedSnapshot.GroupToken,
        ObservedSnapshot.GroupsCreated, ObservedSnapshot.bLogicalReady, ObservedSnapshot.bIdentityMatches,
        ObservedSnapshot.bGroupValid, ObservedSnapshot.bMemberPresent, ObservedSnapshot.bAllowRequested,
        ObservedSnapshot.bMutationPending, ObservedSnapshot.bAdmissionFailed, ObservedPawnId, ObservedMountHandle.GetId(), ObservedRiderId);
    if (ObservedDetail != MountRegistrationLastDetail && MountRegistrationLoggedStates < 16)
    {
        ++MountRegistrationLoggedStates;
        UE_LOG(LogProjectJAuditNetwork, Display, TEXT("MOUNT_SCOPE_REGISTRATION %s"), *ObservedDetail);
    }
    MountRegistrationLastDetail = ObservedDetail;
    MountLastDetail = ObservedDetail;
    if (!bRecord || !bObservedValid) return bObservedValid;
    if (!MountRegistrationGroup)
    {
        MountRegistrationController = ObservedPC; MountRegistrationPawn = ObservedMount;
        MountRegistrationGroup = ObservedSnapshot.GroupToken; MountRegistrationGroupsCreated = ObservedSnapshot.GroupsCreated;
    }
    MountRegistrationRows += FString::Printf(TEXT("%s,-1,0,%llu,%u,%llu,%llu,%d,%d,%d,%d,%d,%d,%d,%d,%d,%llu,%llu,%llu\n"),
        PhaseName, ObservedSnapshot.GroupToken, ObservedSnapshot.GroupsCreated, ObservedSnapshot.PawnHandle.GetId(),
        ObservedCurrent.GetId(), bObservedFullMatch, ObservedSnapshot.bLogicalReady, ObservedSnapshot.bIdentityMatches,
        ObservedSnapshot.bGroupValid, ObservedSnapshot.bMemberPresent, ObservedSnapshot.bAllowRequested,
        ObservedSnapshot.bMutationPending, ObservedSnapshot.bAdmissionFailed, bObservedReplicated,
        ObservedPawnId, ObservedMountHandle.GetId(), ObservedRiderId);
    return true;
}
void AProject_JAuditNetworkGameMode::ObserveMountRegistrationActorPhase(UWorld* ObservedWorld, ELevelTick, float)
{
    if (ObservedWorld == GetWorld() && Case == 43 && (MountRegistrationPhase == 1 || MountRegistrationPhase == 2))
        bMountRegistrationActorPhaseObserved = true; // Observation only; no replication/owner-scope mutation.
}
void AProject_JAuditNetworkGameMode::UpdateMountRegistrationRestart()
{
    if (!bMountRegistrationActorPhaseObserved) return;
    if (MountRegistrationPhase == 1)
    {
        if (!InspectMountScopeRegistration(TEXT("stopped"), true, false, true)) return;
        const TStrongObjectPtr<AProject_JAuditNetworkGameMode> KeepSelf(this);
        const TStrongObjectPtr<AProject_JMountCharacter> KeepMount(MountActor);
        const uint64 ObservedRevision = MountRevision;
        MountRegistrationPhase = 2; bMountRegistrationActorPhaseObserved = false;
        UE::Net::FReplicationSystemUtil::StartReplicatingActor(KeepMount.Get());
        if (bFinished || MountRevision != ObservedRevision || MountActor != KeepMount.Get()) return;
        StepStarted = FPlatformTime::Seconds();
    }
    else if (MountRegistrationPhase == 2)
    {
        if (!InspectMountScopeRegistration(TEXT("restored"), false, false, true)) return;
        MountRegistrationPhase = 3;
        FWorldDelegates::OnWorldPostActorTick.Remove(MountRegistrationPhaseObserver); MountRegistrationPhaseObserver.Reset();
        // Only now publish the actual primary mount identity through the existing
        // observation RPC. No fixture setter repairs the missing production wake.
        StartMountStep(43, 2);
    }
}
void AProject_JAuditNetworkGameMode::CleanupMountAudit()
{
    if (bMountCleaning) return; bMountCleaning = true; ++MountRevision;
    if(MountSpatialCamera)
    {
        const TStrongObjectPtr<ACameraActor> KeepCamera(MountSpatialCamera);MountSpatialCamera=nullptr;
        if(Clients.IsValidIndex(1)&&IsValid(Clients[1])&&!Clients[1]->IsActorBeingDestroyed()&&Clients[1]->PlayerCameraManager&&
            Clients[1]->PlayerCameraManager->ViewTarget.Target==KeepCamera.Get())Clients[1]->SetViewTarget(Clients[1]->GetPawn());
        if(KeepCamera.IsValid()&&!KeepCamera->IsActorBeingDestroyed())KeepCamera->Destroy();
    }
    FWorldDelegates::OnWorldPostActorTick.Remove(MountRegistrationPhaseObserver); MountRegistrationPhaseObserver.Reset();
    const TStrongObjectPtr<AProject_JMountCharacter> KeepMount(MountActor); MountActor = nullptr;
    const TStrongObjectPtr<AProject_JAuditCueProbe> KeepProbe(Probe);
    // Restore only the exact explicit test replacement. Never overwrite a
    // newer recipient or seize a controller that legitimately moved elsewhere.
    for (const auto& ObservedIdentity : MountIdentities)
    {
        const TStrongObjectPtr<AProject_JAuditNetworkPlayer> KeepRider(ObservedIdentity.Rider.Get());
        const TStrongObjectPtr<AProject_JPlayerState> KeepState(ObservedIdentity.State.Get());
        const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepASC(ObservedIdentity.ASC.Get());
        if (KeepRider && !KeepRider->IsActorBeingDestroyed() && KeepState && !KeepState->IsActorBeingDestroyed() && KeepASC && KeepProbe
            && ObservedIdentity.TestReplacementEpoch && KeepASC->GetActorInfoRevision() == ObservedIdentity.TestReplacementEpoch
            && KeepState->GetProjectJAbilitySystemComponent() == KeepASC.Get()
            && KeepASC->GetOwnerActor() == KeepState.Get() && KeepASC->GetAvatarActor() == KeepProbe.Get()
            && KeepMount && KeepMount->GetRider() == KeepRider.Get() && KeepMount->GetPlayerState() == KeepState.Get()
            && KeepState->GetPawn() == KeepMount.Get() && KeepMount->GetController() == KeepState->GetOwner()
            && !KeepASC->IsActorInfoChanging() && GetWorld() && !GetWorld()->bIsTearingDown)
            KeepASC->InitAbilityActorInfo(KeepState.Get(), KeepRider.Get());
    }
    if (KeepMount && !KeepMount->IsActorBeingDestroyed())
    {
        auto* ObservedRider = KeepMount->GetRider();
        auto* ObservedController = KeepMount->GetController();
        const bool bObservedRestore = IsValid(ObservedRider) && !ObservedRider->IsActorBeingDestroyed() && ObservedController
            && Clients.Contains(Cast<AProject_JAuditNetworkController>(ObservedController))
            && ObservedController->GetPawn() == KeepMount.Get() && GetWorld() && !GetWorld()->bIsTearingDown;
        if (ObservedRider) KeepMount->DismountRider(true, bObservedRestore);
        KeepMount->Destroy();
    }
}
void AProject_JAuditNetworkGameMode::EndPlay(EEndPlayReason::Type InReason)
{
    if (AuditQuestHistoryEnabled()) CleanupQuestHistoryAudit();
    if (AuditNPCAssemblyEnabled()) CleanupNPCAssemblyAudit();
    if (AuditSessionEnabled()) CleanupSessionAudit();
    if (AuditWarningEnabled()) CleanupWarningAudit();
    if (AuditMountEnabled()) CleanupMountAudit();
    Super::EndPlay(InReason);
}

bool AProject_JAuditNetworkGameMode::AllConfirmed(int32 InStep)const{for(AProject_JAuditNetworkController* PC:Clients)if(PC->ConfirmedCase!=Case||PC->ConfirmedStep!=InStep)return false;return Clients.Num()==2;}
void AProject_JAuditNetworkGameMode::StartCase(int32 Next){
 Case=Next;CaseStarted=StepStarted=FPlatformTime::Seconds();Step=0;bSent=false;bAuthorityHit=false;
 Attacker->AuditHit->EndAttack();Target->GetAbilitySystemComponent()->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(),100.f);
 // Reset the live presentation through production APIs and let both real
 // clients acknowledge the empty boundary before starting another trial.
 Probe->Presentation->EndAttackPresentation();
 for(AProject_JAuditNetworkController* PC:Clients){PC->ConfirmedCase=PC->ConfirmedStep=INDEX_NONE;PC->ClientPrepare(Case,Attacker,Target,Probe);}
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("CASE_BEGIN %d Attacker=%s pos=%s Target=%s begun=%d pos=%s Probe=%s begun=%d pos=%s"),Case,*GetNameSafe(Attacker),*Attacker->GetActorLocation().ToString(),*GetNameSafe(Target),Target->HasActorBegunPlay(),*Target->GetActorLocation().ToString(),*GetNameSafe(Probe),Probe->HasActorBegunPlay(),*Probe->GetActorLocation().ToString());
}
void AProject_JAuditNetworkGameMode::Tick(float Delta){
 Super::Tick(Delta);if(!AuditEnabled()||bFinished)return;const double Now=FPlatformTime::Seconds();
 if(Now-Started>140){Finish(false,TEXT("Fixture deadline; no forced process termination"));return;}
 if(AuditQuestHistoryEnabled()){UpdateQuestHistoryAudit();return;}
 if(AuditNPCAssemblyEnabled()){UpdateNPCAssemblyAudit();return;}
 if(AuditSessionEnabled()){UpdateSessionAudit();return;}
 if(AuditWarningEnabled()){UpdateWarningAudit();return;}
 if(AuditMountEnabled()){UpdateMountAudit();return;}
 if(Case==INDEX_NONE){if(Clients.Num()==2&&Attacker&&Attacker->FindComponentByClass<UProject_JEquipmentRuntimeComponent>()->GetWeaponRevision()!=0&&Now-Started>3)StartCase(0);return;}
 if(Now-CaseStarted>20){Finish(false,FString::Printf(TEXT("Case %d step %d timed out"),Case,Step));return;}
 if(Step==0&&Now-StepStarted>1){
  for(AProject_JAuditNetworkController* PC:Clients)if(PC->ConfirmedCase!=Case||PC->ConfirmedStep!=0)PC->ClientPrepare(Case,Attacker,Target,Probe);
  StepStarted=Now;
 }
 auto* Hit=Attacker->AuditHit.Get();
 if(Case<6){
  if(Step==0&&AllConfirmed(0)){Hit->BeginAttackNode(AuditNodeA,Attack,17);Hit->SetHitWindowOpen(true);Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));Step=1;StepStarted=Now;}
  if(Hit->GetActiveAttackNodeTag().IsValid())Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));
  if(Step==1&&Now-StepStarted>.15){
   SampleTime=GetWorld()->GetTimeSeconds()-.03;
   // Prove the actual client -> inherited SSR RPC preserves float64, rather
   // than accidentally choosing a sample exactly representable as float32.
   if(SampleTime==static_cast<double>(static_cast<float>(SampleTime))) SampleTime+=1.e-9;
   if(Case==1){bAuthorityHit=Hit->ProcessAuthorityHit(Target);if(!bAuthorityHit||!FMath::IsNearlyEqual(Health(Target),90.f)){Finish(false,TEXT("Authority damage before transition did not apply exactly once"));return;}}
   Clients[0]->ClientSendHit(Case,SampleTime);
   if(Case==1||Case==2||Case==3){Hit->BeginAttackNode(AuditNodeB,Attack,17);Hit->SetHitWindowOpen(true);Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));}
   if(Case==3){Hit->BeginAttackNode(AuditNodeA,Attack,17);Hit->SetHitWindowOpen(true);Hit->RecordAuthoritativeTrace(TraceStart(Attacker),TraceEnd(Attacker));}
   if(Case==4)Hit->EndAttack();
   Step=2;StepStarted=Now;
  }
  if(Step==2&&Now-StepStarted>1.2){
   const float Expected=(Case==0||Case==1||Case==5)?90.f:100.f;
   const int32 ExpectedApplications=(Case==0||Case==5)?1:0;
   if(Arrivals.FindRef(Case)==0||DamageApplications.FindRef(Case)!=ExpectedApplications||!FMath::IsNearlyEqual(Health(Target),Expected)){
    Finish(false,FString::Printf(TEXT("SSR case=%d arrivals=%d applications=%d expected=%d health=%.1f expected=%.1f"),Case,Arrivals.FindRef(Case),DamageApplications.FindRef(Case),ExpectedApplications,Health(Target),Expected));return;
   }
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientCheckHealth(Case,Expected);Step=3;
  }
  if(Step==3&&AllConfirmed(2)){
   if(Case==5&&FParse::Param(FCommandLine::Get(),TEXT("ProjectJAuditSSROnly"))){Finish(true,TEXT("Six SSR cases and exact float64 timestamp round-trip observed on authority/owner/simulated roles"));return;}
   StartCase(Case==5?10:Case+1);
  }return;
 }
 if(Case==10||Case==11){
  const int32 Base=Case*10;
  if(Case==11&&Step==0&&AllConfirmed(0)){
   FProject_JReplicatedCombatPresentationState Baseline;Baseline.ActiveAttackTag=AuditNodeA;Baseline.AttackInstance=Case+1;Baseline.Revision=Base+3;Baseline.EventOrder=Base+2;
   Baseline.ActiveLoopingCueTags.AddTag(AuditCueA);Baseline.ActiveLoopingCueTags.AddTag(AuditCueB);
   Clients[0]->PublishCueState(Probe,Baseline);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveCue(Case,Base+2,true,true,true);
   Step=4;
  }
  if((Case==10&&Step==0&&AllConfirmed(0))||(Case==11&&Step==4&&AllConfirmed(2))){
   Pending={};Pending.ActiveAttackTag=AuditNodeA;Pending.AttackInstance=Case+1;Pending.Revision=Base+(Case==10?3:4);Pending.EventOrder=Base+4;
   Pending.ActiveLoopingCueTags.AddTag(AuditCueB);
   if(Case==10)Pending.ActiveLoopingCueTags.AddTag(AuditCueA);
   Clients[0]->SendCueDelta(Probe,true,Base+5,Pending.AttackInstance,Pending);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveCue(Case,Base+5,Case==10,true,false);
   Step=1;StepStarted=Now;
  }
  // Retransmit the same semantic event/order. A dropped RPC must be observed
  // before the property is published; receipts prove this actual wire order.
  if(Step==1&&!AllConfirmed(1)&&Now-StepStarted>.3){Clients[0]->SendCueDelta(Probe,true,Base+5,Pending.AttackInstance,Pending);StepStarted=Now;}
  if(Step==1&&AllConfirmed(1)){
   Clients[0]->PublishCueState(Probe,Pending);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveCue(Case,Base+5,Case==10,true,true);
   Step=2;
  }
  if(Step==2&&AllConfirmed(2)){if(Case==10)StartCase(11);else StartCase(20);}
 }
 if(Case==20){
  auto* State=Attacker->GetLocomotionAnimStateComponent();auto* Events=Attacker->FindComponentByClass<UProject_JReplicatedAnimEventComponent>();
  if(Step==0&&AllConfirmed(0)){
   State->HandleReplicatedFallOffStarted();Events->DispatchFallOffStarted();
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveMovement(Case,true);Step=1;
  }
  if(Step==1&&AllConfirmed(1)){
   State->HandleReplicatedLandingStarted(1,0.f,600.f,false,false,false);Events->DispatchLandingStarted(600.f,false,false,false);
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientObserveMovement(Case,false);Step=2;
  }
  if(Step==2&&AllConfirmed(2)){
   const int32 Count=Attacker->GetAbilitySystemComponent()->GetTagCount(FProject_JGameplayTags::Get().State_Movement_InAir);
   if(Count!=0){Finish(false,TEXT("Authority InAir contribution survived remote landing"));return;}StartCase(30);
  }
 }
 if(Case==30){
  if(Step==0&&AllConfirmed(0)){
   if(!Clients[0]->ConfigureRate(Attacker)){Finish(false,TEXT("Isolated saved rate style/montage missing"));return;}
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientRate(Case,false);Step=1;
  }
  if(Step==1&&AllConfirmed(1)){
   if(!FMath::IsNearlyEqual(Clients[0]->ReadRate(Attacker),1.f)||!Clients[0]->AdvanceRate(Attacker)){Finish(false,TEXT("Authority rate/task transition failed"));return;}
   for(AProject_JAuditNetworkController* PC:Clients)PC->ClientRate(Case,true);Step=2;
  }
  if(Step==2&&AllConfirmed(2))Finish(true,TEXT("SSR damage, cue recovery, remote landing and montage rates observed on authority/owner/simulated roles"));
 }
}
void AProject_JAuditNetworkGameMode::Observe(AProject_JAuditNetworkController* PC,int32 InCase,int32 InStep,bool Success,const FString& Detail){
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("OBSERVE slot=%d case=%d step=%d success=%d %s"),PC->Slot,InCase,InStep,Success,*Detail);
 if(bFinished||InCase!=Case||!Clients.Contains(PC))return;
 if((AuditMountEnabled()||AuditWarningEnabled()||AuditSessionEnabled()||AuditNPCAssemblyEnabled()||AuditQuestHistoryEnabled())&&InStep!=Step)return;
 if(!Success){Finish(false,FString::Printf(TEXT("Client %d case %d step %d: %s"),PC->Slot,InCase,InStep,*Detail));return;}
 PC->ConfirmedCase=InCase;PC->ConfirmedStep=InStep;
}
void AProject_JAuditNetworkGameMode::RecordHit(int32 Seq,double Time,float Before,float After,FGameplayTag Node,FGameplayTag Current){
 const int32 RequestCase=Seq/100;++Arrivals.FindOrAdd(RequestCase);if(After<Before)++DamageApplications.FindOrAdd(RequestCase);
 Rows+=FString::Printf(TEXT("%d,%d,%.17g,%.17g,%.17g,%.1f,%.1f,%s,%s\n"),RequestCase,Seq,Time,GetWorld()->GetTimeSeconds(),GetWorld()->GetTimeSeconds()-Time,Before,After,*Node.ToString(),*Current.ToString());
 if(RequestCase==Case&&Time!=SampleTime){Finish(false,FString::Printf(TEXT("SSR timestamp lost precision on the actual wire: got=%.17g expected=%.17g"),Time,SampleTime));return;}
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("HIT case=%d seq=%d age=%.4f before=%.1f after=%.1f current=%s"),RequestCase,Seq,GetWorld()->GetTimeSeconds()-Time,Before,After,*Current.ToString());
}
void AProject_JAuditNetworkGameMode::Finish(bool Success,const FString& Why){
 if(bFinished)return;bFinished=true;
 if(AuditQuestHistoryEnabled())
 {
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(QuestHistoryRows,*(Root/TEXT("quest-history-authority.csv")))&&Success;
  CleanupQuestHistoryAudit();
 }
 if(AuditSessionEnabled())
 {
  Success=CleanupSessionAudit()&&Success;
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(SessionRows,*(Root/TEXT("session-authority.csv")))&&Success;
  const FString Summary=FString::Printf(TEXT("{\"schema\":1,\"success\":%s,\"cleanup_passed\":%s,\"captured_application_attempted\":%s,\"captured_application_applied\":%s,\"external_credential_verification_claimed\":false,\"network_persistence_claimed\":false}\n"),
   Success?TEXT("true"):TEXT("false"),bSessionCleanupPassed?TEXT("true"):TEXT("false"),bSessionAppAttempted?TEXT("true"):TEXT("false"),bSessionAppApplied?TEXT("true"):TEXT("false"));
  Success=FFileHelper::SaveStringToFile(Summary,*(Root/TEXT("session-summary.json")))&&Success;
 }
 if(AuditNPCAssemblyEnabled())
 {
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(NPCAssemblyRows,*(Root/TEXT("npc-assembly-authority.csv")))&&Success;
  CleanupNPCAssemblyAudit();
 }
 if(AuditWarningEnabled())
 {
  const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
  Success=FFileHelper::SaveStringToFile(WarningRows,*(Root/TEXT("warning-authority.csv")))&&Success;
  CleanupWarningAudit();
 }
 if(AuditMountEnabled())
 {
  if(AuditMountReplicationRestartEnabled())
  {
   if(!Success&&Case==43) InspectMountScopeRegistration(TEXT("final"),MountRegistrationPhase==1,Step==3,false);
   WriteAudit(TEXT("mount-scope-registration.csv"),MountRegistrationRows);
   if(!Success) WriteAudit(TEXT("mount-scope-registration-failure.txt"),MountRegistrationLastDetail+TEXT("\n"));
  }
  if(AuditMountSpatialEnabled())
  {
   const FString Root=AuditOutput();IFileManager::Get().MakeDirectory(*Root,true);
   Success=FFileHelper::SaveStringToFile(MountSpatialRows,*(Root/TEXT("mount-spatial-authority.csv")))&&Success;
  }
  CleanupMountAudit();WriteAudit(TEXT("mount-authority.csv"),MountRows);
 }
 WriteAudit(TEXT("hits.csv"),Rows);WriteAudit(TEXT("server.txt"),FString::Printf(TEXT("success=%d case=%d step=%d mode=%d reason=%s\n"),Success,Case,Step,int32(GetNetMode()),*Why));
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("FINISH success=%d %s"),Success,*Why);
 for(AProject_JAuditNetworkController* PC:Clients)
  if((!AuditMountEnabled()&&!AuditWarningEnabled()&&!AuditSessionEnabled()&&!AuditNPCAssemblyEnabled()&&!AuditQuestHistoryEnabled())||(IsValid(PC)&&!PC->IsActorBeingDestroyed()&&PC->GetNetConnection()))PC->ClientFinish(Success);
 // Let reliable finish receipts drain under lag/loss. The timeout subsystem
 // independently bounds processes whose connection never succeeds.
 FTimerHandle Handle;GetWorldTimerManager().SetTimer(Handle,[Success](){FPlatformMisc::RequestExitWithStatus(false,Success?0:1);},3.f,false);
}


FProject_JReplicatedCombatPresentationState AProject_JAuditNetworkController::ReadWarningState(AProject_JAuditCueProbe* InProbe) const
{
 if(AuditWarningEnabled()&&HasAuthority()&&IsValid(InProbe)&&!InProbe->IsActorBeingDestroyed()
  &&InProbe->GetWorld()==GetWorld()&&IsValid(InProbe->Presentation))return InProbe->Presentation->ReplicatedPresentationState;
 return {};
}
void AProject_JAuditNetworkController::ClientWarningStep_Implementation(int32 InStep,AProject_JAuditCueProbe* InProbe,uint32 InInstance,int32 InOrder)
{
 if(!AuditWarningEnabled()||HasAuthority()||InStep<0||InStep>3||InStep<WarningStep)return;
 if(Case!=50)
 {
  WarningRows=TEXT("case,step,slot,owner,mode,role,probe_id,frame,instance,revision,order,warning_count,marker,registered,render_state,lines,tick,attached,profile_null,pending_loops,active_niagara,marker_type,passed\n");
  Case=50;Probe=InProbe;WarningProbeId=0;WarningIdentity.Reset();
 }
 else if(InProbe)
 {
  if(IsValid(Probe)&&Probe!=InProbe){ServerObserve(50,InStep,false,TEXT("Warning probe reference was replaced"));return;}
  Probe=InProbe;
 }
 FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditSlot="),Slot);
 WarningStep=InStep;WarningExpectedInstance=InInstance;WarningRequiredOrder=InOrder;
 WarningStableFrame=MAX_uint64;bWarningRecorded=false;ObserveStarted=FPlatformTime::Seconds();
}
void AProject_JAuditNetworkController::ObserveWarningAudit()
{
 if(WarningStep==INDEX_NONE||bWarningRecorded)return;
 if(!Probe&&!WarningProbeId)
 {
  for(TActorIterator<AProject_JAuditCueProbe> It(GetWorld());It;++It)
  {
   if(Probe){bWarningRecorded=true;ServerObserve(50,WarningStep,false,TEXT("Multiple warning probes"));return;}
   Probe=*It;
  }
 }
 auto* P=Probe.Get();auto* C=IsValid(P)?P->Presentation.Get():nullptr;
 const uint64 Id=IsValid(P)?AuditMountPublicId(P):0;
 const bool Binding=IsValid(P)&&!P->IsActorBeingDestroyed()&&P->HasActorBegunPlay()&&P->GetWorld()==GetWorld()
  &&IsValid(C)&&!C->IsBeingDestroyed()&&C->HasBegunPlay()&&C->IsRegistered()&&C->GetOwner()==P&&C->GetWorld()==GetWorld()
  &&GetNetMode()==NM_Client&&P->GetLocalRole()==ROLE_SimulatedProxy&&!P->IsLocallyControlled()&&Id;
 bool Same=Binding;
 if(Binding)
 {
  if(!WarningProbeId){WarningProbeId=Id;WarningIdentity=P;}
  Same=WarningProbeId==Id&&WarningIdentity.IsValid()
   &&WarningIdentity.HasSameIndexAndSerialNumber(TWeakObjectPtr<AProject_JAuditCueProbe>(P));
 }
 ULineBatchComponent* M=IsValid(C)?C->WarningMarker.Get():nullptr;
 const bool Marker=IsValid(M)&&!M->IsBeingDestroyed();
 const bool Registered=Marker&&M->IsRegistered(),Render=Marker&&M->IsRenderStateCreated();
 const int32 Lines=Marker?M->BatchedLines.Num():0;
 const bool Tick=Marker&&M->IsComponentTickEnabled(),Attached=Marker&&M->GetOwner()==P&&M->GetWorld()==GetWorld()&&M->GetAttachParent()==P->GetRootComponent();
 const bool Type=Marker&&M->IsA<UProject_JGameplayWarningMarkerComponent>();
 const bool ProfileNull=IsValid(C)&&!C->BasePresentationSet;
 const int32 Warnings=IsValid(C)?C->ActiveWarningCues.Num():-1;
 const int32 Revision=IsValid(C)?C->LastAppliedPresentationRevision:-1,Order=IsValid(C)?C->LastAppliedRecoveryEventOrder:-1;
 const uint32 Instance=IsValid(C)?C->ReplicatedPresentationState.AttackInstance:0;
 const int32 Pending=IsValid(C)?C->PendingLoopingCues.Num():-1,Niagara=IsValid(C)?C->ActiveLoopingCues.Num():-1;
 const bool End=WarningStep!=2;
 const bool Meaning=End?(Warnings==0&&!Marker):(Warnings==1&&C->ActiveWarningCues.HasTagExact(AuditCueA)
  &&Marker&&Registered&&Render&&Lines==20&&!Tick&&Attached&&Type);
 const bool Current=WarningStep==0||(Instance==WarningExpectedInstance&&Order>=WarningRequiredOrder&&Revision>0);
 const bool Complete=Same&&ProfileNull&&Pending==0&&Niagara==0&&Meaning&&Current;
 if(Complete)
 {
  if(WarningStableFrame==MAX_uint64){WarningStableFrame=GFrameCounter;return;}
  if(GFrameCounter<=WarningStableFrame)return; // Two real PlayerTicks; never pump or overwrite an engine clock.
  const FString OwnerName=P->GetName();
  if(OwnerName.Contains(TEXT(","))||OwnerName.Contains(TEXT("\n"))){bWarningRecorded=true;ServerObserve(50,WarningStep,false,TEXT("Unexpected warning actor CSV name"));return;}
#if DO_ENABLE_NET_TEST
  if(WarningStep==0)
  {
   auto* Driver=GetWorld()->GetNetDriver();
   if(!Driver){bWarningRecorded=true;ServerObserve(50,0,false,TEXT("Warning native client NetDriver missing"));return;}
   const auto& Sim=Driver->PacketSimulationSettings;
   UE_LOG(LogProjectJAuditNetwork,Display,TEXT("WARNING_PACKETS slot=%d lag=%d variance=%d loss=%d order=%d duplicate=%d netmode=%d"),
    Slot,Sim.PktLag,Sim.PktLagVariance,Sim.PktLoss,Sim.PktOrder,Sim.PktDup,int32(GetNetMode()));
  }
#endif
  WarningRows+=FString::Printf(TEXT("50,%d,%d,%s,%d,%d,%llu,%llu,%u,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,1\n"),
   WarningStep,Slot,*OwnerName,int32(GetNetMode()),int32(P->GetLocalRole()),Id,GFrameCounter,Instance,Revision,Order,Warnings,
   Marker,Registered,Render,Lines,Tick,Attached,ProfileNull,Pending,Niagara,Type);
  bWarningRecorded=true;
  ServerObserve(50,WarningStep,true,FString::Printf(TEXT("Warning native state owner=%s revision=%d order=%d instance=%u warnings=%d marker=%d render=%d"),*OwnerName,Revision,Order,Instance,Warnings,Marker,Render));
 }
 else
 {
  WarningStableFrame=MAX_uint64;
  if(FPlatformTime::Seconds()-ObserveStarted>8)
  {
   bWarningRecorded=true;
   ServerObserve(50,WarningStep,false,FString::Printf(TEXT("Warning state unresolved binding=%d same=%d profile_null=%d rev=%d order=%d instance=%u required_instance=%u required_order=%d warnings=%d marker=%d registered=%d render=%d lines=%d tick=%d attached=%d type=%d pending=%d niagara=%d"),
    Binding,Same,ProfileNull,Revision,Order,Instance,WarningExpectedInstance,WarningRequiredOrder,Warnings,Marker,Registered,Render,Lines,Tick,Attached,Type,Pending,Niagara));
  }
 }
}
void AProject_JAuditNetworkController::EndPlay(const EEndPlayReason::Type InReason)
{
 if(AuditQuestHistoryEnabled())
 {
  if(auto* State=QuestHistoryOwnState.Get())if(auto* Component=State->FindComponentByClass<UProject_JQuestComponent>())Component->CancelHistoryPageRequest(QuestHistoryReturnedNonce);
  QuestHistoryStep=INDEX_NONE;bQuestHistoryRecorded=true;QuestHistoryOwnState.Reset();QuestHistoryForeignState.Reset();
 }
 if(AuditNPCAssemblyEnabled()){NPCAssemblyStep=INDEX_NONE;bNPCAssemblyRecorded=true;NPCAssemblyIdentity.Reset();}
 if(AuditWarningEnabled()){WarningStep=INDEX_NONE;bWarningRecorded=true;WarningIdentity.Reset();Probe=nullptr;}
 Super::EndPlay(InReason);
}
bool AProject_JAuditNetworkGameMode::RecordWarningAuthority(const TCHAR* Transition,uint64 Lease,bool bExpectedActive)
{
 if(!IsValid(Probe)||!IsValid(Probe->Presentation)||Clients.Num()!=2||!IsValid(Clients[0]))return false;
 const auto State=Clients[0]->ReadWarningState(Probe);
 const FString OwnerName=Probe->GetName();if(OwnerName.Contains(TEXT(","))||OwnerName.Contains(TEXT("\n")))return false;
 const bool ProfileNull=!Probe->Presentation->BasePresentationSet;
 const bool Passed=ProfileNull&&Lease&&State.AttackInstance&&State.Revision>0&&State.EventOrder>0
  &&State.ActiveWarningCueTags.Num()==(bExpectedActive?1:0)
  &&Probe->Presentation->IsCueLeaseCurrent(Lease)==bExpectedActive;
 WarningRows+=FString::Printf(TEXT("50,%d,%s,%s,%d,%d,%llu,%u,%d,%d,%d,%llu,%d,%d\n"),
  Step,Transition,*OwnerName,int32(GetNetMode()),int32(Probe->GetLocalRole()),GFrameCounter,State.AttackInstance,State.Revision,State.EventOrder,
  State.ActiveWarningCueTags.Num(),Lease,ProfileNull,Passed);
 return Passed;
}
void AProject_JAuditNetworkGameMode::UpdateWarningAudit()
{
 const double Now=FPlatformTime::Seconds();
 if(Clients.Num()!=2)return;
 for(AProject_JAuditNetworkController* PC:Clients)if(!IsValid(PC)||PC->IsActorBeingDestroyed()||!PC->GetNetConnection())
 {Finish(false,TEXT("Warning observer connection retired"));return;}
 if(!IsValid(Probe)||Probe->IsActorBeingDestroyed()||!IsValid(Probe->Presentation))
 {Finish(false,TEXT("Warning authority probe retired"));return;}
 if(Case==INDEX_NONE)
 {
  Case=50;Step=0;CaseStarted=StepStarted=Now;
  for(AProject_JAuditNetworkController* PC:Clients){PC->ConfirmedCase=PC->ConfirmedStep=INDEX_NONE;PC->ClientWarningStep(0,Probe,0,0);}
  return;
 }
 if(Now-CaseStarted>20){Finish(false,FString::Printf(TEXT("Warning case step%d timed out"),Step));return;}
 if(Step==0&&AllConfirmed(0))
 {
  Step=1;StepStarted=Now;
  const TStrongObjectPtr<AProject_JAuditCueProbe> KeepProbe(Probe);
  const TStrongObjectPtr<UProject_JCombatPresentationComponent> C(Probe->Presentation);
  const uint64 PulseFrame=GFrameCounter;
  C->BeginAttackPresentation(AuditNodeA);WarningLease=C->BeginCueLease(AuditCueA,true);
  if(!RecordWarningAuthority(TEXT("pulse_start"),WarningLease,true)){Finish(false,TEXT("Native pulse warning start failed"));return;}
  C->EndCueLease(WarningLease);
  if(!RecordWarningAuthority(TEXT("pulse_end"),WarningLease,false)||GFrameCounter!=PulseFrame)
  {Finish(false,TEXT("Pulse must start/end natively in exactly one server frame"));return;}
  const auto State=Clients[0]->ReadWarningState(Probe);
  for(AProject_JAuditNetworkController* PC:Clients)PC->ClientWarningStep(1,Probe,State.AttackInstance,State.EventOrder);
  return;
 }
 if(Step==1&&AllConfirmed(1))
 {
  Step=2;StepStarted=Now;
  const TStrongObjectPtr<AProject_JAuditCueProbe> KeepProbe(Probe);
  const TStrongObjectPtr<UProject_JCombatPresentationComponent> KeepPresentation(Probe->Presentation);
  Probe->Presentation->BeginAttackPresentation(AuditNodeA);WarningLease=Probe->Presentation->BeginCueLease(AuditCueA,true);
  if(!RecordWarningAuthority(TEXT("long_start"),WarningLease,true)){Finish(false,TEXT("Native persistent warning start failed"));return;}
  const auto State=Clients[0]->ReadWarningState(Probe);
  for(AProject_JAuditNetworkController* PC:Clients)PC->ClientWarningStep(2,Probe,State.AttackInstance,State.EventOrder);
  return;
 }
 if(Step==2&&AllConfirmed(2))
 {
  Step=3;StepStarted=Now;
  const TStrongObjectPtr<AProject_JAuditCueProbe> KeepProbe(Probe);
  const TStrongObjectPtr<UProject_JCombatPresentationComponent> KeepPresentation(Probe->Presentation);
  Probe->Presentation->EndAttackPresentation();
  if(!RecordWarningAuthority(TEXT("attack_end"),WarningLease,false)){Finish(false,TEXT("Native attack end did not retire warning"));return;}
  const auto State=Clients[0]->ReadWarningState(Probe);
  for(AProject_JAuditNetworkController* PC:Clients)PC->ClientWarningStep(3,Probe,State.AttackInstance,State.EventOrder);
  return;
 }
 if(Step==3&&AllConfirmed(3))Finish(true,TEXT("Warning pulse finalempty and persistent native marker/attack end observed on both actual sockets; pulse RPC entry is separately verified from native logs"));
}
void AProject_JAuditNetworkGameMode::CleanupWarningAudit()
{
 const TStrongObjectPtr<AProject_JAuditCueProbe> Old(Probe);Probe=nullptr;
 if(Old.IsValid()&&!Old->IsActorBeingDestroyed())
 {
  if(IsValid(Old->Presentation)&&!Old->Presentation->IsBeingDestroyed()&&Old->Presentation->GetActiveAttackTag().IsValid())Old->Presentation->EndAttackPresentation();
  Old->Destroy();
 }
}

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAuditExplicitSlotOptionsTest,"ProjectJ.NetworkAudit.ExplicitSlotLoginOptions",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAuditExplicitSlotOptionsTest::RunTest(const FString&)
{
 TestEqual(TEXT("Slot0 preserves unrelated login options"),AuditSlotFromLoginOptions(TEXT("?Name=Fixture?ProjectJAuditSlot=0?SplitscreenCount=1")),0);
 TestEqual(TEXT("Slot1 is explicit regardless of arrival order"),AuditSlotFromLoginOptions(TEXT("?ProjectJAuditSlot=1")),1);
 for(const FString Invalid:{TEXT(""),TEXT("?Name=Fixture"),TEXT("?ProjectJAuditSlot"),TEXT("?ProjectJAuditSlot="),TEXT("?ProjectJAuditSlot=00"),TEXT("?ProjectJAuditSlot=-1"),TEXT("?ProjectJAuditSlot=2"),TEXT("?ProjectJAuditSlot=0junk"),TEXT("?ProjectJAuditSlot=0?ProjectJAuditSlot=1")})
  TestEqual(*FString::Printf(TEXT("Reject malformed or ambiguous identity: %s"),*Invalid),AuditSlotFromLoginOptions(Invalid),INDEX_NONE);
 return !HasAnyErrors();
}
#endif


namespace
{
const TCHAR* AuditSessionHeader=TEXT("phase,slot,mode,frame,state_id,pawn_id,asc_object_id,account,character,lease_epoch,actor_info_revision,session_current,pawn_owned,avatar_matches,grant,sprint_active,prediction_key,rpc_sent,rpc_calls,rpc_allowed,rpc_denied,stale_complete_rejected,stale_revoke_rejected,app_attempted,app_applied,passed\n");
FString SessionGuid155(const FGuid& Value){return Value.IsValid()?Value.ToString(EGuidFormats::DigitsWithHyphensLower):FString();}
bool SessionActive155(int32 InPhase,int32 InSlot){return (InPhase==1&&InSlot==0)||((InPhase==2||InPhase==3)&&InSlot==1);}
bool SessionSprint155(UProject_JAbilitySystemComponent* InASC,FGameplayAbilitySpecHandle InGrant,bool bExpectedActive,AActor* InAvatar)
{
 if(!IsValid(InASC)||InASC->IsBeingDestroyed())return false;
 const auto* ObservedSpec=InASC->FindAbilitySpecFromHandle(InGrant);
 if(!ObservedSpec||!ObservedSpec->Ability||ObservedSpec->Ability->GetClass()!=UProject_JGameplayAbility_Sprint::StaticClass())return false;
 const bool ObservedTag=InASC->GetTagCount(FProject_JGameplayTags::Get().State_Movement_Sprinting)>0;
 if(!bExpectedActive)return !ObservedSpec->IsActive()&&!ObservedTag;
 const auto* ObservedInstance=ObservedSpec->GetPrimaryInstance();
 return ObservedSpec->IsActive()&&ObservedTag&&IsValid(ObservedInstance)&&ObservedInstance->GetCurrentAbilitySpecHandle()==InGrant
  &&ObservedInstance->GetAvatarActorFromActorInfo()==InAvatar;
}
}

void AProject_JAuditNetworkController::ClientSessionStep_Implementation(int32 InStep,AProject_JPlayerState* InState,
 AProject_JAuditNetworkPlayer* InPawn,FGameplayAbilitySpecHandle InGrant,FGuid InAccount,FGuid InCharacter)
{
 if(!AuditSessionEnabled()||HasAuthority()||InStep<0||InStep>4||InStep==SessionStep)return;
 if(InStep!=SessionStep+1){ServerObserve(60,InStep,false,TEXT("Nonconsecutive trusted session control phase"));return;}
 FParse::Value(FCommandLine::Get(),TEXT("ProjectJAuditSlot="),Slot);
 Case=60;SessionStep=InStep;SessionState=InState;SessionPawn=InPawn;SessionGrant=InGrant;
 SessionAccount=InAccount;SessionCharacter=InCharacter;bSessionSent=false;bSessionRecorded=false;SessionLastDetail.Reset();
 if(SessionRows.IsEmpty())SessionRows=AuditSessionHeader;
 if(InStep==0&&!bSessionPacketsLogged)
 {
#if DO_ENABLE_NET_TEST
  if(const auto* ObservedDriver=GetWorld()->GetNetDriver())
  {
   const auto& Settings=ObservedDriver->PacketSimulationSettings;
   UE_LOG(LogProjectJAuditNetwork,Display,TEXT("SESSION_PACKETS slot=%d lag=%d variance=%d loss=%d order=%d duplicate=%d netmode=%d"),Slot,
    Settings.PktLag,Settings.PktLagVariance,Settings.PktLoss,Settings.PktOrder,Settings.PktDup,int32(GetNetMode()));
   bSessionPacketsLogged=true;
  }
#endif
 }
}

void AProject_JAuditNetworkController::ObserveSessionAudit()
{
 if(bSessionRecorded||SessionStep==INDEX_NONE)return;
 const auto Wait=[&](const FString& Detail)
 {
  if(Detail!=SessionLastDetail){SessionLastDetail=Detail;UE_LOG(LogProjectJAuditNetwork,Display,TEXT("SESSION_WAIT slot=%d phase=%d %s"),Slot,SessionStep,*Detail);}
 };
 auto* ObservedState=SessionState.Get();auto* ObservedASC=IsValid(ObservedState)?ObservedState->GetProjectJAbilitySystemComponent():nullptr;
 auto* ExpectedPawn=SessionPawn.Get();APawn* CurrentPawn=GetPawn();const bool ExpectedActive=SessionActive155(SessionStep,Slot);
 if(Slot<0||Slot>1||GetNetMode()!=NM_Client||!bSessionPacketsLogged||!IsValid(ObservedState)||ObservedState!=GetPlayerState<AProject_JPlayerState>()
  ||ObservedState->GetOwner()!=this||!IsValid(ObservedASC)||!ObservedASC->IsRegistered()||ObservedASC->IsBeingDestroyed()
  ||ObservedASC->GetOwner()!=ObservedState||ObservedState->GetAccountId().Value!=SessionAccount||ObservedState->GetCharacterId().Value!=SessionCharacter)
 {Wait(FString::Printf(TEXT("owner/state identity state=%s local=%s ASC=%s packets=%d"),*GetNameSafe(ObservedState),*GetNameSafe(GetPlayerState<AProject_JPlayerState>()),*GetNameSafe(ObservedASC),int32(bSessionPacketsLogged)));return;}
 if(ExpectedActive)
 {
  if(!IsValid(ExpectedPawn)||CurrentPawn!=ExpectedPawn||ExpectedPawn->GetController()!=this||ExpectedPawn->GetPlayerState()!=ObservedState
   ||ExpectedPawn->GetLocalRole()!=ROLE_AutonomousProxy||ObservedASC->GetAvatarActor()!=ExpectedPawn||ObservedASC->IsActorInfoChanging())
  {Wait(TEXT("Accepted native owning Pawn/ASC tuple not yet coherent"));return;}
 }
 else
 {
  APawn* StatePawn=ObservedState->GetPawn();
  // PawnPrivate is a native nonreplicated cache, not possession authority.
  // Its retained old replica is harmless only while all actual ownership and
  // gameplay-consumer links are detached. Never accept a different cached Pawn.
  const bool Detached=!IsValid(ExpectedPawn)||(ExpectedPawn->GetController()==nullptr&&ExpectedPawn->GetPlayerState()==nullptr
   &&ExpectedPawn->GetAbilitySystemComponent()!=ObservedASC);
  const bool CacheDetached=!StatePawn||(StatePawn==ExpectedPawn&&Detached);
  const bool RecipientRetired=!SessionGrant.IsValid()||(!ObservedASC->GetOwnerActor()&&!ObservedASC->GetAvatarActor()&&!ObservedASC->IsActorInfoChanging());
  if(CurrentPawn||!Detached||!CacheDetached||!RecipientRetired)
  {
  Wait(FString::Printf(TEXT("Native unpossession/held Pawn not yet observed pc_pawn=%s:%llu role=%d controller=%s ps=%s state_pawn=%s:%llu role=%d controller=%s ps=%s expected=%s:%llu role=%d controller=%s ps=%s ASC_owner=%s ASC_avatar=%s epoch=%llu changing=%d"),
   *GetNameSafe(CurrentPawn),AuditMountPublicId(CurrentPawn),IsValid(CurrentPawn)?int32(CurrentPawn->GetLocalRole()):-1,*GetNameSafe(IsValid(CurrentPawn)?CurrentPawn->GetController():nullptr),*GetNameSafe(IsValid(CurrentPawn)?CurrentPawn->GetPlayerState():nullptr),
   *GetNameSafe(StatePawn),AuditMountPublicId(StatePawn),IsValid(StatePawn)?int32(StatePawn->GetLocalRole()):-1,*GetNameSafe(IsValid(StatePawn)?StatePawn->GetController():nullptr),*GetNameSafe(IsValid(StatePawn)?StatePawn->GetPlayerState():nullptr),
   *GetNameSafe(ExpectedPawn),AuditMountPublicId(ExpectedPawn),IsValid(ExpectedPawn)?int32(ExpectedPawn->GetLocalRole()):-1,*GetNameSafe(IsValid(ExpectedPawn)?ExpectedPawn->GetController():nullptr),*GetNameSafe(IsValid(ExpectedPawn)?ExpectedPawn->GetPlayerState():nullptr),
   *GetNameSafe(ObservedASC->GetOwnerActor()),*GetNameSafe(ObservedASC->GetAvatarActor()),ObservedASC->GetActorInfoRevision(),int32(ObservedASC->IsActorInfoChanging())));return;
  }
 }
 if(SessionGrant.IsValid())
 {
  const auto* ObservedSpec=ObservedASC->FindAbilitySpecFromHandle(SessionGrant);
  if(!ObservedSpec||!ObservedSpec->Ability||ObservedSpec->Ability->GetClass()!=UProject_JGameplayAbility_Sprint::StaticClass())
  {Wait(TEXT("Exact retained production Sprint grant has not replicated"));return;}
  if(ExpectedActive&&!bSessionSent&&(SessionStep==1||SessionStep==2))
  {
   bSessionSent=true;
   if(!ObservedASC->TryActivateAbility(SessionGrant)){bSessionRecorded=true;ServerObserve(60,SessionStep,false,TEXT("Native client Sprint activation rejected"));return;}
  }
  if(!SessionSprint155(ObservedASC,SessionGrant,ExpectedActive,ExpectedPawn)){Wait(TEXT("Production Sprint execution/tag has not reached expected state"));return;}
  if(ExpectedActive)
  {
   ObservedSpec=ObservedASC->FindAbilitySpecFromHandle(SessionGrant);
   const auto* ObservedInstance=ObservedSpec?ObservedSpec->GetPrimaryInstance():nullptr;
   if(!ObservedInstance){Wait(TEXT("Actual Sprint instance unavailable"));return;}
   SessionActivationKey=ObservedInstance->GetCurrentActivationInfo().GetActivationPredictionKey();SessionPredictionKey=SessionActivationKey.Current;
   if(SessionPredictionKey<=0){Wait(TEXT("Native local activation has no positive prediction key"));return;}
  }
  if(SessionStep==3&&Slot==0&&!bSessionSent)
  {
   // Retained PS/ASC actor channel still belongs to this old connection. Local
   // TryActivate could stop before sending; use the native RPC dispatch helper
   // with the original real activation key and the still-existing exact grant.
   if(SessionPredictionKey<=0){bSessionRecorded=true;ServerObserve(60,SessionStep,false,TEXT("Original prediction key lost before negative request"));return;}
   bSessionSent=true;ObservedASC->CallServerTryActivateAbility(SessionGrant,true,SessionActivationKey);
  }
 }
 else if(ExpectedActive){Wait(TEXT("Active phase has no owned grant"));return;}
 TArray<FString> Cells;const auto Number=[&](auto Value){Cells.Add(LexToString(Value));};
 Number(SessionStep);Number(Slot);Number(int32(GetNetMode()));Number(GFrameCounter);Number(AuditMountPublicId(ObservedState));Number(AuditMountPublicId(CurrentPawn));
 Number(ObservedASC->GetUniqueID());Cells.Add(SessionGuid155(ObservedState->GetAccountId().Value));Cells.Add(SessionGuid155(ObservedState->GetCharacterId().Value));Cells.Add(FString());
 Number(ObservedASC->GetActorInfoRevision());Number(0);Number(int32(CurrentPawn&&CurrentPawn==ExpectedPawn));Number(int32(CurrentPawn&&ObservedASC->GetAvatarActor()==CurrentPawn));
 Cells.Add(SessionGrant.IsValid()?SessionGrant.ToString():TEXT("0"));Number(int32(ExpectedActive));Number(SessionPredictionKey);Number(int32(bSessionSent));
 for(int32 Index=0;Index<7;++Index)Number(0);Number(1);check(Cells.Num()==26);
 SessionRows+=FString::Join(Cells,TEXT(","))+TEXT("\n");bSessionRecorded=true;
 ServerObserve(60,SessionStep,true,TEXT("Native session owner/held/retired replica and exact Sprint grant observed"));
}

void AProject_JAuditNetworkGameMode::RecordSessionPermission(int32 ExplicitSlot,UProject_JAbilitySystemComponent* ObservedASC,AProject_JPlayerState* ObservedState,bool Allowed)
{
 if(bSessionCleaning||bFinished||ExplicitSlot<0||ExplicitSlot>1)return;
 auto& SessionOwner=SessionOwners[ExplicitSlot];
 if(SessionOwner.ASC.Get()!=ObservedASC||SessionOwner.State.Get()!=ObservedState||!IsValid(ObservedASC)||ObservedASC->ClientSessionAuthority.GetHandle()!=SessionOwner.PermissionHandle)return;
 ++SessionOwner.Calls;if(Allowed)++SessionOwner.Allowed;else ++SessionOwner.Denied;
 UE_LOG(LogProjectJAuditNetwork,Display,TEXT("SESSION_GATE slot=%d phase=%d allowed=%d ASC=%s calls=%d denied=%d"),ExplicitSlot,Step,int32(Allowed),*GetNameSafe(ObservedASC),SessionOwner.Calls,SessionOwner.Denied);
}

bool AProject_JAuditNetworkGameMode::PrepareSessionOwner(int32 ExplicitSlot,FString& Failure)
{
 auto& SessionOwner=SessionOwners[ExplicitSlot];const TStrongObjectPtr<AProject_JAuditNetworkController> KeepController(SessionOwner.Controller.Get());
 const TStrongObjectPtr<AProject_JPlayerState> KeepState(SessionOwner.State.Get());
 ProjectJ::MMO::FServerSessionAttempt Attempt;
 if(!IsValid(KeepController.Get())||!IsValid(KeepState.Get())||KeepController->GetPawn()||KeepState->GetPawn()
  ||!BeginTrustedSession(KeepController.Get(),SessionAccount,SessionCharacter,Attempt)||!CompleteTrustedSession(Attempt,true,SessionOwner.Lease))
 {Failure=TEXT("Native trusted diagnostic acceptance failed before Pawn assembly");return false;}
 FActorSpawnParameters Params;Params.Owner=KeepController.Get();Params.ObjectFlags|=RF_Transient;Params.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 const TStrongObjectPtr<AProject_JAuditNetworkPlayer> KeepPawn(GetWorld()->SpawnActor<AProject_JAuditNetworkPlayer>(AProject_JAuditNetworkPlayer::StaticClass(),FVector(0,ExplicitSlot*2000,100),FRotator::ZeroRotator,Params));
 SessionOwner.Pawn=KeepPawn.Get(); // Capture before any lifecycle callback or later failure.
 if(!IsValid(KeepPawn.Get())||!KeepState->IsServerSessionCurrent(SessionOwner.Lease)){Failure=TEXT("Accepted owner changed during native Pawn assembly");return false;}
 KeepController->Possess(KeepPawn.Get());
 auto* ObservedASC=KeepState->GetProjectJAbilitySystemComponent();SessionOwner.ASC=ObservedASC;
 if(!IsValid(ObservedASC)||ObservedASC->IsActorInfoChanging()||ObservedASC->GetOwnerActor()!=KeepState.Get()||ObservedASC->GetAvatarActor()!=KeepPawn.Get()
  ||KeepPawn->GetController()!=KeepController.Get()||KeepState->GetPawn()!=KeepPawn.Get()||!KeepState->IsServerSessionCurrent(SessionOwner.Lease))
 {Failure=TEXT("Actual possessed player did not consume its accepted PS/ASC");return false;}
 SessionOwner.AvatarRevision=ObservedASC->GetActorInfoRevision();
 SessionOwner.Grant=ObservedASC->GiveAbility(FGameplayAbilitySpec(UProject_JGameplayAbility_Sprint::StaticClass(),1,INDEX_NONE,KeepState.Get()));
 SessionOwner.SavedPermission=ObservedASC->ClientSessionAuthority;
 if(!SessionOwner.Grant.IsValid()||!SessionOwner.SavedPermission.IsBound()){Failure=TEXT("Owned native Sprint grant or production permission delegate missing");return false;}
 const auto Saved=SessionOwner.SavedPermission;const TWeakObjectPtr<AProject_JAuditNetworkGameMode> WeakThis(this);
 const TWeakObjectPtr<UProject_JAbilitySystemComponent> WeakASC(ObservedASC);const TWeakObjectPtr<AProject_JPlayerState> WeakState(KeepState.Get());
 ObservedASC->ClientSessionAuthority.BindWeakLambda(this,[WeakThis,WeakASC,WeakState,Saved,ExplicitSlot]()
 {
  const bool Allowed=Saved.IsBound()&&Saved.Execute(); // Preserve the production answer, no fixture authority policy.
  if(auto* Mode=WeakThis.Get())Mode->RecordSessionPermission(ExplicitSlot,WeakASC.Get(),WeakState.Get(),Allowed);
  return Allowed;
 });
 SessionOwner.PermissionHandle=ObservedASC->ClientSessionAuthority.GetHandle();return true;
}

void AProject_JAuditNetworkGameMode::SendSessionStep(int32 InStep)
{
 Step=InStep;StepStarted=FPlatformTime::Seconds();
 for(int32 ExplicitSlot=0;ExplicitSlot<2;++ExplicitSlot)
 {
  auto& SessionOwner=SessionOwners[ExplicitSlot];auto* PC=SessionOwner.Controller.Get();
  if(!IsValid(PC)||PC->IsActorBeingDestroyed()||!PC->GetNetConnection()){Finish(false,TEXT("Session control recipient disconnected"));return;}
  PC->ConfirmedCase=PC->ConfirmedStep=INDEX_NONE;
  PC->ClientSessionStep(Step,SessionOwner.State.Get(),SessionOwner.Pawn.Get(),SessionOwner.Grant,
   SessionOwner.Lease.IsValid()?SessionOwner.Lease.AccountId:FGuid(),SessionOwner.Lease.IsValid()?SessionOwner.Lease.CharacterId:FGuid());
 }
}

bool AProject_JAuditNetworkGameMode::RecordSessionPhase(FString& Failure)
{
 for(int32 ExplicitSlot=0;ExplicitSlot<2;++ExplicitSlot)
 {
  auto& SessionOwner=SessionOwners[ExplicitSlot];auto* PC=SessionOwner.Controller.Get();auto* ObservedState=SessionOwner.State.Get();auto* ExpectedPawn=SessionOwner.Pawn.Get();
  auto* ObservedASC=SessionOwner.ASC.Get();const bool ExpectedActive=SessionActive155(Step,ExplicitSlot);
  if(!IsValid(PC)||!PC->GetNetConnection()||!IsValid(ObservedState)||PC->GetPlayerState<AProject_JPlayerState>()!=ObservedState||ObservedState->GetOwner()!=PC
   ||!IsValid(ObservedASC)||ObservedState->GetProjectJAbilitySystemComponent()!=ObservedASC||ObservedASC->GetOwner()!=ObservedState||!ObservedASC->IsRegistered()||ObservedASC->IsBeingDestroyed())
  {Failure=TEXT("Exact session authority recipients lost identity/lifetime");return false;}
  const bool Current=ObservedState->IsServerSessionCurrent(SessionOwner.Lease);
  if(Current!=ExpectedActive||ObservedState->CanAcceptSessionCommand()!=ExpectedActive)
  {Failure=TEXT("Native single-writer permission differs from expected session phase");return false;}
  if(ExpectedActive)
  {
   if(PC->GetPawn()!=ExpectedPawn||!IsValid(ExpectedPawn)||ExpectedPawn->GetController()!=PC||ExpectedPawn->GetPlayerState()!=ObservedState||ObservedState->GetPawn()!=ExpectedPawn
    ||ObservedASC->GetOwnerActor()!=ObservedState||ObservedASC->GetAvatarActor()!=ExpectedPawn||ObservedASC->IsActorInfoChanging()
    ||!SessionSprint155(ObservedASC,SessionOwner.Grant,true,ExpectedPawn)||SessionOwner.Allowed<1)
   {Failure=TEXT("Actual native client activation/owner/Avatar/Sprint positive control missing");return false;}
   SessionOwner.PredictionKey=ObservedASC->FindAbilitySpecFromHandle(SessionOwner.Grant)->GetPrimaryInstance()->GetCurrentActivationInfo().GetActivationPredictionKey().Current;
   if(SessionOwner.PredictionKey<=0){Failure=TEXT("Accepted native client RPC lacks actual prediction key");return false;}
  }
  else
  {
   if(PC->GetPawn()||ObservedState->GetPawn()||(ExpectedPawn&&ExpectedPawn->GetController())){Failure=TEXT("Native old/held Pawn still controlled");return false;}
   if(SessionOwner.Grant.IsValid()&&(!SessionSprint155(ObservedASC,SessionOwner.Grant,false,nullptr)||ObservedASC->GetOwnerActor()||ObservedASC->GetAvatarActor()
    ||ObservedASC->GetActorInfoRevision()<=SessionOwner.AvatarRevision))
   {Failure=TEXT("Captured old Sprint execution/tag/ASC tuple did not retire normally");return false;}
  }
  if(SessionOwner.Lease.IsValid()?(ObservedState->GetAccountId().Value!=SessionAccount.Value||ObservedState->GetCharacterId().Value!=SessionCharacter.Value)
   :(ObservedState->GetAccountId().IsValid()||ObservedState->GetCharacterId().IsValid()))
  {Failure=TEXT("Server-owned diagnostic identity changed or was published before acceptance");return false;}
  TArray<FString> Cells;const auto Number=[&](auto Value){Cells.Add(LexToString(Value));};
  Number(Step);Number(ExplicitSlot);Number(int32(GetNetMode()));Number(GFrameCounter);Number(AuditMountPublicId(ObservedState));Number(AuditMountPublicId(PC->GetPawn()));Number(ObservedASC->GetUniqueID());
  Cells.Add(SessionGuid155(ObservedState->GetAccountId().Value));Cells.Add(SessionGuid155(ObservedState->GetCharacterId().Value));Cells.Add(SessionGuid155(SessionOwner.Lease.Epoch));
  Number(ObservedASC->GetActorInfoRevision());Number(int32(Current));Number(int32(PC->GetPawn()!=nullptr));Number(int32(PC->GetPawn()&&ObservedASC->GetAvatarActor()==PC->GetPawn()));
  Cells.Add(SessionOwner.Grant.IsValid()?SessionOwner.Grant.ToString():TEXT("0"));Number(int32(ExpectedActive));Number(SessionOwner.PredictionKey);Number(0);
  Number(SessionOwner.Calls);Number(SessionOwner.Allowed);Number(SessionOwner.Denied);Number(int32(bSessionStaleComplete));Number(int32(bSessionStaleRevoke));Number(int32(bSessionAppAttempted));Number(int32(bSessionAppApplied));Number(1);check(Cells.Num()==26);
  SessionRows+=FString::Join(Cells,TEXT(","))+TEXT("\n");
 }
 return true;
}

void AProject_JAuditNetworkGameMode::UpdateSessionAudit()
{
 const double Now=FPlatformTime::Seconds();FString Failure;
 if(Case==INDEX_NONE)
 {
  if(Clients.Num()!=2)return;
  for(int32 ExplicitSlot=0;ExplicitSlot<2;++ExplicitSlot)
  {
   const auto* Found=Clients.FindByPredicate([ExplicitSlot](const auto& PC){return PC&&PC->Slot==ExplicitSlot;});
   auto* PC=Found?Found->Get():nullptr;auto* ObservedState=PC?PC->GetPlayerState<AProject_JPlayerState>():nullptr;
   if(!IsValid(PC)||!IsValid(ObservedState)||PC->GetPawn()||ObservedState->GetPawn()||ObservedState->GetAccountId().IsValid()||ObservedState->GetCharacterId().IsValid()||ObservedState->CanAcceptSessionCommand())
   {Finish(false,TEXT("Actual trusted PostLogin did not hold fresh identity/Pawn/permission"));return;}
   SessionOwners[ExplicitSlot].Controller=PC;SessionOwners[ExplicitSlot].State=ObservedState;SessionOwners[ExplicitSlot].ASC=ObservedState->GetProjectJAbilitySystemComponent();
  }
  SessionAccount=FProject_JAccountId::NewId();SessionCharacter=FProject_JCharacterId::NewId();SessionRows=AuditSessionHeader;
  Case=60;CaseStarted=Now;SendSessionStep(0);return;
 }
 if(Now-StepStarted>20){Finish(false,FString::Printf(TEXT("Trusted session phase%d timed out; no permission/identity workaround"),Step));return;}
 if(Step==3&&bSessionAppQueued&&!bSessionAppAttempted&&GFrameCounter>SessionAppQueuedFrame)
 {
  bSessionAppAttempted=true;const TStrongObjectPtr<AProject_JPlayerState> OldState(SessionAppRecipient.Get());
  // One captured completion on a later ordinary Tick. The actual public
  // mutation is behind the captured lease fence, not an injected result.
  if(IsValid(OldState.Get())&&OldState->IsServerSessionCurrent(SessionAppLease))
  {OldState->SetPublicCharacterSnapshot(SessionAppClass,SessionAppRequestedLevel);bSessionAppApplied=true;}
 }
 if(!AllConfirmed(Step))return;
 if(Step==1||Step==2)
 {
  const int32 ActiveSlot=Step==1?0:1;const auto& ActiveOwner=SessionOwners[ActiveSlot];
  // Controller control replies and ASC gameplay RPCs use different channels.
  // Wait for the actual positive server activation, never infer it from ACK.
  if(ActiveOwner.Allowed<1||!SessionSprint155(ActiveOwner.ASC.Get(),ActiveOwner.Grant,true,ActiveOwner.Pawn.Get()))return;
 }
 if(Step==3)
 {
  if(SessionOwners[0].Denied<1)return; // A client send/ACK is not a native server arrival witness.
  if(!bSessionAppAttempted)return;
  const auto* OldState=SessionOwners[0].State.Get();
  if(!IsValid(OldState)||OldState->IsActorBeingDestroyed()){Finish(false,TEXT("Original captured application recipient retired before observation"));return;}
  ProjectJ::MMO::FServerSessionLease Late;
  bSessionStaleComplete=!CompleteTrustedSession(SessionSlowAttempt,true,Late);
  bSessionStaleRevoke=!RevokeTrustedSession(SessionOwners[0].State.Get(),SessionOwners[0].Lease);
  if(!bSessionStaleComplete||!bSessionStaleRevoke||bSessionAppApplied||OldState->GetPublicCharacterLevel()!=SessionAppBeforeLevel)
  {Finish(false,TEXT("Old completion/revoke/captured application changed state after takeover"));return;}
 }
 if(!RecordSessionPhase(Failure)){Finish(false,Failure);return;}
 if(Step==0)
 {
  if(!PrepareSessionOwner(0,Failure)){Finish(false,Failure);return;}SendSessionStep(1);return;
 }
 if(Step==1)
 {
  SessionAppRecipient=SessionOwners[0].State;SessionAppLease=SessionOwners[0].State->CaptureServerSession();
  SessionAppClass=SessionOwners[0].State->GetPublicClassId();SessionAppBeforeLevel=SessionOwners[0].State->GetPublicCharacterLevel();SessionAppRequestedLevel=SessionAppBeforeLevel+1;
  if(!BeginTrustedSession(SessionOwners[0].Controller.Get(),SessionAccount,SessionCharacter,SessionSlowAttempt)||!PrepareSessionOwner(1,Failure))
  {Finish(false,Failure.IsEmpty()?TEXT("Slow server-issued refresh attempt could not begin"):Failure);return;}
  SendSessionStep(2);return;
 }
 if(Step==2){bSessionAppQueued=true;SessionAppQueuedFrame=GFrameCounter;SendSessionStep(3);return;}
 if(Step==3)
 {
  if(!RevokeTrustedSession(SessionOwners[1].State.Get(),SessionOwners[1].Lease)){Finish(false,TEXT("Exact replacement revoke failed"));return;}
  SendSessionStep(4);return;
 }
 Finish(true,TEXT("In-process trusted identities: native PostLogin hold, two socket activation controls, exact takeover/revocation, actual old-ASC RPC denial and captured application fence; no external authentication or network persistence claim"));
}

bool AProject_JAuditNetworkGameMode::CleanupSessionAudit()
{
 if(bSessionCleaned)return bSessionCleanupPassed;bSessionCleaned=true;bSessionCleaning=true;bool Passed=true;
 CancelTrustedSession(SessionSlowAttempt); // Exact pending-only ticket; already-superseded is an expected no-op.
 for(const auto& SessionOwner:SessionOwners)
 {
  const TStrongObjectPtr<AProject_JPlayerState> KeepState(SessionOwner.State.Get());const TStrongObjectPtr<UProject_JAbilitySystemComponent> KeepASC(SessionOwner.ASC.Get());
  const TStrongObjectPtr<AProject_JAuditNetworkPlayer> KeepPawn(SessionOwner.Pawn.Get());
  if(SessionOwner.Lease.IsValid()&&IsValid(KeepState.Get())&&IsTrustedSessionCurrent(KeepState.Get(),SessionOwner.Lease))RevokeTrustedSession(KeepState.Get(),SessionOwner.Lease);
  if(IsValid(KeepASC.Get())&&!KeepASC->IsBeingDestroyed())
  {
   if(SessionOwner.PermissionHandle.IsValid())
   {
    if(KeepASC->ClientSessionAuthority.GetHandle()==SessionOwner.PermissionHandle)KeepASC->ClientSessionAuthority=SessionOwner.SavedPermission;
    else Passed=false; // A newer owner binding is never overwritten by cleanup.
   }
   if(SessionOwner.Grant.IsValid())
   {
    auto* OwnedSpec=KeepASC->FindAbilitySpecFromHandle(SessionOwner.Grant);
    if(OwnedSpec&&OwnedSpec->Ability&&OwnedSpec->Ability->GetClass()==UProject_JGameplayAbility_Sprint::StaticClass()&&OwnedSpec->SourceObject.Get()==KeepState.Get())
    {
     KeepASC->CancelAbilityHandle(SessionOwner.Grant);
     const auto* CurrentSpec=IsValid(KeepASC.Get())&&!KeepASC->IsBeingDestroyed()?KeepASC->FindAbilitySpecFromHandle(SessionOwner.Grant):nullptr;
     if(CurrentSpec&&CurrentSpec->Ability&&CurrentSpec->Ability->GetClass()==UProject_JGameplayAbility_Sprint::StaticClass()&&CurrentSpec->SourceObject.Get()==KeepState.Get())KeepASC->ClearAbility(SessionOwner.Grant);
    }
    if(KeepASC->FindAbilitySpecFromHandle(SessionOwner.Grant))Passed=false;
   }
  }
  if(IsValid(KeepPawn.Get())&&!KeepPawn->IsActorBeingDestroyed())
  {
   if(KeepPawn->GetController())Passed=false;
   else Passed=KeepPawn->Destroy()&&Passed;
  }
 }
 bSessionCleanupPassed=Passed;return Passed;
}

#include "Project_JAuditNetworkFixture.NPCAssembly.inl"
#include "Project_JAuditNetworkFixture.QuestHistory.inl"
