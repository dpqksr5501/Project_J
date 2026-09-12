#pragma once
#include "CoreMinimal.h"

namespace ProjectJ::MMO
{
// Catalog entries describe extension contracts, not installed/running content.
struct FFeatureDefinition
{
    FName Id;
    FString Label;
    FName Domain;
    FName StateOwner; // Account / Character / Group / World / Service / Client
    TArray<FName> Dependencies;
};

class PROJECT_JMMO_API FFeatureCatalog
{
public:
    bool Register(FFeatureDefinition Definition, FString& Error);
    // Transactional output: unknown IDs/cycles leave OutOrder empty.
    bool Resolve(const TArray<FName>& Requested, TArray<FName>& OutOrder, FString& Error) const;
    bool Validate(FString& Error) const;
    const FFeatureDefinition* Find(FName Id) const { return Definitions.Find(Id); }
    int32 Num() const { return Definitions.Num(); }
private:
    TMap<FName, FFeatureDefinition> Definitions;
};

PROJECT_JMMO_API FFeatureCatalog MakeFoundationCatalog();
}
