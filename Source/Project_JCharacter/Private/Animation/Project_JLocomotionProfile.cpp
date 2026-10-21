#include "Animation/Project_JLocomotionProfile.h"
#if WITH_EDITOR
#include "Validation/Project_JDataValidation.h"
#endif

FName UProject_JLocomotionProfile::GetMovementValidationFailure() const
{
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
  Context.AddError(FText::FromString(FString::Printf(TEXT("%s must be finite and nonnegative."),*Failure.ToString())));
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
