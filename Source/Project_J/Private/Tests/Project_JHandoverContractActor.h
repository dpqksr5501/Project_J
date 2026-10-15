#pragma once
#include "GameFramework/Actor.h"
#include "Components/SceneComponent.h"
#include "Network/Project_JHandoverSerializable.h"
#include "Project_JHandoverContractActor.generated.h"

/** Private native callback fixture. Never saved, placed or spawned by gameplay. */
UCLASS(Transient, NotPlaceable)
class AProject_JHandoverContractActor : public AActor, public IProject_JHandoverSerializable
{
	GENERATED_BODY()
public:
	AProject_JHandoverContractActor() { SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root"))); }
	TFunction<void()> OnSerialize;
	TFunction<bool(const TArray<uint8>&)> OnApply;
	virtual void SerializeForHandover(TArray<uint8>& OutData) override
	{ OutData = {1, 2, 3}; const auto Callback = OnSerialize; if (Callback) Callback(); }
	virtual void DeserializeFromHandover(const TArray<uint8>& Data) override { TryApplyHandoverSnapshot(Data); }
	virtual bool TryApplyHandoverSnapshot(const TArray<uint8>& Data) override { const auto Callback = OnApply; return Callback ? Callback(Data) : Data.Num() == 3; }
};
