#include "Project_JPlayerCharacter.h"

#if !UE_BUILD_SHIPPING
#include "Animation/AnimationAsset.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPresentationMeshResolver.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Project_JLocomotionAnimStateComponent.h"
#include "Project_JLocomotionDebugUtils.h"

namespace
{
TAutoConsoleVariable<int32> CVarMouseTurnTrace(TEXT("p.ProjectJ.MouseTurnTrace"), 0,
	TEXT("Local player mouse/camera/animation diagnosis. 0=off, 1=10Hz summaries with per-frame peaks, 2=every player tick. Prefix MouseTurnTrace. Read-only; camera cache is timestamped, not a same-frame final-render sample."));

int32 ReadDebugMode(const TCHAR* Name)
{
	const IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(Name);
	return Var ? Var->GetInt() : 0;
}
FString BoneYaw(USkeletalMeshComponent* Mesh, FName Bone)
{
	if (!Mesh) return TEXT("NoMesh");
	if (Mesh->IsRunningParallelEvaluation()) return TEXT("Pending");
	if (Mesh->GetBoneIndex(Bone) == INDEX_NONE) return TEXT("MissingBone");
	return FString::Printf(TEXT("%.3f"), Mesh->GetBoneQuaternion(Bone, EBoneSpaces::WorldSpace).Rotator().Yaw);
}
}

void AProject_JPlayerCharacter::RecordMouseTurnLookInput(const FVector2D& EnhancedLookAxis)
{
	if (CVarMouseTurnTrace.GetValueOnGameThread() <= 0) return;
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController() || !IsLocallyControlled()) return;
	if (MouseTurnTrace.InputFrame != GFrameCounter)
	{
		MouseTurnTrace.InputFrame = GFrameCounter;
		MouseTurnTrace.LookCalls = 0;
		MouseTurnTrace.EnhancedLook = FVector2D::ZeroVector;
	}
	++MouseTurnTrace.LookCalls;
	MouseTurnTrace.EnhancedLook += EnhancedLookAxis;
	// Observed after AddControllerYaw/PitchInput, before PC::UpdateRotation may
	// consume/reset RotationInput. Enhanced axes are not raw hardware counts.
	MouseTurnTrace.QueuedRotation = PC->RotationInput;
}

void AProject_JPlayerCharacter::RecordMouseTurnFrame(float DeltaTime)
{
	const int32 Mode = FMath::Clamp(CVarMouseTurnTrace.GetValueOnGameThread(), 0, 2);
	if (!Mode)
	{
		if (MouseTurnTrace.Mode) UE_LOG(LogProjectJPlayer, Display, TEXT("MouseTurnTrace End Frame=%llu Actor=%s"), GFrameCounter, *GetName());
		UnregisterMouseTurnCameraTrace();
		if (MouseTurnTrace.Mode || MouseTurnTrace.InputFrame != MAX_uint64) MouseTurnTrace = {};
		return;
	}
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC || !PC->IsLocalController() || !IsLocallyControlled())
	{
		UnregisterMouseTurnCameraTrace();
		if (MouseTurnTrace.Mode || MouseTurnTrace.InputFrame != MAX_uint64) MouseTurnTrace = {};
		return;
	}
	const double WallNow = FPlatformTime::Seconds();
	if (MouseTurnTrace.Mode != Mode)
	{
		MouseTurnTrace.Mode = Mode;
		MouseTurnTrace.bHasPrevious = false;
		MouseTurnTrace.bHasPostCamera = false;
		MouseTurnTrace.LastEmitSeconds = -1.0;
		MouseTurnTrace.LastWallSeconds = 0.0;
		MouseTurnTrace.WindowFrames = 0;
		MouseTurnTrace.PeakWallMs = MouseTurnTrace.PeakWorldDtMs = 0.0f;
		MouseTurnTrace.PeakControlStep = MouseTurnTrace.PeakActorStep = MouseTurnTrace.PeakCameraStep = 0.0f;
		UE_LOG(LogProjectJPlayer, Display,
			TEXT("MouseTurnTrace Begin Actor=%s Mode=%d Stage=AfterPlayerTick CameraStage=LastAvailablePCViewCache MovingTurnTrace=%d TIPTrace=%d AnimFlow=%d MMNetDebug=%d WorldRole=%d"),
			*GetName(), Mode, ReadDebugMode(TEXT("p.ProjectJ.MovingTurnTrace")), ReadDebugMode(TEXT("p.ProjectJ.TIPTrace")),
			ReadDebugMode(TEXT("p.ProjectJ.AnimFlow")), ReadDebugMode(TEXT("p.ProjectJ.MMNetDebug")), int32(GetLocalRole()));
	}
	if (!MouseTurnCameraTraceHandle.IsValid())
	{
		// Bind only while this local diagnostic is enabled. UE 5.8 broadcasts
		// this existing world delegate after UpdateCameraManager. Do not change
		// actor/component tick groups or camera behavior to obtain a later sample.
		MouseTurnCameraTraceHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &AProject_JPlayerCharacter::RecordMouseTurnPostCamera);
	}
	const bool bHasCamera = PC->PlayerCameraManager != nullptr;
	const float CameraTime = bHasCamera ? PC->PlayerCameraManager->GetCameraCacheTime() : -1.0f;
	const FMinimalViewInfo EmptyCamera;
	const FMinimalViewInfo& Camera = bHasCamera ? PC->PlayerCameraManager->GetCameraCacheView() : EmptyCamera;
	const float ControlYaw = PC->GetControlRotation().Yaw;
	const float ActorYaw = GetActorRotation().Yaw;
	const float WallMs = MouseTurnTrace.LastWallSeconds > 0 ? float((WallNow - MouseTurnTrace.LastWallSeconds) * 1000.0) : 0.0f;
	const float ControlStep = MouseTurnTrace.bHasPrevious ? FMath::FindDeltaAngleDegrees(MouseTurnTrace.ControlYaw, ControlYaw) : 0.0f;
	const float ActorStep = MouseTurnTrace.bHasPrevious ? FMath::FindDeltaAngleDegrees(MouseTurnTrace.ActorYaw, ActorYaw) : 0.0f;
	const float CameraStep = MouseTurnTrace.bHasPrevious && bHasCamera ? FMath::FindDeltaAngleDegrees(MouseTurnTrace.CameraYaw, Camera.Rotation.Yaw) : 0.0f;
	const bool bCameraUpdated = MouseTurnTrace.bHasPrevious && bHasCamera && CameraTime != MouseTurnTrace.CameraTime;
	++MouseTurnTrace.WindowFrames;
	if (WallMs >= MouseTurnTrace.PeakWallMs) { MouseTurnTrace.PeakWallMs = WallMs; MouseTurnTrace.PeakWallFrame = GFrameCounter; }
	MouseTurnTrace.PeakWorldDtMs = FMath::Max(MouseTurnTrace.PeakWorldDtMs, DeltaTime * 1000.0f);
	MouseTurnTrace.PeakControlStep = FMath::Max(MouseTurnTrace.PeakControlStep, FMath::Abs(ControlStep));
	MouseTurnTrace.PeakActorStep = FMath::Max(MouseTurnTrace.PeakActorStep, FMath::Abs(ActorStep));
	MouseTurnTrace.PeakCameraStep = FMath::Max(MouseTurnTrace.PeakCameraStep, FMath::Abs(CameraStep));
	MouseTurnTrace.LastWallSeconds = WallNow;
	MouseTurnTrace.ControlYaw = ControlYaw;
	MouseTurnTrace.ActorYaw = ActorYaw;
	MouseTurnTrace.CameraYaw = Camera.Rotation.Yaw;
	MouseTurnTrace.CameraTime = CameraTime;
	MouseTurnTrace.bHasPrevious = true;
	if (Mode == 1 && MouseTurnTrace.LastEmitSeconds >= 0 && WallNow - MouseTurnTrace.LastEmitSeconds < 0.1) return;

	const double WorldTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	const bool bInputThisFrame = MouseTurnTrace.InputFrame == GFrameCounter;
	float MouseX = 0.0f, MouseY = 0.0f; PC->GetInputMouseDelta(MouseX, MouseY);
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("MouseTurnTrace Frame=%llu T=%.4f Actor=%s WorldDtMs=%.3f EngineDtMs=%.3f TickGapMs=%.3f LookFrame=%llu LookCalls=%d EnhancedLook=(%.3f,%.3f) QueuedYaw=%.3f QueuedPitch=%.3f PCMouse=(%.3f,%.3f) LookIgnored=%d ControlYaw=%.3f ControlStep=%.3f ActorYaw=%.3f ActorStep=%.3f CameraManager=%d CameraT=%.4f CameraAgeMs=%.3f CameraUpdated=%d CameraYaw=%.3f CameraPitch=%.3f CameraStep=%.3f CameraPos=%s ViewTarget=%s WindowFrames=%d PeakTickGapMs=%.3f PeakGapFrame=%llu PeakWorldDtMs=%.3f PeakControlStep=%.3f PeakActorStep=%.3f PeakCameraStep=%.3f"),
		GFrameCounter, WorldTime, *GetName(), DeltaTime * 1000.0f, FApp::GetDeltaTime() * 1000.0, WallMs,
		MouseTurnTrace.InputFrame, bInputThisFrame ? MouseTurnTrace.LookCalls : 0,
		bInputThisFrame ? MouseTurnTrace.EnhancedLook.X : 0.0, bInputThisFrame ? MouseTurnTrace.EnhancedLook.Y : 0.0,
		bInputThisFrame ? MouseTurnTrace.QueuedRotation.Yaw : 0.0f, bInputThisFrame ? MouseTurnTrace.QueuedRotation.Pitch : 0.0f,
		MouseX, MouseY, PC->IsLookInputIgnored(), ControlYaw, ControlStep, ActorYaw, ActorStep, bHasCamera,
		CameraTime, bHasCamera ? (WorldTime - CameraTime) * 1000.0 : -1.0, bCameraUpdated, Camera.Rotation.Yaw, Camera.Rotation.Pitch,
		CameraStep, *Camera.Location.ToString(), *GetNameSafe(PC->GetViewTarget()), MouseTurnTrace.WindowFrames,
		MouseTurnTrace.PeakWallMs, MouseTurnTrace.PeakWallFrame, MouseTurnTrace.PeakWorldDtMs,
		MouseTurnTrace.PeakControlStep, MouseTurnTrace.PeakActorStep, MouseTurnTrace.PeakCameraStep);

	const auto* Move = GetCharacterMovement();
	const auto* State = GetLocomotionAnimStateComponent();
	const auto* Anim = GetMesh() ? Cast<UProject_JCharacterAnimInstance>(GetMesh()->GetAnimInstance()) : nullptr;
	const auto MM = Anim ? Anim->GetMotionMatchingDebugSnapshot() : FProject_JAnimMotionMatchingThreadSafeData();
	USkeletalMeshComponent* Visual = Project_J::Animation::FindVisualFollower(*this);
	const FName VisualRoot = Visual && Visual->GetNumBones() > 0 ? Visual->GetBoneName(0) : NAME_None;
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("MouseTurnTrace State Frame=%llu Actor=%s Combat=%d Speed=%.2f Falling=%d OrientToMove=%d ControllerDesired=%d ControllerYaw=%d YawRate=%.2f Phase=%s Turn180=%d TIPTarget=%d FacingYaw=%.3f Present=%d SelectRev=%d Override=%d ExternalClip=%s EntryTime=%.3f HoldTime=%.3f CachedPSD=%s CachedClip=%s CachedTime=%.3f SourcePending=%d SourceMeshYaw=%.3f SourceRootYaw=%s VisualMesh=%s VisualPending=%d VisualMeshYaw=%.3f VisualRootBone=%s VisualRootYaw=%s"),
		GFrameCounter, *GetName(), IsCombatModeActive(), GetVelocity().Size2D(), Move && Move->IsFalling(),
		Move && Move->bOrientRotationToMovement, Move && Move->bUseControllerDesiredRotation, bUseControllerRotationYaw,
		Move ? Move->RotationRate.Yaw : 0.0f, State ? Project_J::LocomotionDebug::ToDebugString(State->DerivedLocomotionContext.PhaseFamily) : TEXT("None"),
		State && State->DerivedLocomotionContext.bIsMovingTurn180, State && State->IsLocalTurnInPlaceTargetActive(),
		Anim ? Anim->GetThreadSafeStateControllerDesiredFacingRotator().Yaw : 0.0f,
		Anim ? int32(Anim->GetThreadSafeStateControllerPresentationState()) : -1,
		Anim ? Anim->GetThreadSafeStateControllerSelectionRevision() : -1, Anim && Anim->GetThreadSafeStateControllerShouldOverrideMotionMatching(),
		*GetNameSafe(Anim ? Anim->GetThreadSafeStateControllerSelectedAnimation() : nullptr),
		Anim ? Anim->GetThreadSafeStateControllerSelectedAnimationStartTime() : 0.0f,
		Anim ? Anim->GetThreadSafeStateControllerPlaybackHoldElapsedTime() : 0.0f,
		*MM.PostSelection.SelectedDatabase.ToString(), *MM.PostSelection.SelectedAnimation.ToString(), MM.PostSelection.SelectedAnimationTime,
		GetMesh() && GetMesh()->IsRunningParallelEvaluation(), GetMesh() ? GetMesh()->GetComponentRotation().Yaw : 0.0f,
		*BoneYaw(GetMesh(), TEXT("root")), *GetNameSafe(Visual), Visual && Visual->IsRunningParallelEvaluation(),
		Visual ? Visual->GetComponentRotation().Yaw : 0.0f, *VisualRoot.ToString(), *BoneYaw(Visual, VisualRoot));
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("MouseTurnTrace CameraRig Frame=%llu Actor=%s BoomValid=%d BoomTargetYaw=%.3f BoomArm=%.2f CollisionTest=%d CollisionFix=%d PositionLag=%d PositionLagSpeed=%.3f RotationLag=%d RotationLagSpeed=%.3f Substep=%d LagMaxStep=%.4f FollowYaw=%.3f FollowPos=%s"),
		GFrameCounter, *GetName(), CameraBoom != nullptr, CameraBoom ? CameraBoom->GetTargetRotation().Yaw : 0.0f,
		CameraBoom ? CameraBoom->TargetArmLength : 0.0f, CameraBoom && CameraBoom->bDoCollisionTest,
		CameraBoom && CameraBoom->IsCollisionFixApplied(), CameraBoom && CameraBoom->bEnableCameraLag,
		CameraBoom ? CameraBoom->CameraLagSpeed : 0.0f, CameraBoom && CameraBoom->bEnableCameraRotationLag,
		CameraBoom ? CameraBoom->CameraRotationLagSpeed : 0.0f, CameraBoom && CameraBoom->bUseCameraLagSubstepping,
		CameraBoom ? CameraBoom->CameraLagMaxTimeStep : 0.0f, FollowCamera ? FollowCamera->GetComponentRotation().Yaw : 0.0f,
		FollowCamera ? *FollowCamera->GetComponentLocation().ToString() : TEXT("NoCamera"));
	MouseTurnTrace.LastEmitSeconds = WallNow;
	MouseTurnTrace.LastEmittedFrame = GFrameCounter;
	MouseTurnTrace.WindowFrames = 0;
	MouseTurnTrace.PeakWallMs = MouseTurnTrace.PeakWorldDtMs = 0.0f;
	MouseTurnTrace.PeakControlStep = MouseTurnTrace.PeakActorStep = MouseTurnTrace.PeakCameraStep = 0.0f;
}

void AProject_JPlayerCharacter::UnregisterMouseTurnCameraTrace()
{
	if (MouseTurnCameraTraceHandle.IsValid())
	{
		FWorldDelegates::OnWorldPostActorTick.Remove(MouseTurnCameraTraceHandle);
		MouseTurnCameraTraceHandle.Reset();
	}
}

void AProject_JPlayerCharacter::RecordMouseTurnPostCamera(UWorld* World, ELevelTick TickType, float DeltaTime)
{
	if (World != GetWorld()) return;
	const int32 Mode = FMath::Clamp(CVarMouseTurnTrace.GetValueOnGameThread(), 0, 2);
	const APlayerController* PC = Cast<APlayerController>(GetController());
	if (!Mode || !PC || !PC->IsLocalController() || !IsLocallyControlled()) { UnregisterMouseTurnCameraTrace(); return; }
	const APlayerCameraManager* Manager = PC->PlayerCameraManager;
	const float CameraTime = Manager ? Manager->GetCameraCacheTime() : -1.0f;
	const FMinimalViewInfo EmptyCamera;
	const FMinimalViewInfo& Camera = Manager ? Manager->GetCameraCacheView() : EmptyCamera;
	const float CameraStep = MouseTurnTrace.bHasPostCamera && Manager
		? FMath::FindDeltaAngleDegrees(MouseTurnTrace.PostCameraYaw, Camera.Rotation.Yaw) : 0.0f;
	MouseTurnTrace.PostCameraYaw = Camera.Rotation.Yaw;
	MouseTurnTrace.bHasPostCamera = Manager != nullptr;
	if (Mode == 1 && MouseTurnTrace.LastEmittedFrame != GFrameCounter) return;
	UE_LOG(LogProjectJPlayer, Display,
		TEXT("MouseTurnTrace PostCamera Frame=%llu T=%.4f Actor=%s Stage=AfterWorldCameraUpdate CameraManager=%d CameraT=%.4f CameraAgeMs=%.3f UpdatedSincePlayerTick=%d ControlYaw=%.3f CameraYaw=%.3f CameraPitch=%.3f CameraStep=%.3f ControlCameraError=%.3f CameraPos=%s ActorYaw=%.3f FollowYaw=%.3f BoomTick=%d BoomInterval=%.4f BoomPawnControl=%d BoomInheritYaw=%d ActorInterval=%.4f FollowActive=%d"),
		GFrameCounter, double(World->GetTimeSeconds()), *GetName(), Manager != nullptr, CameraTime,
		Manager ? (World->GetTimeSeconds() - CameraTime) * 1000.0 : -1.0, Manager && CameraTime != MouseTurnTrace.CameraTime,
		PC->GetControlRotation().Yaw, Camera.Rotation.Yaw, Camera.Rotation.Pitch, CameraStep,
		Manager ? FMath::FindDeltaAngleDegrees(Camera.Rotation.Yaw, PC->GetControlRotation().Yaw) : 0.0f,
		*Camera.Location.ToString(), GetActorRotation().Yaw, FollowCamera ? FollowCamera->GetComponentRotation().Yaw : 0.0f,
		CameraBoom && CameraBoom->IsComponentTickEnabled(), CameraBoom ? CameraBoom->PrimaryComponentTick.TickInterval : 0.0f,
		CameraBoom && CameraBoom->bUsePawnControlRotation, CameraBoom && CameraBoom->bInheritYaw,
		PrimaryActorTick.TickInterval, FollowCamera && FollowCamera->IsActive());
}
#endif
