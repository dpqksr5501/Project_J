#include "AssetRegistry/AssetRegistryModule.h"
#include "Chooser.h"
#include "ObjectChooser_Asset.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PoseSearch/PoseSearchDatabase.h"
#include "PoseSearch/PoseSearchSchema.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/UnrealType.h"
#include "UObject/Package.h"

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
    const bool bChoosersOnly = Args.Num() == 2 && Args[1] == TEXT("ChoosersOnly");
    if (Args.Num() > 2 || (Args.Num() == 2 && !bChoosersOnly)) { return; }
    const FString Name = Args.IsEmpty() ? TEXT("AnimationAssetAudit") : Args[0];
    if (Name.IsEmpty() || Name.Contains(TEXT("/")) || Name.Contains(TEXT("\\")) || Name.Contains(TEXT(".."))) { return; }
    const FString Output = FPaths::ProjectSavedDir() / TEXT("Validation") / (Name + TEXT(".json"));
    if (FPaths::FileExists(Output))
    {
        UE_LOG(LogProjectJAuthoredAssetAudit, Error, TEXT("Preserving existing audit: %s"), *Output);
        return;
    }
    auto Root = MakeShared<FJsonObject>();
    Root->SetBoolField(TEXT("assetsModified"), false);
    Root->SetBoolField(TEXT("choosersOnly"), bChoosersOnly);
    TArray<TSharedPtr<FJsonValue>> Records;
    TSet<const UChooserTable*> SeenChoosers;
    auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
    const UClass* Types[] = {UChooserTable::StaticClass(), UPoseSearchDatabase::StaticClass(), UPoseSearchSchema::StaticClass()};
    for (const UClass* Type : Types)
    {
        if (bChoosersOnly && Type != UChooserTable::StaticClass()) { continue; }
        TArray<FAssetData> Assets; Registry.GetAssetsByClass(Type->GetClassPathName(), Assets, true);
        Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.PackageName.LexicalLess(B.PackageName); });
        for (const auto& Data : Assets)
        {
            if (!Data.PackageName.ToString().StartsWith(bChoosersOnly ? TEXT("/Game/") : TEXT("/Game/Animation_Logic/"))) { continue; }
            auto Record = MakeShared<FJsonObject>();
            Record->SetStringField(TEXT("asset"), Data.GetObjectPathString());
            UObject* Asset = Data.GetAsset();
            if (!Asset) { Record->SetBoolField(TEXT("loadFailed"), true); }
            else if (const auto* Chooser = Cast<UChooserTable>(Asset))
            {
                // Only the scoped mode follows embedded tables. Default exports
                // retain their original root-record count and PSD/schema scope.
                TArray<const UChooserTable*> Pending { Chooser };
                while (!Pending.IsEmpty())
                {
                    const UChooserTable* Current = Pending.Pop(EAllowShrinking::No);
                    if (!Current || SeenChoosers.Contains(Current)) { continue; }
                    SeenChoosers.Add(Current);
                    auto ChooserRecord = Current == Chooser ? Record : MakeShared<FJsonObject>();
                    ChooserRecord->SetStringField(TEXT("asset"), Current->GetPathName());
                    ChooserRecord->SetStringField(TEXT("registryRoot"), Chooser->GetPathName());
                    ChooserRecord->SetStringField(TEXT("contextOwner"), GetPathNameSafe(Current->GetRootChooser()));
                    ChooserRecord->SetStringField(TEXT("outputObjectType"), GetPathNameSafe(Current->OutputObjectType));
                    ChooserRecord->SetNumberField(TEXT("resultType"), int32(Current->ResultType));
                    ChooserRecord->SetBoolField(TEXT("packageDirty"), Current->GetOutermost()->IsDirty());
                    TArray<TSharedPtr<FJsonValue>> Contexts;
                    for (const auto& Context : Current->GetContextData()) { Contexts.Add(MakeShared<FJsonValueString>(ExportStruct(Context))); }
                    ChooserRecord->SetArrayField(TEXT("contexts"), Contexts);
                    TArray<TSharedPtr<FJsonValue>> Rows;
                    TArray<TSharedPtr<FJsonValue>> ReferencedTables;
                    auto FollowResult = [&](const FInstancedStruct& Result)
                    {
                        const UChooserTable* Referenced = nullptr;
                        if (Result.GetScriptStruct() == FAssetChooser::StaticStruct())
                        { Referenced = Cast<UChooserTable>(Result.Get<FAssetChooser>().Asset); }
                        else if (Result.GetScriptStruct() == FEvaluateChooser::StaticStruct())
                        { Referenced = Result.Get<FEvaluateChooser>().Chooser; }
                        else if (Result.GetScriptStruct() == FNestedChooser::StaticStruct())
                        { Referenced = Result.Get<FNestedChooser>().Chooser; }
                        if (Referenced)
                        {
                            ReferencedTables.Add(MakeShared<FJsonValueString>(Referenced->GetPathName()));
                            if (bChoosersOnly) { Pending.Add(Referenced); }
                        }
                    };
                    for (int32 Index = 0; Index < Current->ResultsStructs.Num(); ++Index)
                    {
                        auto Row = MakeShared<FJsonObject>();
                        Row->SetNumberField(TEXT("row"), Index);
                        Row->SetBoolField(TEXT("disabled"), Current->DisabledRows.IsValidIndex(Index) && Current->DisabledRows[Index]);
                        Row->SetStringField(TEXT("result"), ExportStruct(Current->ResultsStructs[Index]));
                        FollowResult(Current->ResultsStructs[Index]);
                        Rows.Add(MakeShared<FJsonValueObject>(Row));
                    }
                    ChooserRecord->SetArrayField(TEXT("rows"), Rows);
                    ChooserRecord->SetStringField(TEXT("fallback"), ExportStruct(Current->FallbackResult));
                    FollowResult(Current->FallbackResult);
                    ChooserRecord->SetArrayField(TEXT("resultReferencedTables"), ReferencedTables);
                    TArray<TSharedPtr<FJsonValue>> Columns;
                    for (const auto& Column : Current->ColumnsStructs) { Columns.Add(MakeShared<FJsonValueString>(ExportStruct(Column))); }
                    ChooserRecord->SetArrayField(TEXT("columns"), Columns);
                    TArray<TSharedPtr<FJsonValue>> Nested;
                    for (const auto& Child : Current->NestedChoosers)
                    {
                        Nested.Add(MakeShared<FJsonValueString>(GetPathNameSafe(Child.Get())));
                        if (bChoosersOnly) { Pending.Add(Child.Get()); }
                    }
                    ChooserRecord->SetArrayField(TEXT("nestedChoosers"), Nested);
                    Records.Add(MakeShared<FJsonValueObject>(ChooserRecord));
                }
                continue;
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
    const bool bWritten = FFileHelper::SaveStringToFile(Json, *Output);
    UE_LOG(LogProjectJAuthoredAssetAudit, Display, TEXT("Read-only authored asset audit: assets=%d written=%d path=%s"), Records.Num(), bWritten, *Output);
}
FAutoConsoleCommand AuditCommand(TEXT("ProjectJ.Animation.AuditAuthoredAssets"),
    TEXT("Read-only Chooser rows, PSD entries/schema compatibility and schema inventory. [OutputName] [ChoosersOnly]. The scoped mode includes all /Game Choosers and embedded tables."),
    FConsoleCommandWithArgsDelegate::CreateStatic(&AuditAuthoredAssets));
}
