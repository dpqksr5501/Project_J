#include "Animation/Project_JFoleyMigrationLibrary.h"

#include "Animation/AnimSequenceBase.h"
#include "Animation/Project_JAnimNotify_FoleyEvent.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PackageTools.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/SavePackage.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace
{
bool ReadPayload(UAnimNotify* Notify, FProject_JFoleyEvent& Out, FString& Error)
{
	const FStructProperty* Tag = FindFProperty<FStructProperty>(Notify->GetClass(), TEXT("Event"));
	const FProperty* Side = FindFProperty<FProperty>(Notify->GetClass(), TEXT("Side"));
	const FNumericProperty* Volume = FindFProperty<FNumericProperty>(Notify->GetClass(), TEXT("VolumeMultiplier"));
	const FNumericProperty* Pitch = FindFProperty<FNumericProperty>(Notify->GetClass(), TEXT("PitchMultiplier"));
	if (!Tag || Tag->Struct != FGameplayTag::StaticStruct() || !Side || !Volume || !Pitch || !Volume->IsFloatingPoint() || !Pitch->IsFloatingPoint())
	{
		Error = TEXT("Unrecognized legacy property schema"); return false;
	}
	Out.Event = *Tag->ContainerPtrToValuePtr<FGameplayTag>(Notify);
	Out.VolumeMultiplier = float(Volume->GetFloatingPointPropertyValue(Volume->ContainerPtrToValuePtr<void>(Notify)));
	Out.PitchMultiplier = float(Pitch->GetFloatingPointPropertyValue(Pitch->ContainerPtrToValuePtr<void>(Notify)));
	UEnum* Enum = nullptr;
	int64 Value = -1;
	if (const FEnumProperty* EnumProperty = CastField<FEnumProperty>(Side))
	{
		Enum = EnumProperty->GetEnum();
		Value = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(EnumProperty->ContainerPtrToValuePtr<void>(Notify));
	}
	else if (const FByteProperty* Byte = CastField<FByteProperty>(Side))
	{
		Enum = Byte->Enum; Value = Byte->GetPropertyValue_InContainer(Notify);
	}
	const FString Label = Enum ? Enum->GetDisplayNameTextByValue(Value).ToString() : FString();
	if (Label == TEXT("None")) { Out.Side = EProject_JFoleySide::None; }
	else if (Label == TEXT("Left")) { Out.Side = EProject_JFoleySide::Left; }
	else if (Label == TEXT("Right")) { Out.Side = EProject_JFoleySide::Right; }
	else { Error = TEXT("Unknown authored Side: ") + Label; return false; }
	if (!Out.Event.IsValid() || !FMath::IsFinite(Out.VolumeMultiplier) || !FMath::IsFinite(Out.PitchMultiplier) ||
		Out.VolumeMultiplier < 0.f || Out.PitchMultiplier <= 0.f || Out.Event.ToString().Contains(TEXT("Slide.Loop")))
	{
		Error = TEXT("Invalid or unsupported looping payload"); return false;
	}
	return true;
}

FString ExportEvent(const FAnimNotifyEvent& Event)
{
	FString Text;
	FAnimNotifyEvent::StaticStruct()->ExportText(Text, &Event, nullptr, nullptr, PPF_None, nullptr);
	return Text;
}

struct FReadOnlySourceMount
{
	TArray<TPair<FString, FString>> Points;
	explicit FReadOnlySourceMount(const FString& Source)
	{
		for (const FString& Folder : {FString(TEXT("Blueprints")), FString(TEXT("Audio"))})
		{
			const FString Root = TEXT("/Game/") + Folder + TEXT("/");
			const FString Directory = FPaths::Combine(Source, Folder) + TEXT("/");
			FPackageName::RegisterMountPoint(Root, Directory);
			Points.Emplace(Root, Directory);
		}
	}
	~FReadOnlySourceMount()
	{
		for (const auto& Point : Points) { FPackageName::UnRegisterMountPoint(Point.Key, Point.Value); }
	}
};
}

FString UProject_JFoleyMigrationLibrary::MigrateGaspFoley(const TArray<FString>& Packages, const FString& SourceContent, bool bApply, const TArray<FString>& VerifiedNamedEvents)
{
	check(IsInGameThread());
	const auto Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Errors = 0, Replacements = 0, Saved = 0;
	auto Fail = [&](const FString& Path, const FString& Reason)
	{
		const auto Item = MakeShared<FJsonObject>();
		Item->SetStringField(TEXT("package"), Path); Item->SetStringField(TEXT("error"), Reason);
		Results.Add(MakeShared<FJsonValueObject>(Item)); ++Errors;
	};
	// A more-specific temporary mount may never hide project-authored folders.
	bool bSourceSafe = FPaths::IsRelative(SourceContent) == false;
	for (const FString& Folder : {FString(TEXT("Blueprints")), FString(TEXT("Audio"))})
	{
		bSourceSafe &= IFileManager::Get().DirectoryExists(*FPaths::Combine(SourceContent, Folder));
		bSourceSafe &= !IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectContentDir(), Folder));
	}
	if (!bSourceSafe) { Fail(TEXT("source"), TEXT("Missing source folder or project mount collision")); }
	else
	{
		FReadOnlySourceMount SourceMount(SourceContent);
		TStrongObjectPtr<UClass> LegacyBase(LoadClass<UAnimNotify>(nullptr,
			TEXT("/Game/Blueprints/AnimNotifies/BP_AnimNotify_FoleyEvent.BP_AnimNotify_FoleyEvent_C")));
		if (!LegacyBase.IsValid()) { Fail(TEXT("source"), TEXT("Cannot load original Foley base class")); }
		else for (const FString& Path : Packages)
		{
			// Explicit imported character animation roots only; source packages cannot be saved here.
			if (!Path.StartsWith(TEXT("/Game/Characters/UEFN_Mannequin/Animations/")))
			{
				Fail(Path, TEXT("Package outside approved imported-animation root")); continue;
			}
			UPackage* Existing = FindPackage(nullptr, *Path);
			if (Existing && Existing->IsDirty()) { Fail(Path, TEXT("Unsaved user changes")); continue; }
			TStrongObjectPtr<UAnimSequenceBase> Sequence(LoadObject<UAnimSequenceBase>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path))));
			if (!Sequence.IsValid()) { Fail(Path, TEXT("Cannot load animation")); continue; }
			bool bMissingFoley = false;
			auto IsVerifiedNamed = [&](const FAnimNotifyEvent& Event)
			{
				return !Event.Notify && !Event.NotifyStateClass && VerifiedNamedEvents.Contains(
					Path + TEXT("|") + Event.Guid.ToString(EGuidFormats::Digits) + TEXT("|") + Event.NotifyName.ToString());
			};
			for (const FAnimNotifyEvent& Event : Sequence->Notifies)
			{
				bMissingFoley |= !Event.Notify && Event.NotifyName.ToString().StartsWith(TEXT("FoleyEvent")) && !IsVerifiedNamed(Event);
			}
			if (bMissingFoley)
			{
				UPackage* Package = Sequence->GetPackage(); Sequence.Reset();
				FText Error;
				if (!UPackageTools::ReloadPackages({Package}, Error, EReloadPackagesInteractionMode::AssumePositive))
				{
					Fail(Path, TEXT("Reload failed: ") + Error.ToString()); continue;
				}
				Sequence.Reset(LoadObject<UAnimSequenceBase>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path))));
				if (!Sequence.IsValid()) { Fail(Path, TEXT("Reload returned no animation")); continue; }
			}
			const auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("package"), Path);
			TArray<TSharedPtr<FJsonValue>> Payloads;
			TArray<TPair<int32, FProject_JFoleyEvent>> Planned;
			bool bValid = true;
			for (int32 Index = 0; Index < Sequence->Notifies.Num(); ++Index)
			{
				const FAnimNotifyEvent& Event = Sequence->Notifies[Index];
				if (!Event.Notify && Event.NotifyName.ToString().StartsWith(TEXT("FoleyEvent")) && !IsVerifiedNamed(Event))
				{
					Fail(Path, TEXT("Unresolved Foley instance after source restore")); bValid = false; break;
				}
				if (!Event.Notify || !Event.Notify->IsA(LegacyBase.Get())) { continue; }
				FProject_JFoleyEvent Payload; FString Error;
				if (!ReadPayload(Event.Notify, Payload, Error)) { Fail(Path, Error); bValid = false; break; }
				Planned.Emplace(Index, Payload);
				const auto Row = MakeShared<FJsonObject>();
				Row->SetNumberField(TEXT("index"), Index); Row->SetNumberField(TEXT("time"), Event.GetTime());
				Row->SetNumberField(TEXT("trigger_time"), Event.GetTriggerTime());
				Row->SetNumberField(TEXT("track"), Event.TrackIndex); Row->SetStringField(TEXT("guid"), Event.Guid.ToString());
				Row->SetStringField(TEXT("legacy_class"), Event.Notify->GetClass()->GetPathName());
				Row->SetStringField(TEXT("event"), Payload.Event.ToString()); Row->SetNumberField(TEXT("side"), int32(Payload.Side));
				Row->SetNumberField(TEXT("volume"), Payload.VolumeMultiplier); Row->SetNumberField(TEXT("pitch"), Payload.PitchMultiplier);
				Payloads.Add(MakeShared<FJsonValueObject>(Row));
			}
			if (!bValid) { continue; }
			Item->SetArrayField(TEXT("events"), Payloads);
			Item->SetNumberField(TEXT("notify_count"), Sequence->Notifies.Num());
			if (bApply && !Planned.IsEmpty())
			{
				Sequence->Modify();
				for (const auto& Plan : Planned)
				{
					FAnimNotifyEvent& Event = Sequence->Notifies[Plan.Key];
					const FString Before = ExportEvent(Event);
					UAnimNotify* Old = Event.Notify;
					auto* Native = NewObject<UProject_JAnimNotify_FoleyEvent>(Sequence.Get(), NAME_None, RF_Transactional);
					Native->FoleyEvent = Plan.Value;
					Native->NotifyColor = Old->NotifyColor;
					Event.Notify = Native;
					// Compare all serialized metadata, including link time/offsets, GUID and filtering.
					FAnimNotifyEvent Normalized = Event; Normalized.Notify = Old;
					checkf(Before == ExportEvent(Normalized), TEXT("Foley migration changed event metadata"));
					Event.bTriggerOnDedicatedServer = false;
					Old->ClearFlags(RF_Public | RF_Standalone); Old->SetFlags(RF_Transient);
				}
				Sequence->RefreshCacheData(); Sequence->MarkPackageDirty();
				FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
				const FString File = FPaths::Combine(FPaths::ProjectContentDir(), Path.RightChop(6)) + FPackageName::GetAssetPackageExtension();
				if (!UPackage::SavePackage(Sequence->GetPackage(), Sequence.Get(), *File, Args))
				{
					Fail(Path, TEXT("Save failed; stop and restore this package from backup")); break;
				}
				++Saved;
			}
			Replacements += Planned.Num(); Results.Add(MakeShared<FJsonValueObject>(Item));
		}
	}
	Report->SetBoolField(TEXT("apply"), bApply); Report->SetNumberField(TEXT("errors"), Errors);
	Report->SetNumberField(TEXT("replacements"), Replacements); Report->SetNumberField(TEXT("saved"), Saved);
	Report->SetArrayField(TEXT("packages"), Results);
	FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json)); return Json;
}

FString UProject_JFoleyMigrationLibrary::RepairMissingGaspFoley(const TArray<FString>& Packages, const FString& SourceContent, bool bApply)
{
	check(IsInGameThread());
	const auto Report = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> Results;
	int32 Errors = 0, Repaired = 0, Saved = 0;
	auto Fail = [&](const FString& Path, const FString& Reason)
	{
		const auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("package"), Path); Row->SetStringField(TEXT("error"), Reason);
		Results.Add(MakeShared<FJsonValueObject>(Row)); ++Errors;
	};
	// Never shadow project-authored content. Existing imported Audio remains in /Game.
	const FString Blueprints = FPaths::Combine(SourceContent, TEXT("Blueprints")) + TEXT("/");
	const FString SourceRoot = FPaths::ConvertRelativePathToFull(SourceContent) + TEXT("/");
	if (FPaths::IsRelative(SourceContent) || !IFileManager::Get().DirectoryExists(*Blueprints) ||
		IFileManager::Get().DirectoryExists(*FPaths::Combine(FPaths::ProjectContentDir(), TEXT("Blueprints"))) ||
		FPackageName::MountPointExists(TEXT("/FoleySource/")))
	{
		Fail(TEXT("source"), TEXT("Invalid source or mount collision"));
	}
	else
	{
		FPackageName::RegisterMountPoint(TEXT("/FoleySource/"), SourceRoot);
		FPackageName::RegisterMountPoint(TEXT("/Game/Blueprints/"), Blueprints);
		TStrongObjectPtr<UClass> LegacyBase(LoadClass<UAnimNotify>(nullptr,
			TEXT("/Game/Blueprints/AnimNotifies/BP_AnimNotify_FoleyEvent.BP_AnimNotify_FoleyEvent_C")));
		if (!LegacyBase.IsValid()) { Fail(TEXT("source"), TEXT("Cannot load original Foley class")); }
		else for (const FString& Path : Packages)
		{
			if (!Path.StartsWith(TEXT("/Game/Characters/UEFN_Mannequin/Animations/")) || Path.Contains(TEXT("..")))
			{
				Fail(Path, TEXT("Outside imported-animation root")); continue;
			}
			UPackage* Existing = FindPackage(nullptr, *Path);
			if (Existing && Existing->IsDirty()) { Fail(Path, TEXT("Unsaved target changes")); continue; }
			const FString ObjectSuffix = TEXT(".") + FPackageName::GetShortName(Path);
			TStrongObjectPtr<UAnimSequenceBase> Target(LoadObject<UAnimSequenceBase>(nullptr, *(Path + ObjectSuffix)));
			const FString SourcePath = TEXT("/FoleySource/") + Path.RightChop(6);
			TStrongObjectPtr<UAnimSequenceBase> Source(LoadObject<UAnimSequenceBase>(nullptr, *(SourcePath + ObjectSuffix)));
			if (!Target.IsValid() || !Source.IsValid()) { Fail(Path, TEXT("Cannot load target/source animation")); continue; }
			const auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("package"), Path);
			TArray<TPair<int32, FProject_JFoleyEvent>> Planned;
			TArray<TSharedPtr<FJsonValue>> Events;
			bool bValid = true;
			for (int32 Index = 0; Index < Target->Notifies.Num(); ++Index)
			{
				const FAnimNotifyEvent& Event = Target->Notifies[Index];
				if (Event.Notify || Event.NotifyStateClass || !Event.NotifyName.ToString().StartsWith(TEXT("FoleyEvent"))) { continue; }
				const FAnimNotifyEvent* Original = Source->Notifies.FindByPredicate([&](const FAnimNotifyEvent& Value) { return Value.Guid == Event.Guid; });
				if (!Event.Guid.IsValid() || !Original)
				{
					Fail(Path, TEXT("Missing original event GUID")); bValid = false; break;
				}
				FAnimNotifyEvent Normalized = *Original; Normalized.Notify = nullptr;
				if (ExportEvent(Normalized).Replace(TEXT("/FoleySource/"), TEXT("/Game/")) != ExportEvent(Event))
				{
					Fail(Path, TEXT("Target event differs from source; manual review required")); bValid = false; break;
				}
				// Genuine named-only source events remain named-only (e.g. two roll markers).
				if (!Original->Notify) { continue; }
				FProject_JFoleyEvent Payload; FString Error;
				if (!Original->Notify->IsA(LegacyBase.Get()) || !ReadPayload(Original->Notify, Payload, Error))
				{
					Fail(Path, TEXT("Unrecognized original payload: ") + Error); bValid = false; break;
				}
				Planned.Emplace(Index, Payload);
				const auto Row = MakeShared<FJsonObject>();
				Row->SetNumberField(TEXT("index"), Index); Row->SetStringField(TEXT("guid"), Event.Guid.ToString());
				Row->SetNumberField(TEXT("trigger_time"), Event.GetTriggerTime());
				Row->SetStringField(TEXT("event"), Payload.Event.ToString()); Row->SetNumberField(TEXT("side"), int32(Payload.Side));
				Row->SetNumberField(TEXT("volume"), Payload.VolumeMultiplier); Row->SetNumberField(TEXT("pitch"), Payload.PitchMultiplier);
				Events.Add(MakeShared<FJsonValueObject>(Row));
			}
			if (!bValid) { continue; }
			Item->SetArrayField(TEXT("events"), Events);
			if (bApply && !Planned.IsEmpty())
			{
				Target->Modify();
				for (const auto& Plan : Planned)
				{
					FAnimNotifyEvent& Event = Target->Notifies[Plan.Key];
					auto* Native = NewObject<UProject_JAnimNotify_FoleyEvent>(Target.Get(), NAME_None, RF_Transactional);
					Native->FoleyEvent = Plan.Value;
					Event.Notify = Native; Event.bTriggerOnDedicatedServer = false;
				}
				Target->RefreshCacheData(); Target->MarkPackageDirty();
				FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
				const FString File = FPaths::Combine(FPaths::ProjectContentDir(), Path.RightChop(6)) + FPackageName::GetAssetPackageExtension();
				if (!UPackage::SavePackage(Target->GetPackage(), Target.Get(), *File, Args))
				{
					Fail(Path, TEXT("Save failed; restore target backup")); break;
				}
				++Saved;
			}
			Repaired += Planned.Num(); Results.Add(MakeShared<FJsonValueObject>(Item));
		}
		FPackageName::UnRegisterMountPoint(TEXT("/Game/Blueprints/"), Blueprints);
		FPackageName::UnRegisterMountPoint(TEXT("/FoleySource/"), SourceRoot);
	}
	Report->SetBoolField(TEXT("apply"), bApply); Report->SetNumberField(TEXT("errors"), Errors);
	Report->SetNumberField(TEXT("repaired"), Repaired); Report->SetNumberField(TEXT("saved"), Saved);
	Report->SetArrayField(TEXT("packages"), Results);
	FString Json; FJsonSerializer::Serialize(Report, TJsonWriterFactory<>::Create(&Json)); return Json;
}
