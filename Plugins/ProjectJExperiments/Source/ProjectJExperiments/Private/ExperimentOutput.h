#pragma once
#include "CoreMinimal.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"

namespace ProjectJ::Experiments
{
inline bool Save(const TCHAR* Name, const FString& Text)
{
    FString Root;
    FParse::Value(FCommandLine::Get(), TEXT("ProjectJFOutput="), Root);
    if (Root.IsEmpty()) { Root = FPaths::ProjectSavedDir() / TEXT("Validation/Experiments"); }
    IFileManager::Get().MakeDirectory(*Root, true);
    return FFileHelper::SaveStringToFile(Text, *(Root / Name), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}
}
