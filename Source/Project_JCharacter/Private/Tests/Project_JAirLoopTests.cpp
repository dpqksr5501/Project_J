#include "Animation/Project_JLocomotionProfile.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JPlayerAirTypes.h"
#include "Animation/AnimSequenceBase.h"
#include "Chooser.h"
#include "ChooserTypes.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAirLoopOutputTest, "ProjectJ.PlayerLocomotion.AirChooserCoverage",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAirLoopOutputTest::RunTest(const FString&)
{
 UClass* Class=LoadClass<UProject_JPlayerAnimInstance>(nullptr,TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master.ABP_Player_Master_C"));
 if(!TestNotNull(TEXT("Current foundation class"),Class))return false;
 const auto* Defaults=Class->GetDefaultObject<UProject_JPlayerAnimInstance>();
 auto* Chooser=Defaults->AirAnimationChooser.Get();if(!TestNotNull(TEXT("Production typed air chooser"),Chooser))return false;
 using namespace ProjectJ::Animation::Air;
 using Mode=EProject_JLocomotionRotationMode;using State=EProject_JStateControllerPresentationState;
 using Gait=EProject_JLocomotionGaitIntent;using Foot=EProject_JStateControllerFoot;
 for(auto ModeValue:{Mode::OrientToMovement,Mode::Strafe})
 for(auto GaitValue:{Gait::Walk,Gait::Run,Gait::Sprint})
 for(auto FootValue:{Foot::Left,Foot::Right})
 for(auto StateValue:{State::TransitionToInAir,State::InAirLoop})
 for(bool bEntry:{false,true})
 for(float Angle:{-135.f,-90.f,-45.f,0.f,45.f,90.f,135.f,180.f})
 {
  FProject_JPlayerAirQuery Live;Live.RotationMode=ModeValue;Live.Gait=GaitValue;Live.Foot=FootValue;Live.State=StateValue;
  auto Query=ResolveReselectionQuery(Live,Live,Angle);
  if(bEntry) { Query.Gait=GaitValue; Query=ResolveEntryQuery(Query); }
  FChooserPlayerSettings Settings;FProject_JStateControllerChooserOutput Output;
  FChooserEvaluationContext Context;Context.AddStructParam(Query);Context.AddStructParam(Settings);Context.AddStructParam(Output);
  UAnimSequenceBase* Selected=nullptr;
  UChooserTable::EvaluateChooser(Context,Chooser,FObjectChooserBase::FObjectChooserIteratorCallback::CreateLambda([&](UObject* Object)
  {Selected=Cast<UAnimSequenceBase>(Object);return Selected?FObjectChooserBase::EIteratorStatus::Stop:FObjectChooserBase::EIteratorStatus::Failed;}));
  const FString Case=FString::Printf(TEXT("Mode=%d Gait=%d Foot=%d State=%d Angle=%.0f"),int32(ModeValue),int32(GaitValue),int32(FootValue),int32(StateValue),Angle);
  if(!TestNotNull(*Case,Selected))continue;
  TestTrue(TEXT("Entry time is valid for the actual clip"),Output.StartTime>=0.f && Output.StartTime<Selected->GetPlayLength());
  TestTrue(TEXT("Blending metadata is finite and nonnegative"),FMath::IsFinite(Output.BlendTime) && Output.BlendTime>=0.f);
  TestEqual(TEXT("Directional reselection retains the launch foot"),Query.Foot,FootValue);
 }
 TestTrue(TEXT("Distinct authored transition and loop states remain installed"),EProject_JPlayerPlaybackState::AirTransition!=EProject_JPlayerPlaybackState::AirLoop);
 return true;
}
#endif
