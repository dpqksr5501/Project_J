#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "Components/Project_JEquipmentRuntimeComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JModularMeshComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JGreatswordCharacter.h"
#include "UObject/UnrealType.h"

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
	UProject_JEquipmentRuntimeComponent* Runtime = NewObject<UProject_JEquipmentRuntimeComponent>(Owner);
	Runtime->RegisterComponent();
	UProject_JEquipmentManagerComponent* Manager = NewObject<UProject_JEquipmentManagerComponent>(Owner);
	Runtime->BindToEquipmentManager(Manager);
	UProject_JEquipmentItemDefinition* Item = NewObject<UProject_JEquipmentItemDefinition>(Runtime);
	Item->EquipmentSlot = EProject_JEquipmentSlot::Head;
	const FSoftObjectPath MeshPath(TEXT("/Engine/EngineMeshes/SkeletalCube.SkeletalCube"));
	Item->EquipmentMesh = TSoftObjectPtr<USkeletalMesh>(MeshPath);
	FStreamableManager& Streamable = UAssetManager::GetStreamableManager();

	Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	TArray<TSharedRef<FStreamableHandle>> Handles;
	Streamable.GetActiveHandles(MeshPath, Handles);
	TestTrue(TEXT("Equipment load is pending before the delayed callback"), !Handles.IsEmpty());
	Runtime->OnEquipmentUnequipped(Item->EquipmentSlot, Item);
	for (const TSharedRef<FStreamableHandle>& Handle : Handles)
	{
		TestTrue(TEXT("Unequip cancels its pending load, including delayed completion"), Handle->WasCanceled());
		Handle->CancelHandle(); // Isolate the baseline failure from subsequent scenarios.
	}

	Handles.Reset();
	Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	Streamable.GetActiveHandles(MeshPath, Handles);
	Runtime->BindToEquipmentManager(nullptr);
	for (const TSharedRef<FStreamableHandle>& Handle : Handles)
	{
		TestTrue(TEXT("Unbind cancels its pending load"), Handle->WasCanceled());
		Handle->CancelHandle();
	}

	// A failed/absent asset must not register an empty skeletal component.
	Item->EquipmentMesh.Reset();
	Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
	TestNull(TEXT("Unresolved mesh creates no visual component"), Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh);
	Runtime->OnEquipmentUnequipped(Item->EquipmentSlot, Item);

	// Load only the engine fixture to verify that successful visual creation still works.
	USkeletalMesh* LoadedMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath.ToString());
	TestNotNull(TEXT("Engine mesh fixture loads"), LoadedMesh);
	Item->EquipmentMesh = TSoftObjectPtr<USkeletalMesh>(MeshPath);
	Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
	UProject_JModularMeshComponent* SpawnedMesh = Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh;
	TestNotNull(TEXT("Resolved mesh creates a visual"), SpawnedMesh);
	if (SpawnedMesh)
	{
		TestEqual(TEXT("Visual uses the loaded asset"), SpawnedMesh->GetSkeletalMeshAsset(), LoadedMesh);
	}
	Runtime->OnEquipmentMeshLoaded(Item->EquipmentSlot, Item);
	TestTrue(TEXT("Repeated completion does not duplicate the visual"),
		Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh == SpawnedMesh);
	Runtime->OnEquipmentUnequipped(Item->EquipmentSlot, Item);

	// Requests for an already resident asset still have delayed completions.
	Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	TSharedPtr<FStreamableHandle> FirstA = Runtime->RuntimeItems[Item->EquipmentSlot].MeshLoadHandle;
	UProject_JEquipmentItemDefinition* OtherItem = NewObject<UProject_JEquipmentItemDefinition>(Runtime);
	OtherItem->EquipmentSlot = Item->EquipmentSlot;
	OtherItem->EquipmentMesh = Item->EquipmentMesh;
	Runtime->OnEquipmentEquipped(OtherItem->EquipmentSlot, OtherItem);
	TSharedPtr<FStreamableHandle> B = Runtime->RuntimeItems[Item->EquipmentSlot].MeshLoadHandle;
	Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	TestTrue(TEXT("A -> B -> A cancels the first A"), FirstA && FirstA->WasCanceled());
	TestTrue(TEXT("A -> B -> A cancels B"), B && B->WasCanceled());
	TSharedPtr<FStreamableHandle> LastA = Runtime->RuntimeItems[Item->EquipmentSlot].MeshLoadHandle;
	TestTrue(TEXT("New A request remains active"), LastA && !LastA->WasCanceled());
	Runtime->OnEquipmentMeshLoaded(OtherItem->EquipmentSlot, OtherItem);
	TestNull(TEXT("An obsolete item cannot create the new slot's visual"),
		Runtime->RuntimeItems[Item->EquipmentSlot].SpawnedMesh);
	Runtime->BeginPlay();
	Runtime->EndPlay(EEndPlayReason::LevelTransition);
	TestTrue(TEXT("Level transition cancels the remaining load"), LastA && LastA->WasCanceled());
	TestTrue(TEXT("Level transition clears slots even without a bound manager"), Runtime->RuntimeItems.IsEmpty());
	Runtime->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	TestTrue(TEXT("An ended component cannot accept new equipment"), Runtime->RuntimeItems.IsEmpty());
	Runtime->DestroyComponent();

	UProject_JEquipmentRuntimeComponent* BeforeBeginPlay = NewObject<UProject_JEquipmentRuntimeComponent>(Owner);
	BeforeBeginPlay->RegisterComponent();
	BeforeBeginPlay->OnEquipmentEquipped(Item->EquipmentSlot, Item);
	TSharedPtr<FStreamableHandle> PrePlayLoad = BeforeBeginPlay->RuntimeItems[Item->EquipmentSlot].MeshLoadHandle;
	BeforeBeginPlay->DestroyComponent();
	TestTrue(TEXT("Destruction before BeginPlay cancels its request"), PrePlayLoad && PrePlayLoad->WasCanceled());
	World->DestroyWorld(false);
	return true;
}

#endif
