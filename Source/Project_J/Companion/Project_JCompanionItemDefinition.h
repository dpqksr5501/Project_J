#pragma once
#include "CoreMinimal.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Project_JCompanionItemDefinition.generated.h"

class AProject_JNPCCharacter;

/** Inventory ownership authorizes a single, runtime-only owner-follow companion.
 * No combat grants, evolution or persistent unlock state are implied. */
UCLASS(BlueprintType, Const)
class PROJECT_J_API UProject_JCompanionItemDefinition : public UProject_JItemDefinition
{
    GENERATED_BODY()
public:
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Companion")
    TSoftClassPtr<AProject_JNPCCharacter> CompanionClass;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Companion", meta=(ClampMin="50", ClampMax="2000"))
    double FollowDistance = 200;
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="Companion", meta=(ClampMin="100", ClampMax="1000"))
    double SpawnDistance = 300;
    bool IsFollowDefinitionValid() const;
#if WITH_EDITOR
    virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
