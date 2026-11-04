#include "Companion/Project_JCompanionItemDefinition.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

bool UProject_JCompanionItemDefinition::IsFollowDefinitionValid() const
{
    return !ItemId.IsNone() && SchemaVersion >= 1 && MaxStackCount >= 1 && CompanionClass.ToSoftObjectPath().IsValid()
        && FMath::IsFinite(FollowDistance) && FollowDistance >= 50 && FollowDistance <= 2000
        && FMath::IsFinite(SpawnDistance) && SpawnDistance >= 100 && SpawnDistance <= 1000;
}
#if WITH_EDITOR
EDataValidationResult UProject_JCompanionItemDefinition::IsDataValid(FDataValidationContext& Context) const
{
    const auto ParentResult = Super::IsDataValid(Context);
    if (!IsFollowDefinitionValid())
    {
        Context.AddError(NSLOCTEXT("ProjectJCompanion", "InvalidFollowDefinition", "Owner-follow items require an ID/version, NPC class path and finite follow/spawn distances within their authored bounds."));
        return EDataValidationResult::Invalid;
    }
    return ParentResult == EDataValidationResult::Invalid ? ParentResult : EDataValidationResult::Valid;
}
#endif
