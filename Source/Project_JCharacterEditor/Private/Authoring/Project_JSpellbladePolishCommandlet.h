#pragma once

#include "Commandlets/Commandlet.h"
#include "Project_JSpellbladePolishCommandlet.generated.h"

/** Applies or checks the Longblade motion and input assets without an editor UI or Python. */
UCLASS()
class UProject_JSpellbladePolishCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	virtual int32 Main(const FString& Params) override;
};
