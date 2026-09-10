#include "Testing/Project_JCombatCookedFixture.h"
#include "AIController.h"
#include "Animation/AnimInstance.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JCombatPresentationComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JSkillInputExecutionComponent.h"
#include "Components/Project_JWeaponPresentationComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JGameplayTags.h"
#include "Project_JGreatswordCharacter.h"
#include "Project_JPlayerState.h"
#include "NiagaraComponent.h"
#include "PipelineStateCache.h"
#include "PSOPrecacheValidation.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectIterator.h"
#include "UnrealClient.h"
#include "ProfilingDebugging/MiscTrace.h"

void UProject_JCombatCookedFixture::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if !UE_BUILD_SHIPPING
	if (!FParse::Param(FCommandLine::Get(), TEXT("ProjectJCombatCookedFixture"))) { return; }
	Started = PhaseStarted = FPlatformTime::Seconds();
	FParse::Value(FCommandLine::Get(), TEXT("ProjectJEOutput="), Output);
	if (Output.IsEmpty()) { Output = FPaths::ProjectSavedDir() / TEXT("Validation/CombatCooked"); }
	IFileManager::Get().MakeDirectory(*Output, true);
	Handle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::Tick));
#endif
}

bool UProject_JCombatCookedFixture::CreateCharacter()
{
	auto* World = GetWorld();
	const double LoadStart = FPlatformTime::Seconds();
	auto* VisualClass = LoadClass<AProject_JPlayerCharacter>(nullptr, TEXT("/Game/Character_BPs/GreatSword/BP_GreatSword.BP_GreatSword_C"));
	Item = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("visual_and_equipment_load_ms=%.6f\ncooked=%d\n"), (FPlatformTime::Seconds() - LoadStart) * 1000, FPlatformProperties::RequiresCookedData()), *(Output / TEXT("load.txt")));
	if (!VisualClass || !Item || !Item->CombatStyleDefinition || !Item->WeaponPresentationProfile) { return false; }
	const auto* Template = VisualClass->GetDefaultObject<AProject_JPlayerCharacter>();
	FActorSpawnParameters Params;
	Params.ObjectFlags |= RF_Transient;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Controller = World->SpawnActor<AAIController>(Params);
	State = World->SpawnActor<AProject_JPlayerState>(Params);
	Params.bDeferConstruction = true;
	Character = World->SpawnActor<AProject_JGreatswordCharacter>(AProject_JGreatswordCharacter::StaticClass(), FVector(0, 0, 100), FRotator::ZeroRotator, Params);
	if (!Controller || !State || !Character) { return false; }
	for (const FName Name : {FName(TEXT("CharacterAnimProfile")), FName(TEXT("CharacterClassDefinition"))})
	{
		auto* Property = FindFProperty<FObjectPropertyBase>(AProject_JPlayerCharacter::StaticClass(), Name);
		if (!Property) { return false; }
		Property->SetObjectPropertyValue_InContainer(Character, Property->GetObjectPropertyValue_InContainer(Template));
	}
	auto* Mesh = Character->GetMesh();
	Mesh->SetSkeletalMesh(Template->GetMesh()->GetSkeletalMeshAsset());
	Mesh->SetRelativeTransform(Template->GetMesh()->GetRelativeTransform());
	Mesh->SetAnimInstanceClass(Template->GetMesh()->GetAnimClass());
	Mesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	Character->SetActorEnableCollision(false);
	State->SetOwner(Controller);
	Controller->SetPlayerState(State);
	Character->SetPlayerState(State);
	Character->FinishSpawning(FTransform(FVector(0, 0, 100)));
	Controller->Possess(Character);
	Character->GetCharacterMovement()->DisableMovement();
	auto* ASC = Character->GetAbilitySystemComponent();
	if (!ASC || ASC != State->GetAbilitySystemComponent() || !Mesh->GetAnimInstance()) { return false; }
	State->GetEquipmentManagerComponent()->UnequipSlot(EProject_JEquipmentSlot::Weapon);
	BaseAbilities = ASC->GetActivatableAbilities().Num();
	World->SpawnActor<ADirectionalLight>(FVector(0, 0, 500), FRotator(-35, -45, 0));
	const FVector CameraPosition(350, -500, 250);
	auto* Camera = World->SpawnActor<ACameraActor>(CameraPosition, (FVector(40, 0, 100) - CameraPosition).Rotation());
	World->GetFirstPlayerController()->SetViewTarget(Camera);
	return true;
}

bool UProject_JCombatCookedFixture::Tick(float Delta)
{
	if (bFinished) { return false; }
	const double Now = FPlatformTime::Seconds();
	if (Now - Started > 120 || (Phase > 0 && Now - PhaseStarted > 20)) { Finish(false, TEXT("Phase timeout")); return false; }
	auto* World = GetWorld();
	if (!World || !World->HasBegunPlay() || !World->GetFirstPlayerController()) { return true; }
	if (World->GetNetMode() != NM_Standalone) { Finish(false, TEXT("Standalone only")); return false; }
	if (Phase == 0)
	{
		if (Now - Started < 2) { return true; }
		if (!CreateCharacter()) { Finish(false, TEXT("Authored visual/equipment/ASC setup failed")); return false; }
		Phase = 1; PhaseStarted = FPlatformTime::Seconds();
	}
	auto* ASC = CastChecked<UProject_JAbilitySystemComponent>(Character->GetAbilitySystemComponent());
	auto* Equipment = State->GetEquipmentManagerComponent();
	auto* Weapon = Character->FindComponentByClass<UProject_JWeaponPresentationComponent>();
	auto* Presentation = Character->FindComponentByClass<UProject_JCombatPresentationComponent>();
	auto* Hit = Character->FindComponentByClass<UProject_JCombatHitValidationComponent>();
	const bool bMontage = Character->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying();
	int32 ActiveEffects = 0;
	// Diagnostic only. Do not put this global object inspection in production ticks.
	for (TObjectIterator<UNiagaraComponent> It; It; ++It)
	{
		if (It->GetWorld() == World && It->IsActive() && !It->IsComplete()) { ++ActiveEffects; }
	}
	const bool bHitWindow = Hit->GetActiveAttackDefinition() != nullptr;
	Rows += FString::Printf(TEXT("%d,%d,%.6f,%.6f,%d,%d,%d,%u,%d,%d,%d,%d\n"), Cycle, Phase, Now - Started, Delta * 1000, bMontage, ActiveEffects, bHitWindow, PipelineStateCache::NumActivePrecacheRequests(), Equipment->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) != nullptr, IsValid(Weapon->GetSpawnedWeapon()), ASC->GetActivatableAbilities().Num(), BaseAbilities);
	if (Phase == 1 && Now - PhaseStarted > (Cycle == 0 ? 2 : .5))
	{
		++Cycle;
		Character->SetActorLocation(FVector(0, 0, 100), false, nullptr, ETeleportType::TeleportPhysics);
		Equipment->EquipItem(Item);
		ASC->AddLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
		Weapon->AttachWeaponToDrawnSocket();
		Phase = 2; PhaseStarted = FPlatformTime::Seconds();
	}
	else if (Phase == 2 && IsValid(Weapon->GetSpawnedWeapon()))
	{
		auto* Input = Character->FindComponentByClass<UProject_JSkillInputExecutionComponent>();
		Input->ClearCommandInputHistory();
		TRACE_BOOKMARK(TEXT("ProjectJ.FullSkill Begin Cycle=%d"), Cycle);
		Input->HandleInputTagPressed(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
		ASC->AbilityInputTagReleased(FProject_JGameplayTags::Get().InputTag_Weapon_LMB);
		if (!ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking)
			|| !Character->GetMesh()->GetAnimInstance()->IsAnyMontagePlaying() || !Presentation->GetActiveAttackTag().IsValid())
		{ Finish(false, TEXT("Input did not start GAS/montage/presentation")); return false; }
		bSawTrail = bSawHitWindow = bShot = false;
		TrailStarted = 0;
		Phase = 3; PhaseStarted = FPlatformTime::Seconds();
	}
	else if (Phase == 3)
	{
		bSawTrail |= ActiveEffects > 0; bSawHitWindow |= bHitWindow;
		if (ActiveEffects > 0 && TrailStarted == 0) { TrailStarted = Now; }
		if (ActiveEffects > 0 && !bShot && Now - TrailStarted >= .1)
		{
			bShot = true; FScreenshotRequest::RequestScreenshot(Output / FString::Printf(TEXT("attack%d.png"), Cycle), false, false);
		}
		if (!bMontage && !ASC->HasMatchingGameplayTag(FProject_JGameplayTags::Get().State_Attacking) && !Presentation->GetActiveAttackTag().IsValid())
		{
			if (!bSawTrail || !bSawHitWindow) { Finish(false, TEXT("Attack did not exercise Niagara and hit-validation window")); return false; }
			Equipment->UnequipSlot(EProject_JEquipmentSlot::Weapon);
			ASC->RemoveLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
			Phase = 4; PhaseStarted = FPlatformTime::Seconds();
		}
	}
	else if (Phase == 4 && Now - PhaseStarted > .25)
	{
		if (Equipment->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) || IsValid(Weapon->GetSpawnedWeapon()) || bMontage
			|| Presentation->GetActiveAttackTag().IsValid() || bHitWindow || ActiveEffects || ASC->GetActivatableAbilities().Num() != BaseAbilities)
		{ return true; } // Asset-authored fade and ability removal may complete on later frames; the bounded phase timeout still fails leaks.
		if (Cycle == 3) { Finish(true, TEXT("Three real input/GAS/montage/notify/unequip cycles; native character with authored visuals")); return false; }
		Phase = 1; PhaseStarted = FPlatformTime::Seconds();
	}
	return true;
}

void UProject_JCombatCookedFixture::Finish(bool bSuccess, const TCHAR* Reason)
{
	if (bFinished) { return; } bFinished = true;
#if PSO_PRECACHING_VALIDATE
	PSOCollectorStats::DumpPSOPrecacheValidationStats();
#endif
	FFileHelper::SaveStringToFile(Rows, *(Output / TEXT("frames.csv")));
	FFileHelper::SaveStringToFile(FString::Printf(TEXT("success=%d\ncooked=%d\ncycles=%d\nreason=%s\n"), bSuccess, FPlatformProperties::RequiresCookedData(), Cycle, Reason), *(Output / TEXT("result.txt")));
	if (IsValid(Character)) { Character->Destroy(); }
	if (IsValid(State)) { State->Destroy(); }
	if (IsValid(Controller)) { Controller->Destroy(); }
	FPlatformMisc::RequestExit(false);
}
void UProject_JCombatCookedFixture::Deinitialize()
{
	FTSTicker::GetCoreTicker().RemoveTicker(Handle);
	Super::Deinitialize();
}
