#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Animation/Project_JLocomotionProfile.h"
#include "Movement/Project_JMoverPawn.h"
#include "Movement/Project_JMoverInputs.h"
#include "DefaultMovementSet/CharacterMoverSimulationTypes.h"
#include "Project_JSpellbladeCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include <limits>
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Movement/Project_JMoverLifecycle.h"
#include "Movement/Project_JGroundMovement.h"
#include "Movement/Project_JMoverComponent.h"
#include "UObject/StrongObjectPtr.h"
#include "Animation/Project_JPlayerAirTypes.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Components/SkeletalMeshComponent.h"
#include "UObject/UnrealType.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "MoverTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionNativeThresholds,"ProjectJ.PlayerLocomotion.NativeDecisionBoundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerLocomotionNativeThresholds::RunTest(const FString&)
{
	auto* Mesh=NewObject<USkeletalMeshComponent>();auto* Anim=NewObject<UProject_JPlayerAnimInstance>(Mesh);
	auto Future=[&](FVector Velocity)
	{
		FTransformTrajectory Trajectory;
		for (float Time:{0.f,1.f,1.5f}) {auto& Sample=Trajectory.Samples.AddDefaulted_GetRef();Sample.TimeInSeconds=Time;Sample.Position=Velocity*Time;Sample.Facing=FQuat::Identity;}
		Anim->PlayerTrajectory=Trajectory;
	};
	Future(FVector(9.,9.,0.));TestFalse(TEXT("Authored per-axis movement tolerance is preserved"),Anim->EvaluateIsMoving());
	Future(FVector(10.1,0.,0.));TestTrue(TEXT("Future velocity beyond the boundary starts movement"),Anim->EvaluateIsMoving());
	Anim->PlayerVelocity=FVector(50.,0.,0.);Anim->PlayerTrajectoryFutureVelocity=FVector(150.,0.,0.);Anim->PlayerSpeed2D=50.;
	TestTrue(TEXT("A predicted gain of exactly 100 qualifies as starting"),Anim->EvaluateIsStarting());
	Anim->PlayerSpeed2D=100.1;TestFalse(TEXT("A running character cannot select a new start"),Anim->EvaluateIsStarting());
	Anim->PlayerMovementState=EProject_JPlayerMovementState::Idle;Anim->PlayerFutureFacingDelta=50.;Anim->PlayerSpeed2D=49.9;
	TestTrue(TEXT("Idle turn includes the 50 degree facing boundary"),Anim->EvaluateTurnInPlace());
	Anim->PlayerSpeed2D=50.;TestFalse(TEXT("Turn-in-place excludes the moving speed boundary"),Anim->EvaluateTurnInPlace());
	Anim->PlayerSpeed2D=150.;Anim->PlayerFutureFacingDelta=130.;
	TestTrue(TEXT("Spin includes 130 degrees at 150 cm/s"),Anim->EvaluateSpinTransition());
	Anim->PlayerFutureFacingDelta=129.9;TestFalse(TEXT("Spin does not bleed below its threshold"),Anim->EvaluateSpinTransition());
	Anim->PlayerMovementState=EProject_JPlayerMovementState::Moving;Anim->PlayerStance=EProject_JPlayerStance::Stand;
	Anim->PlayerGait=static_cast<EProject_JLocomotionGaitIntent>(int32(2.));Anim->PlayerSpeed2D=300.;Anim->PlayerTrajectoryTurnAngle=60.;
	Anim->PlayerTrajectoryIsCircling=true;
	TestTrue(TEXT("GASP Sprint uses its 60-degree decision even when the retired MM setup field is absent"),Anim->EvaluateIsPivoting());
	Anim->PlayerSpeed2D=199.9;TestFalse(TEXT("Sprint decision excludes speeds below 200"),Anim->EvaluateIsPivoting());
	Anim->PlayerSpeed2D=750.;TestTrue(TEXT("Tuned Sprint top speed remains eligible for pivoting"),Anim->EvaluateIsPivoting());
	Anim->PlayerSpeed2D=865.;TestTrue(TEXT("Sprint preserves 115 cm/s of overspeed headroom"),Anim->EvaluateIsPivoting());
	Anim->PlayerSpeed2D=865.1;TestFalse(TEXT("Sprint excludes speeds above its tuned range"),Anim->EvaluateIsPivoting());
	Anim->PlayerMovementMode=EProject_JPlayerMovementMode::OnGround;
	Anim->PlayerSpeed2D=750.;Anim->PlayerTrajectoryCurrentAngularVelocity=FVector(0,0,400);Anim->PlayerTrajectoryCirclingTime=1.;
	TestEqual(TEXT("Ground cadence does not accelerate with speed or circling"),Anim->EvaluateDynamicPlayRate(FAnimNodeReference()),1.);
	Anim->PlayerGait=static_cast<EProject_JLocomotionGaitIntent>(int32(1.));Anim->PlayerSpeed2D=175.;Anim->PlayerTrajectoryTurnAngle=75.;
	Anim->PlayerMovementModeRecent=EProject_JPlayerMovementMode::OnGround;TestTrue(TEXT("GASP Run keeps its own speed and angle boundary"),Anim->EvaluateIsPivoting());
	Anim->PlayerTrajectoryTurnAngle=74.9;TestFalse(TEXT("Run does not inherit the Sprint angle"),Anim->EvaluateIsPivoting());
	Anim->PlayerGait=static_cast<EProject_JLocomotionGaitIntent>(int32(1.));Anim->RequestLocomotionTransition(false,true,EProject_JLocomotionGaitIntent::Run);
	TestFalse(TEXT("Authored GaitNotEqual Run does not request a redirect while running"),Anim->PlayerNotifyTransitionReTransition);
	Anim->PlayerGait=static_cast<EProject_JLocomotionGaitIntent>(int32(2.));Anim->RequestLocomotionTransition(false,true,EProject_JLocomotionGaitIntent::Run);
	TestTrue(TEXT("A new gait accepts the authored redirect"),Anim->PlayerNotifyTransitionReTransition);
	Anim->PlayerMovementMode=EProject_JPlayerMovementMode::OnGround;Anim->PlayerMovementModeLastFrame=EProject_JPlayerMovementMode::InAir;
	Anim->PlayerHeavyLandSpeedThreshold=-700.;Anim->PlayerVelocityLastFrame=FVector(0.,0.,-700.);
	TestTrue(TEXT("Heavy landing owns the exact impact threshold"),Anim->EvaluateHeavyLanding());TestFalse(TEXT("Landing categories do not overlap"),Anim->EvaluateLightLanding());
	Anim->PlayerVelocityLastFrame=FVector(0.,0.,-699.);TestTrue(TEXT("Lighter impact selects the light landing"),Anim->EvaluateLightLanding());
	float Offset;
	const FQuat Facing=UProject_JGroundMovementLibrary::ResolveGroundFacing(FQuat::Identity,FRotator(0.,180.,0.).Quaternion(),0.f,Offset);
	TestEqual(TEXT("Exact 180 degrees retains the source normalized positive tie"),Offset,180.f);
	TestTrue(TEXT("The original 179 degree bound chooses the same turn direction"),Facing.Rotator().Yaw<0.);
	const FQuat NearFacing=UProject_JGroundMovementLibrary::ResolveGroundFacing(FQuat::Identity,FRotator(0.,179.9,0.).Quaternion(),0.f,Offset);
	TestTrue(TEXT("A near 180 degree request retains its own side"),Offset<0.f && NearFacing.Rotator().Yaw>0.);
    {
        // This exercises the generated native event branch, without changing an authored BP or physics implementation.
        const TStrongObjectPtr<UProject_JMoverComponent> Mover(NewObject<UProject_JMoverComponent>());
        Mover->GroundMovementSettings.bSlopeRelativeVelocity = false;
        const TStrongObjectPtr<UProject_JGroundWalkingMode> Native(NewObject<UProject_JGroundWalkingMode>(Mover.Get()));
        const TStrongObjectPtr<USmoothWalkingMode> Direct(NewObject<USmoothWalkingMode>(Mover.Get()));
        const auto* Hook = Native->FindFunction(TEXT("GenerateWalkMove"));
        TestTrue(TEXT("Inherited walking event resolves to the generated native branch"),Hook && Hook->GetOwnerClass()->HasAnyClassFlags(CLASS_Native));
        const auto* OldDefaults=GetDefault<USmoothWalkingMode>();
        const auto* NewDefaults=GetDefault<UProject_JGroundWalkingMode>();
        for(TFieldIterator<FProperty> It(USmoothWalkingMode::StaticClass(),EFieldIteratorFlags::ExcludeSuper);It;++It)
            TestTrue(TEXT("Native parent retains every inherited SmoothWalking tuning default"),It->Identical_InContainer(OldDefaults,NewDefaults));
        FMoverTickStartData Initial;
        Initial.SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().SetTransforms_WorldSpace(
            FVector::ZeroVector,FRotator::ZeroRotator,FVector(100,0,0),FVector::ZeroVector,nullptr,NAME_None);
        Initial.InputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>().SetMoveInput(EMoveInputType::DirectionalIntent,FVector::ForwardVector);
        FMoverTickStartData NativeState,DirectState; NativeState=Initial;DirectState=Initial;
        FMoverSimContext Context;Context.SimulationOwner=Mover.Get();
        FVector NativeVelocity(100,0,0),DirectVelocity=NativeVelocity,NativeAngular=FVector::ZeroVector,DirectAngular=FVector::ZeroVector;
        const FQuat Desired=FRotator(0,90,0).Quaternion();
        Native->GenerateWalkMove(NativeState,1.f/60.f,Context,FVector(500,0,0),Desired,FQuat::Identity,NativeAngular,NativeVelocity);
        UProject_JGroundMovementLibrary::GenerateGroundMove(Direct.Get(),DirectState,1.f/60.f,Context,FVector(500,0,0),Desired,FQuat::Identity,DirectAngular,DirectVelocity);
        TestTrue(TEXT("Native event delegates exactly to existing velocity and facing solver"),NativeVelocity==DirectVelocity && NativeAngular==DirectAngular);
        TestTrue(TEXT("The real spring solver performed nontrivial work"),NativeVelocity!=FVector(100,0,0) || !NativeAngular.IsZero());
        const auto& A=NativeState.SyncState.SyncStateCollection.GetDataArray();const auto& B=DirectState.SyncState.SyncStateCollection.GetDataArray();
        bool Same=A.Num()==B.Num() && A.Num()>=2;
        for(int32 I=0;Same && I<A.Num();++I)
            Same=A[I] && B[I] && A[I]->GetScriptStruct()==B[I]->GetScriptStruct() && A[I]->GetScriptStruct()->CompareScriptStruct(A[I].Get(),B[I].Get(),PPF_None);
        TestTrue(TEXT("Engine default and private reflected spring values are identical; no shallow collection equality"),Same);
        TestEqual(TEXT("The existing native root-motion tag preserves the dictionary identity"),FGameplayTag(Mover_AnimRootMotion),FGameplayTag::RequestGameplayTag(TEXT("Mover.AnimRootMotion"),false));
        const FFloatProperty* Fields[4];
        int32 FieldIndex=0;
        for (const TCHAR* Name : {TEXT("Acceleration"),TEXT("Deceleration"),TEXT("TurningStrength"),TEXT("FacingSmoothingTime")})
        {
            const auto* Field=FindFProperty<FFloatProperty>(USmoothWalkingMode::StaticClass(),Name);
            if (!TestNotNull(TEXT("Generic SmoothWalking retains its original float schema"),Field)) return false;
            Fields[FieldIndex++]=Field;
            TestTrue(TEXT("Direct fields match the unchanged reflection fallback"),Field->Identical_InContainer(Native.Get(),Direct.Get()));
        }
        // Reuse the same positive-delta native solver for sprint, landing stop and NavMover's velocity bypass.
        for (int32 Case=0;Case<3;++Case)
        {
            FMoverTickStartData NativeEdge,DirectEdge;
            const FVector Prior=Case==0?FVector(Mover->GroundMovementSettings.RunSpeed+100.f,0,0):Case==1?FVector(200,0,0):FVector::ZeroVector;
            const FVector DesiredMove=Case==1?FVector::ZeroVector:FVector(500,0,0);
            for (auto* Edge : {&NativeEdge,&DirectEdge})
            {
                Edge->SyncState.SyncStateCollection.FindOrAddMutableDataByType<FMoverDefaultSyncState>().SetTransforms_WorldSpace(
                    FVector::ZeroVector,FRotator::ZeroRotator,Prior,FVector::ZeroVector,nullptr,NAME_None);
                Edge->InputCmd.InputCollection.FindOrAddMutableDataByType<FCharacterDefaultInputs>().SetMoveInput(
                    Case==2?EMoveInputType::Velocity:EMoveInputType::DirectionalIntent,Case==1?FVector::ZeroVector:FVector::ForwardVector);
                Edge->InputCmd.InputCollection.FindOrAddMutableDataByType<FProject_JMoverInputs>().bSprint=Case==0;
                Edge->SyncState.SyncStateCollection.FindOrAddMutableDataByType<FProject_JMoverLifecycle>().LandingBrakingTimeMs=Case==1?100.f:0.f;
            }
            float Before[4]; for (int32 I=0;I<4;++I) Before[I]=Fields[I]->GetPropertyValue_InContainer(Native.Get());
            const float BeforeLimit=Native->MaxSpeedOverride;
            FVector EdgeVelocity=Prior,FallbackVelocity=Prior,EdgeAngular=FVector::ZeroVector,FallbackAngular=FVector::ZeroVector;
            Native->GenerateWalkMove(NativeEdge,1.f/60.f,Context,DesiredMove,Desired,FQuat::Identity,EdgeAngular,EdgeVelocity);
            UProject_JGroundMovementLibrary::GenerateGroundMove(Direct.Get(),DirectEdge,1.f/60.f,Context,DesiredMove,Desired,FQuat::Identity,FallbackAngular,FallbackVelocity);
            TestTrue(TEXT("Dynamic gait/landing/velocity paths retain the same real solver output"),!EdgeVelocity.ContainsNaN() && !EdgeAngular.ContainsNaN() && EdgeVelocity==FallbackVelocity && EdgeAngular==FallbackAngular);
            TestEqual(TEXT("Both paths retain the same dynamic speed limit"),Native->MaxSpeedOverride,Direct->MaxSpeedOverride);
            for (int32 I=0;I<4;++I)
            {
                TestTrue(TEXT("Every per-step native field matches the generic fallback"),Fields[I]->Identical_InContainer(Native.Get(),Direct.Get()));
                if (Case==2) TestEqual(TEXT("NavMover velocity input bypasses all four tuning writes"),Fields[I]->GetPropertyValue_InContainer(Native.Get()),Before[I]);
            }
            if (Case==1) TestEqual(TEXT("Actual landing-braking input preserves the existing stop policy"),Fields[1]->GetPropertyValue_InContainer(Native.Get()),20000.f);
            if (Case==1) TestTrue(TEXT("Positive-delta landing braking reduces the existing planar speed"),!EdgeVelocity.ContainsNaN() && EdgeVelocity.SizeSquared2D()<Prior.SizeSquared2D());
            if (Case==2) TestEqual(TEXT("NavMover velocity input preserves the existing speed override"),Native->MaxSpeedOverride,BeforeLimit);
            const auto& NativeValues=NativeEdge.SyncState.SyncStateCollection.GetDataArray();
            const auto& FallbackValues=DirectEdge.SyncState.SyncStateCollection.GetDataArray();
            bool SameValues=NativeValues.Num()==FallbackValues.Num();
            for (int32 I=0;SameValues && I<NativeValues.Num();++I)
                SameValues=NativeValues[I] && FallbackValues[I] && NativeValues[I]->GetScriptStruct()==FallbackValues[I]->GetScriptStruct() && NativeValues[I]->GetScriptStruct()->CompareScriptStruct(NativeValues[I].Get(),FallbackValues[I].Get(),PPF_None);
            TestTrue(TEXT("Each dynamic step preserves owned default/lifecycle/private spring values"),SameValues);
        }
    }
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJNativeTransitionClocks,"ProjectJ.PlayerLocomotion.NativeTransitionBoundaries",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJNativeTransitionClocks::RunTest(const FString&)
{
 auto* Mesh=NewObject<USkeletalMeshComponent>();auto* Anim=NewObject<UProject_JPlayerAnimInstance>(Mesh);
 using Rule=EProject_JPlayerTransitionRule;
 auto Evaluate=[&](Rule R,float Clock){return Anim->EvaluateNativeTransition(R,Clock);};
 Anim->PlayerGait=EProject_JLocomotionGaitIntent::Sprint;Anim->PlayerGaitLastFrame=EProject_JLocomotionGaitIntent::Run;
 Anim->PlayerMovementDirectionLastFrame=Anim->PlayerMovementDirection;
 TestFalse(TEXT("Settings changes cannot redirect on the state-entry frame"),Evaluate(Rule::LocomotionSettingsChanged,0.f));
 TestTrue(TEXT("The same settings change redirects using this state's clock"),Evaluate(Rule::LocomotionSettingsChanged,.001f));
 Anim->PlayerMovementState=EProject_JPlayerMovementState::Moving;Anim->PlayerStance=EProject_JPlayerStance::Stand;
 Anim->PlayerSpeed2D=300.;Anim->PlayerTrajectoryTurnAngle=90.;Anim->PlayerMovementModeRecent=EProject_JPlayerMovementMode::OnGround;
 Anim->PlayerBlendStackInputs.Loop=false;Anim->PlayerBlendStackInputs.Tags={TEXT("Pivot")};
 TestFalse(TEXT("Authored pivot cannot interrupt itself at half a second"),Evaluate(Rule::NewPivot,.5f));
 TestTrue(TEXT("A later source-state clock releases the pivot gate"),Evaluate(Rule::NewPivot,.501f));
 TestFalse(TEXT("The transition clock is not shared with a previous call"),Evaluate(Rule::NewPivot,0.f));
 TestFalse(TEXT("A nonlooping transition keeps its own half-second gate"),Evaluate(Rule::TransitionPivot,.5f));
 Anim->PlayerBlendStackInputs.Loop=true;
 TestTrue(TEXT("State-entry selection of a loop is visible immediately to the next predicate"),Evaluate(Rule::TransitionPivot,.001f));
 Anim->PlayerTrajectoryIsCircling=true;
 TestFalse(TEXT("A circling clip cannot take the straight transition pivot"),Evaluate(Rule::TransitionPivot,.6f));
 Anim->PlayerTrajectoryIsCircling=false;Anim->PlayerBlendStackInputs.Loop=false;
 Anim->PlayerTrajectoryFutureFacing=FRotator(0.,61.,0.);Anim->PlayerFutureFacingOnTransitionStart=FRotator::ZeroRotator;
 TestTrue(TEXT("Early facing changes redirect inside the authored window"),Evaluate(Rule::EarlyFacingRedirect,.499f));
 TestFalse(TEXT("Early facing changes cannot extend past the window"),Evaluate(Rule::EarlyFacingRedirect,.5f));
 Anim->PlayerFutureFacingDelta=200.;Anim->PlayerFutureFacingDeltaLastFrame=0.;
 TestFalse(TEXT("Facing seam threshold stays strict"),Evaluate(Rule::TrajectoryDiscontinuity,.1f));
 Anim->PlayerFutureFacingDelta=200.01;
 TestTrue(TEXT("A seam discontinuity above threshold redirects"),Evaluate(Rule::TrajectoryDiscontinuity,.1f));
 Anim->PlayerSlopeAngle=FVector2D(0.,16.);
 TestFalse(TEXT("Slope changes keep the source-state delay"),Evaluate(Rule::SlopeChanged,.75f));
 TestTrue(TEXT("Slope changes release after the authored delay"),Evaluate(Rule::SlopeChanged,.751f));
 Anim->SetNativeTransitionToLoop(true);TestTrue(TEXT("Native notify writer is observed in this evaluation"),Evaluate(Rule::NotifyLoop,0.f));
 Anim->SetNativeReTransition(true);TestTrue(TEXT("Native redirect writer is observed in this evaluation"),Evaluate(Rule::NotifyRedirect,0.f));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJLandingWindowRollback,"ProjectJ.Mover.LandingBrakingRollback",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJLandingWindowRollback::RunTest(const FString&)
{
	FProject_JMoverLifecycle Before,Landing;Landing.LandingSequence=2;Landing.AdvanceLandingBraking(&Before,16.f);
	TestEqual(TEXT("A confirmed new landing opens the braking window"),Landing.LandingBrakingTimeMs,200.f);
	auto Next=Landing;Next.AdvanceLandingBraking(&Landing,16.f);TestEqual(TEXT("Simulation time consumes the window"),Next.LandingBrakingTimeMs,184.f);
	auto Replay=Landing;Replay.AdvanceLandingBraking(&Landing,16.f);TestEqual(TEXT("Rollback replay restores the same remaining duration"),Replay.LandingBrakingTimeMs,Next.LandingBrakingTimeMs);
	TArray<uint8> Bytes;bool Success=false;FMemoryWriter Writer(Bytes);Next.NetSerialize(Writer,nullptr,Success);
	FProject_JMoverLifecycle Received;FMemoryReader Reader(Bytes);Received.NetSerialize(Reader,nullptr,Success);
	TestTrue(TEXT("Landing window serializes with simulation state"),Success);TestFalse(TEXT("The same physical state does not reconcile"),Received.ShouldReconcile(Next));
	Received.LandingBrakingTimeMs=0.f;TestTrue(TEXT("A differing landing window requires reconciliation"),Received.ShouldReconcile(Next));
	Received=Next;Received.AdvanceLandingBraking(&Next,250.f);TestEqual(TEXT("The window expires without a latent callback"),Received.LandingBrakingTimeMs,0.f);
	Received.LandingSequence=4;Received.AdvanceLandingBraking(&Next,16.f);TestEqual(TEXT("A later episode opens its own window"),Received.LandingBrakingTimeMs,200.f);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPlayerLocomotionAirDirection,"ProjectJ.PlayerLocomotion.AirLiveDirectionAndSprint",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJPlayerLocomotionAirDirection::RunTest(const FString&)
{
	using namespace ProjectJ::Animation::Air;
	using Mode=EProject_JLocomotionRotationMode;using Direction=EProject_JStateControllerStrafeDirection;using Gait=EProject_JLocomotionGaitIntent;
 TestEqual(TEXT("OTM jump represents physical travel while steering changes"),ResolveLiveAngle(FVector(375,0,0),true,-90.f,0.f,-90.f,Mode::OrientToMovement),0.f);
 TestEqual(TEXT("OTM updates the clip as its body turns against momentum"),ResolveLiveAngle(FVector(375,0,0),true,0.f,-90.f,-90.f,Mode::OrientToMovement),90.f);
 TestEqual(TEXT("OTM returns to a forward clip after a complete air turn"),ResolveLiveAngle(FVector(-375,0,0),true,0.f,180.f,180.f,Mode::OrientToMovement),0.f);
	TestEqual(TEXT("Without input, OTM keeps physical air momentum"),ResolveLiveAngle(FVector(0,375,0),false,0.f,0.f,0.f,Mode::OrientToMovement),90.f);
	TestEqual(TEXT("Strafe camera turn is visible before the capsule catches up"),ResolveLiveAngle(FVector(375,0,0),true,0.f,0.f,90.f,Mode::Strafe),-90.f);
	TestEqual(TEXT("Strafe preserves drift when the direction keys are released"),ResolveLiveAngle(FVector(375,0,0),false,0.f,0.f,90.f,Mode::Strafe),-90.f);
	FProject_JPlayerAirQuery Committed;Committed.Gait=Gait::Sprint;Committed.Foot=EProject_JStateControllerFoot::Left;
	FProject_JPlayerAirQuery Live=Committed;Live.RotationMode=Mode::Strafe;Live.Foot=EProject_JStateControllerFoot::Right;
	for (float Angle:{-45.f,0.f,45.f}) TestEqual(TEXT("Forward sprint family remains supported"),ResolveReselectionQuery(Live,Committed,Angle).Gait,Gait::Sprint);
	for (float Angle:{-135.f,-90.f,90.f,135.f,180.f})
	{
		const auto Query=ResolveReselectionQuery(Live,Committed,Angle);
		TestEqual(TEXT("Unsupported sprint directions select the authored Run family"),Query.Gait,Gait::Run);
		TestEqual(TEXT("Reselection preserves the physical jump foot"),Query.Foot,Committed.Foot);
		TestEqual(TEXT("The current rotation mode replaces its launch value"),Query.RotationMode,Mode::Strafe);
	}
	Live.Gait=Gait::Run;const auto Released=ResolveReselectionQuery(Live,Committed,-90.f);
	TestEqual(TEXT("Releasing sprint cannot leave the jump locked to launch gait"),Released.Gait,Gait::Run);
	TestEqual(TEXT("The live side direction is published with the new gait"),Released.Direction,Direction::Left);
	for (FVector Input:{FVector(1,0,0),FVector(1,1,0),FVector(1,-1,0)}) TestTrue(TEXT("Strafe forward and forward diagonals allow sprint"),UProject_JGroundMovementLibrary::SupportsStrafeSprint(Input,FVector::ForwardVector));
	for (FVector Input:{FVector(0,1,0),FVector(0,-1,0),FVector(-1,0,0),FVector(-1,1,0),FVector(-1,-1,0),FVector::ZeroVector}) TestFalse(TEXT("Strafe lateral, backward and idle input reject sprint"),UProject_JGroundMovementLibrary::SupportsStrafeSprint(Input,FVector::ForwardVector));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeDirectionPolicyTest,"ProjectJ.PlayerLocomotion.StrafeDirectionPolicy",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeDirectionPolicyTest::RunTest(const FString&)
{
 using D=EProject_JPlayerMovementDirection;
 const FProject_JStrafeDirectionPolicy Policy;
 TestTrue(TEXT("Default sectors are valid authored data"),Policy.IsValid());
 const TPair<float,D> Directions[]={{0.f,D::Forward},{45.f,D::Forward},{-45.f,D::Forward},{90.f,D::RightLeftFootForward},
  {-90.f,D::LeftLeftFootForward},{135.f,D::Backward},{-135.f,D::Backward},{180.f,D::Backward},{-180.f,D::Backward}};
 for(const auto& [Angle,Expected]:Directions)
  for(int32 Previous=0;Previous<6;++Previous)
   for(const float Jitter:{-.1f,0.f,.1f})
   {
    const auto Resolved=Policy.Resolve(Angle+Jitter,static_cast<D>(Previous));
    const bool bSameFamily=Resolved==Expected || (Expected==D::LeftLeftFootForward && Resolved==D::LeftRightFootForward) ||
     (Expected==D::RightLeftFootForward && Resolved==D::RightRightFootForward);
    TestTrue(TEXT("Canonical camera-relative inputs have one family regardless of edge order or small angular noise"),bSameFamily);
   }
 TestEqual(TEXT("Analog motion retains Forward within its boundary band"),Policy.Resolve(70.f,D::Forward),D::Forward);
 TestEqual(TEXT("The same boundary retains Right when approached from the side"),Policy.Resolve(70.f,D::RightLeftFootForward),D::RightLeftFootForward);
 TestEqual(TEXT("Analog motion retains Backward within its boundary band"),Policy.Resolve(110.f,D::Backward),D::Backward);
 TestEqual(TEXT("Leaving the band admits a new sector"),Policy.Resolve(100.f,D::Backward),D::RightLeftFootForward);
 auto* Profile=NewObject<UProject_JLocomotionProfile>();
 TestTrue(TEXT("Profile movement contract accepts its default direction policy"),Profile->GetMovementValidationFailure().IsNone());
 Profile->StrafeDirectionPolicy.HysteresisAngle=30.f;
 TestEqual(TEXT("Overlapping direction bands are rejected by profile validation"),Profile->GetMovementValidationFailure(),FName(TEXT("StrafeDirectionPolicy")));
 Profile->StrafeDirectionPolicy=Policy;Profile->StrafeDirectionPolicy.BackwardStartAngle=std::numeric_limits<float>::quiet_NaN();
 TestEqual(TEXT("Nonfinite direction authoring is rejected"),Profile->GetMovementValidationFailure(),FName(TEXT("StrafeDirectionPolicy")));
 return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJStrafeInputFamilyTest,"ProjectJ.PlayerLocomotion.StrafeInputFamily",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJStrafeInputFamilyTest::RunTest(const FString&)
{
 using D=EProject_JPlayerMovementDirection;
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 ON_SCOPE_EXIT {World->DestroyWorld(false);};
 auto* Pawn=World->SpawnActor<AProject_JMoverPawn>();
 auto* Controller=World->SpawnActor<APlayerController>();Controller->Possess(Pawn);Pawn->SetStrafe(true);
 for(const float View:{-160.f,0.f,75.f})
 {
  Controller->SetControlRotation(FRotator(0.f,View,0.f));
  for(const FVector2D Lead:{FVector2D(0,1),FVector2D(0,-1),FVector2D(1,0),FVector2D(-1,0),FVector2D::ZeroVector})
   for(const float Side:{-1.f,1.f})
   {
    FMoverInputCmdContext Before;Pawn->SetMoveIntent(Lead);Pawn->ProduceInput_Implementation(0,Before);
    FMoverInputCmdContext Final;Pawn->SetMoveIntent(FVector2D(Side,-1));Pawn->ProduceInput_Implementation(0,Final);
    const auto* Intent=Final.InputCollection.FindDataByType<FProject_JMoverInputs>();
    const auto* Input=Final.InputCollection.FindDataByType<FCharacterDefaultInputs>();
    if(!TestNotNull(TEXT("Production input publishes movement intent"),Intent) || !TestNotNull(TEXT("Production input publishes facing"),Input)) return false;
    TestEqual(TEXT("Rear diagonal input reaches the authored Backward chooser family for either key edge order"),Intent->MovementDirection,uint8(D::Backward));
    TestTrue(TEXT("The existing curve keeps the front of the body in the camera-facing hemisphere"),FMath::Abs(Intent->RotationOffset)<90.f);
    TestTrue(TEXT("Strafe orientation still comes from the camera"),FMath::Abs(FMath::FindDeltaAngleDegrees(Input->OrientationIntent.Rotation().Yaw,View))<.001f);
   }
 }
 auto* Player=World->SpawnActor<AProject_JSpellbladeCharacter>();
 auto* Profile=NewObject<UProject_JLocomotionProfile>(Player);
 FindFProperty<FObjectPropertyBase>(Player->GetClass(),TEXT("CharacterAnimProfile"))->SetObjectPropertyValue_InContainer(Player,nullptr);
 FindFProperty<FObjectPropertyBase>(Player->GetClass(),TEXT("LocomotionProfile"))->SetObjectPropertyValue_InContainer(Player,Profile);
 Profile->StrafeDirectionPolicy.ForwardHalfAngle=30.f;Profile->StrafeDirectionPolicy.HysteresisAngle=5.f;
 TestTrue(TEXT("The effective locomotion profile owns player direction authoring"),&Player->GetStrafeDirectionPolicy()==&Profile->StrafeDirectionPolicy);
 TestEqual(TEXT("Authored sector edits reach input classification without changing keys or clips"),Player->GetStrafeDirectionPolicy().Resolve(45.f,D::Forward),D::RightLeftFootForward);
 return true;
}
#endif
