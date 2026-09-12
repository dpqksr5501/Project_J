#include "MMO/FeatureCatalog.h"

namespace ProjectJ::MMO
{
bool FFeatureCatalog::Register(FFeatureDefinition Definition, FString& Error)
{
    Error.Reset();
    if (Definition.Id.IsNone() || Definition.Domain.IsNone() || Definition.StateOwner.IsNone() ||
        Definitions.Contains(Definition.Id))
    { Error = TEXT("Invalid or duplicate feature definition"); return false; }
    Definitions.Add(Definition.Id, MoveTemp(Definition));
    return true;
}

bool FFeatureCatalog::Resolve(const TArray<FName>& Requested, TArray<FName>& OutOrder, FString& Error) const
{
    OutOrder.Reset(); Error.Reset();
    TMap<FName, uint8> Marks;
    TArray<FName> Result;
    TFunction<bool(FName)> Visit = [&](FName Id)
    {
        const uint8 Mark = Marks.FindRef(Id);
        if (Mark == 2) { return true; }
        if (Mark == 1) { Error = TEXT("Dependency cycle at ") + Id.ToString(); return false; }
        const auto* Definition = Find(Id);
        if (!Definition) { Error = TEXT("Unknown dependency: ") + Id.ToString(); return false; }
        Marks.Add(Id, 1);
        for (FName Dependency : Definition->Dependencies) { if (!Visit(Dependency)) { return false; } }
        Marks[Id] = 2; Result.Add(Id); return true;
    };
    for (FName Id : Requested) { if (!Visit(Id)) { return false; } }
    OutOrder = MoveTemp(Result); return true;
}

bool FFeatureCatalog::Validate(FString& Error) const
{
    TArray<FName> Keys, Order; Definitions.GetKeys(Keys);
    Keys.Sort(FNameLexicalLess());
    return Resolve(Keys, Order, Error);
}

FFeatureCatalog MakeFoundationCatalog()
{
    FFeatureCatalog Catalog;
    FString Error;
    auto Add = [&](const TCHAR* Id, const TCHAR* Label, const TCHAR* Domain, const TCHAR* Owner, const TCHAR* Dependencies)
    {
        FFeatureDefinition Definition { FName(Id), Label, FName(Domain), FName(Owner), {} };
        TArray<FString> Names; FString(Dependencies).ParseIntoArray(Names, TEXT(","), true);
        for (const FString& Name : Names) { Definition.Dependencies.Add(FName(*Name)); }
        const bool Added = Catalog.Register(MoveTemp(Definition), Error);
        checkf(Added, TEXT("%s"), *Error);
    };
#define PROJECTJ_FEATURE(Id, Label, Domain, Owner, Dependencies) Add(TEXT(Id), TEXT(Label), TEXT(Domain), TEXT(Owner), TEXT(Dependencies));
#include "FeatureCatalog.inl"
#undef PROJECTJ_FEATURE
    return Catalog;
}
}
