#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Testing/Project_JAuthoredAnimationFixture.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JMotionMatchingTrajectoryComponent.h"
#include "Animation/Project_JMotionMatchingAssetSet.h"
#include "Animation/Project_JCombatAnimProfile.h"
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
	TStrongObjectPtr<UProject_JMotionMatchingAssetSet> SchemaTrialSet;
	UProject_JCombatAnimProfile* TrialProfile = nullptr;
	UProject_JMotionMatchingAssetSet* OriginalProfileSet = nullptr;
	bool bValidateInstalled = false;
	bool bVerifyForwardCurve = false;
	bool bVerifyCorrection = false;
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
		const bool bOTM = Case >= 15 && Case <= 17 || Case == 22;
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
		return true;
	}
public:
	explicit FStrafeCameraPlayback(FAutomationTestBase* InTest) : Test(InTest)
	{
		bValidateInstalled = FParse::Param(FCommandLine::Get(), TEXT("ProjectJExpectStrafeDirectionSchema"));
		bVerifyForwardCurve = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyStrafeForwardCurve"));
		bVerifyCorrection = FParse::Param(FCommandLine::Get(), TEXT("ProjectJVerifyCorrectionTurns"));
	}
	virtual ~FStrafeCameraPlayback() override { Cleanup(); }
	virtual bool Update() override
	{
		if (!World && !Setup()) { Test->AddError(TEXT("CMC camera fixture setup failed")); Cleanup(); return true; }
		constexpr float Dt = 1.f / 120;
		const int32 Direction = Case >= 9 ? 3 : Case % 3;
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
		const float MoveOffset = Direction == 0 ? -90 : Direction == 1 ? 180 : Direction == 2 ? 90 : 0;
		if (Case >= 12 && Frame > 120) AccumulatedCameraYaw += Dt * Rate;
		const float CameraYaw = Case >= 12 ? AccumulatedCameraYaw : Frame < 120 ? 0 : (Frame - 120) * Dt * Rate;
		Controller->SetControlRotation(FRotator(0, CameraYaw, 0));
		const FVector Move = FRotator(0, CameraYaw + MoveOffset, 0).Vector();
		const float Radians = FMath::DegreesToRadians(MoveOffset);
		Player->GetLocomotionAnimStateComponent()->SetMoveInput(FVector2D(FMath::Sin(Radians), FMath::Cos(Radians)));
		Player->AddMovementInput(Move);
		TInlineComponentArray<USkeletalMeshComponent*> Meshes(Player);
		for (auto* Mesh : Meshes) Mesh->SetLastRenderTime(World->GetTimeSeconds());
		World->Tick(LEVELTICK_All, Dt);
		auto* Movement = Player->GetCharacterMovement();
		auto* Anim = CastChecked<UProject_JCharacterAnimInstance>(Player->GetMesh()->GetAnimInstance());
		const auto MM = Anim->GetMotionMatchingDebugSnapshot();
		const bool bMeasured = Frame >= 120 && Frame < 360;
		if (bMeasured)
		{
			GroundFrames += Movement->IsMovingOnGround();
			MovingFrames += Movement->Velocity.Size2D() > 100;
			ArcFrames += MM.PostSelection.SelectedAnimation.ToString().Contains(TEXT("_Arc_"));
			GeneralFrames += MM.SelectionContext.bGeneralTurnCandidates;
			const bool bSelectedGeneral = MM.PostSelection.SelectedDatabase.ToString().Contains(TEXT("GeneralTurn"));
			SelectedGeneralFrames += bSelectedGeneral;
			if (Frame >= 300) LateGeneralFrames += MM.SelectionContext.bGeneralTurnCandidates || bSelectedGeneral;
			if (Direction < 3) Test->TestFalse(TEXT("Side/backward strafe does not open forward GeneralTurn"), MM.SelectionContext.bGeneralTurnCandidates);
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
		Report += FString::Printf(TEXT("GeneralSummary Case=%d CandidateFrames=%d SelectedFrames=%d LateFrames=%d\n"),
			Case, GeneralFrames, SelectedGeneralFrames, LateGeneralFrames);
		Test->TestTrue(TEXT("Measured CMC actually remained grounded"), GroundFrames >= 235);
		Test->TestTrue(TEXT("Measured CMC actually moved"), MovingFrames >= 235);
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
		if (++Case < (bVerifyCorrection ? 23 : bVerifyForwardCurve ? 15 : SchemaTrialSet.IsValid() || bValidateInstalled ? 12 : 9)) return false;
		const FString Directory = FPaths::ProjectSavedDir() / TEXT("Validation/StrafeCamera_20261006");
		IFileManager::Get().MakeDirectory(*Directory, true);
		Test->TestTrue(TEXT("Save CMC direction/prediction evidence"), FFileHelper::SaveStringToFile(Report,
			*(Directory / TEXT("Playback.txt")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM));
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
