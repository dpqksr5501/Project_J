#include "UI/Project_JUIIconRequest.h"

#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

bool FProject_JUIIconRequest::Select(const FSoftObjectPath& Desired)
{
	if (Path == Desired) { return false; }
	Reset();
	Path = Desired;
	return true;
}

void FProject_JUIIconRequest::Load(UObject* Owner, TFunction<void(UTexture2D*, uint64)> Apply)
{
	check(IsInGameThread());
	if (!IsValid(Owner) || Path.IsNull()) { return; }
	const auto RequestedPath = Path;
	const auto RequestedGeneration = Generation;
	Handle = UAssetManager::GetStreamableManager().RequestAsyncLoad(RequestedPath,
		[WeakOwner = TWeakObjectPtr<UObject>(Owner), this, RequestedPath, RequestedGeneration, Apply = MoveTemp(Apply)]()
		{
			// Owner validity must be checked before reading this embedded value.
			TStrongObjectPtr<UObject> PinnedOwner(WeakOwner.Get());
			if (!PinnedOwner.IsValid() || !IsCurrent(RequestedPath, RequestedGeneration)) { return; }
			TStrongObjectPtr<UTexture2D> Texture(Cast<UTexture2D>(RequestedPath.ResolveObject()));
			if (Texture.IsValid()) { Apply(Texture.Get(), RequestedGeneration); }
		});
}

void FProject_JUIIconRequest::Reset()
{
	++Generation;
	auto Previous = MoveTemp(Handle);
	Path.Reset();
	if (Previous.IsValid()) { Previous->CancelHandle(); }
}

bool FProject_JUIIconRequest::IsCurrent(const FSoftObjectPath& Desired, uint64 RequestGeneration) const
{
	return Generation == RequestGeneration && Path == Desired;
}
