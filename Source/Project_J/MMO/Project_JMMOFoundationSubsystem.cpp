#include "MMO/Project_JMMOFoundationSubsystem.h"

void UProject_JMMOFoundationSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);
    Catalog = ProjectJ::MMO::MakeFoundationCatalog();
    FString Error;
    if (!Catalog.Validate(Error))
    {
        UE_LOG(LogTemp, Error, TEXT("MMO extension catalog rejected: %s"), *Error);
        Catalog = {}; // No partially usable catalog.
    }
}
bool UProject_JMMOFoundationSubsystem::ResolveContentDependencies(
    const TArray<FName>& Requested, TArray<FName>& OutOrder, FString& Error) const
{
    check(IsInGameThread());
    return Catalog.Resolve(Requested, OutOrder, Error);
}
