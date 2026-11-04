#include "Crafting/Project_JCraftingRecipeDefinition.h"
#include "CharacterClass/Project_JCharacterDataSubsystem.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Serialization/MemoryWriter.h"
#include "Misc/SecureHash.h"

bool UProject_JCraftingRecipeDefinition::CaptureUsing(
	TFunctionRef<UProject_JItemDefinition*(const TSoftObjectPtr<UProject_JItemDefinition>&)> Resolve,
	ProjectJ::Crafting::FRecipeValues& Out, FString& Digest, FString* Error) const
{
	const auto Fail = [&](const TCHAR* Message) { if (Error) *Error = Message; return false; };
	if (RecipeId.IsNone() || RecipeId.ToString().Len() > 64 || SchemaVersion < 1 || MaxBatches < 1 || MaxBatches > 100 ||
		DurationSecondsPerBatch < 0 || DurationSecondsPerBatch > 86400 ||
		Inputs.IsEmpty() || Inputs.Num() > 16 || Outputs.IsEmpty() || Outputs.Num() > 8)
		return Fail(TEXT("Recipe requires a stable ID/version, bounded batches and 1..16 inputs/1..8 outputs."));
	ProjectJ::Crafting::FRecipeValues Values;
	Values.RecipeId = RecipeId; Values.SchemaVersion = SchemaVersion; Values.MaxBatches = MaxBatches;
	Values.DurationSecondsPerBatch = DurationSecondsPerBatch;
	const auto Lines = [&](const TArray<FProject_JCraftingRecipeLine>& Source, TArray<ProjectJ::Crafting::FResolvedLine>& Destination)
	{
		TSet<FName> Ids;
		for (const auto& Line : Source)
		{
			if (Line.CountPerBatch < 1 || Line.CountPerBatch > 1000000 || Line.ItemLevel < 1 || Line.ItemLevel > 10000)
				return false;
			auto* Definition = Resolve(Line.Definition);
			if (!Definition || Definition->ItemId.IsNone() || Definition->SchemaVersion < 1 ||
				Definition->MaxStackCount < 1 || Ids.Contains(Definition->ItemId)) return false;
			Ids.Add(Definition->ItemId);
			Destination.Add({Definition->ItemId, Definition->SchemaVersion, Line.ItemLevel, Line.CountPerBatch, Definition->MaxStackCount});
		}
		return true;
	};
	if (!Lines(Inputs, Values.Inputs) || !Lines(Outputs, Values.Outputs))
		return Fail(TEXT("Recipe item references, versions, quantities or duplicate IDs are invalid."));
	TArray<uint8> Bytes; FMemoryWriter Writer(Bytes);
	FString Id = RecipeId.ToString().ToLower();
	int32 Version = SchemaVersion, Maximum = MaxBatches;
	Writer << Id << Version << Maximum;
	for (const auto* Set : {&Values.Inputs, &Values.Outputs})
	{
		int32 Count = Set->Num(); Writer << Count;
		for (auto Line : *Set)
		{
			FString Item = Line.Definition.ToString().ToLower();
			Writer << Item << Line.DefinitionVersion << Line.Level << Line.CountPerBatch << Line.MaxStackCount;
		}
	}
	// Keep every existing instant recipe digest byte-for-byte. Timed terms are a
	// domain-separated extension, so changing the agreed duration changes intent.
	if (DurationSecondsPerBatch > 0)
	{
		FString Domain = TEXT("ProjectJ.Crafting.Duration.v1");
		int32 Seconds = DurationSecondsPerBatch;
		Writer << Domain << Seconds;
	}
	uint8 Hash[20]; FSHA1::HashBuffer(Bytes.GetData(), Bytes.Num(), Hash);
	Digest = BytesToHex(Hash, UE_ARRAY_COUNT(Hash)).ToLower();
	Out = MoveTemp(Values);
	if (Error) Error->Reset();
	return true;
}

bool UProject_JCraftingRecipeDefinition::Capture(const UProject_JCharacterDataSubsystem& Registry,
	ProjectJ::Crafting::FRecipeValues& Out, FString& Digest, FString* Error) const
{
	check(IsInGameThread());
	return CaptureUsing([&Registry](const auto& Soft)
	{
		auto* Item = Soft.Get();
		return Item && Registry.GetItemDefinition(Item->ItemId, Item->SchemaVersion) == Item ? Item : nullptr;
	}, Out, Digest, Error);
}
#if WITH_EDITOR
EDataValidationResult UProject_JCraftingRecipeDefinition::IsDataValid(FDataValidationContext& Context) const
{
	const auto Parent = Super::IsDataValid(Context);
	ProjectJ::Crafting::FRecipeValues Values; FString Digest, Error;
	// Selected authoring validation may resolve these at most24 explicit soft refs.
	if (!CaptureUsing([](const auto& Soft) { return Soft.LoadSynchronous(); }, Values, Digest, &Error))
	{
		Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid;
	}
	return Parent == EDataValidationResult::Invalid ? Parent : EDataValidationResult::Valid;
}
#endif
