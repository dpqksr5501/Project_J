#include "Authoring/Project_JNiagaraScalabilityCommandlet.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "IAssetTools.h"
#include "ISourceControlModule.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "NiagaraEffectType.h"
#include "NiagaraSystem.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/App.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "HAL/FileManager.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"

namespace
{
struct FRecipe
{
    FString Source, SourcePackage, System, SystemPackage, Type, TypePackage, Profile, ProfilePackage, Cue, Frequency;
    int32 MaxInstances = 0;
    ENiagaraScalabilityUpdateFrequency UpdateFrequency = ENiagaraScalabilityUpdateFrequency::High;
};
struct FProtectedPackage
{
    UObject* Asset = nullptr;
    TMap<FString, FString> Files; // Empty digest is an explicit absent sidecar.
    bool bDirty = false;
};
FString Digest(const FString& File)
{
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *File)) { return {}; }
    FSHAHash Hash; FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash.Hash); return Hash.ToString();
}
FString PackageFile(const FString& Package)
{
    return FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Package, FPackageName::GetAssetPackageExtension()));
}
TMap<FString, FString> PackageDigests(const FString& Package)
{
    TMap<FString, FString> Files;
    const FString Base = FPaths::ChangeExtension(PackageFile(Package), TEXT(""));
    for (const TCHAR* Extension : { TEXT("uasset"), TEXT("uexp"), TEXT("ubulk"), TEXT("m.ubulk"), TEXT("o.ubulk"), TEXT("uptnl"), TEXT("upayload") })
    {
        const FString File = Base + TEXT(".") + Extension;
        Files.Add(File, IFileManager::Get().FileExists(*File) ? Digest(File) : FString());
    }
    return Files;
}
bool CanonicalPath(const FString& Input, const FString& Prefix, FString& Object, FString& Package)
{
    if (Input.Contains(TEXT(":")) || Input.Contains(TEXT("\\"))) { return false; }
    Package = FPackageName::ObjectPathToPackageName(Input);
    if (!Package.StartsWith(Prefix, ESearchCase::CaseSensitive) || !FPackageName::IsValidLongPackageName(Package)) { return false; }
    Object = Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package);
    const FString File = PackageFile(Package);
    return (Input == Object || Input == Package) && FPaths::IsUnderDirectory(File, FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
}
bool NewDestination(const FString& Package)
{
    if (FPackageName::DoesPackageExist(Package) || FindPackage(nullptr, *Package)) { return false; }
    for (const auto& File : PackageDigests(Package))
        if (IFileManager::Get().FileExists(*File.Key)) { return false; }
    return true;
}
bool NoPackageFiles(const FString& Package)
{
    for (const auto& File : PackageDigests(Package))
        if (IFileManager::Get().FileExists(*File.Key)) { return false; }
    return true;
}
TSharedPtr<FJsonObject> Effective(const UNiagaraSystem* System)
{
    auto Json = MakeShared<FJsonObject>(); const auto& S = System->GetScalabilitySettings();
    Json->SetBoolField(TEXT("effect_type_count_enabled"), S.bCullMaxInstanceCount);
    Json->SetNumberField(TEXT("max_effect_type_instances"), S.MaxInstances);
    Json->SetBoolField(TEXT("per_system_count_enabled"), S.bCullPerSystemMaxInstanceCount);
    Json->SetNumberField(TEXT("max_system_instances"), S.MaxSystemInstances);
    Json->SetBoolField(TEXT("distance_enabled"), S.bCullByDistance);
    if (FMath::IsFinite(S.MaxDistance)) { Json->SetNumberField(TEXT("max_distance"), S.MaxDistance); }
    else { Json->SetField(TEXT("max_distance"), MakeShared<FJsonValueNull>()); }
    Json->SetBoolField(TEXT("local_player_culling"), System->AllowCullingForLocalPlayers());
    Json->SetBoolField(TEXT("sorted_significance_needed"), System->NeedsSortedSignificanceCull());
    Json->SetBoolField(TEXT("fixed_bounds_enabled"), System->bFixedBounds);
    Json->SetStringField(TEXT("effect_type"), GetPathNameSafe(System->GetEffectType()));
    return Json;
}
int32 MatchingCue(const UProject_JAttackPresentationProfile* Profile, FGameplayTag Tag, const FString& ExpectedPath)
{
    int32 Found = INDEX_NONE, Count = 0;
    for (int32 Index = 0; Index < Profile->Cues.Num(); ++Index)
        if (Profile->Cues[Index].CueTag == Tag) { Found = Index; ++Count; }
    return Count == 1 && Profile->Cues[Found].bLooping && Profile->Cues[Found].NiagaraSystem.ToSoftObjectPath().ToString() == ExpectedPath ? Found : INDEX_NONE;
}
bool SameCues(const TArray<FProject_JCombatVFXCueDefinition>& A, const TArray<FProject_JCombatVFXCueDefinition>& B)
{
    if (A.Num() != B.Num()) { return false; }
    for (int32 Index = 0; Index < A.Num(); ++Index)
        if (!FProject_JCombatVFXCueDefinition::StaticStruct()->CompareScriptStruct(&A[Index], &B[Index], 0)) { return false; }
    return true;
}
bool SameFiles(const TMap<FString, FString>& A, const TMap<FString, FString>& B)
{
    if (A.Num() != B.Num()) { return false; }
    for (const auto& File : A) { const auto* Other = B.Find(File.Key); if (!Other || *Other != File.Value) { return false; } }
    return true;
}
bool FiniteSettings(const FNiagaraSystemScalabilitySettings& S)
{
    const auto& B = S.BudgetScaling;
    const float Values[] = { S.MaxDistance, S.VisibilityCulling.MaxTimeOutsideViewFrustum, S.VisibilityCulling.MaxTimeWithoutRender,
        B.MaxGlobalBudgetUsage, B.MaxDistanceScaleByGlobalBudgetUse.StartX, B.MaxDistanceScaleByGlobalBudgetUse.StartY,
        B.MaxDistanceScaleByGlobalBudgetUse.EndX, B.MaxDistanceScaleByGlobalBudgetUse.EndY,
        B.MaxInstanceCountScaleByGlobalBudgetUse.StartX, B.MaxInstanceCountScaleByGlobalBudgetUse.StartY,
        B.MaxInstanceCountScaleByGlobalBudgetUse.EndX, B.MaxInstanceCountScaleByGlobalBudgetUse.EndY,
        B.MaxSystemInstanceCountScaleByGlobalBudgetUse.StartX, B.MaxSystemInstanceCountScaleByGlobalBudgetUse.StartY,
        B.MaxSystemInstanceCountScaleByGlobalBudgetUse.EndX, B.MaxSystemInstanceCountScaleByGlobalBudgetUse.EndY };
    for (const float Value : Values) { if (!FMath::IsFinite(Value)) { return false; } }
    return true;
}
}

UProject_JNiagaraScalabilityCommandlet::UProject_JNiagaraScalabilityCommandlet()
{ IsClient = false; IsServer = false; IsEditor = true; LogToConsole = true; ShowErrorCount = true; }

int32 UProject_JNiagaraScalabilityCommandlet::Main(const FString& Params)
{
    const bool bApply = FParse::Param(*Params, TEXT("Apply"));
    const FString ReceiptRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Saved/Authoring/NiagaraScalability") / FGuid::NewGuid().ToString(EGuidFormats::Digits));
    const FString ReceiptFile = ReceiptRoot / TEXT("receipt.json");
    auto Report = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Errors, Warnings;
    Report->SetNumberField(TEXT("schema"), 1); Report->SetBoolField(TEXT("apply_requested"), bApply);
    Report->SetStringField(TEXT("receipt"), ReceiptFile);
    Report->SetStringField(TEXT("scope"), TEXT("Owned looping-cue authoring only; no runtime cull/resume, GPU/VRAM hard cap or Cook verification. Git/LFS/provider settings are not changed."));
    Warnings.Add(MakeShared<FJsonValueString>(TEXT("Periodic native significance/count culling is not an instantaneous GPU/memory cap; existing notify/ManualRelease still owns the loop end.")));
    TStrongObjectPtr<UNiagaraSystem> Source, Destination;
    TStrongObjectPtr<UNiagaraEffectType> SourceType, Type;
    TStrongObjectPtr<UProject_JAttackPresentationProfile> Profile;
    TArray<FProtectedPackage> Protected;
    FRecipe Recipe; bool bPathsValidated = false, bTypeSaved = false, bSystemSaved = false, bProfileSaved = false, bProfileModified = false, bEffectiveVerified = false;
    auto Fail = [&](const FString& Error) { Errors.Add(MakeShared<FJsonValueString>(Error)); };
    const auto Finish = [&]()
    {
        TArray<TSharedPtr<FJsonValue>> Preserved;
        for (const auto& Item : Protected)
        {
            const bool bProfile = Item.Asset == Profile.Get();
            auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("asset"), Item.Asset->GetPathName());
            const auto After = PackageDigests(Item.Asset->GetOutermost()->GetName());
            const bool bFilesSame = SameFiles(Item.Files, After), bDirtyAfter = Item.Asset->GetOutermost()->IsDirty();
            Row->SetBoolField(TEXT("dirty_before"), Item.bDirty); Row->SetBoolField(TEXT("dirty_after"), bDirtyAfter);
            Row->SetBoolField(TEXT("files_unchanged"), bFilesSame);
            TArray<TSharedPtr<FJsonValue>> Digests;
            for (const auto& File : Item.Files)
            {
                auto Entry = MakeShared<FJsonObject>(); Entry->SetStringField(TEXT("path"), File.Key);
                Entry->SetStringField(TEXT("before_sha1"), File.Value); Entry->SetStringField(TEXT("after_sha1"), After.FindRef(File.Key));
                Digests.Add(MakeShared<FJsonValueObject>(Entry));
            }
            Row->SetArrayField(TEXT("files"), Digests); Preserved.Add(MakeShared<FJsonValueObject>(Row));
            if ((!bProfile || !bProfileModified) && (!bFilesSame || bDirtyAfter != Item.bDirty)) { Fail(TEXT("Protected source/asset changed: ") + Item.Asset->GetPathName()); }
        }
        Report->SetArrayField(TEXT("protected_packages"), Preserved);
        Report->SetBoolField(TEXT("created_effect_type_in_memory"), Type.IsValid()); Report->SetBoolField(TEXT("created_system_in_memory"), Destination.IsValid());
        Report->SetStringField(TEXT("actual_created_effect_type"), GetPathNameSafe(Type.Get())); Report->SetStringField(TEXT("actual_created_system"), GetPathNameSafe(Destination.Get()));
        if (Type.IsValid())
        {
            Report->SetBoolField(TEXT("effect_type_dirty_after"), Type->GetOutermost()->IsDirty());
            Report->SetNumberField(TEXT("effect_type_frequency"), int32(Type->UpdateFrequency)); Report->SetNumberField(TEXT("effect_type_cull_reaction"), int32(Type->CullReaction));
            Report->SetBoolField(TEXT("effect_type_local_culling"), Type->bAllowCullingForLocalPlayers);
            Report->SetStringField(TEXT("effect_type_significance_class"), Type->SignificanceHandler ? Type->SignificanceHandler->GetClass()->GetPathName() : FString());
        }
        if (Destination.IsValid()) { Report->SetBoolField(TEXT("system_dirty_after"), Destination->GetOutermost()->IsDirty()); }
        Report->SetBoolField(TEXT("effect_type_saved"), bTypeSaved); Report->SetBoolField(TEXT("system_saved"), bSystemSaved);
        Report->SetBoolField(TEXT("profile_modified"), bProfileModified); Report->SetBoolField(TEXT("profile_saved"), bProfileSaved);
        Report->SetBoolField(TEXT("effective_destination_verified"), bEffectiveVerified);
        if (Source.IsValid()) { Report->SetObjectField(TEXT("source_effective_settings"), Effective(Source.Get())); }
        if (Destination.IsValid()) { Report->SetObjectField(TEXT("destination_effective_settings"), Effective(Destination.Get())); }
        if (bPathsValidated)
        {
            Report->SetBoolField(TEXT("destination_system_file_exists"), IFileManager::Get().FileExists(*PackageFile(Recipe.SystemPackage)));
            Report->SetBoolField(TEXT("destination_type_file_exists"), IFileManager::Get().FileExists(*PackageFile(Recipe.TypePackage)));
        }
        Report->SetArrayField(TEXT("errors"), Errors); Report->SetArrayField(TEXT("warnings"), Warnings);
        Report->SetBoolField(TEXT("success"), Errors.IsEmpty());
        IFileManager::Get().MakeDirectory(*ReceiptRoot, true);
        FString Json;
        const bool bReceiptSaved = FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json)) && FFileHelper::SaveStringToFile(Json, *ReceiptFile);
        UE_LOG(LogTemp, Display, TEXT("ProjectJ Niagara authoring success=%d apply=%d receipt=%s"), Errors.IsEmpty() && bReceiptSaved, bApply, *ReceiptFile);
        return Errors.IsEmpty() && bReceiptSaved ? 0 : 1;
    };
    FString RecipeFile;
    if (!FApp::IsUnattended() || !IsRunningCommandlet() || !FParse::Value(*Params, TEXT("Recipe="), RecipeFile) || FPaths::IsRelative(RecipeFile))
    { Fail(TEXT("Use an isolated unattended Editor commandlet with -Recipe=<absolute JSON file>. Dry-run is default; -Apply explicitly publishes.")); return Finish(); }
    RecipeFile = FPaths::ConvertRelativePathToFull(RecipeFile); Report->SetStringField(TEXT("recipe_file"), RecipeFile);
    const int64 RecipeBytes = IFileManager::Get().FileSize(*RecipeFile);
    FString Text; TSharedPtr<FJsonObject> Json;
    if (RecipeBytes <= 0 || RecipeBytes > 65536 || !FFileHelper::LoadFileToString(Text, *RecipeFile)
        || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Json) || !Json)
    { Fail(TEXT("Recipe must be a valid nonempty JSON object no larger than 64KiB.")); return Finish(); }
    Report->SetStringField(TEXT("recipe_sha1"), Digest(RecipeFile)); Report->SetObjectField(TEXT("requested"), Json);
    const TSet<FString> Fields{ TEXT("SourceSystem"), TEXT("DestinationSystem"), TEXT("DestinationEffectType"), TEXT("PresentationProfile"), TEXT("CueTag"), TEXT("MaxInstances"), TEXT("Frequency"), TEXT("CullReaction"), TEXT("KeepExistingDistance") };
    for (const auto& Pair : Json->Values)
    {
        const FString Key(*Pair.Key); // UE5.8 stores shared JSON keys; materialize the offline validation key explicitly.
        if (!Fields.Contains(Key)) { Fail(TEXT("Unknown recipe field: ") + Key); continue; }
        const EJson Expected = Key == TEXT("MaxInstances") ? EJson::Number : Key == TEXT("KeepExistingDistance") ? EJson::Boolean : EJson::String;
        if (!Pair.Value.IsValid() || Pair.Value->Type != Expected) { Fail(TEXT("Wrong JSON value type: ") + Key); }
    }
    FString SourcePath, SystemPath, TypePath, ProfilePath, Reaction; double Maximum = 0; bool bKeepDistance = false;
    if (Json->Values.Num() != Fields.Num() || !Json->TryGetStringField(TEXT("SourceSystem"), SourcePath)
        || !Json->TryGetStringField(TEXT("DestinationSystem"), SystemPath) || !Json->TryGetStringField(TEXT("DestinationEffectType"), TypePath)
        || !Json->TryGetStringField(TEXT("PresentationProfile"), ProfilePath) || !Json->TryGetStringField(TEXT("CueTag"), Recipe.Cue)
        || !Json->TryGetStringField(TEXT("Frequency"), Recipe.Frequency) || !Json->TryGetStringField(TEXT("CullReaction"), Reaction)
        || !Json->TryGetNumberField(TEXT("MaxInstances"), Maximum) || !Json->TryGetBoolField(TEXT("KeepExistingDistance"), bKeepDistance)
        || !FMath::IsFinite(Maximum) || Maximum < 1 || Maximum > 4096 || FMath::FloorToDouble(Maximum) != Maximum
        || !bKeepDistance || Reaction != TEXT("DeactivateImmediateResume") || (Recipe.Frequency != TEXT("High") && Recipe.Frequency != TEXT("Continuous")))
    { Fail(TEXT("All nine typed recipe fields are required: integer MaxInstances 1..4096, Frequency High/Continuous, CullReaction DeactivateImmediateResume, KeepExistingDistance true.")); return Finish(); }
    if (!Errors.IsEmpty()) { return Finish(); }
    Recipe.MaxInstances = int32(Maximum); Recipe.UpdateFrequency = Recipe.Frequency == TEXT("High") ? ENiagaraScalabilityUpdateFrequency::High : ENiagaraScalabilityUpdateFrequency::Continuous;
    if (!CanonicalPath(SourcePath, TEXT("/Game/"), Recipe.Source, Recipe.SourcePackage)
        || !CanonicalPath(SystemPath, TEXT("/Game/Effects/ProjectJ/Combat/"), Recipe.System, Recipe.SystemPackage)
        || !CanonicalPath(TypePath, TEXT("/Game/Effects/ProjectJ/Combat/"), Recipe.Type, Recipe.TypePackage)
        || !CanonicalPath(ProfilePath, TEXT("/Game/DataAssetSets/Presentation/"), Recipe.Profile, Recipe.ProfilePackage))
    { Fail(TEXT("Use canonical /Game package/object paths. New destinations are restricted to /Game/Effects/ProjectJ/Combat/; profile to /Game/DataAssetSets/Presentation/.")); return Finish(); }
    bPathsValidated = true;
    const TSet<FName> Packages{ FName(*Recipe.SourcePackage), FName(*Recipe.SystemPackage), FName(*Recipe.TypePackage), FName(*Recipe.ProfilePackage) };
    if (Packages.Num() != 4 || !NewDestination(Recipe.SystemPackage) || !NewDestination(Recipe.TypePackage))
    { Fail(TEXT("All four package identities must be distinct and both destination packages/sidecars absent, including already loaded packages.")); return Finish(); }
    Source.Reset(LoadObject<UNiagaraSystem>(nullptr, *Recipe.Source, nullptr, LOAD_NoWarn | LOAD_Quiet));
    Profile.Reset(LoadObject<UProject_JAttackPresentationProfile>(nullptr, *Recipe.Profile, nullptr, LOAD_NoWarn | LOAD_Quiet));
    const FGameplayTag CueTag = FGameplayTag::RequestGameplayTag(FName(*Recipe.Cue), false);
    if (!Source.IsValid() || !Profile.IsValid() || Source->GetPathName() != Recipe.Source || Profile->GetPathName() != Recipe.Profile || !CueTag.IsValid())
    { Fail(TEXT("Exact source NS, existing attack presentation profile and registered semantic CueTag must resolve.")); return Finish(); }
    SourceType.Reset(Source->GetEffectType());
    for (UObject* Asset : { static_cast<UObject*>(Source.Get()), static_cast<UObject*>(Profile.Get()), static_cast<UObject*>(SourceType.Get()) })
    {
        if (!Asset) { continue; }
        FProtectedPackage Item; Item.Asset = Asset; Item.bDirty = Asset->GetOutermost()->IsDirty(); Item.Files = PackageDigests(Asset->GetOutermost()->GetName());
        Protected.Add(Item);
        if (Item.bDirty || Item.Files.FindRef(PackageFile(Asset->GetOutermost()->GetName())).IsEmpty()) { Fail(TEXT("Original package must be saved, readable and clean: ") + Asset->GetPathName()); }
        for (const auto& File : Item.Files)
            if (IFileManager::Get().FileExists(*File.Key) && File.Value.IsEmpty()) { Fail(TEXT("Original sidecar is not readable: ") + File.Key); }
    }
    const int32 CueIndex = MatchingCue(Profile.Get(), CueTag, Recipe.Source);
    if (CueIndex == INDEX_NONE || Source->AllowCullingForLocalPlayers() || Source->GetScalabilitySettings().bCullPerSystemMaxInstanceCount)
    { Fail(TEXT("Profile must have exactly one matching looping cue/old soft path. Source must already protect local effects and have no per-system count cap.")); }
    const FBox Bounds = Source->GetFixedBounds();
    if (!FiniteSettings(Source->GetScalabilitySettings()) || (Source->bFixedBounds && (!Bounds.IsValid || Bounds.Min.ContainsNaN() || Bounds.Max.ContainsNaN())))
    { Fail(TEXT("Source effective settings/fixed bounds must be finite and valid before authoring.")); }
    const auto FixedCountCompatible = [&](const FNiagaraSystemScalabilitySettings& Setting)
    { return FiniteSettings(Setting) && !Setting.bCullPerSystemMaxInstanceCount && !Setting.BudgetScaling.bScaleMaxInstanceCountByGlobalBudgetUse; };
    if (SourceType.IsValid()) for (const auto& Setting : SourceType->SystemScalabilitySettings.Settings)
        if (!FixedCountCompatible(Setting)) { Fail(TEXT("Source EffectType per-system/global-scaled-count policy is outside this fixed-count recipe.")); }
    const auto SourceOverrides = Source->GetScalabilityOverrides();
    for (const auto& Override : SourceOverrides.Overrides)
        if (!FiniteSettings(Override) || (Override.bOverridePerSystemInstanceCountSettings && Override.bCullPerSystemMaxInstanceCount)
            || (Override.bOverrideGlobalBudgetScalingSettings && Override.BudgetScaling.bScaleMaxInstanceCountByGlobalBudgetUse))
        { Fail(TEXT("Source system contains a per-system/global-scaled-count override; it will not be silently rewritten.")); }
    if (SourceType.IsValid()) for (const auto& Setting : SourceType->EmitterScalabilitySettings.Settings)
        if (!FMath::IsFinite(Setting.SpawnCountScale)) { Fail(TEXT("Source emitter scalability must be finite before copying.")); }
    if (bApply && (ISourceControlModule::Get().IsEnabled() || IFileManager::Get().IsReadOnly(*PackageFile(Recipe.ProfilePackage))))
    { Fail(TEXT("Apply requires actual source control provider disabled (invocation -SCCProvider=None) and a writable profile. Settings/filters are never changed by this commandlet.")); }
    Report->SetBoolField(TEXT("source_control_enabled"), ISourceControlModule::Get().IsEnabled());
    Report->SetNumberField(TEXT("matching_cue_index"), CueIndex);
    Report->SetNumberField(TEXT("niagara_quality_level"), FNiagaraPlatformSet::GetQualityLevel());
    Report->SetStringField(TEXT("source_system"), Recipe.Source);
    Report->SetStringField(TEXT("destination_system"), Recipe.System); Report->SetStringField(TEXT("destination_effect_type"), Recipe.Type);
    Report->SetStringField(TEXT("presentation_profile"), Recipe.Profile); Report->SetStringField(TEXT("cue_tag"), Recipe.Cue);
    if (!Errors.IsEmpty() || !bApply) { return Finish(); } // No asset construction, Modify or save occurs above this line.
    if (!NewDestination(Recipe.SystemPackage) || !NewDestination(Recipe.TypePackage))
    { Fail(TEXT("Destination appeared during input loading; existing assets are never overwritten.")); return Finish(); }

    const auto OriginalCues = Profile->Cues;
    auto ExpectedSettings = Source->GetScalabilitySettings(); ExpectedSettings.bCullMaxInstanceCount = true; ExpectedSettings.MaxInstances = Recipe.MaxInstances;
    auto ExpectedOverrides = SourceOverrides;
    int32 ClearedOverrides = 0;
    for (auto& Override : ExpectedOverrides.Overrides) { ClearedOverrides += Override.bOverrideInstanceCountSettings ? 1 : 0; Override.bOverrideInstanceCountSettings = false; }
    Type.Reset(NewObject<UNiagaraEffectType>(CreatePackage(*Recipe.TypePackage), *FPackageName::GetLongPackageAssetName(Recipe.TypePackage), RF_Public | RF_Standalone | RF_Transactional));
    const FNiagaraSystemScalabilitySettings Fallback = Type->GetActiveSystemScalabilitySettings();
    Type->bAllowCullingForLocalPlayers = false; Type->UpdateFrequency = Recipe.UpdateFrequency; Type->CullReaction = ENiagaraCullReaction::DeactivateImmediateResume;
    Type->SignificanceHandler = NewObject<UNiagaraSignificanceHandlerDistance>(Type.Get());
    if (SourceType.IsValid())
    {
        Type->SystemScalabilitySettings = SourceType->SystemScalabilitySettings; Type->EmitterScalabilitySettings = SourceType->EmitterScalabilitySettings;
        Type->ReadinessSettings = SourceType->ReadinessSettings; Type->ReadinessProfile = SourceType->ReadinessProfile;
    }
    // Keep the source's ordered platform/quality rows. Its const getter fallback
    // is copied, never mutated; the final all-platform row covers the same gaps.
    Type->SystemScalabilitySettings.Settings.Add(Fallback);
    for (auto& Setting : Type->SystemScalabilitySettings.Settings) { Setting.bCullMaxInstanceCount = true; Setting.MaxInstances = Recipe.MaxInstances; }
    FAssetRegistryModule::AssetCreated(Type.Get()); Type->MarkPackageDirty(); Type->PostEditChange();
    if (!NewDestination(Recipe.SystemPackage) || ISourceControlModule::Get().IsEnabled())
    { Fail(TEXT("System destination or source control state changed during EffectType callbacks; no overwrite or automatic SCC save is permitted.")); return Finish(); }
    auto& Tools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get();
    Destination.Reset(Cast<UNiagaraSystem>(Tools.DuplicateAsset(FPackageName::GetLongPackageAssetName(Recipe.SystemPackage), FPackageName::GetLongPackagePath(Recipe.SystemPackage), Source.Get())));
    if (!Destination.IsValid() || Destination->GetPathName() != Recipe.System) { Fail(TEXT("AssetTools did not create the exact new system. Partial objects/files are retained and recorded.")); return Finish(); }
    Destination->GetScalabilityOverrides() = ExpectedOverrides; Destination->SetEffectType(Type.Get()); Destination->UpdateScalability(); Destination->MarkPackageDirty();
    const auto& Actual = Destination->GetScalabilitySettings();
    bEffectiveVerified = FNiagaraSystemScalabilitySettings::StaticStruct()->CompareScriptStruct(&ExpectedSettings, &Actual, 0)
        && Type->UpdateFrequency == Recipe.UpdateFrequency && Type->CullReaction == ENiagaraCullReaction::DeactivateImmediateResume
        && !Type->bAllowCullingForLocalPlayers && Type->SignificanceHandler && Type->SignificanceHandler->IsA<UNiagaraSignificanceHandlerDistance>()
        && Destination->AllowCullingForLocalPlayers() == false && Destination->NeedsSortedSignificanceCull()
        && Destination->bFixedBounds == Source->bFixedBounds && Destination->GetFixedBounds() == Source->GetFixedBounds()
        && Destination->GetOverrideScalabilitySettings() == Source->GetOverrideScalabilitySettings()
        && FNiagaraSystemScalabilityOverrides::StaticStruct()->CompareScriptStruct(&ExpectedOverrides, &Destination->GetScalabilityOverrides(), 0);
    Report->SetNumberField(TEXT("effect_type_count_overrides_cleared"), ClearedOverrides);
    if (!bEffectiveVerified) { Fail(TEXT("New effective fixed count/local protection or preserved distance/other scalability/bounds differ; nothing is linked to the profile.")); return Finish(); }
    const auto ProtectedStillClean = [&]()
    {
        for (const auto& Item : Protected)
            if (Item.Asset->GetOutermost()->IsDirty() != Item.bDirty || !SameFiles(Item.Files, PackageDigests(Item.Asset->GetOutermost()->GetName()))) { return false; }
        return SameCues(Profile->Cues, OriginalCues) && MatchingCue(Profile.Get(), CueTag, Recipe.Source) == CueIndex;
    };
    if (!ProtectedStillClean())
    { Fail(TEXT("Original source/profile changed during asset preparation; no profile write is permitted.")); return Finish(); }
    if (!NoPackageFiles(Recipe.TypePackage) || !NoPackageFiles(Recipe.SystemPackage))
    { Fail(TEXT("Unexpected destination files appeared before explicit save; existing files are never overwritten.")); return Finish(); }
    const auto Save = [](UObject* Asset)
    {
        const FString File = PackageFile(Asset->GetOutermost()->GetName()); IFileManager::Get().MakeDirectory(*FPaths::GetPath(File), true);
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        return UPackage::SavePackage(Asset->GetOutermost(), Asset, *File, Args);
    };
    bTypeSaved = Save(Type.Get());
    if (!bTypeSaved) { Fail(TEXT("EffectType save failed; partial outputs remain recorded and the profile is untouched.")); return Finish(); }
    if (!NoPackageFiles(Recipe.SystemPackage)) { Fail(TEXT("Unexpected system file appeared before its explicit save; it will not be overwritten.")); return Finish(); }
    bSystemSaved = Save(Destination.Get());
    if (!bSystemSaved || !ProtectedStillClean()) { Fail(TEXT("System save failed or original input changed; both new outputs remain recorded and profile is untouched.")); return Finish(); }
    // Preserve the one authorized pre-existing package before its final publish.
    // No automatic rollback/removal can conceal a failed partial authoring run.
    IFileManager::Get().MakeDirectory(*ReceiptRoot, true);
    TArray<TSharedPtr<FJsonValue>> Backups;
    for (const auto& File : PackageDigests(Recipe.ProfilePackage))
    {
        if (File.Value.IsEmpty()) { continue; }
        const FString Backup = ReceiptRoot / (TEXT("ProfileBefore.") + FPaths::GetCleanFilename(File.Key));
        auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("source"), File.Key); Row->SetStringField(TEXT("backup"), Backup); Row->SetStringField(TEXT("sha1"), File.Value);
        const bool bCopied = IFileManager::Get().Copy(*Backup, *File.Key, false, false) == COPY_OK && Digest(Backup) == File.Value;
        Row->SetBoolField(TEXT("copy_verified"), bCopied); Backups.Add(MakeShared<FJsonValueObject>(Row)); Report->SetArrayField(TEXT("profile_backup"), Backups);
        if (!bCopied) { Fail(TEXT("Profile backup failed; new outputs retained and profile remains untouched.")); return Finish(); }
    }
    Report->SetArrayField(TEXT("profile_backup"), Backups);
    if (!ProtectedStillClean()) { Fail(TEXT("Profile/source changed immediately before link; no profile write.")); return Finish(); }
    Profile->Modify(); Profile->Cues[CueIndex].NiagaraSystem = Destination.Get(); bProfileModified = true;
    auto ExpectedCues = OriginalCues; ExpectedCues[CueIndex].NiagaraSystem = Destination.Get();
    Profile->PostEditChange();
    if (!SameCues(Profile->Cues, ExpectedCues) || MatchingCue(Profile.Get(), CueTag, Recipe.System) != CueIndex)
    { Fail(TEXT("Profile callback changed fields beyond the single requested soft reference; backup/partial state retained, profile not saved.")); return Finish(); }
    bProfileSaved = Save(Profile.Get());
    if (!bProfileSaved) { Fail(TEXT("Profile save failed. Keep receipt, its original backup and all partial outputs; no automatic rollback or deletion.")); }
    else if (Profile->GetOutermost()->IsDirty() || Type->GetOutermost()->IsDirty() || Destination->GetOutermost()->IsDirty())
    { Fail(TEXT("Saved output remained dirty; preserve receipt/partial state for review.")); }
    return Finish();
}
