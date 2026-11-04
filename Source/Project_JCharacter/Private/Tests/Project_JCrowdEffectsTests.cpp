#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "NativeGameplayTags.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Project_JNPCCharacter.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "NiagaraSystem.h"
#include "NiagaraEffectType.h"
#include "Engine/LocalPlayer.h"
#include "Engine/GameViewportClient.h"
#include "Engine/EngineBaseTypes.h"
#include "SceneView.h"
#include "Movement/Project_JMovementQueries.h"
#include "NiagaraWorldManager.h"
#include "UObject/Package.h"
#include "Misc/Paths.h"
#include "NiagaraComponent.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraActor.h"
#include "UObject/UObjectIterator.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"
#include "UObject/ObjectKey.h"
#include "ProfilingDebugging/MiscTrace.h"

UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdAttackTag, "ProjectJ.Tests.Crowd.Attack");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueTag, "ProjectJ.Tests.Crowd.Cue");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueExtra1, "ProjectJ.Tests.Crowd.Extra1");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueExtra2, "ProjectJ.Tests.Crowd.Extra2");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueExtra3, "ProjectJ.Tests.Crowd.Extra3");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueExtra4, "ProjectJ.Tests.Crowd.Extra4");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueExtra5, "ProjectJ.Tests.Crowd.Extra5");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueExtra6, "ProjectJ.Tests.Crowd.Extra6");
UE_DEFINE_GAMEPLAY_TAG_STATIC(CrowdCueExtra7, "ProjectJ.Tests.Crowd.Extra7");

namespace
{
constexpr const TCHAR* OwnedTrailSystemPath=TEXT("/Game/Effects/ProjectJ/Combat/NS_CombatTrail.NS_CombatTrail");
constexpr const TCHAR* OwnedTrailTypePath=TEXT("/Game/Effects/ProjectJ/Combat/NET_CombatTrail.NET_CombatTrail");
constexpr const TCHAR* TrailProfilePath=TEXT("/Game/DataAssetSets/Presentation/Spellblade/DA_AP_Spellblade/DA_AP_Spellblade.DA_AP_Spellblade");
bool HasOwnedTrailPolicy(const UNiagaraSystem* System,const UNiagaraEffectType* Type,const UNiagaraSystem* Vendor)
{
	if (!IsValid(System) || !IsValid(Type) || !IsValid(Vendor) || System->GetEffectType()!=Type
		|| System->GetPathName()!=OwnedTrailSystemPath || Type->GetPathName()!=OwnedTrailTypePath) { return false; }
	const auto& Effective=System->GetScalabilitySettings(); const auto& Native=Type->GetActiveSystemScalabilitySettings();
	const auto* Handler=Type->GetSignificanceHandler(); const FBox Bounds=System->GetFixedBounds();
	return Effective.bCullMaxInstanceCount && Effective.MaxInstances>0 && Effective.MaxInstances<=4096
		&& Native.bCullMaxInstanceCount && Native.MaxInstances==Effective.MaxInstances
		&& !Effective.bCullPerSystemMaxInstanceCount && !Native.bCullPerSystemMaxInstanceCount
		&& !Effective.BudgetScaling.bScaleMaxInstanceCountByGlobalBudgetUse && !Native.BudgetScaling.bScaleMaxInstanceCountByGlobalBudgetUse
		&& Type->UpdateFrequency==ENiagaraScalabilityUpdateFrequency::High && Type->CullReaction==ENiagaraCullReaction::DeactivateImmediateResume
		&& Handler && Handler->IsA<UNiagaraSignificanceHandlerDistance>() && !Type->bAllowCullingForLocalPlayers && !System->AllowCullingForLocalPlayers()
		&& System->NeedsSortedSignificanceCull() && System->IsAllowedByScalability()
		&& Effective.bCullByDistance && FMath::IsNearlyEqual(Effective.MaxDistance,2500.f)
		&& System->bFixedBounds==Vendor->bFixedBounds && Bounds==Vendor->GetFixedBounds()
		&& (!System->bFixedBounds || (Bounds.IsValid && !Bounds.Min.ContainsNaN() && !Bounds.Max.ContainsNaN()));
}
class FSoftCrowdCueCommand : public IAutomationLatentCommand
{
public:
	explicit FSoftCrowdCueCommand(TFunction<bool()> InBody) : Body(MoveTemp(InBody)) {}
	bool Update() override { return Body(); }
private:
	TFunction<bool()> Body;
};
struct FSoftCrowdCueWorld
{
	UWorld* World;
	UProject_JVisualAssetSubsystem* Assets;
	UProject_JPresentationBudgetSubsystem* Budget;
	TArray<UProject_JCombatPresentationComponent*> Presentations;
	TArray<TStrongObjectPtr<UProject_JWeaponPresentationComponent>> Capacity;
	TStrongObjectPtr<UProject_JWeaponPresentationComponent> InterleavedWeapon;
	IConsoleVariable* Pool;
	int32 PreviousPool;
	bool bColdSetup = false, bReplacedQueuedOwner = false;
	int32 ApplyTicks = 0;
	uint64 LastApplyFrame = MAX_uint64, CapacityQueuedFrame = 0;
	explicit FSoftCrowdCueWorld(const FSoftObjectPath& Path)
	{
		Pool = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.Pool"));
		PreviousPool = Pool->GetInt(); Pool->Set(0, ECVF_SetByCode);
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		Assets = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
		Budget = World->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay();
		for (int32 Index = 0; Index < 5; ++Index)
		{
			FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(Index * 300, 0, 100), FRotator::ZeroRotator, Spawn);
			checkf(NPC, TEXT("Concrete cold-RHI NPC fixture must spawn before component access."));
			auto* C = NewObject<UProject_JCombatPresentationComponent>(NPC);
			NPC->AddInstanceComponent(C); C->RegisterComponent();
			auto* Set = NewObject<UProject_JCombatPresentationSet>(C);
			auto* Profile = NewObject<UProject_JAttackPresentationProfile>(Set);
			auto& Cue = Profile->Cues.AddDefaulted_GetRef(); Cue.CueTag = CrowdCueTag;
			Cue.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(Path); Cue.bLooping = true;
			Cue.bDestroyImmediatelyOnStop = true; Cue.AttachmentTarget = EProject_JCombatVFXAttachmentTarget::World;
			auto& Entry = Set->AttackPresentations.AddDefaulted_GetRef(); Entry.AttackTag = CrowdAttackTag; Entry.Profile = Profile;
			C->BasePresentationSet = Set; Presentations.Add(C);
		}
	}
	~FSoftCrowdCueWorld()
	{
		for (const auto& Holder : Capacity) { Budget->Cancel(Holder.Get()); }
		World->EndPlay(EEndPlayReason::Quit); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		Pool->Set(PreviousPool, ECVF_SetByCode);
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCrowdEffectsTest, "ProjectJ.GroupE.EffectsPoolLifetime",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)
bool FProjectJCrowdEffectsTest::RunTest(const FString&)
{
	const bool bSavedPressure=FParse::Param(FCommandLine::Get(),TEXT("ProjectJTestNiagaraSavedPressure"));
	if (bSavedPressure && FParse::Param(FCommandLine::Get(),TEXT("ProjectJTestActiveNiagaraScalability")))
	{ AddError(TEXT("Saved128 pressure and transient count2 modes are mutually exclusive")); return false; }
	// Separate active-policy fixture: no pool-timing/cold98 preconditions or saved asset edits.
	if (bSavedPressure || FParse::Param(FCommandLine::Get(),TEXT("ProjectJTestActiveNiagaraScalability")))
	{
#if WITH_EDITOR
#if !WITH_NIAGARA_DEBUGGER
		if (bSavedPressure) { AddError(TEXT("Saved128 pressure requires native read-only count/distance/visibility/budget cull flags")); return false; }
#endif
		FString SystemSelection(bSavedPressure ? TEXT("Project") : TEXT("Vendor")); FParse::Value(FCommandLine::Get(),TEXT("ProjectJActiveFXSystem="),SystemSelection);
		if (SystemSelection!=TEXT("Vendor") && SystemSelection!=TEXT("Project"))
		{ AddError(TEXT("Active policy system must be Vendor or Project")); return false; }
		FString FrequencyName(bSavedPressure ? TEXT("High") : TEXT("Continuous"));
		FParse::Value(FCommandLine::Get(),TEXT("ProjectJActiveFXFrequency="),FrequencyName);
		if (FrequencyName!=TEXT("Continuous") && FrequencyName!=TEXT("High"))
		{ AddError(TEXT("Active policy frequency must be Continuous or High")); return false; }
		if (bSavedPressure && (SystemSelection!=TEXT("Project") || FrequencyName!=TEXT("High")))
		{ AddError(TEXT("Saved128 pressure requires the actual Project source and its High cadence")); return false; }
		const FString ReceiptName=bSavedPressure ? TEXT("SavedNiagaraPressure.json") : TEXT("ActiveNiagaraScalability.json");
		const FString StatesName=bSavedPressure ? TEXT("SavedNiagaraPressureStates.csv") : TEXT("ActiveNiagaraStates.csv");
		FString Output;
		if (!FParse::Value(FCommandLine::Get(),TEXT("ProjectJCrowdOutput="),Output) || !IFileManager::Get().DirectoryExists(*Output)
			|| IFileManager::Get().FileExists(*(Output/ReceiptName))
			|| IFileManager::Get().FileExists(*(Output/StatesName)))
		{ AddError(TEXT("Active policy mode requires an existing fresh ProjectJCrowdOutput directory")); return false; }
		// Test-local observer only. Niagara's cached views exist inside world ticks,
		// and are deliberately cleared before the automation latent command runs.
		struct FActiveFXViewTick : FTickFunction
		{
			TFunction<void()> Observe;
			virtual void ExecuteTick(float, ELevelTick, ENamedThreads::Type, const FGraphEventRef&) override
			{ check(IsInGameThread()); if (Observe) { Observe(); } }
			virtual FString DiagnosticMessage() override { return TEXT("ProjectJ active FX native view observation"); }
		};
		struct FActiveFXCase
		{
			TWeakObjectPtr<UWorld> World;
			TWeakObjectPtr<APlayerController> Viewer;
			TWeakObjectPtr<AActor> PreviousView;
			TStrongObjectPtr<UNiagaraSystem> Source, System, VendorTemplate;
			TStrongObjectPtr<UProject_JAttackPresentationProfile> SourceProfile;
			FString SystemSelection;
			bool bVendorDirty = false, bProfileDirty = false;
			TStrongObjectPtr<UNiagaraEffectType> SourceType, Type;
			TStrongObjectPtr<UProject_JCombatPresentationSet> Set;
			TStrongObjectPtr<UProject_JAttackPresentationProfile> Profile;
			TStrongObjectPtr<ACameraActor> Camera;
			TArray<TStrongObjectPtr<AProject_JNPCCharacter>> Owners;
			TArray<TStrongObjectPtr<UProject_JCombatPresentationComponent>> Cues;
			TStrongObjectPtr<UNiagaraComponent> Components[3], RetiredStop, RetiredAttack;
			uint64 Leases[3]{}, OldStopLease = 0, LastFrame = MAX_uint64, LastObservedFrame = MAX_uint64, RequiredObservedFrame = 0;
			uint32 ViewRevision = 0;
			bool bSpawnedRecorded = false, bSavedPressure = false;
			// Only the saved128 mode populates these129 extra slots; the original three anchors stay unchanged.
			TArray<TStrongObjectPtr<UNiagaraComponent>> ExtraComponents;
			TArray<uint64> ExtraLeases;
			TArray<FVector> ExtraExpectedOwners;
			int32 PressureCurrent = 0, PressureLive = 0, PressureCulled = 0, PressureRegistered = 0, PressureReleased = 0;
			int32 PressureCountFlags = 0, PressureOtherFlags = 0, PressureDistinct = 0, PressurePeakDistinct = 0, PressureDestroyed = 0;
			FString PressureDetail;
			struct FObservedExtra
			{
				uint32 ComponentId = 0;
				double Distance = -1;
				FVector OwnerLocation = FVector::ZeroVector;
			};
			struct FObservedView
			{
				uint64 Frame = MAX_uint64;
				uint32 Revision = 0, ComponentIds[3]{};
				double WorldTime = -1, Distances[3]{-1,-1,-1};
				int32 ViewCount = 0;
				bool bProjectionValid = false, bCameraMatches = false;
				TWeakObjectPtr<AActor> Target;
				FVector CameraLocation = FVector::ZeroVector;
				FVector OwnerLocations[3]{FVector::ZeroVector,FVector::ZeroVector,FVector::ZeroVector};
				TArray<FObservedExtra> Extra;
			} Observation;
			FActiveFXViewTick ViewTick;
			uint32 ReplacedAttack = 0;
			int32 Stage = 0, Stable = 0;
			double Began = FPlatformTime::Seconds(), LastWorldTime = -1, StableSince = -1, NativeUpdatePeriod = 0;
			FString FrequencyName;
			ENiagaraScalabilityUpdateFrequency Frequency = ENiagaraScalabilityUpdateFrequency::Continuous;
			FVector Origin = FVector::ZeroVector, ExpectedOwners[3];
			bool bSourceDirty = false, bTypeDirty = false, bCleaned = false;
			FString Rows = TEXT("point,frame,world_time,slot,attack_instance,lease,owner_id,component_id,pool,active,complete,execution_state,registered,local_effect,lod_distance,native_culled,native_count_culled,owner_x,owner_y,owner_z,camera_x,camera_y,camera_z,lease_current,tracked_component_id,observed_frame,observed_world_time,native_view_count,view_match,view_revision,required_view_revision,observed_camera_target_id\n");
			void Cleanup()
			{
				if (bCleaned) { return; } bCleaned = true;
				if (ViewTick.IsTickFunctionRegistered()) { ViewTick.UnRegisterTickFunction(); }
				ViewTick.Observe = nullptr;
				if (auto* PC = Viewer.Get(); IsValid(PC) && !PC->IsActorBeingDestroyed())
				{
					bool bOwnedView = PC->GetViewTarget() == Camera.Get();
					for (const auto& Owner : Owners) { bOwnedView |= PC->GetViewTarget() == Owner.Get(); }
					if (bOwnedView && PreviousView.IsValid()) { PC->SetViewTarget(PreviousView.Get()); }
				}
				for (const auto& C : Cues) { if (IsValid(C.Get())) { C->EndAttackPresentation(); C->DestroyComponent(); } }
				for (const auto& Owner : Owners) { if (IsValid(Owner.Get()) && !Owner->IsActorBeingDestroyed()) { Owner->Destroy(); } }
				if (IsValid(Camera.Get()) && !Camera->IsActorBeingDestroyed()) { Camera->Destroy(); }
			}
			~FActiveFXCase() { Cleanup(); }
		};
		auto Case = MakeShared<FActiveFXCase>(); Case->FrequencyName=FrequencyName; Case->SystemSelection=SystemSelection; Case->bSavedPressure=bSavedPressure;
		if (bSavedPressure)
		{
			Case->Rows=TEXT("point,frame,world_time,slot,attack_instance,lease,owner_id,component_id,pool,active,complete,execution_state,registered,local_effect,lod_distance,native_culled,native_count_culled,native_distance_culled,native_visibility_culled,native_budget_culled,owner_x,owner_y,owner_z,camera_x,camera_y,camera_z,lease_current,tracked_component_id,observed_frame,native_view_count,view_match,view_revision,expected_limit,pressure_total,native_type_instances,source_active_instances,native_object_valid,native_object_destroyed\n");
		}
		if (FrequencyName==TEXT("High"))
		{
			auto* NativePeriod=IConsoleManager::Get().FindConsoleVariable(TEXT("fx.NiagaraScalabilityUpdateTime_High"));
			if (!NativePeriod || !FMath::IsFinite(NativePeriod->GetFloat()) || NativePeriod->GetFloat()<0)
			{ AddError(TEXT("Native High scalability cadence is unavailable/invalid")); return false; }
			Case->Frequency=ENiagaraScalabilityUpdateFrequency::High; Case->NativeUpdatePeriod=NativePeriod->GetFloat();
		}
		auto ActiveBody = [this,Case,Output,ReceiptName,StatesName]()
		{
			const auto Finish = [&](bool bSuccess,const TCHAR* Reason)
			{
				FString ObservationDetail;
				if (Case->bSavedPressure)
				{
					const auto& Last=Case->Observation; int32 MatchingIds=0;
					for (int32 I=0; I<3; ++I) { const auto* N=Case->Components[I].Get(); MatchingIds+=IsValid(N) && Last.ComponentIds[I]==N->GetUniqueID(); }
					for (int32 I=0; I<Case->ExtraComponents.Num(); ++I)
					{ const auto* N=Case->ExtraComponents[I].Get(); MatchingIds+=IsValid(N) && Last.Extra.IsValidIndex(I) && Last.Extra[I].ComponentId==N->GetUniqueID(); }
					ObservationDetail=FString::Printf(TEXT("actual_stage=%d frame=%llu observed_frame=%llu observed_revision=%u required_revision=%u required_frame=%llu views=%d camera_match=%d projection=%d observed_target=%u expected_camera=%u ids=%d/%d"),
						Case->Stage,uint64(GFrameCounter),Last.Frame,Last.Revision,Case->ViewRevision,Case->RequiredObservedFrame,Last.ViewCount,Last.bCameraMatches,Last.bProjectionValid,
						Last.Target.IsValid() ? Last.Target->GetUniqueID() : 0,Case->Camera.IsValid() ? Case->Camera->GetUniqueID() : 0,MatchingIds,3+Case->ExtraComponents.Num());
				}
				const FString DetailedReason=Case->bSavedPressure ? FString::Printf(TEXT("%s; %s"),Reason,*ObservationDetail) : FString(Reason);
				const bool bObserverWasRegistered=Case->ViewTick.IsTickFunctionRegistered();
				Case->Cleanup();
				const bool bObserverReleased=!Case->ViewTick.IsTickFunctionRegistered(); bSuccess &= bObserverReleased;
				const bool bOriginalsUnchanged = (!Case->Source.IsValid() || (Case->Source->GetEffectType() == Case->SourceType.Get()
					&& Case->Source->GetOutermost()->IsDirty() == Case->bSourceDirty
					&& (!Case->SourceType.IsValid() || Case->SourceType->GetOutermost()->IsDirty() == Case->bTypeDirty)))
					&& (!Case->VendorTemplate.IsValid() || Case->VendorTemplate->GetOutermost()->IsDirty()==Case->bVendorDirty)
					&& (!Case->SourceProfile.IsValid() || Case->SourceProfile->GetOutermost()->IsDirty()==Case->bProfileDirty);
				bSuccess &= bOriginalsUnchanged;
				FString Receipt;
				if (Case->bSavedPressure)
				{
					Receipt=FString::Printf(TEXT("{\"mode\":\"saved_project128_native_pressure\",\"success\":%s,\"stage\":%d,\"expected_saved_limit\":128,\"requested_pressure_total\":132,\"pressure_peak_distinct_components\":%d,\"surviving_distinct_components\":%d,\"observed_more_than_saved_limit\":%s,\"current_leases\":%d,\"observed_live\":%d,\"observed_count_culled\":%d,\"observed_registered\":%d,\"observed_pool_released\":%d,\"observed_destroyed\":%d,\"retired_total\":%d,\"source_max_pool_size\":%u,\"native_count_flags\":%d,\"native_other_flags\":%d,\"native_type_instances\":%d,\"source_active_instances\":%d,\"originals_unchanged\":%s,\"observer_was_registered\":%s,\"observer_released\":%s,\"native_update_period\":%.6f,\"source_system\":\"%s\",\"source_type\":\"%s\",\"source_effective_limit\":%d,\"native_cull_reaction\":%d,\"native_frequency\":%d,\"distance\":2500,\"effective_cull_not_rendered\":%s,\"effective_cull_view_frustum\":%s,\"effective_pre_cull_view_frustum\":%s,\"reason\":\"%s\",\"last_pressure_detail\":\"%s\",\"scope\":\"Saved128 policy at132 remote leases; not product capacity, mandatory warning, mixed workload or FPS/GPU approval\"}\n"),
						bSuccess ? TEXT("true") : TEXT("false"),Case->Stage,Case->PressurePeakDistinct,Case->PressureDistinct,Case->PressurePeakDistinct>128 ? TEXT("true") : TEXT("false"),Case->PressureCurrent,Case->PressureLive,Case->PressureCulled,Case->PressureRegistered,Case->PressureReleased,Case->PressureDestroyed,Case->PressureReleased+Case->PressureDestroyed,Case->Source.IsValid() ? uint32(Case->Source->MaxPoolSize) : 0u,Case->PressureCountFlags,Case->PressureOtherFlags,
						Case->SourceType.IsValid() ? Case->SourceType->NumInstances : -1,Case->Source.IsValid() ? Case->Source->GetActiveInstancesCount() : -1,
						bOriginalsUnchanged ? TEXT("true") : TEXT("false"),bObserverWasRegistered ? TEXT("true") : TEXT("false"),bObserverReleased ? TEXT("true") : TEXT("false"),Case->NativeUpdatePeriod,
						*GetPathNameSafe(Case->Source.Get()).ReplaceCharWithEscapedChar(),*GetPathNameSafe(Case->SourceType.Get()).ReplaceCharWithEscapedChar(),
						Case->Source.IsValid() ? Case->Source->GetScalabilitySettings().MaxInstances : 0,Case->SourceType.IsValid() ? int32(Case->SourceType->CullReaction) : -1,Case->SourceType.IsValid() ? int32(Case->SourceType->UpdateFrequency) : -1,
						Case->Source.IsValid() && Case->Source->GetScalabilitySettings().VisibilityCulling.bCullWhenNotRendered ? TEXT("true") : TEXT("false"),
						Case->Source.IsValid() && Case->Source->GetScalabilitySettings().VisibilityCulling.bCullByViewFrustum ? TEXT("true") : TEXT("false"),
						Case->Source.IsValid() && Case->Source->GetScalabilitySettings().VisibilityCulling.bAllowPreCullingByViewFrustum ? TEXT("true") : TEXT("false"),
						*DetailedReason.ReplaceCharWithEscapedChar(),*Case->PressureDetail.ReplaceCharWithEscapedChar());
				}
				else { Receipt = FString::Printf(TEXT("{\"mode\":\"transient_native_active_scalability\",\"success\":%s,\"stage\":%d,\"test_effect_type_limit\":2,\"preserved_system_distance\":2500,\"originals_unchanged\":%s,\"reason\":\"%s\",\"scope\":\"Real local viewport and native world FX ticks; transient count2 fixture only, not production capacity or GPU cost\",\"frequency\":\"%s\",\"native_update_period\":%.6f,\"observed_frame\":\"%llu\",\"native_view_count\":%d,\"projection_valid\":%s,\"view_match\":%s,\"observer_was_registered\":%s,\"observer_released\":%s,\"source_selection\":\"%s\",\"source_system\":\"%s\",\"source_type\":\"%s\",\"source_effective_count_enabled\":%s,\"source_effective_limit\":%d,\"source_native_frequency\":%d}\n"),
					bSuccess ? TEXT("true") : TEXT("false"),Case->Stage,bOriginalsUnchanged ? TEXT("true") : TEXT("false"),*FString(Reason).ReplaceCharWithEscapedChar(),*Case->FrequencyName,Case->NativeUpdatePeriod,Case->Observation.Frame,Case->Observation.ViewCount,
					Case->Observation.bProjectionValid ? TEXT("true") : TEXT("false"),Case->Observation.bCameraMatches ? TEXT("true") : TEXT("false"),
					bObserverWasRegistered ? TEXT("true") : TEXT("false"),bObserverReleased ? TEXT("true") : TEXT("false"),
					*Case->SystemSelection.ReplaceCharWithEscapedChar(),*GetPathNameSafe(Case->Source.Get()).ReplaceCharWithEscapedChar(),
					*GetPathNameSafe(Case->SourceType.Get()).ReplaceCharWithEscapedChar(),
					Case->Source.IsValid() && Case->Source->GetScalabilitySettings().bCullMaxInstanceCount ? TEXT("true") : TEXT("false"),
					Case->Source.IsValid() ? Case->Source->GetScalabilitySettings().MaxInstances : 0,
					Case->SourceType.IsValid() ? int32(Case->SourceType->UpdateFrequency) : INDEX_NONE); }
				const bool bSaved = FFileHelper::SaveStringToFile(Receipt,*(Output/ReceiptName),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
					&& FFileHelper::SaveStringToFile(Case->Rows,*(Output/StatesName),FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
				if (!bSuccess) { AddError(DetailedReason); }
				TestTrue(TEXT("Original input systems, EffectType and optional real profile retain identity/dirty state"),bOriginalsUnchanged);
				TestTrue(TEXT("Test-local world view observer unregisters on every terminal path"),bObserverReleased);
				TestTrue(TEXT("Active native scalability receipt and raw observations are preserved"),bSaved);
				return true;
			};
			if (FPlatformTime::Seconds()-Case->Began >= 30)
			{ return Finish(false,Case->bSavedPressure ? *FString::Printf(TEXT("Saved128 native pressure deadline expired: %s"),*Case->PressureDetail) : TEXT("Bounded native view/FX scalability lifecycle deadline expired")); }
			if (Case->LastFrame == GFrameCounter) { return false; } Case->LastFrame = GFrameCounter;
			if (Case->Stage>0 && !Case->World.IsValid()) { return Finish(false,TEXT("Captured native host world retired")); }
			if (!Case->World.IsValid())
			{
				for (const auto& Context : GEngine->GetWorldContexts())
				{
					auto* W = Context.World(); auto* PC = W ? W->GetFirstPlayerController() : nullptr;
					auto* LP = PC ? PC->GetLocalPlayer() : nullptr; FSceneViewProjectionData Projection;
					if (W && (W->WorldType == EWorldType::Game || W->WorldType == EWorldType::PIE) && W->HasBegunPlay()
						&& !W->bIsTearingDown && W->GetNetMode() != NM_DedicatedServer && PC && PC->IsLocalController()
						&& LP && LP->ViewportClient && LP->ViewportClient->Viewport && W->FXSystem.IsValid() && W->PersistentLevel
						&& LP->GetProjectionData(LP->ViewportClient->Viewport,Projection))
					{
						auto* Manager = FNiagaraWorldManager::Get(W);
						if (Manager) { Case->World = W; Case->Viewer = PC; Case->PreviousView = PC->GetViewTarget(); break; }
					}
				}
				if (!Case->World.IsValid()) { return false; } // No invented view cache or synthetic player.
				if (!Case->PreviousView.IsValid()) { return Finish(false,TEXT("Native host has no restorable initial view target")); }
				const TWeakPtr<FActiveFXCase> WeakCase(Case);
				Case->ViewTick.Observe = [WeakCase]()
				{
					const auto Current=WeakCase.Pin(); if (!Current || Current->bCleaned) { return; }
					FActiveFXCase::FObservedView Observed; Observed.Frame=GFrameCounter; Observed.Revision=Current->ViewRevision;
					auto* World=Current->World.Get(); auto* Player=Current->Viewer.Get();
					if (IsValid(World) && !World->bIsTearingDown && IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->GetWorld()==World)
					{
						Observed.WorldTime=World->GetTimeSeconds(); Observed.Target=Player->GetViewTarget();
						FRotator Rotation; Player->GetPlayerViewPoint(Observed.CameraLocation,Rotation);
						auto* Local=Player->GetLocalPlayer(); FSceneViewProjectionData Projection;
						Observed.bProjectionValid=Local && Local->ViewportClient && Local->ViewportClient->Viewport && Local->GetProjectionData(Local->ViewportClient->Viewport,Projection);
						if (auto* Native=FNiagaraWorldManager::Get(World))
						{
							const auto Views=Native->GetCachedViewInfo(); Observed.ViewCount=Views.Num();
							FVector ExpectedCamera; FRotator ExpectedRotation;
							if (auto* Target=Observed.Target.Get())
							{
								Target->GetActorEyesViewPoint(ExpectedCamera,ExpectedRotation);
								if (Observed.CameraLocation.Equals(ExpectedCamera,.1))
									for (const auto& View : Views) { Observed.bCameraMatches |= View.ViewToWorld.GetOrigin().Equals(Observed.CameraLocation,.1); }
							}
							for (int32 I=0; I<3; ++I)
							{
								if (Current->Owners.IsValidIndex(I) && Current->Owners[I].IsValid()) { Observed.OwnerLocations[I]=Current->Owners[I]->GetActorLocation(); }
								if (auto* N=Current->Components[I].Get(); IsValid(N))
								{
									Observed.ComponentIds[I]=N->GetUniqueID();
									if (Observed.ViewCount>0) { Observed.Distances[I]=Native->GetLODDistance(N->GetComponentLocation()); }
								}
							}
							if (Current->bSavedPressure)
							{
								Observed.Extra.SetNum(Current->ExtraComponents.Num());
								for (int32 I=0; I<Current->ExtraComponents.Num(); ++I)
								{
									auto& Extra=Observed.Extra[I]; const int32 OwnerIndex=I+3;
									if (Current->Owners.IsValidIndex(OwnerIndex) && Current->Owners[OwnerIndex].IsValid()) { Extra.OwnerLocation=Current->Owners[OwnerIndex]->GetActorLocation(); }
									if (auto* N=Current->ExtraComponents[I].Get(); IsValid(N))
									{ Extra.ComponentId=N->GetUniqueID(); if (Observed.ViewCount>0) { Extra.Distance=Native->GetLODDistance(N->GetComponentLocation()); } }
								}
							}
						}
					}
					Current->Observation=MoveTemp(Observed); // Values only; no native cache/state mutation.
				};
				Case->ViewTick.bCanEverTick=true; Case->ViewTick.bStartWithTickEnabled=true; Case->ViewTick.bRunOnAnyThread=false;
				Case->ViewTick.TickGroup=Case->ViewTick.EndTickGroup=TG_PostUpdateWork;
				Case->ViewTick.RegisterTickFunction(Case->World->PersistentLevel); Case->ViewTick.SetTickFunctionEnable(true);
				return false; // Wait for a genuine world-tick observation.
			}
			auto* W = Case->World.Get(); auto* PC = Case->Viewer.Get(); auto* Manager = W ? FNiagaraWorldManager::Get(W) : nullptr;
			if (!IsValid(W) || W->bIsTearingDown || !IsValid(PC) || PC->IsActorBeingDestroyed() || PC->GetWorld()!=W || !Manager)
			{ return Finish(false,TEXT("Captured native host world/player/FX manager retired")); }
			const auto& Observed=Case->Observation;
			if (Observed.Frame==MAX_uint64 || Observed.Frame>GFrameCounter || GFrameCounter-Observed.Frame>1
				|| (Case->LastObservedFrame!=MAX_uint64 && Observed.Frame<=Case->LastObservedFrame)
				|| Observed.Frame<Case->RequiredObservedFrame || Observed.Revision!=Case->ViewRevision || Observed.ViewCount<1
				|| !Observed.bProjectionValid || !Observed.bCameraMatches || Observed.Target.Get()!=PC->GetViewTarget()) { return false; }
			Case->LastObservedFrame=Observed.Frame;
			if (Case->LastWorldTime==Observed.WorldTime) { return false; } Case->LastWorldTime=Observed.WorldTime;
			const auto Record = [&](const TCHAR* Point)
			{
				const FVector ViewLocation=Observed.CameraLocation;
				if (Case->bSavedPressure)
				{
					for (int32 I=0; I<Case->Cues.Num(); ++I)
					{
						auto* C=Case->Cues[I].Get(); auto* RawNative=I<3 ? Case->Components[I].Get() : Case->ExtraComponents[I-3].Get();
						const bool bNativeValid=IsValid(RawNative), bNativeDestroyed=!bNativeValid || RawNative->IsBeingDestroyed();
						auto* N=bNativeValid ? RawNative : nullptr;
						const uint64 Lease=I<3 ? Case->Leases[I] : Case->ExtraLeases[I-3];
						const FVector P=I<3 ? Observed.OwnerLocations[I] : Observed.Extra[I-3].OwnerLocation;
						const double Distance=I<3 ? Observed.Distances[I] : Observed.Extra[I-3].Distance;
						const auto* Tracked=C ? C->ActiveLoopingCues.FindRef(CrowdCueTag).Get() : nullptr;
						int32 Cull=-1,Count=-1,DistanceCull=-1,Visibility=-1,Budget=-1;
#if WITH_NIAGARA_DEBUGGER
						if (N) { const auto& State=N->DebugCachedScalabilityState; Cull=State.bCulled; Count=State.bCulledByInstanceCount; DistanceCull=State.bCulledByDistance; Visibility=State.bCulledByVisibility; Budget=State.bCulledByGlobalBudget; }
#endif
						Case->Rows+=FString::Printf(TEXT("%s,%llu,%.6f,%d,%u,%llu,%u,%u,%d,%d,%d,%d,%d,%d,%.6f,%d,%d,%d,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%u,%llu,%d,%d,%u,128,132,%d,%d,%d,%d\n"),Point,uint64(GFrameCounter),Observed.WorldTime,I,C ? C->ActiveAttackInstance : 0,Lease,
							Case->Owners[I]->GetUniqueID(),N ? N->GetUniqueID() : 0,N ? int32(N->PoolingMethod) : -1,N && N->IsActive(),N && N->IsComplete(),N ? int32(N->GetExecutionState()) : -1,
							N && N->IsRegisteredWithScalabilityManager(),N && N->IsLocalPlayerEffect(),Distance,Cull,Count,DistanceCull,Visibility,Budget,P.X,P.Y,P.Z,ViewLocation.X,ViewLocation.Y,ViewLocation.Z,
							C && C->IsCueLeaseCurrent(Lease),Tracked ? Tracked->GetUniqueID() : 0,Observed.Frame,Observed.ViewCount,Observed.bCameraMatches,Observed.Revision,Case->SourceType->NumInstances,Case->Source->GetActiveInstancesCount(),bNativeValid,bNativeDestroyed);
					}
					return;
				}
				for (int32 I=0; I<3; ++I)
				{
					const FVector OwnerLocation=Observed.OwnerLocations[I];
					auto* N = Case->Components[I].Get(); auto* C = Case->Cues[I].Get();
					const auto* Tracked=C->ActiveLoopingCues.FindRef(CrowdCueTag).Get();
					int32 NativeCulled=-1, NativeCountCulled=-1;
#if WITH_NIAGARA_DEBUGGER
					if (N) { NativeCulled=N->DebugCachedScalabilityState.bCulled; NativeCountCulled=N->DebugCachedScalabilityState.bCulledByInstanceCount; }
#endif
					Case->Rows += FString::Printf(TEXT("%s,%llu,%.6f,%d,%u,%llu,%u,%u,%d,%d,%d,%d,%d,%d,%.6f,%d,%d,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%u,%llu,%.6f,%d,%d,%u,%u,%u\n"),Point,
						uint64(GFrameCounter),Observed.WorldTime,I,C->ActiveAttackInstance,Case->Leases[I],Case->Owners[I]->GetUniqueID(),N ? N->GetUniqueID() : 0,
						N ? int32(N->PoolingMethod) : -1,N && N->IsActive(),N && N->IsComplete(),N ? int32(N->GetExecutionState()) : -1,
						N && N->IsRegisteredWithScalabilityManager(),N && N->IsLocalPlayerEffect(),Observed.Distances[I],
						NativeCulled,NativeCountCulled,OwnerLocation.X,OwnerLocation.Y,OwnerLocation.Z,ViewLocation.X,ViewLocation.Y,ViewLocation.Z,
						C->IsCueLeaseCurrent(Case->Leases[I]),Tracked ? Tracked->GetUniqueID() : 0,
						Observed.Frame,Observed.WorldTime,Observed.ViewCount,Observed.bCameraMatches,Observed.Revision,Case->ViewRevision,
						Observed.Target.IsValid() ? Observed.Target->GetUniqueID() : 0);
				}
			};
			const auto Live = [&](int32 I) { auto* N=Case->Components[I].Get(); return IsValid(N) && N->IsActive() && !N->IsComplete(); };
			const auto Culled = [&](int32 I) { auto* N=Case->Components[I].Get(); return IsValid(N) && !N->IsActive() && N->IsComplete() && N->IsRegisteredWithScalabilityManager(); };
			const auto Released = [](UNiagaraComponent* N) { return IsValid(N) && !N->IsActive() && !N->IsRegisteredWithScalabilityManager() && N->PoolingMethod==ENCPoolMethod::FreeInPool; };
			const auto PressureMatches = [&](int32 ExpectedCurrent,int32 ExpectedCulled)
			{
				if (!Case->bSavedPressure) { return true; }
				Case->PressureCurrent=Case->PressureLive=Case->PressureCulled=Case->PressureRegistered=Case->PressureReleased=Case->PressureCountFlags=Case->PressureOtherFlags=Case->PressureDestroyed=0;
				const bool bTerminal=Case->Stage==9;
				bool bValid=Case->Owners.Num()==132 && Case->Cues.Num()==132 && Case->ExtraComponents.Num()==129 && Observed.Extra.Num()==129;
				int32 InvalidSlot=-1; TSet<UNiagaraComponent*> SeenComponents; SeenComponents.Reserve(132);
				if (bValid) for (int32 I=0; I<132; ++I)
				{
					auto* C=Case->Cues[I].Get(); auto* N=I<3 ? Case->Components[I].Get() : Case->ExtraComponents[I-3].Get();
					const uint64 Lease=I<3 ? Case->Leases[I] : Case->ExtraLeases[I-3];
					const auto* Owner=Case->Owners[I].Get(); const FVector P=I<3 ? Observed.OwnerLocations[I] : Observed.Extra[I-3].OwnerLocation;
					const FVector Expected=I<3 ? Case->ExpectedOwners[I] : Case->ExtraExpectedOwners[I-3];
					const uint32 Id=I<3 ? Observed.ComponentIds[I] : Observed.Extra[I-3].ComponentId;
					const double Distance=I<3 ? Observed.Distances[I] : Observed.Extra[I-3].Distance;
					if (bTerminal)
					{
						bool bSlot=IsValid(C) && IsValid(Owner) && !Owner->IsActorBeingDestroyed() && Owner->GetWorld()==W && C->GetOwner()==Owner
							&& C->IsRegistered() && !C->IsBeingDestroyed() && P.Equals(Expected,.1) && Owner->GetActorLocation().Equals(Expected,.1)
							&& !C->IsCueLeaseCurrent(Lease) && C->ActiveLoopingCues.IsEmpty();
						if (!IsValid(N)) { ++Case->PressureDestroyed; }
						else if (N->IsBeingDestroyed())
						{
							++Case->PressureDestroyed; bSlot &= !N->IsActive() && !N->IsRegisteredWithScalabilityManager() && !N->IsRegistered() && N->PoolingMethod==ENCPoolMethod::None;
						}
						else
						{
							const bool bDistinct=!SeenComponents.Contains(N); SeenComponents.Add(N);
							Case->PressureLive+=N->IsActive(); Case->PressureRegistered+=N->IsRegisteredWithScalabilityManager();
							const bool bReleased=Released(N); Case->PressureReleased+=bReleased;
							bSlot &= bDistinct && N->GetWorld()==W && N->GetAsset()==Case->Source.Get() && N->GetUniqueID()==Id && bReleased;
						}
						if (!bSlot && InvalidSlot<0) { InvalidSlot=I; } bValid &= bSlot; continue;
					}
					if (!IsValid(C) || !IsValid(N) || !IsValid(Owner)) { bValid=false; if (InvalidSlot<0) { InvalidSlot=I; } continue; }
					const bool bCurrent=C->IsCueLeaseCurrent(Lease);
					const bool bExpectedCurrent=Case->Stage<9 && !(I==0 && Case->Stage>=6) && !(I==2 && Case->Stage==4);
					const bool bDistinct=!SeenComponents.Contains(N); SeenComponents.Add(N);
					bool bSlot=bDistinct && IsValid(C) && IsValid(N) && IsValid(Owner) && !Owner->IsActorBeingDestroyed() && Owner->GetWorld()==W && C->GetOwner()==Owner
						&& N->GetWorld()==W && N->GetAsset()==Case->Source.Get() && N->GetUniqueID()==Id && P.Equals(Expected,.1) && Owner->GetActorLocation().Equals(Expected,.1)
						&& bCurrent==bExpectedCurrent;
					if (bCurrent)
					{
						++Case->PressureCurrent; const bool bLive=N && N->IsActive() && !N->IsComplete();
						const bool bCulled=N && !N->IsActive() && N->IsComplete() && N->IsRegisteredWithScalabilityManager();
						Case->PressureLive+=bLive; Case->PressureCulled+=bCulled; Case->PressureRegistered+=N && N->IsRegisteredWithScalabilityManager();
						bSlot &= C->ActiveLoopingCues.FindRef(CrowdCueTag).Get()==N && N->PoolingMethod==ENCPoolMethod::ManualRelease && !N->IsLocalPlayerEffect()
							&& N->IsRegisteredWithScalabilityManager() && FMath::IsFinite(Distance) && Distance>=0 && Distance<2500 && (bLive || bCulled);
#if WITH_NIAGARA_DEBUGGER
						const auto& State=N->DebugCachedScalabilityState; Case->PressureCountFlags+=State.bCulledByInstanceCount;
						Case->PressureOtherFlags+=State.bCulledByDistance || State.bCulledByVisibility || State.bCulledByGlobalBudget;
						bSlot &= bool(State.bCulled)==bCulled && bool(State.bCulledByInstanceCount)==bCulled;
#else
						bSlot=false;
#endif
					}
					else { const bool bReleased=Released(N); Case->PressureReleased+=bReleased; bSlot &= C && C->ActiveLoopingCues.IsEmpty() && bReleased; }
					if (!bSlot && InvalidSlot<0) { InvalidSlot=I; } bValid &= bSlot;
				}
				Case->PressureDistinct=SeenComponents.Num();
				Case->PressureDetail=FString::Printf(TEXT("stage=%d current=%d live=%d culled=%d registered=%d pool=%d destroyed=%d survivingDistinct=%d peakDistinct=%d countflags=%d otherflags=%d invalid=%d nativeType=%d poolMax=%u"),Case->Stage,
					Case->PressureCurrent,Case->PressureLive,Case->PressureCulled,Case->PressureRegistered,Case->PressureReleased,Case->PressureDestroyed,Case->PressureDistinct,Case->PressurePeakDistinct,Case->PressureCountFlags,Case->PressureOtherFlags,InvalidSlot,
					Case->SourceType.IsValid() ? Case->SourceType->NumInstances : -1,Case->Source.IsValid() ? uint32(Case->Source->MaxPoolSize) : 0u);
				if (bTerminal)
				{
					return bValid && Case->PressurePeakDistinct==132 && Case->PressureCurrent==0 && Case->PressureLive==0 && Case->PressureRegistered==0
						&& Case->PressureDistinct==Case->PressureReleased && Case->PressureReleased+Case->PressureDestroyed==132;
				}
				const bool bMatches=bValid && Case->PressureDistinct==132 && Case->PressureCurrent==ExpectedCurrent && Case->PressureLive==ExpectedCurrent-ExpectedCulled && Case->PressureCulled==ExpectedCulled
					&& Case->PressureRegistered==ExpectedCurrent && Case->PressureReleased==132-ExpectedCurrent && Case->PressureCountFlags==ExpectedCulled && Case->PressureOtherFlags==0;
				if (bMatches) { Case->PressurePeakDistinct=FMath::Max(Case->PressurePeakDistinct,Case->PressureDistinct); }
				return bMatches;
			};
			const auto Stable = [&](bool bCondition)
			{
				if (!bCondition) { Case->Stable=0; Case->StableSince=-1; return false; }
				if (!Case->Stable) { Case->StableSince=Observed.WorldTime; } ++Case->Stable;
				// Observe at least two native High periods without advancing or overriding its clock.
				return Case->Stable>=3 && Observed.WorldTime-Case->StableSince>=2*Case->NativeUpdatePeriod;
			};
			const auto Next = [&]()
			{ ++Case->Stage; Case->Stable=0; Case->StableSince=-1; ++Case->ViewRevision; Case->RequiredObservedFrame=GFrameCounter; };
			if (Case->Stage == 0)
			{
				const bool bProject=Case->SystemSelection==TEXT("Project");
				if (bProject)
				{
					Case->SourceProfile.Reset(LoadObject<UProject_JAttackPresentationProfile>(nullptr,TrailProfilePath));
					if (!Case->SourceProfile.IsValid()) { return Finish(false,TEXT("Actual project presentation profile unavailable")); }
					Case->bProfileDirty=Case->SourceProfile->GetOutermost()->IsDirty();
					const FGameplayTag TrailTag=FGameplayTag::RequestGameplayTag(TEXT("PresentationCue.Combat.Trail"),false);
					int32 Matches=0;
					for (const auto& Cue : Case->SourceProfile->Cues)
						if (Cue.CueTag==TrailTag)
						{
							if (!Cue.bLooping || !Cue.bUseRemoteSpawnBudget || Cue.NiagaraSystem.ToSoftObjectPath()!=FSoftObjectPath(OwnedTrailSystemPath))
							{ return Finish(false,TEXT("Actual looping Trail does not consume the exact project-owned system")); } ++Matches;
						}
					if (!TrailTag.IsValid() || Matches!=1) { return Finish(false,TEXT("Actual project Trail cue identity is missing or ambiguous")); }
				}
				Case->Source.Reset(LoadObject<UNiagaraSystem>(nullptr,bProject ? OwnedTrailSystemPath : TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic")));
				if (!Case->Source.IsValid()) { return Finish(false,TEXT("Selected original trail unavailable")); }
				Case->SourceType.Reset(Case->Source->GetEffectType()); Case->bSourceDirty=Case->Source->GetOutermost()->IsDirty();
				Case->bTypeDirty=Case->SourceType.IsValid() && Case->SourceType->GetOutermost()->IsDirty();
				if (bProject)
				{
					Case->VendorTemplate.Reset(LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic")));
					Case->bVendorDirty=Case->VendorTemplate.IsValid() && Case->VendorTemplate->GetOutermost()->IsDirty();
					if (!HasOwnedTrailPolicy(Case->Source.Get(),Case->SourceType.Get(),Case->VendorTemplate.Get()))
					{ return Finish(false,TEXT("Saved project system/type do not match the fixed-count High resume/local/distance/bounds policy")); }
				}
				if (Case->bSavedPressure)
				{
					if (Case->Source->GetScalabilitySettings().MaxInstances!=128)
					{ return Finish(false,TEXT("Saved128 pressure requires the actual effective128 policy")); }
					Case->System.Reset(Case->Source.Get()); Case->Type.Reset(Case->SourceType.Get());
				}
				else
				{
				Case->System.Reset(DuplicateObject<UNiagaraSystem>(Case->Source.Get(),GetTransientPackage(),MakeUniqueObjectName(GetTransientPackage(),UNiagaraSystem::StaticClass(),TEXT("ProjectJActiveFX99"))));
				Case->Type.Reset(NewObject<UNiagaraEffectType>(GetTransientPackage(),NAME_None,RF_Transient));
				if (!Case->System.IsValid() || !Case->Type.IsValid()) { return Finish(false,TEXT("Transient native system/type creation failed")); }
				Case->System->SetFlags(RF_Transient); Case->System->ClearFlags(RF_Public|RF_Standalone);
				Case->Type->bAllowCullingForLocalPlayers=false; Case->Type->UpdateFrequency=Case->Frequency;
				Case->Type->CullReaction=ENiagaraCullReaction::DeactivateImmediateResume;
				Case->Type->SignificanceHandler=NewObject<UNiagaraSignificanceHandlerDistance>(Case->Type.Get());
				if (Case->SourceType.IsValid())
				{
					Case->Type->EmitterScalabilitySettings=Case->SourceType->EmitterScalabilitySettings;
					Case->Type->ReadinessSettings=Case->SourceType->ReadinessSettings; Case->Type->ReadinessProfile=Case->SourceType->ReadinessProfile;
				}
				auto Settings=Case->SourceType.IsValid() ? Case->SourceType->GetActiveSystemScalabilitySettings() : Case->Type->GetActiveSystemScalabilitySettings();
				Settings.Platforms=FNiagaraPlatformSet(FNiagaraPlatformSet::CreateQualityLevelMask(FNiagaraPlatformSet::GetQualityLevel()));
				Settings.bCullMaxInstanceCount=true; Settings.MaxInstances=2; Case->Type->SystemScalabilitySettings.Settings.Add(Settings);
				Case->System->SetEffectType(Case->Type.Get()); // Only the transient copy; retain the original2500 distance override.
				const auto& Effective=Case->System->GetScalabilitySettings();
				if (!Settings.Platforms.IsActive() || !Effective.bCullMaxInstanceCount || Effective.MaxInstances!=2
					|| !Effective.bCullByDistance || !FMath::IsNearlyEqual(Effective.MaxDistance,2500.f) || !Case->System->NeedsSortedSignificanceCull()
					|| Case->System->AllowCullingForLocalPlayers()) { return Finish(false,TEXT("Transient effective native count/distance/local policy did not resolve")); }
				}
				FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride=ESpawnActorCollisionHandlingMethod::AlwaysSpawn; Spawn.ObjectFlags|=RF_Transient;
				FRotator ViewRotation; PC->GetPlayerViewPoint(Case->Origin,ViewRotation);
				Case->Camera.Reset(W->SpawnActor<ACameraActor>(Case->Origin,FRotator::ZeroRotator,Spawn));
				if (!Case->Camera.IsValid()) { return Finish(false,TEXT("Native test camera spawn failed")); }
				Case->Set.Reset(NewObject<UProject_JCombatPresentationSet>(GetTransientPackage())); Case->Profile.Reset(NewObject<UProject_JAttackPresentationProfile>(Case->Set.Get()));
				auto& Def=Case->Profile->Cues.AddDefaulted_GetRef(); Def.CueTag=CrowdCueTag; Def.NiagaraSystem=Case->System.Get();
				Def.AttachmentTarget=EProject_JCombatVFXAttachmentTarget::CharacterMesh; Def.bLooping=true; Def.bDestroyImmediatelyOnStop=true;
				Def.bUseRemoteSpawnBudget=false; // This isolated test measures active-policy lifecycle;98 default admission tests stay unchanged.
				auto& Entry=Case->Set->AttackPresentations.AddDefaulted_GetRef(); Entry.AttackTag=CrowdAttackTag; Entry.Profile=Case->Profile.Get();
				PC->SetViewTarget(Case->Camera.Get());
				for (int32 I=0; I<3; ++I)
				{
					const float X=I==0 ? 400.f : (I==1 ? 1000.f : 1800.f);
					Case->ExpectedOwners[I]=Case->Origin+FVector(X,0,0);
					auto* Owner=W->SpawnActor<AProject_JNPCCharacter>(Case->ExpectedOwners[I],FRotator::ZeroRotator,Spawn);
					if (!Owner) { return Finish(false,TEXT("Native cosmetic owner spawn failed")); } Case->Owners.Emplace(Owner);
					// Existing public audit kinematics contract, only on fixture-owned actors in empty Entry.
					if (!Project_J::Movement::SetMovementMode(Owner,MOVE_None)) { return Finish(false,TEXT("Fixture public movement freeze unavailable")); }
					auto* C=NewObject<UProject_JCombatPresentationComponent>(Owner); Owner->AddInstanceComponent(C); C->RegisterComponent(); C->BasePresentationSet=Case->Set.Get(); Case->Cues.Emplace(C);
					C->BeginAttackPresentation(CrowdAttackTag); Case->Leases[I]=C->BeginCueLease(CrowdCueTag);
					Case->Components[I].Reset(C->ActiveLoopingCues.FindRef(CrowdCueTag).Get());
					if (!Case->Leases[I] || !Case->Components[I].IsValid() || Case->Components[I]->PoolingMethod!=ENCPoolMethod::ManualRelease)
					{ return Finish(false,TEXT("Actual tracked loop requires native ManualRelease pooling")); }
				}
				if (Case->bSavedPressure)
				{
					Case->ExtraComponents.Reserve(129); Case->ExtraLeases.Reserve(129); Case->ExtraExpectedOwners.Reserve(129);
					for (int32 I=0; I<129; ++I)
					{
						const FVector Location=Case->Origin+FVector(600.f+1100.f*(float(I)/128.f),0,0);
						auto* Owner=W->SpawnActor<AProject_JNPCCharacter>(Location,FRotator::ZeroRotator,Spawn);
						if (!Owner) { return Finish(false,TEXT("Saved128 extra cosmetic owner spawn failed")); } Case->Owners.Emplace(Owner); Case->ExtraExpectedOwners.Add(Location);
						if (!Project_J::Movement::SetMovementMode(Owner,MOVE_None)) { return Finish(false,TEXT("Saved128 extra public movement freeze unavailable")); }
						auto* C=NewObject<UProject_JCombatPresentationComponent>(Owner); Owner->AddInstanceComponent(C); C->RegisterComponent(); C->BasePresentationSet=Case->Set.Get(); Case->Cues.Emplace(C);
						C->BeginAttackPresentation(CrowdAttackTag); const uint64 Lease=C->BeginCueLease(CrowdCueTag); Case->ExtraLeases.Add(Lease);
						auto* N=C->ActiveLoopingCues.FindRef(CrowdCueTag).Get(); Case->ExtraComponents.Emplace(N);
						if (!Lease || !IsValid(N) || N->GetAsset()!=Case->Source.Get() || N->PoolingMethod!=ENCPoolMethod::ManualRelease)
						{ return Finish(false,TEXT("Saved128 extra slot did not acquire an actual ManualRelease loop")); }
					}
				}
				Next(); return false; // Spawn row is captured after the first matching native FX observation.
			}
			if (PC->GetViewTarget()!=(Case->Stage==7 ? static_cast<AActor*>(Case->Owners[1].Get()) : static_cast<AActor*>(Case->Camera.Get())))
			{ return Finish(false,TEXT("Captured test view target changed externally")); }
			if (!Case->System->IsReadyToRun()) { return false; }
			const bool bTerminalPressure=Case->bSavedPressure && Case->Stage==9;
			if (!bTerminalPressure) for (int32 I=0; I<3; ++I)
				if (!Case->Components[I].IsValid() || Observed.ComponentIds[I]!=Case->Components[I]->GetUniqueID()) { return false; }
			if (Case->bSavedPressure)
			{
				if (Observed.Extra.Num()!=129) { return false; }
				if (!bTerminalPressure) for (int32 I=0; I<129; ++I)
					if (!Case->ExtraComponents[I].IsValid() || Observed.Extra[I].ComponentId!=Case->ExtraComponents[I]->GetUniqueID()) { return false; }
			}
			if (!Case->bSpawnedRecorded) { Record(TEXT("spawned")); Case->bSpawnedRecorded=true; }
			for (int32 I=0; I<3; ++I)
			{
				if (!Case->Owners[I].IsValid() || Case->Owners[I]->IsActorBeingDestroyed() || !Case->Cues[I].IsValid()) { return Finish(false,TEXT("Captured cosmetic owner/component retired")); }
				if (!Case->Owners[I]->GetActorLocation().Equals(Case->ExpectedOwners[I],.1)) { return Finish(false,TEXT("Fixture actor moved and invalidated distance significance ordering")); }
				const bool bMustBeReleased=(I==2 && Case->Stage==4) || (I==0 && Case->Stage>=6) || (Case->bSavedPressure && Case->Stage==9);
				if (!bMustBeReleased && (Case->Cues[I]->ActiveLoopingCues.FindRef(CrowdCueTag).Get()!=Case->Components[I].Get()
					|| !Case->Cues[I]->IsCueLeaseCurrent(Case->Leases[I]))) { return Finish(false,TEXT("Captured current attack/loop identity changed outside its native lifecycle phase")); }
				if (Case->Stage <= 3 && (!FMath::IsFinite(Observed.Distances[I]) || Observed.Distances[I]<0 || Observed.Distances[I]>=2500))
				{ return Finish(false,TEXT("Count-significance phase left the preserved distance range")); }
			}
			if (Case->Stage == 1 && Stable(PressureMatches(132,4)&&Live(0)&&Live(1)&&Culled(2)))
			{ Record(Case->bSavedPressure ? TEXT("saved128_count_pressure") : TEXT("count_two")); Case->Camera->SetActorLocation(Case->Origin+FVector(2300,0,0)); Next(); return false; }
			if (Case->Stage == 2 && Stable(PressureMatches(132,4)&&Culled(0)&&Live(1)&&Live(2)))
			{ Record(TEXT("same_component_resumed")); Case->Camera->SetActorLocation(Case->Origin); Next(); return false; }
			if (Case->Stage == 3 && Stable(PressureMatches(132,4)&&Live(0)&&Live(1)&&Culled(2)))
			{
				Record(TEXT("culled_before_matching_stop")); Case->RetiredStop.Reset(Case->Components[2].Get()); Case->OldStopLease=Case->Leases[2];
				Case->Cues[2]->EndCueLease(Case->OldStopLease);
				if (Case->Cues[2]->IsCueLeaseCurrent(Case->OldStopLease) || !Case->Cues[2]->ActiveLoopingCues.IsEmpty() || !Released(Case->RetiredStop.Get()))
				{ return Finish(false,TEXT("Matching culled cue stop did not release exact native pool/scalability ownership")); }
				Record(TEXT("matching_stop_released")); Case->Camera->SetActorLocation(Case->Origin+FVector(2300,0,0)); Next(); return false;
			}
			if (Case->Stage == 4 && Stable(PressureMatches(131,3)&&Released(Case->RetiredStop.Get())&&Case->Cues[2]->ActiveLoopingCues.IsEmpty()))
			{
				Record(TEXT("stopped_loop_did_not_resume")); Case->Cues[2]->BeginAttackPresentation(CrowdAttackTag); Case->Leases[2]=Case->Cues[2]->BeginCueLease(CrowdCueTag);
				Case->Components[2].Reset(Case->Cues[2]->ActiveLoopingCues.FindRef(CrowdCueTag).Get()); Case->Cues[2]->EndCueLease(Case->OldStopLease);
				if (!Case->Cues[2]->IsCueLeaseCurrent(Case->Leases[2]) || !Case->Components[2].IsValid()) { return Finish(false,TEXT("Old notify end canceled the replacement attack lease")); }
				Next(); return false;
			}
			if (Case->Stage == 5 && Stable(PressureMatches(132,4)&&Culled(0)&&Live(1)&&Live(2)))
			{
				Record(TEXT("culled_before_attack_replace")); Case->RetiredAttack.Reset(Case->Components[0].Get()); Case->ReplacedAttack=Case->Cues[0]->ActiveAttackInstance;
				Case->Cues[0]->BeginAttackPresentation(CrowdAttackTag); Case->Cues[0]->EndCueLease(Case->Leases[0]);
				if (Case->Cues[0]->ActiveAttackInstance==Case->ReplacedAttack || !Case->Cues[0]->ActiveLoopingCues.IsEmpty() || !Released(Case->RetiredAttack.Get()))
				{ return Finish(false,TEXT("Attack replacement did not retire the culled native incarnation")); }
				Case->Camera->SetActorLocation(Case->Origin); Next(); return false;
			}
			if (Case->bSavedPressure && Case->Stage==6 && Stable(PressureMatches(131,3)&&Released(Case->RetiredAttack.Get())
				&& !Case->Cues[2]->IsCueLeaseCurrent(Case->OldStopLease) && Live(1)&&Culled(2)))
			{
				Record(TEXT("saved128_retired_loops_did_not_resume"));
				for (const auto& C : Case->Cues) { C->EndAttackPresentation(); }
				Case->Stage=9; Case->Stable=0; Case->StableSince=-1; ++Case->ViewRevision; Case->RequiredObservedFrame=GFrameCounter;
				return false;
			}
			if (Case->bSavedPressure && Case->Stage==9 && Stable(PressureMatches(0,0)))
			{ Record(TEXT("saved128_all_leases_pool_released")); return Finish(true,TEXT("Actual saved128 count-only pressure, same-component view resume and exact culled retirement observed")); }
			if (!Case->bSavedPressure && Case->Stage == 6 && Stable(Released(Case->RetiredAttack.Get())&&Case->Cues[0]->ActiveLoopingCues.IsEmpty()&&Live(1)&&Live(2)))
			{ Record(TEXT("replaced_loop_did_not_resume")); PC->SetViewTarget(Case->Owners[1].Get()); Next(); return false; }
			if (Case->Stage == 7 && Stable(Live(1)&&Case->Components[1]->IsLocalPlayerEffect()&&!Case->Components[1]->IsRegisteredWithScalabilityManager()))
			{ Record(TEXT("native_locally_viewed_protected")); PC->SetViewTarget(Case->Camera.Get()); Next(); return false; }
			if (Case->Stage == 8 && Stable(Live(1)&&Live(2)&&!Case->Components[1]->IsLocalPlayerEffect()&&Case->Components[1]->IsRegisteredWithScalabilityManager()&&Released(Case->RetiredAttack.Get())))
			{ Record(TEXT("native_view_return")); return Finish(true,TEXT("Native significance cull/resume and matching-stop/attack/view lifetimes observed")); }
			return false;
		};
		ADD_LATENT_AUTOMATION_COMMAND(FSoftCrowdCueCommand(MoveTemp(ActiveBody)));
		return true;
#else
		AddError(TEXT("Active Niagara policy mode requires an Editor host for the public transient EffectType setup API")); return false;
#endif
	}
	// Opt-in metadata read: return before the normal world/pool/spawn regression.
	if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJReadSelectedNiagaraScalability")))
	{
		if (!TestTrue(TEXT("Selected Niagara settings are read on the game thread"), IsInGameThread())) { return false; }
		FString Output;
		if (!FParse::Value(FCommandLine::Get(), TEXT("ProjectJCrowdOutput="), Output) || Output.IsEmpty()
			|| !IFileManager::Get().DirectoryExists(*Output))
		{
			AddError(TEXT("Selected Niagara read requires an existing ProjectJCrowdOutput directory.")); return false;
		}
		const FString ReceiptPath = FPaths::Combine(Output, TEXT("SelectedNiagaraScalability.json"));
		if (IFileManager::Get().FileExists(*ReceiptPath))
		{
			AddError(TEXT("Preserve earlier selected Niagara evidence; use a fresh output directory.")); return false;
		}
		const auto Quote = [](const FString& Value)
		{
			FString Escaped(TEXT("\""));
			for (const TCHAR Character : Value)
			{
				switch (Character)
				{
				case TEXT('"'): Escaped += TEXT("\\\""); break;
				case TEXT('\\'): Escaped += TEXT("\\\\"); break;
				case TEXT('\n'): Escaped += TEXT("\\n"); break;
				case TEXT('\r'): Escaped += TEXT("\\r"); break;
				case TEXT('\t'): Escaped += TEXT("\\t"); break;
				default:
					if (Character < 0x20) { Escaped.Appendf(TEXT("\\u%04x"), static_cast<uint32>(Character)); }
					else { Escaped.AppendChar(Character); }
					break;
				}
			}
			return Escaped + TEXT("\"");
		};
		const auto Bool = [](const bool bValue) { return bValue ? TEXT("true") : TEXT("false"); };
		bool bFiniteSettings = true;
		const auto Number = [&bFiniteSettings](const float Value)
		{
			if (!FMath::IsFinite(Value)) { bFiniteSettings = false; return FString(TEXT("null")); }
			return FString::Printf(TEXT("%.9g"), static_cast<double>(Value));
		};
		const auto Ramp = [&Number](const FNiagaraLinearRamp& Value)
		{
			return TEXT("[") + Number(Value.StartX) + TEXT(",") + Number(Value.StartY)
				+ TEXT(",") + Number(Value.EndX) + TEXT(",") + Number(Value.EndY) + TEXT("]");
		};
		const auto Settings = [&Bool, &Number, &Ramp](const FNiagaraSystemScalabilitySettings& Value)
		{
			const auto& Visibility = Value.VisibilityCulling;
			const auto& Budget = Value.BudgetScaling;
			FString Json = FString::Printf(TEXT("{\"cull_by_distance\":%s,\"max_distance\":%s,\"cull_effect_type_count\":%s,\"max_effect_type_instances\":%d,\"cull_system_count\":%s,\"max_system_instances\":%d,"),
				Bool(Value.bCullByDistance), *Number(Value.MaxDistance), Bool(Value.bCullMaxInstanceCount), Value.MaxInstances,
				Bool(Value.bCullPerSystemMaxInstanceCount), Value.MaxSystemInstances);
			Json += FString::Printf(TEXT("\"visibility\":{\"cull_not_rendered\":%s,\"cull_view_frustum\":%s,\"allow_precull_view_frustum\":%s,\"max_time_without_render\":%s,\"max_time_outside_view_frustum\":%s},"),
				Bool(Visibility.bCullWhenNotRendered), Bool(Visibility.bCullByViewFrustum), Bool(Visibility.bAllowPreCullingByViewFrustum),
				*Number(Visibility.MaxTimeWithoutRender), *Number(Visibility.MaxTimeOutsideViewFrustum));
			Json += FString::Printf(TEXT("\"global_budget\":{\"cull\":%s,\"max_usage\":%s,\"scale_distance\":%s,\"scale_effect_type_count\":%s,\"scale_system_count\":%s,"),
				Bool(Budget.bCullByGlobalBudget), *Number(Budget.MaxGlobalBudgetUsage), Bool(Budget.bScaleMaxDistanceByGlobalBudgetUse),
				Bool(Budget.bScaleMaxInstanceCountByGlobalBudgetUse), Bool(Budget.bScaleSystemInstanceCountByGlobalBudgetUse));
			Json += TEXT("\"distance_ramp\":") + Ramp(Budget.MaxDistanceScaleByGlobalBudgetUse)
				+ TEXT(",\"effect_type_count_ramp\":") + Ramp(Budget.MaxInstanceCountScaleByGlobalBudgetUse)
				+ TEXT(",\"system_count_ramp\":") + Ramp(Budget.MaxSystemInstanceCountScaleByGlobalBudgetUse);
			Json += FString::Printf(TEXT("},\"cull_proxy_mode\":%u,\"max_system_proxies\":%d}"), static_cast<uint32>(Value.CullProxyMode), Value.MaxSystemProxies);
			return Json;
		};
		const TCHAR* SelectedPaths[] = {
			TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"),
			TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_AuraFX_Basic.NS_AuraFX_Basic")
		};
		FString Rows;
		bool bLoadedAll = true;
		bool bDirtyUnchanged = true;
		int32 LoadedCount = 0;
		for (const TCHAR* SelectedPath : SelectedPaths)
		{
			if (!Rows.IsEmpty()) { Rows += TEXT(","); }
			TStrongObjectPtr<UNiagaraSystem> SelectedSystem(LoadObject<UNiagaraSystem>(nullptr, SelectedPath));
			if (!TestNotNull(TEXT("Exact selected Niagara asset loads with its native type"), SelectedSystem.Get()))
			{
				bLoadedAll = false;
				Rows += TEXT("{\"requested_path\":") + Quote(SelectedPath) + TEXT(",\"loaded\":false}"); continue;
			}
			++LoadedCount;
			const bool bSystemDirtyBefore = SelectedSystem->GetOutermost()->IsDirty();
			TStrongObjectPtr<UNiagaraEffectType> SelectedType(SelectedSystem->GetEffectType());
			const bool bTypeDirtyBefore = SelectedType.IsValid() && SelectedType->GetOutermost()->IsDirty();
			Rows += TEXT("{\"requested_path\":") + Quote(SelectedPath) + TEXT(",\"loaded\":true,\"actual_path\":") + Quote(SelectedSystem->GetPathName());
			Rows += FString::Printf(TEXT(",\"platform_allowed\":%s,\"effective_allow_local_player_culling\":%s,\"sorted_significance_needed\":%s,\"has_gpu_emitters\":%s,\"fixed_bounds_enabled\":%s,"),
				Bool(SelectedSystem->IsAllowedByScalability()), Bool(SelectedSystem->AllowCullingForLocalPlayers()),
				Bool(SelectedSystem->NeedsSortedSignificanceCull()), Bool(SelectedSystem->HasAnyGPUEmitters()), Bool(SelectedSystem->bFixedBounds));
			Rows += TEXT("\"effective_settings\":") + Settings(SelectedSystem->GetScalabilitySettings());
			const auto& CurrentOverride = SelectedSystem->GetCurrentOverrideSettings();
			Rows += FString::Printf(TEXT(",\"active_override_flags\":{\"distance\":%s,\"effect_type_count\":%s,\"system_count\":%s,\"visibility\":%s,\"global_budget\":%s,\"cull_proxy\":%s}"),
				Bool(CurrentOverride.bOverrideDistanceSettings), Bool(CurrentOverride.bOverrideInstanceCountSettings), Bool(CurrentOverride.bOverridePerSystemInstanceCountSettings),
				Bool(CurrentOverride.bOverrideVisibilitySettings), Bool(CurrentOverride.bOverrideGlobalBudgetScalingSettings), Bool(CurrentOverride.bOverrideCullProxySettings));
			Rows += TEXT(",\"effect_type\":");
			if (SelectedType.IsValid())
			{
				Rows += TEXT("{\"path\":") + Quote(SelectedType->GetPathName());
				Rows += FString::Printf(TEXT(",\"allow_local_player_culling\":%s,\"update_frequency\":%d,\"cull_reaction\":%d,\"settings_count\":%d,\"significance_handler_class\":"),
					Bool(SelectedType->bAllowCullingForLocalPlayers), static_cast<int32>(SelectedType->UpdateFrequency),
					static_cast<int32>(SelectedType->CullReaction), SelectedType->GetSystemScalabilitySettings().Settings.Num());
				const UNiagaraSignificanceHandler* Handler = SelectedType->GetSignificanceHandler();
				Rows += Handler ? Quote(Handler->GetClass()->GetPathName()) : TEXT("null");
				Rows += TEXT(",\"active_settings_getter\":") + Settings(SelectedType->GetActiveSystemScalabilitySettings()) + TEXT("}");
			}
			else { Rows += TEXT("null"); }
			const bool bSystemDirtyAfter = SelectedSystem->GetOutermost()->IsDirty();
			const bool bTypeDirtyAfter = SelectedType.IsValid() && SelectedType->GetOutermost()->IsDirty();
			bDirtyUnchanged &= bSystemDirtyBefore == bSystemDirtyAfter && bTypeDirtyBefore == bTypeDirtyAfter;
			Rows += FString::Printf(TEXT(",\"system_dirty_before_read\":%s,\"system_dirty_after_read\":%s,\"effect_type_dirty_before_read\":%s,\"effect_type_dirty_after_read\":%s}"),
				Bool(bSystemDirtyBefore), Bool(bSystemDirtyAfter), Bool(bTypeDirtyBefore), Bool(bTypeDirtyAfter));
		}
		TestTrue(TEXT("Selected Niagara settings contain finite scalar values"), bFiniteSettings);
		TestTrue(TEXT("Native settings getters leave package dirty state unchanged"), bDirtyUnchanged);
		const bool bReadPassed = bLoadedAll && bFiniteSettings && bDirtyUnchanged;
		const FString Receipt = FString::Printf(TEXT("{\"schema\":1,\"mode\":\"selected_native_scalability_read\",\"game_thread\":true,\"requested_count\":2,\"loaded_count\":%d,\"niagara_quality_level\":%d,\"diagnostic_spawn_count\":0,\"finite_settings\":%s,\"dirty_state_unchanged\":%s,\"read_passed\":%s,\"systems\":[%s]}\n"),
			LoadedCount, FNiagaraPlatformSet::GetQualityLevel(), Bool(bFiniteSettings), Bool(bDirtyUnchanged), Bool(bReadPassed), *Rows);
		const bool bWritten = FFileHelper::SaveStringToFile(Receipt, *ReceiptPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		TestTrue(TEXT("Selected Niagara settings receipt is written"), bWritten);
		return bReadPassed && bWritten;
	}
	auto* System = LoadObject<UNiagaraSystem>(nullptr, TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic"));
	if (!TestNotNull(TEXT("Authored player trail"), System)) { return false; }
	auto* Pool = IConsoleManager::Get().FindConsoleVariable(TEXT("ProjectJ.Combat.Presentation.Pool"));
	const int32 Previous = Pool->GetInt();
	FString CSV = TEXT("pool,cycle,count,spawn_ms,stop_ms,unique_components\n");
	for (const int32 Mode : {0, 1})
	{
		Pool->Set(Mode, ECVF_SetByCode);
		auto* World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		World->InitializeActorsForPlay(FURL()); World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		auto* Set = NewObject<UProject_JCombatPresentationSet>(World);
		auto* Profile = NewObject<UProject_JAttackPresentationProfile>(Set);
		FProject_JCombatVFXCueDefinition Cue; Cue.CueTag = CrowdCueTag; Cue.NiagaraSystem = System;
		Cue.AttachmentTarget = EProject_JCombatVFXAttachmentTarget::World; Cue.bLooping = true; Cue.bDestroyImmediatelyOnStop = true;
		Cue.bUseRemoteSpawnBudget = false; // This existing fixture isolates native pool/notify timing, not shared admission.
		Profile->Cues.Add(Cue);
		FProject_JCombatAttackPresentationEntry Entry; Entry.AttackTag = CrowdAttackTag; Entry.Profile = Profile; Set->AttackPresentations.Add(Entry);
		TArray<UProject_JCombatPresentationComponent*> Components;
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		for (int32 I = 0; I < 100; ++I)
		{
			auto* Pawn = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector((I % 10) * 150, (I / 10) * 150, 100), FRotator::ZeroRotator, Params);
			auto* C = NewObject<UProject_JCombatPresentationComponent>(Pawn); C->RegisterComponent(); C->BasePresentationSet = Set; Components.Add(C);
		}
		TSet<FObjectKey> Identities;
		const uint64 Region = TRACE_BEGIN_REGION_WITH_ID(Mode ? TEXT("ProjectJ.Effects.Pooled") : TEXT("ProjectJ.Effects.Unpooled"));
		for (int32 Cycle = 0; Cycle < 12; ++Cycle)
		{
			const double Start = FPlatformTime::Seconds();
			for (auto* C : Components)
			{
				C->BeginAttackPresentation(CrowdAttackTag); C->PlayCue(CrowdCueTag);
				auto* N = C->ActiveLoopingCues.FindRef(CrowdCueTag).Get();
				if (TestNotNull(TEXT("Every simultaneous cue has a tracked Niagara component"), N))
				{
					Identities.Add(FObjectKey(N));
					if (Mode) { TestEqual(TEXT("Loop retains manual ownership until its notify stops"), N->PoolingMethod, ENCPoolMethod::ManualRelease); }
				}
			}
			const double SpawnMs = (FPlatformTime::Seconds() - Start) * 1000;
			for (int32 Frame = 0; Frame < 3; ++Frame) { World->Tick(LEVELTICK_All, 1.f / 60); }
			const double Stop = FPlatformTime::Seconds();
			for (auto* C : Components)
			{
				if (Cycle % 3 == 0) { C->StopCue(CrowdCueTag); }
				else if (Cycle % 3 == 1) { C->RefreshPresentation(); }
				else { C->EndAttackPresentation(); }
				TestEqual(TEXT("Notify, equipment refresh and attack end release all tracked references"), C->ActiveLoopingCues.Num(), 0);
			}
			CSV += FString::Printf(TEXT("%d,%d,100,%.6f,%.6f,%d\n"), Mode, Cycle, SpawnMs, (FPlatformTime::Seconds() - Stop) * 1000, Identities.Num());
			World->Tick(LEVELTICK_All, 1.f / 60);
		}
		if (Mode) { TestTrue(TEXT("Engine pool reuses components across attacks"), Identities.Num() < 1200); }
		if (Mode)
		{
			// Spectating a server-AI pawn is locally viewed even without owning it.
			auto* Viewer = World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, Params);
			if (TestNotNull(TEXT("Concrete local view fixture controller spawns"), Viewer))
			{
				Viewer->SetAsLocalPlayerController();
				auto* ViewedPawn = CastChecked<APawn>(Components[0]->GetOwner());
				Viewer->SetViewTarget(ViewedPawn);
				TestTrue(TEXT("Normal view target establishes Niagara locally-viewed policy"), ViewedPawn->IsLocallyViewed());
				for (const EProject_JCombatVFXAttachmentTarget Target : {EProject_JCombatVFXAttachmentTarget::World, EProject_JCombatVFXAttachmentTarget::CharacterMesh})
				{
					Profile->Cues[0].AttachmentTarget = Target;
					Components[0]->BeginAttackPresentation(CrowdAttackTag); Components[0]->PlayCue(CrowdCueTag);
					TStrongObjectPtr<UNiagaraComponent> ViewedCue(Components[0]->ActiveLoopingCues.FindRef(CrowdCueTag).Get());
					if (TestNotNull(TEXT("Locally viewed world and attached cues spawn"), ViewedCue.Get()))
					{
						TestEqual(TEXT("Only world cues force the captured local linkage"), ViewedCue->GetForceLocalPlayerEffect(), Target == EProject_JCombatVFXAttachmentTarget::World);
						TestTrue(TEXT("Engine recognizes local world and attached effects"), ViewedCue->IsLocalPlayerEffect());
						if (Target == EProject_JCombatVFXAttachmentTarget::CharacterMesh)
						{
							TestTrue(TEXT("View-change boundary observes an actually active attached cue"), ViewedCue->IsActive());
							Viewer->SetViewTarget(Viewer);
							TestFalse(TEXT("Live attached cue loses local linkage after normal camera target change"), ViewedCue->IsLocalPlayerEffect());
							TestEqual(TEXT("View change preserves the same tracked attached cue"), Components[0]->ActiveLoopingCues.FindRef(CrowdCueTag).Get(), ViewedCue.Get());
							Viewer->SetViewTarget(ViewedPawn);
							TestTrue(TEXT("The same attached cue follows the restored local view"), ViewedCue->IsLocalPlayerEffect());
						}
						Components[0]->StopCue(CrowdCueTag);
						TestEqual(TEXT("Stopped local loop returns to the real engine pool"), ViewedCue->PoolingMethod, ENCPoolMethod::FreeInPool);
						Components[1]->BeginAttackPresentation(CrowdAttackTag); Components[1]->PlayCue(CrowdCueTag);
						auto* RemoteCue = Components[1]->ActiveLoopingCues.FindRef(CrowdCueTag).Get();
						if (TestNotNull(TEXT("Unviewed owner acquires the next pooled cue"), RemoteCue))
						{
							TestEqual(TEXT("Actual pool reuses the local component for remote playback"), RemoteCue, ViewedCue.Get());
							TestFalse(TEXT("Remote reuse clears the prior forced-local exemption"), RemoteCue->GetForceLocalPlayerEffect());
							TestFalse(TEXT("Unviewed attached/world cue follows ordinary engine scalability"), RemoteCue->IsLocalPlayerEffect());
						}
						Components[1]->StopCue(CrowdCueTag);
					}
				}
				Profile->Cues[0].AttachmentTarget = EProject_JCombatVFXAttachmentTarget::World;
#if WITH_EDITORONLY_DATA
				// Retire a real pooled controller while it is inactive, then observe
				// its next native initialization callback inside Activate(true).
				Components[0]->BeginAttackPresentation(CrowdAttackTag); Components[0]->PlayCue(CrowdCueTag);
				TStrongObjectPtr<UNiagaraComponent> CancelledCue(Components[0]->ActiveLoopingCues.FindRef(CrowdCueTag).Get());
				Components[0]->StopCue(CrowdCueTag);
				if (TestNotNull(TEXT("Activation cancellation fixture retains a real pooled component"), CancelledCue.Get()))
				{
					CancelledCue->DestroyInstance();
					bool bCancelledDuringActivation = false;
					const FDelegateHandle CancelHandle = CancelledCue->OnSystemInstanceChanged().AddLambda([C = Components[0], &bCancelledDuringActivation]()
					{
						if (bCancelledDuringActivation) { return; }
						bCancelledDuringActivation = true; C->RefreshPresentation(); CollectGarbage(RF_NoFlags, true);
					});
					Components[0]->BeginAttackPresentation(CrowdAttackTag); Components[0]->PlayCue(CrowdCueTag);
					CancelledCue->OnSystemInstanceChanged().Remove(CancelHandle);
					TestTrue(TEXT("Real activation callback cancels the captured presentation generation"), bCancelledDuringActivation);
					TestEqual(TEXT("Cancelled activation cannot publish its stale looping cue"), Components[0]->ActiveLoopingCues.Num(), 0);
					TestEqual(TEXT("Selection refresh removes the old desired loop"), Components[0]->PendingLoopingCues.Num(), 0);
					TestFalse(TEXT("Cancelled generation does not retain its started-cue claim"), Components[0]->StartedCueTags.HasTagExact(CrowdCueTag));
				}
#endif
				Viewer->SetViewTarget(Viewer);
				TestFalse(TEXT("Ending the local view retires the pawn's view state"), ViewedPawn->IsLocallyViewed());
			}
		}
		// Exercise graceful completion separately from immediate trail removal.
		Profile->Cues[0].bDestroyImmediatelyOnStop = false;
		Components[0]->BeginAttackPresentation(CrowdAttackTag); Components[0]->PlayCue(CrowdCueTag);
		Components[0]->EndAttackPresentation();
		TestEqual(TEXT("Graceful stop drops project ownership too"), Components[0]->ActiveLoopingCues.Num(), 0);
		TRACE_END_REGION_WITH_ID(Region);
		World->Tick(LEVELTICK_All, 1.f / 60); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
	Pool->Set(Previous, ECVF_SetByCode);
	FString Output; FParse::Value(FCommandLine::Get(), TEXT("ProjectJCrowdOutput="), Output);
	if (!Output.IsEmpty()) { IFileManager::Get().MakeDirectory(*Output, true); TestTrue(TEXT("Effect measurements saved"), FFileHelper::SaveStringToFile(CSV, *(Output / TEXT("effects-pool.csv")))); }
	AddInfo(TEXT("100 simultaneous authored GPU trails x12 rounds per mode; real RHI/world simulation, no gameplay viewport. Spawn/stop timings include contract checks, not GPU frame cost or visual-quality approval."));
	// Run after selected legacy presentation-profile migration/resave, then in a
	// fresh process, after checking other hard refs. This real RHI boundary is separate from NullRHI.
	const FSoftObjectPath ColdPath(TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_AuraFX_Basic.NS_AuraFX_Basic"));
	if (!TestNull(TEXT("Cold soft Niagara package is absent before admission"), ColdPath.ResolveObject())) { return false; }
	auto Async = MakeShared<FSoftCrowdCueWorld>(ColdPath);
	auto* ManyProfile = Async->Presentations[1]->BasePresentationSet->AttackPresentations[0].Profile.Get();
	const FProject_JCombatVFXCueDefinition BaseCue = ManyProfile->Cues[0];
	for (const FGameplayTag Tag : {CrowdCueExtra1.GetTag(), CrowdCueExtra2.GetTag(), CrowdCueExtra3.GetTag(),
		CrowdCueExtra4.GetTag(), CrowdCueExtra5.GetTag(), CrowdCueExtra6.GetTag(), CrowdCueExtra7.GetTag()})
	{ auto Cue = BaseCue; Cue.CueTag = Tag; ManyProfile->Cues.Add(Cue); }
	auto* FairProfile = Async->Presentations[2]->BasePresentationSet->AttackPresentations[0].Profile.Get();
	auto ExtraFairCue = BaseCue; ExtraFairCue.CueTag = CrowdCueExtra1; FairProfile->Cues.Add(ExtraFairCue);
	for (int32 Owner = 0; Owner < 3; ++Owner)
	{
		auto* C = Async->Presentations[Owner]; C->BeginAttackPresentation(CrowdAttackTag);
		for (const auto& Cue : C->BasePresentationSet->AttackPresentations[0].Profile->Cues) { C->PlayCue(Cue.CueTag); }
	}
	// A real weapon consumer uses the same FIFO and must still reach native apply.
	auto* WeaponOwner = CastChecked<AProject_JNPCCharacter>(Async->Presentations[0]->GetOwner());
	WeaponOwner->GetMesh()->SetSkeletalMesh(LoadObject<USkeletalMesh>(nullptr,
		TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
	Async->InterleavedWeapon.Reset(NewObject<UProject_JWeaponPresentationComponent>(WeaponOwner));
	WeaponOwner->AddInstanceComponent(Async->InterleavedWeapon.Get()); Async->InterleavedWeapon->RegisterComponent();
	auto* WeaponProfile = NewObject<UProject_JWeaponPresentationProfile>(Async->InterleavedWeapon.Get());
	WeaponProfile->WeaponActorClass = ACameraActor::StaticClass();
	WeaponProfile->DrawnSocketName = WeaponProfile->SheathedSocketName = TEXT("root");
	WeaponProfile->bPreferVisualFollowerSockets = false; Async->InterleavedWeapon->OwnerPresentationProfile = WeaponProfile;
	TestTrue(TEXT("Real current initial weapon request joins shared FIFO"), Async->Budget->Request(Async->InterleavedWeapon.Get(), 0));
	Async->Presentations[3]->BeginAttackPresentation(CrowdAttackTag); Async->Presentations[3]->PlayCue(CrowdCueTag);
	FActorSpawnParameters ViewSpawn; ViewSpawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	auto* Viewer = Async->World->SpawnActor<APlayerController>(APlayerController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, ViewSpawn);
	if (!TestNotNull(TEXT("Cold local-view completion fixture"), Viewer)) { return false; }
	Viewer->SetAsLocalPlayerController(); Viewer->SetViewTarget(Async->Presentations[4]->GetOwner());
	TestTrue(TEXT("The actual view target is locally viewed before cold completion"), CastChecked<APawn>(Async->Presentations[4]->GetOwner())->IsLocallyViewed());
	Async->Presentations[4]->BeginAttackPresentation(CrowdAttackTag); Async->Presentations[4]->PlayCue(CrowdCueTag);
	TestEqual(TEXT("Five owners still share one physical cold load"), Async->Assets->GetGroupCount(), 1);
	TestEqual(TEXT("Each owner/path retains one bounded lease"), Async->Assets->GetLeaseCount(), 5);
	TestEqual(TEXT("No cold Niagara spawn on Request"), Async->Presentations[1]->ActiveLoopingCues.Num(), 0);
	Async->Presentations[0]->StopCue(CrowdCueTag);
	// This owner now receives an instantaneous cue while the package is missing.
	auto* FirstProfile = Async->Presentations[0]->BasePresentationSet->AttackPresentations[0].Profile.Get();
	FirstProfile->Cues[0].bLooping = false;
	Async->Presentations[0]->PlayCue(CrowdCueTag);
	const double Began = FPlatformTime::Seconds();
	auto WaitForColdCues = [this, Async, Began]()
	{
		if (FPlatformTime::Seconds() - Began > 30) { AddError(TEXT("Cold Niagara application timed out")); return true; }
		// Drive actual loader completion before any fixture World Tick can apply the
		// presentation queue. The application's outer engine still advances frames.
		if (!Async->bColdSetup) { Async->Assets->Tick(0); }
		if (Async->Assets->GetStats().Delivered < 5) { return false; }
		auto* First = Async->Presentations[0]; auto* Current = Async->Presentations[1];
		auto* Fair = Async->Presentations[2]; auto* Rejected = Async->Presentations[3];
		auto* Local = Async->Presentations[4];
		if (!Async->bColdSetup)
		{
		TestEqual(TEXT("One loader completion cannot drain eight same-system remote loops"), Current->ActiveLoopingCues.Num(), 0);
		TestEqual(TEXT("Distinct tags sharing one system retain eight pending visual meanings"), Current->PendingLoopingCues.Num(), 8);
		TestEqual(TEXT("Local-view cold completion preserves immediate visual timing"), Local->ActiveLoopingCues.Num(), 1);
		const int32 AdmittedOwners = Async->Budget->GetStats().Pending;
		Current->ApplyReplicatedState();
		TestEqual(TEXT("Duplicate same-attack recovery cannot bypass a deferred ready admission"), Current->ActiveLoopingCues.Num(), 0);
		TestEqual(TEXT("Recovery leaves all distinct deferred descriptors owned"), Current->PendingLoopingCues.Num(), 8);
		TestEqual(TEXT("Recovery coalesces through the existing admission without adding owners"), Async->Budget->GetStats().Pending, AdmittedOwners);
		TestEqual(TEXT("The later saturation target first receives real cold readiness"), Rejected->PendingLoopingCues.Num(), 1);
		// Admission saturation is a separate deterministic phase after all loader
		// callbacks. LeaseCursorToken may rotate; no completion order is assumed.
		Async->Budget->Cancel(Rejected);
		auto* RejectedProfile = Rejected->BasePresentationSet->AttackPresentations[0].Profile.Get();
		const FSoftObjectPath UndeliveredPath(TEXT("/Engine/BasicShapes/Cube.Cube"));
		auto UndeliveredCue = RejectedProfile->Cues[0]; UndeliveredCue.CueTag = CrowdCueExtra2;
		UndeliveredCue.NiagaraSystem = TSoftObjectPtr<UNiagaraSystem>(UndeliveredPath); RejectedProfile->Cues.Add(UndeliveredCue);
		Rejected->PlayCue(CrowdCueExtra2); // Real pending demand; no loader Tick delivers this separate path yet.
		TestTrue(TEXT("A distinct undelivered path joins the same current attack"), Rejected->PendingLoopingCues.Contains(CrowdCueExtra2));
		const int32 Remaining = UProject_JPresentationBudgetSubsystem::MaxPending - Async->Budget->GetStats().Pending;
		for (int32 Index = 0; Index < Remaining; ++Index)
		{
			auto* Holder = NewObject<UProject_JWeaponPresentationComponent>(First->GetOwner());
			Async->Capacity.Emplace(Holder);
			if (!TestTrue(TEXT("Actual typed queue admits a capacity holder"), Async->Budget->Request(Holder, 1))) { return true; }
		}
		TestEqual(TEXT("Actual shared queue reaches its existing capacity before readmission"), Async->Budget->GetStats().Pending, UProject_JPresentationBudgetSubsystem::MaxPending);
		Rejected->QueueReadyLoopApplications();
		TestEqual(TEXT("Rejected readiness closes only its ready desire and retains the undelivered path"), Rejected->PendingLoopingCues.Num(), 1);
		const auto* Undelivered = Rejected->PendingLoopingCues.Find(CrowdCueExtra2);
		TestTrue(TEXT("Admission failure preserves the exact not-yet-ready path and incarnation"),
			Undelivered && Undelivered->SystemPath == UndeliveredPath && Undelivered->AttackInstance == Rejected->ActiveAttackInstance && !Undelivered->bReadyForBudget);
		TestEqual(TEXT("Rejected readiness never synchronously falls back to Niagara spawn"), Rejected->ActiveLoopingCues.Num(), 0);
		TestEqual(TEXT("Rejected ready-loop count is observed on its real consumer"), Rejected->RejectedReadyLoops, uint64(1));
		TestTrue(TEXT("Shared queue rejection remains observable"), Async->Budget->GetStats().Rejected > 0);
		Rejected->ApplyReplicatedState();
		TestEqual(TEXT("Same-attack full recovery cannot replay a rejected ready cosmetic as warm"), Rejected->ActiveLoopingCues.Num(), 0);
		TestTrue(TEXT("The rejected tag remains consumed for this attack incarnation"), Rejected->StartedCueTags.HasTagExact(CrowdCueTag));
		Rejected->StopCue(CrowdCueTag);
		TestTrue(TEXT("Stopping a consumed attempt cannot refund its admission"), Rejected->StartedCueTags.HasTagExact(CrowdCueTag));
		for (const auto& Holder : Async->Capacity) { Async->Budget->Cancel(Holder.Get()); }
		Async->Capacity.Reset(); Async->CapacityQueuedFrame = GFrameCounter;
		Current->StopCue(CrowdCueExtra7);
		TestEqual(TEXT("Matching stop before budget apply removes just one deferred tag"), Current->PendingLoopingCues.Num(), 7);
		RejectedProfile->Cues[0].bUseRemoteSpawnBudget = false; // Existing recovery/pool ownership check explicitly isolates immediate timing.
		Rejected->BeginAttackPresentation(CrowdAttackTag); Rejected->PlayCue(CrowdCueTag);
		TestEqual(TEXT("An authored exact-timing warm exception remains immediate"), Rejected->ActiveLoopingCues.Num(), 1);
		TestFalse(TEXT("A new attack clears the old rejection's consumed mark"), Rejected->StartedCueTags.HasTagExact(CrowdCueTag));
		Async->bColdSetup = true;
		}
		if (Async->Budget->IsTickable())
		{
			if (Async->LastApplyFrame == GFrameCounter) { return false; }
			Async->LastApplyFrame = GFrameCounter;
			if (++Async->ApplyTicks >= 256) { AddError(TEXT("Bounded natural-frame cold queue drain did not complete")); return true; }
			const uint64 BeforeApply = Async->Budget->GetStats().Applied;
			Async->Budget->Tick(0);
			TestTrue(TEXT("Both typed consumers and tail continuation share one real-frame quota"),
				Async->Budget->GetStats().Applied - BeforeApply <= UProject_JPresentationBudgetSubsystem::MaxApplications
				&& Async->Budget->GetStats().FrameApplications <= UProject_JPresentationBudgetSubsystem::MaxApplications);
			if (!Async->bReplacedQueuedOwner && Fair->ActiveLoopingCues.Num() > 0 && !Fair->PendingLoopingCues.IsEmpty())
			{
				TestTrue(TEXT("A second owner progresses before the many-cue owner drains"), Current->ActiveLoopingCues.Num() < 7);
				const uint32 OldRevision = Fair->VisualAssetRevision;
				TStrongObjectPtr<UNiagaraSystem> ReplacementSystem(LoadObject<UNiagaraSystem>(nullptr,
					TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic")));
				if (!TestNotNull(TEXT("Existing trail is a distinct real replacement path"), ReplacementSystem.Get())) { return true; }
				Fair->BasePresentationSet->AttackPresentations[0].Profile->Cues[0].NiagaraSystem = ReplacementSystem.Get();
				Fair->BasePresentationSet->AttackPresentations[0].Profile->Cues[0].bUseRemoteSpawnBudget = false;
				Fair->BeginAttackPresentation(CrowdAttackTag); Fair->RefreshPresentation(); Fair->PlayCue(CrowdCueTag);
				Fair->ApplyBudgetedReadyLoop(OldRevision);
				TestEqual(TEXT("Old queued apply cannot replace the new attack/style incarnation"), Fair->ActiveLoopingCues.Num(), 1);
				if (auto* Replacement = Fair->ActiveLoopingCues.FindRef(CrowdCueTag).Get())
				{ TestTrue(TEXT("Old ready-path apply cannot substitute its retired system"), Replacement->GetAsset() == ReplacementSystem.Get()); }
				TestEqual(TEXT("Replacement leaves no orphaned deferred desire"), Fair->PendingLoopingCues.Num(), 0);
				Async->bReplacedQueuedOwner = true;
			}
			return false; // The next apply pass must receive a naturally advanced GFrameCounter.
		}
		TestTrue(TEXT("Second-owner FIFO progress and queued-generation replacement were exercised"), Async->bReplacedQueuedOwner);
		TestTrue(TEXT("Cancelled capacity tombstones retain bounded real-frame visits and eventually drain"),
			GFrameCounter-Async->CapacityQueuedFrame >= 32 && GFrameCounter-Async->CapacityQueuedFrame < 256 && Async->ApplyTicks < 256);
		TestEqual(TEXT("Deferred request and continuation queue drains"), Async->Budget->GetStats().Pending, 0);
		TestNotNull(TEXT("The interleaved actual weapon consumer reaches native creation"), Async->InterleavedWeapon->GetSpawnedWeapon());
		TestFalse(TEXT("Stopped deferred tag never appears after continuation"), Current->ActiveLoopingCues.Contains(CrowdCueExtra7));
		auto* ReadyNiagara = Current->ActiveLoopingCues.FindRef(CrowdCueTag).Get();
		if (!TestNotNull(TEXT("Actual deferred loop creates a Niagara component"), ReadyNiagara)) { return true; }
		TestEqual(TEXT("Stopped cold loop does not appear after load"), First->ActiveLoopingCues.Num(), 0);
		TestTrue(TEXT("Missed one-shot remains consumed after readiness"), First->StartedCueTags.HasTagExact(CrowdCueTag));
		TestEqual(TEXT("All seven surviving distinct desired loops start after real async load"), Current->ActiveLoopingCues.Num(), 7);
		int32 ActiveNiagara = 0;
		for (TObjectIterator<UNiagaraComponent> It; It; ++It)
		{
			if (It->GetWorld() == Async->World && It->IsActive()) { ++ActiveNiagara; }
		}
		TestEqual(TEXT("Seven deferred plus fair/local/warm loops exclude the missed one-shot"), ActiveNiagara, 10);
		First->PlayCue(CrowdCueTag);
		TestEqual(TEXT("Prediction/multicast duplicate keeps missed one-shot consumed"), First->ActiveLoopingCues.Num(), 0);
#if WITH_EDITORONLY_DATA
		// Preserve this native stop/reentrant GC test's original exact-timing purpose.
		Current->BasePresentationSet->AttackPresentations[0].Profile->Cues[0].bUseRemoteSpawnBudget = false;
		TStrongObjectPtr<UNiagaraComponent> Niagara(Current->ActiveLoopingCues.FindRef(CrowdCueTag).Get());
		if (TestNotNull(TEXT("Current loop has a real Niagara component"), Niagara.Get()))
		{
			const uint32 OldInstance = Current->ActiveAttackInstance;
			bool bReentered = false;
			const FDelegateHandle Handle = Niagara->OnSystemInstanceChanged().AddLambda([Current, &bReentered]()
			{
				if (bReentered) { return; } bReentered = true;
				Current->BeginAttackPresentation(CrowdAttackTag); Current->PlayCue(CrowdCueTag);
				CollectGarbage(RF_NoFlags, true);
			});
			Current->StopCue(CrowdCueTag);
			Niagara->OnSystemInstanceChanged().Remove(Handle);
			TestTrue(TEXT("Actual Niagara stop callback reentered and collected garbage"), bReentered);
			TestTrue(TEXT("Callback starts a distinct same-tag attack"), Current->ActiveAttackInstance != OldInstance);
			TestTrue(TEXT("Old Stop cannot erase new same-tag loop recovery"), Current->ReplicatedPresentationState.ActiveLoopingCueTags.HasTagExact(CrowdCueTag));
			TestEqual(TEXT("New same-tag loop retains current local ownership"), Current->ActiveLoopingCues.Num(), 1);
			TestEqual(TEXT("Callback reentry leaves no pending loop"), Current->PendingLoopingCues.Num(), 0);
			Current->EndAttackPresentation();
			TestEqual(TEXT("Current attack still retires its own loop"), Current->ActiveLoopingCues.Num(), 0);
		}
#else
		Current->StopCue(CrowdCueTag);
#endif
		for (auto* Presentation : Async->Presentations) { Presentation->DestroyComponent(); }
		Async->InterleavedWeapon->DestroyComponent();
		TestEqual(TEXT("Retirement releases all component asset leases"), Async->Assets->GetLeaseCount(), 0);
		TestEqual(TEXT("Retirement leaves no queued ready-loop or weapon owner"), Async->Budget->GetStats().Pending, 0);
		return true;
	};
	ADD_LATENT_AUTOMATION_COMMAND(FSoftCrowdCueCommand(MoveTemp(WaitForColdCues)));
	struct FWarmBudgetState
	{
		TSharedPtr<FSoftCrowdCueWorld> Case;
		TStrongObjectPtr<UNiagaraSystem> OneShotSystem, LoopSystem;
		TWeakObjectPtr<UNiagaraComponent> LocalLoop;
		uint64 LastFrame = MAX_uint64;
		int32 Stage = 0;
		double Began = 0;
	};
	auto Warm = MakeShared<FWarmBudgetState>();
	auto CheckWarmBudget = [this,Warm]()
	{
		if (!Warm->Case)
		{
			Warm->Began = FPlatformTime::Seconds();
			Warm->OneShotSystem.Reset(LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_Hit_Basic_Once.NS_Hit_Basic_Once")));
			if (!TestNotNull(TEXT("Warm one-shot uses an actual finite authored Niagara system"),Warm->OneShotSystem.Get())) { return true; }
			Warm->LoopSystem.Reset(LoadObject<UNiagaraSystem>(nullptr,TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic.NS_SlashTrail_Basic")));
			if (!TestNotNull(TEXT("Warm loop fixture pins the actual resident payload before cue admission"),Warm->LoopSystem.Get())) { return true; }
			Warm->Case = MakeShared<FSoftCrowdCueWorld>(FSoftObjectPath(Warm->LoopSystem->GetPathName()));
			auto& S = *Warm->Case;
			for (auto* C : S.Presentations) { C->BeginAttackPresentation(CrowdAttackTag); C->PlayCue(CrowdCueTag); }
			TestEqual(TEXT("Five warm default remote loops join the real shared queue"),S.Budget->GetStats().Pending,5);
			for (auto* C : S.Presentations) { TestEqual(TEXT("Warm readiness alone cannot create a remote loop"),C->ActiveLoopingCues.Num(),0); }
			auto* First = S.Presentations[0]; auto* Profile = First->BasePresentationSet->AttackPresentations[0].Profile.Get();
			auto Shot = Profile->Cues[0]; Shot.CueTag = CrowdCueExtra7; Shot.NiagaraSystem = Warm->OneShotSystem.Get(); Shot.bLooping = false; Profile->Cues.Add(Shot);
			const uint64 RejectedBefore = S.Budget->GetStats().ImmediateRejected;
			First->PlayCue(CrowdCueExtra7);
			TestEqual(TEXT("Live queued owners take precedence over a new remote one-shot"),S.Budget->GetStats().ImmediateRejected,RejectedBefore+1);
			TestTrue(TEXT("A skipped one-shot consumes this attack's instant"),First->StartedCueTags.HasTagExact(CrowdCueExtra7));
			First->ApplyReplicatedState(); TestEqual(TEXT("Warm recovery cannot bypass or duplicate pending admission"),S.Budget->GetStats().Pending,5);
			S.Presentations[4]->StopCue(CrowdCueTag);
			TestEqual(TEXT("Stop before warm apply removes that exact owner request"),S.Budget->GetStats().Pending,4);
			TestEqual(TEXT("Stopped warm intent is detached before any spawn"),S.Presentations[4]->PendingLoopingCues.Num(),0);
			FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			auto* Viewer = S.World->SpawnActor<APlayerController>(Spawn);
			if (!TestNotNull(TEXT("Warm local-view fixture controller"),Viewer)) { return true; }
			Viewer->SetAsLocalPlayerController(); Viewer->SetViewTarget(S.Presentations[4]->GetOwner());
			const int32 CreditsBeforeLocal = S.Budget->GetStats().FrameApplications;
			S.Presentations[4]->BeginAttackPresentation(CrowdAttackTag); S.Presentations[4]->PlayCue(CrowdCueTag);
			Warm->LocalLoop = S.Presentations[4]->ActiveLoopingCues.FindRef(CrowdCueTag).Get();
			TestTrue(TEXT("Default locally viewed warm cue remains immediate despite remote backlog"),Warm->LocalLoop.IsValid());
			TestEqual(TEXT("Local timing does not consume remote attempt credits"),S.Budget->GetStats().FrameApplications,CreditsBeforeLocal);
			auto* LocalProfile = S.Presentations[4]->BasePresentationSet->AttackPresentations[0].Profile.Get();
			auto LocalShot = LocalProfile->Cues[0]; LocalShot.CueTag = CrowdCueExtra7; LocalShot.NiagaraSystem = Warm->OneShotSystem.Get(); LocalShot.bLooping = false; LocalProfile->Cues.Add(LocalShot);
			int32 LocalShotsBefore = 0, LocalShotsAfter = 0;
			for (TObjectIterator<UNiagaraComponent> It; It; ++It) { if (It->GetWorld() == S.World && !It->IsBeingDestroyed() && It->GetAsset() == Warm->OneShotSystem.Get()) { ++LocalShotsBefore; } }
			S.Presentations[4]->PlayCue(CrowdCueExtra7);
			for (TObjectIterator<UNiagaraComponent> It; It; ++It) { if (It->GetWorld() == S.World && !It->IsBeingDestroyed() && It->GetAsset() == Warm->OneShotSystem.Get()) { ++LocalShotsAfter; } }
			TestEqual(TEXT("Locally viewed default one-shot remains immediate despite the remote FIFO"),LocalShotsAfter-LocalShotsBefore,1);
			TestEqual(TEXT("Local one-shot does not borrow a remote frame credit"),S.Budget->GetStats().FrameApplications,CreditsBeforeLocal);
			Warm->Stage = 1;
		}
		if (FPlatformTime::Seconds()-Warm->Began > 30) { AddError(TEXT("Warm presentation natural-frame regression timed out")); return true; }
		auto& S = *Warm->Case;
		if (Warm->LastFrame == GFrameCounter) { return false; }
		Warm->LastFrame = GFrameCounter;
		const auto CountOneShots = [&]()
		{
			int32 Count = 0;
			for (TObjectIterator<UNiagaraComponent> It; It; ++It)
				if (It->GetWorld() == S.World && !It->IsBeingDestroyed() && It->GetAsset() == Warm->OneShotSystem.Get()) { ++Count; }
			return Count;
		};
		const auto AddShots = [&](UProject_JCombatPresentationComponent* C)
		{
			auto* Profile = C->BasePresentationSet->AttackPresentations[0].Profile.Get();
			for (FGameplayTag Tag : {CrowdCueExtra1.GetTag(),CrowdCueExtra2.GetTag(),CrowdCueExtra3.GetTag(),CrowdCueExtra4.GetTag(),CrowdCueExtra5.GetTag()})
			{ auto Shot = Profile->Cues[0]; Shot.CueTag = Tag; Shot.NiagaraSystem = Warm->OneShotSystem.Get(); Shot.bLooping = false; Profile->Cues.Add(Shot); }
		};
		if (Warm->Stage == 1)
		{
			S.Budget->Tick(0); S.Budget->Tick(0);
			TestTrue(TEXT("Sequential Tick calls share one native frame's four apply attempts"),S.Budget->GetStats().FrameApplications <= S.Budget->MaxApplications);
			if (S.Budget->GetStats().Pending) { return false; }
			for (int32 Index=0; Index<4; ++Index) { TestEqual(TEXT("All surviving warm default intents eventually spawn on real frame progress"),S.Presentations[Index]->ActiveLoopingCues.Num(),1); }
			TestEqual(TEXT("Cancelled old warm request cannot replace the new locally viewed generation"),S.Presentations[4]->ActiveLoopingCues.FindRef(CrowdCueTag).Get(),Warm->LocalLoop.Get());
			for (auto* C : S.Presentations) { C->StopCue(CrowdCueTag); }
			Warm->Stage = 2; return false;
		}
		if (Warm->Stage == 2)
		{
			auto* First = S.Presentations[0]; AddShots(First);
			const uint64 Before = S.Budget->GetStats().ImmediateApplications;
			const int32 PhysicalBefore = CountOneShots();
			for (FGameplayTag Tag : {CrowdCueExtra1.GetTag(),CrowdCueExtra2.GetTag(),CrowdCueExtra3.GetTag()}) { First->PlayCue(Tag); }
			TestEqual(TEXT("Three admitted warm one-shots use three real Niagara components"),CountOneShots()-PhysicalBefore,3);
			S.Presentations[1]->PlayCue(CrowdCueTag); // A fourth, queued consumer shares this frame.
			First->PlayCue(CrowdCueExtra4);
			TestEqual(TEXT("One-shot cannot steal the fourth credit from an existing queued loop"),S.Budget->GetStats().ImmediateApplications-Before,uint64(3));
			S.Budget->Tick(0); S.Budget->Tick(0);
			TestEqual(TEXT("Three immediate shots plus one queued loop consume exactly four shared frame attempts"),S.Budget->GetStats().FrameApplications,4);
			TestEqual(TEXT("The queued warm loop reaches actual native creation"),S.Presentations[1]->ActiveLoopingCues.Num(),1);
			First->PlayCue(CrowdCueExtra5); TestTrue(TEXT("Exhausted frame consumes the fifth instant instead of queuing it"),First->StartedCueTags.HasTagExact(CrowdCueExtra5));
			TestEqual(TEXT("Repeated Tick cannot replenish one-shot allowance"),S.Budget->GetStats().ImmediateApplications-Before,uint64(3));
			Warm->Stage = 3; return false;
		}
		if (Warm->Stage == 3)
		{
			auto* C = S.Presentations[2]; AddShots(C);
			const uint64 Before = S.Budget->GetStats().ImmediateApplications; const int32 PhysicalBefore = CountOneShots();
			for (FGameplayTag Tag : {CrowdCueExtra1.GetTag(),CrowdCueExtra2.GetTag(),CrowdCueExtra3.GetTag(),CrowdCueExtra4.GetTag(),CrowdCueExtra5.GetTag()}) { C->PlayCue(Tag); }
			TestEqual(TEXT("A new real frame admits four remote one-shot attempts"),S.Budget->GetStats().ImmediateApplications-Before,uint64(4));
			TestEqual(TEXT("Fifth same-frame one-shot creates no additional Niagara component"),CountOneShots()-PhysicalBefore,4);
			TestEqual(TEXT("Immediate-only path shares the same hard frame cap"),S.Budget->GetStats().FrameApplications,4);
			S.Budget->Tick(0); Warm->Stage = 4; return false;
		}
		const uint64 BeforeReplay = S.Budget->GetStats().ImmediateApplications; const int32 PhysicalBeforeReplay = CountOneShots();
		S.Presentations[0]->PlayCue(CrowdCueExtra7); S.Presentations[0]->PlayCue(CrowdCueExtra4); S.Presentations[0]->PlayCue(CrowdCueExtra5); S.Presentations[2]->PlayCue(CrowdCueExtra5);
		TestEqual(TEXT("Later frame cannot replay skipped or duplicate same-attack one-shots"),S.Budget->GetStats().ImmediateApplications,BeforeReplay);
		TestEqual(TEXT("Skipped instants never create late Niagara components"),CountOneShots(),PhysicalBeforeReplay);
		TestEqual(TEXT("Shared attempt peak never exceeds the authored existing world cap"),S.Budget->GetStats().PeakFrameApplications,4);
		// Current saved presentation consumes the project-owned active-policy system.
		// Earlier98 evidence separately verified the old tagged-struct true default.
		TStrongObjectPtr<UProject_JAttackPresentationProfile> Authored(LoadObject<UProject_JAttackPresentationProfile>(nullptr,
			TEXT("/Game/DataAssetSets/Presentation/Spellblade/DA_AP_Spellblade/DA_AP_Spellblade.DA_AP_Spellblade")));
		TStrongObjectPtr<UProject_JCombatPresentationSet> AuthoredSet(LoadObject<UProject_JCombatPresentationSet>(nullptr,
			TEXT("/Game/DataAssetSets/Presentation/Spellblade/DA_CPS_Spellblade_Base.DA_CPS_Spellblade_Base")));
		if (!TestNotNull(TEXT("Actual migrated Spellblade presentation profile loads"),Authored.Get())
			|| !TestNotNull(TEXT("Actual migrated Spellblade presentation set loads"),AuthoredSet.Get())) { return true; }
		const bool ProfileDirtyBefore = Authored->GetOutermost()->IsDirty(), SetDirtyBefore = AuthoredSet->GetOutermost()->IsDirty();
		TStrongObjectPtr<UNiagaraSystem> Owned(LoadObject<UNiagaraSystem>(nullptr,OwnedTrailSystemPath));
		TStrongObjectPtr<UNiagaraEffectType> OwnedType(LoadObject<UNiagaraEffectType>(nullptr,OwnedTrailTypePath));
		if (!TestNotNull(TEXT("Actual saved project combat trail system loads"),Owned.Get())
			|| !TestNotNull(TEXT("Actual saved project combat trail EffectType loads"),OwnedType.Get())) { return true; }
		const bool SystemDirtyBefore=Owned->GetOutermost()->IsDirty(), TypeDirtyBefore=OwnedType->GetOutermost()->IsDirty();
		TestTrue(TEXT("Saved project trail keeps fixed count, High resume, distance significance, local protection and original bounds"),
			HasOwnedTrailPolicy(Owned.Get(),OwnedType.Get(),Warm->LoopSystem.Get()));
		const FGameplayTag TrailTag = FGameplayTag::RequestGameplayTag(TEXT("PresentationCue.Combat.Trail"));
		const auto* AuthoredCue = Authored->FindCue(TrailTag);
		if (TestNotNull(TEXT("Actual Spellblade trail retains its semantic cue identity"),AuthoredCue))
		{
			TestTrue(TEXT("Saved Spellblade cue keeps remote spawn budgeting enabled"),AuthoredCue->bUseRemoteSpawnBudget);
			TestEqual(TEXT("Actual saved trail consumes the exact project-owned Niagara path"),AuthoredCue->NiagaraSystem.ToSoftObjectPath(),FSoftObjectPath(OwnedTrailSystemPath));
			TestTrue(TEXT("Actual saved trail preserves its notify-owned looping contract"),AuthoredCue->bLooping);
			if (AuthoredCue->bLooping)
			{
				FGameplayTag AuthoredAttack;
				for (const auto& Entry : AuthoredSet->AttackPresentations) { if (Entry.Profile == Authored.Get()) { AuthoredAttack = Entry.AttackTag; break; } }
				if (TestTrue(TEXT("Actual set resolves the same authored trail profile through a valid attack"),AuthoredAttack.IsValid()))
				{
					auto* C = S.Presentations[3]; C->BasePresentationSet = AuthoredSet.Get();
					C->BeginAttackPresentation(AuthoredAttack); C->PlayCue(TrailTag);
					TestEqual(TEXT("Actual saved default cue enters the production remote queue"),S.Budget->GetStats().Pending,1);
					TestEqual(TEXT("Actual saved warm cue cannot spawn before its admitted apply"),C->ActiveLoopingCues.Num(),0);
					C->StopCue(TrailTag);
				}
			}
		}
		TestEqual(TEXT("Actual profile read/consumer exercise never dirties the authored asset"),Authored->GetOutermost()->IsDirty(),ProfileDirtyBefore);
		TestEqual(TEXT("Actual set read/consumer exercise never dirties the authored asset"),AuthoredSet->GetOutermost()->IsDirty(),SetDirtyBefore);
		TestEqual(TEXT("Project Niagara consumer/metadata read never dirties its saved system"),Owned->GetOutermost()->IsDirty(),SystemDirtyBefore);
		TestEqual(TEXT("Project Niagara consumer/metadata read never dirties its saved EffectType"),OwnedType->GetOutermost()->IsDirty(),TypeDirtyBefore);
		for (auto* C : S.Presentations) { C->DestroyComponent(); }
		TestEqual(TEXT("Warm retirement releases shared queue and visual residency ownership"),S.Budget->GetStats().Pending,0);
		TestEqual(TEXT("Warm retirement leaves no visual leases"),S.Assets->GetLeaseCount(),0);
		Warm->Case.Reset(); return true;
	};
	ADD_LATENT_AUTOMATION_COMMAND(FSoftCrowdCueCommand(MoveTemp(CheckWarmBudget)));
	return true;
}
#endif
