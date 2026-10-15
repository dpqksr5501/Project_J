#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "Project_JStrafeUpperBodyFacingEditorLibrary.generated.h"

class UAnimBlueprint;

/** Scoped graph installation for review/testing. Never saves a package. */
UCLASS()
class PROJECT_JANIMATIONNODES_API UProject_JStrafeUpperBodyFacingEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category = "Project J|Animation|Editor")
	static bool InstallInMasterForReview(UAnimBlueprint* Blueprint);
	/** Inserts a lower-body constraint node between existing History and Upper Body Facing. Never saves. */
	UFUNCTION(BlueprintCallable, Category = "Project J|Animation|Editor")
	static bool InstallLowerBodyInMasterForReview(UAnimBlueprint* Blueprint);
private:
	friend class FProjectJFacingInstallerAtomicTest;
	static bool InstallUpperGraphForReview(UAnimBlueprint* Blueprint);
	static bool InstallLowerGraphForReview(UAnimBlueprint* Blueprint);
};
