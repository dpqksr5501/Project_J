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
#include "PresentationMigrationJournal.h"

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
    using namespace ProjectJ::Experiments::Migration;
    const TArray<FString> Paths {
        TEXT("/Game/Animation_Logic/ABPs/ABP_Humanoid_Master"),
        TEXT("/Game/Animation_Logic/ABPs/GreatSword/ABP_Greatsword_Layers"),
        TEXT("/Game/SlashTrail_SoftTofu/Niagara/Basic/NS_SlashTrail_Basic")
    };
    TArray<FString> Targets;
    for (const auto& Path : Paths) { Targets.Add(Canonical(FPackageName::LongPackageNameToFilename(Path, FPackageName::GetAssetPackageExtension()))); }
    const bool bApply = FParse::Param(*Params, TEXT("Apply"));
    const bool bDryRun = FParse::Param(*Params, TEXT("DryRun"));
    const FString JournalRoot = Canonical(FPaths::ProjectSavedDir() / TEXT("Validation/PresentationMigration"));
    FString Recovery;
    if (FParse::Value(*Params, TEXT("RestoreReceipt="), Recovery))
    {
        if (!FPaths::IsUnderDirectory(Canonical(Recovery), JournalRoot) || (!bApply && !bDryRun) || (bApply && bDryRun)) { return 1; }
        const bool bRestored = RestoreReceipt(Recovery, Targets, bApply);
        UE_LOG(LogTemp, Display, TEXT("Migration recovery %s (%s): %s"), bApply ? TEXT("apply") : TEXT("dry run"), bRestored ? TEXT("validated") : TEXT("refused/failed"), *Recovery);
        return bRestored ? 0 : 7;
    }
    if (bApply == bDryRun) { UE_LOG(LogTemp, Error, TEXT("Specify exactly one of -DryRun or -Apply")); return 1; }
    const FString Folder = JournalRoot / FGuid::NewGuid().ToString(EGuidFormats::Digits);
    const FString Receipt = Folder / TEXT("receipt.json");
    TArray<UObject*> Assets;
    TArray<FEntry> Entries;
    for (int32 I = 0; I < Paths.Num(); ++I)
    {
        auto* Asset = LoadObject<UObject>(nullptr, *(Paths[I] + TEXT(".") + FPackageName::GetShortName(Paths[I])));
        if (!Asset || Asset->GetOutermost()->IsDirty()) { UE_LOG(LogTemp, Error, TEXT("Missing or dirty source %s"), *Paths[I]); return 3; }
        if (bDryRun)
        {
            Asset = DuplicateObject<UObject>(Asset, GetTransientPackage(), MakeUniqueObjectName(GetTransientPackage(), Asset->GetClass(), TEXT("ProjectJMigrationPlan")));
            Asset->SetFlags(RF_Transient);
        }
        Assets.Add(Asset);
        if (bApply)
        {
            IFileManager::Get().MakeDirectory(*Folder, true);
            FEntry Entry; Entry.Target = Targets[I]; Entry.Backup = Folder / (FString::FromInt(I) + TEXT("-original.uasset"));
            Entry.Staged = Folder / (FString::FromInt(I) + TEXT("-staged.uasset")); Entry.OriginalDigest = DigestFile(Entry.Target);
            if (Entry.OriginalDigest.IsEmpty() || IFileManager::Get().Copy(*Entry.Backup, *Entry.Target, false) != COPY_OK || DigestFile(Entry.Backup) != Entry.OriginalDigest) { return 2; }
            Entries.Add(MoveTemp(Entry));
        }
    }
    if (bApply && !WriteReceipt(Receipt, Entries, TEXT("Prepared"))) { return 2; }
    FString Csv = TEXT("asset,replaced_getters,bound_before,bound_after\n");
    for (int32 I = 0; I < 2; ++I)
    {
        auto* BP = Cast<UAnimBlueprint>(Assets[I]); if (!BP) { return 4; }
        const int32 Before = CountBound(BP);
        const int32 Replaced = ReplaceSnapshotGetters(BP);
        FCompilerResultsLog Log;
        if (Replaced) { FKismetEditorUtilities::CompileBlueprint(BP, EBlueprintCompileOptions::SkipGarbageCollection, &Log); }
        int32 Connected = 0; TArray<UEdGraph*> Graphs; BP->GetAllGraphs(Graphs);
        for (auto* Graph : Graphs) for (UEdGraphNode* Node : Graph->Nodes)
        {
            auto* Get = Cast<UK2Node_VariableGet>(Node);
            if (!Get || !Get->VariableReference.IsSelfContext()) { continue; }
            const auto Name = Get->VariableReference.GetMemberName();
            if (Name != TEXT("GraphAimYaw") && Name != TEXT("GraphAimPitch") && Name != TEXT("GraphAimOffsetAlpha") && Name != TEXT("GraphCombatSpeed") && Name != TEXT("GraphCombatDirection")) { continue; }
            auto* Pin = Get->FindPin(Name, EGPD_Output); Connected += Pin && !Pin->LinkedTo.IsEmpty();
        }
        const int32 After = CountBound(BP);
        if (Log.NumErrors || Connected != (I == 0 ? 3 : 2) || (Replaced && After >= Before))
        { UE_LOG(LogTemp, Error, TEXT("Unexpected/invalid graph; no source files published")); return 5; }
        Csv += FString::Printf(TEXT("%s,%d,%d,%d\n"), *Paths[I], Replaced, Before, After);
    }
    auto* System = Cast<UNiagaraSystem>(Assets[2]); if (!System) { return 4; }
    auto& Overrides = System->GetScalabilityOverrides().Overrides;
    if (Overrides.IsEmpty()) { Overrides.AddDefaulted(); }
    for (auto& Override : Overrides)
    { Override.bOverrideDistanceSettings = true; Override.bCullByDistance = true; Override.MaxDistance = 2500; }
    System->SetOverrideScalabilitySettings(true);
    System->bFixedBounds = true; System->SetFixedBounds(FBox(FVector(-350), FVector(350)));
    System->UpdateScalability(); System->MarkPackageDirty();
    if (bDryRun)
    {
        Save(TEXT("animation-dry-run.csv"), Csv);
        UE_LOG(LogTemp, Display, TEXT("Transient plan validated; no source assets saved")); return 0;
    }
    // Stage every package before publishing any source file. Receipt contains both
    // digests before publication, so an interrupted run can be safely reconciled.
    for (int32 I = 0; I < Assets.Num(); ++I)
    {
        FSavePackageArgs Args; Args.TopLevelFlags = RF_Public | RF_Standalone; Args.SaveFlags = SAVE_NoError;
        if (!UPackage::SavePackage(Assets[I]->GetOutermost(), Assets[I], *Entries[I].Staged, Args)) { return 6; }
        Entries[I].PlannedDigest = DigestFile(Entries[I].Staged);
        if (Entries[I].PlannedDigest.IsEmpty()) { return 6; }
    }
    if (!WriteReceipt(Receipt, Entries, TEXT("ReadyToPublish"))) { return 6; }
    // Refuse to overwrite source edits made while the plan was prepared.
    for (const auto& Entry : Entries) { if (DigestFile(Entry.Target) != Entry.OriginalDigest) { return 7; } }
    for (const auto& Entry : Entries)
    {
        if (!ReplaceVerifiedFile(Entry.Target, Entry.Staged, Entry.PlannedDigest))
        {
            const bool bRestored = RestoreReceipt(Receipt, Targets, true);
            UE_LOG(LogTemp, Error, TEXT("Publish failed. Verified rollback=%d; receipt=%s"), bRestored, *Receipt); return 6;
        }
    }
    if (!WriteReceipt(Receipt, Entries, TEXT("Published")))
    { UE_LOG(LogTemp, Error, TEXT("Published assets; completion receipt needs reconciliation: %s"), *Receipt); return 8; }
    Save(TEXT("animation-applied.csv"), Csv);
    UE_LOG(LogTemp, Display, TEXT("Published three validated assets; receipt/backups: %s"), *Receipt);
    return 0;
}
