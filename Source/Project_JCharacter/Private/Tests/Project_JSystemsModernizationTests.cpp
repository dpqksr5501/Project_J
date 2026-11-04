#include "Misc/AutomationTest.h"
#include "GameFramework/Character.h"
#if WITH_DEV_AUTOMATION_TESTS

#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "System/Project_JVisualAssetSubsystem.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JSpellbladeCharacter.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "TimerManager.h"
#include "Misc/App.h"
#include "Components/SkeletalMeshComponent.h"
#include "Tests/Project_JPlayerMaturityFixtures.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/GarbageCollection.h"

namespace
{
class FEquipmentRegistrationCommand121 final : public IAutomationLatentCommand
{
public:
 explicit FEquipmentRegistrationCommand121(TFunction<bool()> InBody):Body(MoveTemp(InBody)){}
 virtual bool Update() override { return Body(); }
private:
 TFunction<bool()> Body;
};
struct FEquipmentRegistration121
{
 TStrongObjectPtr<UWorld> World;
 TStrongObjectPtr<AProject_JPlayerMaturityASCFixture> Owner;
 TStrongObjectPtr<UProject_JEquipmentRuntimeComponent> Runtime;
 TStrongObjectPtr<UProject_JEquipmentItemDefinition> Item,Other;
 TStrongObjectPtr<USkeletalMesh> SetupPin;
 TWeakObjectPtr<USkeletalMesh> Payload;
 TWeakObjectPtr<USkeletalMeshComponent> Candidate;
 double Started=FPlatformTime::Seconds();
 uint64 OldRevision=0,OldToken=0,NewRevision=0,NewToken=0;
 int32 Case=0,Stage=0,Calls=0;
 ~FEquipmentRegistration121()
 {
  if(Owner.IsValid()) Owner->OnEquipmentAnimationInitialize=nullptr;
  if(Runtime.IsValid() && !Runtime->IsBeingDestroyed()) Runtime->DestroyComponent();
  if(World.IsValid())
  {
   World->DestroyWorld(false);
   if(GEngine) GEngine->DestroyWorldContext(World.Get());
  }
 }
};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatInputBoundaryTest,
	"ProjectJ.Modernization.CombatInputBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJCombatInputBoundaryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AProject_JPlayerCharacter* Player = World->SpawnActor<AProject_JSpellbladeCharacter>();
	if (!TestNotNull(TEXT("Native player requires no project assets"), Player))
	{
		World->DestroyWorld(false);
		return false;
	}
	UProject_JAbilitySystemComponent* ASC = NewObject<UProject_JAbilitySystemComponent>(Player);
	// Provide the normal ASC seam without loading a PlayerState Blueprint or a map.
	FObjectPropertyBase* ASCProperty = FindFProperty<FObjectPropertyBase>(
		AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"));
	if (!TestNotNull(TEXT("ASC seam exists"), ASCProperty))
	{
		World->DestroyWorld(false);
		return false;
	}
	ASCProperty->SetObjectPropertyValue_InContainer(Player, ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Player, Player);
	UProject_JSkillInputExecutionComponent* Input = Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>();
	Input->Initialize(Player);
	const FProject_JGameplayTags& Tags = FProject_JGameplayTags::Get();
	int32 GameplayEvents = 0;
	int32 ValidInputs = 0;
	ASC->GenericGameplayEventCallbacks.FindOrAdd(Tags.Event_Combat_ComboWindow).AddLambda(
		[&GameplayEvents](const FGameplayEventData*) { ++GameplayEvents; });
	ASC->GenericGameplayEventCallbacks.FindOrAdd(Tags.InputTag_Weapon_LMB).AddLambda(
		[&ValidInputs](const FGameplayEventData*) { ++ValidInputs; });
	Input->ReceiveAuthorityCombatInput(Tags.Event_Combat_ComboWindow, 0.0f, 1);
	TestEqual(TEXT("Client cannot inject a montage gameplay event through input RPC"), GameplayEvents, 0);
	Input->ReceiveAuthorityCombatInput(Tags.InputTag_Weapon_LMB, 0.0f, 2);
	TestEqual(TEXT("Valid LMB input reaches the server event listener"), ValidInputs, 1);
	Input->ReceiveAuthorityCombatInput(Tags.InputTag_Weapon_LMB, 0.0f, 2);
	TestEqual(TEXT("Duplicate sequence does not replay input"), ValidInputs, 1);
	int32 CommandEvents = 0;
	const FGameplayTag CommandTag = FGameplayTag::RequestGameplayTag(TEXT("Command.Greatsword.Special1"));
	ASC->GenericGameplayEventCallbacks.FindOrAdd(CommandTag).AddLambda(
		[&CommandEvents](const FGameplayEventData*) { ++CommandEvents; });
	Input->ReceiveAuthorityCombatInput(CommandTag, 0.0f, 3);
	TestEqual(TEXT("Client cannot skip command matching with a resolved alias"), CommandEvents, 0);
	Input->ReceiveAuthorityCombatInput(FGameplayTag(), 0.0f, 4);
	Input->ReceiveAuthorityCombatInput(Tags.Event_Combat_ComboWindow, 0.0f, 100);
	Input->ReceiveAuthorityCombatInput(Tags.InputTag_Weapon_LMB, 0.0f, 3);
	TestEqual(TEXT("Rejected tags do not advance the accepted sequence"), ValidInputs, 2);
	for (const uint32 Sequence : {0x7fffffffu, 0xfffffffeu, 0xffffffffu, 0u, 1u})
	{ Input->ReceiveAuthorityCombatInput(Tags.InputTag_Weapon_LMB, 0.0f, Sequence); }
	TestEqual(TEXT("Accepted serials survive uint32 wrap"), ValidInputs, 7);
	Input->ReceiveAuthorityCombatInput(Tags.InputTag_Weapon_LMB, 0.0f, 1u);
	Input->ReceiveAuthorityCombatInput(Tags.InputTag_Weapon_LMB, 0.0f, 0u);
	TestEqual(TEXT("Duplicate and stale serials remain rejected after wrap"), ValidInputs, 7);
	const auto RetireHandle = ASC->GenericGameplayEventCallbacks.FindChecked(Tags.InputTag_Weapon_LMB).AddLambda(
		[Input](const FGameplayEventData*) { Input->Initialize(nullptr); });
	Input->HandleInputTagPressed(Tags.InputTag_Weapon_LMB);
	TestEqual(TEXT("Synchronous receiver retirement returns safely after the original event"), ValidInputs, 8);
	Input->HandleInputTagPressed(Tags.InputTag_Weapon_LMB);
	TestEqual(TEXT("Retired receiver cannot dispatch again"), ValidInputs, 8);
	ASC->GenericGameplayEventCallbacks.FindChecked(Tags.InputTag_Weapon_LMB).Remove(RetireHandle);
	ASC->GenericGameplayEventCallbacks.Empty();
	World->DestroyWorld(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentLoadLifecycleTest,
	"ProjectJ.Modernization.EquipmentLoadLifecycle",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJEquipmentLoadLifecycleTest::RunTest(const FString& Parameters)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 ACharacter* Owner = World->SpawnActor<ACharacter>();
 auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Owner); Runtime->RegisterComponent();
 auto* Service = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
 auto* Item = NewObject<UProject_JEquipmentItemDefinition>(Runtime);
 Item->EquipmentSlot = EProject_JEquipmentSlot::Head;
 const FSoftObjectPath MeshPath(TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
 Item->EquipmentMesh = TSoftObjectPtr<USkeletalMesh>(MeshPath);
 if (!FApp::CanEverRender())
 {
  const uint64 Accepted = Service->GetStats().Accepted;
  Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
  const uint64 FirstRevision = Runtime->RuntimeItems[Item->EquipmentSlot].VisualRevision;
  TestTrue(TEXT("Headless equip retains actual runtime state/revision"), Runtime->RuntimeItems.Contains(Item->EquipmentSlot) && FirstRevision > 0);
  TestEqual(TEXT("Headless authority retains public definition transport"), Runtime->GetPublicEquipment().Items.Num(), 1);
  Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item); // Late completion cannot bypass admission.
  for (int32 Index = 0; Index < 8; ++Index) Runtime->RetryEquipmentVisuals();
  TestEqual(TEXT("Headless equip/retry/completion requests no payload"), Service->GetStats().Accepted, Accepted);
  TestEqual(TEXT("No headless mesh lease"), Runtime->RuntimeItems[Item->EquipmentSlot].VisualLoadToken, uint64(0));
  TestEqual(TEXT("Ineligible visuals consume no retry attempts"), Runtime->RuntimeItems[Item->EquipmentSlot].VisualAttempts, uint32(0));
  TestNull(TEXT("Headless completion creates no mesh"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh);
  TestFalse(TEXT("Headless equip arms no retry timer"), World->GetTimerManager().TimerExists(Runtime->VisualRetryTimer));
  auto* Replacement = NewObject<UProject_JEquipmentItemDefinition>(Runtime);
  Replacement->EquipmentSlot = Item->EquipmentSlot; Replacement->EquipmentMesh = Item->EquipmentMesh;
  Runtime->OnEquipmentEquipped(Replacement->EquipmentSlot, Replacement);
  Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
  TestTrue(TEXT("Headless replacement keeps the latest item and a fresh generation"), Runtime->RuntimeItems[Item->EquipmentSlot].ItemDef == Replacement && Runtime->RuntimeItems[Item->EquipmentSlot].VisualRevision != FirstRevision);
  Runtime->BeginPlay(); Runtime->EndPlay(EEndPlayReason::LevelTransition); Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
  TestTrue(TEXT("Headless end/late equip retires all runtime state"), Runtime->RuntimeItems.IsEmpty());
  TestEqual(TEXT("Headless end retains no lease"), Service->GetLeaseCount(), 0);
  TestFalse(TEXT("Headless end retains no timer"), World->GetTimerManager().TimerExists(Runtime->VisualRetryTimer));
  World->DestroyWorld(false);
  return true;
 }
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 TestEqual(TEXT("Visual request queued, no inline mesh creation"), Service->GetLeaseCount(), 1);
 TestNull(TEXT("Deferred visual application"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh);
 const uint64 FirstRevision = Runtime->RuntimeItems[Item->EquipmentSlot].VisualRevision;
 Runtime->OnEquipmentUnequipped(Item->EquipmentSlot, Item);
 TestEqual(TEXT("Unequip releases lease"), Service->GetLeaseCount(), 0);
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 TestTrue(TEXT("A-B-A uses a fresh generation"), Runtime->RuntimeItems[Item->EquipmentSlot].VisualRevision != FirstRevision);
 Runtime->BindToEquipmentManager(nullptr);
 TestEqual(TEXT("Unbind releases leases even without a manager"), Service->GetLeaseCount(), 0);
 auto* Mesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath.ToString()); // fixture only
 if (!TestNotNull(TEXT("Engine fixture"), Mesh)) { World->DestroyWorld(false); return false; }
 // Modular equipment follows a compatible body; a bare Character has no pose source.
 AddExpectedError(TEXT("Equipment visual has no compatible pose source or attachment socket"), EAutomationExpectedErrorFlags::Contains, 1);
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
 TestNull(TEXT("Missing pose source is still rejected"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh);
 Runtime->OnEquipmentUnequipped(Item->EquipmentSlot, Item);
 Owner->GetMesh()->SetSkeletalMesh(Mesh);
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
 auto* Visual = Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh;
 TestNotNull(TEXT("Completed asset creates visual"), Visual);
 if (Visual)
 {
  TestTrue(TEXT("First registered frame already has the authored pose source"), Visual->IsRegistered() && Visual->LeaderPoseComponent.Get() == Owner->GetMesh());
  TestTrue(TEXT("Runtime and authored parts share one lifecycle policy"), Visual->bFollowAttachParentPose);
  TestFalse(TEXT("Plain equipment has no independent pose tick"), Visual->IsComponentTickEnabled());
  TestNull(TEXT("Plain equipment has no duplicate graph"), Visual->GetAnimInstance());
 }
 Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
 TestEqual(TEXT("No duplicated visual"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh, Visual);
 auto* Replacement = NewObject<UProject_JEquipmentItemDefinition>(Runtime);
 Replacement->EquipmentSlot = Item->EquipmentSlot; Replacement->EquipmentMesh = Item->EquipmentMesh;
 Runtime->OnEquipmentEquipped(Replacement->EquipmentSlot, Replacement);
 TestTrue(TEXT("Replacing the slot destroys its previous mesh"), !IsValid(Visual) || Visual->IsBeingDestroyed());
 Runtime->OnEquipmentMeshLoaded(Replacement->EquipmentSlot, Item);
 TestNull(TEXT("Stale previous item completion cannot recreate the old visual"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh);
 Runtime->OnEquipmentMeshLoaded(Replacement->EquipmentSlot, Replacement);
 TestNotNull(TEXT("Replacement creates exactly its own visual"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh);
 Runtime->BeginPlay(); Runtime->EndPlay(EEndPlayReason::LevelTransition);
 TestEqual(TEXT("World transition releases lease"), Service->GetLeaseCount(), 0);
 TestTrue(TEXT("World transition clears runtime"), Runtime->RuntimeItems.IsEmpty());
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 TestTrue(TEXT("Ended component rejects equip"), Runtime->RuntimeItems.IsEmpty());
 Runtime->DestroyComponent();
 auto* EarlyRuntime = NewObject<UProject_JEquipmentRuntimeComponent>(Owner); EarlyRuntime->RegisterComponent();
 EarlyRuntime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 EarlyRuntime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
 auto* EarlyVisual = EarlyRuntime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh;
 TestNotNull(TEXT("Visual before BeginPlay"), EarlyVisual);
 EarlyRuntime->DestroyComponent();
 TestTrue(TEXT("Early destruction clears runtime"), EarlyRuntime->RuntimeItems.IsEmpty());
 TestTrue(TEXT("Early destruction destroys the separately owned visual"), !IsValid(EarlyVisual) || EarlyVisual->IsBeingDestroyed());
 TestEqual(TEXT("Early destruction releases lease"), Service->GetLeaseCount(), 0);
 World->DestroyWorld(false);
 // Actual warm delivery and real CopyPose post-process registration callbacks.
 // The existing synchronous/LeaderPose/headless lifecycle assertions above remain intact.
 auto Registration=MakeShared<FEquipmentRegistration121>();
 Registration->World.Reset(UWorld::CreateWorld(EWorldType::Game,false));
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(Registration->World.Get());
 Registration->Owner.Reset(Registration->World->SpawnActor<AProject_JPlayerMaturityASCFixture>());
 if(!TestTrue(TEXT("Registration callback fixture has a real owner"),Registration->Owner.IsValid())) return false;
 Registration->Owner->GetMesh()->SetSkeletalMesh(Mesh);
 auto Body=[this,Registration]()
 {
  auto& S=Registration.Get();
  if(FPlatformTime::Seconds()-S.Started>UProject_JVisualAssetSubsystem::RequestLifetimeSeconds)
  { AddError(TEXT("Actual warm equipment delivery/registration did not finish within the existing loader lifetime"));return true; }
  auto* Service=S.World->GetSubsystem<UProject_JVisualAssetSubsystem>();
  if(!TestNotNull(TEXT("Actual registration visual loader"),Service)) return true;
  if(S.Case==4) return true;
  if(S.Stage==0)
  {
   S.Calls=0;S.Candidate.Reset();S.NewRevision=0;S.NewToken=0;
   S.Runtime.Reset(NewObject<UProject_JEquipmentRuntimeComponent>(S.Owner.Get()));S.Runtime->RegisterComponent();
   S.Item.Reset(NewObject<UProject_JEquipmentItemDefinition>());S.Other.Reset(NewObject<UProject_JEquipmentItemDefinition>());
   const TStrongObjectPtr<USkeletalMesh> SetupMesh(DuplicateObject<USkeletalMesh>(S.Owner->GetMesh()->GetSkeletalMeshAsset(),GetTransientPackage(),
    MakeUniqueObjectName(GetTransientPackage(),USkeletalMesh::StaticClass(),TEXT("EquipmentRegistration121"))));
   if(!TestTrue(TEXT("Transient equipment mesh duplicate exists"),SetupMesh.IsValid())) return true;
   SetupMesh->ClearFlags(RF_Public|RF_Standalone);SetupMesh->SetFlags(RF_Transient);
   SetupMesh->SetPostProcessAnimBlueprint(UProject_JEquipmentRegistrationAnimFixture::StaticClass());
   if(!TestFalse(TEXT("Transient duplicate is not test-rooted"),SetupMesh->IsRooted())) return true;
   S.Payload=SetupMesh.Get();
   S.SetupPin.Reset(SetupMesh.Get()); // Keep the authored warm precondition only until real delivery.
   for(auto* Definition:{S.Item.Get(),S.Other.Get()})
   {Definition->EquipmentSlot=EProject_JEquipmentSlot::Head;Definition->PoseMode=EProject_JEquipmentPoseMode::CopyPose;Definition->EquipmentMesh=TSoftObjectPtr<USkeletalMesh>(SetupMesh.Get());}
   FEquipmentRegistration121* Context=&S;
   S.Owner->OnEquipmentAnimationInitialize=[this,Context](USkeletalMeshComponent* Candidate)
   {
    auto& C=*Context;++C.Calls;C.Candidate=Candidate;
    TestTrue(TEXT("Callback is the actual registered mesh's native post-process instance"),Candidate && Candidate->GetPostProcessInstance() &&
     Candidate->GetPostProcessInstance()->IsA<UProject_JEquipmentRegistrationAnimFixture>() && Candidate->GetSkeletalMeshAsset()==C.Payload.Get());
    if(C.Case==1) C.Runtime->BindToEquipmentManager(nullptr);
    else if(C.Case==2)
    {
     C.Runtime->OnEquipmentEquipped(C.Item->EquipmentSlot,C.Other.Get());
     C.Runtime->OnEquipmentEquipped(C.Item->EquipmentSlot,C.Item.Get());
     const auto* New=C.Runtime->RuntimeItems.Find(C.Item->EquipmentSlot);
     if(TestNotNull(TEXT("Actual A-B-A event creates a replacement row"),New))
     {C.NewRevision=New->VisualRevision;C.NewToken=New->VisualLoadToken;}
     TestTrue(TEXT("A-B-A owns a new revision and a real replacement request"),C.NewRevision!=C.OldRevision && C.NewToken!=0 && C.NewToken!=C.OldToken);
    }
    else if(C.Case==3) C.Runtime->DestroyComponent();
   };
   S.Runtime->OnEquipmentEquipped(S.Item->EquipmentSlot,S.Item.Get());
   const auto* Row=S.Runtime->RuntimeItems.Find(S.Item->EquipmentSlot);
   if(!TestNotNull(TEXT("Actual copy equipment request row"),Row)) return true;
   S.OldRevision=Row->VisualRevision;S.OldToken=Row->VisualLoadToken;
   TestTrue(TEXT("Real warm request is accepted before application"),S.OldToken!=0 && Service->GetLeaseCount()==1);
   TestNull(TEXT("Shared loader does not create a visual inline"),Row->SpawnedMesh);
   TestEqual(TEXT("Post-process initialization has not been injected manually"),S.Calls,0);
   S.Stage=1;
   // This temporary dies here. The explicit setup pin is retired after real delivery, before GC assertions.
   return false;
  }
  Service->Tick(0.f); // Readiness follows the real streamable callback on natural engine frames.
  if(S.Calls==0) return false;
  TestEqual(TEXT("Real native initialization callback is consumed exactly once"),S.Calls,1);
  const auto* Row=S.Runtime->RuntimeItems.Find(S.Item->EquipmentSlot);
  if(S.Case==0)
  {
   auto* Visual=Row ? Row->SpawnedMesh : nullptr;
   if(!TestNotNull(TEXT("Normal warm application publishes its component"),Visual)) return true;
   S.SetupPin.Reset();
   TestFalse(TEXT("The fixture does not pin the duplicate during the ownership GC check"),S.SetupPin.IsValid());
   TestTrue(TEXT("Registered CopyPose part uses the actual post-process fixture"),Visual->IsRegistered() &&
    Visual->GetPostProcessInstance() && Visual->GetPostProcessInstance()->IsA<UProject_JEquipmentRegistrationAnimFixture>() && Visual==S.Candidate.Get());
   // HANDOFF_ASSERT_BEGIN_121
   TestEqual(TEXT("Successful component handoff detaches the exact load token"),Row->VisualLoadToken,uint64(0));
   TestEqual(TEXT("Successful component handoff no longer retains the loader lease"),Service->GetLeaseCount(),0);
   TestEqual(TEXT("Successful component handoff no longer retains the loader group"),Service->GetGroupCount(),0);
   // HANDOFF_ASSERT_END_121
   AddInfo(FString::Printf(TEXT("Equipment121 normal lease=%d groups=%d token=%llu candidate=%u payload=%u"),Service->GetLeaseCount(),Service->GetGroupCount(),Row->VisualLoadToken,
    Visual->GetUniqueID(),S.Payload.IsValid()?S.Payload->GetUniqueID():0));
   CollectGarbage(RF_NoFlags);
   TestTrue(TEXT("Registered component keeps its SkinnedAsset alive across real GC after setup pin retirement"),
    S.Payload.IsValid() && Visual->GetSkeletalMeshAsset()==S.Payload.Get());
   S.Runtime->OnEquipmentUnequipped(S.Item->EquipmentSlot,S.Item.Get());
   TestTrue(TEXT("Normal handoff component is destroyed by real unequip"),!IsValid(Visual) || Visual->IsBeingDestroyed());
   TestEqual(TEXT("Normal unequip leaves no loader lease"),Service->GetLeaseCount(),0);
  }
  else
  {
   TestTrue(TEXT("Retired registration cannot leave its candidate registered"),!S.Candidate.IsValid() ||
    S.Candidate->IsBeingDestroyed() || !S.Candidate->IsRegistered());
   if(S.Case==2)
   {
    if(!TestNotNull(TEXT("Fresh A-B-A row survives old completion"),Row)) return true;
    TestEqual(TEXT("Older completion does not replace the new generation"),Row->VisualRevision,S.NewRevision);
    TestTrue(TEXT("Replacement request or its own published component survives old cleanup"),
     (Row->VisualLoadToken==S.NewToken && !Row->SpawnedMesh) ||
     (Row->SpawnedMesh && Row->SpawnedMesh!=S.Candidate.Get()));
    if(!Row->SpawnedMesh) return false;
    S.SetupPin.Reset();
    TestTrue(TEXT("Fresh A-B-A loads its own registered component"),Row->SpawnedMesh!=S.Candidate.Get() && Row->SpawnedMesh->IsRegistered());
    // HANDOFF_ASSERT_BEGIN_121
    TestEqual(TEXT("Replacement handoff returns its own load token"),Row->VisualLoadToken,uint64(0));
    TestEqual(TEXT("Replacement handoff returns its own loader lease"),Service->GetLeaseCount(),0);
    // HANDOFF_ASSERT_END_121
   }
   else
   {
    S.SetupPin.Reset();
    TestNull(TEXT("Retired slot is not republished by its old registration"),Row);
    TestEqual(TEXT("Retired registration retains no loader lease"),Service->GetLeaseCount(),0);
   }
  }
  TArray<UProject_JModularMeshComponent*> Parts;S.Owner->GetComponents(Parts);
  int32 Registered=0;for(const auto* Part:Parts) Registered+=IsValid(Part) && !Part->IsBeingDestroyed() && Part->IsRegistered()?1:0;
  TestEqual(TEXT("Actor has only its current equipment component, no unpublished orphan"),Registered,S.Case==2?1:0);
  AddInfo(FString::Printf(TEXT("Equipment121 case=%d callbacks=%d registered=%d leases=%d oldRevision=%llu newRevision=%llu"),
   S.Case,S.Calls,Registered,Service->GetLeaseCount(),S.OldRevision,S.NewRevision));
  S.Owner->OnEquipmentAnimationInitialize=nullptr;
  S.Runtime->DestroyComponent();
  TestTrue(TEXT("Ending runtime clears the exact current row"),S.Runtime->RuntimeItems.IsEmpty());
  TestEqual(TEXT("Ending runtime returns all request leases"),Service->GetLeaseCount(),0);
  // Cleanup follows the orphan assertion; before-fix failure evidence is not hidden.
  for(auto* Part:Parts) if(IsValid(Part) && !Part->IsBeingDestroyed()) Part->DestroyComponent();
  S.Runtime.Reset();S.Item.Reset();S.Other.Reset();S.Payload.Reset();S.Candidate.Reset();
  ++S.Case;S.Stage=0;return S.Case==4;
 };
 ADD_LATENT_AUTOMATION_COMMAND(FEquipmentRegistrationCommand121(MoveTemp(Body)));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponPresentationTeardownTest,
	"ProjectJ.NPCGameplay.WeaponPresentationTeardown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponPresentationTeardownTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	auto* Owner = World->SpawnActor<ACharacter>();
	auto* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Owner); Presentation->RegisterComponent();
	TestEqual(TEXT("Live presentation follows actual renderer capability"), Presentation->CanCreatePresentation(), FApp::CanEverRender());
	Presentation->BeginPlay();
	auto* Weapon = World->SpawnActor<AActor>(); Presentation->SpawnedWeapon = Weapon;
	Presentation->EndPlay(EEndPlayReason::LevelTransition);
	TestFalse(TEXT("Ended component blocks visuals"), Presentation->CanCreatePresentation());
	TestNull(TEXT("Owned weapon removed"), Presentation->GetSpawnedWeapon());
	TestTrue(TEXT("Owned weapon destroyed"), !IsValid(Weapon) || Weapon->IsActorBeingDestroyed());
	Presentation->RefreshPresentation(); Presentation->ExitCombatPresentation(); // No attempt to resolve/spawn after end.
	TestNull(TEXT("Late callbacks do not recreate weapon"), Presentation->GetSpawnedWeapon());
	Presentation->DestroyComponent();
	auto* Early = NewObject<UProject_JWeaponPresentationComponent>(Owner); Early->RegisterComponent();
	auto* EarlyWeapon = World->SpawnActor<AActor>(); Early->SpawnedWeapon = EarlyWeapon;
	World->bIsTearingDown = true;
	TestFalse(TEXT("World teardown blocks visuals before component EndPlay"), Early->CanCreatePresentation());
	Early->RefreshPresentation();
	TestNull(TEXT("Teardown refresh only cleans up"), Early->GetSpawnedWeapon());
	Early->DestroyComponent(); World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentRetryTest, "ProjectJ.GroupA.EquipmentRetry",
 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJEquipmentRetryTest::RunTest(const FString& Parameters)
{
 UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
 GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
 auto* Owner = World->SpawnActor<ACharacter>();
 auto* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Owner); Runtime->RegisterComponent();
 auto* Service = World->GetSubsystem<UProject_JVisualAssetSubsystem>();
 if (!FApp::CanEverRender())
 {
  Runtime->BeginPlay();
  auto* Item = NewObject<UProject_JEquipmentItemDefinition>(Runtime);
  Item->EquipmentSlot = EProject_JEquipmentSlot::Head;
  Item->EquipmentMesh = TSoftObjectPtr<USkeletalMesh>(FSoftObjectPath(TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube")));
  const uint64 Accepted = Service->GetStats().Accepted;
  Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
  for (int32 Index = 0; Index < 16; ++Index) Runtime->RetryEquipmentVisuals();
  TestEqual(TEXT("Headless repeated retry creates no loader work"), Service->GetStats().Accepted, Accepted);
  TestEqual(TEXT("Headless retry does not spend visual attempts"), Runtime->RuntimeItems[Item->EquipmentSlot].VisualAttempts, uint32(0));
  TestFalse(TEXT("Headless retry does not arm a timer"), World->GetTimerManager().TimerExists(Runtime->VisualRetryTimer));
  Runtime->EndPlay(EEndPlayReason::LevelTransition);
  TestTrue(TEXT("Headless retry teardown clears its runtime entries"), Runtime->RuntimeItems.IsEmpty());
  World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
  return true;
 }
 TArray<uint64> Fill;
 Runtime->BeginPlay();
 const FSoftObjectPath Path(TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
 for (int32 I = 0; I < Service->MaxLeasesPerOwner; ++I) { Fill.Add(Service->Request(Runtime, Path)); }
 auto* Item = NewObject<UProject_JEquipmentItemDefinition>(Runtime);
 Item->EquipmentSlot = EProject_JEquipmentSlot::Head; Item->EquipmentMesh = TSoftObjectPtr<USkeletalMesh>(Path);
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 auto& State = Runtime->RuntimeItems[Item->EquipmentSlot];
 TestEqual(TEXT("Rejected admission consumes first bounded attempt"), State.VisualAttempts, uint32(1));
 TestTrue(TEXT("Rejected admission automatically schedules retry"), World->GetTimerManager().TimerExists(Runtime->VisualRetryTimer));
 for (int32 I = 0; I < 10; ++I) { Runtime->RetryEquipmentVisuals(); }
 TestEqual(TEXT("Repeated manual retry respects deadline"), State.VisualAttempts, uint32(1));
 for (uint32 I = 1; I < Runtime->MaxVisualAttempts; ++I) { State.NextVisualRetry = 0; Runtime->RetryEquipmentVisuals(); }
 TestEqual(TEXT("Retry count bounded"), State.VisualAttempts, Runtime->MaxVisualAttempts);
 TestFalse(TEXT("Exhaustion leaves no retry timer"), World->GetTimerManager().TimerExists(Runtime->VisualRetryTimer));
 for (const auto Token : Fill) { Service->Release(Token); }
 const auto OldRevision = State.VisualRevision;
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 TestTrue(TEXT("Re-equip creates fresh generation"), Runtime->RuntimeItems[Item->EquipmentSlot].VisualRevision != OldRevision);
 TestTrue(TEXT("Fresh generation can request again"), Runtime->RuntimeItems[Item->EquipmentSlot].VisualLoadToken != 0);
 Runtime->EndPlay(EEndPlayReason::LevelTransition);
 TestFalse(TEXT("EndPlay clears pending retry"), World->GetTimerManager().TimerExists(Runtime->VisualRetryTimer));
 TestEqual(TEXT("EndPlay releases equipment lease"), Service->GetLeaseCount(), 0);
 World->DestroyWorld(false); GEngine->DestroyWorldContext(World);
 return true;
}
#endif
