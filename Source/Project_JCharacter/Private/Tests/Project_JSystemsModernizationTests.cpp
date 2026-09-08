#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
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
#include "Project_JGreatswordCharacter.h"
#include "UObject/UnrealType.h"
#include "Engine/Engine.h"
#include "TimerManager.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCombatInputBoundaryTest,
	"ProjectJ.Modernization.CombatInputBoundary",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FProjectJCombatInputBoundaryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	AProject_JPlayerCharacter* Player = World->SpawnActor<AProject_JGreatswordCharacter>();
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
	Input->ServerSendCombatInputEvent_Implementation(Tags.Event_Combat_ComboWindow, 0.0f, 1);
	TestEqual(TEXT("Client cannot inject a montage gameplay event through input RPC"), GameplayEvents, 0);
	Input->ServerSendCombatInputEvent_Implementation(Tags.InputTag_Weapon_LMB, 0.0f, 2);
	TestEqual(TEXT("Valid LMB input reaches the server event listener"), ValidInputs, 1);
	Input->ServerSendCombatInputEvent_Implementation(Tags.InputTag_Weapon_LMB, 0.0f, 2);
	TestEqual(TEXT("Duplicate sequence does not replay input"), ValidInputs, 1);
	int32 CommandEvents = 0;
	const FGameplayTag CommandTag = FGameplayTag::RequestGameplayTag(TEXT("Command.Greatsword.Special1"));
	ASC->GenericGameplayEventCallbacks.FindOrAdd(CommandTag).AddLambda(
		[&CommandEvents](const FGameplayEventData*) { ++CommandEvents; });
	Input->ServerSendCombatInputEvent_Implementation(CommandTag, 0.0f, 3);
	TestEqual(TEXT("Client cannot skip command matching with a resolved alias"), CommandEvents, 0);
	Input->ServerSendCombatInputEvent_Implementation(FGameplayTag(), 0.0f, 4);
	Input->ServerSendCombatInputEvent_Implementation(Tags.Event_Combat_ComboWindow, 0.0f, 100);
	Input->ServerSendCombatInputEvent_Implementation(Tags.InputTag_Weapon_LMB, 0.0f, 3);
	TestEqual(TEXT("Rejected tags do not advance the accepted sequence"), ValidInputs, 2);
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
 TestNotNull(TEXT("Engine fixture"), Mesh);
 Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
 Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
 auto* Visual = Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh;
 TestNotNull(TEXT("Completed asset creates visual"), Visual);
 Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
 TestEqual(TEXT("No duplicated visual"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh, Visual);
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
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJWeaponPresentationTeardownTest,
	"ProjectJ.NPCGameplay.WeaponPresentationTeardown", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FProjectJWeaponPresentationTeardownTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	auto* Owner = World->SpawnActor<ACharacter>();
	auto* Presentation = NewObject<UProject_JWeaponPresentationComponent>(Owner); Presentation->RegisterComponent();
	TestTrue(TEXT("Live component permits visuals"), Presentation->CanCreatePresentation());
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
	Early->DestroyComponent(); World->DestroyWorld(false);
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
