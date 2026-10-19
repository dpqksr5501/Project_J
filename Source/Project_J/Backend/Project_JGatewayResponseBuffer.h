#pragma once

#include "CoreMinimal.h"
#include "Misc/ScopeLock.h"

/** HTTP worker owns bytes only. A rejected chunk is never copied or converted to FString. */
class FProject_JGatewayResponseBuffer
{
public:
	explicit FProject_JGatewayResponseBuffer(int32 InLimit) : Limit(FMath::Clamp(InLimit, 1, 16777216)) {}
	void Receive(void* Data, int64& Length)
	{
		FScopeLock Guard(&Mutex);
		if (bExceeded || Length < 0 || Length > Limit - Bytes.Num() || (Length > 0 && !Data))
		{
			bExceeded = true;
			Length = 0;
			Bytes.Reset();
			return;
		}
		if (Length > 0) Bytes.Append(static_cast<const uint8*>(Data), static_cast<int32>(Length));
	}
	bool Read(FString& Out) const
	{
		FScopeLock Guard(&Mutex);
		Out.Reset();
		if (bExceeded) return false;
		if (!Bytes.IsEmpty())
		{
			FUTF8ToTCHAR Converted(reinterpret_cast<const ANSICHAR*>(Bytes.GetData()), Bytes.Num());
			Out = FString(Converted.Length(), Converted.Get());
		}
		return true;
	}
private:
	const int32 Limit;
	mutable FCriticalSection Mutex;
	TArray<uint8> Bytes;
	bool bExceeded = false;
};
