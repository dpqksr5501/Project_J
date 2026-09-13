#if WITH_DEV_AUTOMATION_TESTS
#include <limits>
#include "Misc/AutomationTest.h"
#include "Tests/Project_JInternalTestReceiver.h"
#include "Animation/Project_JTrajectoryQuery.h"
#include "Animation/Project_JCharacterAnimInstanceProxy.h"
#include "Combat/Project_JServerSideRewindComponent.h"
#include "Camera/Project_JCameraComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/Project_JPlayerInputBindingComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Inventory/Project_JItemDefinition.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "Interaction/Project_JInteractionQuery.h"
#include "Interaction/Project_JInteractionTargetComponent.h"
#include "Components/SphereComponent.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/WorldSettings.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JGameplayTags.h"
#include "Project_JAbilitySystemComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JCombatCommandSet.h"
#include "UObject/UnrealType.h"
#include "System/Project_JObjectPoolRegistrySubsystem.h"

namespace
{
struct FInternalTestWorld
{
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false);
	FInternalTestWorld() { GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World); }
	~FInternalTestWorld() { if (World->GetBegunPlay()) World->EndPlay(EEndPlayReason::LevelTransition); World->DestroyWorld(false); GEngine->DestroyWorldContext(World); }
};
constexpr auto Flags = EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInventoryCallbacksTest, "ProjectJ.Internal.Inventory.CallbacksAndBounds", Flags)
bool FProjectJInventoryCallbacksTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Owner = Scope.World->SpawnActor<AActor>();
	auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
	auto* Definition = NewObject<UProject_JEquipmentItemDefinition>(); Definition->MaxStackCount = MAX_int32;
	auto* Receiver = NewObject<UProject_JInternalTestReceiver>(); Receiver->Inventory = Inventory;
	Inventory->OnItemAdded.AddDynamic(Receiver, &UProject_JInternalTestReceiver::OnAdded);
	const auto Added = Inventory->AddItemDefinition(Definition, 2);
	TestTrue(TEXT("Add callback keeps its payload across array growth"), Receiver->bPayloadStable);
	TestTrue(TEXT("Add returns the committed identity after reentrant adds"), Inventory->HasItemInstance(Added.InstanceId));
	TestFalse(TEXT("Stack addition rejects int32 overflow"), Inventory->AddItemStackCount(Added.InstanceId, MAX_int32));
	TestFalse(TEXT("Zero delta does not validate an unknown identity"), Inventory->AddItemStackCount(FGuid::NewGuid(), 0));
	for (bool bConsume : {false, true})
	{
		const auto Removed = Inventory->AddItemDefinition(Definition);
		Receiver->OtherItem = Inventory->AddItemDefinition(Definition).InstanceId;
		Receiver->bMutate = true;
		Inventory->OnItemRemoved.AddUniqueDynamic(Receiver, &UProject_JInternalTestReceiver::OnRemoved);
		TestTrue(TEXT("Removal succeeds"), bConsume ? Inventory->ConsumeItemStack(Removed.InstanceId) : Inventory->RemoveItemInstance(Removed.InstanceId));
		TestTrue(TEXT("Removal callback observes committed state"), Receiver->bRemovalCommitted);
		TestTrue(TEXT("The same removal cannot recurse"), Receiver->bRecursiveRemovalRejected);
		TestFalse(TEXT("Nested removal of a different item is safe"), Inventory->HasItemInstance(Receiver->OtherItem));
	}
	FProject_JItemInstanceData Out = Added;
	TestFalse(TEXT("Unknown lookup fails"), Inventory->FindItemInstance(FGuid::NewGuid(), Out));
	TestFalse(TEXT("Failed lookup clears the prior result"), Out.IsValid());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJEquipmentCallbacksTest, "ProjectJ.Internal.Equipment.ReentrantRemoval", Flags)
bool FProjectJEquipmentCallbacksTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Owner = Scope.World->SpawnActor<AActor>();
	auto* Inventory = NewObject<UProject_JInventoryComponent>(Owner); Inventory->RegisterComponent();
	auto* Equipment = NewObject<UProject_JEquipmentManagerComponent>(Owner); Equipment->RegisterComponent();
	auto* Definition = NewObject<UProject_JEquipmentItemDefinition>(); Definition->EquipmentSlot = EProject_JEquipmentSlot::Weapon;
	const auto First = Inventory->AddItemDefinition(Definition);
	const auto Second = Inventory->AddItemDefinition(Definition);
	TestTrue(TEXT("Initial inventory equipment succeeds"), Equipment->TryEquipItemInstanceById(First.InstanceId).bSucceeded);
	auto* Receiver = NewObject<UProject_JInternalTestReceiver>(); Receiver->Equipment = Equipment; Receiver->ReentrantEquipmentItem = Second.InstanceId;
	Equipment->OnEquipmentUnequipped.AddDynamic(Receiver, &UProject_JInternalTestReceiver::OnUnequipped);
	Equipment->UnequipSlot(EProject_JEquipmentSlot::Weapon);
	TestTrue(TEXT("Unequip notification observes an empty slot"), Receiver->bRemovalCommitted);
	TestEqual(TEXT("Nested slot mutation is explicitly rejected"), Receiver->ReentrantFailure, EProject_JEquipmentOperationFailure::OperationInProgress);
	TestFalse(TEXT("Removed inventory item is unlocked"), Inventory->IsItemInstanceLocked(First.InstanceId));
	TestTrue(TEXT("New command succeeds after the transaction exits"), Equipment->TryEquipItemInstanceById(Second.InstanceId).bSucceeded);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJTrajectoryQueryTest, "ProjectJ.Internal.Trajectory.TimeLayoutAndInvalidInput", Flags)
bool FProjectJTrajectoryQueryTest::RunTest(const FString&)
{
	FTransformTrajectory Trajectory; Trajectory.Samples.SetNum(3);
	Trajectory.Samples[0].TimeInSeconds = 0.0f;
	Trajectory.Samples[1].TimeInSeconds = 0.2f;
	Trajectory.Samples[2].TimeInSeconds = 0.5f;
	Trajectory.Samples[1].SetTransform(FTransform(FVector(20, 0, 0)));
	Trajectory.Samples[2].SetTransform(FTransform(FVector(0, 100, 0)));
	FVector Velocity; float Angle;
	TestTrue(TEXT("Initial layout resolves horizon"), Project_J::Animation::TryGetFuturePlanarVelocity(Trajectory, 0.2f, FVector(100, 0, 0), Velocity, Angle));
	TestTrue(TEXT("Initial planar speed"), Velocity.Equals(FVector(100, 0, 0)));
	Trajectory.Samples[1].TimeInSeconds = 0.8f; Trajectory.Samples[2].TimeInSeconds = 0.2f;
	TestTrue(TEXT("Same sample count with changed times resolves again"), Project_J::Animation::TryGetFuturePlanarVelocity(Trajectory, 0.2f, FVector(100, 0, 0), Velocity, Angle));
	TestTrue(TEXT("New sample supplies velocity"), Velocity.Equals(FVector(0, 500, 0), 0.01));
	TestTrue(TEXT("Turn angle"), FMath::IsNearlyEqual(Angle, 90.0f));
	TestFalse(TEXT("NaN horizon fails closed"), Project_J::Animation::TryGetFuturePlanarVelocity(Trajectory, std::numeric_limits<float>::quiet_NaN(), FVector::ZeroVector, Velocity, Angle));
	TestTrue(TEXT("Failure clears outputs"), Velocity.IsZero() && Angle == 0.0f);
	Trajectory.Samples[2].TimeInSeconds = std::numeric_limits<float>::infinity();
	TestFalse(TEXT("Infinite sample time fails closed"), Project_J::Animation::TryGetFuturePlanarVelocity(Trajectory, 0.2f, FVector::ZeroVector, Velocity, Angle));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJRewindHistoryTest, "ProjectJ.Internal.Combat.RewindHistory", Flags)
bool FProjectJRewindHistoryTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Character = Scope.World->SpawnActor<ACharacter>();
	auto* Rewind = NewObject<UProject_JServerSideRewindComponent>(Character); Rewind->PoseHistory.SetNum(3);
	FProject_JPoseHistoryBuffer Record, A, B; float Alpha;
	Record.Timestamp = 1; Rewind->AppendPoseHistoryRecord(Record);
	TestTrue(TEXT("Single exact record is queryable"), Rewind->GetPosesForTime(1, A, B, Alpha));
	for (int32 Time = 2; Time <= 4; ++Time) { Record.Timestamp = Time; Record.CapsuleHalfHeight = Time * 20; Rewind->AppendPoseHistoryRecord(Record); }
	TestFalse(TEXT("Overwritten record is outside history"), Rewind->GetPosesForTime(1, A, B, Alpha));
	TestTrue(TEXT("Oldest retained endpoint is queryable"), Rewind->GetPosesForTime(2, A, B, Alpha));
	TestTrue(TEXT("Wrapped ring interpolates"), Rewind->GetPosesForTime(2.5f, A, B, Alpha));
	TestEqual(TEXT("Interpolation alpha"), Alpha, 0.5f);
	TestEqual(TEXT("Historical geometry is retained"), A.CapsuleHalfHeight, 40.0f);
	TestEqual(TEXT("Next historical geometry is retained"), B.CapsuleHalfHeight, 60.0f);
	Record.CapsuleHalfHeight = 99; Rewind->AppendPoseHistoryRecord(Record);
	TestEqual(TEXT("Duplicate time replaces instead of consuming capacity"), Rewind->PoseHistoryCount, 3);
	Rewind->GetPosesForTime(4, A, B, Alpha); TestEqual(TEXT("Duplicate has latest geometry"), A.CapsuleHalfHeight, 99.0f);
	TestFalse(TEXT("NaN time rejected"), Rewind->GetPosesForTime(std::numeric_limits<float>::quiet_NaN(), A, B, Alpha));
	Record.Timestamp = 0.5f; Rewind->AppendPoseHistoryRecord(Record);
	TestEqual(TEXT("Clock rollback starts a new history"), Rewind->PoseHistoryCount, 1);
	Rewind->ResetHistory(); TestFalse(TEXT("Teleport reset cannot bridge old poses"), Rewind->GetPosesForTime(0.5f, A, B, Alpha));
	Record.Timestamp = Scope.World->GetTimeSeconds(); Record.CapsuleLocation = FVector::ZeroVector;
	Record.CapsuleRadius = 20.0f; Record.CapsuleHalfHeight = 100.0f;
	Rewind->AppendPoseHistoryRecord(Record);
	Character->GetCapsuleComponent()->SetCapsuleSize(20.0f, 20.0f);
	TestTrue(TEXT("Rewind hit uses historical standing capsule after current capsule shrinks"),
		Rewind->ServerVerifyHit(Record.Timestamp, FVector(-100, 0, 80), FVector(100, 0, 80)));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInputLifecycleTest, "ProjectJ.Internal.Input.Rebinding", Flags)
bool FProjectJInputLifecycleTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Player = Scope.World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* Binding = Player->FindComponentByClass<UProject_JPlayerInputBindingComponent>();
	auto* Router = Player->FindComponentByClass<UProject_JSkillInputRouterComponent>();
	const FGameplayTag Tag = FProject_JGameplayTags::Get().InputTag_Weapon_LMB;
	FGameplayTagContainer Required; Required.AddTag(Tag);
	Router->HandleModifierPressed(Tag); TestTrue(TEXT("Modifier initially active"), Router->AreModifierTagsMatched(Required, {}));
	Router->Initialize(Player); TestFalse(TEXT("Reinitialize clears held modifiers"), Router->AreModifierTagsMatched(Required, {}));
	auto* Input = NewObject<UEnhancedInputComponent>(Player);
	FProject_JPlayerInputActionSet Actions; Actions.JumpAction = NewObject<UInputAction>();
	TestTrue(TEXT("First binding succeeds"), Binding->BindInput(Input, Player, Actions));
	const int32 Count = Input->GetActionEventBindings().Num();
	TestEqual(TEXT("Jump has press/completed/canceled bindings"), Count, 3);
	Binding->BindInput(Input, Player, Actions);
	TestEqual(TEXT("Rebinding does not duplicate callbacks"), Input->GetActionEventBindings().Num(), Count);
	Binding->UnbindInput(); TestEqual(TEXT("Owned bindings removed"), Input->GetActionEventBindings().Num(), 0);
	Binding->UnbindInput(); TestFalse(TEXT("Repeated unbind keeps reconciliation idle"), Binding->IsComponentTickEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCameraOwnershipTest, "ProjectJ.Internal.Camera.Possession", Flags)
bool FProjectJCameraOwnershipTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Pawn = Scope.World->SpawnActor<ACharacter>();
	auto* Controller = Scope.World->SpawnActor<APlayerController>();
	auto* Boom = NewObject<USpringArmComponent>(Pawn); Boom->RegisterComponent(); Boom->bEnableCameraLag = true;
	auto* View = NewObject<UCameraComponent>(Pawn); View->RegisterComponent();
	auto* Camera = NewObject<UProject_JCameraComponent>(Pawn); Camera->Initialize(Boom, View); Camera->RegisterComponent();
	Scope.World->InitializeActorsForPlay(FURL());
	Scope.World->GetWorldSettings()->NotifyBeginPlay();
	TestFalse(TEXT("Unpossessed camera is inactive"), View->IsActive());
	Controller->SetAsLocalPlayerController();
	Controller->Possess(Pawn);
	TestTrue(TEXT("Local possession restores the camera"), View->IsActive() && Camera->IsComponentTickEnabled());
	TestTrue(TEXT("Authored lag configuration survives"), Boom->bEnableCameraLag);
	Controller->UnPossess();
	TestFalse(TEXT("Ownership loss disables interpolation"), Camera->IsComponentTickEnabled());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJPoolDefinitionBoundsTest, "ProjectJ.Internal.Pooling.DefinitionBounds", Flags)
bool FProjectJPoolDefinitionBoundsTest::RunTest(const FString&)
{
	auto* Registry = NewObject<UProject_JObjectPoolRegistrySubsystem>(NewObject<UGameInstance>());
	FProject_JObjectPoolDefinition Definition; Definition.PoolId = TEXT("InternalTest");
	Definition.PrewarmCount = -2; Definition.MaxRetainedCount = -1;
	TestFalse(TEXT("Runtime API rejects negative counts despite ordered bounds"), Registry->RegisterDefinition(Definition));
	Definition.PrewarmCount = 0; Definition.MaxRetainedCount = 0;
	TestTrue(TEXT("An empty pool policy is valid"), Registry->RegisterDefinition(Definition));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJAnimationSnapshotBoundaryTest, "ProjectJ.Internal.Animation.SnapshotBoundary", Flags)
bool FProjectJAnimationSnapshotBoundaryTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Character = Scope.World->SpawnActor<ACharacter>();
	auto* Anim = NewObject<UProject_JCharacterAnimInstance>(Character->GetMesh());
	Anim->FootPlacementPlantSettingsDefault.UnplantRadius = 42.0f;
	FProject_JAnimThreadSafeData Data;
	Anim->FillProceduralIKThreadSafeData(Data);
	TestEqual(TEXT("Game-thread publication captures authored IK settings"), Data.ProceduralIK.PlantSettings.UnplantRadius, 42.0f);
	Data.bIsLocallyControlled = true;
	Data.LocomotionContext.DesiredFacingDeltaYaw = 60.0f;
	auto& Proxy = Anim->GetProxyOnGameThread<FProject_JCharacterAnimInstanceProxy>();
	Proxy.ThreadSafeData = Data;
	Anim->FootPlacementPlantSettingsDefault.UnplantRadius = 99.0f;
	TestEqual(TEXT("Getter reads the published frame, not mutable profile defaults"), Anim->Get_FootPlacementPlantSettings().UnplantRadius, 42.0f);
	TestEqual(TEXT("Turn bucket uses captured ownership without resolving a live pawn"), Anim->GetThreadSafeStateControllerTurnInPlaceIndex(), 3.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJInteractionSelectionTest, "ProjectJ.Internal.Interaction.ServerSelection", Flags)
bool FProjectJInteractionSelectionTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Interactor = Scope.World->SpawnActor<ACharacter>();
	const auto MakeTarget = [&](float Distance, int32 Priority)
	{
		auto* Target = Scope.World->SpawnActor<AProject_JInternalInteractionTarget>();
		auto* Sphere = NewObject<USphereComponent>(Target);
		Target->SetRootComponent(Sphere); Sphere->SetSphereRadius(20);
		Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly); Sphere->SetCollisionObjectType(ECC_WorldDynamic);
		Sphere->SetCollisionResponseToAllChannels(ECR_Overlap); Sphere->RegisterComponent();
		Target->SetActorLocation(FVector(Distance, 0, 0));
		auto* Policy = NewObject<UProject_JInteractionTargetComponent>(Target); Policy->RegisterComponent();
		Policy->bRequireLineOfSight = false; Policy->Priority = Priority;
		return Target;
	};
	auto* Near = MakeTarget(100, 0);
	auto* Preferred = MakeTarget(200, 10);
	Scope.World->InitializeActorsForPlay(FURL());
	Scope.World->GetWorldSettings()->NotifyBeginPlay();
	TestTrue(TEXT("Dynamic world actors participate in server interaction"), Project_J::Interaction::TryInteract(*Interactor));
	TestEqual(TEXT("Authored priority beats proximity"), Preferred->Interactions, 1);
	TestEqual(TEXT("Lower priority was not activated"), Near->Interactions, 0);
	auto* Policy = Preferred->FindComponentByClass<UProject_JInteractionTargetComponent>();
	Policy->bEnabled = false;
	TestTrue(TEXT("Disabled target falls back to eligible target"), Project_J::Interaction::TryInteract(*Interactor));
	TestEqual(TEXT("Near target now receives the action"), Near->Interactions, 1);
	Policy->bEnabled = true; Policy->InteractionRange = 50;
	TestFalse(TEXT("Target-specific range is enforced"), Project_J::Interaction::IsEligible(*Interactor, Preferred, 300));
	Policy->InteractionRange = std::numeric_limits<float>::quiet_NaN();
	TestFalse(TEXT("Invalid authored range fails closed"), Project_J::Interaction::IsEligible(*Interactor, Preferred, 300));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FProjectJCommandReleaseTest, "ProjectJ.Internal.Input.CommandAliasRelease", Flags)
bool FProjectJCommandReleaseTest::RunTest(const FString&)
{
	FInternalTestWorld Scope;
	auto* Player = Scope.World->SpawnActor<AProject_JGreatswordCharacter>();
	auto* ASC = NewObject<UProject_JAbilitySystemComponent>(Player); ASC->RegisterComponent();
	FObjectPropertyBase* ASCProperty = FindFProperty<FObjectPropertyBase>(AProject_JBaseCharacter::StaticClass(), TEXT("AbilitySystemComponent"));
	ASCProperty->SetObjectPropertyValue_InContainer(Player, ASC); ASC->InitAbilityActorInfo(Player, Player);
	auto* Input = Player->FindComponentByClass<UProject_JSkillInputExecutionComponent>(); Input->Initialize(Player);
	const auto& Tags = FProject_JGameplayTags::Get();
	const FGameplayTag Raw = Tags.InputTag_Weapon_LMB, Alias = Tags.InputTag_Weapon_RMB;
	auto* Style = NewObject<UProject_JCombatStyleDefinition>();
	auto* Commands = NewObject<UProject_JCombatCommandSet>(); Style->CommandSet = Commands;
	FProject_JCombatCommandDefinition Command;
	Command.CommandTag = Alias; Command.OrderedInputSequence.Add(Raw); Command.ResultInputTag = Alias;
	Commands->Commands.Add(Command); Player->SetCurrentCombatStyle(Style);
	FGameplayAbilitySpec Spec(UProject_JInternalInputAbility::StaticClass()); Spec.GetDynamicSpecSourceTags().AddTag(Alias);
	const FGameplayAbilitySpecHandle Handle = ASC->GiveAbility(Spec);
	Input->HandleInputTagPressed(Raw);
	TestTrue(TEXT("Raw input activates the command alias"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
	// A style change during the hold must not change which tag gets released.
	Player->SetCurrentCombatStyle(nullptr);
	Input->HandleInputTagReleased(Raw);
	TestFalse(TEXT("Raw release clears the original alias even after a style change"), ASC->FindAbilitySpecFromHandle(Handle)->InputPressed);
	ASC->CancelAllAbilities(); ASC->ClearAllAbilities();
	return true;
}
#endif
