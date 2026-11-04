#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Equipment/Project_JWeaponPresentationActor.h"
#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Animation/Skeleton.h"

namespace Project_J::WeaponPresentation
{
 bool ResolveRigidGripInRoot(const USceneComponent*,FName,const USceneComponent*,FTransform&,FName&);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponSkeletalVisualTest,"ProjectJ.Combat.WeaponSkeletalVisualLifecycle",
 EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponSkeletalVisualTest::RunTest(const FString&)
{
 auto* Class=LoadClass<AProject_JWeaponPresentationActor>(nullptr,TEXT("/Game/Character_BPs/GreatSword/BP_Greatsword_Basic.BP_Greatsword_Basic_C"));
 auto* Drawn=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade.SK_Spellblade_longblade"));
 auto* Sheathed=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade_in.SK_Spellblade_longblade_in"));
 if (!TestNotNull(TEXT("Preserved Basic class"),Class) || !TestNotNull(TEXT("Drawn rig"),Drawn) || !TestNotNull(TEXT("Sheathed rig"),Sheathed)) return false;
 if (!TestNotNull(TEXT("Persistent drawn Skeleton"),Drawn->GetSkeleton()) || !TestNotNull(TEXT("Persistent sheath Skeleton"),Sheathed->GetSkeleton())) return false;
 TestEqual(TEXT("Variants share the saved independent weapon Skeleton"),Drawn->GetSkeleton(),Sheathed->GetSkeleton());
 TestEqual(TEXT("Shared Skeleton has the same complete eight-bone rig"),Drawn->GetSkeleton()->GetReferenceSkeleton().GetNum(),8);
 UWorld* World=UWorld::CreateWorld(EWorldType::Game,false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Actor=World->SpawnActor<AProject_JWeaponPresentationActor>(Class);
 if (TestNotNull(TEXT("Spawn"),Actor))
 {
  auto* Legacy=Actor->GetWeaponMesh();
  TestTrue(TEXT("Basic still uses original serialized static subobject"),Legacy && Legacy->GetFName()==TEXT("WeaponMesh") && Legacy->GetStaticMesh());
  FProject_JWeaponSkeletalPart Part;
  Part.PartId=TEXT("Blade");Part.DrawnMesh=Drawn;Part.SheathedMesh=Sheathed;
  Actor->SkeletalParts.Add(Part);
  Actor->RerunConstructionScripts();
  TestEqual(TEXT("Legacy subobject identity survives reconstruction"),Actor->GetWeaponMesh(),Legacy);
  for (int32 Pass=0;Pass<6;++Pass)
  {
   const bool bDrawn=Pass%2==0;
   Actor->SetPresentationDrawnState(bDrawn);
   const auto Visuals=Actor->GetVisualMeshes();
   TestEqual(TEXT("Exactly one configured skeletal part plus the preserved static mesh"),Visuals.Num(),2);
   if (Visuals.Num()==2)
   {
    auto* Mesh=Cast<USkeletalMeshComponent>(Visuals[1]);
    TestNotNull(TEXT("Skinned visual retained"),Mesh);
    if (Mesh)
    {
     TestEqual(TEXT("Draw/sheath selects data, no duplicate actor"),Mesh->GetSkeletalMeshAsset(),bDrawn ? Drawn : Sheathed);
     TestFalse(TEXT("Reference pose part has no recurring tick"),Mesh->IsComponentTickEnabled());
     TestNull(TEXT("No character ABP on each weapon"),Mesh->GetAnimClass());
     TestEqual(TEXT("Cosmetic collision disabled"),Mesh->GetCollisionEnabled(),ECollisionEnabled::NoCollision);
    }
   }
  }
  Actor->RerunConstructionScripts();
  TInlineComponentArray<USkeletalMeshComponent*> Skinned(Actor);
  TestEqual(TEXT("Reconstruction does not leak registered parts"),Skinned.Num(),1);
  TestEqual(TEXT("Independent rig contains eight weapon bones, not the character rig"),Drawn->GetRefSkeleton().GetNum(),8);
  if (Skinned.Num()==1)
  {
   auto* Copy=DuplicateObject<USkeletalMesh>(Drawn,Actor);
   auto* Socket=NewObject<USkeletalMeshSocket>(Copy);
   Socket->SocketName=TEXT("ReferenceMountTest");Socket->BoneName=Copy->GetRefSkeleton().GetBoneName(0);
   Socket->RelativeLocation=FVector(2,3,4);
   Copy->GetMeshOnlySocketList().Add(Socket);
   Skinned[0]->SetSkeletalMesh(Copy);
   FTransform Before,After;FName Reason;
   TestTrue(TEXT("Root reference socket is an eligible fixed mount"),Project_J::WeaponPresentation::ResolveRigidGripInRoot(
    Skinned[0],Socket->SocketName,Actor->GetRootComponent(),Before,Reason));
   Actor->SetActorTransform(FTransform(FRotator(11,27,53),FVector(500,-200,900),FVector(1.5)));
   TestTrue(TEXT("Changing previous world mount leaves reference calibration unchanged"),Project_J::WeaponPresentation::ResolveRigidGripInRoot(
    Skinned[0],Socket->SocketName,Actor->GetRootComponent(),After,Reason) && Before.Equals(After,.001f));
   Socket->BoneName=Copy->GetRefSkeleton().GetBoneName(1);
   TestFalse(TEXT("Moving auxiliary bones cannot define the fixed mount"),Project_J::WeaponPresentation::ResolveRigidGripInRoot(
    Skinned[0],Socket->SocketName,Actor->GetRootComponent(),After,Reason));
  }
  Actor->Destroy();
 }
 auto* ProductionClass=LoadClass<AProject_JWeaponPresentationActor>(nullptr,TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade_Weapon.BP_Spellblade_Weapon_C"));
 if (TestNotNull(TEXT("Saved production weapon BP"),ProductionClass))
 {
  auto* Production=World->SpawnActor<AProject_JWeaponPresentationActor>(ProductionClass);
  if (TestNotNull(TEXT("Production spawn"),Production))
  {
   TestNull(TEXT("Skeletal production does not duplicate the legacy static visual"),Production->GetWeaponMesh()->GetStaticMesh());
   for (bool bDrawn:{false,true,false,true})
   {
    Production->SetPresentationDrawnState(bDrawn);
    const auto Parts=Production->GetVisualMeshes();
    TestEqual(TEXT("Saved BP constructs blade and ornament exactly once"),Parts.Num(),2);
    for (auto* Part:Parts)
    {
     auto* Mesh=Cast<USkeletalMeshComponent>(Part);
     TestTrue(TEXT("Each production part has a persistent shared Skeleton"),Mesh && Mesh->GetSkeletalMeshAsset() && Mesh->GetSkeletalMeshAsset()->GetSkeleton()==Drawn->GetSkeleton());
     if (Mesh) TestEqual(TEXT("Production variants follow the same draw/sheath selection"),Mesh->GetSkeletalMeshAsset()->GetName().EndsWith(TEXT("_in")),!bDrawn);
    }
   }
   Production->Destroy();
  }
 }
 World->DestroyWorld(false);GEngine->DestroyWorldContext(World);
 return !HasAnyErrors();
}
#endif
