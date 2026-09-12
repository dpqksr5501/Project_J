#include "Misc/AutomationTest.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimClassInterface.h"
#include "Animation/AnimSubsystem_Base.h"
#include "K2Node_CallFunction.h"
#include "K2Node_VariableGet.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "KismetCompiler.h"
#include "EdGraph/EdGraph.h"
#include "ExperimentOutput.h"
#include "ProjectJPresentationMigrationCommandlet.h"
#include "NiagaraSystem.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "HAL/FileManager.h"

namespace ProjectJ::Experiments
{
static int32 CountBound(const UAnimBlueprint* BP)
{
    const auto* Interface = IAnimClassInterface::GetFromClass(BP->GeneratedClass);
    const auto* Base = Interface ? Interface->FindSubsystem<FAnimSubsystem_Base>() : nullptr;
    int32 Bound = 0;
    if (Base) for (const auto& Handler : Base->GetExposedValueHandlers())
    {
        if (Handler.GetHandlerStruct()->IsChildOf(FAnimNodeExposedValueHandler_Base::StaticStruct()))
        { Bound += !static_cast<const FAnimNodeExposedValueHandler_Base*>(Handler.GetHandler())->BoundFunction.IsNone(); }
    }
    return Bound;
}

static int32 ReplaceSnapshotGetters(UAnimBlueprint* BP)
{
    const TMap<FName, FName> Replacements {
        {TEXT("GetThreadSafeAimYaw"), TEXT("GraphAimYaw")},
        {TEXT("GetThreadSafeAimPitch"), TEXT("GraphAimPitch")},
        {TEXT("GetThreadSafeAimOffsetAlpha"), TEXT("GraphAimOffsetAlpha")},
        {TEXT("GetThreadSafeCombatLocomotionSpeed"), TEXT("GraphCombatSpeed")},
        {TEXT("GetThreadSafeCombatLocomotionDirection"), TEXT("GraphCombatDirection")}
    };
    TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
    int32 Changed = 0;
    for (auto* Graph : Graphs)
    {
        // Snapshot the list: replacement inserts/removes nodes.
        const auto Nodes = Graph->Nodes;
        for (UEdGraphNode* Node : Nodes)
        {
            auto* Call = Cast<UK2Node_CallFunction>(Node);
            const FName* Property = Call ? Replacements.Find(Call->FunctionReference.GetMemberName()) : nullptr;
            if (!Property || !Call->FunctionReference.IsSelfContext()) { continue; }
            auto* Old = Call->FindPin(TEXT("ReturnValue"), EGPD_Output);
            if (!Old || Old->LinkedTo.IsEmpty()) { continue; }
            auto* Get = NewObject<UK2Node_VariableGet>(Graph);
            Get->VariableReference.SetSelfMember(*Property); Get->CreateNewGuid();
            Get->NodePosX = Call->NodePosX; Get->NodePosY = Call->NodePosY;
            Graph->AddNode(Get, false, false); Get->AllocateDefaultPins();
            auto* Output = Get->FindPin(*Property, EGPD_Output);
            if (!Output || Output->PinType != Old->PinType) { Graph->RemoveNode(Get); continue; }
            const auto Links = Old->LinkedTo;
            for (auto* Link : Links) { Old->BreakLinkTo(Link); Output->MakeLinkTo(Link); }
            FBlueprintEditorUtils::RemoveNode(BP, Call, true); ++Changed;
        }
    }
    if (Changed) { FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(BP); }
    return Changed;
}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationMigrationPreflight, "ProjectJ.GroupE.AnimationMigrationPreflight",
    EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationMigrationPreflight::RunTest(const FString&)
{
    using namespace ProjectJ::Experiments;
    FString Csv = TEXT("asset,replaced_getters,bound_before,bound_after\n");
    for (const TCHAR* Path : {TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master.ABP_Humanoid_Master"),
        TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Layers.ABP_Greatsword_Layers")})
    {
        auto* Source = LoadObject<UAnimBlueprint>(nullptr, Path);
        if (!TestNotNull(TEXT("Source ABP exists"), Source)) { continue; }
        const bool Dirty = Source->GetOutermost()->IsDirty();
        auto* Copy = DuplicateObject<UAnimBlueprint>(Source, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), UAnimBlueprint::StaticClass(), TEXT("ProjectJFPreflight")));
        Copy->SetFlags(RF_Transient);
        const int32 Before = CountBound(Source);
        const int32 Replaced = ReplaceSnapshotGetters(Copy);
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(Copy, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        TestEqual(TEXT("Migrated transient graph compiles"), Log.NumErrors, 0);
        int32 ConnectedSnapshots = 0;
        TArray<UEdGraph*> Graphs; Copy->GetAllGraphs(Graphs);
        for (auto* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
        {
            auto* Get = Cast<UK2Node_VariableGet>(Node);
            if (!Get || !Get->VariableReference.IsSelfContext()) { continue; }
            const FName Name = Get->VariableReference.GetMemberName();
            if (Name != TEXT("GraphAimYaw") && Name != TEXT("GraphAimPitch") && Name != TEXT("GraphAimOffsetAlpha") &&
                Name != TEXT("GraphCombatSpeed") && Name != TEXT("GraphCombatDirection")) { continue; }
            auto* Pin = Get->FindPin(Name, EGPD_Output);
            ConnectedSnapshots += Pin && !Pin->LinkedTo.IsEmpty();
        }
        TestEqual(TEXT("All expected snapshot properties are connected"), ConnectedSnapshots,
            FString(Path).Contains(TEXT("Master")) ? 3 : 2);
        const int32 After = CountBound(Copy);
        TestTrue(TEXT("Migration decreases handlers; already applied graph stays stable"), Replaced ? After < Before : After == Before);
        TestEqual(TEXT("Original package dirtiness unchanged"), Source->GetOutermost()->IsDirty(), Dirty);
        Csv += FString::Printf(TEXT("%s,%d,%d,%d\n"), Path, Replaced, Before, After);
    }
    TestTrue(TEXT("Save migration preflight"), Save(TEXT("animation-preflight.csv"), Csv));
    return true;
}

int32 UProjectJPresentationMigrationCommandlet::Main(const FString& Params)
{
    using namespace ProjectJ::Experiments;
    // Save permission is supplied by the user, not inferred from loading this
    // plugin. The operator must explicitly invoke this separate commandlet.
    if (!FParse::Param(*Params, TEXT("Apply"))) { UE_LOG(LogTemp, Error, TEXT("Explicit -Apply is required")); return 1; }
    const TArray<FString> Paths {
        TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master"),
        TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Layers"),
        TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic")
    };
    const FString Backup = FPaths::ProjectSavedDir() / TEXT("Validation/PresentationMigration") / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    IFileManager::Get().MakeDirectory(*Backup, true);
    TArray<UObject*> Assets;
    for (const FString& Path : Paths)
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension());
        const FString BackupFile = Backup / (FPackageName::GetShortName(Path) + TEXT(".uasset"));
        if (IFileManager::Get().Copy(*BackupFile, *Filename, false) != COPY_OK) { UE_LOG(LogTemp, Error, TEXT("Cannot back up %s"), *Path); return 2; }
        auto* Asset = LoadObject<UObject>(nullptr, *(Path + TEXT(".") + FPackageName::GetShortName(Path)));
        if (!Asset || Asset->GetOutermost()->IsDirty()) { UE_LOG(LogTemp, Error, TEXT("Missing or dirty source %s"), *Path); return 3; }
        Assets.Add(Asset);
    }
    FString Csv = TEXT("asset,replaced_getters,bound_before,bound_after\n");
    // Complete all transformations and compilation before the first save.
    for (int32 I = 0; I < 2; ++I)
    {
        auto* BP = CastChecked<UAnimBlueprint>(Assets[I]);
        const int32 Before = CountBound(BP);
        const int32 Replaced = ReplaceSnapshotGetters(BP);
        if (Replaced != (I == 0 ? 3 : 2)) { UE_LOG(LogTemp, Error, TEXT("Unexpected source graph; nothing saved")); return 4; }
        FCompilerResultsLog Log;
        FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log);
        const int32 After = CountBound(BP);
        if (Log.NumErrors || After >= Before) { UE_LOG(LogTemp, Error, TEXT("Compile/handler validation failed; nothing saved")); return 5; }
        Csv += FString::Printf(TEXT("%s,%d,%d,%d\n"), *Paths[I], Replaced, Before, After);
    }
    auto* System = CastChecked<UNiagaraSystem>(Assets[2]);
    auto& Overrides = System->GetScalabilityOverrides().Overrides;
    if (Overrides.IsEmpty()) { Overrides.AddDefaulted(); }
    for (auto& Override : Overrides)
    { Override.bOverrideDistanceSettings = true; Override.bCullByDistance = true; Override.MaxDistance = 2500; }
    System->SetOverrideScalabilitySettings(true);
    System->bFixedBounds = true; System->SetFixedBounds(FBox(FVector(-350), FVector(350)));
    System->UpdateScalability(); System->MarkPackageDirty();
    for (int32 I = 0; I < Assets.Num(); ++I)
    {
        const FString Filename = FPackageName::LongPackageNameToFilename(Paths[I], FPackageName::GetAssetPackageExtension());
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Assets[I]->GetOutermost(), Assets[I], *Filename, Args))
        { UE_LOG(LogTemp, Error, TEXT("Save failed; backups: %s"), *Backup); return 6; }
    }
    Save(TEXT("animation-applied.csv"), Csv);
    UE_LOG(LogTemp, Display, TEXT("Saved exactly three presentation assets. Backups: %s"), *Backup);
    return 0;
}
