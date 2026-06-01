// Fill out your copyright notice in the Description page of Project Settings.

#include "Project_JLocomotionAnimStateComponent.h"

void UProject_JLocomotionAnimStateComponent::UpdateLocalAirState(bool bIsCurrentlyInAir)
{
	bIsPhysicallyInAir = bIsCurrentlyInAir;
	UpdateLocalAirborneEvidence(bIsCurrentlyInAir);

	if (!TryStartLocalLandingFromJump(bIsCurrentlyInAir) &&
		!TryStartLocalFallOff(bIsCurrentlyInAir) &&
		!TryClearLocalGroundedAirState(bIsCurrentlyInAir) &&
		bIsCurrentlyInAir)
	{
		bIsInAir = true;
	}

	RefreshLocalAirEntryFlags(bIsCurrentlyInAir);
}

void UProject_JLocomotionAnimStateComponent::UpdateLocalAirborneEvidence(bool bIsCurrentlyInAir)
{
	if (bIsCurrentlyInAir)
	{
		LastFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
	}
}

bool UProject_JLocomotionAnimStateComponent::TryStartLocalLandingFromJump(bool bIsCurrentlyInAir)
{
	if (bIsJumping && !bIsCurrentlyInAir && JumpStartElapsedTime >= JumpStartGroundContactGraceTime)
	{
		const float ImpactFallSpeed = VerticalSpeed < 0.0f
			? FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed))
			: LastFallSpeed;
		StartLanding(ImpactFallSpeed, false, true);
		return true;
	}

	return false;
}

bool UProject_JLocomotionAnimStateComponent::TryStartLocalFallOff(bool bIsCurrentlyInAir)
{
	if (!bWasInAir && bIsCurrentlyInAir && !bIsJumping && !bIsLanding && !bSuppressFallOffStart)
	{
		StartFallOffStart();
		return true;
	}

	return false;
}

bool UProject_JLocomotionAnimStateComponent::TryClearLocalGroundedAirState(bool bIsCurrentlyInAir)
{
	if (!bIsCurrentlyInAir && !bIsJumping && !bLandingRequested && !bIsLanding)
	{
		bIsInAir = false;
		bSuppressFallOffStart = false;
		LastFallSpeed = 0.0f;
		return true;
	}

	return false;
}

void UProject_JLocomotionAnimStateComponent::RefreshLocalAirEntryFlags(bool bIsCurrentlyInAir)
{
	bWasInAir = bIsCurrentlyInAir;
	bCanEnterLand = bLandingRequested;
	bCanEnterGround = !bIsInAir && !bIsLanding && !bLandingRequested;
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteAirState(float DeltaTime, bool bIsCurrentlyInAir)
{
	const bool bHadRemoteAirborneEvidence = HasRemoteAirborneEvidence(bWasInAir);

	if (UpdateRemoteJumpStartState(DeltaTime, bIsCurrentlyInAir, bHadRemoteAirborneEvidence))
	{
		return;
	}

	bIsPhysicallyInAir = bIsCurrentlyInAir;
	bIsJumping = false;
	bJumpStartFinishPendingExit = false;
	if (!bIsFallOffStart)
	{
		bSuppressFallOffStart = false;
	}

	if (bIsCurrentlyInAir)
	{
		UpdateRemoteAirborneEvidence(DeltaTime);
	}

	if (!bIsCurrentlyInAir && bHadRemoteAirborneEvidence && !bIsLanding && !bLandingRequested)
	{
		StartLanding(FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed)), false, false);
		bWasInAir = false;
		RemoteAirborneTime = 0.0f;
		return;
	}

	if (bIsCurrentlyInAir)
	{
		bIsInAir = true;
		bIsLanding = false;
		bLandingRequested = false;
		bCanEnterLand = false;
		bCanEnterGround = false;
	}
	else if (!bIsLanding && !bLandingRequested)
	{
		ClearRemoteGroundedAirState();
	}

	bWasInAir = bIsCurrentlyInAir;
}

bool UProject_JLocomotionAnimStateComponent::HasRemoteAirborneEvidence(bool bWasRemoteInAir) const
{
	return
		bWasRemoteInAir ||
		RemoteAirborneTime >= RemoteLandingMinAirTime ||
		LastFallSpeed >= RemoteLandingMinFallSpeed;
}

void UProject_JLocomotionAnimStateComponent::UpdateRemoteAirborneEvidence(float DeltaTime)
{
	RemoteAirborneTime += DeltaTime;
	LastFallSpeed = FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed));
}

void UProject_JLocomotionAnimStateComponent::ClearRemoteGroundedAirState()
{
	bIsInAir = bIsFallOffStart;
	bCanEnterLand = false;
	bCanEnterGround = !bIsFallOffStart;
	RemoteAirborneTime = 0.0f;
	LastFallSpeed = 0.0f;
}

bool UProject_JLocomotionAnimStateComponent::UpdateRemoteJumpStartState(float DeltaTime, bool bIsCurrentlyInAir, bool bHadRemoteAirborneEvidence)
{
	if (!bIsJumping)
	{
		return false;
	}

	bIsPhysicallyInAir = bIsCurrentlyInAir;
	bIsInAir = true;
	bSuppressFallOffStart = false;
	bCanEnterLand = false;
	bCanEnterGround = false;

	if (bIsCurrentlyInAir)
	{
		UpdateRemoteAirborneEvidence(DeltaTime);
	}
	else if (bHadRemoteAirborneEvidence && JumpStartElapsedTime >= JumpStartGroundContactGraceTime && !bIsLanding && !bLandingRequested)
	{
		StartLanding(FMath::Max(LastFallSpeed, FMath::Abs(VerticalSpeed)), false, false);
		bWasInAir = false;
		RemoteAirborneTime = 0.0f;
		return true;
	}

	bWasInAir = bIsCurrentlyInAir;
	return true;
}
