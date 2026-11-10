struct FExpansionSource
{
	const TCHAR* Name;
	const TCHAR* Fbx;
};

constexpr FExpansionSource ExpansionSources[] = {
	{TEXT("Bash1"), TEXT("pdw_01_01_att_skill_bash4combo_att_00")},
	{TEXT("Bash2"), TEXT("pdw_01_01_att_skill_bash4combo_att_01")},
	{TEXT("Bash3"), TEXT("pdw_01_01_att_skill_bash4combo_att_02")},
	{TEXT("Bash4"), TEXT("pdw_01_01_att_skill_bash4combo_att_03")},
	{TEXT("Twin1"), TEXT("pdw_05_01_att_skill_combo_01")},
	{TEXT("Twin2"), TEXT("pdw_05_01_att_skill_combo_02")},
	{TEXT("GroundGush"), TEXT("pdw_05_00_att_skill_groundgush_00")}
};

const FString ExpansionFolder = Sequences + TEXT("DirectBody/Expansion");

void AuditTravelSequence(UAnimSequence& Clip, double Start, double End, const TCHAR* Label)
{
	const auto& Reference = Clip.GetSkeleton()->GetReferenceSkeleton();
	const FName Root = Reference.GetBoneName(0);
	FAnimPoseEvaluationOptions Options;
	Options.bShouldRetarget = false;
	Options.bExtractRootMotion = false;
	FAnimPose Pose;
	for (double Time = Start; ; Time = FMath::Min(Time + .1, End))
	{
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Time, Options, Pose);
		const auto Read = [&Pose, &Reference](FName Bone)
		{
			return Reference.FindBoneIndex(Bone) != INDEX_NONE
				? UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World).GetTranslation().ToCompactString() : FString(TEXT("-"));
		};
		const bool bLegacy = Reference.FindBoneIndex(TEXT("pelvis")) == INDEX_NONE;
		UE_LOG(LogTemp, Display, TEXT("SpellbladeTravel %s %s %.3f %s Root=%s Pelvis=%s Left=%s Right=%s Enabled=%d"),
			Label, *Clip.GetName(), Time, *Root.ToString(), *Read(Root), *Read(bLegacy ? TEXT("Bip01_Pelvis") : TEXT("pelvis")),
			*Read(bLegacy ? TEXT("Bip01_L_Toe0") : TEXT("ball_l")), *Read(bLegacy ? TEXT("Bip01_R_Toe0") : TEXT("ball_r")),
			Clip.bEnableRootMotion);
		if (Time >= End) { break; }
	}
}

bool ImportExpansion(const FString& Params)
{
	FString SourceFolder;
	if (!FParse::Value(*Params, TEXT("FbxRoot="), SourceFolder)) { return false; }
	const bool bAudit = FParse::Param(*Params, TEXT("AuditTravel"));
	auto* Legacy = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Spellblade_Woman"));
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	auto* Rig = Asset<UIKRetargeter>(TEXT("/Game/Characters/Spellblade/Animation/CombatKit/RTG_SpellbladeLegacy_to_DirectKit"));
	if (!Legacy || !Body || !Rig) { return false; }

	FIKRetargetBatchOperationInputs Retarget;
	Retarget.SourceMesh = Legacy;
	Retarget.TargetMesh = Body;
	Retarget.IKRetargetAsset = Rig;
	Retarget.TargetPath = ExpansionFolder;
	Retarget.Prefix = TEXT("AS_Longblade_");
	Retarget.bIncludeReferencedAssets = false;
	TArray<UAssetImportTask*> Tasks;
	for (const auto& Source : ExpansionSources)
	{
		const FString Filename = FPaths::Combine(SourceFolder, FString(Source.Fbx) + TEXT(".fbx"));
		if (!FPaths::FileExists(Filename)
			|| (!bAudit && FPackageName::DoesPackageExist(ExpansionFolder / (Retarget.Prefix + Source.Name)))) { return false; }
		auto* Task = NewObject<UAssetImportTask>();
		auto* Options = NewObject<UFbxImportUI>(Task);
		Options->bAutomatedImportShouldDetectType = false;
		Options->MeshTypeToImport = FBXIT_Animation;
		Options->bImportAsSkeletal = true;
		Options->bImportMesh = false;
		Options->bImportAnimations = true;
		Options->Skeleton = Legacy->GetSkeleton();
		Task->Filename = Filename;
		Task->DestinationName = Source.Name;
		Task->DestinationPath = Sequences + TEXT("SourceTransient/Expansion");
		Task->Factory = NewObject<UFbxFactory>(Task);
		Task->Options = Options;
		Task->bAutomated = true;
		Task->bSave = false;
		Task->bReplaceExisting = false;
		Tasks.Add(Task);
	}
	FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
	for (const auto* Task : Tasks)
	{
		const auto& Imported = Task->GetObjects();
		auto* Sequence = Imported.Num() == 1 ? Cast<UAnimSequence>(Imported[0]) : nullptr;
		if (!Sequence || Sequence->GetSkeleton() != Legacy->GetSkeleton()) { return false; }
		if (bAudit) { AuditTravelSequence(*Sequence, 0., Sequence->GetPlayLength(), TEXT("Source")); }
		Retarget.AssetsToRetarget.Emplace(Sequence);
	}
	if (bAudit) { return true; }
	const auto Outputs = UIKRetargetBatchOperation::RunBatchRetarget(Retarget);
	if (Outputs.Num() != UE_ARRAY_COUNT(ExpansionSources)) { return false; }
	FAnimPoseEvaluationOptions Options;
	Options.bShouldRetarget = false;
	Options.OptionalSkeletalMesh = Body;
	TArray<UAnimSequence*> SequencesToSave;
	for (const auto& Output : Outputs)
	{
		auto* Clip = Cast<UAnimSequence>(Output.GetAsset());
		if (!Clip || Clip->GetSkeleton() != Body->GetSkeleton()) { return false; }
		SequencesToSave.Add(Clip);
		for (double Time = 0.; Time <= Clip->GetPlayLength(); Time += .1)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Clip, Time, Options, Pose);
			const auto Read = [&Pose](FName Bone)
			{
				return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World).GetTranslation();
			};
			UE_LOG(LogTemp, Display, TEXT("ExpansionPose %s %.2f R=%s L=%s FeetZ=%.1f/%.1f"),
				*Clip->GetName(), Time, *Read(TEXT("hand_r")).ToCompactString(), *Read(TEXT("hand_l")).ToCompactString(),
				Read(TEXT("ball_l")).Z, Read(TEXT("ball_r")).Z);
		}
	}
	for (auto* Clip : SequencesToSave)
	{
		if (!Save(Clip)) { return false; }
	}
	UE_LOG(LogTemp, Display, TEXT("SPELLBLADE_EXPANSION_IMPORTED"));
	return true;
}

struct FExpansionStrike
{
	const TCHAR* Name;
	const TCHAR* Next;
	float OpeningEnd;
	float RecoveryRate;
	FVector2f Hit;
	FVector2f Continue;
};

const FExpansionStrike ExpansionStrikes[] = {
	{TEXT("Bash1"), TEXT("Bash2"), 1.12f, 4.2f, {.63f, .80f}, {.83f, 1.10f}},
	{TEXT("Bash2"), TEXT("Bash3"), .90f, 4.6f, {.34f, .53f}, {.62f, .87f}},
	{TEXT("Bash3"), TEXT("Bash4"), .94f, 4.4f, {.39f, .56f}, {.65f, .91f}},
	{TEXT("Bash4"), nullptr, .78f, 3.9f, {.17f, .46f}, {0.f, 0.f}},
	{TEXT("Twin1"), TEXT("Twin2"), .99f, 5.2f, {.49f, .69f}, {.72f, .97f}},
	{TEXT("Twin2"), nullptr, .64f, 4.4f, {.23f, .43f}, {0.f, 0.f}},
	{TEXT("GroundGush"), nullptr, 0.f, 2.2f, {1.33f, 1.68f}, {0.f, 0.f}}
};

struct FExpansionRoute
{
	const TCHAR* Key;
	const TCHAR* Command;
	const TCHAR* FirstNode;
};

constexpr FExpansionRoute ExpansionRoutes[] = {
	{TEXT("E"), TEXT("BashChain"), TEXT("Bash1")},
	{TEXT("Q"), TEXT("TwinChain"), TEXT("Twin1")},
	{TEXT("V"), TEXT("GroundGush"), TEXT("GroundGush")}
};

FGameplayTag ExpansionKey(const FExpansionRoute& Route)
{
	return Tag(FString(TEXT("InputTag.Skill.Caps.")) + Route.Key);
}

FGameplayTag ExpansionCommand(const FExpansionRoute& Route)
{
	return Tag(FString(TEXT("Command.Spellblade.")) + Route.Command);
}

bool AuthorExpansion(UProject_JCombatStyleDefinition& Style, UProject_JSkillInputMappingData& Mapping, TArray<UObject*>& Assets)
{
	auto& Combo = *Style.ComboDefinition;
	const auto* Base = Combo.FindNode(Tag(TEXT("Combo.Spellblade.Light.1")));
	const FGameplayTag Primary = Tag(TEXT("Command.Spellblade.Primary"));
	auto* Grant = Style.InlineAbilities.FindByPredicate([Primary](const auto& Entry)
		{ return Entry.InputTag == Primary || Entry.AdditionalInputTags.HasTagExact(Primary); });
	if (!Base || !Base->AttackDefinition || !Grant || Combo.FindNode(Tag(TEXT("Combo.Spellblade.Bash1")))) { return false; }
	auto* BaseAttack = Base->AttackDefinition.Get();
	const auto* Trail = Style.CombatPresentationSet->AttackPresentations.FindByPredicate([BaseAttack](const auto& Entry)
		{ return Entry.AttackTag == BaseAttack->AttackTag; });
	if (!Trail) { return false; }
	const auto TrailTemplate = *Trail;
	FGameplayTagContainer Blocked;
	for (const auto& State : {TEXT("State.Movement.InAir"), TEXT("State.Mounted"), TEXT("State.HitReacting"), TEXT("State.Dead")})
	{
		Blocked.AddTag(Tag(State));
	}

	for (const auto& Route : ExpansionRoutes)
	{
		const auto StandardTag = Tag(FString(TEXT("InputTag.Skill.")) + Route.Key);
		const auto* Standard = Mapping.DirectSkillBindings.FindByPredicate([StandardTag](const auto& Binding)
			{ return Binding.InputTag == StandardTag; });
		if (!Standard || !Standard->InputAction) { return false; }
		auto Alternate = *Standard;
		Alternate.InputTag = ExpansionKey(Route);
		Alternate.RequiredHeldKey = EKeys::CapsLock;
		Alternate.Priority = Standard->Priority + 1;
		if (FCString::Strcmp(Route.Key, TEXT("Q")) == 0)
		{
			auto* QuickModifier = Mapping.ModifierBindings.FindByPredicate([&Alternate](const auto& Binding)
				{ return Binding.InputAction == Alternate.InputAction; });
			if (!QuickModifier) { return false; }
			QuickModifier->BlockedHeldKey = EKeys::CapsLock;
		}
		Mapping.DirectSkillBindings.Add(Alternate);

		auto& Command = Style.CommandSet->Commands.AddDefaulted_GetRef();
		Command.CommandTag = ExpansionCommand(Route);
		Command.ResultInputTag = Command.CommandTag;
		Command.OrderedInputSequence.Add(ExpansionKey(Route));
		Command.RequiredOwnerTags.AddTag(Tag(TEXT("State.CombatMode")));
		Command.BlockedOwnerTags = Blocked;
		Grant->AdditionalInputTags.AddTag(Command.CommandTag);
	}

	for (const auto& Strike : ExpansionStrikes)
	{
		const FString Name = Strike.Name;
		auto* Clip = Asset<UAnimSequence>(ExpansionFolder / (TEXT("AS_Longblade_") + Name));
		auto* Attack = Duplicate(BaseAttack, SpellbladeDataRoot + TEXT("AttackDefinition/Spellblade/CombatKit/DA_Attack_Longblade_") + Name);
		if (!Clip || !Attack || Clip->HasRootMotion() || Strike.OpeningEnd >= Clip->GetPlayLength()) { return false; }
		const FString MontageName = TEXT("AM_Longblade_") + Name;
		const FString MontageFolder = Montages + TEXT("Authored");
		if (FPackageName::DoesPackageExist(MontageFolder / MontageName)) { return false; }
		auto* Factory = NewObject<UAnimMontageFactory>();
		Factory->TargetSkeleton = Clip->GetSkeleton();
		Factory->SourceAnimation = Clip;
		auto* Montage = Cast<UAnimMontage>(FAssetToolsModule::GetModule().Get().CreateAsset(
			MontageName, MontageFolder, UAnimMontage::StaticClass(), Factory));
		if (!Montage || Montage->SlotAnimTracks.Num() != 1) { return false; }
		Montage->SlotAnimTracks[0].SlotName = TEXT("DefaultSlot");
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Reset();
		Montage->BlendIn.SetBlendTime(.10f);
		Montage->BlendOut.SetBlendTime(.23f);
		if (Strike.OpeningEnd > 0.f)
		{
			Append(*Montage, *Clip, Strike.OpeningEnd);
			Append(*Montage, *Clip, Clip->GetPlayLength(), Strike.RecoveryRate, Strike.OpeningEnd);
		}
		else
		{
			Append(*Montage, *Clip, .92f, 1.2f);
			Append(*Montage, *Clip, 2.45f, 5.f, .92f);
			Append(*Montage, *Clip, 3.22f, 1.2f, 2.45f);
			Append(*Montage, *Clip, Clip->GetPlayLength(), Strike.RecoveryRate, 3.22f);
		}
		UpdateMontage(*Montage);
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, TEXT("Combat"));
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, TEXT("Trail"));
		if (!AddHit(*Montage, Strike.Hit, TEXT("PalmGrip_R"), true)) { return false; }
		Attack->AttackTag = Tag(TEXT("Attack.Spellblade.") + Name);
		Attack->Montage = Montage;
		Attack->MontageSectionName = NAME_None;
		Attack->PlayRate = 1.f;
		Attack->MovementPolicy = EProject_JAttackMovementPolicy::InPlace;
		Attack->MeleeTrajectory = {};
		auto Entry = TrailTemplate;
		Entry.AttackTag = Attack->AttackTag;
		Style.CombatPresentationSet->AttackPresentations.Add(Entry);

		auto& Node = Combo.Nodes.AddDefaulted_GetRef();
		Node.NodeTag = Tag(TEXT("Combo.Spellblade.") + Name);
		Node.AttackDefinition = Attack;
		Node.BlockedOwnerTags = Blocked;
		for (const auto& Route : ExpansionRoutes)
		{
			if (Name == Route.FirstNode) { Node.StartInputTags.AddTag(ExpansionCommand(Route)); }
		}
		if (Strike.Next)
		{
			if (!UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(Montage, TEXT("Combat"), Strike.Continue.X,
				Strike.Continue.Y - Strike.Continue.X, UProject_JAnimNotifyState_ComboWindow::StaticClass())) { return false; }
			const bool bBash = Name.StartsWith(TEXT("Bash"));
			for (const FGameplayTag Follow : {ExpansionCommand(ExpansionRoutes[bBash ? 0 : 1]),
				Tag(bBash ? TEXT("Command.Spellblade.Flurry") : TEXT("Command.Spellblade.Quick")),
				bBash ? Primary : Tag(TEXT("Command.Spellblade.Secondary"))})
			{
				auto& Transition = Node.Transitions.AddDefaulted_GetRef();
				Transition.InputTag = Follow;
				Transition.TargetNodeTag = Tag(FString(TEXT("Combo.Spellblade.")) + Strike.Next);
				Transition.BlockedOwnerTags = Blocked;
			}
		}
		UpdateMontage(*Montage);
		Assets.Append({Montage, Attack});
	}
	return true;
}

bool VerifyExpansion(const UProject_JCombatStyleDefinition& Style, const UProject_JSkillInputMappingData& Mapping)
{
	for (const auto& Route : ExpansionRoutes)
	{
		const auto Input = ExpansionKey(Route);
		const auto Command = ExpansionCommand(Route);
		const auto* First = Style.ComboDefinition->FindNode(Tag(FString(TEXT("Combo.Spellblade.")) + Route.FirstNode));
		if (!First || !First->StartInputTags.HasTagExact(Command) ||
			!Mapping.DirectSkillBindings.ContainsByPredicate([Input](const auto& Binding)
				{ return Binding.InputTag == Input && Binding.RequiredHeldKey == EKeys::CapsLock; }) ||
			!Style.CommandSet->Commands.ContainsByPredicate([Input, Command](const auto& Entry)
				{ return Entry.ResultInputTag == Command && Entry.OrderedInputSequence.Num() == 1 && Entry.OrderedInputSequence[0] == Input; }))
		{
			return false;
		}
	}
	for (const auto& Strike : ExpansionStrikes)
	{
		const auto* Node = Style.ComboDefinition->FindNode(Tag(FString(TEXT("Combo.Spellblade.")) + Strike.Name));
		if (!Node || !Node->AttackDefinition || !Node->AttackDefinition->Montage ||
			Node->Transitions.Num() != (Strike.Next ? 3 : 0)) { return false; }
	}
	UE_LOG(LogTemp, Display, TEXT("SPELLBLADE_CAPS_EXPANSION_VALIDATED"));
	return true;
}

bool AuditAttackTravel(const UProject_JCombatStyleDefinition& Style)
{
	for (const auto& Node : Style.ComboDefinition->Nodes)
	{
		const auto* Attack = Node.AttackDefinition.Get();
		if (!Attack || !Attack->Montage) { return false; }
		UE_LOG(LogTemp, Display, TEXT("SpellbladeTravelRoute %s Policy=%d MontageRoot=%d"),
			*Node.NodeTag.ToString(), int32(Attack->MovementPolicy), Attack->Montage->HasRootMotion());
		for (const auto& Slot : Attack->Montage->SlotAnimTracks)
		{
			for (const auto& Segment : Slot.AnimTrack.AnimSegments)
			{
				auto* Clip = Cast<UAnimSequence>(Segment.GetAnimReference());
				if (!Clip) { return false; }
				AuditTravelSequence(*Clip, Segment.AnimStartTime, Segment.AnimEndTime, *Node.NodeTag.ToString());
			}
		}
	}
	return true;
}

// The imported top bone carries the hips. Give Mover a translation-only carrier,
// then preserve the original height and rotation in every child of that carrier.
bool BakeAttackTravel(UAnimSequence& Clip, float DescentEnd = 0.f)
{
	const auto& Reference = Clip.GetSkeleton()->GetReferenceSkeleton();
	const auto* Model = Clip.GetDataModel();
	const FName RootName = Reference.GetBoneName(0);
	TArray<FTransform> OriginalRoot;
	Model->GetBoneTrackTransforms(RootName, OriginalRoot);
	if (Clip.HasRootMotion() || OriginalRoot.Num() < 2 || OriginalRoot.Num() != Model->GetNumberOfKeys()) { return false; }

	const bool bDescent = DescentEnd > 0.f;
	const FVector FirstRoot = OriginalRoot[0].GetTranslation();
	const FVector Origin = bDescent ? FVector::ZeroVector : FirstRoot * FVector(1., 1., 0.);
	const FTransform NeutralRoot = Reference.GetRawRefBonePose()[0];
	TArray<FTransform> Carrier;
	double Descent = 0.;
	for (int32 Key = 0; Key < OriginalRoot.Num(); ++Key)
	{
		const FVector Position = OriginalRoot[Key].GetTranslation();
		const double Time = Clip.GetPlayLength() * Key / (OriginalRoot.Num() - 1);
		if (bDescent && Time <= DescentEnd + UE_SMALL_NUMBER)
		{
			// Only the airborne descent drives the capsule. The following crouch
			// and rise belong to the source pose after contact.
			Descent = FMath::Min(Descent, Position.Z - FirstRoot.Z);
		}
		Carrier.Add(NeutralRoot);
		Carrier.Last().AddToTranslation(bDescent ? FVector(0., 0., Descent)
			: Position * FVector(1., 1., 0.) - Origin);
	}

	const FName TestBones[] = {TEXT("pelvis"), TEXT("ball_l"), TEXT("ball_r"), TEXT("hand_r")};
	FAnimPoseEvaluationOptions Options;
	Options.bShouldRetarget = false;
	Options.bIncorporateRootMotionIntoPose = false;
	TArray<FVector> Before;
	const int32 Samples = 32;
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Clip.GetPlayLength() * Index / Samples, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		for (const FName Bone : TestBones)
		{
			Before.Add(UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World).GetTranslation() - Origin);
		}
	}

	for (int32 BoneIndex = 1; BoneIndex < Reference.GetRawBoneNum(); ++BoneIndex)
	{
		if (Reference.GetParentIndex(BoneIndex) != 0) { continue; }
		const FName Bone = Reference.GetBoneName(BoneIndex);
		TArray<FTransform> Keys;
		const bool bHasTrack = Model->IsValidBoneTrackName(Bone);
		if (bHasTrack) { Model->GetBoneTrackTransforms(Bone, Keys); }
		else
		{
			if (!Clip.GetController().AddBoneCurve(Bone, false)) { return false; }
			Keys.Init(Reference.GetRawRefBonePose()[BoneIndex], Carrier.Num());
		}
		if (Keys.Num() == 1)
		{
			const FTransform Constant = Keys[0];
			Keys.Init(Constant, Carrier.Num());
		}
		if (Keys.Num() != Carrier.Num()) { return false; }
		for (int32 Key = 0; Key < Keys.Num(); ++Key)
		{
			FTransform Component = Keys[Key] * OriginalRoot[Key];
			Component.AddToTranslation(-Origin);
			Keys[Key] = Component.GetRelativeTransform(Carrier[Key]);
		}
		if (!SetTrack(Clip, Bone, Keys)) { return false; }
	}
	if (!SetTrack(Clip, RootName, Carrier)) { return false; }
	Clip.bEnableRootMotion = true;
	Clip.bForceRootLock = true;
	Clip.RootMotionRootLock = ERootMotionRootLock::RefPose;

	double MaxPoseError = 0.;
	double MaxTravel = 0.;
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		const double Time = Clip.GetPlayLength() * Index / Samples;
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Time, Options, Pose);
		const double Key = double(Index) * (Carrier.Num() - 1) / Samples;
		const int32 Lower = FMath::FloorToInt(Key);
		const FVector Travel = FMath::Lerp(Carrier[Lower].GetTranslation(),
			Carrier[FMath::Min(Lower + 1, Carrier.Num() - 1)].GetTranslation(), Key - Lower) - NeutralRoot.GetTranslation();
		MaxTravel = FMath::Max(MaxTravel, Travel.Size());
		for (int32 Bone = 0; Bone < UE_ARRAY_COUNT(TestBones); ++Bone)
		{
			const FVector World = UAnimPoseExtensions::GetBonePose(Pose, TestBones[Bone], EAnimPoseSpaces::World).GetTranslation() + Travel;
			MaxPoseError = FMath::Max(MaxPoseError, FVector::Dist(World, Before[Index * UE_ARRAY_COUNT(TestBones) + Bone]));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeTravelBake %s Peak=%.2fcm PoseError=%.3fcm"),
		*Clip.GetName(), MaxTravel, MaxPoseError);
	return MaxTravel > 4. && MaxPoseError < 2.;
}

void UseMontageAttackTravel(UProject_JAttackDefinition& Attack)
{
	Attack.MovementPolicy = EProject_JAttackMovementPolicy::RootMotionMontage;
	Attack.bUseFlyingMovementModeForRootMotion = false;
}

bool ApplyAttackTravel(const UProject_JCombatStyleDefinition& Style, TArray<UObject*>& Assets)
{
	const float HoldEnds[] = {2.13f, 2.38f, 1.92f, 2.40f, 1.80f, 1.62f, 0.f};
	const float ReturnRates[] = {2.55f, 2.75f, 2.65f, 2.80f, 2.35f, 2.15f, 2.2f};
	static_assert(UE_ARRAY_COUNT(HoldEnds) == UE_ARRAY_COUNT(ExpansionStrikes));
	static_assert(UE_ARRAY_COUNT(ReturnRates) == UE_ARRAY_COUNT(ExpansionStrikes));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ExpansionStrikes); ++Index)
	{
		const auto& Strike = ExpansionStrikes[Index];
		const FString Name = Strike.Name;
		const auto* Node = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.") + Name));
		auto* Clip = Asset<UAnimSequence>(ExpansionFolder / (TEXT("AS_Longblade_") + Name));
		if (!Node || !Node->AttackDefinition || !Node->AttackDefinition->Montage || !Clip) { return false; }
		auto& Attack = *Node->AttackDefinition;
		auto& Montage = *Attack.Montage;
		const bool bBaked = Clip->HasRootMotion() && Clip->bForceRootLock;
		if (Montage.SlotAnimTracks.Num() != 1 || (!bBaked && !BakeAttackTravel(*Clip))) { return false; }
		auto& Segments = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments;
		if (Segments.IsEmpty() || !Segments.ContainsByPredicate([Clip](const FAnimSegment& Segment)
			{ return Segment.GetAnimReference() == Clip; })) { return false; }
		if (Strike.OpeningEnd > 0.f && Segments.Num() == 2)
		{
			if (Segments[0].GetAnimReference() != Clip || Segments[1].GetAnimReference() != Clip
				|| !FMath::IsNearlyEqual(Segments[1].AnimStartTime, Strike.OpeningEnd)) { return false; }
			Segments.SetNum(1);
			Append(Montage, *Clip, HoldEnds[Index], 8.f, Strike.OpeningEnd);
			if (Name == TEXT("Twin1"))
			{
				Append(Montage, *Clip, 3.14f, ReturnRates[Index], HoldEnds[Index]);
				Append(Montage, *Clip, 4.88f, 8.f, 3.14f);
				Append(Montage, *Clip, Clip->GetPlayLength(), ReturnRates[Index], 4.88f);
			}
			else
			{
				Append(Montage, *Clip, Clip->GetPlayLength(), ReturnRates[Index], HoldEnds[Index]);
			}
		}
		// Mover samples an active montage. Finish the last planted step before
		// natural blend-out removes that montage from the active slot.
		Montage.BlendOutTriggerTime = 0.f;
		UpdateMontage(Montage);
		UseMontageAttackTravel(Attack);
		Assets.Append({Clip, &Montage, &Attack});
	}

	for (int32 Index = 1; Index <= 4; ++Index)
	{
		const auto* Node = Style.ComboDefinition->FindNode(Tag(FString::Printf(TEXT("Combo.Spellblade.Flurry.%d"), Index)));
		if (!Node || !Node->AttackDefinition || !Node->AttackDefinition->Montage) { return false; }
		auto& Attack = *Node->AttackDefinition;
		auto& Montage = *Attack.Montage;
		if (Index == 4 && !Montage.HasRootMotion())
		{
			// Keep the preview takes available. The two active segments share an original
			// exit/entry pose, so their per-clip root deltas add across the seam.
			if (Montage.SlotAnimTracks.Num() != 1) { return false; }
			auto& Segments = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments;
			if (Segments.Num() != 2) { return false; }
			for (int32 Segment = 0; Segment < Segments.Num(); ++Segment)
			{
				auto* Source = Cast<UAnimSequence>(Segments[Segment].GetAnimReference());
				auto* Clip = Duplicate(Source, ExpansionFolder / (Segment == 0
					? TEXT("AS_Longblade_Flurry4Travel") : TEXT("AS_Longblade_Flurry4ReturnTravel")));
				if (!Clip || !BakeAttackTravel(*Clip)) { return false; }
				Segments[Segment].SetAnimReference(Clip, false);
				Assets.Add(Clip);
			}
		}
		if (Index == 4)
		{
			Montage.BlendOutTriggerTime = 0.f;
			UpdateMontage(Montage);
			Assets.Add(&Montage);
		}
		if (!Montage.HasRootMotion()) { return false; }
		UseMontageAttackTravel(Attack);
		Assets.Add(&Attack);
	}
	return true;
}

bool VerifyDowncutTravel(const UProject_JCombatStyleDefinition& Style)
{
	const auto* Node = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.Air.DownCut")));
	if (!Node || !Node->AttackDefinition || !Node->AttackDefinition->Montage) { return false; }
	const auto& Attack = *Node->AttackDefinition;
	const auto& Montage = *Attack.Montage;
	const auto* Vertical = Montage.FindMetaDataByClass<UProject_JMontageVerticalMotion>();
	if (!Vertical || Vertical->EndTime <= 0.f || Vertical->EndTime >= Montage.GetPlayLength()
		|| !Montage.HasRootMotion() || Attack.bUseFlyingMovementModeForRootMotion
		|| Attack.MovementPolicy != EProject_JAttackMovementPolicy::RootMotionMontage) { return false; }

	const auto Descent = Montage.ExtractRootMotionFromTrackRange(0.f, Vertical->EndTime, FAnimExtractContext());
	const auto Recovery = Montage.ExtractRootMotionFromTrackRange(Vertical->EndTime, Montage.GetPlayLength(), FAnimExtractContext());
	const FVector Travel = Descent.GetTranslation();
	if (Travel.Z >= -90. || Travel.Z <= -115. || Travel.Size2D() > .1 || !Descent.GetRotation().IsIdentity(.001)
		|| !Recovery.GetTranslation().IsNearlyZero(.1) || !Recovery.GetRotation().IsIdentity(.001)) { return false; }
	UE_LOG(LogTemp, Display, TEXT("SpellbladeDowncutTravel Z=%.2fcm End=%.3fs Recovery=%s"),
		Travel.Z, Vertical->EndTime, *Recovery.GetTranslation().ToCompactString());
	return true;
}

bool ApplyDowncutTravel(const UProject_JCombatStyleDefinition& Style)
{
	const auto* Node = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.Air.DownCut")));
	if (!Node || !Node->AttackDefinition || !Node->AttackDefinition->Montage) { return false; }
	auto& Attack = *Node->AttackDefinition;
	auto& Montage = *Attack.Montage;
	if (Montage.FindMetaDataByClass<UProject_JMontageVerticalMotion>()) { return VerifyDowncutTravel(Style); }
	if (Montage.SlotAnimTracks.Num() != 1 || Montage.SlotAnimTracks[0].SlotName != TEXT("UpperBody")) { return false; }
	auto& Segments = Montage.SlotAnimTracks[0].AnimTrack.AnimSegments;
	if (Segments.Num() != 2 || Segments[0].GetAnimReference() != Segments[1].GetAnimReference()) { return false; }
	auto* Source = Cast<UAnimSequence>(Segments[0].GetAnimReference());
	auto* Clip = Duplicate(Source, ExpansionFolder / TEXT("AS_Longblade_DownCutVertical"));
	constexpr float DescentEnd = .2f;
	if (!Clip || !BakeAttackTravel(*Clip, DescentEnd)) { return false; }
	for (auto& Segment : Segments) { Segment.SetAnimReference(Clip, false); }
	auto* Vertical = NewObject<UProject_JMontageVerticalMotion>(&Montage);
	Vertical->EndTime = DescentEnd;
	Montage.AddMetaData(Vertical);
	UpdateMontage(Montage);
	UseMontageAttackTravel(Attack);
	return VerifyDowncutTravel(Style) && Save(Clip) && Save(&Montage) && Save(&Attack);
}

bool VerifyAttackTravel(const UProject_JCombatStyleDefinition& Style)
{
	for (const auto& Node : Style.ComboDefinition->Nodes)
	{
		const FString Name = Node.NodeTag.ToString();
		const auto* Attack = Node.AttackDefinition.Get();
		if (!Attack || !Attack->Montage) { return false; }
		const auto& Montage = *Attack->Montage;
		if (Montage.HasRootMotion() != (Attack->MovementPolicy == EProject_JAttackMovementPolicy::RootMotionMontage))
		{
			UE_LOG(LogTemp, Error, TEXT("SpellbladeTravel policy mismatch %s"), *Name);
			return false;
		}
		const bool bExpansion = Name.Contains(TEXT(".Bash")) || Name.Contains(TEXT(".Twin")) || Name.EndsWith(TEXT(".GroundGush"));
		if (!bExpansion && !Name.Contains(TEXT(".Flurry."))) { continue; }
		if (!Montage.HasRootMotion() || Attack->bUseFlyingMovementModeForRootMotion) { return false; }
		if ((bExpansion || Name.EndsWith(TEXT(".Flurry.4"))) && Montage.BlendOutTriggerTime != 0.f) { return false; }
		double Peak = 0.;
		FVector End;
		for (int32 Sample = 1; Sample <= 100; ++Sample)
		{
			const auto Delta = Montage.ExtractRootMotionFromTrackRange(0.f, Montage.GetPlayLength() * Sample / 100.f, FAnimExtractContext());
			End = Delta.GetTranslation();
			if (!FMath::IsNearlyZero(End.Z, .1) || !Delta.GetRotation().IsIdentity(.001)) { return false; }
			Peak = FMath::Max(Peak, End.Size2D());
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeTravelVerified %s Peak=%.2fcm End=%s Duration=%.2f"),
			*Name, Peak, *End.ToCompactString(), Montage.GetPlayLength());
		if (Peak < 4.) { return false; }
	}
	return true;
}
