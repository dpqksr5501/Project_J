// Explicit companion audience extension of the existing real-socket fixture.
// Public object IDs are metadata only: no foreign ActiveCompanion reference is
// transported by fixture RPC and no private production property is rewritten.
namespace
{
uint64 CompanionPublicId(const AActor* Actor)
{
	const auto* Bridge = Actor ? UE::Net::FReplicationSystemUtil::GetActorReplicationBridge(Actor) : nullptr;
	return Bridge ? Bridge->GetReplicatedRefHandle(Actor).GetId() : 0;
}
bool CompanionExpected(int32 Phase, int32 Slot)
{
	return Phase == 9 ? Slot == 0 : (Phase == 10 || Phase == 11 || Phase == 13) || (Phase == 12 && Slot == 1);
}
bool CompanionHasNoPlayerConnection(AProject_JNPCCharacter* NPC)
{
	const auto* ASC = NPC ? NPC->GetAbilitySystemComponent() : nullptr;
	return NPC && !NPC->GetNetConnection() && !NPC->GetNetOwningPlayer() && !Cast<APlayerController>(NPC->GetController()) &&
		ASC && ASC->AbilityActorInfo.IsValid() && ASC->GetOwnerActor() == NPC && ASC->GetAvatarActor() == NPC &&
		!ASC->AbilityActorInfo->PlayerController.IsValid();
}
}
void AProject_JNetworkLoadController::ClientCompanionStage_Implementation(int32 Phase, FGuid OwnItem, int32 Action,
	bool bExpectationReady, const TArray<uint64>& PublicIds, const TArray<float>& PublicHealth)
{
	if (!CompanionAudienceEnabled() || Phase < 8 || Phase > 14 || Action < 0 || Action > 2 ||
		(bExpectationReady && (PublicIds.Num() != 2 || PublicHealth.Num() != 8))) return;
	if (CurrentStage != Phase)
	{
		CurrentStage = Phase; bCompanionExpectReady = false; CompanionStableSince = 0; NextObservation = 0;
	}
	CompanionItem = OwnItem;
	if (Action) CompanionAction = Action;
	if (bExpectationReady)
	{
		CompanionExpectedIds = PublicIds; CompanionExpectedHealth = PublicHealth; bCompanionExpectReady = true;
	}
}
void AProject_JNetworkLoadController::ObserveCompanion()
{
	const double Now = FPlatformTime::Seconds();
	if (Now < NextObservation) return;
	NextObservation = Now + .25;
	auto* OwnState = GetPlayerState<AProject_JPlayerState>();
	if (!OwnState || !OwnState->GetCompanionComponent()) return;
	if (!CompanionModel)
	{
		CompanionModel = NewObject<UProject_JInventoryViewModel>(this);
		CompanionModel->Bind(OwnState->GetInventoryComponent(), OwnState->GetEquipmentManagerComponent());
	}
	FProject_JItemInstanceData Item;
	auto* Data = GetGameInstance()->GetSubsystem<UProject_JCharacterDataSubsystem>();
	const bool bHasItem = OwnState->GetInventoryComponent()->FindItemInstance(CompanionItem, Item);
	const auto* Definition = bHasItem ? Cast<UProject_JCompanionItemDefinition>(Item.ItemDef) : nullptr;
	const bool bCanonical = Definition && Data && Data->GetItemDefinition(Definition->ItemId, Definition->SchemaVersion) == Definition &&
		Definition->IsFollowDefinitionValid() && Item.StackCount == 1 && !Item.bIsLocked && !Item.bIsEquipped;
	if (CompanionAction && bCanonical)
	{
		if (CompanionAction == 2)
		{
			OwnState->GetCompanionComponent()->RequestCompanion(FGuid{}); // Actual production owner RPC.
			CompanionAction = 0;
		}
		else
		{
			const UProject_JInventoryEntry* Entry = nullptr;
			for (const auto& Candidate : CompanionModel->InventoryEntries)
				if (Candidate && Candidate->Item.InstanceId == CompanionItem) { Entry = Candidate.Get(); break; }
			if (Entry && CompanionModel->Activate(Entry))
			{
				if (CompanionModel->bPending) bCompanionViolation = true;
				CompanionAction = 0;
			}
		}
	}
	if (!bCompanionExpectReady) return;
	bool bReady = bCanonical && CompanionAction == 0 && !bCompanionViolation;
	TArray<AProject_JPlayerState*> States; States.SetNumZeroed(2);
	for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
	{
		const FString PublicId = It->GetPublicClassId().ToString();
		if (PublicId == TEXT("DClient0")) States[0] = *It;
		else if (PublicId == TEXT("DClient1")) States[1] = *It;
	}
	TMap<uint64, AProject_JNPCCharacter*> PublicActors;
	if (Definition)
		for (TActorIterator<AProject_JNPCCharacter> It(GetWorld()); It; ++It)
			if (!It->IsActorBeingDestroyed() && It->GetClass()->GetPathName() == Definition->CompanionClass.ToSoftObjectPath().ToString())
			{
				const uint64 Id = CompanionPublicId(*It);
				if (!Id || PublicActors.Contains(Id)) bReady = false;
				PublicActors.Add(Id, *It);
			}
	const auto* Defaults = GetDefault<UProject_JAttributeSet>();
	FString PendingRows;
	int32 WantedActors = 0;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		const uint64 ExpectedId = CompanionExpectedIds[Slot]; WantedActors += ExpectedId != 0;
		auto* State = States[Slot];
		auto* Pointer = State && State->GetCompanionComponent() ? State->GetCompanionComponent()->GetActiveCompanion() : nullptr;
		const bool bMine = State == OwnState;
		const uint64 PointerId = CompanionPublicId(Pointer);
		if (!bMine && Pointer) bCompanionViolation = true;
		bool bPassed = State && (bMine ? PointerId == ExpectedId : Pointer == nullptr);
		auto* NPC = PublicActors.FindRef(ExpectedId);
		const auto* Stats = NPC ? NPC->GetAttributeSet() : nullptr;
		const bool bNoPlayer = !ExpectedId || CompanionHasNoPlayerConnection(NPC);
		if (NPC && !bNoPlayer) bCompanionViolation = true;
		float Values[8] = {};
		if (ExpectedId)
		{
			bPassed &= Stats && NPC->GetLocalRole() == ROLE_SimulatedProxy;
			if (Stats)
			{
				const FGameplayAttributeData* Attributes[] = {&Stats->MaxHealth, &Stats->Health, &Stats->AttackPower, &Stats->Defense};
				for (int32 I = 0; I < 4; ++I) { Values[I*2] = Attributes[I]->GetBaseValue(); Values[I*2+1] = Attributes[I]->GetCurrentValue(); }
				for (int32 I = 0; I < 4; ++I) bPassed &= FMath::IsNearlyEqual(Values[I], CompanionExpectedHealth[Slot*4+I]);
				const bool bPrivate = FMath::IsNearlyEqual(Values[4], Defaults->AttackPower.GetBaseValue()) &&
					FMath::IsNearlyEqual(Values[5], Defaults->AttackPower.GetCurrentValue()) &&
					FMath::IsNearlyEqual(Values[6], Defaults->Defense.GetBaseValue()) &&
					FMath::IsNearlyEqual(Values[7], Defaults->Defense.GetCurrentValue());
				if (!bPrivate) bCompanionViolation = true;
				bPassed &= bPrivate;
			}
		}
		bPassed &= bNoPlayer; bReady &= bPassed;
		PendingRows += FString::Printf(TEXT("%d,%d,%d,%llu,%llu,%d,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%d,%d\n"),
			CurrentStage, LocalSlot, Slot, ExpectedId, PointerId, bMine || Pointer == nullptr, Values[0], Values[1], Values[2], Values[3],
			Values[4], Values[5], Values[6], Values[7], bNoPlayer, bPassed);
	}
	bReady &= PublicActors.Num() == WantedActors && !bCompanionViolation;
	if (!bReady) CompanionStableSince = 0;
	else if (!CompanionStableSince) CompanionStableSince = Now;
	const bool bStable = bReady && Now - CompanionStableSince >= 1.;
	if (bStable && CompanionRecordedStage != CurrentStage) { CompanionRows += PendingRows; CompanionRecordedStage = CurrentStage; }
	ServerCompanionObserved(CurrentStage, bStable, bCompanionViolation,
		FString::Printf(TEXT("canonical=%d action=%d actors=%d/%d stable=%d violation=%d"), bCanonical, CompanionAction, PublicActors.Num(), WantedActors, bStable, bCompanionViolation));
}
void AProject_JNetworkLoadController::ServerCompanionObserved_Implementation(int32 Phase, bool bReady, bool bViolation, const FString& Detail)
{
	if (!CompanionAudienceEnabled() || Detail.Len() > 192) return;
	if (auto* Mode = GetWorld()->GetAuthGameMode<AProject_JNetworkLoadGameMode>()) Mode->AcceptCompanionObservation(this, Phase, bReady, bViolation, Detail);
}
void AProject_JNetworkLoadGameMode::AcceptCompanionObservation(AProject_JNetworkLoadController* PC, int32 Phase,
	bool bReady, bool bViolation, const FString& Detail)
{
	if (!bFixture || bFinished || !CompanionAudienceEnabled() || !Clients.Contains(PC) || Phase != Stage || !bCompanionPublished) return;
	PC->LastObservation = FString::Printf(TEXT("companion phase=%d %s"), Phase, *Detail);
	if (bViolation) { Finish(false, TEXT("Companion pointer/private attribute/owning-connection audience violation")); return; }
	PC->ConfirmedStage = bReady ? Stage : INDEX_NONE;
}
void AProject_JNetworkLoadGameMode::StartCompanionStage(int32 Phase)
{
	if (Clients.Num() != 2 || CompanionItems.Num() != 2 || Clients.ContainsByPredicate([](const TObjectPtr<AProject_JNetworkLoadController>& PC)
		{ return !IsValid(PC) || !PC->GetPawn() || !PC->GetPlayerState<AProject_JPlayerState>(); }))
	{ Finish(false, TEXT("Companion owner disconnected before the next phase")); return; }
	Stage = Phase; StageStarted = FPlatformTime::Seconds(); bCompanionPublished = false;
	if (CompanionPreviousIds.IsEmpty()) CompanionPreviousIds.SetNumZeroed(2);
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		auto* PC = Clients[Slot].Get(); PC->ConfirmedStage = INDEX_NONE;
		if (Phase == 8)
		{
			auto Teleport = MakeShared<FTeleportEffect>(); Teleport->TargetLocation = FVector(0, Slot*700, 100);
			CastChecked<AProject_JPlayerCharacter>(PC->GetPawn())->GetMover()->QueueInstantMovementEffect(Teleport);
		}
		const int32 Action = ((Phase == 9 || Phase == 13) && Slot == 0) || (Phase == 10 && Slot == 1) ? 1 :
			(Phase == 12 && Slot == 0) || Phase == 14 ? 2 : 0;
		PC->ClientCompanionStage(Phase, CompanionItems[Slot], Action, false, {}, {});
	}
}
bool AProject_JNetworkLoadGameMode::PublishCompanionStage()
{
	if (Clients.Num() != 2 || Clients.ContainsByPredicate([](const TObjectPtr<AProject_JNetworkLoadController>& PC)
		{ return !IsValid(PC) || !PC->GetPawn() || !PC->GetPlayerState<AProject_JPlayerState>(); }))
	{ Finish(false, TEXT("Companion owner disconnected during preparation")); return false; }
	TArray<uint64> PublicIds; PublicIds.SetNumZeroed(2);
	TArray<float> PublicHealth; PublicHealth.SetNumZeroed(8);
	TArray<TStrongObjectPtr<AProject_JNPCCharacter>> Pins;
	// Resolve the complete expected set before changing attributes or updating
	// retired IDs. A slow second preparation cannot partially publish this phase.
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		auto* PS = Clients[Slot]->GetPlayerState<AProject_JPlayerState>();
		auto* NPC = PS->GetCompanionComponent()->GetActiveCompanion();
		const bool bWanted = CompanionExpected(Stage, Slot);
		if ((NPC != nullptr) != bWanted || PS->GetCompanionComponent()->HasPendingPreparation()) return false;
		Pins.Emplace(NPC);
		if (!bWanted) continue;
		const uint64 Id = CompanionPublicId(NPC);
		if (!Id) return false;
		if (Stage == 13 && Slot == 0 && Id == CompanionPreviousIds[0]) { Finish(false, TEXT("Re-summon reused retired public actor identity")); return false; }
		if (NPC->GetClass()->GetPathName() != TEXT("/Game/NPCs/BP_NPC_Test.BP_NPC_Test_C") || !CompanionHasNoPlayerConnection(NPC))
		{ Finish(false, TEXT("Companion uses a wrong authored class or a player owning connection")); return false; }
		UProject_JNPCActionComponent* Follow = nullptr;
		TInlineComponentArray<UProject_JNPCActionComponent*> Actions(NPC);
		for (auto* Action : Actions) if (Action->GetOwnershipToken() && Action->GetIntentTarget() == Clients[Slot]->GetPawn()) { Follow = Action; break; }
		if (!Follow || Follow->GetAttackAbilityHandle().IsValid()) { Finish(false, TEXT("Companion did not retain its non-combat owner follow action")); return false; }
		PublicIds[Slot] = Id;
	}
	FString PendingRows;
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		auto* NPC = Pins[Slot].Get();
		if (!NPC) { PendingRows += FString::Printf(TEXT("%d,%d,0,0,0,0,0,0,0,0,0,,1,1\n"), Stage, Slot); continue; }
		auto* PS = Clients[Slot]->GetPlayerState<AProject_JPlayerState>();
		auto* ASC = NPC->GetAbilitySystemComponent();
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetMaxHealthAttribute(), 2000.f + Slot*100 + Stage);
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetHealthAttribute(), 1200.f + Slot*100 + Stage);
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetAttackPowerAttribute(), 7000.f + Slot*100 + Stage);
		ASC->SetNumericAttributeBase(UProject_JAttributeSet::GetDefenseAttribute(), 8000.f + Slot*100 + Stage);
		if (!IsValid(NPC) || NPC->IsActorBeingDestroyed() || PS->GetCompanionComponent()->GetActiveCompanion() != NPC)
		{ Finish(false, TEXT("Companion retired during authoritative attribute publication")); return false; }
		const auto* Stats = NPC->GetAttributeSet();
		const FGameplayAttributeData* Attributes[] = {&Stats->MaxHealth, &Stats->Health, &Stats->AttackPower, &Stats->Defense};
		float Values[8];
		for (int32 I = 0; I < 4; ++I) { Values[I*2] = Attributes[I]->GetBaseValue(); Values[I*2+1] = Attributes[I]->GetCurrentValue(); }
		for (float Value : Values) if (!FMath::IsFinite(Value)) { Finish(false, TEXT("Non-finite companion attribute publication")); return false; }
		if (Values[2] <= 0 || Values[3] <= 0 || Values[3] > Values[1] ||
			!FMath::IsNearlyEqual(Values[4], 7000.f + Slot*100 + Stage) || !FMath::IsNearlyEqual(Values[6], 8000.f + Slot*100 + Stage) ||
			!PS->GetCharacterId().IsValid()) { Finish(false, TEXT("Companion public/private sentinels or follow target identity are invalid")); return false; }
		for (int32 I = 0; I < 4; ++I) PublicHealth[Slot*4+I] = Values[I];
		PendingRows += FString::Printf(TEXT("%d,%d,%llu,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%.9g,%s,1,1\n"),
			Stage, Slot, PublicIds[Slot], Values[0], Values[1], Values[2], Values[3], Values[4], Values[5], Values[6], Values[7],
			*PS->GetCharacterId().Value.ToString(EGuidFormats::Digits));
		CompanionPreviousIds[Slot] = PublicIds[Slot]; NPC->ForceNetUpdate(); PS->ForceNetUpdate();
	}
	CompanionAuthorityRows += PendingRows; bCompanionPublished = true;
	for (int32 Slot = 0; Slot < 2; ++Slot) Clients[Slot]->ClientCompanionStage(Stage, CompanionItems[Slot], 0, true, PublicIds, PublicHealth);
	return true;
}

void AProject_JNetworkLoadController::EndPlay(EEndPlayReason::Type Reason)
{
 RetireCanonicalMelee();
	const TStrongObjectPtr<AProject_JNetworkLoadController> KeepSelf(this);
	FScreenshotRequest::OnScreenshotRequestProcessed().Remove(PartyScreenshotHandle); PartyScreenshotHandle.Reset();
	const TStrongObjectPtr<UProject_JPlayerUIComponent> RetiringUI(PartyUI.Get());
	PartyUI = nullptr;
	if (RetiringUI.IsValid()) RetiringUI->DestroyComponent();
	RetireEquipmentOwnerCommands();
	if (CompanionModel) { CompanionModel->Unbind(); CompanionModel = nullptr; }
	Super::EndPlay(Reason);
}
