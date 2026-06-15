#include "Validation/Project_JDataValidation.h"

#if WITH_EDITOR

void Project_J::DataValidation::AddError(FDataValidationContext& Context, bool& bHasError, const FText& Message)
{
	Context.AddError(Message);
	bHasError = true;
}

void Project_J::DataValidation::AddWarning(FDataValidationContext& Context, const FText& Message)
{
	Context.AddWarning(Message);
}

EDataValidationResult Project_J::DataValidation::MakeResult(EDataValidationResult SuperResult, bool bHasError)
{
	return bHasError || SuperResult == EDataValidationResult::Invalid
		? EDataValidationResult::Invalid
		: EDataValidationResult::Valid;
}

#endif
