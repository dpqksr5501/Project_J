#pragma once
#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "Project_JInteractable.generated.h"
class APawn;
UINTERFACE(BlueprintType) class PROJECT_JCORE_API UProject_JInteractable : public UInterface { GENERATED_BODY() };
class PROJECT_JCORE_API IProject_JInteractable
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction") bool CanInteract(APawn* Interactor) const;
	UFUNCTION(BlueprintNativeEvent, BlueprintCallable, Category="Interaction") void Interact(APawn* Interactor);
};
