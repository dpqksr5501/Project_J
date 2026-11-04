namespace
{
bool EquipmentNear(int32 Phase) { return Phase == 16 || Phase == 19 || Phase == 28; }
bool EquipmentOwnerCommandsEnabled() { return EquipmentInterestEnabled() && QuestsEnabled(); }
bool EquipmentCommandPhase(int32 Phase) { return Phase == 16 || Phase == 18 || Phase == 19; }
bool EquipmentPresent(int32 Phase) { return Phase == 16 || Phase == 17 || Phase >= 28 || (EquipmentOwnerCommandsEnabled() && Phase == 19); }
}
void AProject_JNetworkLoadController::RetireEquipmentOwnerCommands()
{
	bEquipmentCommandsEnding = true;
	const TStrongObjectPtr<UProject_JInventoryViewModel> Retiring(EquipmentCommandModel.Get());
	EquipmentCommandModel = nullptr;
	EquipmentCommandState.Reset();
	if (Retiring.IsValid()) Retiring->Unbind();
}
bool AProject_JNetworkLoadController::ObserveEquipmentOwnerCommands(int32 ForeignBag, int32 ForeignQuests)
{
	if (!EquipmentOwnerCommandsEnabled() || !EquipmentCommandPhase(CurrentStage)) return true;
	if (bEquipmentCommandsEnding || HasAuthority() || !IsLocalController()) return false;
	const int32 Phase = CurrentStage;
	const TStrongObjectPtr<AProject_JNetworkLoadController> Self(this);
	const TStrongObjectPtr<UWorld> World(GetWorld());
	if (!World.IsValid() || World->bIsTearingDown) return false;
	const TStrongObjectPtr<AProject_JPlayerState> State(GetPlayerState<AProject_JPlayerState>());
	if (!State.IsValid() || !State->GetCharacterId().IsValid()) return false;
	const TStrongObjectPtr<UProject_JInventoryComponent> Inventory(State->GetInventoryComponent());
	const TStrongObjectPtr<UProject_JEquipmentManagerComponent> Equipment(State->GetEquipmentManagerComponent());
	const TStrongObjectPtr<UProject_JQuestComponent> Quest(State->FindComponentByClass<UProject_JQuestComponent>());
	if (!Inventory.IsValid() || !Equipment.IsValid() || !Quest.IsValid()) return false;
	auto Current = [&] { return !bEquipmentCommandsEnding && !IsActorBeingDestroyed() && CurrentStage == Phase &&
		GetWorld() == World.Get() && !World->bIsTearingDown && State->GetWorld() == World.Get() &&
		GetPlayerState<AProject_JPlayerState>() == State.Get() && !State->IsActorBeingDestroyed() &&
		State->GetInventoryComponent() == Inventory.Get() && State->GetEquipmentManagerComponent() == Equipment.Get() &&
		State->FindComponentByClass<UProject_JQuestComponent>() == Quest.Get() &&
		IsValid(Inventory.Get()) && IsValid(Equipment.Get()) && IsValid(Quest.Get()); };
	if (ForeignBag != 0 || ForeignQuests != 0) { bEquipmentInterestViolation = true; return false; }
	if (!EquipmentCommandModel)
	{
		EquipmentCommandState = State.Get();
		EquipmentCommandModel = NewObject<UProject_JInventoryViewModel>(this);
		const TStrongObjectPtr<UProject_JInventoryViewModel> Binding(EquipmentCommandModel.Get());
		Binding->Bind(Inventory.Get(), Equipment.Get());
		if (!Current()) return false;
	}
	const TStrongObjectPtr<UProject_JInventoryViewModel> Model(EquipmentCommandModel.Get());
	if (!Model.IsValid() || EquipmentCommandState.Get() != State.Get()) { bEquipmentInterestViolation = true; return false; }
	const auto* Progress = Quest->States.FindByPredicate([](const auto& S) { return S.QuestId == FixtureQuestId; });
	if (!Progress || Progress->Count != 5 || Progress->bClaimed) return false;
	if (EquipmentCommandSentPhase != Phase)
	{
		if (Model->bPending) return false;
		if (Phase == 16)
		{
			// Two ordinary fixture grants of the canonical definition, at levels 2/3.
			// Wait for both actual owner entries, never select an arbitrary partial delta.
			UProject_JInventoryEntry* Source = nullptr; UProject_JInventoryEntry* Target = nullptr;
			int32 SourceMatches = 0, TargetMatches = 0;
			for (const auto& Entry : Model->InventoryEntries)
			{
				if (!Entry || Entry->bEquipmentEntry || Entry->Item.bIsLocked || Entry->Item.bIsEquipped) continue;
				if (Entry->Item.ItemLevel == 2) { Source = Entry.Get(); ++SourceMatches; }
				if (Entry->Item.ItemLevel == 3) { Target = Entry.Get(); ++TargetMatches; }
			}
			FProject_JItemInstanceData Weapon;
			if (SourceMatches != 1 || TargetMatches != 1 || !Source || !Target ||
				Source->Item.ItemDef != Target->Item.ItemDef || !Model->CanDropInBag(Source, Target, false) ||
				!Equipment->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Weapon)) return false;
			EquipmentCommandSource = Source->Item.InstanceId; EquipmentCommandTarget = Target->Item.InstanceId;
			EquipmentCommandWeapon = Weapon.InstanceId;
			EquipmentSourceOrder = Source->Item.BagOrder; EquipmentTargetOrder = Target->Item.BagOrder;
			EquipmentCommandSentPhase = Phase;
			const bool Accepted = Model->DropInBag(Source, Target, false);
			if (!Current() || EquipmentCommandModel.Get() != Model.Get()) return false;
			if (!Accepted) { bEquipmentInterestViolation = true; return false; }
			Quest->RequestTracking(FixtureQuestId, false);
		}
		else
		{
			EquipmentCommandSentPhase = Phase;
			const bool Accepted = Phase == 18 ? Model->Unequip(EquipmentCommandWeapon, EProject_JEquipmentSlot::Weapon) :
				Model->Equip(EquipmentCommandWeapon, EProject_JEquipmentSlot::Weapon);
			if (!Current() || EquipmentCommandModel.Get() != Model.Get()) return false;
			if (!Accepted) { bEquipmentInterestViolation = true; return false; }
			if (Phase == 19) Quest->RequestTracking(FixtureQuestId, true);
		}
		if (!Current() || EquipmentCommandModel.Get() != Model.Get()) return false;
	}
	if (Model->bPending) return false;
	const auto Namespace = FTextInspector::GetNamespace(Model->Status);
	const auto Key = FTextInspector::GetKey(Model->Status);
	const FString ExpectedKey = Phase == 16 ? TEXT("BagChanged") : TEXT("Confirmed");
	if (!Namespace.IsSet() || Namespace.GetValue() != TEXT("ProjectJUI") || !Key.IsSet() || Key.GetValue() != ExpectedKey)
	{ bEquipmentInterestViolation = true; return false; } // Includes normal rejection and request timeout, never success-by-ACK.
	FProject_JItemInstanceData Source, Target, Weapon;
	const bool Equipped = Equipment->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Weapon);
	Progress = Quest->States.FindByPredicate([](const auto& S) { return S.QuestId == FixtureQuestId; });
	if (!Inventory->FindItemInstance(EquipmentCommandSource, Source) || !Inventory->FindItemInstance(EquipmentCommandTarget, Target) ||
		Source.BagOrder != EquipmentTargetOrder || Target.BagOrder != EquipmentSourceOrder || Source.bIsEquipped || Target.bIsEquipped ||
		Equipped != (Phase != 18) || (Equipped && Weapon.InstanceId != EquipmentCommandWeapon) ||
		!Progress || Progress->bTracked != (Phase == 19)) return false;
	if (EquipmentCommandRecordedPhase != Phase)
	{
		EquipmentCommandRows += FString::Printf(TEXT("%d,%d,%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%s,1\n"), Phase, LocalSlot,
			*State->GetCharacterId().Value.ToString(EGuidFormats::Digits), *EquipmentCommandSource.ToString(EGuidFormats::Digits),
			*EquipmentCommandTarget.ToString(EGuidFormats::Digits), *EquipmentCommandWeapon.ToString(EGuidFormats::Digits),
			EquipmentSourceOrder, EquipmentTargetOrder, Source.BagOrder, Target.BagOrder, Equipped, Progress->bTracked,
			ForeignBag, ForeignQuests, *Key.GetValue());
		EquipmentCommandRecordedPhase = Phase;
	}
	return true;
}
void AProject_JNetworkLoadController::ClientEquipmentStage_Implementation(int32 Phase)
{
	if (!EquipmentInterestEnabled() || !EquipmentInterestStage(Phase)) return;
	CurrentStage = Phase; EquipmentStableSince = 0; NextObservation = 0;
}
void AProject_JNetworkLoadController::ObserveEquipmentInterest()
{
	const double Now = FPlatformTime::Seconds();
	if (Now < NextObservation) return;
	NextObservation = Now + .25;
	auto* Own = GetPlayerState<AProject_JPlayerState>();
	if (!Own || LocalSlot < 0 || LocalSlot > 1) return;
	const int32 Expected = EquipmentPresent(CurrentStage) ? 1 : 0;
	const bool Near = EquipmentNear(CurrentStage);
	int32 ForeignPrivate = 0, RemotePawns = 0, RemoteRows = 0, RemotePresented = 0, ManagerPublic = 0, RemoteVisuals = 0;
	const TStrongObjectPtr<AProject_JNetworkLoadController> KeepSelf(this);
	int32 OwnerPublic = -1, States = 0, ForeignBag = 0, ForeignQuests = 0;
	bool NativeQuestComponents = true;
	for (TActorIterator<AProject_JPlayerState> It(GetWorld()); It; ++It)
	{
		if (!It->GetPublicClassId().ToString().StartsWith(TEXT("DClient"))) continue;
		++States;
		if (*It == Own) continue;
		ForeignPrivate += It->GetEquipmentManagerComponent()->GetPrivateEquipmentCount();
		if (EquipmentOwnerCommandsEnabled())
		{
			ForeignBag += It->GetInventoryComponent()->GetItemInstances().Num();
			const auto* Quest = It->FindComponentByClass<UProject_JQuestComponent>();
			NativeQuestComponents &= Quest != nullptr;
			if (Quest) ForeignQuests += Quest->States.Num();
		}
		ManagerPublic += It->GetEquipmentManagerComponent()->GetAllEquippedItems().Num();
	}
	for (TActorIterator<AProject_JPlayerCharacter> It(GetWorld()); It; ++It)
	{
		if (It->IsActorBeingDestroyed()) continue;
		auto* Runtime = It->FindComponentByClass<UProject_JEquipmentRuntimeComponent>();
		if (It->GetPlayerState() == Own) { OwnerPublic = Runtime ? Runtime->GetPublicEquipment().Items.Num() : -1; continue; }
		++RemotePawns;
		if (!Runtime) continue;
		RemoteRows += Runtime->GetPublicEquipment().Items.Num();
		RemotePresented += Runtime->GetPresentedItemInSlot(EProject_JEquipmentSlot::Weapon) ? 1 : 0;
		RemoteVisuals += IsValid(It->GetWeaponPresentationComponent()->GetSpawnedWeapon()) ? 1 : 0;
	}
	const int32 Private = Own->GetEquipmentManagerComponent()->GetPrivateEquipmentCount();
	if (ForeignPrivate != 0 || (EquipmentOwnerCommandsEnabled() && (ForeignBag != 0 || ForeignQuests != 0))) bEquipmentInterestViolation = true;
	const int32 RemoteExpected = Near ? Expected : 0;
	const bool CommandsReady = NativeQuestComponents && ObserveEquipmentOwnerCommands(ForeignBag, ForeignQuests);
	if (bEquipmentCommandsEnding || IsActorBeingDestroyed()) return;
	const bool Ready = CommandsReady && !bEquipmentInterestViolation && States == 2 && Private == Expected && OwnerPublic == 0 &&
		ForeignPrivate == 0 && RemotePawns == (Near ? 1 : 0) && RemoteRows == RemoteExpected &&
		RemotePresented == RemoteExpected && ManagerPublic == RemoteExpected && RemoteVisuals == RemoteExpected;
	if (Ready) { if (EquipmentStableSince == 0) EquipmentStableSince = Now; } else EquipmentStableSince = 0;
	const bool Stable = Ready && Now - EquipmentStableSince >= 1;
	if (Stable && EquipmentRecordedPhase != CurrentStage)
	{
		EquipmentInterestRows += FString::Printf(TEXT("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,1\n"), CurrentStage, LocalSlot,
			Private, OwnerPublic, ForeignPrivate, RemotePawns, RemoteRows, RemotePresented, ManagerPublic, RemoteVisuals);
		EquipmentRecordedPhase = CurrentStage;
	}
	ServerEquipmentObserved(CurrentStage, Stable, bEquipmentInterestViolation,
		FString::Printf(TEXT("owner=%d ownerPublic=%d foreignPrivate=%d pawns=%d rows=%d presented=%d query=%d visual=%d"),
			Private, OwnerPublic, ForeignPrivate, RemotePawns, RemoteRows, RemotePresented, ManagerPublic, RemoteVisuals));
}
void AProject_JNetworkLoadController::ServerEquipmentObserved_Implementation(int32 Phase, bool bReady, bool bViolation, const FString& Detail)
{
	if (!EquipmentInterestEnabled() || Detail.Len() > 256) return;
	auto* Mode = GetWorld() ? GetWorld()->GetAuthGameMode<AProject_JNetworkLoadGameMode>() : nullptr;
	if (Mode) Mode->ObserveEquipmentClient(this, Phase, bReady, bViolation, Detail);
}
void AProject_JNetworkLoadGameMode::ObserveEquipmentClient(AProject_JNetworkLoadController* PC, int32 Phase, bool bReady, bool bViolation, const FString& Detail)
{
	if (!bFixture || bFinished || !EquipmentInterestEnabled() || !Clients.Contains(PC) || Stage != Phase || !EquipmentInterestStage(Phase)) return;
	PC->LastObservation = Detail;
	if (bViolation) { Finish(false, TEXT("Private component audience or normal owner command/reply failed")); return; }
	if (bReady && EquipmentOwnerCommandsEnabled() && EquipmentCommandPhase(Phase) && PC->ConfirmedStage != Phase &&
		!ValidateEquipmentOwnerCommands(PC, Phase))
	{ Finish(false, TEXT("Owner command did not match actual authority inventory/equipment/quest state")); return; }
	if (bReady) PC->ConfirmedStage = Phase;
}
bool AProject_JNetworkLoadGameMode::ValidateEquipmentOwnerCommands(AProject_JNetworkLoadController* PC, int32 Phase)
{
	const int32 Slot = Clients.IndexOfByKey(PC);
	const auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
	if (Slot < 0 || Slot > 1 || !State || EquipmentOwnerBagItems.Num() != 4 || EquipmentOwnerBagOrders.Num() != 4 ||
		EquipmentInterestItems.Num() != 2) return false;
	const auto* Inventory = State->GetInventoryComponent(); const auto* Equipment = State->GetEquipmentManagerComponent();
	const auto* Quest = State->FindComponentByClass<UProject_JQuestComponent>();
	const auto* Progress = Quest ? Quest->States.FindByPredicate([](const auto& S) { return S.QuestId == FixtureQuestId; }) : nullptr;
	FProject_JItemInstanceData Source, Target, Weapon;
	const bool Equipped = Equipment->GetEquippedItemInstance(EProject_JEquipmentSlot::Weapon, Weapon);
	const int32 Index = Slot * 2;
	if (!State->GetCharacterId().IsValid() || !Inventory->FindItemInstance(EquipmentOwnerBagItems[Index], Source) ||
		!Inventory->FindItemInstance(EquipmentOwnerBagItems[Index + 1], Target) || Source.ItemLevel != 2 || Target.ItemLevel != 3 ||
		Source.BagOrder != EquipmentOwnerBagOrders[Index + 1] || Target.BagOrder != EquipmentOwnerBagOrders[Index] ||
		Source.bIsLocked || Target.bIsLocked || Source.bIsEquipped || Target.bIsEquipped ||
		Equipped != (Phase != 18) || (Equipped && Weapon.InstanceId != EquipmentInterestItems[Slot]) ||
		!Progress || Progress->Count != 5 || Progress->bClaimed || Progress->bTracked != (Phase == 19)) return false;
	EquipmentCommandAuthorityRows += FString::Printf(TEXT("%d,%d,%s,%s,%s,%s,%d,%d,%d,%d,%d,%d,na,na,state,1\n"), Phase, Slot,
		*State->GetCharacterId().Value.ToString(EGuidFormats::Digits), *EquipmentOwnerBagItems[Index].ToString(EGuidFormats::Digits),
		*EquipmentOwnerBagItems[Index + 1].ToString(EGuidFormats::Digits), *EquipmentInterestItems[Slot].ToString(EGuidFormats::Digits),
		EquipmentOwnerBagOrders[Index], EquipmentOwnerBagOrders[Index + 1], Source.BagOrder, Target.BagOrder, Equipped, Progress->bTracked);
	return true;
}
void AProject_JNetworkLoadGameMode::StartEquipmentStage(int32 Phase)
{
	const TStrongObjectPtr<AProject_JNetworkLoadGameMode> KeepSelf(this);
	Stage = Phase; StageStarted = FPlatformTime::Seconds();
	if (Clients.Num() != 2) { Finish(false, TEXT("Equipment interest needs exactly two real clients")); return; }
	auto* Definition = LoadObject<UProject_JEquipmentItemDefinition>(nullptr, TEXT("/Game/DataAssetSets/Animation_Profiles/Equip/DA_Greatsword_Equip.DA_Greatsword_Equip"));
	if (!Definition) { Finish(false, TEXT("Canonical equipment definition unavailable")); return; }
	for (int32 Slot = 0; Slot < 2; ++Slot)
	{
		auto* PC = Clients[Slot].Get();
		auto* State = PC ? PC->GetPlayerState<AProject_JPlayerState>() : nullptr;
		auto* Player = PC ? Cast<AProject_JPlayerCharacter>(PC->GetPawn()) : nullptr;
		if (!State || !Player) { Finish(false, TEXT("Equipment interest lost current Avatar")); return; }
		const TStrongObjectPtr<AProject_JNetworkLoadController> KeepController(PC);
		const TStrongObjectPtr<AProject_JPlayerState> KeepPlayerState(State);
		auto* Manager = State->GetEquipmentManagerComponent();
		if (Phase == 16)
		{
			const auto Item = State->GetInventoryComponent()->AddItemDefinition(Definition);
			if (!Item.InstanceId.IsValid() || !Manager->TryEquipItemInstanceById(Item.InstanceId).bSucceeded)
			{ Finish(false, TEXT("Actual owned equipment admission failed")); return; }
			EquipmentInterestItems.Add(Item.InstanceId);
			if (EquipmentOwnerCommandsEnabled())
			{
				if (bFinished || IsActorBeingDestroyed() || !IsValid(PC) || PC->IsActorBeingDestroyed() ||
					PC->GetPlayerState<AProject_JPlayerState>() != State || !IsValid(State) || State->IsActorBeingDestroyed() ||
					!IsValid(Manager) || State->GetEquipmentManagerComponent() != Manager || !IsValid(Player) ||
					Player->IsActorBeingDestroyed() || PC->GetPawn() != Player || !GetWorld() || GetWorld()->bIsTearingDown) return;
				const TStrongObjectPtr<AProject_JNetworkLoadGameMode> Self(this);
				const TStrongObjectPtr<AProject_JNetworkLoadController> KeepPC(PC);
				const TStrongObjectPtr<AProject_JPlayerState> KeepState(State);
				const TStrongObjectPtr<UProject_JInventoryComponent> Inventory(State->GetInventoryComponent());
				const TStrongObjectPtr<UProject_JEquipmentManagerComponent> KeepManager(Manager);
				const TStrongObjectPtr<UProject_JEquipmentItemDefinition> KeepDefinition(Definition);
				const TStrongObjectPtr<AProject_JPlayerCharacter> KeepPlayer(Player);
				const TStrongObjectPtr<UWorld> World(GetWorld());
				auto Current = [&] { return !bFinished && !IsActorBeingDestroyed() && Stage == Phase &&
					GetWorld() == World.Get() && !World->bIsTearingDown && PC->GetWorld() == World.Get() &&
					Clients.IsValidIndex(Slot) && Clients[Slot].Get() == PC && IsValid(PC) && !PC->IsActorBeingDestroyed() &&
					PC->GetPlayerState<AProject_JPlayerState>() == State && IsValid(State) && !State->IsActorBeingDestroyed() &&
					PC->GetPawn() == Player && IsValid(Player) && !Player->IsActorBeingDestroyed() && Player->GetWorld() == World.Get() &&
					State->GetWorld() == World.Get() &&
					State->GetInventoryComponent() == Inventory.Get() && State->GetEquipmentManagerComponent() == Manager &&
					IsValid(Inventory.Get()) && IsValid(Manager); };
				if (EquipmentOwnerBagItems.Num() != Slot * 2 || !Current()) { Finish(false, TEXT("Owner bag setup lost its current authority binding")); return; }
				for (int32 Level = 2; Level <= 3; ++Level)
				{
					const auto BagItem = Inventory->AddItemDefinition(Definition, 1, Level);
					if (!Current()) return;
					if (!BagItem.IsValid() || BagItem.bIsLocked || BagItem.bIsEquipped || BagItem.BagOrder < 0)
					{ Finish(false, TEXT("Normal owner bag fixture grant was not admitted")); return; }
					EquipmentOwnerBagItems.Add(BagItem.InstanceId); EquipmentOwnerBagOrders.Add(BagItem.BagOrder);
				}
			}
		}
		if (Phase == 18 && !EquipmentOwnerCommandsEnabled()) Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
		if (Phase == 28)
		{
			auto* Old = Player;
			Player = ProjectJAuthoredAnimationFixture::Spawn(GetWorld(), PC, State, Old->GetActorLocation());
			if (!Player) { Finish(false, TEXT("Actual replacement Avatar spawn failed")); return; }
			Old->Destroy();
			State->SetPublicCharacterSnapshot(FName(*FString::Printf(TEXT("DClient%d"), Slot)), 1);
			Manager->UnequipSlot(EProject_JEquipmentSlot::Weapon);
			if (!Manager->TryEquipItemInstanceById(EquipmentInterestItems[Slot]).bSucceeded)
			{ Finish(false, TEXT("Replacement Avatar failed existing owned equipment commit")); return; }
		}
		const auto* Authored = Player->GetClass()->GetDefaultObject<AProject_JPlayerCharacter>();
		if (!Authored || Player->bAlwaysRelevant || FMath::Sqrt(Authored->GetNetCullDistanceSquared()) >= 40000)
		{ Finish(false, TEXT("Equipment interest needs the actual finite authored Pawn cull")); return; }
		Player->SetNetCullDistanceSquared(Authored->GetNetCullDistanceSquared());
		auto Teleport = MakeShared<FTeleportEffect>();
		Teleport->TargetLocation = FVector((Slot == 0 ? -1 : 1) * (EquipmentNear(Phase) ? 1000 : 40000), 0, 100);
		Player->GetMover()->QueueInstantMovementEffect(Teleport); Player->ForceNetUpdate(); PC->SetViewTarget(Player);
		const int32 Expected = EquipmentPresent(Phase) ? 1 : 0;
		const bool AwaitOwnerCommand = EquipmentOwnerCommandsEnabled() && (Phase == 18 || Phase == 19);
		if ((!AwaitOwnerCommand && Manager->GetPrivateEquipmentCount() != Expected) || State->GetInventoryComponent()->HasItemInstance(EquipmentInterestItems[Slot]) == false)
		{ Finish(false, TEXT("Authority owner equipment/inventory changed unexpectedly")); return; }
		PC->ConfirmedStage = INDEX_NONE; PC->ClientEquipmentStage(Phase);
	}
}
