#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Containers/Ticker.h"
#include "Project_JCombatCookedFixture.generated.h"
class AProject_JGreatswordCharacter;
class AProject_JPlayerState;
class AAIController;
class UProject_JEquipmentItemDefinition;

/** Opt-in full native input/GAS/montage/notify fixture using read-only authored visuals. */
UCLASS()
class UProject_JCombatCookedFixture : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
private:
	bool Tick(float Delta);
	bool CreateCharacter();
	void Finish(bool bSuccess, const TCHAR* Reason);
	FTSTicker::FDelegateHandle Handle;
	UPROPERTY() TObjectPtr<AProject_JGreatswordCharacter> Character;
	UPROPERTY() TObjectPtr<AProject_JPlayerState> State;
	UPROPERTY() TObjectPtr<AAIController> Controller;
	UPROPERTY() TObjectPtr<UProject_JEquipmentItemDefinition> Item;
	FString Output, Rows = TEXT("cycle,phase,elapsed_s,delta_ms,montage,niagara,hit_window,precache_remaining,equipped,weapon,abilities,baseline_abilities\n");
	double Started = 0, PhaseStarted = 0, TrailStarted = 0;
	int32 Phase = 0, Cycle = 0, BaseAbilities = 0;
	bool bFinished = false, bSawTrail = false, bSawHitWindow = false, bShot = false;
};
