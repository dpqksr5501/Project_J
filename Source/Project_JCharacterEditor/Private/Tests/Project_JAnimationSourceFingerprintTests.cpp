#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationSourceFingerprint,"ProjectJ.PlayerLocomotion.AnimationSourceFingerprint",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAnimationSourceFingerprint::RunTest(const FString&)
{
 auto* Original=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Characters/Mannequins/Animations/Locomotion/Idle/M_Relaxed_Stand_Idle_Loop.M_Relaxed_Stand_Idle_Loop"));
 if(!TestNotNull(TEXT("Real Manny animation"),Original))return false;
 const FString OriginalBefore=UProject_JPlayerLocomotionLibrary::GetAnimationMotionFingerprint(Original);
 TArray<FName> Tracks;Original->GetDataModel()->GetBoneTrackNames(Tracks);
 if(!TestFalse(TEXT("Production animation contains raw tracks"),Tracks.IsEmpty()))return false;
 TArray<FTransform> Transforms;Original->GetDataModel()->GetBoneTrackTransforms(Tracks[0],Transforms);
 TArray<FVector> Positions,Scales;TArray<FQuat> Rotations;
 for(const auto& Transform:Transforms){Positions.Add(Transform.GetTranslation());Rotations.Add(Transform.GetRotation());Scales.Add(Transform.GetScale3D());}
 // Build the real track in a fresh transient model. Duplicating a loaded imported
 // sequence also copies linker/compression dependencies and is unrelated to the
 // identity contract being tested here.
 auto* Sample=NewObject<UAnimSequence>(GetTransientPackage(),NAME_None,RF_Transient);
 Sample->SetSkeleton(Original->GetSkeleton());
 auto& Controller=Sample->GetController();Controller.InitializeModel();
 Controller.OpenBracket(NSLOCTEXT("ProjectJTests","SourceFingerprint","Populate import identity fixture"),false);
 Controller.SetFrameRate(Original->GetDataModel()->GetFrameRate(),false);
 Controller.SetNumberOfFrames(FFrameNumber(Original->GetDataModel()->GetNumberOfFrames()),false);
 TestTrue(TEXT("An actual source track is populated"),Controller.AddBoneCurve(Tracks[0],false));
 TestTrue(TEXT("The source keys populate"),Controller.SetBoneTrackKeys(Tracks[0],Positions,Rotations,Scales,false));
 Controller.NotifyPopulated();Controller.CloseBracket(false);
 const FString Before=UProject_JPlayerLocomotionLibrary::GetAnimationMotionFingerprint(Sample);
 const FString NotifyBefore=UProject_JPlayerLocomotionLibrary::GetAnimationNotifyFingerprint(Sample);
 TestEqual(TEXT("BLAKE3 identifier length"),Before.Len(),64);
 auto* Destination=CreatePackage(*FString::Printf(TEXT("/Temp/ProjectJImportFingerprint_%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
 Destination->SetFlags(RF_Transient);
 TestTrue(TEXT("Fixture moves and renames without content writes"),Sample->Rename(TEXT("RenamedImport"),Destination,REN_DontCreateRedirectors|REN_NonTransactional));
 TestEqual(TEXT("Names and packages do not bypass duplicate detection"),UProject_JPlayerLocomotionLibrary::GetAnimationMotionFingerprint(Sample),Before);
 TestEqual(TEXT("Notify identity survives an owner move"),UProject_JPlayerLocomotionLibrary::GetAnimationNotifyFingerprint(Sample),NotifyBefore);
 FAnimNotifyEvent Added;Added.NotifyName=TEXT("ImportContractChanged");Sample->Notifies.Add(Added);
 TestNotEqual(TEXT("Changed notify semantics require review"),UProject_JPlayerLocomotionLibrary::GetAnimationNotifyFingerprint(Sample),NotifyBefore);
 Sample->Notifies.Reset();
 Positions[0].X+=0.001;
 TestTrue(TEXT("An actual raw-track key changes"),Controller.SetBoneTrackKeys(Tracks[0],Positions,Rotations,Scales,false));
 TestNotEqual(TEXT("Revised motion must not silently reuse an import"),UProject_JPlayerLocomotionLibrary::GetAnimationMotionFingerprint(Sample),Before);
 TestEqual(TEXT("Production source data was not mutated"),UProject_JPlayerLocomotionLibrary::GetAnimationMotionFingerprint(Original),OriginalBefore);
 return true;
}
#endif
