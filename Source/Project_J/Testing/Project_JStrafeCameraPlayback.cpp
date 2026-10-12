#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JCombatAnimProfile.h"
#include "Animation/Project_JStrafeUpperBodyFacingEditorLibrary.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimNode_ProjectJStrafeLowerBodyFacing.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JCombatStyleDefinition.h"
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
#include "Misc/Crc.h"
#include "Engine/SkeletalMesh.h"
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
	UPoseSearchDatabase* ForwardAuditDatabase = nullptr;
	EPoseSearchMode OriginalForwardSearchMode = EPoseSearchMode::PCAKDTree;
	EPoseSearchMode OriginalSearchMode = EPoseSearchMode::PCAKDTree;
	TArray<TPair<int32, float>> OriginalWeights;
	TMap<FName, float> NonLoopIndexedEnds;
	bool bPacketCamera = false;
	bool bSlowForwardCamera = false;
	bool bCurveStability = false;
	bool bForwardPoolAudit = false;
	bool bLateralCamera = false;
	int32 LateralBoxFrames = 0, LateralDiamondFrames = 0;
	int32 TotalLateralBoxFrames = 0, TotalLateralDiamondFrames = 0;
	TArray<FVector2D> CameraReplay;
	bool bRecordedMicro = false;
	bool bRecordedCurve = false;
	bool bReplayPrecurve = false;
	bool bReplayWarmup = false;
	int32 PrecurveArcFrames = 0;
	int32 ReplayBoxDiamondFrames = 0;
	int32 ReplayIndex = 0, ReplayFrames = 0, ReplayLoopFrames = 0, ReplayForwardFrames = 0, ReplayArcFrames = 0;
	int32 FrameStep = 1;
	int32 MicroLoopFrames = 0, MicroFrames = 0, ForwardPoolFrames = 0;
	int32 SingleCase = INDEX_NONE;
	int32 SettledCurveFrames = 0;
	bool bFacingBoundary = false;
	bool bColdFacingAudit = false;
	int32 MontageFrames = 0, TurnInPlaceFrames = 0;
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
		if (ForwardAuditDatabase) { ForwardAuditDatabase->PoseSearchMode = OriginalForwardSearchMode; ForwardAuditDatabase = nullptr; }
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
		const bool bOTM = !bForwardPoolAudit && (Case >= 15 && Case <= 17 || Case == 22 || Case == 31 || Case == 32 || Case == 39 || Case == 40 || Case >= 49);
		if (!bOTM) Player->GetAbilitySystemComponent()->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		const auto* Set = bOTM ? Player->GetMotionMatchingAssetSet() : Player->GetCombatStrafeMotionMatchingAssetSet();
		if (!Set || !Set->RunDatabases.Cycle) return false;
		if (const auto* Forward = Set->RunDatabases.ForwardCycle.Get())
			Report += FString::Printf(TEXT("ForwardData Case=%d Schema=%s Cardinality=%d ContinuingBias=%.4f\n"), Case,
				*GetPathNameSafe(Forward->Schema), Forward->Schema ? Forward->Schema->SchemaCardinality : 0, Forward->ContinuingPoseCostBias);
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
			if (Set->RunDatabases.ForwardCycle)
			{
				ForwardAuditDatabase = Set->RunDatabases.ForwardCycle;
				OriginalForwardSearchMode = ForwardAuditDatabase->PoseSearchMode;
				ForwardAuditDatabase->PoseSearchMode = EPoseSearchMode::BruteForce;
			}
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
		int32 IndexedBoxPoses = 0, IndexedDiamondPoses = 0;
		for (const auto& Asset : CoverageIndex.Assets)
		{
			if (Asset.IsLooping()) continue;
			const auto* Entry = Database->GetDatabaseAnimationAsset(Asset.GetSourceAssetIdx());
			if (!Entry) continue;
			if (bLateralCamera)
			{
				const FString Name = GetNameSafe(Entry->GetAnimationAsset());
				IndexedBoxPoses += Name.Contains(TEXT("_Box_")) ? Asset.GetNumPoses() : 0;
				IndexedDiamondPoses += Name.Contains(TEXT("_Diamond_")) ? Asset.GetNumPoses() : 0;
			}
			const float End = Asset.GetLastSampleTime(Database->Schema->SampleRate);
			NonLoopIndexedEnds.Add(GetFNameSafe(Entry->GetAnimationAsset()), End);
		}
		if (bLateralCamera)
		{
			Test->TestTrue(TEXT("The lateral search index retains usable Box and Diamond poses"), IndexedBoxPoses > 0 && IndexedDiamondPoses > 0);
			Report += FString::Printf(TEXT("LateralCandidates Case=%d BoxPoses=%d DiamondPoses=%d\n"), Case, IndexedBoxPoses, IndexedDiamondPoses);
		}
		Report += FString::Printf(TEXT("EntryCoverageMeasuredOnly=1 Margin=%.3f PacketCamera=%d\n"), CoverageMargin, bPacketCamera);
		return true;
	}
public:
	explicit FStrafeCameraPlayback(FAutomationTestBase* InTest, bool bRecorded = false, bool bCurve = false, bool bLateral = false) : Test(InTest), bLateralCamera(bLateral), bRecordedMicro(bRecorded), bRecordedCurve(bCurve)
	{
		bValidateInstalled = FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectStrafeDirectionSchema"));
		bVerifyForwardCurve = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyStrafeForwardCurve"));
		bVerifyCorrection = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyCorrectionTurns"));
		bVerifyRapidTurn = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyRapidTurns"));
		bPacketCamera = FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafePacketCamera"));
		// Reuse the actual CMC/MM fixture for this read-only facing diagnostic;
		// existing regression cases/rates remain unchanged without this flag.
		bSlowForwardCamera = FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeSlowForwardCamera"));
		bFacingBoundary = FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeFacingBoundary"));
		bSlowForwardCamera |= bFacingBoundary;
		if (bSlowForwardCamera)
		{
			Case = 9;
			bVerifyForwardCurve = bVerifyCorrection = bVerifyRapidTurn = false;
			Report += TEXT("FacingAudit=ForwardW Rates=30,60,90deg/s\n");
			if (bFacingBoundary) Report += TEXT("BoundaryAudit=IdleTurnInPlace,MovingAuthoredCombatMontage,InputRelease\n");
		}
		bColdFacingAudit = FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeFacingColdPoseAudit"));
		bCurveStability = FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeCurveStability"));
		bForwardPoolAudit = bRecordedMicro || bRecordedCurve || FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeForwardPoolAudit"));
		bReplayPrecurve = bRecordedMicro && FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeReplayPrecurve"));
		bReplayWarmup = bReplayPrecurve && FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeReplayWarmup"));
		bCurveStability |= bForwardPoolAudit;
		if (bForwardPoolAudit)
		{
			int32 PlaybackHz = 120;
			FParse::Value(FCommandLine::Get(), TEXT("ProjectJStrafePlaybackHz="), PlaybackHz);
			if (PlaybackHz != 30 && PlaybackHz != 60 && PlaybackHz != 120) Test->AddError(TEXT("PlaybackHz must be 30, 60 or 120"));
			FrameStep = PlaybackHz == 30 ? 4 : PlaybackHz == 60 ? 2 : 1;
			FString ReplayPath;
			if (bRecordedMicro) ReplayPath = FPaths::ProjectDir() / TEXT("Source/Project_J/Testing/Fixtures/StrafeMicroCamera_20261012.csv");
			else if (bRecordedCurve) ReplayPath = FPaths::ProjectDir() / TEXT("Source/Project_J/Testing/Fixtures/StrafeCurveCamera_20261012.csv");
			const bool bHasReplayPath = FParse::Value(FCommandLine::Get(), TEXT("ProjectJStrafeCameraReplay="), ReplayPath) || bRecordedMicro || bRecordedCurve;
			if (bHasReplayPath)
			{
				TArray<FString> Lines;
				if (!FFileHelper::LoadFileToStringArray(Lines, *ReplayPath)) Test->AddError(TEXT("Cannot read camera replay"));
				for (const FString& Line : Lines)
				{
					TArray<FString> Fields; Line.ParseIntoArray(Fields, TEXT(","));
					if (Fields.Num() != 2) { Test->AddError(TEXT("Replay requires time,yaw rows")); break; }
					const FVector2D Sample(FCString::Atod(*Fields[0]), FCString::Atod(*Fields[1]));
					if (Sample.ContainsNaN() || Sample.X < 0 || (!CameraReplay.IsEmpty() && Sample.X <= CameraReplay.Last().X))
					{ Test->AddError(TEXT("Replay times must increase")); break; }
					CameraReplay.Add(Sample);
				}
				if (CameraReplay.Num() < 2) Test->AddError(TEXT("Camera replay needs at least two samples"));
			}
			Report += FString::Printf(TEXT("CameraReplay Samples=%d PlaybackHz=%d Duration=%.3f\n"), CameraReplay.Num(), 120 / FrameStep,
				CameraReplay.IsEmpty() ? 0.0 : CameraReplay.Last().X);
		}
		if (bCurveStability)
		{
			Case = 9; bSlowForwardCamera = bFacingBoundary = bVerifyRapidTurn = bVerifyForwardCurve = bVerifyCorrection = false;
			Report += TEXT("CurveStability=Forward21,Reversal21,Reversal90,Reversal5,PointerNoise,IsolatedNudge NoAssetWrites=1\n");
		}
		if (bColdFacingAudit)
		{
			Case = 15; bSlowForwardCamera = bFacingBoundary = bVerifyRapidTurn = bVerifyForwardCurve = false;
			bVerifyCorrection = true;
			Report += TEXT("ColdPoseAudit=OTM Cases=15,16,17,22\n");
		}
		if (FParse::Value(FCommandLine::Get(), TEXT("ProjectJStrafeSingleCase="), SingleCase))
		{
			SingleCase = FMath::Clamp(SingleCase, bCurveStability ? 9 : 0, bForwardPoolAudit ? 19 : bCurveStability ? 14 : 56); Case = SingleCase;
		}
		if (!CameraReplay.IsEmpty()) SingleCase = Case = 15;
	}
	virtual ~FStrafeCameraPlayback() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("CMC camera fixture setup failed")); Cleanup(); return true; }
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		const float Dt = float(FrameStep) / 120;
		const int32 Direction = bLateralCamera ? (Case % 2 ? 2 : 0) : bForwardPoolAudit && Case == 17 ? 0 : bForwardPoolAudit && Case == 18 ? 1 :
			Case >= 33 && Case <= 35 ? Case - 33 : Case >= 9 ? 3 : Case % 3;
		const int32 RateIndex = Case >= 9 ? Case - 9 : Case / 3;
		float Rate = RateIndex == 0 ? 90 : RateIndex == 1 ? 240 : 480;
		if (bSlowForwardCamera) Rate = 30.f * (RateIndex + 1);
		if (bSlowForwardCamera && FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeSlowForwardCameraLeft"))) Rate = -Rate;
		if (Case == 12) Rate = 180;
		if (Case == 13) Rate = 180 + 25 * FMath::Sin(float(Frame - 120) * Dt * 8);
		if (Case == 14) Rate = Frame < 210 ? 360 : 180;
		if (Case == 15) Rate = 180;
		if (Case == 16 || Case == 18) Rate = 211;
		if (Case == 17 || Case == 20) Rate = 320;
		if (Case == 19) Rate = 268;
		if (Case == 21 || Case == 22) Rate = Frame < 139 ? 900 : 0;
		if (bFacingBoundary && Case == 9) Rate = 120;
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
		if (bLateralCamera)
		{
			const float Rates[] = {21, 90, 240};
			Rate = Rates[Case / 4] * (Case % 4 >= 2 ? -1.f : 1.f);
		}
		const float MoveOffset = bForwardPoolAudit && Case == 19 ? 45 : Direction == 0 ? -90 : Direction == 1 ? 180 : Direction == 2 ? 90 : 0;
		const int32 RotationStartFrame = Case >= 41 ? 180 : 120;
		if (Case >= 12 && Frame > RotationStartFrame) AccumulatedCameraYaw += Dt * Rate;
		if (Case >= 23) AccumulatedCameraYaw = FMath::Clamp(AccumulatedCameraYaw,
			FMath::Min(0.f, RapidTarget), FMath::Max(0.f, RapidTarget));
		// A 40Hz pointer sampled by a 120Hz character tick delivers the same
		// average camera motion in packets, rather than an ideal change every tick.
		const int32 CameraFrames = bPacketCamera ? ((Frame - 120) / 3) * 3 : Frame - 120;
		float CameraYaw = Case >= 12 ? AccumulatedCameraYaw : Frame < 120 ? 0 : CameraFrames * Dt * Rate;
		if (bCurveStability)
		{
			const float T = FMath::Max(0.f, (Frame - 120) * Dt);
			const float Sampled = bPacketCamera ? FMath::FloorToFloat(T * 20) / 20 : T;
			const float Rates[] = {21, 21, 90, 5, 0, 0}; Rate = Case <= 14 ? Rates[Case - 9] : 0;
			CameraYaw = Rate * (Case == 9 ? FMath::Min(Sampled, 4.f) : Sampled <= 2 ? Sampled : FMath::Max(0.f, 4 - Sampled));
			if (Case == 13) CameraYaw = T > 0 && T < 4 ? (int32(Sampled * 20) % 2 ? .15f : -.15f) : 0;
			if (Case == 14) CameraYaw = T > 0 ? .5f : 0;
			if (bForwardPoolAudit && Case >= 15)
			{
				const auto MicroYaw = [](float Time)
				{
					const int32 Periods[] = {4, 7, 5, 6, 3, 9, 4, 8};
					const int32 Ticks = FMath::RoundToInt(FMath::Max(Time, 0.f) * 120);
					int32 Tick = 0, Pulses = 0;
					while (Tick + Periods[Pulses % 8] <= Ticks) { Tick += Periods[Pulses % 8]; ++Pulses; }
					return Pulses * .175f;
				};
				CameraYaw = Case == 15 ? MicroYaw(FMath::Min(T, 4.f)) :
					T <= 1 ? 21.f * T : T <= 1.35f ? 21.f : 21.f - MicroYaw(FMath::Min(T - 1.35f, 2.65f));
			}
		}
		if (!CameraReplay.IsEmpty())
		{
			const double CameraTime = FMath::Max(0.0, (Frame - 120) / 120.0);
			const double ReplayTime = FMath::Max(0.0, CameraTime - (bReplayPrecurve ? (bReplayWarmup ? 4.0 : 2.0) : 0.0));
			while (ReplayIndex + 1 < CameraReplay.Num() && CameraReplay[ReplayIndex + 1].X <= ReplayTime) ++ReplayIndex;
			const FVector2D& From = CameraReplay[ReplayIndex];
			const FVector2D& To = CameraReplay[FMath::Min(ReplayIndex + 1, CameraReplay.Num() - 1)];
			const double Alpha = To.X > From.X ? FMath::Clamp((ReplayTime - From.X) / (To.X - From.X), 0.0, 1.0) : 0.0;
			CameraYaw = FMath::Lerp(From.Y, To.Y, Alpha);
			if (bReplayPrecurve) CameraYaw += 90.f * FMath::Clamp(CameraTime - (bReplayWarmup ? 2.0 : 0.0), 0.0, 2.0);
		}
		Controller->SetControlRotation(FRotator(0, CameraYaw, 0));
		const FVector Move = FRotator(0, CameraYaw + MoveOffset, 0).Vector();
		const float Radians = FMath::DegreesToRadians(MoveOffset);
		const bool bRelease = Case == 36 && Frame >= 133 || bFacingBoundary && (Case == 9 || Case == 11 && Frame >= 180);
		if (bRelease) Player->GetLocomotionAnimStateComponent()->ClearMoveInput();
		else
		{
			Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(FMath::Sin(Radians), FMath::Cos(Radians)));
			Player->AddMovementInput(Move);
		}
		if (Case == 37) Player->bIsDodging = Frame >= 133 && Frame < 145;
		if (Case == 38 && Frame == 133)
			Player->GetAbilitySystemComponent()->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		if (bFacingBoundary && Case == 10 && Frame == 180)
		{
			FGameplayTagContainer Tags; Player->GetAbilitySystemComponent()->GetOwnedGameplayTags(Tags);
			const auto* Style = Player->GetCombatStyleDefinition();
			const auto* Entry = Style && Style->ComboDefinition ? Style->ComboDefinition->FindStartNode(FProject_JGameplayTags::Get().InputTag_Weapon_LMB, Tags) : nullptr;
			auto* Montage = Entry && Entry->AttackDefinition ? Entry->AttackDefinition->Montage.Get() : nullptr;
			if (!Test->TestTrue(TEXT("Read-only fixture plays real equipped combat montage"), Montage && Anim->Montage_Play(Montage) > 0))
			{ Cleanup(); return true; }
		}
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, Dt);
		if (bFacingBoundary)
		{
			const bool bMontage = Anim->IsAnyMontagePlaying();
			const bool bTIP = Anim->GetThreadSafeStateControllerPresentationState() == EProject_JStateControllerPresentationState::TurnInPlace;
			MontageFrames += bMontage; TurnInPlaceFrames += bTIP;
			if (bMontage || bTIP || bRelease)
				Test->TestFalse(TEXT("Real montage/idle rotation/input release owns lower pose without residual"), Anim->GetThreadSafeStrafeLowerBodyFacing().Facing.bCanRelease);
		}
		auto* Movement = Player->GetCharacterMovement();
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		const int32 PrecurveStartFrame = bReplayWarmup ? 360 : 120;
		if (bReplayPrecurve && Frame >= PrecurveStartFrame && Frame < PrecurveStartFrame + 240) PrecurveArcFrames += MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Arc_"));
		// Give the previous authored curve half a second to hand off before
		// measuring the recorded micro interval; neither interval is discarded.
		const int32 ReplayStartFrame = bReplayPrecurve ? PrecurveStartFrame + 240 : 120;
		const int32 ReplayObservationStart = bReplayPrecurve ? ReplayStartFrame + 60 : 120;
		if (!CameraReplay.IsEmpty() && Frame >= ReplayObservationStart && !Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching())
		{
			++ReplayFrames;
			ReplayLoopFrames += MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Loop_"));
			const FString ReplayClip = MM.PostSelection.SelectedAnimation.ToString();
			ReplayArcFrames += ReplayClip.Contains(TEXT("_Arc_"));
			ReplayBoxDiamondFrames += ReplayClip.Contains(TEXT("_Box_")) || ReplayClip.Contains(TEXT("_Diamond_"));
			ReplayForwardFrames += MM.PostSelection.SelectedDatabase.ToString().Contains(TEXT("ForwardCycle"));
			if (Frame % 12 == 0 || !MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Loop_"))) Report += FString::Printf(TEXT("ReplayFrame Frame=%d Time=%.3f Forward=%d Settled=%d PSD=%s Clip=%s\n"),
				Frame, (Frame - 120) / 120.f, MM.SelectionContext.bUseForwardCycle, MM.SelectionContext.bUseSettledCycle,
				*MM.PostSelection.SelectedDatabase.ToString(), *MM.PostSelection.SelectedAnimation.ToString());
		}
		if (bForwardPoolAudit && Frame >= 360 && Frame < 600 && !Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching())
		{
			ForwardPoolFrames += MM.PostSelection.SelectedDatabase.ToString().Contains(TEXT("ForwardCycle"));
				if (Case == 15 || Case == 16)
			{
				++MicroFrames;
				MicroLoopFrames += MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Loop_"));
			}
		}
		const bool bMeasured = Frame >= 120 && Frame < (bCurveStability ? 720 : 360);
		if (bMeasured)
		{
			if (bLateralCamera)
			{
				Test->TestFalse(TEXT("A/D camera rotation keeps the forward-only query family ineligible"), MM.SelectionContext.bUseForwardCycle);
				LateralBoxFrames += MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Box_"));
				LateralDiamondFrames += MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Diamond_"));
			}
			if (!Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching() && !MM.PostSelection.bIsContinuingPoseSearch)
			{
				++Reselections;
				if (LastJumpFrame >= 0 && (Frame - LastJumpFrame) / 120.f < CoverageBlendTime) ++ShortSegments;
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
			if (bCurveStability && Case <= 12 && Frame >= 240 && Frame < 600 &&
				!Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching())
				SettledCurveFrames += MM.SelectionContext.bUseSettledCycle;
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
			if (bLateralCamera)
			{
				TArray<double> Pose;
				for (const auto& T : Player->GetMesh()->GetBoneSpaceTransforms())
				{
					const auto Q = T.GetRotation(); const auto V = T.GetLocation();
					Pose.Append({Q.X, Q.Y, Q.Z, Q.W, V.X, V.Y, V.Z});
				}
				Report += FString::Printf(TEXT("LateralPose Case=%d Frame=%d Forward=%d PSD=%s Clip=%s Time=%.6f PoseCRC=%u\n"), Case, Frame,
					MM.SelectionContext.bUseForwardCycle, *MM.PostSelection.SelectedDatabase.ToString(), *MM.PostSelection.SelectedAnimation.ToString(),
					MM.PostSelection.SelectedAnimationTime, FCrc::MemCrc32(Pose.GetData(), Pose.Num() * sizeof(double)));
			}
			if (bForwardPoolAudit && FParse::Param(FCommandLine::Get(), TEXT("ProjectJForwardCostAudit")))
			{
				Report += FString::Printf(TEXT("ForwardCost Case=%d Frame=%d Rate=%.2f PSD=%s Selected=%s%s\n"),
					Case, Frame, Player->GetLocomotionAnimStateComponent()->GetContinuousCurveYawRate(),
					*MM.PostSelection.SelectedDatabase.ToString(), *MM.PostSelection.SelectedAnimation.ToString(),
					*Anim->DescribeReconstructedSearchCostsForTesting());
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeLowerBodyFacingTrial")))
			{
				auto* Mesh = Player->GetMesh();
				const auto& CS = Mesh->GetComponentSpaceTransforms();
				const auto& Local = Mesh->GetBoneSpaceTransforms();
				const auto Facing = Anim->GetThreadSafeStrafeLowerBodyFacing();
				bool bSourceFeetChecked = false;
				if (const auto* Interface = IAnimClassInterface::GetFromClass(Anim->GetClass()))
					for (const FStructProperty* Property : Interface->GetAnimNodeProperties())
					{
						if (!Property || Property->Struct != FAnimNode_ProjectJStrafeLowerBodyFacing::StaticStruct()) continue;
						const auto* Node = Property->ContainerPtrToValuePtr<FAnimNode_ProjectJStrafeLowerBodyFacing>(Anim);
						FTransform Left, Right;
						if (!Node->GetValidationSourceFeet(Left, Right)) continue;
						const int32 L = Mesh->GetBoneIndex(TEXT("foot_l")), R = Mesh->GetBoneIndex(TEXT("foot_r"));
						bSourceFeetChecked = CS.IsValidIndex(L) && CS.IsValidIndex(R) && Left.Equals(CS[L], .000001) && Right.Equals(CS[R], .000001);
					}
				Test->TestTrue(TEXT("Both final feet match this evaluation's source before Lower Facing, including excluded OTM startup"), bSourceFeetChecked);
				TArray<double> Values;
				for (const auto& T : Local)
				{
					const auto Q = T.GetRotation(); const auto V = T.GetLocation(); const auto S = T.GetScale3D();
					Values.Append({Q.X, Q.Y, Q.Z, Q.W, V.X, V.Y, V.Z, S.X, S.Y, S.Z});
				}
				Report += FString::Printf(TEXT("LowerBodyPose Case=%d Frame=%d Eligible=%d SourceFeetPreserved=%d AllLocal=%u"), Case, Frame,
					Facing.Facing.bCanRelease, bSourceFeetChecked, FCrc::MemCrc32(Values.GetData(), Values.Num() * sizeof(double)));
				if (Frame == 12 && (Case >= 15 && Case <= 17 || Case == 22))
				{
					Report += TEXT(" AllValues=");
					for (const double Value : Values) Report += FString::Printf(TEXT("%.17g,"), Value);
					Report += TEXT(" AllNames=");
					const auto& Ref = Mesh->GetSkeletalMeshAsset()->GetRefSkeleton();
					for (int32 I = 0; I < Ref.GetNum(); ++I) Report += Ref.GetBoneName(I).ToString() + TEXT(",");
				}
				for (const FName Bone : {FName(TEXT("root")), FName(TEXT("foot_l")), FName(TEXT("foot_r")), FName(TEXT("ik_foot_l")), FName(TEXT("ik_foot_r"))})
				{
					const int32 Index = Mesh->GetBoneIndex(Bone);
					if (!CS.IsValidIndex(Index)) { Test->AddError(TEXT("Lower-body constraint evidence is missing a required bone")); continue; }
					const auto& T = CS[Index]; const auto Q = T.GetRotation(); const auto V = T.GetLocation(); const auto S = T.GetScale3D();
					Report += FString::Printf(TEXT(" %s=%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f"),
						*Bone.ToString(), V.X, V.Y, V.Z, Q.X, Q.Y, Q.Z, Q.W, S.X, S.Y, S.Z);
				}
				Report += TEXT("\n");
			}
			if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeUpperBodyFacingTrial")))
			{
				auto* Mesh = Player->GetMesh();
				const auto* Asset = Mesh->GetSkeletalMeshAsset();
				const auto& Local = Mesh->GetBoneSpaceTransforms();
				const auto& Component = Mesh->GetComponentSpaceTransforms();
				TArray<double> Protected, Feet;
				auto Append = [](TArray<double>& Values, const FTransform& Transform)
				{
					const auto Q = Transform.GetRotation(); const auto P = Transform.GetTranslation(); const auto S = Transform.GetScale3D();
					Values.Append({Q.X, Q.Y, Q.Z, Q.W, P.X, P.Y, P.Z, S.X, S.Y, S.Z});
				};
				if (Asset)
				{
					const auto& Ref = Asset->GetRefSkeleton();
					for (int32 Index = 0; Index < FMath::Min(Local.Num(), Ref.GetNum()); ++Index)
						if (!Ref.GetBoneName(Index).ToString().StartsWith(TEXT("spine_"))) Append(Protected, Local[Index]);
					for (const FName Bone : {FName(TEXT("foot_l")), FName(TEXT("foot_r"))})
					{
						const int32 Index = Ref.FindBoneIndex(Bone);
						if (Component.IsValidIndex(Index)) Append(Feet, Component[Index]);
					}
				}
				Test->TestTrue(TEXT("Protected real-pose evidence includes both feet and other local bones"), Feet.Num() == 20 && Protected.Num() > 20);
				Report += FString::Printf(TEXT("ProtectedPose Case=%d Frame=%d OtherLocal=%u FeetComponent=%u\n"), Case, Frame,
					FCrc::MemCrc32(Protected.GetData(), Protected.Num() * sizeof(double)), FCrc::MemCrc32(Feet.GetData(), Feet.Num() * sizeof(double)));
			}
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
		Frame += FrameStep;
		const int32 EndFrame = CameraReplay.IsEmpty() ? (bCurveStability ? 720 : 360) : ReplayStartFrame + FMath::CeilToInt(CameraReplay.Last().X * 120);
		if (Frame < EndFrame) return false;
		if (!CameraReplay.IsEmpty()) Report += FString::Printf(TEXT("ReplaySummary Frames=%d LoopFrames=%d ForwardFrames=%d BoxDiamondFrames=%d ArcFrames=%d\n"), ReplayFrames, ReplayLoopFrames, ReplayForwardFrames, ReplayBoxDiamondFrames, ReplayArcFrames);
		if (bRecordedCurve)
		{
			const float RecordedFrameCount = CameraReplay.IsEmpty() ? 0.f : CameraReplay.Last().X * 120.f / FrameStep;
			Test->TestTrue(TEXT("At least ninety percent of the recorded curve interval reaches real MM playback"),
				RecordedFrameCount > 0 && ReplayFrames >= RecordedFrameCount * .9f);
			Test->TestTrue(TEXT("The recorded sustained curves predominantly select authored Arc"), ReplayFrames > 0 && ReplayArcFrames >= ReplayFrames * .85f);
			// This recording also contains straight crossings during reversals.
			// Bound unintended Box exposure without banning those assets at runtime.
			Test->TestTrue(TEXT("Box/Diamond exposure remains below one percent in the recorded curve regression"), ReplayFrames > 0 && ReplayBoxDiamondFrames <= ReplayFrames * .01f);
		}
		if (bRecordedMicro)
		{
			if (bReplayPrecurve)
			{
				Report += FString::Printf(TEXT("PrecurveSummary ArcFrames=%d Warmup=%d ObservationDelay=0.5\n"), PrecurveArcFrames, bReplayWarmup);
				Test->TestTrue(TEXT("The previous curve actually selected Arc before the recorded input"), PrecurveArcFrames >= 30 / FrameStep);
			}
			Test->TestTrue(TEXT("Recorded reproduction traverses the real locomotion graph"), ReplayFrames >= 600 / FrameStep);
			Test->TestEqual(TEXT("Recorded micro input never selects Box or Diamond"), ReplayBoxDiamondFrames, 0);
			Test->TestTrue(TEXT("Recorded micro travel predominantly retains Loop"), ReplayFrames > 0 && ReplayLoopFrames >= ReplayFrames * .95f);
		}
		Report += FString::Printf(TEXT("Summary Case=%d GroundFrames=%d MovingFrames=%d ArcFrames=%d\n"), Case, GroundFrames, MovingFrames, ArcFrames);
		if (bLateralCamera)
		{
			Report += FString::Printf(TEXT("LateralSummary Case=%d BoxFrames=%d DiamondFrames=%d\n"), Case, LateralBoxFrames, LateralDiamondFrames);
			TotalLateralBoxFrames += LateralBoxFrames; TotalLateralDiamondFrames += LateralDiamondFrames;
			LateralBoxFrames = LateralDiamondFrames = 0;
		}
		Report += FString::Printf(TEXT("EntrySummary Case=%d Reselections=%d ShortSegments=%d TailEntries=%d\n"), Case, Reselections, ShortSegments, TailEntries);
		Report += FString::Printf(TEXT("GeneralSummary Case=%d CandidateFrames=%d SelectedFrames=%d LateFrames=%d\n"),
			Case, GeneralFrames, SelectedGeneralFrames, LateGeneralFrames);
		Test->TestTrue(TEXT("Measured CMC actually remained grounded"), GroundFrames >= 235 / FrameStep);
		if (bCurveStability)
		{
			Report += FString::Printf(TEXT("CurveSummary Case=%d SettledDuringCurve=%d\n"), Case, SettledCurveFrames);
			if (Case <= 12) Test->TestEqual(TEXT("Actual coherent CMC curves and reversals keep the existing Cycle candidates"), SettledCurveFrames, 0);
			else if (CameraReplay.IsEmpty()) Test->TestTrue(TEXT("Actual pointer noise and isolated nudge settle into Loop candidates"), MM.SelectionContext.bUseSettledCycle);
			if (bForwardPoolAudit)
				Report += FString::Printf(TEXT("ForwardPoolSummary Case=%d PoolFrames=%d MicroLoop=%d MicroFrames=%d\n"), Case, ForwardPoolFrames, MicroLoopFrames, MicroFrames);
			if (bForwardPoolAudit && (Case == 15 || Case == 16) && FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectForwardPool")))
				Test->TestTrue(TEXT("Irregular gentle motion retains Loop throughout the settled observation"), MicroFrames > 0 && MicroLoopFrames == MicroFrames);
			if (bForwardPoolAudit && Case >= 17)
				Test->TestEqual(TEXT("Side/back/diagonal input never selects the forward pool"), ForwardPoolFrames, 0);
			if (bForwardPoolAudit && Case == 11 && FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectForwardPool")))
				Test->TestTrue(TEXT("An inclusive forward pool still selects authored Arc for a sustained 90 degree/s curve"), ArcFrames > 0);
		}
		if (Case < 23 && !bFacingBoundary) Test->TestTrue(TEXT("Measured CMC actually moved"), MovingFrames >= 235 / FrameStep);
		if (bFacingBoundary)
		{
			Report += FString::Printf(TEXT("BoundarySummary Case=%d MontageFrames=%d TurnInPlaceFrames=%d\n"), Case, MontageFrames, TurnInPlaceFrames);
			if (Case == 9) Test->TestTrue(TEXT("Idle camera rotation selected actual TurnInPlace"), TurnInPlaceFrames > 0);
			if (Case == 10) Test->TestTrue(TEXT("Moving Strafe evaluated an actual combat montage"), MontageFrames > 0);
		}
		if (Case >= 23)
		{
			const auto& FinalRequest = Player->GetLocomotionAnimStateComponent()->GetTurnRequestSample();
			Report += FString::Printf(TEXT("FinalCorrection Case=%d VisualValid=%d RemainingFacing=%.3f\n"),
				Case, FinalRequest.bVisualFacingValid, FinalRequest.RemainingFacing);
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
					const auto* Profile = Player->GetLocomotionProfile();
					const float CompletionAngle = Profile ? Profile->GetForwardTurnSettings(
						MM.SelectionContext.RotationMode, MM.SelectionContext.GaitIntent).Resolved().CompletionFacingAngle : 15.f;
					const bool bResolvedByLocomotion = FinalRequest.bVisualFacingValid &&
						FMath::IsFinite(FinalRequest.RemainingFacing) && FMath::Abs(FinalRequest.RemainingFacing) <= CompletionAngle &&
						!MM.PostSelection.SelectedAnimation.IsNone();
					// A live MM search may resolve correction with its current Arc.
					// Verify evaluated completion rather than require a specific asset
					// family after the remaining physical demand has already closed.
					Test->TestTrue(TEXT("Delayed correction selects Turn or resolves evaluated facing within the authored completion limit"),
						AcuteSelectedFrames > 0 || bResolvedByLocomotion);
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
		MontageFrames = TurnInPlaceFrames = 0;
		GeneralFrames = SelectedGeneralFrames = LateGeneralFrames = 0; AccumulatedCameraYaw = 0;
		AcuteFrames = AcuteSelectedFrames = FalseStopFrames = BrakingTurnFrames = PreparedFrames = PreparedEnterFrames = 0;
		MinimumTurnSpeed = TNumericLimits<float>::Max();
		Reselections = ShortSegments = TailEntries = SettledCurveFrames = MicroLoopFrames = MicroFrames = ForwardPoolFrames = 0; LastJumpFrame = -1;
		++Case;
		if (bColdFacingAudit && Case == 18) Case = 22;
		if (SingleCase == INDEX_NONE && Case < (bLateralCamera ? 12 : bForwardPoolAudit ? 20 : bCurveStability ? 15 : bSlowForwardCamera ? 12 : bVerifyRapidTurn ? 57 : bVerifyCorrection ? 23 : bVerifyForwardCurve ? 15 : SchemaTrialSet.IsValid() || bValidateInstalled ? 12 : 9)) return false;
		if (bLateralCamera)
		{
			// A pure side tangent may favour Box over diagonal Diamond. Verify that
			// both remain eligible, and that real matching uses the lateral family.
			Test->TestTrue(TEXT("Real lateral camera curves select authored lateral turns"), TotalLateralBoxFrames + TotalLateralDiamondFrames > 0);
		}
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
	if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeLowerBodyFacingTrial")))
	{
		auto* Master = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
		if (!TestTrue(TEXT("Compile unsaved constrained lower-body trial"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerBodyInMasterForReview(Master)) ||
			!TestTrue(TEXT("Lower-body trial is idempotent"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallLowerBodyInMasterForReview(Master))) return false;
	}
	if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJStrafeUpperBodyFacingTrial")))
	{
		auto* Master = LoadObject<UAnimBlueprint>(nullptr, TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"));
		if (!TestTrue(TEXT("Compile unsaved Master facing trial"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallInMasterForReview(Master)))
			return false;
		if (!TestTrue(TEXT("Master trial installation is idempotent"), UProject_JStrafeUpperBodyFacingEditorLibrary::InstallInMasterForReview(Master)))
			return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStrafeCameraPlayback(this));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRecordedStrafeMicroTest, "ProjectJ.Animation.Naturalness.RecordedStrafeMicro",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRecordedStrafeMicroTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStrafeCameraPlayback(this, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRecordedStrafeCurveTest, "ProjectJ.Animation.Naturalness.RecordedStrafeCurve",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJRecordedStrafeCurveTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStrafeCameraPlayback(this, false, true));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLateralStrafeCameraTest, "ProjectJ.Animation.Naturalness.StrafeLateralCamera",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJLateralStrafeCameraTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStrafeCameraPlayback(this, false, false, true));
	return true;
}
#endif
