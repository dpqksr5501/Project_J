#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Camera/CameraActor.h"
#include "Engine/TargetPoint.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "Project_JSpellbladeCharacter.h"
#include "Project_JNPCCharacter.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/SkeletalMesh.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "System/Project_JPresentationBudgetSubsystem.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/App.h"

namespace
{
// Own the independent world until the existing test's latent command is retired.
struct FWeaponClassLoadFixture
{
	UWorld* World = nullptr;
	UProject_JVisualAssetSubsystem* Assets = nullptr;
	UProject_JPresentationBudgetSubsystem* Budget = nullptr;
	TStrongObjectPtr<USkeletalMesh> Mesh;
	TArray<TStrongObjectPtr<UProject_JWeaponPresentationComponent>> Capacity;
	TArray<uint64> LoaderCapacity;
	FDelegateHandle SpawnHandle;
	int32 VisualSpawns = 0, Phase = 0;
	double Started = FPlatformTime::Seconds();
	uint64 RejectedBeforeCompletion = 0;
	TWeakObjectPtr<UProject_JWeaponPresentationComponent> Destroyed;
	TStrongObjectPtr<UProject_JWeaponPresentationComponent> Target, Stale, WrongType;
	TStrongObjectPtr<AActor> StaleWeapon;
	FSoftObjectPath Path{TEXT("/Game/Character_BPs/GreatSword/BP_Greatsword_Basic.BP_Greatsword_Basic_C")};
	explicit FWeaponClassLoadFixture(bool bDedicated = false)
	{
		const auto Type = bDedicated ? EWorldType::PIE : EWorldType::Game;
		World = UWorld::CreateWorld(Type, false);
		GEngine->CreateNewWorldContext(Type).SetCurrentWorld(World);
#if WITH_EDITOR
		if (bDedicated) { World->SetPlayInEditorInitialNetMode(NM_DedicatedServer); }
#endif
		Assets = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
		Budget = World->GetSubsystem<UProject_JPresentationBudgetSubsystem>();
		Mesh.Reset(LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
	}
	UProject_JWeaponPresentationComponent* Create(const FSoftObjectPath& ClassPath)
	{
		FActorSpawnParameters Params; Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		auto* NPC = World->SpawnActor<AProject_JNPCCharacter>(AProject_JNPCCharacter::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Params);
		if (!NPC) { return nullptr; }
		NPC->GetMesh()->SetSkeletalMesh(Mesh.Get());
		auto* Component = NewObject<UProject_JWeaponPresentationComponent>(NPC);
		NPC->AddInstanceComponent(Component); Component->RegisterComponent();
		auto* Definition = NewObject<UProject_JWeaponPresentationProfile>(Component);
		Definition->WeaponActorClass = TSoftClassPtr<AActor>(ClassPath);
		Definition->DrawnSocketName = TEXT("pelvis"); Definition->SheathedSocketName = TEXT("root");
		Definition->bPreferVisualFollowerSockets = false;
		Component->OwnerPresentationProfile = Definition;
		return Component;
	}
	~FWeaponClassLoadFixture()
	{
		if (SpawnHandle.IsValid()) { World->RemoveOnActorSpawnedHandler(SpawnHandle); }
		for (const uint64 Token : LoaderCapacity) { Assets->Release(Token); }
		for (const auto& Component : Capacity) { Budget->Cancel(Component.Get()); }
		World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	}
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponPresentationIdentityTest, "ProjectJ.Combat.WeaponPresentationIdentity",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponPresentationIdentityTest::RunTest(const FString&)
{
 if (!FApp::CanEverRender())
 {
  FWeaponClassLoadFixture Scene;
  const bool Cold = FParse::Param(FCommandLine::Get(), TEXT("ProjectJWeaponClassCold"));
  if (Cold && !TestNull(TEXT("Actual headless cold class precondition"), Scene.Path.ResolveObject())) return false;
  Scene.Target.Reset(Scene.Create(Scene.Path));
  if (!TestNotNull(TEXT("Actual headless consumer"), Scene.Target.Get())) return false;
  Scene.Target->BeginPlay(); // Match the real component lifecycle before its later EndPlay.
  TestEqual(TEXT("Headless fixture remains ordinary standalone"), Scene.World->GetNetMode(), NM_Standalone);
  const auto* PoseMesh = Cast<AProject_JNPCCharacter>(Scene.Target->GetOwner())->GetMesh();
  const auto* PoseAsset = PoseMesh->GetSkeletalMeshAsset();
  const uint64 Accepted = Scene.Assets->GetStats().Accepted;
  for (int32 Index = 0; Index < 8; ++Index)
  { Scene.Target->AttachWeaponToDrawnSocket(); Scene.Target->AttachWeaponToSheathedSocket(); Scene.Target->RefreshPresentation(); }
  Scene.Assets->Tick(0); Scene.Budget->Tick(0);
  TestEqual(TEXT("Headless cold refresh accepts no class payload"), Scene.Assets->GetStats().Accepted, Accepted);
  TestEqual(TEXT("Headless consumer owns no class lease"), Scene.Target->WeaponClassLease, uint64(0));
  TestNull(TEXT("Headless cold refresh creates no cosmetic actor"), Scene.Target->GetSpawnedWeapon());
  if (Cold) TestNull(TEXT("No hidden synchronous cold class load"), Scene.Path.ResolveObject());
  Scene.Target->OwnerPresentationProfile->WeaponActorClass = ACameraActor::StaticClass();
  Scene.Target->RefreshPresentation();
  TestNull(TEXT("Resident class cannot bypass headless admission"), Scene.Target->GetSpawnedWeapon());
  TestFalse(TEXT("No futile presentation retry tick"), Scene.Target->IsComponentTickEnabled());
  TestFalse(TEXT("No pending physical creation retry"), Scene.Target->bRetryBudget);
  TestTrue(TEXT("Authoritative source pose asset is preserved"), PoseMesh->GetSkeletalMeshAsset() == PoseAsset);
  Scene.Target->EndPlay(EEndPlayReason::LevelTransition); Scene.Target->RefreshPresentation();
  TestEqual(TEXT("Headless retire/late refresh preserves zero class requests"), Scene.Assets->GetStats().Accepted, Accepted);
  TestNull(TEXT("Headless retire cannot create an actor"), Scene.Target->GetSpawnedWeapon());
  return true;
 }
 if (FParse::Param(FCommandLine::Get(), TEXT("ProjectJWeaponClassCold")))
 {
  // Run in a fresh process after the selected legacy profile was resaved. No unload/GC manufactures coldness.
  auto State = MakeShared<FWeaponClassLoadFixture>();
  if (!TestNull(TEXT("Strict cold class precondition"), State->Path.ResolveObject()) ||
      !TestNotNull(TEXT("Cold fixture pose mesh"), State->Mesh.Get())) { return false; }
  {
   FWeaponClassLoadFixture Server(true);
   TStrongObjectPtr<UProject_JWeaponPresentationComponent> ServerWeapon(Server.Create(Server.Path));
   if (!TestNotNull(TEXT("Dedicated fixture component"), ServerWeapon.Get())) { return false; }
   TestEqual(TEXT("Actual dedicated world role"), Server.World->GetNetMode(), NM_DedicatedServer);
   ServerWeapon->AttachWeaponToDrawnSocket(); ServerWeapon->RefreshPresentation();
   TestEqual(TEXT("Dedicated equipment requests no visual payload"), Server.Assets->GetStats().Accepted, uint64(0));
   TestEqual(TEXT("Dedicated equipment owns no class lease"), ServerWeapon->WeaponClassLease, uint64(0));
   TestNull(TEXT("Dedicated equipment creates no cosmetic actor"), ServerWeapon->GetSpawnedWeapon());
  }
  State->Target.Reset(State->Create(State->Path));
  if (!TestNotNull(TEXT("Cold current consumer"), State->Target.Get())) { return false; }
  const FSoftObjectPath Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
  for (int32 Index = 0; Index < State->Assets->MaxLeasesPerOwner; ++Index)
  {
   const uint64 Token = State->Assets->Request(State->Target.Get(), Cube);
   if (!TestTrue(TEXT("Real loader owner capacity admitted"), Token != 0)) { return false; }
   State->LoaderCapacity.Add(Token);
  }
  const uint64 LoaderRejected = State->Assets->GetStats().Rejected;
  State->Target->AttachWeaponToDrawnSocket();
  TestTrue(TEXT("Full loader admits no current class lease"), State->Target->WeaponClassLease == 0 && State->Target->bRetryBudget);
  TestTrue(TEXT("Actual shared-loader rejection observed"), State->Assets->GetStats().Rejected > LoaderRejected);
  TestNull(TEXT("Cold request cannot synchronously create"), State->Target->GetSpawnedWeapon());
  for (const uint64 Token : State->LoaderCapacity) { State->Assets->Release(Token); }
  State->LoaderCapacity.Reset();
  State->Target->NextBudgetRetry = 0;
  State->Target->TickComponent(0, LEVELTICK_All, &State->Target->PrimaryComponentTick);
  State->Budget->Tick(0);
  TestTrue(TEXT("Existing retry readmits exactly one cold lease"), State->Target->WeaponClassLease != 0 && State->Assets->GetLeaseCount() == 1);
  const uint64 Accepted = State->Assets->GetStats().Accepted;
  State->Target->AttachWeaponToSheathedSocket(); State->Target->AttachWeaponToDrawnSocket(); State->Target->RefreshPresentation();
  TestEqual(TEXT("Pending refreshes share the current class request"), State->Assets->GetStats().Accepted, Accepted);
  TestEqual(TEXT("Latest socket intent is latched while cold"), State->Target->CurrentPresentationSocket, EProject_JWeaponPresentationSocket::Drawn);
  State->Stale.Reset(State->Create(State->Path));
  if (!TestNotNull(TEXT("Stale selection consumer"), State->Stale.Get())) { return false; }
  State->Stale->RefreshPresentation();
  State->Stale->OwnerPresentationProfile->WeaponActorClass = ACameraActor::StaticClass();
  State->Stale->RefreshPresentation(); State->StaleWeapon.Reset(State->Stale->GetSpawnedWeapon());
  TestNotNull(TEXT("New warm selection stays immediate"), State->StaleWeapon.Get());
  TestEqual(TEXT("Changed selection releases its pending class"), State->Stale->WeaponClassLease, uint64(0));
  auto* Retiring = State->Create(State->Path);
  if (!TestNotNull(TEXT("Retiring owner fixture"), Retiring)) { return false; }
  State->Destroyed = Retiring; Retiring->RefreshPresentation(); Retiring->GetOwner()->Destroy();
  CollectGarbage(RF_NoFlags, true);
  State->WrongType.Reset(State->Create(Cube));
  if (!TestNotNull(TEXT("Wrong-type terminal consumer"), State->WrongType.Get())) { return false; }
  State->WrongType->RefreshPresentation();
  State->Capacity.Reserve(State->Budget->MaxPending);
  for (int32 Index = 0; Index < State->Budget->MaxPending; ++Index)
  {
   auto* Holder = NewObject<UProject_JWeaponPresentationComponent>(State->Target->GetOwner());
   State->Capacity.Emplace(Holder);
   if (!TestTrue(TEXT("Real physical creation queue capacity admitted"), State->Budget->Request(Holder, 1))) { return false; }
  }
  TestEqual(TEXT("Physical queue is full before loader completion"), State->Budget->GetStats().Pending, State->Budget->MaxPending);
  State->RejectedBeforeCompletion = State->Budget->GetStats().Rejected;
  State->SpawnHandle = State->World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([StatePtr = &State.Get()](AActor* Actor)
  {
   if (Actor && Actor->GetClass()->GetPathName() == StatePtr->Path.ToString()) { ++StatePtr->VisualSpawns; }
  }));
  FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FFunctionLatentCommand>([this, State]()
  {
   if (FPlatformTime::Seconds() - State->Started > 15) { AddError(TEXT("Cold weapon class loading/admission timed out")); return true; }
   State->Assets->Tick(0);
   if (State->Phase == 0)
   {
    if (!State->Target->bRetryBudget || State->Budget->GetStats().Rejected <= State->RejectedBeforeCompletion) { return false; }
    TestNull(TEXT("Loader completion never spawns through a full physical queue"), State->Target->GetSpawnedWeapon());
    TestTrue(TEXT("Cold completion remains pinned while awaiting physical admission"), State->Target->WeaponClassLease != 0);
    for (const auto& Holder : State->Capacity) { State->Budget->Cancel(Holder.Get()); }
    State->Target->NextBudgetRetry = 0;
    State->Target->TickComponent(0, LEVELTICK_All, &State->Target->PrimaryComponentTick);
    TestNull(TEXT("Readmission alone cannot synchronously create"), State->Target->GetSpawnedWeapon());
    State->Phase = 1;
   }
   State->Budget->Tick(0);
   TestTrue(TEXT("Cold physical creation uses the existing per-tick quota"), State->Budget->GetStats().LastApplications <= State->Budget->MaxApplications);
   if (!State->Target->GetSpawnedWeapon() || State->WrongType->WeaponClassLease != 0) { return false; }
   TestEqual(TEXT("Only the current cold selection is created"), State->VisualSpawns, 1);
   TestEqual(TEXT("Cold visual uses latest latched draw socket"), State->Target->GetSpawnedWeapon()->GetRootComponent()->GetAttachSocketName(), FName(TEXT("pelvis")));
   TestEqual(TEXT("Actor ownership replaces the pending class pin after spawn"), State->Target->WeaponClassLease, uint64(0));
   TestTrue(TEXT("Superseded cold completion cannot replace the current warm actor"), State->Stale->GetSpawnedWeapon() == State->StaleWeapon.Get());
   TestTrue(TEXT("Destroyed owner cannot be resurrected by completion"), !State->Destroyed.IsValid() || !State->Destroyed->GetSpawnedWeapon());
   TestFalse(TEXT("Delivered wrong type is terminal without retry tick"), State->WrongType->bRetryBudget);
   TestNull(TEXT("Wrong-type payload never creates an actor"), State->WrongType->GetSpawnedWeapon());
   TestEqual(TEXT("No obsolete class leases remain"), State->Assets->GetLeaseCount(), 0);
   auto* Ready = State->Target->GetSpawnedWeapon(); State->Target->RefreshPresentation();
   TestTrue(TEXT("Warm post-load refresh preserves actor identity"), State->Target->GetSpawnedWeapon() == Ready);
   const uint64 BeforeTearDown = State->Assets->GetStats().Accepted;
   State->World->bIsTearingDown = true; State->Target->RefreshPresentation();
   TestNull(TEXT("World teardown retires the visual"), State->Target->GetSpawnedWeapon());
   TestEqual(TEXT("World teardown admits no class request"), State->Assets->GetStats().Accepted, BeforeTearDown);
   return true;
  }));
  return true;
 }
 TStrongObjectPtr<USkeletalMesh> FixtureMesh(LoadObject<USkeletalMesh>(nullptr,
  TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple.SKM_Quinn_Simple")));
 if (!TestNotNull(TEXT("Default fixture has an authored root socket"), FixtureMesh.Get())) { return false; }
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 FActorSpawnParameters Spawn; Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
 auto* Pawn = World->SpawnActor<AProject_JSpellbladeCharacter>(AProject_JSpellbladeCharacter::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Spawn);
 if (!TestNotNull(TEXT("Concrete default identity pawn"), Pawn))
 { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
 Pawn->GetMesh()->SetSkeletalMesh(FixtureMesh.Get());
 // Player equipment normally lives on PlayerState. This isolated fixture has no
 // controller/PlayerState; supply an explicit authority-side manager for it.
 auto* Manager = NewObject<UProject_JEquipmentManagerComponent>(Pawn);
 Pawn->AddInstanceComponent(Manager); Manager->RegisterComponent();
 auto* Runtime = Pawn->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();
 auto* Presentation = Pawn->FindComponentByClass<UProject_JWeaponPresentationComponent>();
 if (!TestNotNull(TEXT("Existing equipment runtime"), Runtime) || !TestNotNull(TEXT("Existing weapon presentation"), Presentation))
 { World->DestroyWorld(false); GEngine->DestroyWorldContext(World); return false; }
 Runtime->BindToEquipmentManager(Manager);
 auto* Style = NewObject<UProject_JCombatStyleDefinition>(World);
 auto* Profile = NewObject<UProject_JWeaponPresentationProfile>(World);
 Profile->WeaponActorClass = ACameraActor::StaticClass();
 Profile->DrawnSocketName = Profile->SheathedSocketName = TEXT("root"); Profile->bPreferVisualFollowerSockets = false;
 auto* Item = NewObject<UProject_JEquipmentItemDefinition>(World);
 Item->CombatStyleDefinition = Style; Item->WeaponPresentationProfile = Profile;
 int32 Spawned = 0;
 const auto SpawnHandle = World->AddOnActorSpawnedHandler(FOnActorSpawned::FDelegate::CreateLambda([&](AActor*) { ++Spawned; }));

 Manager->EquipItem(Item);
 auto* First = Presentation->GetSpawnedWeapon();
 TestNotNull(TEXT("Equipment creates a weapon"), First);
 TestEqual(TEXT("Exactly one actor created for equip"), Spawned, 1);
 Presentation->bIndependentMotionActive = true; // observe preservation of notify-owned state
 Presentation->RefreshPresentation();
 Pawn->SetCurrentEquipmentConfiguration(Style, Profile);
 TestTrue(TEXT("Duplicate refresh keeps actor identity"), Presentation->GetSpawnedWeapon() == First);
 TestTrue(TEXT("Duplicate refresh keeps independent motion"), Presentation->IsIndependentMotionActive());

 // Invoke replication reactions in either order, without claiming this is a net-driver test.
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentCombatStyle")), nullptr);
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentWeaponPresentationProfile")), nullptr);
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentWeaponPresentationProfile")), nullptr);
 Pawn->ProcessEvent(Pawn->FindFunctionChecked(TEXT("OnRep_CurrentCombatStyle")), nullptr);
 TestTrue(TEXT("Repeated replication notifications keep weapon identity"), Presentation->GetSpawnedWeapon() == First);
 TestTrue(TEXT("Repeated replication notifications keep independent motion"), Presentation->IsIndependentMotionActive());
 TestEqual(TEXT("Repeated callbacks do not spawn"), Spawned, 1);

 Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
 TestEqual(TEXT("Unequip creates zero intermediate weapons"), Spawned, 1);
 TestNull(TEXT("Unequip removes visual"), Presentation->GetSpawnedWeapon());
 TestFalse(TEXT("Unequip ends independent motion"), Presentation->IsIndependentMotionActive());
 Presentation->RefreshPresentation(); Presentation->ExitCombatPresentation();
 TestEqual(TEXT("Empty-slot callbacks do not spawn"), Spawned, 1);
 Manager->EquipItem(Item);
 TestEqual(TEXT("Reequip creates exactly one new actor"), Spawned, 2);
 auto* Second = Presentation->GetSpawnedWeapon();
 auto* OtherStyle = NewObject<UProject_JCombatStyleDefinition>(World);
 Pawn->SetCurrentCombatStyle(OtherStyle);
 TestTrue(TEXT("Style-only change keeps the same weapon"), Presentation->GetSpawnedWeapon() == Second);

 auto* OtherProfile = NewObject<UProject_JWeaponPresentationProfile>(World);
 OtherProfile->WeaponActorClass = ACameraActor::StaticClass();
 OtherProfile->DrawnSocketName = OtherProfile->SheathedSocketName = TEXT("root"); OtherProfile->bPreferVisualFollowerSockets = false;
 Pawn->SetCurrentWeaponPresentationProfile(OtherProfile);
 TestEqual(TEXT("Changed profile rebuilds once"), Spawned, 3);
 TestTrue(TEXT("Changed profile removes previous actor"), !IsValid(Second) || Second->IsActorBeingDestroyed());
 OtherProfile->WeaponActorClass = ATargetPoint::StaticClass();
 Presentation->RefreshPresentation();
 TestEqual(TEXT("Same asset with changed actor class rebuilds"), Spawned, 4);
 if (auto* Current = Presentation->GetSpawnedWeapon()) Current->Destroy();
 Presentation->RefreshPresentation();
 TestEqual(TEXT("External destruction can recover"), Spawned, 5);
 World->bIsTearingDown = true;
 Presentation->RefreshPresentation();
 TestNull(TEXT("Teardown only removes visual"), Presentation->GetSpawnedWeapon());
 TestEqual(TEXT("Teardown cannot spawn"), Spawned, 5);
 World->RemoveOnActorSpawnedHandler(SpawnHandle);
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
#endif
