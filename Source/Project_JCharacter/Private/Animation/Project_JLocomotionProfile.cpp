#include "Animation/Project_JLocomotionProfile.h"
#include "Animation/Project_JPlayerLocomotionTypes.h"
#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

bool FProject_JStrafeDirectionPolicy::IsValid() const
{
 return FMath::IsFinite(ForwardHalfAngle) && FMath::IsFinite(BackwardStartAngle) && FMath::IsFinite(HysteresisAngle) &&
  ForwardHalfAngle>=0.f && ForwardHalfAngle<BackwardStartAngle && BackwardStartAngle<=180.f && HysteresisAngle>=0.f &&
  HysteresisAngle<ForwardHalfAngle && HysteresisAngle<180.f-BackwardStartAngle &&
  HysteresisAngle*2.f<BackwardStartAngle-ForwardHalfAngle;
}

EProject_JPlayerMovementDirection FProject_JStrafeDirectionPolicy::Resolve(float SignedAngle,EProject_JPlayerMovementDirection Previous) const
{
 using D=EProject_JPlayerMovementDirection;
 const float Angle=FMath::UnwindDegrees(SignedAngle);
 const float Magnitude=FMath::Abs(Angle);
 // Retain only the previous sector's border band. The same canonical direction
 // outside those bands has one family regardless of the preceding input edge.
 if(Previous==D::Forward && Magnitude<=ForwardHalfAngle+HysteresisAngle) return D::Forward;
 if(Previous==D::Backward && Magnitude>=BackwardStartAngle-HysteresisAngle) return D::Backward;
 const bool bSameSide=Angle<0.f?(Previous==D::LeftLeftFootForward || Previous==D::LeftRightFootForward):
  (Previous==D::RightLeftFootForward || Previous==D::RightRightFootForward);
 if(bSameSide && Magnitude>=ForwardHalfAngle-HysteresisAngle && Magnitude<=BackwardStartAngle+HysteresisAngle) return Previous;
 if(Magnitude<=ForwardHalfAngle) return D::Forward;
 if(Magnitude>=BackwardStartAngle) return D::Backward;
 return Angle<0.f?D::LeftLeftFootForward:D::RightLeftFootForward;
}

FName UProject_JLocomotionProfile::GetMovementValidationFailure() const
{
 if(!StrafeDirectionPolicy.IsValid()) return TEXT("StrafeDirectionPolicy");
 for(const auto& Field : {TPair<FName,float>(TEXT("WalkSpeed"),WalkSpeed), TPair<FName,float>(TEXT("SprintSpeed"),SprintSpeed),
  TPair<FName,float>(TEXT("IdleSpeedThreshold"),TransitionPolicy.IdleSpeedThreshold)})
  if(!FMath::IsFinite(Field.Value) || Field.Value<0.f) return Field.Key;
 return NAME_None;
}
#if WITH_EDITOR
EDataValidationResult UProject_JLocomotionProfile::IsDataValid(FDataValidationContext& Context) const
{
 const auto Base=Super::IsDataValid(Context);
 bool bError=Base==EDataValidationResult::Invalid;
 if(const FName Failure=GetMovementValidationFailure();!Failure.IsNone())
 {
  Context.AddError(FText::FromString(Failure==TEXT("StrafeDirectionPolicy")?
   TEXT("StrafeDirectionPolicy requires ordered finite sectors and nonoverlapping hysteresis bands inside 0..180 degrees."):
   FString::Printf(TEXT("%s must be finite and nonnegative."),*Failure.ToString())));
  bError=true;
 }
 const auto Check=[&](float Value,float Minimum,float Maximum,const TCHAR* Name)
 {
  if(!FMath::IsFinite(Value) || Value<Minimum || Value>Maximum)
  { Context.AddError(FText::FromString(FString::Printf(TEXT("AirPolicy.%s is outside its supported range."),Name)));bError=true; }
 };
 Check(AirPolicy.AirJumpReselectCooldown,.01f,TNumericLimits<float>::Max(),TEXT("AirJumpReselectCooldown"));
 Check(AirPolicy.AirJumpBlendTime,.05f,.5f,TEXT("AirJumpBlendTime"));
 Check(AirPolicy.AirJumpMinBlendProgressRatio,.3f,1.f,TEXT("AirJumpMinBlendProgressRatio"));
 Check(AirPolicy.AirJumpMaxProgressRatio,.1f,1.f,TEXT("AirJumpMaxProgressRatio"));
 Check(AirPolicy.OTMJumpLaunchAngleThreshold,10.f,90.f,TEXT("OTMJumpLaunchAngleThreshold"));
 Check(AirPolicy.RemoteJumpUrgentAnimationUpdateDuration,0.f,TNumericLimits<float>::Max(),TEXT("RemoteJumpUrgentAnimationUpdateDuration"));
 return Project_J::DataValidation::MakeResult(Base,bError);
}
#endif
