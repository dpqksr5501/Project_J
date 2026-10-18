#include "World/Project_JWorldState.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Net/UnrealNetwork.h"

AProject_JWorldState::AProject_JWorldState()
{
    bReplicates = true;
    bAlwaysRelevant = true;
    SetNetUpdateFrequency(2);
#if WITH_EDITORONLY_DATA
    bIsSpatiallyLoaded = false;
#endif
}
void AProject_JWorldState::BeginPlay()
{
    Super::BeginPlay();
    if (HasAuthority()) { ClockStart = GetSynchronizedSeconds(); ForceNetUpdate(); }
}
double AProject_JWorldState::GetSynchronizedSeconds() const
{
    const AGameStateBase* State = GetWorld()->GetGameState();
    return State ? State->GetServerWorldTimeSeconds() : GetWorld()->GetTimeSeconds();
}
float AProject_JWorldState::EvaluateRain(const FProject_JEnvironmentState& State, double Now)
{
    const double Alpha = State.TransitionSeconds > 0 ? FMath::Clamp((Now-State.TransitionStart)/State.TransitionSeconds, 0.0, 1.0) : 1.0;
    return FMath::Lerp(State.FromRain, State.ToRain, static_cast<float>(Alpha));
}
bool AProject_JWorldState::SetRain(float Target, float Duration)
{
    if (!HasAuthority() || !FMath::IsFinite(Target) || !FMath::IsFinite(Duration)) { return false; }
    const double Now = GetSynchronizedSeconds();
    Environment.FromRain = EvaluateRain(Environment, Now);
    Environment.ToRain = FMath::Clamp(Target, 0.f, 1.f);
    Environment.TransitionStart = Now;
    Environment.TransitionSeconds = FMath::Clamp(Duration, 0.f, 600.f);
    ++Environment.Revision;
    ForceNetUpdate();
    return true;
}
float AProject_JWorldState::GetRain() const { return EvaluateRain(Environment, GetSynchronizedSeconds()); }
float AProject_JWorldState::GetHour() const
{
    const float Duration = Definition ? FMath::Max(60.f, Definition->DayLengthSeconds) : 2400.f;
    const double Hour = (Definition ? Definition->InitialHour : 10.f) + FMath::Max(0.0, GetSynchronizedSeconds()-ClockStart)*24.0/Duration;
    return static_cast<float>(FMath::Fmod(Hour, 24.0));
}
void AProject_JWorldState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
    Super::GetLifetimeReplicatedProps(OutLifetimeProps);
    DOREPLIFETIME(AProject_JWorldState, Environment);
    DOREPLIFETIME(AProject_JWorldState, ClockStart);
}
