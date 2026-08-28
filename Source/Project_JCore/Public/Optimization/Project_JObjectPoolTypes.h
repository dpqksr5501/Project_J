#pragma once

#include "CoreMinimal.h"
#include "Project_JObjectPoolTypes.generated.h"

UENUM(BlueprintType)
enum class EProject_JPoolKind : uint8
{
	Actor,
	NiagaraComponent,
	Widget
};

/**
 * Declarative pool policy only. Registering this definition never spawns, reuses,
 * or destroys objects; concrete gameplay systems opt in later.
 */
USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JObjectPoolDefinition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling")
	FName PoolId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling")
	EProject_JPoolKind Kind = EProject_JPoolKind::Actor;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling", meta = (ClampMin = "0", UIMin = "0"))
	int32 PrewarmCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling", meta = (ClampMin = "0", UIMin = "0"))
	int32 MaxRetainedCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pooling")
	bool bAuthorityOnly = true;
};
