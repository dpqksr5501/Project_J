#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Project_JNPCCharacter.h"
#include "Project_JSpellbladeCharacter.h"
#include "Combat/Project_JServerSideRewindComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJProductionRewindTest, "ProjectJ.Combat.ProductionRewindLifecycle",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJProductionRewindTest::RunTest(const FString&)
{
 auto* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 ON_SCOPE_EXIT {
  if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::LevelTransition);
  World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 };
 FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* NPC = World->SpawnActor<AProject_JNPCCharacter>(Params);
 auto* Player = World->SpawnActor<AProject_JSpellbladeCharacter>(Params);
 for (auto* Actor : TArray<AActor*>{ NPC, Player })
 {
  TArray<UProject_JServerSideRewindComponent*> Components; Actor->GetComponents(Components);
  if (!TestEqual(TEXT("Each production native character has exactly one history owner"), Components.Num(), 1)) return false;
 }
 auto* Rewind = NPC->FindComponentByClass<UProject_JServerSideRewindComponent>();
 World->InitializeActorsForPlay(FURL());
 World->GetWorldSettings()->NotifyBeginPlay(); World->GetWorldSettings()->NotifyMatchStarted();
 TestTrue(TEXT("Authority history capture is enabled without fixture attachment"), Rewind->IsComponentTickEnabled());
 for (int32 Frame = 0; Frame < 12; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f/60.f); }
 double Oldest = 0, Newest = 0;
 if (!TestTrue(TEXT("Standard target records real post-physics samples"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest))) return false;
 const FVector Position = NPC->GetActorLocation();
 NPC->ResetCombatSpatialHistory();
 TestFalse(TEXT("Teleport/handover boundary clears production history"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));
 TestFalse(TEXT("Old timestamp cannot verify after spatial reset"), Rewind->ServerVerifyHit(Newest, Position - FVector(100,0,0), Position + FVector(100,0,0)));
 for (int32 Frame = 0; Frame < 6; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f/60.f); }
 TestTrue(TEXT("Recording resumes with a new spatial history"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));
 Rewind->UnregisterComponent();
 TestFalse(TEXT("Unregister retires history immediately"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));
 TestFalse(TEXT("Retired component cannot verify"), Rewind->ServerVerifyHit(Newest, Position, Position));
 Rewind->RegisterComponent();
 for (int32 Frame = 0; Frame < 6; ++Frame) { ++GFrameCounter; World->Tick(LEVELTICK_All, 1.f/60.f); }
 NPC->Destroy();
 TestFalse(TEXT("EndPlay retires history"), Rewind->TryGetHistoryTimeBounds(Oldest, Newest));
 return true;
}
#endif
