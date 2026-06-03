#pragma once

#include "CoreMinimal.h"
#include "Project_JMMOTypes.generated.h"

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JAccountId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Identity")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JAccountId NewId();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JCharacterId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|Identity")
	FGuid Value;

	bool IsValid() const { return Value.IsValid(); }
	FString ToString() const { return Value.ToString(EGuidFormats::DigitsWithHyphensLower); }
	static FProject_JCharacterId NewId();
};

USTRUCT(BlueprintType)
struct PROJECT_JCORE_API FProject_JWorldInstanceId
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName WorldId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName ZoneId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName InstanceId = NAME_None;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MMO|World")
	FName ChannelId = NAME_None;

	bool IsValid() const { return !WorldId.IsNone() && !ZoneId.IsNone(); }
	FString ToDebugString() const;
};
