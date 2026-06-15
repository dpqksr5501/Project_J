#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

namespace Project_J::DataValidation
{
PROJECT_JCHARACTER_API void AddError(FDataValidationContext& Context, bool& bHasError, const FText& Message);
PROJECT_JCHARACTER_API void AddWarning(FDataValidationContext& Context, const FText& Message);
PROJECT_JCHARACTER_API EDataValidationResult MakeResult(EDataValidationResult SuperResult, bool bHasError);
}
#endif
