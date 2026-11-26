#pragma once

#include "GameFramework/Actor.h"
#include "Project_JWorldRouteFixture.generated.h"

class ACameraActor;
class AProject_JMountMoverPawn;

/** Cooked Development acceptance fixture. It uses the same world portal and mount input as gameplay. */
UCLASS(NotBlueprintable)
class AProject_JWorldRouteFixture : public AActor
{
	GENERATED_BODY()
public:
	AProject_JWorldRouteFixture();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(EditAnywhere, Category="Route") bool bEntry = false;
	UPROPERTY(EditAnywhere, Category="Route") TSubclassOf<AProject_JMountMoverPawn> MountClass;
	UPROPERTY(EditAnywhere, Category="Route") TSubclassOf<AProject_JMountMoverPawn> FlightMountClass;
	UPROPERTY(EditAnywhere, Category="Route") double BridgeStartX = 6000.0;
	UPROPERTY(EditAnywhere, Category="Route") double TurnaroundX = 32000.0;
	UPROPERTY(EditAnywhere, Category="Route") FName OriginalCanopyTag = TEXT("RouteCanopy00");

private:
	void Finish(bool bPassed, const TCHAR* Reason);
	void CaptureRepresentation(const FString& Name) const;
	enum class EStage { Spawn, Outbound, Turn, Inbound, Takeoff, Flight, Near, Away, Far, Return, Reentered };
	EStage Stage = EStage::Spawn;
	TWeakObjectPtr<AProject_JMountMoverPawn> Mount;
	TWeakObjectPtr<ACameraActor> Camera;
	double StartTime = 0.0;
	double StageTime = 0.0;
	double CenterY = 0.0;
	int32 RoadFrames = 0;
	int32 MissingRoadFrames = 0;
	int32 HLODFrames = 0;
	int32 NeitherVisibleFrames = 0;
	FString CaptureDirectory;
	bool bReturnCaptureRequested = false;
	double ClimbPeak = 0.0;
	double DivePeak = 0.0;
	double FlightYaw = 0.0;
	double TurnDegrees = 0.0;
};
