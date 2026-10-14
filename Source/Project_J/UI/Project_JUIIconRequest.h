#pragma once

#include "CoreMinimal.h"

class UTexture2D;
struct FStreamableHandle;

/** One widget's icon request; no row, quick-slot or gameplay state is owned here. */
struct FProject_JUIIconRequest
{
	/** False retains the current request/brush for an unchanged icon. */
	bool Select(const FSoftObjectPath& Desired);
	void Load(UObject* Owner, TFunction<void(UTexture2D*, uint64)> Apply);
	void Reset();
	bool IsCurrent(const FSoftObjectPath& Desired, uint64 RequestGeneration) const;
	const FSoftObjectPath& GetPath() const { return Path; }

private:
	FSoftObjectPath Path;
	TSharedPtr<FStreamableHandle> Handle;
	uint64 Generation = 0;
};
