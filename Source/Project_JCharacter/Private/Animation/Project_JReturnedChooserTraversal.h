#pragma once

#include "Chooser.h"
#include "ChooserFunctionLibrary.h"
#include "UObject/StrongObjectPtr.h"

namespace Project_J::Animation
{
// Returned table objects are a project-level chain, distinct from the engine's
// nested chooser expressions. Malformed content must not stall the game thread.
inline constexpr int32 MaxReturnedChooserTables = 64;

inline UObject* EvaluateReturnedChooserTables(UChooserTable* Root, FChooserEvaluationContext& Context, FString& Path)
{
	check(IsInGameThread());
	Path.Reset();
	TArray<TStrongObjectPtr<UChooserTable>, TInlineAllocator<8>> Visited;
	UObject* Result = Root;
	while (UChooserTable* Table = Cast<UChooserTable>(Result))
	{
		if (!IsValid(Table) || Visited.Num() >= MaxReturnedChooserTables ||
			Visited.ContainsByPredicate([Table](const auto& Pin) { return Pin.Get() == Table; }))
		{
			Path += TEXT(" -> [invalid/cyclic/overlong returned chooser]");
			return nullptr;
		}
		Visited.Emplace(Table);
		if (!Path.IsEmpty()) { Path += TEXT(" -> "); }
		Path += GetNameSafe(Table);
		const FInstancedStruct Expression = UChooserFunctionLibrary::MakeEvaluateChooser(Table);
		Result = Expression.IsValid()
			? UChooserFunctionLibrary::EvaluateObjectChooserBase(Context, Expression, UObject::StaticClass())
			: nullptr;
	}
	return Result;
}
}
