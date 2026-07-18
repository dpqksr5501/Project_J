#include "Animation/Project_JFlyingMountAnimInstance.h"
#include "Mount/Project_JFlyingMountCharacter.h"
void UProject_JFlyingMountAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);
	if (const AProject_JFlyingMountCharacter* Mount = Cast<AProject_JFlyingMountCharacter>(TryGetPawnOwner()))
	{
		const EProject_JMountFlightState State = Mount->GetFlightState();
		bIsFlying = Mount->IsFlyingMount();
		bIsGliding = Mount->IsGliding();
		bIsTakingOff = State == EProject_JMountFlightState::TakingOff;
		bIsAutoAscending = State == EProject_JMountFlightState::AutoAscending;
		bIsLanding = State == EProject_JMountFlightState::Landing;
	}
}
