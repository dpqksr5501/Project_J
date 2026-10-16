#include "Authoring/Project_JMeshLODCommandlet.h"
#include "Authoring/Project_JMeshLODAuthoring.h"
#include "Authoring/Project_JMeshLODSettings.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Misc/App.h"
#include "HAL/FileManager.h"
#include "UObject/SavePackage.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "UObject/Linker.h"

namespace
{
FString HashFile(const FString& Path)
{TArray<uint8> Data;if(!FFileHelper::LoadFileToArray(Data,*Path))return {};FSHAHash Hash;FSHA1::HashBuffer(Data.GetData(),Data.Num(),Hash.Hash);return Hash.ToString();}
bool HasSidecars(const FString& File)
{const FString Base=FPaths::ChangeExtension(File,TEXT(""));for(const TCHAR* Ext:{TEXT("uexp"),TEXT("ubulk"),TEXT("m.ubulk"),TEXT("o.ubulk"),TEXT("uptnl"),TEXT("upayload")})if(IFileManager::Get().FileExists(*(Base+TEXT(".")+Ext)))return true;return false;}
bool ProjectPackage(const FString& Name,FString& Filename)
{
    if(!Name.StartsWith(TEXT("/Game/")) || !FPackageName::IsValidLongPackageName(Name))return false;
    Filename=FPaths::ConvertRelativePathToFull(FPackageName::LongPackageNameToFilename(Name,FPackageName::GetAssetPackageExtension()));
    return FPaths::IsUnderDirectory(Filename,FPaths::ConvertRelativePathToFull(FPaths::ProjectContentDir()));
}
}
UProject_JMeshLODCommandlet::UProject_JMeshLODCommandlet()
{IsClient=false;IsServer=false;IsEditor=true;LogToConsole=true;ShowErrorCount=true;}
int32 UProject_JMeshLODCommandlet::Main(const FString& Params)
{
    using namespace ProjectJ::MeshLODAuthoring;
    FString Action=TEXT("Inspect"),Paths,PolicyPath,Output,RoleName;
    FParse::Value(*Params,TEXT("Action="),Action);FParse::Value(*Params,TEXT("Assets="),Paths,false);
    FParse::Value(*Params,TEXT("Policy="),PolicyPath);FParse::Value(*Params,TEXT("Report="),Output);FParse::Value(*Params,TEXT("Role="),RoleName);
    for(const TCHAR* Known:{TEXT("Inspect"),TEXT("Preview"),TEXT("Apply"),TEXT("CreatePolicy")})if(Action.Equals(Known,ESearchCase::IgnoreCase)){Action=Known;break;}
    const FString Operation=FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir()/TEXT("MeshLODAuthoring")/FGuid::NewGuid().ToString(EGuidFormats::Digits));
    IFileManager::Get().MakeDirectory(*Operation,true);if(Output.IsEmpty())Output=Operation/TEXT("report.json");
    auto Report=MakeShared<FJsonObject>();TArray<TSharedPtr<FJsonValue>> Rows;
    int32 Errors=0,Reviews=0,Generated=0,Preserved=0;
    auto Fail=[&](const FString& Message){++Errors;Report->SetStringField(TEXT("error"),Message);};
    if(!FApp::IsUnattended() || !IsRunningCommandlet())Fail(TEXT("Use an unattended, isolated editor commandlet."));
    UProject_JMeshLODSettings* Policy=nullptr;
    if(!Errors && Action==TEXT("CreatePolicy"))
    {
        const FString Object=PolicyPath.IsEmpty() ? DefaultPolicyPath() : PolicyPath;
        const FString Package=FPackageName::ObjectPathToPackageName(Object);FString File;
        if(!ProjectPackage(Package,File) || FPackageName::DoesPackageExist(Package) || FindPackage(nullptr,*Package))Fail(TEXT("Policy destination must be a new project package; existing policy is never overwritten."));
        else
        {
            auto* Pkg=CreatePackage(*Package);Policy=NewObject<UProject_JMeshLODSettings>(Pkg,*FPackageName::GetShortName(Package),RF_Public|RF_Standalone);
            FString Error;if(!ValidatePolicy(*Policy,Error))Fail(Error);
            else
            {
                IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
                if(!UPackage::SavePackage(Pkg,Policy,*File,Args))Fail(TEXT("Cannot save new shared policy."));
                else Report->SetStringField(TEXT("created_policy"),Policy->GetPathName());
            }
        }
    }
    else if(!Errors)
    {
        TArray<FString> Assets;Paths.ParseIntoArray(Assets,TEXT(","),true);
        if((Action!=TEXT("Inspect") && Action!=TEXT("Preview") && Action!=TEXT("Apply")) || Assets.IsEmpty() || Assets.Num()>32)
            Fail(TEXT("Use Inspect/Preview/Apply with 1..32 explicit project mesh paths."));
        if(Paths.StartsWith(TEXT(",")) || Paths.EndsWith(TEXT(",")) || Paths.Contains(TEXT(",,")))Fail(TEXT("Empty asset entries are not accepted."));
        Policy=LoadPolicy(PolicyPath);FString Error;
        if(!Policy || !ValidatePolicy(*Policy,Error))Fail(Policy ? Error : TEXT("Shared policy missing; run CreatePolicy once."));
        TSet<FName> Requested;
        for(const FString& Path:Assets)
        {
            const FString Package=FPackageName::ObjectPathToPackageName(Path);FString File;
            if(!ProjectPackage(Package,File) || Requested.Contains(FName(*Package)))Fail(TEXT("Invalid or duplicate project package list; no assets processed."));
            Requested.Add(FName(*Package));
        }
        if(Errors)Assets.Reset();
        const ERole Role=RoleName.Equals(TEXT("Surface"),ESearchCase::IgnoreCase) ? ERole::Surface : RoleName.Equals(TEXT("Hair"),ESearchCase::IgnoreCase) ? ERole::Hair : ERole::Unclassified;
        TSet<FString> Seen;
        for(const FString& Path:Assets)
        {
            if(Errors && !Policy)break;
            auto Row=MakeShared<FJsonObject>();Row->SetStringField(TEXT("asset"),Path);Rows.Add(MakeShared<FJsonValueObject>(Row));
            const FString Package=FPackageName::ObjectPathToPackageName(Path);FString File;
            if(!ProjectPackage(Package,File) || Seen.Contains(Package))
            {++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetStringField(TEXT("reason"),TEXT("Invalid or duplicate project package."));continue;}
            Seen.Add(Package);
            if(const auto* Existing=FindPackage(nullptr,*Package);Existing && Existing->IsDirty())
            {++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetStringField(TEXT("reason"),TEXT("Unsaved package changes; no apply."));continue;}
            const FString Object=Path.Contains(TEXT(".")) ? Path : Path+TEXT(".")+FPackageName::GetShortName(Path);
            TStrongObjectPtr<UObject> Asset(LoadObject<UObject>(nullptr,*Object));
            if(!Asset.IsValid() || Asset->GetOutermost()->GetName()!=Package || Asset->GetOuter()!=Asset->GetOutermost())
            {++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetStringField(TEXT("reason"),TEXT("Missing asset, redirector or non-top-level object; no package replacement."));continue;}
            const FInspection State=Inspect(Asset.Get(),*Policy,Role);Row->SetNumberField(TEXT("lods_before"),State.LODs);Row->SetStringField(TEXT("reason"),State.Reason);
            if(State.Status==EStatus::Existing){++Preserved;Row->SetStringField(TEXT("status"),TEXT("PreservedExisting"));continue;}
            if(State.Status==EStatus::Review){++Reviews;Row->SetStringField(TEXT("status"),TEXT("ReviewRequired"));continue;}
            if(State.Status!=EStatus::Missing){++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));continue;}
            if(Action==TEXT("Inspect")){Row->SetStringField(TEXT("status"),TEXT("MissingLOD"));continue;}
            auto Draft=Preview(Asset.Get(),*Policy,Role,Error);
            if(!Draft.IsValid()){++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetStringField(TEXT("reason"),Error);continue;}
            Row->SetNumberField(TEXT("lods_after"),Inspect(Draft.Get(),*Policy,Role).LODs);
            if(Action==TEXT("Preview")){Row->SetStringField(TEXT("status"),TEXT("PreviewValidated"));continue;}
            // No live-session edits, automatic imports or Save All. Any in-memory
            // apply failure dies with this commandlet; the original file is untouched.
            const FString BeforeHash=HashFile(File);
            if(Asset->GetOutermost()->IsDirty() || BeforeHash.IsEmpty() || HasSidecars(File) || IFileManager::Get().IsReadOnly(*File))
            {++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetStringField(TEXT("reason"),TEXT("Dirty/read-only/split source package cannot be committed."));continue;}
            const FString Directory=Operation/FString::FromInt(Generated+Errors);IFileManager::Get().MakeDirectory(*Directory,true);
            const FString Backup=Directory/(TEXT("original_")+FPaths::GetCleanFilename(File));
            const FString Staged=Directory/FPaths::GetCleanFilename(File);
            if(IFileManager::Get().Copy(*Backup,*File,false,true)!=COPY_OK || !Generate(Asset.Get(),*Policy,Role,Error))
            {++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetStringField(TEXT("reason"),Error);continue;}
            FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;Args.SaveFlags=SAVE_NoError;
            const FString BeforeSave=ProtectedFingerprint(Asset.Get());
            if(!UPackage::SavePackage(Asset->GetOutermost(),Asset.Get(),*Staged,Args) || HasSidecars(Staged) || HashFile(File)!=BeforeHash || ProtectedFingerprint(Asset.Get())!=BeforeSave)
            {++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetStringField(TEXT("reason"),TEXT("Staging failed, split output or source file changed concurrently; original not replaced."));continue;}
            // Staging has serialized all required payloads. Release the old
            // linker before replacing its file, as native package saving does.
            ResetLoaders(Asset->GetOutermost());
            if(!IFileManager::Get().Move(*File,*Staged,true,false,false,true))
            {
                const bool Intact=HashFile(File)==BeforeHash;
                const bool Restored=Intact || (IFileManager::Get().Copy(*File,*Backup,true,false)==COPY_OK && HashFile(File)==BeforeHash);
                ++Errors;Row->SetStringField(TEXT("status"),TEXT("Error"));Row->SetBoolField(TEXT("original_restored"),Restored);Row->SetStringField(TEXT("backup"),Backup);
                Row->SetStringField(TEXT("reason"),Restored ? TEXT("Commit failed; original restored and verified.") : TEXT("Commit and rollback failed; recover the recorded original backup before using this asset."));continue;
            }
            ++Generated;Row->SetStringField(TEXT("status"),TEXT("Generated"));Row->SetStringField(TEXT("backup"),Backup);
            Row->SetStringField(TEXT("sha1_before"),BeforeHash);Row->SetStringField(TEXT("sha1_after"),HashFile(File));
        }
    }
    Report->SetStringField(TEXT("action"),Action);Report->SetNumberField(TEXT("errors"),Errors);Report->SetNumberField(TEXT("review_required"),Reviews);
    if(Policy)Report->SetStringField(TEXT("policy"),Policy->GetPathName());
    Report->SetNumberField(TEXT("generated"),Generated);Report->SetNumberField(TEXT("preserved_existing"),Preserved);Report->SetArrayField(TEXT("assets"),Rows);
    Report->SetStringField(TEXT("scope"),TEXT("Structural preservation/native reduction only; visual transition/contact/cloth/groom quality requires asset review. Per-asset commits; not a multi-asset transaction."));
    FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));IFileManager::Get().MakeDirectory(*FPaths::GetPath(Output),true);
    if(!FFileHelper::SaveStringToFile(Json,*Output))return 1;
    UE_LOG(LogTemp,Display,TEXT("ProjectJ MeshLOD errors=%d review=%d generated=%d preserved=%d report=%s"),Errors,Reviews,Generated,Preserved,*Output);
    return Errors ? 1 : Reviews ? 2 : 0;
}
