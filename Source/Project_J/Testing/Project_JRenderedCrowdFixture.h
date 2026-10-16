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
	FTSTicker::FDelegateHandle Handle;
	FString Output, Rows = TEXT("phase,elapsed_s,frame_delta_ms,gpu_ms,actors,rendered_actors,montages,capture_requested,body_lod0,body_lod1,body_lod2,local_body_lod\n");
	double Started = 0, PhaseAt = 0;
	int32 Phase = 0, Samples = 0, VisibleSamples = 0;
	bool bShot = false, bFinished = false;
	bool bWithLocalPlayer = false;
	int32 LocalQualitySamples = 0;
};
