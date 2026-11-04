#include "Authoring/Project_JAssetValidationCommandlet.h"
#include "EditorValidatorSubsystem.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "AssetRegistry/AssetData.h"
#include "Misc/PackageName.h"
#include "Misc/Parse.h"

int32 UProject_JAssetValidationCommandlet::Main(const FString& FullCommandLine)
{
    bExplicitSelection = false;
    ExplicitAssetPaths.Reset();
    const auto Fail = [](const FString& Reason)
    {
        UE_LOG(LogTemp, Error, TEXT("ProjectJ explicit validation selection rejected: %s"), *Reason);
        return 2;
    };
    // Parse this one option before Super's native registry startup scan. Invalid
    // shape/empty/count/duplicates never turn into the engine's unfiltered selection.
    FString Encoded, Argument;
    const TCHAR* Cursor = *FullCommandLine;
    while (FParse::Token(Cursor, Argument, false))
    {
        if (!Argument.StartsWith(TEXT("-"))) continue;
        FString Key = Argument.Mid(1);
        int32 Equals = INDEX_NONE;
        const bool bHasValue = Key.FindChar(TEXT('='), Equals);
        if (bHasValue) Key = Key.Left(Equals);
        if (!Key.Equals(TEXT("ProjectJAssetPaths"), ESearchCase::IgnoreCase)) continue;
        if (bExplicitSelection) return Fail(TEXT("ProjectJAssetPaths must occur exactly once."));
        bExplicitSelection = true;
        if (!bHasValue) return Fail(TEXT("ProjectJAssetPaths requires a nonempty value."));
        Encoded = Argument.Mid(Equals + 2); // Skip '-' and the parsed '='.
    }
    if (!bExplicitSelection) return Super::Main(FullCommandLine);
    if (Encoded.IsEmpty() || Encoded.StartsWith(TEXT("+")) || Encoded.EndsWith(TEXT("+")) || Encoded.Contains(TEXT("++")))
        return Fail(TEXT("Explicit selection contains an empty path token."));
    int32 Requested = 1;
    for (const TCHAR Character : Encoded)
        if (Character == TEXT('+') && ++Requested > 128) return Fail(TEXT("Explicit selection exceeds128 assets."));
    TArray<FString> Paths;
    Encoded.ParseIntoArray(Paths, TEXT("+"), false);
    if (Paths.Num() < 1 || Paths.Num() > 128) return Fail(TEXT("Explicit selection requires1..128 assets."));
    TSet<FSoftObjectPath> Unique;
    TArray<FSoftObjectPath> Validated;
    Validated.Reserve(Paths.Num());
    for (const FString& Path : Paths)
    {
        // Guard native FName bounds before constructing a soft object path.
        FText Reason;
        const FString PackageName = FPackageName::ObjectPathToPackageName(Path);
        const FString ObjectName = FPackageName::ObjectPathToObjectName(Path);
        if (Path.IsEmpty() || PackageName.Len() >= NAME_SIZE || ObjectName.Len() >= NAME_SIZE || !Path.StartsWith(TEXT("/Game/"), ESearchCase::CaseSensitive) ||
            Path.Contains(TEXT(":")) || !FPackageName::IsValidObjectPath(Path, &Reason) ||
            PackageName == Path)
            return Fail(TEXT("Every explicit token must be a full top-level /Game/Package.Asset object path."));
        const FSoftObjectPath ObjectPath(Path);
        if (!ObjectPath.IsValid() || !ObjectPath.GetSubPathString().IsEmpty())
            return Fail(TEXT("Explicit subobjects or invalid object paths are unsupported."));
        if (Unique.Contains(ObjectPath)) return Fail(TEXT("Explicit object paths must be unique."));
        if (!FPackageName::DoesPackageExist(ObjectPath.GetLongPackageName()))
            return Fail(FString::Printf(TEXT("Selected package is not present on disk: %s"), *ObjectPath.GetLongPackageName()));
        Unique.Add(ObjectPath);
        Validated.Add(ObjectPath);
    }
    ExplicitAssetPaths = MoveTemp(Validated);
    // Retain native registry startup, module loading, filtering, settings and
    // registered validators. Only the selection hook below becomes exact/bounded.
    return Super::Main(FullCommandLine);
}

bool UProject_JAssetValidationCommandlet::GetAssetsToValidate(IAssetRegistry& AssetRegistry, TArray<FAssetData>& OutAssetDataList)
{
    if (!bExplicitSelection) return Super::GetAssetsToValidate(AssetRegistry, OutAssetDataList);
    if (ExplicitAssetPaths.IsEmpty() || ExplicitAssetPaths.Num() > 128) return false;
    TArray<FAssetData> Selected;
    Selected.Reserve(ExplicitAssetPaths.Num());
    for (const FSoftObjectPath& ObjectPath : ExplicitAssetPaths)
    {
        const FAssetData Asset = AssetRegistry.GetAssetByObjectPath(ObjectPath, true);
        if (!Asset.IsValid() || Asset.GetSoftObjectPath() != ObjectPath)
        {
            UE_LOG(LogTemp, Error, TEXT("ProjectJ explicit validation selection rejected: exact on-disk asset is absent: %s"), *ObjectPath.ToString());
            return false;
        }
        Selected.Add(Asset);
    }
    OutAssetDataList = MoveTemp(Selected);
    UE_LOG(LogTemp, Display, TEXT("ProjectJ Selected Assets: %d"), OutAssetDataList.Num());
    for (const FAssetData& Asset : OutAssetDataList)
        UE_LOG(LogTemp, Display, TEXT("ProjectJ Selected Asset: %s"), *Asset.GetSoftObjectPath().ToString());
    return true;
}

bool UProject_JAssetValidationCommandlet::ProcessValidationResults(FValidateAssetsResults& Results)
{
    UE_LOG(LogTemp, Display, TEXT("Files Checked: %d, Passed: %d, Failed: %d, Skipped: %d, Unable to validate: %d"),
        Results.NumChecked, Results.NumValid, Results.NumInvalid, Results.NumSkipped, Results.NumUnableToValidate);
    UE_LOG(LogTemp, Display, TEXT("Validation Warnings: %d, Requested: %d, Asset limit reached: %d"),
        Results.NumWarnings, Results.NumRequested, Results.bAssetLimitReached);
    if (bExplicitSelection)
    {
        const int32 Count = ExplicitAssetPaths.Num();
        return Count >= 1 && Count <= 128 && Results.NumRequested == Count && Results.NumChecked == Count && Results.NumValid == Count &&
            Results.NumInvalid == 0 && Results.NumSkipped == 0 && Results.NumUnableToValidate == 0 && Results.NumWarnings == 0 && !Results.bAssetLimitReached;
    }
    return Results.NumChecked > 0 && Results.NumValid > 0 && Results.NumInvalid == 0 &&
        Results.NumUnableToValidate == 0 && !Results.bAssetLimitReached;
}
