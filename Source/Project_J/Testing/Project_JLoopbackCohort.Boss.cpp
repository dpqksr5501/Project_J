#include "Testing/Project_JLoopbackCohort.h"

#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Components/CapsuleComponent.h"
#include "Components/Project_JCombatHitValidationComponent.h"
#include "Components/Project_JEquipmentManagerComponent.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Engine/NetConnection.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "GameFramework/GameNetworkManager.h"
#include "Game/Project_JPlayerState.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Movement/Project_JMovementQueries.h"
#include "Movement/Project_JMoverComponent.h"
#include "MoverDataModelTypes.h"
#include "Project_JAbilitySystemComponent.h"
#include "Project_JAttributeSet.h"
#include "Project_JGameplayTags.h"
#include "Project_JPlayerCharacter.h"
#include "World/Project_JWorldEncounter.h"

namespace
{
const TCHAR* BossClassPath = TEXT("/Game/DataAssetSets/MMO/BP_FieldBoss_Test.BP_FieldBoss_Test_C");
const TCHAR* AvatarClassPath = TEXT("/Game/Character_BPs/Spellblade/BP_Spellblade.BP_Spellblade_C");

bool FindBossCohortGround(const UWorld& World, const FVector& Origin, FVector& Ground)
{
	FHitResult Hit;
	if (!World.LineTraceSingleByObjectType(Hit, Origin + FVector(0, 0, 500), Origin - FVector(0, 0, 500),
		FCollisionObjectQueryParams(ECC_WorldStatic))) return false;
	Ground = Hit.ImpactPoint;
	return true;
}

FRotator GetCohortTargetView(const APawn& Player, const AActor& Target)
{
	return FRotator(0., (Target.GetActorLocation() - Player.GetActorLocation()).Rotation().Yaw, 0.);
}
}

bool AProject_JLoopbackCohort::PrepareBossCombat()
{
	bStableBossLayout = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortStableBossLayout"));
	bBossHitDiagnostics = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortBossHitDiagnostics"));
	if (bStableBossLayout)
	{
		if (Expected < 32 || Expected > 64) return false;
		UE_LOG(LogTemp, Display, TEXT("COHORT_BOSS_LAYOUT anchor=32 participants=%d"), Expected);
	}
	if (bOwnerOnlyPeers && !GetDefault<AGameNetworkManager>()->bUseDistanceBasedRelevancy)
	{
		UE_LOG(LogTemp, Error, TEXT("The owner-only boss fixture requires distance-based pawn relevancy for Generic multicasts."));
		return false;
	}
	const bool bDungeonArena = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortDungeonCombat"));
	const bool bFieldArena = FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortFieldArena"));
	if (bDungeonArena && bFieldArena) return false;
	AProject_JWorldEncounter* PlacedEncounter = nullptr;
	const FName EncounterId = bDungeonArena ? FName(TEXT("Dungeon_Starter_Boss")) : FName(TEXT("Sandbox_FieldBoss_01"));
	if (bDungeonArena || bFieldArena)
	{
		if (!GetWorld()->GetMapName().EndsWith(bDungeonArena ? TEXT("Test_MMO_Dungeon") : TEXT("Test_MMO_Sandbox"))) return false;
		for (TActorIterator<AProject_JWorldEncounter> It(GetWorld()); It; ++It)
		{
			if (It->EncounterId != EncounterId) continue;
			if (PlacedEncounter || (bDungeonArena && (!It->bTriggerDungeonClearOnDefeat || !It->DungeonDefinition))) return false;
			PlacedEncounter = *It;
		}
		if (!PlacedEncounter) return false;
	}
	const auto BossClass = PlacedEncounter ? PlacedEncounter->NPCClass.LoadSynchronous() :
		LoadClass<AProject_JNPCCharacter>(nullptr, BossClassPath);
	BossPlayerClass = LoadClass<AProject_JPlayerCharacter>(nullptr, AvatarClassPath);
	if (!BossClass || !BossPlayerClass) return false;
	FVector Ground;
	const FVector Arena = PlacedEncounter ? PlacedEncounter->GetActorLocation() : FVector(3850, 0, 100);
	if (!FindBossCohortGround(*GetWorld(), Arena, Ground)) return false;
	if (PlacedEncounter)
	{
		// The timing cohort owns the target in this isolated server world.
		// Use the placed encounter's class and arena without spawning a second boss.
		if (!PlacedEncounter->Destroy()) return false;
		UE_LOG(LogTemp, Display, TEXT("COHORT_%s_ARENA encounter=%s map=%s boss=%s"),
			bDungeonArena ? TEXT("DUNGEON") : TEXT("FIELD"), *EncounterId.ToString(), *GetWorld()->GetMapName(), *GetNameSafe(BossClass));
	}
	const float HalfHeight = BossClass->GetDefaultObject<AProject_JNPCCharacter>()->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	FActorSpawnParameters Spawn;
	Spawn.ObjectFlags |= RF_Transient;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	Boss = GetWorld()->SpawnActor<AProject_JNPCCharacter>(BossClass, Ground + FVector(0, 0, HalfHeight + 2), FRotator::ZeroRotator, Spawn);
	if (!Boss || !Boss->GetAbilitySystemComponent()) return false;
	Boss->bAlwaysRelevant = true;
	BossStartingHealth = 25000.f;
	auto* Attributes = Boss->GetAttributeSet();
	if (!Attributes || !Attributes->CanBeginHealthGEObservation()) return false;
	BossObservationFailures = Attributes->GetHealthGEObservationFailures();
	BossDamageHandle = Attributes->OnConfirmedHealthGELoss.AddUObject(this, &ThisClass::ObserveBossDamage);
	return true;
}

bool AProject_JLoopbackCohort::SpawnBossParticipant(AProject_JCohortController& Player, int32 Slot)
{
	auto* State = Player.GetPlayerState<AProject_JPlayerState>();
	auto* Equipment = State ? State->GetEquipmentManagerComponent() : nullptr;
	if (!Boss || !Equipment || Slot >= Expected) return false;

	// The cohort owns real pawns. Ignore overlap among the tightly spaced test melee lanes.
	float Angle = Slot * (2.f * UE_PI / Expected);
	if (bStableBossLayout)
	{
		// Keep the 32 control transforms and progressively fill their gaps around the ring.
		constexpr int32 GapsWithinQuadrant[] = {0, 4, 2, 6, 1, 5, 3, 7};
		const int32 Extra = Slot - 32;
		const float Position = Extra < 0 ? Slot : (Extra % 4) * 8 + GapsWithinQuadrant[Extra / 4] + .5f;
		Angle = Position * (2.f * UE_PI / 32);
	}
	const FVector Outward(FMath::Cos(Angle), FMath::Sin(Angle), 0);
	const float Distance = Boss->GetCapsuleComponent()->GetScaledCapsuleRadius() + 65.f;
	FVector Ground;
	if (!FindBossCohortGround(*GetWorld(), Boss->GetActorLocation() + Outward * Distance, Ground)) return false;
	const auto* DefaultAvatar = BossPlayerClass->GetDefaultObject<AProject_JPlayerCharacter>();
	const float HalfHeight = DefaultAvatar->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const FTransform SpawnPose((-Outward).Rotation(), Ground + FVector(0, 0, HalfHeight + 2));
	auto* Avatar = GetWorld()->SpawnActorDeferred<AProject_JPlayerCharacter>(BossPlayerClass, SpawnPose, &Player,
		nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Avatar) return false;
	Avatar->bOnlyRelevantToOwner = bOwnerOnlyPeers;
	Avatar->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	if (bOwnerOnlyPeers)
	{
		// Generic multicasts call APawn::IsNetRelevantFor directly. Its owner test precedes distance culling.
		Avatar->bAlwaysRelevant = false;
		Avatar->SetNetCullDistanceSquared(0.f);
		State->bAlwaysRelevant = false;
		State->bOnlyRelevantToOwner = true;
	}
	Avatar->FinishSpawning(SpawnPose);
	Player.Possess(Avatar);
	State->SetPublicCharacterSnapshot(TEXT("Spellblade"), 1);
	State->SetSocialState(TEXT("Cohort.PvE"), NAME_None, FGuid(), FGuid());
	// Possession installs the avatar's configured starting equipment. Use its
	// normal attack grants so load tests follow changes to the production loadout.
	auto* Weapon = Equipment->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon);
	auto* ASC = Cast<UProject_JAbilitySystemComponent>(Avatar->GetAbilitySystemComponent());
	if (!IsValid(Avatar) || Player.GetPawn() != Avatar || !ASC || !IsValid(Weapon)) return false;
	if (Slot == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("COHORT_BOSS_LOADOUT avatar=%s weapon=%s"),
			*GetNameSafe(Avatar->GetClass()), *GetNameSafe(Weapon));
	}

	// Start with weapons drawn, as in the canonical network combat fixture.
	ASC->AddProjectJLooseGameplayTag(FProject_JGameplayTags::Get().State_CombatMode);
	return true;
}

bool AProject_JLoopbackCohort::CheckBossMulticastAudience(const UNetDriver& Driver) const
{
	for (const auto& Connection : Driver.ClientConnections)
	{
		if (!Connection || !Connection->ViewTarget) return false;
		const FNetViewer Viewer(Connection, 0.f);
		for (const auto& Entry : Participants)
		{
			const auto* ParticipantController = Entry.Key.Get();
			if (ParticipantController == Connection->PlayerController) continue;
			const APawn* Pawn = ParticipantController ? ParticipantController->GetPawn() : nullptr;
			if (!Pawn || Pawn->IsNetRelevantFor(Viewer.InViewer, Viewer.ViewTarget, Viewer.ViewLocation))
				return false;
		}
	}
	return true;
}

int32 AProject_JLoopbackCohort::CountBossCombatAimedParticipants() const
{
	if (!IsValid(Boss)) return 0;
	int32 Aimed = 0;
	for (const auto& [Controller, Participant] : Participants)
	{
		const auto* Player = IsValid(Controller.Get()) ? Cast<AProject_JPlayerCharacter>(Controller->GetPawn()) : nullptr;
		const auto* Mover = Player ? Player->GetMover() : nullptr;
		const auto* Input = Mover ? Mover->GetLastInputCmd().InputCollection.FindDataByType<FCharacterDefaultInputs>() : nullptr;
		if (Participant.bBossReady && Input && !Input->ControlRotation.ContainsNaN() &&
			FMath::Abs(FMath::FindDeltaAngleDegrees(Input->ControlRotation.Yaw, GetCohortTargetView(*Player, *Boss).Yaw)) <= 5.)
		{
			++Aimed;
		}
	}
	return Aimed;
}

void AProject_JLoopbackCohort::ObserveBossDamage(const FProject_JConfirmedHealthGELoss& Loss)
{
	const auto* Avatar = Cast<AProject_JPlayerCharacter>(Loss.Proof.SourceAvatar.Get());
	auto* Player = Avatar ? Cast<AProject_JCohortController>(Avatar->GetController()) : nullptr;
	auto* Participant = Participants.Find(Player);
	if (!Boss || Loss.Proof.TargetAvatar.Get() != Boss || !Participant || !Player->GetNetConnection()
		|| Avatar != Player->GetPawn() || Loss.Proof.SourceASC.Get() != Avatar->GetAbilitySystemComponent()
		|| !Participant->SentAt.IsValidIndex(Participant->Received) || Loss.NewValue >= Loss.OldValue)
	{
		++StrikeViolations;
		return;
	}
	const double ConfirmedAt = FPlatformTime::Seconds();
	const double SentAt = Participant->SentAt[Participant->Received];
	StrikeLatenciesMs.Add((ConfirmedAt - SentAt) * 1000.);
	Participant->BossStrikeIndex = BossStrikeTimings.AddDefaulted();
	auto& Timing = BossStrikeTimings.Last();
	Timing.ConfirmedAt = ConfirmedAt;
	Timing.ConfirmedHealth = Loss.NewValue;
	const auto* Validation = Avatar->FindComponentByClass<UProject_JCombatHitValidationComponent>();
	const double HitWindowAt = Validation ? Validation->ReadHitWindowTelemetry() : 0.;
	if (HitWindowAt >= SentAt && HitWindowAt <= ConfirmedAt)
	{
		Timing.HitWindowMs = (HitWindowAt - SentAt) * 1000.;
		Timing.AuthorityMs = (ConfirmedAt - HitWindowAt) * 1000.;
	}
	ConfirmedBossDamage += Loss.OldValue - Loss.NewValue;
	++Participant->Received;
	++MeasuredStrikes;
}

void AProject_JCohortController::PollBossReadiness()
{
	if (!bBossCombatPrepared || bBossCombatReady) return;
	auto* Avatar = Cast<AProject_JPlayerCharacter>(GetPawn());
	const auto* State = GetPlayerState<AProject_JPlayerState>();
	const auto* Equipment = State ? State->GetEquipmentManagerComponent() : nullptr;
	const auto* Weapon = Equipment ? Equipment->GetEquippedItemInSlot(EProject_JEquipmentSlot::Weapon) : nullptr;
	if (Avatar && Avatar->GetCapsuleComponent()->GetCollisionResponseToChannel(ECC_Pawn) != ECR_Ignore)
		Avatar->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	const auto Physical = Project_J::Movement::ReadPhysicalFrame(Avatar);
	const bool bOwnsAvatar = Avatar && Avatar->GetLocalRole() == ROLE_AutonomousProxy && Avatar->IsLocallyControlled();
	if (!bBossViewAssigned && bOwnsAvatar && ObservedBoss.IsValid())
	{
		// Feed ordinary owner camera input. Spawning a server pawn facing the
		// target does not initialize its remote player's camera rotation.
		SetControlRotation(GetCohortTargetView(*Avatar, *ObservedBoss));
		bBossViewAssigned = true;
	}
	const auto* ASC = Avatar ? Cast<UProject_JAbilitySystemComponent>(Avatar->GetAbilitySystemComponent()) : nullptr;
	const bool bHasASC = ASC && ASC->GetAvatarActor() == Avatar && ASC->AbilityActorInfo.IsValid()
		&& ASC->AbilityActorInfo->PlayerController.Get() == this && ASC->AbilityActorInfo->IsLocallyControlled();
	const bool bHasWeapon = IsValid(Weapon);
	const bool bHasRouter = Avatar && Avatar->FindComponentByClass<UProject_JSkillInputRouterComponent>();
	const auto& CombatTags = FProject_JGameplayTags::Get();
	const bool bCanAttack = bHasASC && ASC->HasMatchingGameplayTag(CombatTags.State_CombatMode)
		&& ASC->GetActivatableAbilities().ContainsByPredicate([&](const FGameplayAbilitySpec& Spec)
		{
			// A style can bind its melee grant to a resolved command instead of the raw mouse tag.
			// The burst still drives the normal router and requires confirmed damage from this player.
			return !Spec.PendingRemove && Spec.Ability && Spec.Ability->IsA<UProject_JGameplayAbility_Melee>()
				&& Spec.Ability->CanActivateAbility(Spec.Handle, ASC->AbilityActorInfo.Get());
		});
	const uint32 Blocker = (!ObservedBoss.IsValid() << 0) | (!bOwnsAvatar << 1) | (!bHasASC << 2)
		| (!bHasWeapon << 3) | (!bHasRouter << 4) | ((!Physical.bValid || !Physical.bGrounded) << 5) | (!bCanAttack << 6);
	if (Blocker)
	{
		if (Blocker != LastBossBlocker)
			UE_LOG(LogTemp, Display, TEXT("COHORT_BOSS_WAIT world=%s key=%d blocker=%u boss=%d owns=%d asc=%d weapon=%s router=%d grounded=%d melee=%d pos=%s"),
				*GetWorld()->GetPackage()->GetName(), State ? State->GetPlayerId() : -1, Blocker, ObservedBoss.IsValid(), bOwnsAvatar, bHasASC,
				*GetNameSafe(Weapon), bHasRouter, Physical.bGrounded, bCanAttack, Avatar ? *Avatar->GetActorLocation().ToCompactString() : TEXT("none"));
		LastBossBlocker = Blocker;
		return;
	}
	auto* BossASC = ObservedBoss->GetAbilitySystemComponent();
	if (!BossASC || !BossASC->HasAttributeSetForAttribute(UProject_JAttributeSet::GetHealthAttribute())) return;
	const TWeakObjectPtr<AProject_JNPCCharacter> Target = ObservedBoss;
	BossASC->GetGameplayAttributeValueChangeDelegate(UProject_JAttributeSet::GetHealthAttribute()).AddWeakLambda(this,
		[this, Target](const FOnAttributeChangeData& Change)
		{
			// Cohort owners never locally predict another actor's Health. This
			// delegate is driven by the replicated GAS attribute on the client.
			if (ObservedBoss != Target || Change.NewValue >= Change.OldValue || BossHealthObservations.Num() >= 512) return;
			auto& Sample = BossHealthObservations.AddDefaulted_GetRef();
			Sample.Health = Change.NewValue;
			Sample.ObservedAt = FPlatformTime::Seconds();
		});
	bBossCombatReady = true;
	ServerBossCombatReady();
}

void AProject_JCohortController::PressBossCombatAttack()
{
	if (!bBossCombatReady || !ObservedBoss.IsValid()) return;
	if (const APawn* Avatar = GetPawn())
	{
		if (auto* Router = Avatar->FindComponentByClass<UProject_JSkillInputRouterComponent>())
		{
			Router->HandleButtonPressed(EProject_JSkillInputButton::LMB);
			Router->HandleButtonReleased(EProject_JSkillInputButton::LMB);
		}
	}
}

bool AProject_JCohortController::HasExpectedBossAudience() const
{
	if (!bBossCombatPrepared || !FParse::Param(FCommandLine::Get(), TEXT("ProjectJCohortOwnerOnlyPeers"))) return true;
	for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (*It != GetPawn())
		{
			UE_LOG(LogTemp, Error, TEXT("An owner-only boss client received a remote participant pawn: %s"), *It->GetName());
			return false;
		}
	}
	return true;
}
#else
void AProject_JCohortController::PollBossReadiness() {}
bool AProject_JCohortController::HasExpectedBossAudience() const { return true; }
#endif

void AProject_JLoopbackCohort::AcknowledgeBossReady(AProject_JCohortController* Player)
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	auto* Participant = Participants.Find(Player);
	if (!bBossCombat || CombatPhase != ECombatPhase::Idle || !Participant || Participant->bBossReady || !Player->GetPawn())
	{
		++StrikeViolations;
		return;
	}
	Participant->bBossReady = true;
	++BossReadyPlayers;
#endif
}

void AProject_JCohortController::ClientPrepareBossCombat_Implementation(AProject_JNPCCharacter* Target)
{
	if (Target) ObservedBoss = Target;
	bBossCombatPrepared = true;
}

void AProject_JCohortController::ServerBossCombatReady_Implementation()
{
#if WITH_EDITOR && WITH_DEV_AUTOMATION_TESTS
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JLoopbackCohort>()) Mode->AcknowledgeBossReady(this);
#endif
}
