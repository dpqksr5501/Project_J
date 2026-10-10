#include "Animation/Project_JEarlyTransitionMigrationLibrary.h"
#include "Animation/Project_JAnimNotifyState_LocomotionEarlyTransition.h"
#include "Animation/AnimSequence.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace ProjectJEarlyMigration
{
FString Metadata(FAnimNotifyEvent Event)
{
	Event.NotifyStateClass = nullptr;
	FString Text; FAnimNotifyEvent::StaticStruct()->ExportText(Text, &Event, nullptr, nullptr, PPF_None, nullptr);
	return Text.Replace(TEXT("/EarlySource/"), TEXT("/Game/"));
}
int64 EnumValue(UObject* Object, const TCHAR* Name)
{
	const auto* Property = FindFProperty<FProperty>(Object->GetClass(), Name);
	if (const auto* Enum = CastField<FEnumProperty>(Property))
		return Enum->GetUnderlyingProperty()->GetSignedIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Object));
	if (const auto* Byte = CastField<FByteProperty>(Property)) return Byte->GetPropertyValue_InContainer(Object);
	return -1;
}
}

FString UProject_JEarlyTransitionMigrationLibrary::RepairSixRunClips(const FString& SourceContent, bool bApply)
{
	check(IsInGameThread()); using namespace ProjectJEarlyMigration;
	auto Report = MakeShared<FJsonObject>(); TArray<TSharedPtr<FJsonValue>> Rows;
	int32 Errors = 0, Saved = 0;
	auto Fail = [&](const FString& Package, const FString& Error)
	{
		auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("package"), Package); Row->SetStringField(TEXT("error"), Error);
		Rows.Add(MakeShared<FJsonValueObject>(Row)); ++Errors;
	};
	const FString SourceRoot = FPaths::ConvertRelativePathToFull(SourceContent) + TEXT("/");
	const FString Blueprints = FPaths::Combine(SourceContent, TEXT("Blueprints")) + TEXT("/");
	if (FPaths::IsRelative(SourceContent) || !IFileManager::Get().DirectoryExists(*Blueprints) ||
		IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Blueprints"))) ||
		FPackageName::MountPointExists(TEXT("/EarlySource/"))) Fail(TEXT("source"), TEXT("Source missing or mount collision"));
	else
	{
		FPackageName::RegisterMountPoint(TEXT("/EarlySource/"), SourceRoot);
		FPackageName::RegisterMountPoint(TEXT("/Game/Blueprints/"), Blueprints);
		for (const TCHAR* Name : {TEXT("Box_LR_F_Lfoot"), TEXT("Box_LR_F_Rfoot"), TEXT("Box_RL_F_Lfoot"), TEXT("Box_RL_F_Rfoot"), TEXT("Pivot_B_F_Lfoot"), TEXT("Pivot_B_F_Rfoot")})
		{
			const FString Path = FString(TEXT("/Game/Characters/UEFN_Mannequin/Animations/Run/M_Neutral_Run_")) + Name;
			if (UPackage* Existing = FindPackage(nullptr, *Path); Existing && Existing->IsDirty()) { Fail(Path, TEXT("Unsaved target changes")); break; }
			const FString Suffix = TEXT(".") + FPackageName::GetShortName(Path);
			TStrongObjectPtr<UAnimSequence> Target(LoadObject<UAnimSequence>(nullptr, *(Path + Suffix)));
			TStrongObjectPtr<UAnimSequence> Original(LoadObject<UAnimSequence>(nullptr, *(TEXT("/EarlySource/") + Path.RightChop(6) + Suffix)));
			if (!Target.IsValid() || !Original.IsValid()) { Fail(Path, TEXT("Cannot load source/target")); break; }
			int32 Index = INDEX_NONE; const FAnimNotifyEvent* SourceEvent = nullptr;
			for (int32 I = 0; I < Target->Notifies.Num(); ++I)
			{
				const auto& Event = Target->Notifies[I];
				if (!Event.NotifyName.ToString().Contains(TEXT("EarlyTransition"))) continue;
				if (Index != INDEX_NONE) { Index = INDEX_NONE; break; }
				Index = I; SourceEvent = Original->Notifies.FindByPredicate([&](const FAnimNotifyEvent& Source) { return Source.Guid == Event.Guid; });
			}
			if (Index == INDEX_NONE || !SourceEvent || !SourceEvent->NotifyStateClass ||
				!SourceEvent->NotifyStateClass->GetClass()->GetName().Contains(TEXT("BP_NotifyState_EarlyTransition")) ||
				EnumValue(SourceEvent->NotifyStateClass, TEXT("TransitionDestination")) != 0 ||
				EnumValue(SourceEvent->NotifyStateClass, TEXT("TransitionCondition")) != 1 ||
				EnumValue(SourceEvent->NotifyStateClass, TEXT("GaitNotEqual")) != 1 ||
				Metadata(Target->Notifies[Index]) != Metadata(*SourceEvent))
			{ Fail(Path, TEXT("GUID, timing, metadata or ReTransition/GaitNotEqualRun differs; manual review required")); break; }
			auto Row = MakeShared<FJsonObject>(); Row->SetStringField(TEXT("package"), Path);
			Row->SetStringField(TEXT("metadata"), Metadata(Target->Notifies[Index]));
			Row->SetNumberField(TEXT("time"), SourceEvent->GetTime()); Row->SetNumberField(TEXT("duration"), SourceEvent->GetDuration());
			Row->SetStringField(TEXT("condition"), TEXT("ReTransition / Gait != Run / not blending out"));
			if (bApply)
			{
				Target->Modify(); auto& Event = Target->Notifies[Index];
				auto* Previous = Event.NotifyStateClass.Get();
				auto* Native = NewObject<UProject_JAnimNotifyState_LocomotionEarlyTransition>(Target.Get(), NAME_None, RF_Transactional);
				Native->bRequireGaitChange = true; Native->ExcludedGait = EProject_JLocomotionGaitIntent::Run;
				Native->NotifyColor = SourceEvent->NotifyStateClass->NotifyColor;
				Event.NotifyStateClass = Native;
				check(Metadata(Event) == Metadata(*SourceEvent));
				if (Previous) { Previous->ClearFlags(RF_Public | RF_Standalone); Previous->SetFlags(RF_Transient); }
				Target->RefreshCacheData(); Target->MarkPackageDirty();
				FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
				const FString File = FPaths::Combine(FPaths::ProjectContentDir(), Path.RightChop(6)) + FPackageName::GetAssetPackageExtension();
				if (!UPackage::SavePackage(Target->GetPackage(), Target.Get(), *File, Args)) { Fail(Path, TEXT("Save failed; restore backup")); break; }
				++Saved;
			}
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
		FPackageName::UnRegisterMountPoint(TEXT("/Game/Blueprints/"), Blueprints);
		FPackageName::UnRegisterMountPoint(TEXT("/EarlySource/"), SourceRoot);
	}
	Report->SetNumberField(TEXT("errors"), Errors); Report->SetNumberField(TEXT("saved"), Saved); Report->SetArrayField(TEXT("packages"), Rows);
	FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json)); return Json;
}
