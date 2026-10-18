#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "UObject/Package.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAudit3ExternalPackageTest,
 "ProjectJ.SourceAudit3.ExternalActorPackageOwnership", EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FProjectJAudit3ExternalPackageTest::RunTest(const FString&)
{
 auto* World=UWorld::CreateWorld(EWorldType::Game,false);
 auto* Actor=World->SpawnActor<AActor>();
 auto* External=CreatePackage(*FString::Printf(TEXT("/Game/__ExternalActors__/__SourceAuditTests__/%s"),*FGuid::NewGuid().ToString(EGuidFormats::Digits)));
 Actor->SetExternalPackage(External);
 TestTrue(TEXT("Owning save package is external, not the shared world outer"),Actor->GetOutermost()==External);
 TestTrue(TEXT("Fixture distinguishes external ownership from outer traversal"),Actor->GetOuter()->GetOutermost()!=External);
 Actor->SetExternalPackage(nullptr);World->DestroyWorld(false);
 return true;
}
#endif
