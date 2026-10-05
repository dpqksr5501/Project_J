#pragma once
#include "CoreMinimal.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/SecureHash.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"

namespace ProjectJ::Experiments::Migration
{
struct FEntry { FString Target, Backup, Staged, OriginalDigest, PlannedDigest; };
inline FString DigestFile(const FString& Filename)
{
	TArray<uint8> Bytes;
	return FFileHelper::LoadFileToArray(Bytes, *Filename) ? FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num()).ToString() : FString();
}
inline FString Canonical(const FString& Path)
{
	FString Result = FPaths::ConvertRelativePathToFull(Path);
	FPaths::NormalizeFilename(Result); FPaths::CollapseRelativeDirectories(Result); return Result;
}
inline bool ReplaceVerifiedFile(const FString& Target, const FString& Source, const FString& Digest)
{
	const FString Pending = Target + TEXT(".ProjectJMigration-") + FGuid::NewGuid().ToString(EGuidFormats::Digits) + TEXT(".tmp");
	if (IFileManager::Get().Copy(*Pending, *Source, false, true) != COPY_OK || DigestFile(Pending) != Digest) { return false; }
	return IFileManager::Get().Move(*Target, *Pending, true, true) && DigestFile(Target) == Digest;
}
inline bool WriteReceipt(const FString& Receipt, const TArray<FEntry>& Entries, const FString& Phase)
{
	auto Root = MakeShared<FJsonObject>(); Root->SetNumberField(TEXT("version"), 1); Root->SetStringField(TEXT("phase"), Phase);
	TArray<TSharedPtr<FJsonValue>> Rows;
	for (const auto& Entry : Entries)
	{
		auto Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("target"), Canonical(Entry.Target)); Row->SetStringField(TEXT("backup"), Canonical(Entry.Backup));
		Row->SetStringField(TEXT("staged"), Canonical(Entry.Staged)); Row->SetStringField(TEXT("originalDigest"), Entry.OriginalDigest);
		Row->SetStringField(TEXT("plannedDigest"), Entry.PlannedDigest); Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	Root->SetArrayField(TEXT("entries"), Rows); FString Json;
	if (!FJsonSerializer::Serialize(Root, TJsonWriterFactory<>::Create(&Json)) ||
		!FFileHelper::SaveStringToFile(Json, *(Receipt + TEXT(".tmp")), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)) { return false; }
	return IFileManager::Get().Move(*Receipt, *(Receipt + TEXT(".tmp")), true, true);
}

/** Validate every target before restoring any. Reject subsequent author edits, malformed receipts and redirected backups. */
inline bool RestoreReceipt(const FString& Receipt, const TArray<FString>& AllowedTargets, bool bApply)
{
	if (IFileManager::Get().FileSize(*Receipt) > 65536) { return false; }
	FString Json; TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Json, *Receipt) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) || !Root) { return false; }
	double Version; const TArray<TSharedPtr<FJsonValue>>* Rows;
	if (!Root->TryGetNumberField(TEXT("version"), Version) || Version != 1 || !Root->TryGetArrayField(TEXT("entries"), Rows) || Rows->Num() != AllowedTargets.Num()) { return false; }
	TArray<FEntry> Entries;
	const FString Folder = FPaths::GetPath(Canonical(Receipt));
	for (int32 I = 0; I < Rows->Num(); ++I)
	{
		const TSharedPtr<FJsonObject>* Row = nullptr; FEntry Entry;
		if (!(*Rows)[I]->TryGetObject(Row) || !Row || !(*Row)->TryGetStringField(TEXT("target"), Entry.Target) || !(*Row)->TryGetStringField(TEXT("backup"), Entry.Backup) ||
			!(*Row)->TryGetStringField(TEXT("staged"), Entry.Staged) || !(*Row)->TryGetStringField(TEXT("originalDigest"), Entry.OriginalDigest) ||
			!(*Row)->TryGetStringField(TEXT("plannedDigest"), Entry.PlannedDigest)) { return false; }
		Entry.Target = Canonical(Entry.Target); Entry.Backup = Canonical(Entry.Backup); Entry.Staged = Canonical(Entry.Staged);
		if (Entry.Target != Canonical(AllowedTargets[I]) || !FPaths::IsUnderDirectory(Entry.Backup, Folder) || !FPaths::IsUnderDirectory(Entry.Staged, Folder) ||
			Entry.OriginalDigest.IsEmpty() || DigestFile(Entry.Backup) != Entry.OriginalDigest) { return false; }
		const FString CurrentDigest = DigestFile(Entry.Target);
		if (IFileManager::Get().FileExists(*Entry.Target) && CurrentDigest != Entry.OriginalDigest &&
			(Entry.PlannedDigest.IsEmpty() || CurrentDigest != Entry.PlannedDigest)) { return false; }
		Entries.Add(MoveTemp(Entry));
	}
	if (!bApply) { return true; }
	for (const auto& Entry : Entries)
	{
		if (DigestFile(Entry.Target) == Entry.OriginalDigest) { continue; }
		if (!ReplaceVerifiedFile(Entry.Target, Entry.Backup, Entry.OriginalDigest)) { return false; }
	}
	return WriteReceipt(Receipt, Entries, TEXT("Restored"));
}
}
