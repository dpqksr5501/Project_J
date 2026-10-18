#include "Authoring/Project_JAssetValidationCommandlet.h"
#include "EditorValidatorSubsystem.h"

bool UProject_JAssetValidationCommandlet::ProcessValidationResults(FValidateAssetsResults& Results)
{
    UE_LOG(LogTemp, Display, TEXT("Files Checked: %d, Passed: %d, Failed: %d, Skipped: %d, Unable to validate: %d"),
        Results.NumChecked, Results.NumValid, Results.NumInvalid, Results.NumSkipped, Results.NumUnableToValidate);
    UE_LOG(LogTemp, Display, TEXT("Validation Warnings: %d, Requested: %d, Asset limit reached: %d"),
        Results.NumWarnings, Results.NumRequested, Results.bAssetLimitReached);
    return Results.NumChecked > 0 && Results.NumValid > 0 && Results.NumInvalid == 0 &&
        Results.NumUnableToValidate == 0 && !Results.bAssetLimitReached;
}
