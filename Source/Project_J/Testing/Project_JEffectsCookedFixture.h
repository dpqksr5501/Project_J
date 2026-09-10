#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "NiagaraEffectType.h"
#include "Project_JEffectsCookedFixture.generated.h"
class UNiagaraComponent;
class UNiagaraSystem;

/** Explicit CLI diagnostic in an isolated Entry map. Never saves authored assets. */
UCLASS()
class UProject_JEffectsCookedFixture : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	bool Tick(float Delta);
	void Finish(bool bSuccess, const TCHAR* Reason);
	FTSTicker::FDelegateHandle Handle;
	UPROPERTY() TObjectPtr<UNiagaraSystem> System;
	UPROPERTY() TArray<TObjectPtr<UNiagaraComponent>> Effects;
	double Started = 0, PhaseStarted = 0;
	int32 Phase = 0;
	bool bFinished = false, bPreload = false, bShot = false;
	bool bMixedDistance = false, bDistanceCull = false, bFixedBounds = false;
	bool bOriginalOverrideEnabled = false;
	TArray<FNiagaraSystemScalabilityOverride> OriginalOverrides;
	FString Output, Rows = TEXT("phase,elapsed_s,delta_ms,precache_remaining,effects,active,complete\n");
};
