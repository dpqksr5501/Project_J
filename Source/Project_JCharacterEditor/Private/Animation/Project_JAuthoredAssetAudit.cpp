#include "AssetRegistry/AssetRegistryModule.h"
#include "Chooser.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogProjectJAuthoredAssetAudit, Log, All);
namespace
{
FString ExportStruct(const FInstancedStruct& Value)
{
    FString Text;
    if (const auto* Type = Value.GetScriptStruct())
    { Type->ExportText(Text, Value.GetMemory(), nullptr, nullptr, PPF_None, nullptr); }
    return Text;
}
void AuditAuthoredAssets(const TArray<FString>& Args)
{
    // This tool supplements MCP's public-property view with native authoring
    // rows. It reads only; it never compiles, annotates, modifies or saves assets.
    const FString Name = Args.Num() == 1 ? Args[0] : TEXT("AnimationAssetAudit");
    if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT(".."))) { return; }
    auto Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("assetsModified"), false);
    TArray<TSharedPtr<FJsonValue>> Records;
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const UClass* Types[] = {UChooserTable::StaticClass(), UPoseSearchDatabase::StaticClass(), UPoseSearchSchema::StaticClass()};
    for (const UClass* Type : Types)
    {
        TArray<FAssetData> Assets; Registry.GetAssetsByClass(Type->GetClassPathName(), Assets, true);
        Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
        for (const auto& Data : Assets)
        {
            if (!Data.PackageName.ToString().StartsWith(TEXT("/Game/Animation_Logic/"))) { continue; }
            auto Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("asset"), Data.GetObjectPathString());
            UObject* Asset = Data.GetAsset();
            if (!Asset) { Record->SetBoolField(TEXT("loadFailed"), true); }
            else if (const auto* Chooser = Cast<UChooserTable>(Asset))
            {
                TArray<TSharedPtr<FJsonValue>> Rows;
                for (int32 Index = 0; Index < Chooser->ResultsStructs.Num(); ++Index)
                {
                    auto Row = MakeShared<FJsonObject>();
                    Row->SetNumberField(TEXT("row"), Index);
                    Row->SetBoolField(TEXT("disabled"), Chooser->DisabledRows.IsValidIndex(Index) && Chooser->DisabledRows[Index]);
                    Row->SetStringField(TEXT("result"), ExportStruct(Chooser->ResultsStructs[Index]));
                    Rows.Add(MakeShared<FJsonValueObject>(Row));
                }
                Record->SetArrayField(TEXT("rows"), Rows);
                Record->SetStringField(TEXT("fallback"), ExportStruct(Chooser->FallbackResult));
                TArray<TSharedPtr<FJsonValue>> Columns;
                for (const auto& Column : Chooser->ColumnsStructs) { Columns.Add(MakeShared<FJsonValueString>(ExportStruct(Column))); }
                Record->SetArrayField(TEXT("columns"), Columns);
            }
            else if (const auto* Database = Cast<UPoseSearchDatabase>(Asset))
            {
                Record->SetStringField(TEXT("schema"), GetPathNameSafe(Database->Schema));
                // Authored rows are readable before the asynchronous DDC index is
                // ready. Do not call GetSearchIndex here: it requires a valid index.
                Record->SetBoolField(TEXT("searchIndexNotInspected"), true);
                TArray<TSharedPtr<FJsonValue>> Entries;
                for (int32 Index = 0; Index < Database->GetNumAnimationAssets(); ++Index)
                {
                    const auto* Entry = Database->GetDatabaseAnimationAsset(Index);
                    auto Row = MakeShared<FJsonObject>();
                    Row->SetNumberField(TEXT("entry"), Index);
                    Row->SetStringField(TEXT("animation"), GetPathNameSafe(Database->GetAnimationAsset(Index)));
                    if (Entry)
                    {
                        Row->SetBoolField(TEXT("enabled"), Entry->IsEnabled());
                        Row->SetBoolField(TEXT("skeletonCompatible"), Entry->IsSkeletonCompatible(Database->Schema));
                        Row->SetBoolField(TEXT("mirrorCompatible"), Entry->IsMirrorOptionCompatible(Database->Schema));
                        Row->SetBoolField(TEXT("disableReselection"), Entry->IsDisableReselection());
                        Row->SetNumberField(TEXT("rangeMin"), Entry->GetSamplingRange().Min);
                        Row->SetNumberField(TEXT("rangeMax"), Entry->GetSamplingRange().Max);
                        Row->SetNumberField(TEXT("mirrorOption"), int32(Entry->GetMirrorOption()));
                    }
                    Entries.Add(MakeShared<FJsonValueObject>(Row));
                }
                Record->SetArrayField(TEXT("entries"), Entries);
            }
            else if (const auto* Schema = Cast<UPoseSearchSchema>(Asset))
            {
                Record->SetNumberField(TEXT("sampleRate"), Schema->SampleRate);
                Record->SetNumberField(TEXT("cardinality"), Schema->SchemaCardinality);
                Record->SetNumberField(TEXT("finalizedChannelCount"), Schema->GetChannels().Num());
                TArray<TSharedPtr<FJsonValue>> Skeletons;
                for (const auto& Entry : Schema->GetRoledSkeletons()) { Skeletons.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Entry.Skeleton))); }
                Record->SetArrayField(TEXT("skeletons"), Skeletons);
            }
            Records.Add(MakeShared<FJsonValueObject>(Record));
        }
    }
    Root->SetArrayField(TEXT("assets"), Records);
    FString Json; FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json));
    const FString Output = FPaths::ProjectSavedDir() / TEXT("Validation") / (Name + TEXT(".json"));
    const bool bWritten = FFileHelper::SaveStringToFile(Json, *Output);
    UE_LOG(LogProjectJAuthoredAssetAudit, Display, TEXT("Read-only authored asset audit: assets=%d written=%d path=%s"), Records.Num(), bWritten, *Output);
}
FAutoConsoleCommand AuditCommand(TEXT("ProjectJ.Animation.AuditAuthoredAssets"),
    TEXT("Read-only Chooser rows, PSD entries/schema compatibility and schema inventory. Optional output basename under Saved/Validation."),
    FConsoleCommandWithArgsDelegate::CreateStatic(&AuditAuthoredAssets));
}
