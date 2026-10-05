#pragma once

#include "Mount/Project_JMountCharacter.h"
#include "Project_JMountLifecycleFixture.generated.h"

/** Concrete, asset-independent automation fixture. Gameplay never spawns this class. */
UCLASS(NotBlueprintable, Transient)
class PROJECT_JMOUNT_API AProject_JMountLifecycleFixture : public AProject_JMountCharacter
{
	GENERATED_BODY()
public:
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override { ++InputSetupCount; Super::SetupPlayerInputComponent(Input); }
	void SetTestInputComponent(UInputComponent* Input) { InputComponent = Input; }
	void NotifyTestControllerChanged() { OnRep_Controller(); }
	void NotifyTestClientRestart() { PawnClientRestart(); }
	int32 InputSetupCount = 0;
	void SetTestInteractAction(UInputAction* Action) { InteractAction = Action; }
	void SetTestRider(ACharacter* Character) { ACharacter* Previous = Rider; Rider = Character; OnRep_Rider(Previous); }
	void UnrelatedInput() {}
};
