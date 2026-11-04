#include "World/Project_JWorldDefinition.h"
#include "World/Project_JWorldArrivalSubsystem.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UProject_JWorldDefinition::AreStreamingSettingsValid(float Seconds, float Radius, float Maximum)
{
    return FMath::IsFinite(Seconds) && Seconds >= 0 && Seconds <= 10 &&
        FMath::IsFinite(Radius) && Radius >= 1000 && Radius <= 200000 &&
        FMath::IsFinite(Maximum) && Maximum >= 0 && Maximum <= 200000 &&
        Maximum <= Radius * (MaxPreparationShapes - 1);
}
bool UProject_JWorldDefinition::IsClockTuningValid() const
{
    return FMath::IsFinite(DayLengthSeconds) && DayLengthSeconds >= 60 &&
        FMath::IsFinite(InitialHour) && InitialHour >= 0 && InitialHour <= 24;
}
bool UProject_JWorldDefinition::IsPreparedArrivalAdmissionValid() const
{
    return MaxSimultaneousPreparedArrivals >= 1 && MaxSimultaneousPreparedArrivals <= UProject_JWorldArrivalSubsystem::MaxPending;
}
#if WITH_EDITOR
EDataValidationResult UProject_JWorldDefinition::IsDataValid(FDataValidationContext& Context) const
{
    const auto Base = Super::IsDataValid(Context);
    bool bValid = Base != EDataValidationResult::Invalid;
    if (!IsTemplate() && RegionId.IsNone())
    { Context.AddError(FText::FromString(TEXT("World definition requires a stable RegionId."))); bValid = false; }
    if (!IsClockTuningValid())
    { Context.AddError(FText::FromString(TEXT("World clock requires finite day length >=60s and hour in [0,24]."))); bValid = false; }
    if (!IsPreparedArrivalAdmissionValid())
    { Context.AddError(FText::FromString(TEXT("Prepared arrival concurrency must be within the existing 1..256 request capacity."))); bValid = false; }
    if (!AreStreamingSettingsValid(PredictionSeconds, PreparationRadius, MaxPredictionDistance))
    { Context.AddError(FText::FromString(TEXT("Streaming requires finite bounded tuning and prediction coverage within eight overlapping spheres."))); bValid = false; }
    return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
