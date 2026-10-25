#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Project_JRenderedCrowdFixture.generated.h"
class AProject_JPlayerCharacter;
class USkeletalMesh;

/** Explicit non-shipping diagnostic; actual viewport/RHI, authored Blueprint bodies. */
UCLASS()
class UProject_JRenderedCrowdFixture : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	bool Tick(float Delta);
	bool Setup();
	void Finish(bool bSuccess, const TCHAR* Reason);
	UPROPERTY() TArray<TObjectPtr<AProject_JPlayerCharacter>> Players;
	UPROPERTY() TObjectPtr<class ACameraActor> ReviewCamera;
	FTSTicker::FDelegateHandle Handle;
	FString Output, Rows = TEXT("phase,elapsed_s,frame_delta_ms,gpu_ms,actors,rendered_actors,montages,capture_requested,body_lod0,body_lod1,body_lod2,local_body_lod,gt_ms,render_ms,rhi_ms,prediction30,nanite_candidates,nanite_parts,cloth_parts,cloth_tick_parts,modular_parts,phase_game_s,weapon_meshes,weapon_visible,weapon_rendered,weapon_auto_lod,weapon_mesh_matches,direct_bodies,retarget_bodies,cloth_managed_parts,cloth_reduced_parts,local_cloth_reduced_parts,render_lod0_parts,render_lod1_parts,render_lod2_parts,local_max_render_lod,cloth_simulations,cloth_substeps1,cloth_substeps3,remote_cloth_max_iterations,local_cloth_substeps,local_cloth_iterations\n");
	FString PoseRoutes=TEXT("actor,component,asset,main_class,post_class,leader,bones,modular\n");
	int32 ExpectedModularParts=0;
	// Wall time bounds the run; world/game time controls the prescribed animation workload.
	double Started = 0, PhaseAt = 0;
	int32 Phase = 0, Samples = 0, VisibleSamples = 0, CrowdCount = 200;
	bool bSimulateMovement = false;
	TArray<FVector> PreviousPositions;
	TArray<double> TravelDistances;
	TArray<int32> Reversals, Landings;
	TArray<bool> WasInAir;
	TArray<float> LastDirection;
	bool bJumpRequested = false;
	bool bShot = false, bFinished = false;
	bool bWithLocalPlayer = false;
	bool bCloseView = false;
	int32 LocalQualitySamples = 0;
	// Optional assertion for profiling the same saved assets through both render paths.
	int32 ExpectedNaniteMode = -1;
	// Optional visual-only comparison: equipment, abilities and weapon motion keep running.
	int32 WeaponVisibility = -1;
	FString ExpectedWeaponMesh;
};
