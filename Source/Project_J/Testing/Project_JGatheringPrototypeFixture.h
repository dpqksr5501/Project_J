#pragma once
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Project_JGatheringPrototypeFixture.generated.h"
class AProject_JGatheringNode;
class AProject_JNPCCharacter;
class UProject_JItemDefinition;
class UProject_JCompanionItemDefinition;
/** CLI-only consumer on the single owned gathering map. No ordinary-play grants. */
UCLASS(NotBlueprintable)
class AProject_JGatheringPrototypeFixture : public AActor
{
	GENERATED_BODY()
public:
	AProject_JGatheringPrototypeFixture();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
private:
	void Finish(bool Success, const TCHAR* Reason);
	UPROPERTY(Transient) TObjectPtr<AProject_JGatheringNode> Node;
	UPROPERTY(Transient) TObjectPtr<UProject_JItemDefinition> Yield;
	UPROPERTY(Transient) TObjectPtr<UProject_JCompanionItemDefinition> CompanionDefinition;
	UPROPERTY(Transient) TObjectPtr<AProject_JNPCCharacter> RetiredCompanion;
	FGuid CompanionItem;
	FString Output;
	double Started = 0;
	int64 Revision = 0;
	int32 Phase = 0, CompanionSummons = 0, CompanionDismissals = 0;
	bool bFinished = false, bCompanion = false, bCompound = false, bQuestScreenshotProcessed = false;
	FString QuestScreenshot;
	FDelegateHandle QuestScreenshotHandle;
	int64 InitialExperience = 0;
	UPROPERTY(Transient) TObjectPtr<class AProject_JCraftingStation> QuestBench;
	UPROPERTY(Transient) TObjectPtr<UProject_JItemDefinition> QuestMana;
};
