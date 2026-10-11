#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "PoseSearch/PoseSearchFeatureChannel_Trajectory.h"
#include "PoseSearch/PoseSearchFeatureChannel_Velocity.h"
#include "PoseSearch/PoseSearchNormalizationSet.h"
#include "PoseSearch/PoseSearchDerivedData.h"
#include "UObject/StrongObjectPtr.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Components/BoxComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "HAL/FileManager.h"

namespace
{
class FStrafeCameraPlayback : public IAutomationLatentCommand
{
	FAutomationTestBase* Test;
	UWorld* World = nullptr;
	AProject_JPlayerCharacter* Player = nullptr;
	APlayerController* Controller = nullptr;
	UPoseSearchDatabase* AuditDatabase = nullptr;
	EPoseSearchMode OriginalSearchMode = EPoseSearchMode::PCAKDTree;
	TArray<TPair<int32, float>> OriginalWeights;
	TMap<FName, float> NonLoopIndexedEnds;
	bool bPacketCamera = false;
	float CoverageBlendTime = .20f, CoverageMargin = .23f;
	int32 Reselections = 0, ShortSegments = 0, TailEntries = 0, LastJumpFrame = -1;
	TStrongObjectPtr<UProject_JMotionMatchingAssetSet> SchemaTrialSet;
	UProject_JCombatAnimProfile* TrialProfile = nullptr;
	UProject_JMotionMatchingAssetSet* OriginalProfileSet = nullptr;
	bool bValidateInstalled = false;
	bool bVerifyForwardCurve = false;
	bool bVerifyCorrection = false;
	bool bVerifyRapidTurn = false;
	int32 AcuteFrames = 0, AcuteSelectedFrames = 0, FalseStopFrames = 0, BrakingTurnFrames = 0;
	int32 PreparedFrames = 0, PreparedEnterFrames = 0;
	float MinimumTurnSpeed = TNumericLimits<float>::Max();
	int32 Case = 0, Frame = 0, GroundFrames = 0, MovingFrames = 0, ArcFrames = 0;
	int32 GeneralFrames = 0, SelectedGeneralFrames = 0, LateGeneralFrames = 0;
	float AccumulatedCameraYaw = 0;
	FString Report = TEXT("Actual CMC on a transient collision floor; camera-relative A/S/D, no asset writes. NullRHI does not certify rendered foot contact.\n");
	void Cleanup()
	{
		if (AuditDatabase)
		{
			auto& Index = const_cast<UE::PoseSearch::FSearchIndex&>(AuditDatabase->GetSearchIndex());
			for (const auto& Weight : OriginalWeights) Index.WeightsSqrt[Weight.Key] = Weight.Value;
			OriginalWeights.Reset();
			AuditDatabase->PoseSearchMode = OriginalSearchMode;
			AuditDatabase = nullptr;
		}
		if (TrialProfile)
		{
			TrialProfile->CombatStrafeMotionMatchingAssetSet = OriginalProfileSet;
			TrialProfile = nullptr;
		}
		if (!World) return;
		World->BeginTearingDown(); World->EndPlay(EEndPlayReason::Quit);
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
		World = nullptr; Player = nullptr; Controller = nullptr;
		NonLoopIndexedEnds.Reset();
	}
	bool PrepareSchemaTrial(const UProject_JMotionMatchingAssetSet* Source, float Multiplier)
	{
		if (SchemaTrialSet.IsValid()) return true;
		if (!Source->RunDatabases.Cycle->Schema || !Source->RunDatabases.Cycle->NormalizationSet) return false;
		SchemaTrialSet.Reset(DuplicateObject<UProject_JMotionMatchingAssetSet>(Source, GetTransientPackage()));
		auto* Schema = DuplicateObject<UPoseSearchSchema>(Source->RunDatabases.Cycle->Schema, SchemaTrialSet.Get());
		auto* Trajectory = const_cast<UPoseSearchFeatureChannel_Trajectory*>(Schema->FindFirstChannelOfType<UPoseSearchFeatureChannel_Trajectory>());
		if (!Trajectory) return false;
		TArray<FPoseSearchTrajectorySample> Split;
		const int32 PositionFlags = int32(EPoseSearchTrajectoryFlags::Position | EPoseSearchTrajectoryFlags::PositionXY);
		const int32 VelocityFlags = int32(EPoseSearchTrajectoryFlags::Velocity | EPoseSearchTrajectoryFlags::VelocityXY |
			EPoseSearchTrajectoryFlags::VelocityDirection | EPoseSearchTrajectoryFlags::VelocityDirectionXY);
		const int32 FacingFlags = int32(EPoseSearchTrajectoryFlags::FacingDirection | EPoseSearchTrajectoryFlags::FacingDirectionXY);
		for (const auto& Sample : Trajectory->Samples)
		{
			// Preserve dimensions, feature order, sample times and normalization groups;
			// split velocity from position/facing so only its cost changes.
			for (int32 Mask : {PositionFlags, VelocityFlags, FacingFlags})
			{
				if (!(Sample.Flags & Mask)) continue;
				auto Part = Sample; Part.Flags &= Mask;
				if (Mask == VelocityFlags) Part.Weight *= Multiplier;
				Split.Add(Part);
			}
		}
		Trajectory->Samples = Split;
		for (const auto& Sample : Split)
			Report += FString::Printf(TEXT("TrialSample Time=%.3f Flags=%d Weight=%.3f NormalizationGroup=%s\n"),
				Sample.Offset, Sample.Flags, Sample.Weight, *Sample.NormalizationGroup.ToString());
		Schema->PostEditChange();
		if (!Test->TestEqual(TEXT("Split schema preserves feature cardinality"), Schema->SchemaCardinality,
			Source->RunDatabases.Cycle->Schema->SchemaCardinality)) return false;
		auto* Normalization = DuplicateObject<UPoseSearchNormalizationSet>(Source->RunDatabases.Cycle->NormalizationSet, SchemaTrialSet.Get());
		Normalization->Databases.Reset();
		TArray<UPoseSearchDatabase*> Databases;
		for (auto* Slot : {&SchemaTrialSet->RunDatabases.Cycle, &SchemaTrialSet->RunDatabases.TurnRedirect, &SchemaTrialSet->RunDatabases.GeneralTurn})
		{
			if (!*Slot) return false;
			auto* Copy = DuplicateObject<UPoseSearchDatabase>(Slot->Get(), SchemaTrialSet.Get());
			Copy->Schema = Schema; Copy->NormalizationSet = Normalization;
			*Slot = Copy; Normalization->Databases.Add(Copy); Databases.Add(Copy);
		}
		for (auto* Database : Databases)
		{
			using namespace UE::PoseSearch;
			if (!Test->TestTrue(TEXT("Transient PCA index build completed"),
				FAsyncPoseSearchDatabasesManagement::RequestAsyncBuildIndex(Database,
					ERequestAsyncBuildFlag::NewRequest | ERequestAsyncBuildFlag::WaitForCompletion) == EAsyncBuildIndexResult::Success)) return false;
		}
		Report += FString::Printf(TEXT("TransientSchemaVelocityMultiplier=%.1f Samples=%d Cardinality=%d Databases=%d\n"),
			Multiplier, Split.Num(), Schema->SchemaCardinality, Databases.Num());
		return true;
	}
	bool Setup()
	{
		World = UWorld::CreateWorld(EWorldType::Game, false);
		GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
		auto* Floor = World->SpawnActor<AActor>();
		auto* Box = NewObject<UBoxComponent>(Floor);
		Floor->SetRootComponent(Box);
		Box->SetBoxExtent(FVector(10000, 10000, 100));
		Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		Box->SetCollisionObjectType(ECC_WorldStatic);
		Box->SetCollisionResponseToAllChannels(ECR_Block);
		Box->RegisterComponent(); Floor->SetActorLocation(FVector(0, 0, -100));
		World->InitializeActorsForPlay(FURL());
		World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
		Controller = World->SpawnActor<APlayerController>();
		Controller->SetAsLocalPlayerController();
		auto* State = World->SpawnActor<AProject_JPlayerState>();
		State->SetOwner(Controller); Controller->SetPlayerState(State);
		Player = ProjectJAuthoredAnimationFixture::Spawn(World, Controller, State, FVector(0, 0, 110));
		if (!Player) return false;
		Player->SetActorEnableCollision(true);
		Player->GetCharacterMovement()->SetComponentTickEnabled(true);
		Player->GetCharacterMovement()->SetMovementMode(MOVE_Walking);
		const bool bOTM = Case >= 15 && Case <= 17 || Case == 22 || Case == 31 || Case == 32 || Case == 39 || Case == 40 || Case >= 49;
		if (!bOTM) Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		const auto* Set = bOTM ? Player->GetMotionMatchingAssetSet() : Player->GetCombatStrafeMotionMatchingAssetSet();
		if (!Set || !Set->RunDatabases.Cycle) return false;
		float SchemaWeight = 1.f;
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJStrafeSchemaVelocityWeight="), SchemaWeight);
		if (SchemaWeight > 1.f)
		{
			if (!PrepareSchemaTrial(Set, FMath::Clamp(SchemaWeight, 1.f, 16.f))) return false;
			TrialProfile = const_cast<UProject_JCombatAnimProfile*>(Player->GetCombatAnimProfile());
			if (!TrialProfile) return false;
			OriginalProfileSet = TrialProfile->CombatStrafeMotionMatchingAssetSet;
			TrialProfile->CombatStrafeMotionMatchingAssetSet = SchemaTrialSet.Get();
			Set = SchemaTrialSet.Get();
		}
		auto* Database = Set->RunDatabases.Cycle.Get();
		if (bValidateInstalled && !bOTM && !Test->TestEqual(TEXT("Playback uses real installed schema"), Database->Schema->GetOutermost()->GetName(),
			FString(TEXT("/Game/Animation_Logic/PSS/PSS_Combat_Run_Continuity")))) return false;
		Report += FString::Printf(TEXT("Database Case=%d Mode=%d Neighbors=%d PCA=%d ContinuingBias=%.3f BaseBias=%.3f Schema=%s\n"),
			Case, int32(Database->PoseSearchMode), Database->KDTreeQueryNumNeighbors,
			Database->NumberOfPrincipalComponents, Database->ContinuingPoseCostBias, Database->BaseCostBias, *Database->Schema->GetPathName());
		// Diagnostic only: compare exact search on the same index. No rebuild,
		// Modify/PostEditChange or save, and restore before tearing down each world.
		float VelocityWeight = 1.f;
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJStrafeVelocityWeight="), VelocityWeight);
		VelocityWeight = FMath::Clamp(VelocityWeight, 1.f, 16.f);
		if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeBruteForce")) || VelocityWeight > 1.f)
		{
			AuditDatabase = Database;
			OriginalSearchMode = Database->PoseSearchMode;
			Database->PoseSearchMode = EPoseSearchMode::BruteForce;
			Report += TEXT("InMemoryAudit=BruteForce\n");
			if (VelocityWeight > 1.f)
			{
				auto& Index = const_cast<UE::PoseSearch::FSearchIndex&>(Database->GetSearchIndex());
				Database->Schema->IterateChannels([&](const UPoseSearchFeatureChannel* Channel)
				{
					const auto* Velocity = Cast<UPoseSearchFeatureChannel_Velocity>(Channel);
					if (!Velocity || !Cast<UPoseSearchFeatureChannel_Trajectory>(Velocity->GetOuter())) return;
					for (int32 Offset = 0; Offset < Velocity->GetChannelCardinality(); ++Offset)
					{
						const int32 Dimension = Velocity->GetChannelDataOffset() + Offset;
						OriginalWeights.Emplace(Dimension, Index.WeightsSqrt[Dimension]);
						Index.WeightsSqrt[Dimension] *= FMath::Sqrt(VelocityWeight);
					}
				});
				Report += FString::Printf(TEXT("InMemoryVelocityCostMultiplier=%.1f Dimensions=%d\n"), VelocityWeight, OriginalWeights.Num());
			}
		}
		const auto& CoverageIndex = Database->GetSearchIndex();
		const auto* LocomotionProfile = Player->GetLocomotionProfile();
		CoverageBlendTime = LocomotionProfile ? LocomotionProfile->MotionMatchingSearchPolicy.DefaultBlendTime : .20f;
		CoverageMargin = CoverageBlendTime * (LocomotionProfile ? LocomotionProfile->MotionMatchingSearchPolicy.MaxPlayRate : 1.15f);
		for (const auto& Asset : CoverageIndex.Assets)
		{
			if (Asset.IsLooping()) continue;
			const auto* Entry = Database->GetDatabaseAnimationAsset(Asset.GetSourceAssetIdx());
			if (!Entry) continue;
			const float End = Asset.GetLastSampleTime(Database->Schema->SampleRate);
			NonLoopIndexedEnds.Add(GetFNameSafe(Entry->GetAnimationAsset()), End);
		}
		Report += FString::Printf(TEXT("EntryCoverageMeasuredOnly=1 Margin=%.3f PacketCamera=%d\n"), CoverageMargin, bPacketCamera);
		return true;
	}
public:
	explicit FStrafeCameraPlayback(FAutomationTestBase* InTest) : Test(InTest)
	{
		bValidateInstalled = FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectStrafeDirectionSchema"));
		bVerifyForwardCurve = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyStrafeForwardCurve"));
		bVerifyCorrection = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyCorrectionTurns"));
		bVerifyRapidTurn = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyRapidTurns"));
		bPacketCamera = FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafePacketCamera"));
	}
	virtual ~FStrafeCameraPlayback() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("CMC camera fixture setup failed")); Cleanup(); return true; }
		constexpr float Dt = 1.f / 120;
		const int32 Direction = Case >= 33 && Case <= 35 ? Case - 33 : Case >= 9 ? 3 : Case % 3;
		const int32 RateIndex = Case >= 9 ? Case - 9 : Case / 3;
		float Rate = RateIndex == 0 ? 90 : RateIndex == 1 ? 240 : 480;
		if (Case == 12) Rate = 180;
		if (Case == 13) Rate = 180 + 25 * FMath::Sin(float(Frame - 120) * Dt * 8);
		if (Case == 14) Rate = Frame < 210 ? 360 : 180;
		if (Case == 15) Rate = 180;
		if (Case == 16 || Case == 18) Rate = 211;
		if (Case == 17 || Case == 20) Rate = 320;
		if (Case == 19) Rate = 268;
		if (Case == 21 || Case == 22) Rate = Frame < 139 ? 900 : 0;
		float RapidTarget = 180;
		if (Case >= 23 && Case <= 30)
		{
			const float Angles[] = {170, 180, 200, 211};
			RapidTarget = Angles[(Case - 23) / 2] * ((Case - 23) % 2 ? 1.f : -1.f);
		}
		if (Case == 31) RapidTarget = -180;
		if (Case == 39 || Case == 40) RapidTarget = Case == 39 ? -211 : 211;
		if (Case >= 23) Rate = FMath::Sign(RapidTarget) * 3000;
		if (Case >= 41)
		{
			const int32 Trial = (Case - 41) % 8;
			const float Rates[] = {450, 600, 900, 1200};
			RapidTarget = Trial % 2 ? 180 : -180;
			Rate = FMath::Sign(RapidTarget) * Rates[Trial / 2];
		}
		const float MoveOffset = Direction == 0 ? -90 : Direction == 1 ? 180 : Direction == 2 ? 90 : 0;
		const int32 RotationStartFrame = Case >= 41 ? 180 : 120;
		if (Case >= 12 && Frame > RotationStartFrame) AccumulatedCameraYaw += Dt * Rate;
		if (Case >= 23) AccumulatedCameraYaw = FMath::Clamp(AccumulatedCameraYaw,
			FMath::Min(0.f, RapidTarget), FMath::Max(0.f, RapidTarget));
		// A 40Hz pointer sampled by a 120Hz character tick delivers the same
		// average camera motion in packets, rather than an ideal change every tick.
		const int32 CameraFrames = bPacketCamera ? ((Frame - 120) / 3) * 3 : Frame - 120;
		const float CameraYaw = Case >= 12 ? AccumulatedCameraYaw : Frame < 120 ? 0 : CameraFrames * Dt * Rate;
		Controller->SetControlRotation(FRotator(0, CameraYaw, 0));
		const FVector Move = FRotator(0, CameraYaw + MoveOffset, 0).Vector();
		const float Radians = FMath::DegreesToRadians(MoveOffset);
		const bool bRelease = Case == 36 && Frame >= 133;
		if (bRelease) Player->GetLocomotionAnimStateComponent()->ClearMoveInput();
		else
		{
			Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(FMath::Sin(Radians), FMath::Cos(Radians)));
			Player->AddMovementInput(Move);
		}
		if (Case == 37) Player->bIsDodging = Frame >= 133 && Frame < 145;
		if (Case == 38 && Frame == 133)
			Player->GetAbilitySystemComponent()->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, Dt);
		auto* Movement = Player->GetCharacterMovement();
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		const bool bMeasured = Frame >= 120 && Frame < 360;
		if (bMeasured)
		{
			if (!Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching() && !MM.PostSelection.bIsContinuingPoseSearch)
			{
				++Reselections;
				if (LastJumpFrame >= 0 && (Frame - LastJumpFrame) * Dt < CoverageBlendTime) ++ShortSegments;
				LastJumpFrame = Frame;
				if (const float* End = NonLoopIndexedEnds.Find(MM.PostSelection.SelectedAnimation))
					TailEntries += MM.PostSelection.SelectedAnimationTime > *End - CoverageMargin;
			}
			GroundFrames += Movement->IsMovingOnGround();
			MovingFrames += Movement->Velocity.Size2D() > 100;
			ArcFrames += MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Arc_"));
			GeneralFrames += MM.SelectionContext.bGeneralTurnCandidates;
			const bool bSelectedGeneral = MM.PostSelection.SelectedDatabase.ToString().Contains(TEXT("GeneralTurn"));
			SelectedGeneralFrames += bSelectedGeneral;
			if (Frame >= 300) LateGeneralFrames += MM.SelectionContext.bGeneralTurnCandidates || bSelectedGeneral;
			AcuteFrames += MM.SelectionContext.bMovingTurn180;
			if (Direction < 3) Test->TestFalse(TEXT("Side/backward strafe does not open forward GeneralTurn"), MM.SelectionContext.bGeneralTurnCandidates);
			if (Case >= 23)
			{
				const bool bAcute = MM.SelectionContext.bMovingTurn180;
				const auto& Request = Player->GetLocomotionAnimStateComponent()->GetTurnRequestSample();
				PreparedFrames += Request.bPreparing;
				PreparedEnterFrames += FCString::Strcmp(Request.AcuteReason, TEXT("PreparedEnter")) == 0;
				const auto* Set = MM.SelectionContext.RotationMode == EProject_JLocomotionRotationMode::Strafe
					? Player->GetCombatStrafeMotionMatchingAssetSet() : Player->GetMotionMatchingAssetSet();
				AcuteSelectedFrames += bAcute && Set && Set->RunDatabases.TurnRedirect &&
					MM.PostSelection.SelectedDatabase == Set->RunDatabases.TurnRedirect->GetFName();
				FalseStopFrames += !bRelease && Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching() &&
					Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TransitionToIdle;
				if (bAcute)
				{
					MinimumTurnSpeed = FMath::Min(MinimumTurnSpeed, Movement->Velocity.Size2D());
					BrakingTurnFrames += Movement->Velocity.Size2D() < 80;
				}
				if (Direction < 3) Test->TestFalse(TEXT("Rapid A/S/D never borrows forward 180 data"), bAcute);
				if (bRelease || Case == 37 && Frame >= 133 && Frame < 145)
				{
					Test->TestFalse(TEXT("Release/action cancels the GT request immediately"),
						Player->GetLocomotionAnimStateComponent()->GetTurnRequestSample().bAcuteActive);
					if (Frame >= 135) Test->TestFalse(TEXT("Animation consumes cancellation across its snapshot boundary"), bAcute);
				}
			}
		}
		if (Case >= 23 && Frame >= 120 && Frame <= (Case >= 41 ? 300 : 180))
		{
			const auto& Request = Player->GetLocomotionAnimStateComponent()->GetTurnRequestSample();
			Report += FString::Printf(TEXT("RapidFrame Case=%d Frame=%d Speed=%.2f Acute=%d Approach=%d Preparing=%d Demand=%d Sweep=%.1f Remaining=%.1f VisualValid=%d VisualYaw=%.1f Reason=%s General=%d Override=%d Present=%d PSD=%s Clip=%s\n"),
				Case, Frame, Movement->Velocity.Size2D(), Request.bAcuteActive, Request.bAcuteApproach, Request.bPreparing, Request.Demand, Request.RequestedSweep, Request.RemainingFacing, Request.bVisualFacingValid, Request.VisualFacingYaw, Request.AcuteReason,
				MM.SelectionContext.bGeneralTurnCandidates, Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching(),
				int32(Anim->GetThreadSafeStateControllerPresentationState()), *MM.PostSelection.SelectedDatabase.ToString(),
				*MM.PostSelection.SelectedAnimation.ToString());
		}
		if (Frame % 12 == 0)
		{
			const float ActorYaw = Player->GetActorRotation().Yaw;
			Report += FString::Printf(TEXT("Direction Case=%d Key=%s Rate=%.0f Frame=%d Camera=%.2f Actor=%.2f Ground=%d Speed=%.2f Accel=%.2f VelocityToActor=%.2f Clip=%s\n"),
				Case, Direction == 0 ? TEXT("A") : Direction == 1 ? TEXT("S") : Direction == 2 ? TEXT("D") : TEXT("W"), Rate, Frame,
				CameraYaw, ActorYaw, Movement->IsMovingOnGround(), Movement->Velocity.Size2D(), Movement->GetCurrentAcceleration().Size2D(),
				FMath::FindDeltaAngleDegrees(ActorYaw, Movement->Velocity.Rotation().Yaw), *MM.PostSelection.SelectedAnimation.ToString());
			const auto* C = Player->GetMotionMatchingTrajectoryComponent();
			const auto& Samples = C->GetTrajectory().Samples;
			for (int32 Index = 0; Index + 1 < Samples.Num(); ++Index)
			{
				const auto& Sample = Samples[Index];
				if (Sample.TimeInSeconds < .35f || Sample.TimeInSeconds > .81f) continue;
				const FVector Travel = Samples[Index + 1].Position - Sample.Position;
				const float Facing = (Sample.Facing * Player->GetMesh()->GetRelativeRotation().Quaternion().Inverse()).Rotator().Yaw;
				Report += FString::Printf(TEXT("Prediction Case=%d Frame=%d Time=%.2f CameraRate=%.2f ClampedRate=%.2f TravelToFacing=%.2f\n"),
					Case, Frame, Sample.TimeInSeconds, C->GetCharacterTrajectoryData().ControllerYawRate,
					C->GetCharacterTrajectoryData().ControllerYawRateClamped, FMath::FindDeltaAngleDegrees(Facing, Travel.Rotation().Yaw));
			}
		}
		if (++Frame < 360) return false;
		Report += FString::Printf(TEXT("Summary Case=%d GroundFrames=%d MovingFrames=%d ArcFrames=%d\n"), Case, GroundFrames, MovingFrames, ArcFrames);
		Report += FString::Printf(TEXT("EntrySummary Case=%d Reselections=%d ShortSegments=%d TailEntries=%d\n"), Case, Reselections, ShortSegments, TailEntries);
		Report += FString::Printf(TEXT("GeneralSummary Case=%d CandidateFrames=%d SelectedFrames=%d LateFrames=%d\n"),
			Case, GeneralFrames, SelectedGeneralFrames, LateGeneralFrames);
		Test->TestTrue(TEXT("Measured CMC actually remained grounded"), GroundFrames >= 235);
		if (Case < 23) Test->TestTrue(TEXT("Measured CMC actually moved"), MovingFrames >= 235);
		if (Case >= 23)
		{
			Report += FString::Printf(TEXT("RapidSummary Case=%d Target=%.0f AcuteFrames=%d PreparedFrames=%d PreparedEnterFrames=%d SelectedAcuteFrames=%d FalseStopFrames=%d BrakingTurnFrames=%d MinimumTurnSpeed=%.2f\n"),
				Case, RapidTarget, AcuteFrames, PreparedFrames, PreparedEnterFrames, AcuteSelectedFrames, FalseStopFrames, BrakingTurnFrames, MinimumTurnSpeed);
			Test->TestEqual(TEXT("Held steering cannot synthesize an external Stop"), FalseStopFrames, 0);
			if (Case >= 25 && Case <= 30 || Case == 39 || Case == 40)
				Test->TestTrue(TEXT("Qualified rapid reversal opens acute candidates"), AcuteFrames > 0);
			if (Case == 31 || Case == 32)
				Test->TestTrue(TEXT("A prepared OTM reversal admits real remaining correction"), AcuteFrames > 0);
			if (Case >= 41)
			{
				if (Case == 49 || Case == 50)
				{
					Test->TestEqual(TEXT("Already followed OTM 450 deg/s rotation requires no acute correction"), AcuteFrames, 0);
					Test->TestEqual(TEXT("Already followed OTM rotation records no preparation debt"), PreparedFrames, 0);
				}
				else
				{
					Test->TestTrue(TEXT("Delayed correction exercises preparation"), PreparedFrames > 0);
					Test->TestTrue(TEXT("Delayed correction can select authored Turn poses"), AcuteSelectedFrames > 0);
				}
				// Lower-rate curves can be fully followed by CMC. A camera angle
				// alone must not require Turn; inspect the recorded residual too.
				if ((Case - 41) % 8 >= 4)
					Test->TestTrue(TEXT("Delayed 900/1200 deg/s reversal reaches prepared commitment"), PreparedEnterFrames > 0);
			}
			if (Case >= 25 && Case <= 30) Test->TestTrue(TEXT("Qualified Strafe reversal can select authored 180 data"), AcuteSelectedFrames > 0);
			if (Case == 36) Test->TestFalse(TEXT("Released input eventually stops"), Player->GetLocomotionAnimStateComponent()->bHasMoveInput);
			else Test->TestTrue(TEXT("Steering recovers travel after the braking minimum"), Movement->Velocity.Size2D() > 250);
			Test->TestFalse(TEXT("An old acute event cannot remain latched"), MM.SelectionContext.bMovingTurn180);
		}
		if ((SchemaTrialSet.IsValid() || bValidateInstalled) && Case < 6)
			Test->TestEqual(TEXT("Camera-aligned side/backward strafe avoids forward Arc dominance"), ArcFrames, 0);
		if (bVerifyForwardCurve && (Case == 9 || Case == 12 || Case == 13))
		{
			Test->TestEqual(TEXT("Smooth forward camera motion stays in Cycle"), GeneralFrames, 0);
			Test->TestEqual(TEXT("Smooth forward curve selects no GeneralTurn"), SelectedGeneralFrames, 0);
		}
		if (bVerifyForwardCurve && !bVerifyCorrection && Case == 14)
		{
			Test->TestTrue(TEXT("Actual CMC fast forward turn still opens GeneralTurn"), GeneralFrames > 0);
			Test->TestTrue(TEXT("Actual fast forward turn still selects GeneralTurn"), SelectedGeneralFrames > 0);
			Test->TestEqual(TEXT("Turn hands back to Cycle when mouse rotation becomes smooth"), LateGeneralFrames, 0);
		}
		if (bVerifyCorrection && (Case == 10 || Case >= 14 && Case <= 20))
		{
			Test->TestEqual(TEXT("Already followed OTM/Strafe curves never manufacture acute Turn"), AcuteFrames, 0);
			Test->TestEqual(TEXT("Aligned OTM/Strafe curves stay in Cycle irrespective of angular-rate boundaries"), GeneralFrames, 0);
			Test->TestEqual(TEXT("Aligned curves select no GeneralTurn"), SelectedGeneralFrames, 0);
		}
		if (bVerifyCorrection && (Case == 21 || Case == 22))
		{
			Test->TestTrue(TEXT("Actual CMC abrupt forward correction still opens GeneralTurn"), GeneralFrames > 0);
			Test->TestTrue(TEXT("Actual CMC correction can select authored GeneralTurn"), SelectedGeneralFrames > 0);
			Test->TestEqual(TEXT("Actual CMC recovered correction returns to Cycle"), LateGeneralFrames, 0);
		}
		Cleanup(); Frame = GroundFrames = MovingFrames = ArcFrames = 0;
		GeneralFrames = SelectedGeneralFrames = LateGeneralFrames = 0; AccumulatedCameraYaw = 0;
		AcuteFrames = AcuteSelectedFrames = FalseStopFrames = BrakingTurnFrames = PreparedFrames = PreparedEnterFrames = 0;
		MinimumTurnSpeed = TNumericLimits<float>::Max();
		Reselections = ShortSegments = TailEntries = 0; LastJumpFrame = -1;
		if (++Case < (bVerifyRapidTurn ? 57 : bVerifyCorrection ? 23 : bVerifyForwardCurve ? 15 : SchemaTrialSet.IsValid() || bValidateInstalled ? 12 : 9)) return false;
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/StrafeCamera_20261006");
		IFileManager::Get().MakeDirectory(*Directory, true);
		FString Output = Directory / TEXT("Playback.txt");
		FParse::Value(FCommandLine::Get(), TEXT("ProjectJStrafePlaybackOutput="), Output);
		Test->TestTrue(TEXT("Save CMC direction/prediction evidence"), FFileHelper::SaveStringToFile(Report,
			*Output, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
		return true;
	}
};
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeCameraPlaybackTest, "ProjectJ.Animation.Naturalness.StrafeCameraCMC",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeCameraPlaybackTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStrafeCameraPlayback(this));
	return true;
}
#endif
