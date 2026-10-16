#include "Authoring/Project_JMeshLODSettings.h"
#include "Authoring/Project_JMeshLODAuthoring.h"
#include "Misc/DataValidation.h"
#include "UObject/UnrealType.h"

UProject_JMeshLODSettings::UProject_JMeshLODSettings(const FObjectInitializer& Initializer):Super(Initializer)
{
    SkeletalLODs=CreateDefaultSubobject<USkeletalMeshLODSettings>(TEXT("SkeletalLODs"));
    // Native LODSettings is MinimalAPI with unexported virtuals; compose it
    // instead of subclassing it. Initialize its reflected editable recipe once.
    const auto* Property=FindFProperty<FArrayProperty>(USkeletalMeshLODSettings::StaticClass(),TEXT("LODGroups"));
    const auto* Inner=Property ? CastField<FStructProperty>(Property->Inner) : nullptr;
    if(!Inner || Inner->Struct!=FSkeletalMeshLODGroupSettings::StaticStruct())return;
    auto& LODGroups=*Property->ContainerPtrToValuePtr<TArray<FSkeletalMeshLODGroupSettings>>(SkeletalLODs);
    LODGroups.SetNum(3);
    StaticMeshLODs.bAutoComputeLODScreenSize=false;
    StaticMeshLODs.ReductionSettings.SetNum(3);
    for(int32 I=0;I<3;++I)
    {
        const float Screen=I==0 ? 1.f : I==1 ? .35f : .18f;
        const float Fraction=I==0 ? 1.f : I==1 ? .5f : .25f;
        LODGroups[I].ScreenSize.Default=Screen;
        LODGroups[I].ReductionSettings.NumOfTrianglesPercentage=Fraction;
        LODGroups[I].ReductionSettings.TerminationCriterion=SMTC_NumOfTriangles;
        LODGroups[I].ReductionSettings.bEnforceBoneBoundaries=true;
        LODGroups[I].ReductionSettings.bRecalcNormals=false;
        StaticMeshLODs.ReductionSettings[I].PercentTriangles=Fraction;
        StaticMeshLODs.ReductionSettings[I].ScreenSize=Screen;
    }
    ValidationRoots={TEXT("/Game/Characters/"),TEXT("/Game/Weapons/"),TEXT("/Game/Equipment/"),TEXT("/Game/Anim_Assets/Great_Sword/Weapon/")};
}
EDataValidationResult UProject_JMeshLODSettings::IsDataValid(FDataValidationContext& Context) const
{
    FString Error;
    if(!ProjectJ::MeshLODAuthoring::ValidatePolicy(*this,Error))
    { Context.AddError(FText::FromString(Error)); return EDataValidationResult::Invalid; }
    return EDataValidationResult::Valid;
}
