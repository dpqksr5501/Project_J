#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/PackageName.h"
#include "Authoring/Project_JMeshLODAuthoring.h"
#include "Authoring/Project_JMeshLODSettings.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Animation/MorphTarget.h"
#include "AssetCompilingManager.h"
#include "LODUtilities.h"
#include "UObject/SavePackage.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "HAL/FileManager.h"
#include <limits>
#include "Tests/Project_JMeshLODTestActor.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/Character.h"
#include "GameFramework/PlayerController.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/ScopeExit.h"
#include "Validation/Project_JMeshLODAssetValidator.h"
#include "Misc/DataValidation.h"

namespace
{
constexpr auto Flags=EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter;
USkeletalMesh* SingleBody(const TCHAR* Path=TEXT("/Game/Characters/GreatSword/GreatSword_Woman.GreatSword_Woman"))
{
    auto* Source=LoadObject<USkeletalMesh>(nullptr,Path);
    if(!Source)return nullptr;
    auto* Copy=DuplicateObject<USkeletalMesh>(Source,GetTransientPackage());TArray<UObject*> Pending{Copy};
    FAssetCompilingManager::Get().FinishCompilationForObjects(Pending);
    // This fixture intentionally removes authored levels, so establish a valid
    // new single-LOD input without changing the real asset's platform policies.
    Copy->SetMinLod(FPerPlatformInt(0));Copy->SetQualityLevelMinLod(FPerQualityLevelInt(0));
    FSkeletalMeshUpdateContext Context;Context.SkeletalMesh=Copy;TArray<int32> Remove;
    for(int32 I=1;I<Copy->GetLODNum();++I)Remove.Add(I);
    if(!Remove.IsEmpty())FLODUtilities::RemoveLODs(Context,Remove);
    FAssetCompilingManager::Get().FinishCompilationForObjects(Pending);
    return Copy;
}
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeshLODContracts,"ProjectJ.Authoring.MeshLOD.Contracts",Flags)
bool FProjectJMeshLODContracts::RunTest(const FString&)
{
    using namespace ProjectJ::MeshLODAuthoring;
    auto* Policy=NewObject<UProject_JMeshLODSettings>();FString Error;
    TestTrue(TEXT("Common native defaults valid"),ValidatePolicy(*Policy,Error));
    auto* Copy=DuplicateObject<UProject_JMeshLODSettings>(Policy,GetTransientPackage());
    TestTrue(TEXT("Native settings are owned per policy, not a shared mutable CDO"),Copy->SkeletalLODs!=Policy->SkeletalLODs && Copy->SkeletalLODs->GetOuter()==Copy);
    TestTrue(TEXT("Recipe cannot become a runtime dependency"),Policy->IsEditorOnly());
    Policy->StaticMeshLODs.ReductionSettings[1].PercentTriangles=std::numeric_limits<float>::quiet_NaN();
    TestFalse(TEXT("Nonfinite recipe rejected before generation"),ValidatePolicy(*Policy,Error));
    Policy=NewObject<UProject_JMeshLODSettings>();
    const_cast<FSkeletalMeshLODGroupSettings&>(Policy->SkeletalLODs->GetSettingsForLODLevel(1)).BoneList.AddDefaulted();
    TestFalse(TEXT("Shared bone-removal recipe rejected"),ValidatePolicy(*Policy,Error));
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeshLODGeneration,"ProjectJ.Authoring.MeshLOD.NativeGeneration",Flags)
bool FProjectJMeshLODGeneration::RunTest(const FString&)
{
    using namespace ProjectJ::MeshLODAuthoring;
    auto* Policy=NewObject<UProject_JMeshLODSettings>();auto* Body=SingleBody();
    auto* Manny=SingleBody(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple.SKM_Manny_Simple"));
    auto* Equipment=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(!TestNotNull(TEXT("Single-LOD skeletal fixture"),Body)||!TestNotNull(TEXT("Second character fixture"),Manny)||!TestNotNull(TEXT("Engine static fixture"),Equipment))return false;
    for(UObject* Source:TArray<UObject*>{Body,Manny,Equipment})
    {
        const FString Before=ProtectedFingerprint(Source);FString Error;
        auto Draft=Preview(Source,*Policy,ERole::Surface,Error);
        if(!TestNotNull(*Error,Draft.Get()))continue;
        TestEqual(TEXT("Preview never mutates the source"),ProtectedFingerprint(Source),Before);
        TestTrue(TEXT("Native output is actually reduced and structurally valid"),ValidateGenerated(Draft.Get(),*Policy,Error));
        const FString Preserved=ProtectedFingerprint(Draft.Get());
        TestTrue(TEXT("Generated chain becomes an existing preserved chain"),Inspect(Draft.Get(),*Policy,ERole::Surface).Status==EStatus::Existing);
        TestFalse(TEXT("A second generation cannot replace existing authored LODs"),Generate(Draft.Get(),*Policy,ERole::Surface,Error));
        TestEqual(TEXT("Repeated request preserves data"),ProtectedFingerprint(Draft.Get()),Preserved);
    }
    return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeshLODExceptions,"ProjectJ.Authoring.MeshLOD.Exceptions",Flags)
bool FProjectJMeshLODExceptions::RunTest(const FString&)
{
    using namespace ProjectJ::MeshLODAuthoring;
    auto* Policy=NewObject<UProject_JMeshLODSettings>();auto* Body=SingleBody();
    if(!TestNotNull(TEXT("Fixture"),Body))return false;
    TestTrue(TEXT("No implicit assumption that a mesh is not hair"),Inspect(Body,*Policy,ERole::Unclassified).Status==EStatus::Review);
    TestTrue(TEXT("Hair/card workflow excluded"),Inspect(Body,*Policy,ERole::Hair).Status==EStatus::Review);
    Policy->ManualReviewAssets.Add(FSoftObjectPath(Body));
    TestTrue(TEXT("Per-asset review override wins over surface classification"),Inspect(Body,*Policy,ERole::Surface).Status==EStatus::Review);
    Policy->ManualReviewAssets.Reset();
    FPerPlatformInt Minimum(0);Minimum.PerPlatform.Add(TEXT("Mobile"),1);Body->SetMinLod(Minimum);
    TestTrue(TEXT("Latent platform minimum cannot silently lower local quality after adding LODs"),Inspect(Body,*Policy,ERole::Surface).Status==EStatus::Review);
    Body->SetMinLod(FPerPlatformInt(0));
    Body->GetMorphTargets().Add(NewObject<UMorphTarget>(Body));
    TestTrue(TEXT("Morph targets require native authoring review and remain untouched"),Inspect(Body,*Policy,ERole::Surface).Status==EStatus::Review);
    Body->GetMorphTargets().Reset();
    Body->GetSkinWeightProfiles().AddDefaulted();
    TestTrue(TEXT("Alternate weight profiles require review"),Inspect(Body,*Policy,ERole::Surface).Status==EStatus::Review);
    Body->GetSkinWeightProfiles().Reset();
    auto* Cloak=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/GreatSword/Clothes/cloak.cloak"));
    if(TestNotNull(TEXT("Actual clothing-bound mesh"),Cloak))TestTrue(TEXT("Clothing bindings are preserved rather than regenerated"),Inspect(Cloak,*Policy,ERole::Surface).Status==EStatus::Review);
    TestTrue(TEXT("Non-mesh assets use their native workflow"),Inspect(Policy,*Policy,ERole::Surface).Status==EStatus::Review);
    return true;
}

// Explicit one-off integration input preparation. Only new UUID packages are
// created; ordinary automation never saves a content asset.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeshLODIntegrationInputs,"ProjectJ.Authoring.MeshLOD.IntegrationInputs",Flags)
bool FProjectJMeshLODIntegrationInputs::RunTest(const FString&)
{
    FString Folder;if(!FParse::Value(FCommandLine::Get(),TEXT("ProjectJMeshLODIntegrationFolder="),Folder))return true;
    if(!TestTrue(TEXT("Integration fixtures live under a dedicated generated folder"),Folder.StartsWith(TEXT("/Game/Developers/LODAuthoringValidation_")) && !Folder.Contains(TEXT(".."))))return false;
    auto* Body=SingleBody();auto* Sphere=LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Sphere.Sphere"));
    if(!Body || !Sphere)return false;
    int32 I=0;FString Paths;
    for(UObject* Source:TArray<UObject*>{Body,Sphere})
    {
        const FString Package=Folder/(I++==0 ? TEXT("SK_TestBody") : TEXT("SM_TestEquipment"));
        if(!TestFalse(TEXT("Never overwrite a test or user package"),FPackageName::DoesPackageExist(Package)))return false;
        auto* Pkg=CreatePackage(*Package);auto* Copy=DuplicateObject(Source,Pkg,*FPackageName::GetShortName(Package));Copy->SetFlags(RF_Public|RF_Standalone);
        const FString File=FPackageName::LongPackageNameToFilename(Package,FPackageName::GetAssetPackageExtension());
        IFileManager::Get().MakeDirectory(*FPaths::GetPath(File),true);FSavePackageArgs Args;Args.TopLevelFlags=RF_Public|RF_Standalone;
        if(!TestTrue(TEXT("Save only newly generated integration fixture"),UPackage::SavePackage(Pkg,Copy,*File,Args)))return false;
        Paths+=Copy->GetPathName()+TEXT("\n");
    }
    const FString Out=FPaths::ProjectSavedDir()/TEXT("MeshLODIntegrationInputs.txt");
    TestTrue(TEXT("Integration paths recorded"),FFileHelper::SaveStringToFile(Paths,*Out));return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeshLODLocalWeapon,"ProjectJ.Authoring.MeshLOD.LocalWeaponLifecycle",Flags)
bool FProjectJMeshLODLocalWeapon::RunTest(const FString&)
{
    auto* World=UWorld::CreateWorld(EWorldType::Game,false);GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
    ON_SCOPE_EXIT {World->BeginTearingDown();World->EndPlay(EEndPlayReason::Quit);World->DestroyWorld(false);GEngine->DestroyWorldContext(World);};
    World->InitializeActorsForPlay(FURL());
    auto* Pawn=World->SpawnActor<ACharacter>();auto* Controller=World->SpawnActor<APlayerController>();Controller->SetAsLocalPlayerController();
    FActorSpawnParameters Params;Params.Owner=Pawn;
    auto* Weapon=World->SpawnActor<AProject_JMeshLODTestWeapon>(Params);
    World->GetWorldSettings()->NotifyBeginPlay();World->GetWorldSettings()->NotifyMatchStarted();
    TestTrue(TEXT("Fixture executes the actor's real BeginPlay"),Weapon->HasActorBegunPlay());
    TestEqual(TEXT("Uncontrolled/remote presentation retains automatic LOD"),Weapon->GetWeaponMesh()->GetForcedLodModel(),0);
    Controller->Possess(Pawn);
    TestTrue(TEXT("Fixture has the actual local player controller even before PlayerState"),Pawn->GetController()==Controller && Pawn->IsLocallyControlled());
    TestEqual(TEXT("Possession protects local static equipment at LOD0"),Weapon->GetWeaponMesh()->GetForcedLodModel(),1);
    Controller->UnPossess();
    TestEqual(TEXT("Unpossess restores automatic remote LOD"),Weapon->GetWeaponMesh()->GetForcedLodModel(),0);
    Controller->Possess(Pawn);Weapon->SetOwner(nullptr);
    TestEqual(TEXT("Owner transfer releases the previous local lease"),Weapon->GetWeaponMesh()->GetForcedLodModel(),0);
    Weapon->SetOwner(Pawn);
    TestEqual(TEXT("Owner transfer back acquires local quality"),Weapon->GetWeaponMesh()->GetForcedLodModel(),1);
    Weapon->GetWeaponMesh()->SetForcedLodModel(3);Controller->UnPossess();
    TestEqual(TEXT("New external forced LOD is not overwritten during release"),Weapon->GetWeaponMesh()->GetForcedLodModel(),3);
    Controller->Possess(Pawn);
    TestEqual(TEXT("Authored forced LOD policy retains ownership"),Weapon->GetWeaponMesh()->GetForcedLodModel(),3);
    Weapon->Destroy();return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJMeshLODValidator,"ProjectJ.Authoring.MeshLOD.AssetValidation",Flags)
bool FProjectJMeshLODValidator::RunTest(const FString&)
{
    auto* Body=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/GreatSword/GreatSword_Woman.GreatSword_Woman"));
    auto* Cloak=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Characters/GreatSword/Clothes/cloak.cloak"));
    if(!Body || !Cloak)return false;
    auto* Validator=NewObject<UProject_JMeshLODAssetValidator>();FDataValidationContext Existing,Missing;
    TestEqual(TEXT("Existing production LOD chain passes engine validation"),Validator->ValidateLoadedAsset(FAssetData(Body),Body,Existing),EDataValidationResult::Valid);
    TestEqual(TEXT("Existing LOD chain emits no missing-LOD warning"),Existing.GetNumWarnings(),0);
    TestEqual(TEXT("Cloth review is diagnostic, not destructive generation or false asset failure"),Validator->ValidateLoadedAsset(FAssetData(Cloak),Cloak,Missing),EDataValidationResult::Valid);
    TestTrue(TEXT("Missing/cloth path reports a useful review warning"),Missing.GetNumWarnings()>0);return true;
}
#endif
