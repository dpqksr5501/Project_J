const FString StanceFolder = Sequences + TEXT("Locomotion/Longblade/");
constexpr TCHAR GuardPrefix[] = TEXT("AS_LongbladeStance_");

struct FStanceTake
{
	const TCHAR* Name;
	const TCHAR* Source;
};

constexpr FStanceTake StanceTakes[] = {
	{TEXT("Idle00"), TEXT("stand_idle_00")}, {TEXT("Idle01"), TEXT("stand_idle_01")},
	{TEXT("Idle02"), TEXT("stand_idle_02")}, {TEXT("Idle03"), TEXT("stand_idle_03")},
	{TEXT("Forward"), TEXT("move_run_f_00")}, {TEXT("Backward"), TEXT("move_run_b_00")},
	{TEXT("Left"), TEXT("move_run_l_00")}, {TEXT("Right"), TEXT("move_run_r_00")}
};

constexpr FStanceTake GuardTakes[] = {
	{TEXT("Ready"), TEXT("stand_idle_00")}, {TEXT("Forward"), TEXT("move_run_f_00")},
	{TEXT("Backward"), TEXT("move_run_b_00")}, {TEXT("Left"), TEXT("move_run_l_00")},
	{TEXT("Right"), TEXT("move_run_r_00")}
};

constexpr FStanceTake CastTakes[] = {
	{TEXT("Buff"), TEXT("01_01_att_skill_buff_00")},
	{TEXT("CastLeft"), TEXT("05_00_att_skill_stand_magic_l_00")},
	{TEXT("CastRight"), TEXT("05_00_att_skill_stand_magic_r_00")}
};

constexpr FStanceTake LongbladeActionTakes[] = {
	{TEXT("Draw"), TEXT("stand_weapon_out_00")},
	{TEXT("Sheathe"), TEXT("stand_weapon_in_00")},
	{TEXT("BasicCut01"), TEXT("01_01_att_normal_01")},
	{TEXT("BasicCut02"), TEXT("01_01_att_normal_02")},
	{TEXT("BasicCut03Ground"), TEXT("01_01_att_normal_r_02")},
	{TEXT("MovementDash"), TEXT("01_01_att_move_dash_ing_01")}
};

FString GuardSampleName(const FVector& Point)
{
	if (FMath::IsNearlyZero(Point.Y)) { return FString(GuardPrefix) + GuardTakes[0].Name; }
	const int32 Sector = FMath::RoundToInt(Point.X / 90.);
	const int32 Index = FMath::Abs(Sector) == 2 ? 2 : Sector == -1 ? 3 : Sector == 1 ? 4 : 1;
	return FString(GuardPrefix) + GuardTakes[Index].Name;
}

void InspectArmTwist(const UAnimSequence& Clip, USkeletalMesh& Mesh, bool bLegacy)
{
	const FReferenceSkeleton& Reference = Mesh.GetRefSkeleton();
	TArray<FTransform> Ref = Reference.GetRawRefBonePose();
	for (int32 Index = 1; Index < Ref.Num(); ++Index)
	{
		Ref[Index] *= Ref[Reference.GetParentIndex(Index)];
	}
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Mesh;
	Options.bShouldRetarget = false;
	for (int32 Sample = 0; Sample <= 8; ++Sample)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Clip.GetPlayLength() * Sample / 8., Options, Pose);
		if (!Pose.IsValid()) { return; }
		for (const bool bRight : {false, true})
		{
			const FName Shoulder(bLegacy ? (bRight ? TEXT("Bip01-R-UpperArm") : TEXT("Bip01-L-UpperArm")) : (bRight ? TEXT("upperarm_r") : TEXT("upperarm_l")));
			const FName Elbow(bLegacy ? (bRight ? TEXT("Bip01-R-Forearm") : TEXT("Bip01-L-Forearm")) : (bRight ? TEXT("lowerarm_r") : TEXT("lowerarm_l")));
			const FName Hand(bLegacy ? (bRight ? TEXT("Bip01-R-Hand") : TEXT("Bip01-L-Hand")) : (bRight ? TEXT("hand_r") : TEXT("hand_l")));
			const int32 A = Reference.FindBoneIndex(Shoulder), B = Reference.FindBoneIndex(Elbow), C = Reference.FindBoneIndex(Hand);
			if (!Ref.IsValidIndex(A) || !Ref.IsValidIndex(B) || !Ref.IsValidIndex(C)) { return; }
			const FTransform Upper = UAnimPoseExtensions::GetBonePose(Pose, Shoulder, EAnimPoseSpaces::World);
			const FTransform Lower = UAnimPoseExtensions::GetBonePose(Pose, Elbow, EAnimPoseSpaces::World);
			const FTransform Wrist = UAnimPoseExtensions::GetBonePose(Pose, Hand, EAnimPoseSpaces::World);
			const FVector RefUpper = Ref[B].GetLocation() - Ref[A].GetLocation();
			const FVector RefLower = Ref[C].GetLocation() - Ref[B].GetLocation();
			const FVector Forearm = (Wrist.GetLocation() - Lower.GetLocation()).GetSafeNormal();
			const FQuat SwingUpper = FQuat::FindBetweenVectors(RefUpper, Lower.GetLocation() - Upper.GetLocation());
			const FQuat Swing = FQuat::FindBetweenVectors(SwingUpper.RotateVector(RefLower), Forearm) * SwingUpper;
			const FQuat Roll = Lower.GetRotation() * (Swing * Ref[B].GetRotation()).Inverse();
			const FQuat WristDelta = Wrist.GetRelativeTransform(Lower).GetRotation() * Ref[C].GetRelativeTransform(Ref[B]).GetRotation().Inverse();
			const auto SignedTwist = [](const FQuat& Rotation, const FVector& Axis)
			{
				return FMath::UnwindDegrees(FMath::RadiansToDegrees(2. * FMath::Atan2(FVector::DotProduct(FVector(Rotation.X, Rotation.Y, Rotation.Z), Axis), Rotation.W)));
			};
			UE_LOG(LogTemp, Display, TEXT("SpellbladeArmAudit %s %s %s %d forearm=%.1f wrist=%.1f"),
				*Clip.GetName(), *Mesh.GetName(), bRight ? TEXT("R") : TEXT("L"), Sample,
				SignedTwist(Roll, Forearm), SignedTwist(WristDelta, Ref[B].GetRotation().UnrotateVector(RefLower.GetSafeNormal())));
		}
	}
}

void InspectStancePose(UAnimSequence& Clip, USkeletalMesh& Mesh, bool bLegacy)
{
	FAnimPoseEvaluationOptions Options;
	Options.bShouldRetarget = false;
	Options.OptionalSkeletalMesh = &Mesh;
	for (int32 Sample = 0; Sample < 5; ++Sample)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Clip.GetPlayLength() * Sample / 4., Options, Pose);
		if (!Pose.IsValid()) { return; }
		const auto Read = [&Pose](FName Name) { return UAnimPoseExtensions::GetBonePose(Pose, Name, EAnimPoseSpaces::World); };
		const auto Chest = Read(bLegacy ? TEXT("Bip01-Spine2") : TEXT("spine_05"));
		const auto Hand = Read(bLegacy ? TEXT("Bip01-R-Hand") : TEXT("hand_r"));
		const auto Elbow = Read(bLegacy ? TEXT("Bip01-R-Forearm") : TEXT("lowerarm_r"));
		const auto Shoulder = Read(bLegacy ? TEXT("Bip01-R-UpperArm") : TEXT("upperarm_r"));
		const auto Other = Read(bLegacy ? TEXT("Bip01-L-Hand") : TEXT("hand_l"));
		const auto Pos = [&Chest](const FTransform& Bone) { return Chest.InverseTransformPosition(Bone.GetLocation()).ToCompactString(); };
		UE_LOG(LogTemp, Display, TEXT("SpellbladeStance %s %s sample=%d Hand=%s Elbow=%s Shoulder=%s Left=%s Wrist=%s"),
			*Clip.GetName(), *Mesh.GetName(), Sample, *Pos(Hand), *Pos(Elbow), *Pos(Shoulder), *Pos(Other),
			*Hand.GetRelativeTransform(Elbow).GetRotation().Rotator().ToCompactString());
	}
}

bool VerifyStance(const UBlendSpace& Blend)
{
	const auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	if (!Body || Blend.GetBlendSamples().Num() != 10) { return false; }
	const UAnimSequence* Idle = nullptr;
	for (const auto& Sample : Blend.GetBlendSamples())
	{
		const bool bIdle = FMath::IsNearlyZero(Sample.SampleValue.Y);
		if (!Sample.Animation || Sample.Animation->GetSkeleton() != Body->GetSkeleton()
			|| Sample.Animation->HasRootMotion()
			|| (!bIdle && !FMath::IsNearlyEqual(Sample.SampleValue.Y, 500.))
			|| Sample.Animation->GetName() != GuardSampleName(Sample.SampleValue))
		{
			UE_LOG(LogTemp, Display, TEXT("Spellblade stance invalid sample %s at speed %.1f (skeleton=%s root motion=%d)"),
				*GetNameSafe(Sample.Animation.Get()), Sample.SampleValue.Y,
				*GetNameSafe(Sample.Animation ? Sample.Animation->GetSkeleton() : nullptr),
				Sample.Animation && Sample.Animation->HasRootMotion());
			return false;
		}
		if (bIdle) { Idle = Sample.Animation; }
	}
	if (!Idle) { return false; }
	for (const FName Bone : {FName(TEXT("clavicle_r")), FName(TEXT("upperarm_r")), FName(TEXT("lowerarm_r")), FName(TEXT("hand_r")),
		FName(TEXT("clavicle_l")), FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l"))})
	{
		TArray<FTransform> IdleKeys;
		Idle->GetDataModel()->GetBoneTrackTransforms(Bone, IdleKeys);
		if (IdleKeys.IsEmpty()) { return false; }
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeStanceVerified Body=%s Samples=10"), *Body->GetName());
	return true;
}

bool TrialArmJointMapping(UIKRetargeter& Rig, const USkeletalMesh& Legacy, const USkeletalMesh& Body)
{
	const auto SourceSide = ERetargetSourceOrTarget::Source;
	const auto TargetSide = ERetargetSourceOrTarget::Target;
	const auto* SourceRig = Rig.GetIKRig(SourceSide);
	const auto* TargetRig = Rig.GetIKRig(TargetSide);
	auto* Op = Rig.GetFirstRetargetOpOfType<FIKRetargetFKChainsOp>();
	if (!Op || !SourceRig || !TargetRig) { return false; }

	const auto JointCount = [](const FReferenceSkeleton& Reference, const FBoneChain& Chain)
	{
		const int32 Start = Reference.FindBoneIndex(Chain.StartBone.BoneName);
		int32 Count = 1;
		for (int32 Index = Reference.FindBoneIndex(Chain.EndBone.BoneName); Index != INDEX_NONE; Index = Reference.GetParentIndex(Index))
		{
			if (Index == Start) { return Count; }
			++Count;
		}
		return 0;
	};

	int32 Matched = 0;
	for (FRetargetFKChainSettings& Settings : Op->Settings.ChainsToRetarget)
	{
		const auto* Target = TargetRig->GetRetargetChains().FindByPredicate([&](const FBoneChain& Chain)
		{
			return Chain.ChainName == Settings.TargetChainName;
		});
		if (!Target) { continue; }
		const FName Start = Target->StartBone.BoneName;
		if (Start != TEXT("upperarm_l") && Start != TEXT("upperarm_r")
			&& Start != TEXT("lowerarm_l") && Start != TEXT("lowerarm_r")) { continue; }
		const FName SourceName = Op->Settings.ChainMapping.GetChainMappedTo(Settings.TargetChainName, TargetSide);
		const auto* Source = SourceRig->GetRetargetChains().FindByPredicate([&](const FBoneChain& Chain)
		{
			return Chain.ChainName == SourceName;
		});
		if (!Source) { continue; }
		const int32 SourceCount = JointCount(Legacy.GetRefSkeleton(), *Source);
		const int32 TargetCount = JointCount(Body.GetRefSkeleton(), *Target);
		UE_LOG(LogTemp, Display, TEXT("SpellbladeArmChain %s %s..%s (%d) -> %s %s..%s (%d) mode=%d"),
			*SourceName.ToString(), *Source->StartBone.BoneName.ToString(), *Source->EndBone.BoneName.ToString(), SourceCount,
			*Settings.TargetChainName.ToString(), *Start.ToString(), *Target->EndBone.BoneName.ToString(), TargetCount,
			int32(Settings.RotationMode));
		if (SourceCount > 1 && SourceCount == TargetCount)
		{
			Settings.RotationMode = EFKChainRotationMode::OneToOne;
			++Matched;
		}
	}
	return Matched == 2;
}

TArray<UAnimSequence*> RetargetStanceTakes(const FString& Params, TConstArrayView<FStanceTake> Takes,
	const TCHAR* Prefix, bool bApply, const FString& OutputFolder = StanceFolder, UAnimSequence** FirstSource = nullptr,
	TArray<UAnimSequence*>* SourceClips = nullptr)
{
	FString SourceFolder;
	if (!FParse::Value(*Params, TEXT("FbxRoot="), SourceFolder)) { return {}; }
	auto* Legacy = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Spellblade_Woman"));
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	auto* Rig = Asset<UIKRetargeter>(TEXT("/Game/Characters/Spellblade/Animation/CombatKit/RTG_SpellbladeLegacy_to_DirectKit"));
	if (!Legacy || !Body || !Rig) { return {}; }
	if (!bApply && FParse::Param(*Params, TEXT("TrialResetArmAlignment")))
	{
		Rig = DuplicateObject<UIKRetargeter>(Rig, GetTransientPackage());
		auto* Controller = UIKRetargeterController::GetController(Rig);
		const auto Side = ERetargetSourceOrTarget::Target;
		const TArray<FName> Bones = {TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
			TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")};
		for (const FName Bone : Bones)
		{
			const FQuat Delta = Controller->GetRotationOffsetForRetargetPoseBone(Bone, Side);
			UE_LOG(LogTemp, Display, TEXT("SpellbladeArmAlignment %s %s"), *Bone.ToString(), *Delta.Rotator().ToCompactString());
		}
		Controller->ResetRetargetPose(Rig->GetCurrentRetargetPoseName(Side), Bones, Side);
	}
	if (!bApply && FParse::Param(*Params, TEXT("TrialArmJointMapping")))
	{
		Rig = DuplicateObject<UIKRetargeter>(Rig, GetTransientPackage());
		if (!TrialArmJointMapping(*Rig, *Legacy, *Body)) { return {}; }
	}
	FIKRetargetBatchOperationInputs Retarget;
	Retarget.SourceMesh = Legacy;
	Retarget.TargetMesh = Body;
	Retarget.IKRetargetAsset = Rig;
	Retarget.TargetPath = bApply ? OutputFolder : Sequences + (FParse::Param(*Params, TEXT("BakeArmRollTrial"))
		? TEXT("SourceTransient/ArmRollTrial") : TEXT("SourceTransient/StanceAudit"));
	Retarget.Prefix = Prefix;
	Retarget.bIncludeReferencedAssets = false;
	TArray<UAssetImportTask*> Tasks;
	for (const auto& Take : Takes)
	{
		const FString Filename = SourceFolder / (FString(TEXT("pdw_")) + (FChar::IsDigit(Take.Source[0]) ? TEXT("") : TEXT("01_01_")) + Take.Source + TEXT(".fbx"));
		if (!FPaths::FileExists(Filename)) { return {}; }
		auto* Task = NewObject<UAssetImportTask>();
		auto* Options = NewObject<UFbxImportUI>(Task);
		Options->bAutomatedImportShouldDetectType = false;
		Options->MeshTypeToImport = FBXIT_Animation;
		Options->bImportAsSkeletal = true;
		Options->bImportMesh = false;
		Options->bImportAnimations = true;
		Options->Skeleton = Legacy->GetSkeleton();
		Task->Filename = Filename;
		Task->DestinationName = Take.Name;
		Task->DestinationPath = Sequences + TEXT("SourceTransient/StanceOriginal");
		Task->Factory = NewObject<UFbxFactory>(Task);
		Task->Options = Options;
		Task->bAutomated = true;
		Task->bSave = false;
		Tasks.Add(Task);
	}
	FAssetToolsModule::GetModule().Get().ImportAssetTasks(Tasks);
	for (const auto* Task : Tasks)
	{
		const auto& Objects = Task->GetObjects();
		auto* Original = Objects.Num() == 1 ? Cast<UAnimSequence>(Objects[0]) : nullptr;
		if (!Original || Original->GetSkeleton() != Legacy->GetSkeleton()) { return {}; }
		if (FirstSource && Retarget.AssetsToRetarget.IsEmpty()) { *FirstSource = Original; }
		if (SourceClips) { SourceClips->Add(Original); }
		if (!bApply) { InspectArmTwist(*Original, *Legacy, true); }
		Retarget.AssetsToRetarget.Emplace(Original);
	}
	const auto Outputs = UIKRetargetBatchOperation::RunBatchRetarget(Retarget);
	if (Outputs.Num() != Tasks.Num()) { return {}; }
	TArray<UAnimSequence*> Clips;
	for (const auto& Output : Outputs)
	{
		auto* Clip = Cast<UAnimSequence>(Output.GetAsset());
		if (!Clip || Clip->GetSkeleton() != Body->GetSkeleton()) { return {}; }
		if (!bApply) { InspectArmTwist(*Clip, *Body, false); }
		Clips.Add(Clip);
	}
	return Clips;
}

bool BakeArmRollTrial(UAnimSequence& Clip, USkeletalMesh& Body)
{
	const auto& Skeleton = Body.GetRefSkeleton();
	TArray<FTransform> Reference = Skeleton.GetRawRefBonePose();
	for (int32 Index = 1; Index < Reference.Num(); ++Index)
	{
		Reference[Index] *= Reference[Skeleton.GetParentIndex(Index)];
	}

	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Body;
	Options.bShouldRetarget = false;
	const int32 Count = Clip.GetDataModel()->GetNumberOfKeys();
	if (Count < 2) { return false; }

	for (const bool bRight : {false, true})
	{
		const FName UpperName(bRight ? TEXT("upperarm_r") : TEXT("upperarm_l"));
		const FName LowerName(bRight ? TEXT("lowerarm_r") : TEXT("lowerarm_l"));
		const FName HandName(bRight ? TEXT("hand_r") : TEXT("hand_l"));
		const int32 UpperIndex = Skeleton.FindBoneIndex(UpperName), LowerIndex = Skeleton.FindBoneIndex(LowerName);
		const int32 HandIndex = Skeleton.FindBoneIndex(HandName);
		if (UpperIndex == INDEX_NONE || LowerIndex == INDEX_NONE || HandIndex == INDEX_NONE
			|| Skeleton.GetParentIndex(LowerIndex) != UpperIndex || Skeleton.GetParentIndex(HandIndex) != LowerIndex)
		{
			return false;
		}
		const FTransform& RefUpper = Reference[UpperIndex];
		const FTransform& RefLower = Reference[LowerIndex];
		const FTransform& RefHand = Reference[HandIndex];
		const FVector UpperAxis = RefLower.GetLocation() - RefUpper.GetLocation();
		const FVector LowerAxis = RefHand.GetLocation() - RefLower.GetLocation();
		const FVector RefWristAxis = RefLower.GetRotation().UnrotateVector(LowerAxis.GetSafeNormal());
		const FQuat RefWrist = RefHand.GetRelativeTransform(RefLower).GetRotation();
		TArray<FTransform> LowerKeys, HandKeys;
		TArray<FTransform> OriginalHands;
		double MaxCorrection = 0.;
		for (int32 Key = 0; Key < Count; ++Key)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Clip.GetPlayLength() * Key / (Count - 1), Options, Pose);
			if (!Pose.IsValid()) { return false; }
			const FTransform Upper = UAnimPoseExtensions::GetBonePose(Pose, UpperName, EAnimPoseSpaces::World);
			const FTransform Lower = UAnimPoseExtensions::GetBonePose(Pose, LowerName, EAnimPoseSpaces::World);
			const FTransform Hand = UAnimPoseExtensions::GetBonePose(Pose, HandName, EAnimPoseSpaces::World);
			const FVector Axis = (Hand.GetLocation() - Lower.GetLocation()).GetSafeNormal();
			const FQuat UpperSwing = FQuat::FindBetweenVectors(UpperAxis, Lower.GetLocation() - Upper.GetLocation());
			const FQuat Swing = FQuat::FindBetweenVectors(UpperSwing.RotateVector(LowerAxis), Axis) * UpperSwing;
			const auto Twist = [](const FQuat& Rotation, const FVector& Direction)
			{
				return FMath::UnwindDegrees(FMath::RadiansToDegrees(2. * FMath::Atan2(
					FVector::DotProduct(FVector(Rotation.X, Rotation.Y, Rotation.Z), Direction), Rotation.W)));
			};
			const double Forearm = Twist(Lower.GetRotation() * (Swing * RefLower.GetRotation()).Inverse(), Axis);
			const double Wrist = Twist(Hand.GetRelativeTransform(Lower).GetRotation() * RefWrist.Inverse(), RefWristAxis);
			const double Paired = FMath::Min(FMath::Abs(Forearm), FMath::Abs(Wrist));
			// Only redistribute opposed roll. The authored hand transform owns weapon contact.
			const double Correction = Forearm * Wrist < 0. ? -FMath::Sign(Forearm) * FMath::Max(0., Paired - 55.) : 0.;
			FTransform NewLower = Lower;
			NewLower.SetRotation((FQuat(Axis, FMath::DegreesToRadians(Correction)) * Lower.GetRotation()).GetNormalized());
			LowerKeys.Add(NewLower.GetRelativeTransform(Upper));
			HandKeys.Add(Hand.GetRelativeTransform(NewLower));
			OriginalHands.Add(Hand);
			MaxCorrection = FMath::Max(MaxCorrection, FMath::Abs(Correction));
		}
		if (MaxCorrection > 0. && (!SetTrack(Clip, LowerName, LowerKeys) || !SetTrack(Clip, HandName, HandKeys))) { return false; }
		for (int32 Key = 0; Key < Count; ++Key)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Clip.GetPlayLength() * Key / (Count - 1), Options, Pose);
			if (!Pose.IsValid()) { return false; }
			const FTransform Hand = UAnimPoseExtensions::GetBonePose(Pose, HandName, EAnimPoseSpaces::World);
			if (!Hand.Equals(OriginalHands[Key], .05)) { return false; }
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeArmTrial %s %s redistributed=%.1f"), *Clip.GetName(), *HandName.ToString(), MaxCorrection);
	}
	return true;
}

bool AuditSharedArmRetarget(const FString& Params)
{
	const FStanceTake Attacks[] = {
		{TEXT("Light01"), TEXT("05_01_att_skill_comboatt_01")},
		{TEXT("Light02"), TEXT("05_01_att_skill_comboatt_02")},
		{TEXT("Light03"), TEXT("05_01_att_skill_comboatt_03")},
		{TEXT("RightAttack"), TEXT("01_01_att_normal_stand_01")},
		{TEXT("QuickSkill"), TEXT("01_01_att_skill_crossatt_01")},
		{TEXT("ChargeRelease"), TEXT("01_01_att_skill_charging_att_00")},
		{TEXT("Finisher"), TEXT("01_01_att_skill_finish_00")},
		{TEXT("Draw"), TEXT("stand_weapon_out_00")}, {TEXT("Sheathe"), TEXT("stand_weapon_in_00")}
	};
	TArray<FStanceTake> Takes;
	for (const FStanceTake& Take : StanceTakes) { Takes.Add(Take); }
	for (const FStanceTake& Take : Attacks) { Takes.Add(Take); }
	const auto Clips = RetargetStanceTakes(Params, Takes, TEXT("ArmAudit_"), false);
	if (Clips.IsEmpty()) { return false; }
	if (FParse::Param(*Params, TEXT("BakeArmRollTrial")))
	{
		auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
		if (!Body) { return false; }
		for (UAnimSequence* Clip : Clips)
		{
			if (!BakeArmRollTrial(*Clip, *Body)) { return false; }
			InspectArmTwist(*Clip, *Body, false);
		}
		// Only the separate trial sequences may be persisted. No montage or blend space references them.
		for (UAnimSequence* Clip : Clips)
		{
			if (!Save(Clip)) { return false; }
		}
	}
	return true;
}

bool SolveReadyArm(const FTransform& OldUpper, const FTransform& OldForearm, const FTransform& OldWrist,
	const FTransform& DesiredWrist, FTransform& Upper, FTransform& Forearm, const FVector* PreferredElbow = nullptr)
{
	const FVector Shoulder = OldUpper.GetLocation();
	const FVector UpperDirection = OldForearm.GetLocation() - Shoulder;
	const FVector LowerDirection = OldWrist.GetLocation() - OldForearm.GetLocation();
	const FVector Reach = DesiredWrist.GetLocation() - Shoulder;
	const FVector Axis = Reach.GetSafeNormal();
	const FVector Pole = FVector::VectorPlaneProject(PreferredElbow ? *PreferredElbow : UpperDirection, Axis).GetSafeNormal();
	const double A = UpperDirection.Length(), B = LowerDirection.Length(), Length = Reach.Length();
	if (A < 1. || B < 1. || Pole.IsNearlyZero() || Length < FMath::Abs(A - B) + .01 || Length > A + B - .01)
	{
		return false;
	}
	const double Along = (A * A - B * B + Length * Length) / (2. * Length);
	const FVector NewUpper = Axis * Along + Pole * FMath::Sqrt(FMath::Max(0., A * A - Along * Along));
	const FVector NewLower = Reach - NewUpper;
	const FQuat UpperSwing = FQuat::FindBetweenVectors(UpperDirection, NewUpper);
	const FQuat LowerSwing = FQuat::FindBetweenVectors(UpperSwing.RotateVector(LowerDirection), NewLower) * UpperSwing;
	Upper = FTransform((UpperSwing * OldUpper.GetRotation()).GetNormalized(), Shoulder, OldUpper.GetScale3D());
	Forearm = FTransform((LowerSwing * OldForearm.GetRotation()).GetNormalized(), Shoulder + NewUpper, OldForearm.GetScale3D());
	return true;
}

const UProject_JRetargetAnimInstance* StanceBodyDefaults()
{
	const auto* Class = LoadClass<UProject_JRetargetAnimInstance>(nullptr,
		TEXT("/Game/Characters/Spellblade/Animation/ABP_Spellblade_Body.ABP_Spellblade_Body_C"));
	return Class ? Class->GetDefaultObject<UProject_JRetargetAnimInstance>() : nullptr;
}

FName StancePrimaryGripName()
{
	const auto* Item = Asset<UProject_JEquipmentItemDefinition>(SpellbladeDataRoot + TEXT("Animation_Profiles/Equip/DA_Spellblade_Equip"));
	const auto* Presentation = Item ? Item->WeaponPresentationProfile.Get() : nullptr;
	return Presentation && Presentation->DrawnAttachmentMode == EProject_JDrawnAttachmentMode::PrimaryGripContact
		? Presentation->MotionPresentation.PrimaryGripSocketName : NAME_None;
}

bool ResolveStanceWeaponMount(const USkeletalMesh& Body, const USkeletalMesh& Weapon, FTransform& WeaponInRightHand)
{
	const auto* Defaults = StanceBodyDefaults();
	const auto* Profile = Defaults ? Defaults->HandGripProfile.Get() : nullptr;
	if (!Profile) { return false; }
	const auto* Palm = Body.FindSocket(Profile->Calibration.PrimaryPalmSocketName);
	const auto* Grip = Weapon.FindSocket(StancePrimaryGripName());
	// The stance baker consumes root-bone-local sockets, so this mount maps
	// the weapon bone, rather than the mesh component, into the hand.
	return Palm && Grip && Palm->BoneName == TEXT("hand_r") && Grip->BoneName == Weapon.GetRefSkeleton().GetBoneName(0)
		&& Project_J::Animation::MakePrimaryGripAttachment(Grip->GetSocketLocalTransform(), Palm->GetSocketLocalTransform(),
			Profile->Calibration.PrimaryHandOffset, WeaponInRightHand);
}

bool FitReadyGrip(UAnimSequence& Ready, UAnimSequence& Original)
{
	auto* Legacy = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Spellblade_Woman"));
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	if (!Legacy || !Body || !Weapon) { return false; }
	const auto* PalmL = Body->FindSocket(TEXT("PalmGrip_L"));
	const auto* GripL = Weapon->FindSocket(TEXT("WeaponGrip_L"));
	const FName Right(TEXT("hand_r")), RightForearm(TEXT("lowerarm_r")), RightUpper(TEXT("upperarm_r"));
	const FName Left(TEXT("hand_l")), LeftForearm(TEXT("lowerarm_l")), LeftUpper(TEXT("upperarm_l"));
	if (!PalmL || !GripL || PalmL->BoneName != Left || GripL->BoneName != Weapon->GetRefSkeleton().GetBoneName(0)) { return false; }
	FTransform WeaponInRightHand;
	if (!ResolveStanceWeaponMount(*Body, *Weapon, WeaponInRightHand)) { return false; }
	const FTransform LeftGripInWeapon = GripL->GetSocketLocalTransform();
	const FTransform LeftPalmInWrist = PalmL->GetSocketLocalTransform();

	FAnimPoseEvaluationOptions SourceOptions, TargetOptions;
	SourceOptions.OptionalSkeletalMesh = Legacy;
	TargetOptions.OptionalSkeletalMesh = Body;
	SourceOptions.bShouldRetarget = TargetOptions.bShouldRetarget = false;
	const int32 Count = Ready.GetDataModel()->GetNumberOfKeys();
	if (Count < 2 || !FMath::IsNearlyEqual(Ready.GetPlayLength(), Original.GetPlayLength(), .001)) { return false; }
	const auto World = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
	};
	const auto PalmBasis = [&World](const FAnimPose& Pose, FName Hand, FName Index, FName Middle, FName Pinky)
	{
		return FRotationMatrix::MakeFromXY(World(Pose, Middle).GetLocation() - World(Pose, Hand).GetLocation(),
			World(Pose, Index).GetLocation() - World(Pose, Pinky).GetLocation()).ToQuat();
	};
	TArray<FTransform> RightKeys, RightForearmKeys, RightUpperKeys, LeftKeys, LeftForearmKeys, LeftUpperKeys;
	double BeforeGap = 0., AfterGap = 0., MaxGripShift = 0.;
	for (int32 Key = 0; Key < Count; ++Key)
	{
		FAnimPose Src, Dst;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Original, Ready.GetPlayLength() * Key / (Count - 1), SourceOptions, Src);
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Ready, Key, TargetOptions, Dst);
		if (!Src.IsValid() || !Dst.IsValid()) { return false; }

		const FQuat SourcePalm = PalmBasis(Src, TEXT("Bip01-R-Hand"), TEXT("Bip01-R-Finger1"),
			TEXT("Bip01-R-Finger2"), TEXT("Bip01-R-Finger4"));
		const FQuat TargetPalm = PalmBasis(Dst, Right, TEXT("index_01_r"), TEXT("middle_01_r"), TEXT("pinky_01_r"));
		const FQuat DesiredWeapon = TargetPalm * SourcePalm.Inverse() * World(Src, TEXT("Bip_Weapon_R")).GetRotation();
		const FTransform OldRight = World(Dst, Right), OldForearmR = World(Dst, RightForearm), OldUpperR = World(Dst, RightUpper);
		FTransform NewRight = OldRight;
		NewRight.SetRotation((DesiredWeapon * WeaponInRightHand.GetRotation().Inverse()).GetNormalized());
		const FTransform OldLeft = World(Dst, Left), OldForearmL = World(Dst, LeftForearm), OldUpperL = World(Dst, LeftUpper);
		const FVector OldPalm = (LeftPalmInWrist * OldLeft).GetLocation();
		const FVector GripBefore = (LeftGripInWeapon * WeaponInRightHand * OldRight).GetLocation();
		const FVector GripAfter = (LeftGripInWeapon * WeaponInRightHand * NewRight).GetLocation();
		BeforeGap = FMath::Max(BeforeGap, FVector::Distance(OldPalm, GripBefore));
		const FVector WristTarget = GripAfter - OldLeft.TransformVector(LeftPalmInWrist.GetLocation());
		const FVector LeftReach = WristTarget - OldUpperL.GetLocation();
		const double LeftArmLength = FVector::Distance(OldUpperL.GetLocation(), OldForearmL.GetLocation())
			+ FVector::Distance(OldForearmL.GetLocation(), OldLeft.GetLocation());
		// Move the whole two-handed grip into reach and retain bend in the
		// support elbow. Neither limb is scaled, and the sword mount stays rigid.
		const double Shift = FMath::Max(0., LeftReach.Length() - (LeftArmLength - 3.5));
		const FVector GripShift = -LeftReach.GetSafeNormal() * Shift;
		MaxGripShift = FMath::Max(MaxGripShift, Shift);
		NewRight.AddToTranslation(GripShift);
		FTransform NewLeft = OldLeft;
		NewLeft.SetLocation(WristTarget + GripShift);
		FTransform UpperR, ForearmR, UpperL, ForearmL;
		if (!SolveReadyArm(OldUpperR, OldForearmR, OldRight, NewRight, UpperR, ForearmR)
			|| !SolveReadyArm(OldUpperL, OldForearmL, OldLeft, NewLeft, UpperL, ForearmL)) { return false; }

		const FVector TwistAxis = (NewRight.GetLocation() - ForearmR.GetLocation()).GetSafeNormal();
		FQuat Swing, Twist;
		(NewRight.GetRotation() * OldRight.GetRotation().Inverse()).ToSwingTwist(TwistAxis, Swing, Twist);
		ForearmR.SetRotation((FQuat::Slerp(FQuat::Identity, Twist, .65) * ForearmR.GetRotation()).GetNormalized());
		const auto UpperInParent = [&Dst](FName Bone, const FTransform& New, const FTransform& Old)
		{
			const FTransform Local = UAnimPoseExtensions::GetBonePose(Dst, Bone, EAnimPoseSpaces::Local);
			return New.GetRelativeTransform(Local.Inverse() * Old);
		};
		RightUpperKeys.Add(UpperInParent(RightUpper, UpperR, OldUpperR));
		RightForearmKeys.Add(ForearmR.GetRelativeTransform(UpperR));
		RightKeys.Add(NewRight.GetRelativeTransform(ForearmR));
		LeftUpperKeys.Add(UpperInParent(LeftUpper, UpperL, OldUpperL));
		LeftForearmKeys.Add(ForearmL.GetRelativeTransform(UpperL));
		LeftKeys.Add(NewLeft.GetRelativeTransform(ForearmL));
		AfterGap = FMath::Max(AfterGap, FVector::Distance((LeftPalmInWrist * NewLeft).GetLocation(), GripAfter + GripShift));
	}
	UE_LOG(LogTemp, Display, TEXT("LongbladeReadyGrip offhand gap %.2f -> %.2f cm, grip shift %.2f cm over %d keys"),
		BeforeGap, AfterGap, MaxGripShift, Count);
	return AfterGap < .1 && SetTrack(Ready, RightUpper, RightUpperKeys)
		&& SetTrack(Ready, RightForearm, RightForearmKeys) && SetTrack(Ready, Right, RightKeys)
		&& SetTrack(Ready, LeftUpper, LeftUpperKeys) && SetTrack(Ready, LeftForearm, LeftForearmKeys)
		&& SetTrack(Ready, Left, LeftKeys);
}

// Preserve the hand transform and move excessive axial rotation into the
// forearm. The swing that locates the wrist remains part of the arm pose.
double DistributeCarryRoll(const FTransform& Wrist, const FQuat& BindWristLocal, FTransform& Forearm, bool bAdjust)
{
	const FVector Axis = Forearm.InverseTransformVectorNoScale(Wrist.GetLocation() - Forearm.GetLocation()).GetSafeNormal();
	const FQuat Delta = Forearm.GetRotation().Inverse() * Wrist.GetRotation() * BindWristLocal.Inverse();
	FQuat Swing, Twist;
	Delta.GetNormalized().ToSwingTwist(Axis, Swing, Twist);
	if (Twist.W < 0.) { Twist = Twist * -1.; }
	const double Degrees = FMath::RadiansToDegrees(Twist.GetAngle());
	constexpr double MaxWristRoll = 32.;
	if (bAdjust && Degrees > MaxWristRoll)
	{
		const FQuat Correction = FQuat::Slerp(FQuat::Identity, Twist, 1. - MaxWristRoll / Degrees);
		Forearm.SetRotation((Forearm.GetRotation() * Correction).GetNormalized());
	}
	return Degrees;
}

struct FReadyGuardAdjustment
{
	double LowerCm;
};

const FName CarryArmBones[] = {TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r"),
	TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l")};

bool RefitCarryWrist(bool bApply)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	if (!Body) { return false; }
	const auto& Ref = Body->GetRefSkeleton();
	const FName UpperName(TEXT("upperarm_r")), LowerName(TEXT("lowerarm_r")), HandName(TEXT("hand_r"));
	const int32 UpperIndex = Ref.FindBoneIndex(UpperName), HandIndex = Ref.FindBoneIndex(HandName);
	if (UpperIndex == INDEX_NONE || HandIndex == INDEX_NONE) { return false; }
	const FName Parent = Ref.GetBoneName(Ref.GetParentIndex(UpperIndex));
	const FTransform& HandRef = Ref.GetRefBonePose()[HandIndex];
	const FVector Neutral = HandRef.GetRotation().UnrotateVector(HandRef.GetLocation()).GetSafeNormal();
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	auto* Blend = Asset<UBlendSpace>(BlendPath);
	if (!Blend) { return false; }
	TSet<UAnimSequence*> Clips;
	for (const FBlendSample& Sample : Blend->GetBlendSamples())
	{
		if (!Sample.Animation) { return false; }
		Clips.Add(Sample.Animation);
	}
	if (Clips.Num() != 5) { return false; }
	struct FFit
	{
		UAnimSequence* Clip;
		TArray<FTransform> Upper, Lower, Hand;
	};
	TArray<FFit> Fits;
	for (UAnimSequence* Source : Clips)
	{
		if (!Source->GetName().StartsWith(TEXT("AS_LongbladeStance_")) || Source->GetSkeleton() != Body->GetSkeleton()) { return false; }
		auto& Fit = Fits.AddDefaulted_GetRef();
		Fit.Clip = Source;
		double Before = 0., After = 0., MaxShift = 0.;
		const int32 Count = Source->GetDataModel()->GetNumberOfKeys();
		for (int32 Frame = 0; Frame < Count; ++Frame)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtFrame(Source, Frame, Options, Pose);
			if (!Pose.IsValid()) { return false; }
			const auto Read = [&Pose](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World); };
			const FTransform Upper = Read(UpperName), Lower = Read(LowerName), Hand = Read(HandName);
			const FVector Reach = (Hand.GetLocation() - Upper.GetLocation()).GetSafeNormal();
			const FVector Pole = FVector::VectorPlaneProject(Lower.GetLocation() - Upper.GetLocation(), Reach).GetSafeNormal();
			const FVector WristAxis = Hand.TransformVectorNoScale(Neutral);
			const FVector Target = FVector::VectorPlaneProject(-WristAxis, Reach).GetSafeNormal();
			const double Angle = FMath::Atan2((FVector::CrossProduct(Pole, Target) | Reach), Pole | Target);
			const double Limit = FMath::DegreesToRadians(17.);
			const FVector ImprovedPole = FQuat(Reach, FMath::Clamp(Angle * .5, -Limit, Limit)).RotateVector(Pole);
			FTransform FittedUpper, FittedLower;
			if (!SolveReadyArm(Upper, Lower, Hand, Hand, FittedUpper, FittedLower, &ImprovedPole)) { return false; }
			const auto Bend = [&Hand, &WristAxis](const FTransform& Elbow)
			{
				return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(
					((Hand.GetLocation() - Elbow.GetLocation()).GetSafeNormal() | WristAxis), -1., 1.)));
			};
			Before = FMath::Max(Before, Bend(Lower));
			After = FMath::Max(After, Bend(FittedLower));
			MaxShift = FMath::Max(MaxShift, FVector::Distance(FittedLower.GetLocation(), Lower.GetLocation()));
			Fit.Upper.Add(FittedUpper.GetRelativeTransform(Read(Parent)));
			Fit.Lower.Add(FittedLower.GetRelativeTransform(FittedUpper));
			Fit.Hand.Add(Hand.GetRelativeTransform(FittedLower));
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeCarryWrist %s bend %.2f -> %.2f degrees, elbow shift %.2f cm"),
			*Source->GetName(), Before, After, MaxShift);
		if (After > Before + .1 || MaxShift > 7.) { return false; }
	}
	if (bApply)
	{
		for (const auto& Fit : Fits)
		{
			if (!SetTrack(*Fit.Clip, UpperName, Fit.Upper) || !SetTrack(*Fit.Clip, LowerName, Fit.Lower)
				|| !SetTrack(*Fit.Clip, HandName, Fit.Hand) || !Save(Fit.Clip)) { return false; }
		}
	}
	return true;
}

bool FitCarryContact(UAnimSequence& Clip, USkeletalMesh& Body, const FTransform& WeaponInHand, const FTransform& GripL,
	const FTransform& PalmL, const FTransform& LeftOffset, const FAnimPose& ReadyPose, const FVector& TipInHand,
	FName ActionCurveName, bool bApply, bool bMoving, bool bPreserveSource = false, const FReadyGuardAdjustment* ReadyAdjustment = nullptr)
{
	const auto& Bones = CarryArmBones;
	const auto& Reference = Body.GetRefSkeleton();
	const int32 RightIndex = Reference.FindBoneIndex(Bones[2]), LeftIndex = Reference.FindBoneIndex(Bones[5]);
	if (RightIndex == INDEX_NONE || LeftIndex == INDEX_NONE || Clip.GetSkeleton() != Body.GetSkeleton()) { return false; }
	const auto& RefPose = Reference.GetRefBonePose();
	const FVector NeutralLeft = RefPose[LeftIndex].GetRotation().UnrotateVector(RefPose[LeftIndex].GetLocation()).GetSafeNormal();
	const FName RootBone(TEXT("root")), TorsoBone(TEXT("spine_03")), ChestBone(TEXT("spine_05")), NeckBone(TEXT("neck_01"));
	const auto ReadReady = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(ReadyPose, Bone, EAnimPoseSpaces::World); };
	const FTransform ReadyChest = ReadReady(ChestBone);
	const FTransform ReadyRight = ReadReady(Bones[2]);
	const FTransform ReadyHead = ReadReady(TEXT("head"));
	const FTransform HeadInChest = ReadyHead.GetRelativeTransform(ReadyChest);
	const FVector ReadySide = (ReadReady(Bones[0]).GetLocation() - ReadReady(Bones[3]).GetLocation()).GetSafeNormal2D();
	const FTransform ReadyRoot = ReadReady(RootBone);
	const FVector RootSide = ReadyRoot.InverseTransformVectorNoScale(ReadySide);
	const FVector HeadSide = ReadyHead.InverseTransformVectorNoScale(ReadySide);
	const FVector HeadForward = ReadyHead.InverseTransformVectorNoScale(FVector::CrossProduct(ReadySide, FVector::UpVector));
	const FVector ChestForward = ReadyChest.InverseTransformVectorNoScale(FVector::CrossProduct(ReadySide, FVector::UpVector));
	const FVector GripAxis = (ReadReady(Bones[5]).GetLocation() - ReadyRight.GetLocation()).GetSafeNormal();
	const FVector ReadyElbows[] = {ReadReady(Bones[1]).GetLocation() - ReadReady(Bones[0]).GetLocation(),
		ReadReady(Bones[4]).GetLocation() - ReadReady(Bones[3]).GetLocation()};
	FTransform CarryRight = ReadyRight;
	// Clock the whole two-handed grip around the line between the wrists.
	// Both palms and the blade remain part of the same rigid pose.
	CarryRight.SetRotation((FQuat(GripAxis, FMath::DegreesToRadians(-26.)) * ReadyRight.GetRotation()).GetNormalized());
	const FTransform CarryInChest = CarryRight.GetRelativeTransform(ReadyChest);
	const FAnimationCurveIdentifier RotationCurve(Project_J::Animation::LeftHandCarryAuthoredRotationCurve, ERawCurveTrackTypes::RCT_Float);
	const bool bAuthoredRotation = Clip.GetDataModel()->FindFloatCurve(RotationCurve) != nullptr;
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Body;
	Options.bShouldRetarget = false;
	const int32 Count = Clip.GetDataModel()->GetNumberOfKeys();
	if (Count < 2) { return false; }
	TArray<FTransform> Keys[UE_ARRAY_COUNT(Bones)];
	TArray<FTransform> NeckKeys;
	TArray<FTransform> RootKeys;
	TArray<FTransform> TorsoKeys;
	double PeakBefore = 0., PeakAfter = 0., PeakBend = 0., MaxPositionError = 0., MaxRotationError = 0., MaxShift = 0.;
	for (int32 Key = 0; Key < Count; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		FTransform Old[UE_ARRAY_COUNT(Bones)];
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
		{
			Old[Index] = UAnimPoseExtensions::GetBonePose(Pose, Bones[Index], EAnimPoseSpaces::World);
		}
		FTransform Target[UE_ARRAY_COUNT(Bones)];
		Target[2] = Old[2];
		FVector HeadCenter = UAnimPoseExtensions::GetBonePose(Pose, TEXT("head"), EAnimPoseSpaces::World).GetLocation();
		FVector CarryElbows[2] = {FVector::ZeroVector, FVector::ZeroVector};
		if (bMoving && bApply)
		{
			// Normalize source heading while preserving the authored pitch and roll.
			FTransform Root = UAnimPoseExtensions::GetBonePose(Pose, RootBone, EAnimPoseSpaces::Local);
			const FVector SourceSide = Root.TransformVectorNoScale(RootSide).GetSafeNormal2D();
			const double Heading = FMath::Atan2(FVector::CrossProduct(SourceSide, ReadySide).Z, SourceSide | ReadySide);
			Root.SetRotation((FQuat(FVector::UpVector, Heading) * Root.GetRotation()).GetNormalized());
			RootKeys.Add(Root);
			FTransform Chest = UAnimPoseExtensions::GetBonePose(Pose, ChestBone, EAnimPoseSpaces::World);
			FTransform Head = UAnimPoseExtensions::GetBonePose(Pose, TEXT("head"), EAnimPoseSpaces::World);
			FTransform Neck = UAnimPoseExtensions::GetBonePose(Pose, NeckBone, EAnimPoseSpaces::World);
			FTransform NeckParent = UAnimPoseExtensions::GetBonePose(Pose, NeckBone, EAnimPoseSpaces::Local).Inverse() * Neck;
			FTransform Torso = UAnimPoseExtensions::GetBonePose(Pose, TorsoBone, EAnimPoseSpaces::World);
			const FVector TorsoPivot = Torso.GetLocation();
			const FTransform TorsoParent = UAnimPoseExtensions::GetBonePose(Pose, TorsoBone, EAnimPoseSpaces::Local).Inverse() * Torso;
			const FVector ChestDirection = Chest.TransformVectorNoScale(ChestForward);
			const double ChestPitch = FMath::Atan2(ChestDirection.Z, ChestDirection.Size2D());
			const double ChestLimit = FMath::DegreesToRadians(15.);
			const FQuat TorsoCorrection(FVector::CrossProduct(ChestDirection.GetSafeNormal2D(), FVector::UpVector),
				FMath::Clamp(ChestPitch, -ChestLimit, ChestLimit) - ChestPitch);
			const auto AdjustTorso = [&](FTransform& Bone)
			{
				Bone.SetLocation(TorsoPivot + TorsoCorrection.RotateVector(Bone.GetLocation() - TorsoPivot));
				Bone.SetRotation((TorsoCorrection * Bone.GetRotation()).GetNormalized());
			};
			AdjustTorso(Torso);
			TorsoKeys.Add(Torso.GetRelativeTransform(TorsoParent));
			AdjustTorso(Chest);
			AdjustTorso(Head);
			HeadCenter = Head.GetLocation();
			AdjustTorso(Neck);
			AdjustTorso(NeckParent);
			for (FTransform& Bone : Old) { AdjustTorso(Bone); }
			const FVector ForwardGaze = (HeadInChest * Chest).TransformVectorNoScale(HeadSide).GetSafeNormal2D();
			const FVector ClipGaze = Head.TransformVectorNoScale(HeadSide).GetSafeNormal2D();
			const double Turn = FMath::Atan2(FVector::CrossProduct(ForwardGaze, ClipGaze).Z, ForwardGaze | ClipGaze);
			const double TurnLimit = FMath::DegreesToRadians(25.);
			const FQuat GazeCorrection(FVector::UpVector, FMath::Clamp(Turn, -TurnLimit, TurnLimit) - Turn);
			const FVector Gaze = GazeCorrection.RotateVector(Head.TransformVectorNoScale(HeadForward));
			const double Pitch = FMath::Atan2(Gaze.Z, Gaze.Size2D());
			const FVector ReadyGaze = (HeadInChest * Chest).TransformVectorNoScale(HeadForward);
			const double ChestGazePitch = FMath::Atan2(ReadyGaze.Z, ReadyGaze.Size2D());
			const double RelativePitchLimit = FMath::DegreesToRadians(14.);
			const double PitchLimit = FMath::DegreesToRadians(12.);
			const double MinPitch = FMath::Clamp(ChestGazePitch - RelativePitchLimit, -PitchLimit, PitchLimit);
			const double MaxPitch = FMath::Clamp(ChestGazePitch + RelativePitchLimit, -PitchLimit, PitchLimit);
			const FVector PitchAxis = FVector::CrossProduct(Gaze.GetSafeNormal2D(), FVector::UpVector);
			const FQuat PitchCorrection(PitchAxis, FMath::Clamp(Pitch, MinPitch, MaxPitch) - Pitch);
			Neck.SetRotation((PitchCorrection * GazeCorrection * Neck.GetRotation()).GetNormalized());
			NeckKeys.Add(Neck.GetRelativeTransform(NeckParent));
			Target[2] = bPreserveSource ? Old[2] : CarryInChest * Chest;
			const FQuat ChestDelta = Chest.GetRotation() * ReadyChest.GetRotation().Inverse();
			CarryElbows[0] = bPreserveSource ? Old[1].GetLocation() - Old[0].GetLocation() : ChestDelta.RotateVector(ReadyElbows[0]);
			CarryElbows[1] = bPreserveSource ? Old[4].GetLocation() - Old[3].GetLocation() : ChestDelta.RotateVector(ReadyElbows[1]);
			// The locomotion graph can lower the mesh through its stride. Limit
			// blade drop before making either wrist target, so the contacts agree.
			const FVector Direction = Target[2].TransformVectorNoScale(TipInHand);
			constexpr double MaxBladeDrop = 57.;
			if (Direction.Z < -MaxBladeDrop)
			{
				const double Lift = FMath::Asin(-Direction.Z / Direction.Length()) - FMath::Asin(MaxBladeDrop / Direction.Length());
				const FVector Axis = FVector::CrossProduct(Direction, FVector::UpVector).GetSafeNormal();
				Target[2].SetRotation((FQuat(Axis, Lift) * Target[2].GetRotation()).GetNormalized());
			}
		}
		if (ReadyAdjustment)
		{
			// Lower the entire grip in the authored pose. The blade keeps its
			// orientation relative to both hands.
			Target[2].AddToTranslation(-FVector::UpVector * ReadyAdjustment->LowerCm);
		}
		const FTransform GripWorld = GripL * WeaponInHand * Target[2];
		if (!Project_J::Animation::MakeWristContactTarget(GripWorld, PalmL, LeftOffset, Target[5], Old[5].GetScale3D())) { return false; }
		if (ReadyAdjustment)
		{
			Target[5].SetRotation(Old[5].GetRotation());
			Target[5].SetLocation((LeftOffset * GripWorld).GetLocation() - Target[5].TransformVector(PalmL.GetLocation()));
		}
		if (!bApply)
		{
			const double PositionError = bAuthoredRotation
				? FVector::Distance((PalmL * Old[5]).GetLocation(), (LeftOffset * GripWorld).GetLocation())
				: FVector::Distance(Old[5].GetLocation(), Target[5].GetLocation());
			MaxPositionError = FMath::Max(MaxPositionError, PositionError);
			if (!bAuthoredRotation)
				MaxRotationError = FMath::Max(MaxRotationError, Old[5].GetRotation().AngularDistance(Target[5].GetRotation()));
			PeakAfter = FMath::Max(PeakAfter, DistributeCarryRoll(Old[2], RefPose[RightIndex].GetRotation(), Old[1], false));
			PeakAfter = FMath::Max(PeakAfter, DistributeCarryRoll(Old[5], RefPose[LeftIndex].GetRotation(), Old[4], false));
			const FVector Forearm = (Old[5].GetLocation() - Old[4].GetLocation()).GetSafeNormal();
			const double Bend = FMath::Acos(FMath::Clamp(Forearm | Old[5].TransformVectorNoScale(NeutralLeft), -1., 1.));
			PeakBend = FMath::Max(PeakBend, FMath::RadiansToDegrees(Bend));
			continue;
		}

		// Both wrists move with the same rigid grip. Project the requested
		// positions into the two reach spheres while reserving elbow bend.
		FVector Shift = bPreserveSource ? (Old[5].GetLocation() - Target[5].GetLocation()) * .5 : FVector::ZeroVector;
		for (int32 Iteration = 0; Iteration < 8; ++Iteration)
		{
			if (bPreserveSource)
			{
				const FVector Wrist = Target[2].GetLocation() + Shift;
				const FVector Tip = Target[2].TransformPosition(TipInHand) + Shift;
				const FVector Away = FMath::ClosestPointOnSegment(HeadCenter, Wrist, Tip) - HeadCenter;
				// This is a conservative authoring constraint. Mesh clearance is reviewed separately.
				Shift += Away.GetSafeNormal() * FMath::Max(0., 29. - Away.Length());
			}
			for (const int32 Upper : {0, 3})
			{
				const double Length = FVector::Distance(Old[Upper].GetLocation(), Old[Upper + 1].GetLocation())
					+ FVector::Distance(Old[Upper + 1].GetLocation(), Old[Upper + 2].GetLocation());
				const FVector Reach = Target[Upper + 2].GetLocation() + Shift - Old[Upper].GetLocation();
				Shift -= Reach.GetSafeNormal() * FMath::Max(0., Reach.Length() - (Length - 3.5));
			}
		}
		MaxShift = FMath::Max(MaxShift, Shift.Length());
		Target[2].AddToTranslation(Shift);
		Target[5].AddToTranslation(Shift);
		if (bMoving)
		{
			const FVector ReachAxis = (Target[5].GetLocation() - Old[3].GetLocation()).GetSafeNormal();
			const FVector AuthoredPole = FVector::VectorPlaneProject(CarryElbows[1], ReachAxis).GetSafeNormal();
			const FVector NeutralPole = FVector::VectorPlaneProject(-Target[5].TransformVectorNoScale(NeutralLeft), ReachAxis).GetSafeNormal();
			// Keep the ready pose's elbow side while allowing the forearm to
			// approach the handle along the hand's neutral axis.
			CarryElbows[1] = AuthoredPole + NeutralPole * (bPreserveSource ? 1.75 : .75);
		}
		if (ReadyAdjustment)
		{
			for (const int32 Arm : {0, 1})
			{
				const int32 Upper = 3 * Arm;
				const FVector ReachAxis = (Target[Upper + 2].GetLocation() - Old[Upper].GetLocation()).GetSafeNormal();
				const FVector AuthoredPole = FVector::VectorPlaneProject(Old[Upper + 1].GetLocation() - Old[Upper].GetLocation(), ReachAxis).GetSafeNormal();
				const FVector LowerPole = FVector::VectorPlaneProject(-FVector::UpVector, ReachAxis).GetSafeNormal();
				CarryElbows[Arm] = AuthoredPole + LowerPole * .5;
			}
		}
		const bool bUseCarryPoles = bMoving || ReadyAdjustment;
		if (!SolveReadyArm(Old[0], Old[1], Old[2], Target[2], Target[0], Target[1], bUseCarryPoles ? &CarryElbows[0] : nullptr)
			|| !SolveReadyArm(Old[3], Old[4], Old[5], Target[5], Target[3], Target[4], bUseCarryPoles ? &CarryElbows[1] : nullptr)) { return false; }
		PeakBefore = FMath::Max(PeakBefore, DistributeCarryRoll(Old[2], RefPose[RightIndex].GetRotation(), Old[1], false));
		PeakBefore = FMath::Max(PeakBefore, DistributeCarryRoll(Old[5], RefPose[LeftIndex].GetRotation(), Old[4], false));
		DistributeCarryRoll(Target[2], RefPose[RightIndex].GetRotation(), Target[1], true);
		DistributeCarryRoll(Target[5], RefPose[LeftIndex].GetRotation(), Target[4], true);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
		{
			const FTransform Parent = (Index % 3) ? Target[Index - 1]
				: UAnimPoseExtensions::GetBonePose(Pose, Bones[Index], EAnimPoseSpaces::Local).Inverse() * Old[Index];
			Keys[Index].Add(Target[Index].GetRelativeTransform(Parent));
		}
	}
	const FAnimationCurveIdentifier ContactCurve(Project_J::Animation::LeftHandCarryIKCurve, ERawCurveTrackTypes::RCT_Float);
	const FAnimationCurveIdentifier ActionCurve(ActionCurveName, ERawCurveTrackTypes::RCT_Float);
	const auto* Curve = Clip.GetDataModel()->FindFloatCurve(ContactCurve);
	const auto* PriorCurve = Clip.GetDataModel()->FindFloatCurve(ActionCurve);
	if (!bApply)
	{
		UE_LOG(LogTemp, Display, TEXT("LongbladeCarryContact %s palm error %.3f cm / %.3f deg, wrist roll %.1f deg, left bend %.1f deg"),
			*Clip.GetName(), MaxPositionError, FMath::RadiansToDegrees(MaxRotationError), PeakAfter, PeakBend);
		return !PriorCurve && Curve && FMath::IsNearlyEqual(Curve->FloatCurve.Eval(0.f), 1.f)
			&& FMath::IsNearlyEqual(Curve->FloatCurve.Eval(Clip.GetPlayLength()), 1.f)
			&& MaxPositionError < .1 && MaxRotationError < FMath::DegreesToRadians(.25) && PeakAfter < 33.
			&& (!bMoving || PeakBend < 50.);
	}
	UE_LOG(LogTemp, Display, TEXT("LongbladeCarryContact %s initial wrist roll %.1f deg, shared reach shift %.2f cm"),
		*Clip.GetName(), PeakBefore, MaxShift);
	if (bMoving && (!SetTrack(Clip, TorsoBone, TorsoKeys) || !SetTrack(Clip, NeckBone, NeckKeys) || !SetTrack(Clip, RootBone, RootKeys))) { return false; }
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
	{
		if (!SetTrack(Clip, Bones[Index], Keys[Index])) { return false; }
	}
	if (ReadyAdjustment) { return bAuthoredRotation && Curve && !PriorCurve; }
	for (const TCHAR* Finger : {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")})
	{
		for (int32 Joint = 1; Joint <= 3; ++Joint)
		{
			const FName Bone(*FString::Printf(TEXT("%s_%02d_l"), Finger, Joint));
			const int32 BoneIndex = Reference.FindBoneIndex(Bone);
			if (BoneIndex == INDEX_NONE) { return false; }
			const FTransform Local = UAnimPoseExtensions::GetBonePose(ReadyPose, Bone, EAnimPoseSpaces::Local);
			TArray<FTransform> FingerKeys;
			FingerKeys.Init(Local, Count);
			if (!SetTrack(Clip, Bone, FingerKeys)) { return false; }
		}
	}
	auto& Controller = Clip.GetController();
	// Retire the original carry bake's two constant action keys.
	if (PriorCurve && (PriorCurve->FloatCurve.GetNumKeys() != 2 || !FMath::IsNearlyEqual(PriorCurve->FloatCurve.Eval(0.f), 1.f)
		|| !FMath::IsNearlyEqual(PriorCurve->FloatCurve.Eval(Clip.GetPlayLength()), 1.f)
		|| !Controller.RemoveCurve(ActionCurve, false))) { return false; }
	return (Curve || Controller.AddCurve(ContactCurve, AACF_DefaultCurve, false))
		&& Controller.SetCurveKeys(ContactCurve, {{0.f, 1.f}, {Clip.GetPlayLength(), 1.f}}, false);
}

bool PrepareCarryContact(bool bApply, const FString* TrialSource = nullptr)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* Defaults = StanceBodyDefaults();
	const auto* Profile = Defaults ? Defaults->HandGripProfile.Get() : nullptr;
	if (!Body || !Weapon || !Profile) { return false; }
	const auto& Calibration = Profile->Calibration;
	const auto* PalmL = Body->FindSocket(Calibration.SecondaryPalmSocketName);
	auto* GripL = const_cast<USkeletalMeshSocket*>(Weapon->FindSocket(TEXT("WeaponGrip_L")));
	const auto* Tip = Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip"));
	FTransform WeaponInHand;
	if (!PalmL || PalmL->BoneName != TEXT("hand_l") || !GripL || GripL->GetOuter() != Weapon
		|| GripL->BoneName != Weapon->GetRefSkeleton().GetBoneName(0)
		|| !Tip || Tip->BoneName != GripL->BoneName)
	{
		UE_LOG(LogTemp, Error, TEXT("Longblade carry requires a left palm on hand_l and mesh-local offhand grip and tip on the weapon root (palm=%s, grip=%s, tip=%s)."),
			*GetPathNameSafe(PalmL), *GetPathNameSafe(GripL), *GetPathNameSafe(Tip));
		return false;
	}
	if (!ResolveStanceWeaponMount(*Body, *Weapon, WeaponInHand))
	{
		UE_LOG(LogTemp, Error, TEXT("Longblade carry could not resolve the configured primary grip %s."), *StancePrimaryGripName().ToString());
		return false;
	}
	const FVector TipInHand = (Tip->GetSocketLocalTransform() * WeaponInHand).GetLocation();

	TArray<UAnimSequence*> Clips;
	for (const auto& Take : GuardTakes)
	{
		auto* Clip = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + Take.Name);
		if (!Clip) { return false; }
		Clips.Add(Clip);
	}
	if (TrialSource)
	{
		const auto Retargeted = RetargetStanceTakes(*TrialSource, GuardTakes, TEXT("SourceCarryTrial_"), false);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(GuardTakes); ++Index)
		{
			const FString Expected = FString(TEXT("SourceCarryTrial_")) + GuardTakes[Index].Name;
			UAnimSequence* const* Match = Retargeted.FindByPredicate([&](const UAnimSequence* Candidate) { return Candidate->GetName() == Expected; });
			if (!Match) { return false; }
			Clips[Index] = *Match;
		}
	}
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	FAnimPose Ready;
	UAnimPoseExtensions::GetAnimPoseAtFrame(Clips[0], 0, Options, Ready);
	if (!Ready.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Longblade carry could not evaluate the Ready sequence."));
		return false;
	}
	const FTransform PalmInHand = PalmL->GetSocketLocalTransform();
	FTransform Grip = GripL->GetSocketLocalTransform();
	const FAnimationCurveIdentifier RotationCurve(Project_J::Animation::LeftHandCarryAuthoredRotationCurve, ERawCurveTrackTypes::RCT_Float);
	const bool bUsesSocketRotation = Clips.ContainsByPredicate([&](const UAnimSequence* Clip)
	{
		return !Clip->GetDataModel()->FindFloatCurve(RotationCurve);
	});
	// Identity is a valid socket rotation when the stance keys own wrist orientation.
	const bool bNeedsOrientation = bUsesSocketRotation && Grip.GetRotation().Equals(FQuat::Identity, .001);
	const bool bStanceTrial = TrialSource && FParse::Param(**TrialSource, TEXT("TrialStanceGrips"));
	if (TrialSource && (bStanceTrial || FParse::Param(**TrialSource, TEXT("TrialSweepGripOrientation"))))
	{
		const FTransform WeaponInPose = WeaponInHand * UAnimPoseExtensions::GetBonePose(Ready, TEXT("hand_r"), EAnimPoseSpaces::World);
		const FTransform PalmInPose = PalmInHand * UAnimPoseExtensions::GetBonePose(Ready, TEXT("hand_l"), EAnimPoseSpaces::World);
		const FQuat ReadyGrip = (WeaponInPose.GetRotation().Inverse() * PalmInPose.GetRotation()
			* Calibration.SecondaryHandOffset.GetRotation().Inverse()).GetNormalized();
		if (bStanceTrial)
		{
			bool bPass = true;
			for (int32 Index = 0; Index < Clips.Num(); ++Index)
			{
				auto* Clip = Clips[Index];
				const bool bMoving = Index != 0;
				const bool bBackward = Clip->GetName().EndsWith(TEXT("_Backward"));
				FTransform Candidate = Grip;
				Candidate.SetRotation(FQuat::Slerp(Grip.GetRotation(), ReadyGrip, !bMoving || bBackward ? 1. : .5).GetNormalized());
				if (!FitCarryContact(*Clip, *Body, WeaponInHand, Candidate, PalmInHand, Calibration.SecondaryHandOffset,
					Ready, TipInHand, Defaults->LeftHandIKCurveName, true, bMoving, true)) { return false; }
				bPass &= FitCarryContact(*Clip, *Body, WeaponInHand, Candidate, PalmInHand, Calibration.SecondaryHandOffset,
					Ready, TipInHand, Defaults->LeftHandIKCurveName, false, bMoving);
			}
			if (FParse::Param(**TrialSource, TEXT("SaveStanceReview")))
			{
				// These retarget packages are unlinked previews. Gameplay assets keep their original references.
				for (auto* Clip : Clips)
				{
					if (!Clip->GetOutermost()->GetName().Contains(TEXT("/SourceTransient/StanceAudit/SourceCarryTrial_")) || !Save(Clip)) { return false; }
				}
			}
			return bPass;
		}
		for (const double Weight : {.25, .50, .75})
		{
			FTransform Candidate = Grip;
			Candidate.SetRotation(FQuat::Slerp(Grip.GetRotation(), ReadyGrip, Weight).GetNormalized());
			UE_LOG(LogTemp, Display, TEXT("SourceCarryTrial GripWeight=%.2f"), Weight);
			for (const UAnimSequence* SourceClip : Clips)
			{
				const FName Name = MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), SourceClip->GetFName());
				auto* TestClip = DuplicateObject<UAnimSequence>(SourceClip, GetTransientPackage(), Name);
				const bool bMoving = SourceClip != Clips[0];
				if (TestClip && FitCarryContact(*TestClip, *Body, WeaponInHand, Candidate, PalmInHand, Calibration.SecondaryHandOffset,
					Ready, TipInHand, Defaults->LeftHandIKCurveName, true, bMoving, true))
				{
					FitCarryContact(*TestClip, *Body, WeaponInHand, Candidate, PalmInHand, Calibration.SecondaryHandOffset,
						Ready, TipInHand, Defaults->LeftHandIKCurveName, false, bMoving);
				}
			}
		}
		return true;
	}
	if (bNeedsOrientation || (TrialSource && FParse::Param(**TrialSource, TEXT("TrialSourceGripOrientation"))))
	{
		if (!bApply) { return false; }
		const FTransform WeaponInPose = WeaponInHand * UAnimPoseExtensions::GetBonePose(Ready, TEXT("hand_r"), EAnimPoseSpaces::World);
		const FTransform PalmInPose = PalmInHand * UAnimPoseExtensions::GetBonePose(Ready, TEXT("hand_l"), EAnimPoseSpaces::World);
		Grip.SetRotation((WeaponInPose.GetRotation().Inverse() * PalmInPose.GetRotation()
			* Calibration.SecondaryHandOffset.GetRotation().Inverse()).GetNormalized());
	}
	bool bAllValid = true;
	for (auto* Clip : Clips)
	{
		const bool bMoving = Clip != Clips[0];
		const bool bBake = bApply && !Clip->GetDataModel()->FindFloatCurve(RotationCurve);
		if (!FitCarryContact(*Clip, *Body, WeaponInHand, Grip, PalmInHand, Calibration.SecondaryHandOffset,
			Ready, TipInHand, Defaults->LeftHandIKCurveName, bBake, bMoving, TrialSource != nullptr)
			|| (bBake && !FitCarryContact(*Clip, *Body, WeaponInHand, Grip, PalmInHand, Calibration.SecondaryHandOffset,
				Ready, TipInHand, Defaults->LeftHandIKCurveName, false, bMoving)))
		{
			if (!TrialSource) { return false; }
			bAllValid = false;
		}
	}
	if (TrialSource) { return bAllValid; }
	if (!bApply) { return true; }
	if (bNeedsOrientation)
	{
		GripL->Modify();
		GripL->RelativeRotation = Grip.Rotator();
		if (!Save(Weapon)) { return false; }
	}
	for (auto* Clip : Clips) { if (Clip->GetPackage()->IsDirty() && !Save(Clip)) { return false; } }
	return true;
}

bool AuthorCarryElbowClearance(bool bApply)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	if (!Body) { return false; }
	const FName UpperBone(TEXT("upperarm_l")), ForearmBone(TEXT("lowerarm_l")), HandBone(TEXT("hand_l"));
	const int32 HandIndex = Body->GetRefSkeleton().FindBoneIndex(HandBone);
	if (HandIndex == INDEX_NONE) { return false; }
	const FQuat BindWrist = Body->GetRefSkeleton().GetRefBonePose()[HandIndex].GetRotation();
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	TArray<UAnimSequence*> Changed;
	for (const TCHAR* Name : {TEXT("Ready"), TEXT("Backward")})
	{
		auto* Clip = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + Name);
		const FAnimationCurveIdentifier RotationCurve(Project_J::Animation::LeftHandCarryAuthoredRotationCurve, ERawCurveTrackTypes::RCT_Float);
		if (!Clip || !Clip->GetDataModel()->FindFloatCurve(RotationCurve)) { return false; }
		TArray<FTransform> UpperKeys, ForearmKeys, WristKeys;
		double Before = TNumericLimits<double>::Max(), After = Before, Shift = 0.;
		for (int32 Key = 0; Key < Clip->GetDataModel()->GetNumberOfKeys(); ++Key)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtFrame(Clip, Key, Options, Pose);
			if (!Pose.IsValid()) { return false; }
			const auto Read = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World); };
			const FTransform Upper = Read(UpperBone), Forearm = Read(ForearmBone), Wrist = Read(HandBone);
			const FVector Down = (Read(TEXT("pelvis")).GetLocation() - Read(TEXT("spine_05")).GetLocation()).GetSafeNormal();
			const FVector UpperSegment = Forearm.GetLocation() - Upper.GetLocation();
			const double OriginalDrop = UpperSegment | Down;
			Before = FMath::Min(Before, OriginalDrop);
			FTransform NewUpper = Upper, NewForearm = Forearm;
			if (OriginalDrop < .98)
			{
				const FVector Reach = (Wrist.GetLocation() - Upper.GetLocation()).GetSafeNormal();
				const FVector Along = Reach * (UpperSegment | Reach);
				const FVector OriginalPole = UpperSegment - Along;
				const FVector Out = (Upper.GetLocation() - Read(TEXT("upperarm_r")).GetLocation()).GetSafeNormal();
				const FVector Preferred = FVector::VectorPlaneProject(Down + .35 * Out, Reach).GetSafeNormal();
				const FVector Authored = OriginalPole.GetSafeNormal();
				const auto PoleAt = [&](double Weight) { return (Authored + Preferred * Weight).GetSafeNormal(); };
				const auto DropAt = [&](double Weight) { return (Along + PoleAt(Weight) * OriginalPole.Length()) | Down; };
				double Min = 0., Max = .75;
				if (DropAt(Max) >= 1.)
				{
					for (int32 Iteration = 0; Iteration < 10; ++Iteration)
					{
						const double Mid = (Min + Max) * .5;
						if (DropAt(Mid) < 1.) { Min = Mid; }
						else { Max = Mid; }
					}
				}
				if (DropAt(Max) > OriginalDrop + .02)
				{
					const FVector Pole = PoleAt(Max);
					if (!SolveReadyArm(Upper, Forearm, Wrist, Wrist, NewUpper, NewForearm, &Pole)) { return false; }
					DistributeCarryRoll(Wrist, BindWrist, NewForearm, true);
				}
			}
			const FTransform LocalUpper = UAnimPoseExtensions::GetBonePose(Pose, UpperBone, EAnimPoseSpaces::Local);
			UpperKeys.Add(NewUpper.GetRelativeTransform(LocalUpper.Inverse() * Upper));
			ForearmKeys.Add(NewForearm.GetRelativeTransform(NewUpper));
			WristKeys.Add(Wrist.GetRelativeTransform(NewForearm));
			After = FMath::Min(After, (NewForearm.GetLocation() - NewUpper.GetLocation()) | Down);
			Shift = FMath::Max(Shift, FVector::Distance(NewForearm.GetLocation(), Forearm.GetLocation()));
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeCarryElbowClearance %s minimum elbow drop %.2f -> %.2f cm, max elbow shift %.2f cm"),
			Name, Before, After, Shift);
		if (bApply && Shift > .02)
		{
			if (!SetTrack(*Clip, UpperBone, UpperKeys) || !SetTrack(*Clip, ForearmBone, ForearmKeys) || !SetTrack(*Clip, HandBone, WristKeys))
				return false;
			Changed.Add(Clip);
		}
	}
	if (!bApply) { return true; }
	if (!PrepareCarryContact(false)) { return false; }
	for (auto* Clip : Changed) { if (!Save(Clip)) { return false; } }
	return true;
}

bool AuthorCastWeaponClearance(bool bApply)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* Grip = Weapon ? Weapon->FindSocket(StancePrimaryGripName()) : nullptr;
	const auto* Tip = Weapon ? Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip")) : nullptr;
	FTransform WeaponInHand;
	if (!Body || !Grip || !Tip || !ResolveStanceWeaponMount(*Body, *Weapon, WeaponInHand)) { return false; }
	const FVector GripInHand = (Grip->GetSocketLocalTransform() * WeaponInHand).GetLocation();
	const FVector TipInHand = (Tip->GetSocketLocalTransform() * WeaponInHand).GetLocation();
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	const FName ArmBones[] = {TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")};
	constexpr int32 ArmCount = UE_ARRAY_COUNT(ArmBones);
	constexpr double HeadClearance = 22., NeckClearance = 17.;
	struct FArmPose
	{
		FTransform Bones[ArmCount];
	};
	struct FFrame
	{
		FArmPose Arm;
		FTransform Parent;
		FVector Head, Neck;
		FVector OriginalWrist;
	};
	struct FAnchor
	{
		FArmPose Arm;
		FString Name;
	};
	const auto ReadArm = [&](const FAnimPose& Pose)
	{
		FArmPose Arm;
		for (int32 Bone = 0; Bone < ArmCount; ++Bone)
			Arm.Bones[Bone] = UAnimPoseExtensions::GetBonePose(Pose, ArmBones[Bone], EAnimPoseSpaces::Local);
		return Arm;
	};
	const auto BlendArm = [](const FArmPose& Source, const FArmPose& Anchor, double Weight)
	{
		FArmPose Result = Source;
		for (int32 Bone = 0; Bone < ArmCount; ++Bone)
			Result.Bones[Bone].Blend(Source.Bones[Bone], Anchor.Bones[Bone], Weight);
		return Result;
	};
	const auto WristForArm = [](const FFrame& Frame, const FArmPose& Arm)
	{
		FTransform Wrist = Frame.Parent;
		for (const FTransform& Local : Arm.Bones) { Wrist = Local * Wrist; }
		return Wrist;
	};
	const auto Measure = [&](const FFrame& Frame, const FTransform& Wrist)
	{
		const FVector A = Wrist.TransformPosition(GripInHand), B = Wrist.TransformPosition(TipInHand);
		return FVector(FVector::Distance(Frame.Head, FMath::ClosestPointOnSegment(Frame.Head, A, B)),
			FVector::Distance(Frame.Neck, FMath::ClosestPointOnSegment(Frame.Neck, A, B)), B.Z);
	};
	TArray<FAnchor> Anchors;
	for (const auto& Take : GuardTakes)
	{
		const auto* Guard = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + Take.Name);
		if (!Guard) { return false; }
		for (const double Phase : {.0, .25, .5, .75})
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Guard, Guard->GetPlayLength() * Phase, Options, Pose);
			if (!Pose.IsValid()) { return false; }
			Anchors.Add({ReadArm(Pose), FString::Printf(TEXT("%s@%.2f"), Take.Name, Phase)});
		}
	}
	TArray<TPair<UAnimSequence*, TArray<FArmPose>>> Pending;
	bool bAllClear = true;
	for (const TCHAR* Role : {TEXT("Buff"), TEXT("CastLeft"), TEXT("CastRight")})
	{
		if (FCString::Strcmp(Role, TEXT("Buff")) == 0) { continue; }
		auto* Clip = Asset<UAnimSequence>(Sequences + TEXT("DirectBody/Skill/AS_LongbladeCast_") + Role);
		if (!Clip) { return false; }
		const int32 FadeKeys = FMath::Max(1, FMath::RoundToInt(Clip->GetDataModel()->GetFrameRate().AsDecimal() * .32));
		TArray<FFrame> Frames;
		TArray<FArmPose> Result;
		TArray<FIntPoint> Regions;
		bool bNeedsChange = false;
		for (int32 Key = 0; Key < Clip->GetDataModel()->GetNumberOfKeys(); ++Key)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtFrame(Clip, Key, Options, Pose);
			if (!Pose.IsValid()) { return false; }
			const auto Read = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World); };
			const FArmPose Arm = ReadArm(Pose);
			const FTransform Wrist = Read(TEXT("hand_r"));
			const FFrame& Frame = Frames.Add_GetRef({Arm, Arm.Bones[0].Inverse() * Read(ArmBones[0]),
				Read(TEXT("head")).GetLocation(), Read(TEXT("neck_01")).GetLocation(), Wrist.GetLocation()});
			Result.Add(Arm);
			const FVector Clearance = Measure(Frame, Wrist);
			bNeedsChange |= Clearance.X < HeadClearance - .5 || Clearance.Y < NeckClearance - .5;
			if (Clearance.X >= HeadClearance + 2. && Clearance.Y >= NeckClearance + 2.) { continue; }
			if (Regions.IsEmpty() || Key > Regions.Last().Y + 2 * FadeKeys)
				Regions.Emplace(Key, Key);
			else
				Regions.Last().Y = Key;
		}
		if (!bNeedsChange) { continue; }
		for (const FIntPoint Region : Regions)
		{
			const int32 First = FMath::Max(0, Region.X - FadeKeys);
			const int32 Last = FMath::Min(Frames.Num() - 1, Region.Y + FadeKeys);
			const auto Alpha = [&](int32 Key)
			{
				const int32 Dist = FMath::Max3(Region.X - Key, Key - Region.Y, 0);
				return FMath::SmoothStep(0., 1., FMath::Clamp(1. - double(Dist) / FadeKeys, 0., 1.));
			};
			double BestScore = -1., BestShift = TNumericLimits<double>::Max(), BestStrength = 0.;
			FVector BestClearance = FVector::ZeroVector;
			const FAnchor* Best = nullptr;
			for (const FAnchor& Anchor : Anchors)
			{
				for (const double Strength : {.35, .50, .65, .80, 1.})
				{
					FVector Min(TNumericLimits<double>::Max());
					double Shift = 0.;
					for (int32 Key = First; Key <= Last; ++Key)
					{
						const FFrame& Frame = Frames[Key];
						const FTransform Wrist = WristForArm(Frame, BlendArm(Frame.Arm, Anchor.Arm, Strength * Alpha(Key)));
						const FVector Clearance = Measure(Frame, Wrist);
						Min = Min.ComponentMin(Clearance);
						Shift = FMath::Max(Shift, FVector::Distance(Wrist.GetLocation(), Frame.OriginalWrist));
					}
					const double Score = FMath::Min3(1., Min.X / HeadClearance, Min.Y / NeckClearance);
					if (Score < BestScore || (Score == BestScore && Shift >= BestShift)) { continue; }
					Best = &Anchor;
					BestScore = Score;
					BestShift = Shift;
					BestStrength = Strength;
					BestClearance = Min;
				}
			}
			if (!Best) { return false; }
			bAllClear &= BestScore >= 1.;
			UE_LOG(LogTemp, Display, TEXT("SpellbladeCastClearanceTrial %s keys %d-%d %s weight %.2f head %.2f neck %.2f tip Z %.2f max wrist shift %.2f cm"),
				Role, Region.X, Region.Y, *Best->Name, BestStrength, BestClearance.X, BestClearance.Y, BestClearance.Z, BestShift);
			for (int32 Key = First; Key <= Last; ++Key)
				Result[Key] = BlendArm(Frames[Key].Arm, Best->Arm, BestStrength * Alpha(Key));
		}
		if (!Regions.IsEmpty()) { Pending.Emplace(Clip, MoveTemp(Result)); }
	}
	if (!bApply) { return true; }
	if (!bAllClear) { return false; }
	for (auto& Pair : Pending)
	{
		for (int32 Bone = 0; Bone < ArmCount; ++Bone)
		{
			TArray<FTransform> Keys;
			for (const FArmPose& Arm : Pair.Value) { Keys.Add(Arm.Bones[Bone]); }
			if (!SetTrack(*Pair.Key, ArmBones[Bone], Keys)) { return false; }
		}
	}
	for (const auto& Pair : Pending)
	{
		const int32 Samples = FMath::CeilToInt(Pair.Key->GetPlayLength() * 60.);
		FVector Min(TNumericLimits<double>::Max());
		for (int32 Sample = 0; Sample <= Samples; ++Sample)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Pair.Key, Pair.Key->GetPlayLength() * Sample / Samples, Options, Pose);
			if (!Pose.IsValid()) { return false; }
			const auto Read = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World); };
			FFrame Frame;
			Frame.Head = Read(TEXT("head")).GetLocation();
			Frame.Neck = Read(TEXT("neck_01")).GetLocation();
			Min = Min.ComponentMin(Measure(Frame, Read(TEXT("hand_r"))));
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeCastClearanceVerified %s head %.2f neck %.2f tip Z %.2f cm at 60 Hz"), *Pair.Key->GetName(), Min.X, Min.Y, Min.Z);
		if (Min.X < HeadClearance - .5 || Min.Y < NeckClearance - .5) { return false; }
	}
	for (const auto& Pair : Pending) { if (!Save(Pair.Key)) { return false; } }
	return true;
}

bool RaiseWeaponTipAtElbow(const FVector& Tip, double Height, FTransform& Elbow, FQuat& Raise)
{
	Raise = FQuat::Identity;
	const double Need = Height + 5. - Tip.Z;
	if (Need <= 0.) { return true; }
	// Round the onset so the correction enters and exits with zero velocity.
	const double Lift = Need < 10. ? Need * Need / 20. : Need - 5.;
	const FVector Lever = Tip - Elbow.GetLocation();
	const double Radius = Lever.Size();
	const FVector Axis = FVector::CrossProduct(Lever, FVector::UpVector).GetSafeNormal();
	if (Axis.IsNearlyZero() || Radius <= UE_SMALL_NUMBER || Lever.Z + Lift > Radius) { return false; }
	const double Angle = FMath::Asin(FMath::Clamp((Lever.Z + Lift) / Radius, -1., 1.))
		- FMath::Asin(FMath::Clamp(Lever.Z / Radius, -1., 1.));
	Raise = FQuat(Axis, Angle);
	Elbow.SetRotation((Raise * Elbow.GetRotation()).GetNormalized());
	return true;
}

bool AuthorCastGroundClearance(bool bApply)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* TipSocket = Weapon ? Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip")) : nullptr;
	const auto* GripSocket = Weapon ? Weapon->FindSocket(StancePrimaryGripName()) : nullptr;
	FTransform WeaponInHand;
	if (!Body || !TipSocket || !GripSocket || !ResolveStanceWeaponMount(*Body, *Weapon, WeaponInHand)) { return false; }
	const FVector TipInHand = (TipSocket->GetSocketLocalTransform() * WeaponInHand).GetLocation();
	const FVector GripInHand = (GripSocket->GetSocketLocalTransform() * WeaponInHand).GetLocation();
	const FName Lower(TEXT("lowerarm_r")), Hand(TEXT("hand_r"));
	const auto& Ref = Body->GetRefSkeleton();
	const int32 LowerIndex = Ref.FindBoneIndex(Lower), HandIndex = Ref.FindBoneIndex(Hand);
	if (LowerIndex <= 0 || HandIndex <= 0 || Ref.GetParentIndex(HandIndex) != LowerIndex) { return false; }
	const FName Parent = Ref.GetBoneName(Ref.GetParentIndex(LowerIndex));
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	const auto Read = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
	};
	const auto Measure = [&](const FAnimPose& Pose, const FTransform& Wrist)
	{
		const FVector Tip = Wrist.TransformPosition(TipInHand), Grip = Wrist.TransformPosition(GripInHand);
		const FVector Head = Read(Pose, TEXT("Bip01_Head")).GetLocation();
		const FVector Neck = Read(Pose, TEXT("Bip01_Neck")).GetLocation();
		return FVector(FVector::Distance(Head, FMath::ClosestPointOnSegment(Head, Grip, Tip)),
			FVector::Distance(Neck, FMath::ClosestPointOnSegment(Neck, Grip, Tip)), Tip.Z);
	};
	struct FGroundTarget { const TCHAR* Role; double Height; };
	// The measured flat-ground composition can lower the tip below the
	// isolated pose. Keep the reserve in the source trajectory.
	const FGroundTarget Targets[] = {{TEXT("Buff"), 35.}, {TEXT("CastLeft"), 40.}, {TEXT("CastRight"), 37.}};
	TArray<UAnimSequence*> Pending;
	for (const FGroundTarget& Target : Targets)
	{
		auto* Clip = Asset<UAnimSequence>(CastPerformancePath + Target.Role);
		if (!Clip) { return false; }
		auto* Candidate = bApply ? Clip : DuplicateObject<UAnimSequence>(Clip, GetTransientPackage());
		if (!Candidate) { return false; }
		TArray<FTransform> Keys;
		double WorstAngle = 0., WorstWristShift = 0.;
		FVector Before(TNumericLimits<double>::Max());
		for (int32 Key = 0; Key < Clip->GetDataModel()->GetNumberOfKeys(); ++Key)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtFrame(Clip, Key, Options, Pose);
			if (!Pose.IsValid()) { return false; }
			FTransform Elbow = Read(Pose, Lower);
			const FTransform Wrist = Read(Pose, Hand);
			const FVector Tip = Wrist.TransformPosition(TipInHand);
			Before = Before.ComponentMin(Measure(Pose, Wrist));
			FQuat Raise;
			if (!RaiseWeaponTipAtElbow(Tip, Target.Height, Elbow, Raise)) { return false; }
			WorstAngle = FMath::Max(WorstAngle, FMath::RadiansToDegrees(Raise.GetAngle()));
			const FVector Offset = Wrist.GetLocation() - Elbow.GetLocation();
			WorstWristShift = FMath::Max(WorstWristShift, FVector::Distance(Raise.RotateVector(Offset), Offset));
			Keys.Add(Elbow.GetRelativeTransform(Read(Pose, Parent)));
		}
		if (Before.Z >= Target.Height - .6)
		{
			UE_LOG(LogTemp, Display, TEXT("SpellbladeCastGround %s already clears at %.2f cm"), Target.Role, Before.Z);
			continue;
		}
		if (!SetTrack(*Candidate, Lower, Keys)) { return false; }
		FVector After(TNumericLimits<double>::Max());
		const double Length = Candidate->GetPlayLength();
		const int32 Samples = FMath::CeilToInt(Length * 120.);
		for (int32 Sample = 0; Sample <= Samples; ++Sample)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Candidate, Length * (double(Sample) / Samples), Options, Pose);
			if (!Pose.IsValid()) { return false; }
			After = After.ComponentMin(Measure(Pose, Read(Pose, Hand)));
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeCastGround %s tip %.2f -> %.2f cm, head %.2f -> %.2f, neck %.2f -> %.2f, elbow %.2f deg, wrist %.2f cm"),
			Target.Role, Before.Z, After.Z, Before.X, After.X, Before.Y, After.Y, WorstAngle, WorstWristShift);
		if (bApply && (After.Z < Target.Height - .8 || After.X < FMath::Max(16., Before.X - .25)
			|| After.Y < FMath::Max(20., Before.Y - .25) || WorstAngle > 28. || WorstWristShift > 10.))
		{
			return false;
		}
		Pending.Add(Candidate);
	}
	if (bApply) for (auto* Clip : Pending) { if (!Save(Clip)) { return false; } }
	return true;
}

const FName ForwardGuardSupport[] = {TEXT("spine_03"), TEXT("spine_04"), TEXT("spine_05"),
	TEXT("clavicle_r"), TEXT("clavicle_l")};

// Retain the forward clip's cadence, but build its upper guard around the
// Ready posture and the same rigid grip used by the runtime attachment.
bool FitForwardGuard(UAnimSequence& Forward, UAnimSequence& Ready)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* Defaults = StanceBodyDefaults();
	const auto* Profile = Defaults ? Defaults->HandGripProfile.Get() : nullptr;
	if (!Body || !Weapon || !Profile || Forward.GetSkeleton() != Body->GetSkeleton()
		|| Ready.GetSkeleton() != Body->GetSkeleton()) { return false; }
	const auto& Calibration = Profile->Calibration;
	const auto* Palm = Body->FindSocket(Calibration.SecondaryPalmSocketName);
	const auto* Grip = Weapon->FindSocket(TEXT("WeaponGrip_L"));
	const auto* Tip = Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip"));
	FTransform WeaponInHand;
	if (!Palm || Palm->BoneName != TEXT("hand_l") || !Grip || !Tip
		|| Grip->BoneName != Weapon->GetRefSkeleton().GetBoneName(0)
		|| Tip->BoneName != Grip->BoneName || !ResolveStanceWeaponMount(*Body, *Weapon, WeaponInHand)) { return false; }

	const FName Chest(TEXT("spine_05"));
	const FName Arms[][3] = {{TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")},
		{TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l")}};
	const auto& Support = ForwardGuardSupport;
	const double SupportWeights[] = {.42, .61, .72, .40, .40};
	constexpr float GripWeight = .62f;
	constexpr float BladeWeight = .70f;
	const FName ChangedBones[] = {Support[0], Support[1], Support[2], Support[3], Support[4],
		Arms[0][0], Arms[0][1], Arms[0][2], Arms[1][0], Arms[1][1], Arms[1][2]};
	const int32 Count = Forward.GetDataModel()->GetNumberOfKeys();
	if (Count < 2) { return false; }
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	FAnimPose Reference;
	UAnimPoseExtensions::GetAnimPoseAtFrame(&Ready, 0, Options, Reference);
	if (!Reference.IsValid()) { return false; }
	const auto World = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
	};
	const auto Local = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::Local);
	};
	const FTransform ReadyRight = World(Reference, Arms[0][2]);
	const FTransform ReadyChest = World(Reference, Chest);
	const FTransform ReadyGrip = ReadyRight.GetRelativeTransform(ReadyChest);
	const FQuat ReadyLeft = World(Reference, Arms[1][2]).GetRelativeTransform(ReadyRight).GetRotation();
	const FTransform PalmInHand = Palm->GetSocketLocalTransform();
	const FTransform LeftContact = Calibration.SecondaryHandOffset * Grip->GetSocketLocalTransform() * WeaponInHand;
	const FVector TipInHand = (Tip->GetSocketLocalTransform() * WeaponInHand).GetLocation();
	auto* Candidate = DuplicateObject<UAnimSequence>(&Forward, GetTransientPackage());
	if (!Candidate) { return false; }

	TArray<FTransform> Keys[UE_ARRAY_COUNT(ChangedBones)];
	TArray<FTransform> Grips;
	TArray<FQuat> LeftRotations;
	for (int32 Key = 0; Key < Count; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Forward, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		FQuat ParentRotation = World(Pose, TEXT("spine_02")).GetRotation();
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Support); ++Index)
		{
			FTransform Joint = Local(Pose, Support[Index]);
			if (Index < 3)
			{
				// The guard overlay reads mesh-space rotation. Match the upper
				// posture there so the source clip's pelvis tilt isn't carried in.
				const FQuat Rotation = FQuat::Slerp(World(Pose, Support[Index]).GetRotation(),
					World(Reference, Support[Index]).GetRotation(), SupportWeights[Index]).GetNormalized();
				Joint.SetRotation((ParentRotation.Inverse() * Rotation).GetNormalized());
				ParentRotation = Rotation;
			}
			else
			{
				Joint.SetRotation(FQuat::Slerp(Joint.GetRotation(), Local(Reference, Support[Index]).GetRotation(), SupportWeights[Index]).GetNormalized());
			}
			Keys[Index].Add(Joint);
		}
		const FTransform Right = World(Pose, Arms[0][2]);
		const FTransform OriginalGrip = Right.GetRelativeTransform(World(Pose, Chest));
		FTransform GripInChest;
		GripInChest.Blend(OriginalGrip, ReadyGrip, GripWeight);
		GripInChest.SetRotation(FQuat::Slerp(OriginalGrip.GetRotation(), ReadyGrip.GetRotation(), BladeWeight).GetNormalized());
		Grips.Add(GripInChest);
		LeftRotations.Add(FQuat::Slerp(World(Pose, Arms[1][2]).GetRelativeTransform(Right).GetRotation(), ReadyLeft, GripWeight).GetNormalized());
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Support); ++Index)
	{
		if (!SetTrack(*Candidate, Support[Index], Keys[Index])) { return false; }
	}
	const auto& Ref = Body->GetRefSkeleton();
	for (int32 Key = 0; Key < Count; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(Candidate, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		const FTransform ChestPose = World(Pose, Chest);
		const FQuat ChestDelta = ChestPose.GetRotation() * ReadyChest.GetRotation().Inverse();
		FTransform Wrists[] = {Grips[Key] * ChestPose, World(Pose, Arms[1][2])};
		Wrists[1].SetRotation((Wrists[0].GetRotation() * LeftRotations[Key]).GetNormalized());
		Wrists[1].SetLocation((LeftContact * Wrists[0]).GetLocation() - Wrists[1].TransformVector(PalmInHand.GetLocation()));
		FVector Shift = FVector::ZeroVector;
		for (int32 Iteration = 0; Iteration < 8; ++Iteration)
		{
			for (int32 Side = 0; Side < 2; ++Side)
			{
				const FVector Shoulder = World(Pose, Arms[Side][0]).GetLocation();
				const FVector Elbow = World(Pose, Arms[Side][1]).GetLocation();
				const double Reach = FVector::Distance(Shoulder, Elbow) + FVector::Distance(Elbow, World(Pose, Arms[Side][2]).GetLocation());
				const FVector Arm = Wrists[Side].GetLocation() + Shift - Shoulder;
				Shift -= Arm.GetSafeNormal() * FMath::Max(0., Arm.Length() - (Reach - 3.5));
			}
		}
		for (int32 Side = 0; Side < 2; ++Side)
		{
			const FName* Arm = Arms[Side];
			const FTransform Upper = World(Pose, Arm[0]), Lower = World(Pose, Arm[1]), Wrist = World(Pose, Arm[2]);
			const FTransform& WristBind = Ref.GetRefBonePose()[Ref.FindBoneIndex(Arm[2])];
			FTransform NewUpper, NewLower;
			Wrists[Side].AddToTranslation(Shift);
			const FVector Axis = (Wrists[Side].GetLocation() - Upper.GetLocation()).GetSafeNormal();
			const FVector ReadyPole = ChestDelta.RotateVector(World(Reference, Arm[1]).GetLocation() - World(Reference, Arm[0]).GetLocation());
			const FVector Pole = FMath::Lerp(Lower.GetLocation() - Upper.GetLocation(), ReadyPole, GripWeight);
			const FVector NeutralWrist = WristBind.GetRotation().UnrotateVector(WristBind.GetLocation()).GetSafeNormal();
			const FVector GripPole = FVector::VectorPlaneProject(-Wrists[Side].TransformVectorNoScale(NeutralWrist), Axis).GetSafeNormal();
			const FVector Elbow = FVector::VectorPlaneProject(Pole, Axis).GetSafeNormal() + (Side == 1 ? 1.75 : .75) * GripPole;
			if (!SolveReadyArm(Upper, Lower, Wrist, Wrists[Side], NewUpper, NewLower, &Elbow)) { return false; }
			DistributeCarryRoll(Wrists[Side], WristBind.GetRotation(), NewLower, true);
			const int32 Index = UE_ARRAY_COUNT(Support) + 3 * Side;
			Keys[Index].Add(NewUpper.GetRelativeTransform(Local(Pose, Arm[0]).Inverse() * Upper));
			Keys[Index + 1].Add(NewLower.GetRelativeTransform(NewUpper));
			Keys[Index + 2].Add(Wrists[Side].GetRelativeTransform(NewLower));
		}
	}
	for (int32 Index = UE_ARRAY_COUNT(Support); Index < UE_ARRAY_COUNT(ChangedBones); ++Index)
	{
		if (!SetTrack(*Candidate, ChangedBones[Index], Keys[Index])) { return false; }
	}
	if (!FitCarryContact(*Candidate, *Body, WeaponInHand, Grip->GetSocketLocalTransform(), PalmInHand, Calibration.SecondaryHandOffset,
		Reference, TipInHand, Defaults->LeftHandIKCurveName, false, true)) { return false; }
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(ChangedBones); ++Index)
	{
		if (!SetTrack(Forward, ChangedBones[Index], Keys[Index])) { return false; }
	}
	return true;
}

constexpr TCHAR ForwardGuardMarker[] = TEXT("ProjectJ.LongbladeForwardGuard");

bool AuthorForwardGuard(bool bApply)
{
	auto* Forward = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + TEXT("Forward"));
	auto* Ready = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + TEXT("Ready"));
	if (!Forward || !Ready) { return false; }
	auto& Metadata = Forward->GetOutermost()->GetMetaData();
	if (Metadata.HasValue(Forward, ForwardGuardMarker)) { return PrepareCarryContact(false); }
	auto* Target = bApply ? Forward : DuplicateObject<UAnimSequence>(Forward, GetTransientPackage());
	if (!Target || !FitForwardGuard(*Target, *Ready)) { return false; }
	if (!bApply) { return true; }
	Metadata.SetValue(Forward, ForwardGuardMarker, TEXT("1"));
	return Save(Forward);
}

constexpr TCHAR StrafeCarryMarker[] = TEXT("ProjectJ.LongbladeStrafeCarry");
constexpr TCHAR StrafeCarryVersion[] = TEXT("2");
constexpr TCHAR LeftStrafeBlendVersion[] = TEXT("3");

double StrafeCarryLift(FStringView Take)
{
	// The right strafe loop banks farther into the blade than the left loop.
	if (Take == TEXT("Left")) { return 18.; }
	if (Take == TEXT("Right")) { return 32.; }
	return 0.;
}

bool FitStrafeCarry(UAnimSequence& Clip, double BladeLiftDegrees, const UAnimSequence& RightArmReference)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* Tip = Weapon ? Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip")) : nullptr;
	FTransform WeaponInHand;
	if (!Body || !Tip || !ResolveStanceWeaponMount(*Body, *Weapon, WeaponInHand)) { return false; }
	const FName Chest(TEXT("spine_05")), RightUpper(TEXT("upperarm_r")), RightLower(TEXT("lowerarm_r")), RightHand(TEXT("hand_r"));
	const FName LeftUpper(TEXT("upperarm_l")), LeftLower(TEXT("lowerarm_l")), LeftHand(TEXT("hand_l"));
	const FName Bones[] = {RightUpper, RightLower, RightHand, LeftUpper, LeftLower, LeftHand};
	const FVector TipInHand = (Tip->GetSocketLocalTransform() * WeaponInHand).GetLocation();
	FAnimPose Reference;
	UAnimPoseExtensions::GetReferencePose(Body->GetSkeleton(), Reference);
	if (!Reference.IsValid()) { return false; }
	const FVector ChestUp = UAnimPoseExtensions::GetBonePose(Reference, Chest, EAnimPoseSpaces::World)
		.InverseTransformVectorNoScale(FVector::UpVector);
	const FQuat BindRightWrist = UAnimPoseExtensions::GetBonePose(Reference, RightHand, EAnimPoseSpaces::Local).GetRotation();
	const FTransform BindLeftHand = UAnimPoseExtensions::GetBonePose(Reference, LeftHand, EAnimPoseSpaces::Local);
	const FVector NeutralLeft = BindLeftHand.InverseTransformVectorNoScale(BindLeftHand.GetLocation()).GetSafeNormal();
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	const int32 Count = Clip.GetDataModel()->GetNumberOfKeys();
	if (Count < 2) { return false; }
	FAnimPose SeedPose, RightSeedPose;
	UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Count / 2, Options, SeedPose);
	UAnimPoseExtensions::GetAnimPoseAtFrame(&RightArmReference, RightArmReference.GetDataModel()->GetNumberOfKeys() / 2, Options, RightSeedPose);
	if (!SeedPose.IsValid() || !RightSeedPose.IsValid()) { return false; }
	FTransform SeedInChest[UE_ARRAY_COUNT(Bones)];
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
	{
		const FAnimPose& ArmSeed = Index < 3 ? RightSeedPose : SeedPose;
		const FTransform SeedChest = UAnimPoseExtensions::GetBonePose(ArmSeed, Chest, EAnimPoseSpaces::World);
		SeedInChest[Index] = UAnimPoseExtensions::GetBonePose(ArmSeed, Bones[Index], EAnimPoseSpaces::World).GetRelativeTransform(SeedChest);
	}
	TArray<FTransform> Keys[UE_ARRAY_COUNT(Bones)];
	// Restore the blade margin measured at full strafe speed and during pivots.
	// A constant chest-relative pitch preserves each source loop's swing.
	for (int32 Key = 0; Key < Count; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		const auto Read = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World); };
		const auto Local = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::Local); };
		FTransform Old[UE_ARRAY_COUNT(Bones)], Target[UE_ARRAY_COUNT(Bones)];
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index) { Target[Index] = Old[Index] = Read(Bones[Index]); }
		const FTransform CurrentChest = Read(Chest);
		const FVector Up = CurrentChest.TransformVectorNoScale(ChestUp).GetSafeNormal();
		const FVector Lever = Old[2].TransformPosition(TipInHand) - Old[2].GetLocation();
		const FVector Axis = FVector::CrossProduct(Lever, Up).GetSafeNormal();
		if (Axis.IsNearlyZero()) { return false; }
		Target[2].SetRotation((FQuat(Axis, FMath::DegreesToRadians(BladeLiftDegrees)) * Old[2].GetRotation()).GetNormalized());
		Target[5] = Old[5].GetRelativeTransform(Old[2]) * Target[2];
		FVector Shift = FVector::ZeroVector;
		for (int32 Iteration = 0; Iteration < 8; ++Iteration)
		{
			for (const int32 Upper : {0, 3})
			{
				const double Length = FVector::Distance(Old[Upper].GetLocation(), Old[Upper + 1].GetLocation())
					+ FVector::Distance(Old[Upper + 1].GetLocation(), Old[Upper + 2].GetLocation());
				const FVector Reach = Target[Upper + 2].GetLocation() + Shift - Old[Upper].GetLocation();
				Shift -= Reach.GetSafeNormal() * FMath::Max(0., Reach.Length() - (Length - 3.5));
			}
		}
		Target[2].AddToTranslation(Shift);
		Target[5].AddToTranslation(Shift);
		// Rebuild the elbow plane and roll from a common chest-relative pose.
		// Reusing each source frame's forearm as the seed preserves its twist flips.
		FTransform Seed[UE_ARRAY_COUNT(Bones)];
		for (const int32 Upper : {0, 3})
		{
			const FVector Anchor = Old[Upper].GetLocation() - (SeedInChest[Upper] * CurrentChest).GetLocation();
			for (int32 Index = Upper; Index < Upper + 3; ++Index)
			{
				Seed[Index] = SeedInChest[Index] * CurrentChest;
				Seed[Index].AddToTranslation(Anchor);
			}
		}
		const FVector LeftReach = (Target[5].GetLocation() - Old[3].GetLocation()).GetSafeNormal();
		const FVector LeftElbow = FVector::VectorPlaneProject(Seed[4].GetLocation() - Seed[3].GetLocation(), LeftReach).GetSafeNormal();
		const FVector WristPole = FVector::VectorPlaneProject(-Target[5].TransformVectorNoScale(NeutralLeft), LeftReach).GetSafeNormal();
		const FVector LeftPole = LeftElbow + .75 * WristPole;
		if (!SolveReadyArm(Seed[0], Seed[1], Seed[2], Target[2], Target[0], Target[1])
			|| !SolveReadyArm(Seed[3], Seed[4], Seed[5], Target[5], Target[3], Target[4], &LeftPole))
		{
			UE_LOG(LogTemp, Error, TEXT("SpellbladeStrafeCarry %s frame %d cannot reach the paired grip"), *Clip.GetName(), Key);
			return false;
		}
		DistributeCarryRoll(Target[2], BindRightWrist, Target[1], true);
		DistributeCarryRoll(Target[5], BindLeftHand.GetRotation(), Target[4], true);
		for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
		{
			const FTransform Parent = (Index % 3) ? Target[Index - 1] : Local(Bones[Index]).Inverse() * Old[Index];
			Keys[Index].Add(Target[Index].GetRelativeTransform(Parent));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeStrafeCarry %s blade lift %.2f deg"), *Clip.GetName(), BladeLiftDegrees);
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
	{
		double PeakStep = 0.;
		for (int32 Key = 1; Key < Count; ++Key)
		{
			PeakStep = FMath::Max(PeakStep, Keys[Index][Key - 1].GetRotation().AngularDistance(Keys[Index][Key].GetRotation()));
		}
		const double Step = FMath::RadiansToDegrees(PeakStep);
		const double Seam = FMath::RadiansToDegrees(Keys[Index][0].GetRotation().AngularDistance(Keys[Index].Last().GetRotation()));
		UE_LOG(LogTemp, Display, TEXT("SpellbladeStrafeContinuity %s %s step %.2f deg, loop %.2f deg"),
			*Clip.GetName(), *Bones[Index].ToString(), Step, Seam);
		if (Step > 20. || Seam > 12.) { return false; }
	}
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Bones); ++Index)
	{
		if (!SetTrack(Clip, Bones[Index], Keys[Index])) { return false; }
	}
	return true;
}

bool VerifyGuardBlendArmEnvelope(const UAnimSequence* TrialLeft = nullptr, const UAnimSequence* TrialReady = nullptr)
{
	const FName Bones[] = {TEXT("clavicle_l"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("hand_l"),
		TEXT("clavicle_r"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")};
	TArray<FTransform> Keys[UE_ARRAY_COUNT(GuardTakes)][UE_ARRAY_COUNT(Bones)];
	for (int32 Take = 0; Take < UE_ARRAY_COUNT(GuardTakes); ++Take)
	{
		const FStringView Name(GuardTakes[Take].Name);
		const UAnimSequence* Clip = Name == TEXT("Ready") ? TrialReady : Name == TEXT("Left") ? TrialLeft : nullptr;
		if (!Clip) { Clip = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + GuardTakes[Take].Name); }
		if (!Clip) { return false; }
		for (int32 Bone = 0; Bone < UE_ARRAY_COUNT(Bones); ++Bone)
		{
			Clip->GetDataModel()->GetBoneTrackTransforms(Bones[Bone], Keys[Take][Bone]);
			if (Keys[Take][Bone].IsEmpty()) { return false; }
			const FQuat Reference = Keys[0][Bone][0].GetRotation();
			for (FTransform& Key : Keys[Take][Bone])
			{
				const FQuat Rotation = Key.GetRotation();
				if ((Rotation | Reference) < 0.) { Key.SetRotation(Rotation * -1.); }
			}
		}
	}
	// Different speeds or markers can put neighboring samples at different phases.
	// One consistent hemisphere also keeps the sign stable when three samples contribute.
	constexpr double MaxSeparation = 160.;
	bool bValid = true;
	for (int32 A = 0; A < UE_ARRAY_COUNT(GuardTakes); ++A)
	{
		for (int32 B = A + 1; B < UE_ARRAY_COUNT(GuardTakes); ++B)
		{
			double Peak = 0.;
			FName PeakBone;
			for (int32 Bone = 0; Bone < UE_ARRAY_COUNT(Bones); ++Bone)
			{
				for (const FTransform& X : Keys[A][Bone])
				{
					for (const FTransform& Y : Keys[B][Bone])
					{
						const double Dot = X.GetRotation() | Y.GetRotation();
						const double Angle = 2. * FMath::Acos(FMath::Clamp(Dot, -1., 1.));
						if (Angle > Peak) { Peak = Angle; PeakBone = Bones[Bone]; }
					}
				}
			}
			const double Degrees = FMath::RadiansToDegrees(Peak);
			UE_LOG(LogTemp, Display, TEXT("SpellbladeGuardBlendEnvelope %s %s %.2f deg %s"),
				GuardTakes[A].Name, GuardTakes[B].Name, Degrees, *PeakBone.ToString());
			bValid &= Degrees < MaxSeparation;
		}
	}
	return bValid;
}

const FString ReadyChestGuardReviewPath = Sequences + TEXT("SourceTransient/StanceAudit/AS_LongbladeReadyChestGuardReview");

bool TrialReadyGuard(bool bSave)
{
	auto* Ready = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + TEXT("Ready"));
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* Defaults = StanceBodyDefaults();
	const auto* Profile = Defaults ? Defaults->HandGripProfile.Get() : nullptr;
	if (!Ready || !Body || !Weapon || !Profile || Weapon->GetRefSkeleton().GetNum() == 0) { return false; }
	const auto& Calibration = Profile->Calibration;
	const auto* Palm = Body->FindSocket(Calibration.SecondaryPalmSocketName);
	const auto* Grip = Weapon->FindSocket(TEXT("WeaponGrip_L"));
	const auto* Tip = Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip"));
	const FName Root = Weapon->GetRefSkeleton().GetBoneName(0);
	FTransform WeaponInHand;
	if (!Palm || Palm->BoneName != TEXT("hand_l") || !Grip || !Tip
		|| Grip->BoneName != Root || Tip->BoneName != Root
		|| !ResolveStanceWeaponMount(*Body, *Weapon, WeaponInHand)) { return false; }

	const FVector TipInHand = WeaponInHand.TransformPosition(Tip->RelativeLocation);
	const FReadyGuardAdjustment Adjustment{16.};
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	Options.bShouldRetarget = false;
	FAnimPose Pose;
	UAnimPoseExtensions::GetAnimPoseAtFrame(Ready, 0, Options, Pose);
	if (!Pose.IsValid()) { return false; }
	auto* Review = bSave ? Duplicate(Ready, ReadyChestGuardReviewPath)
		: DuplicateObject<UAnimSequence>(Ready, GetTransientPackage());
	if (!Review) { return false; }
	const auto Check = [&](bool bFit)
	{
		return FitCarryContact(*Review, *Body, WeaponInHand, Grip->GetSocketLocalTransform(), Palm->GetSocketLocalTransform(),
			Calibration.SecondaryHandOffset, Pose, TipInHand, Defaults->LeftHandIKCurveName, bFit, false, false,
			bFit ? &Adjustment : nullptr);
	};
	return Check(true) && Check(false) && VerifyGuardBlendArmEnvelope(nullptr, Review) && (!bSave || Save(Review));
}

bool InstallReadyGuardReview()
{
	auto* Ready = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + TEXT("Ready"));
	const auto* Review = Asset<UAnimSequence>(ReadyChestGuardReviewPath);
	const auto* Blend = Asset<UBlendSpace>(BlendPath);
	if (!Ready || !Review || !Blend || Ready->GetSkeleton() != Review->GetSkeleton()
		|| Ready->GetDataModel()->GetFrameRate() != Review->GetDataModel()->GetFrameRate()
		|| Ready->GetDataModel()->GetNumberOfKeys() != Review->GetDataModel()->GetNumberOfKeys()
		|| !VerifyStance(*Blend) || !VerifyGuardBlendArmEnvelope(nullptr, Review)) { return false; }
	for (const auto& Sample : Blend->GetBlendSamples())
	{
		if (FMath::IsNearlyZero(Sample.SampleValue.Y) != (Sample.Animation == Ready)) { return false; }
	}
	for (const FName Bone : CarryArmBones)
	{
		if (!Review->GetDataModel()->IsValidBoneTrackName(Bone)) { return false; }
		TArray<FTransform> Keys;
		Review->GetDataModel()->GetBoneTrackTransforms(Bone, Keys);
		if (Keys.IsEmpty() || !SetTrack(*Ready, Bone, Keys)) { return false; }
	}
	return Save(Ready);
}

bool AuthorStrafeCarry(bool bApply = true)
{
	auto* Right = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + TEXT("Right"));
	auto* Left = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + TEXT("Left"));
	if (!Right || !Left) { return false; }
	const FString RightVersion = Right->GetOutermost()->GetMetaData().GetValue(Right, StrafeCarryMarker);
	const FString LeftVersion = Left->GetOutermost()->GetMetaData().GetValue(Left, StrafeCarryMarker);
	const bool bChangeRight = RightVersion != StrafeCarryVersion;
	const bool bChangeLeft = LeftVersion != LeftStrafeBlendVersion;
	if (!bApply && bChangeRight) { return false; }
	if (bChangeRight && !FitStrafeCarry(*Right, RightVersion.IsEmpty() ? StrafeCarryLift(TEXT("Right")) : 0., *Right)) { return false; }
	if (bChangeLeft)
	{
		if (!bApply) { Left = DuplicateObject<UAnimSequence>(Left, GetTransientPackage()); }
		// Left and Right share the same weapon arm plane before they blend with Idle and Forward.
		if (!Left || !FitStrafeCarry(*Left, LeftVersion.IsEmpty() ? StrafeCarryLift(TEXT("Left")) : 0., *Right)) { return false; }
	}
	if (!VerifyGuardBlendArmEnvelope(Left)) { return false; }
	if (!bApply) { return true; }
	if (!PrepareCarryContact(false)) { return false; }
	if (bChangeRight)
	{
		Right->GetOutermost()->GetMetaData().SetValue(Right, StrafeCarryMarker, StrafeCarryVersion);
		if (!Save(Right)) { return false; }
	}
	if (bChangeLeft)
	{
		Left->GetOutermost()->GetMetaData().SetValue(Left, StrafeCarryMarker, LeftStrafeBlendVersion);
		if (!Save(Left)) { return false; }
	}
	return true;
}

bool InstallStanceGripTrial()
{
	TArray<UAnimSequence*> Targets;
	UAnimSequence* Forward = nullptr;
	UAnimSequence* Left = nullptr;
	UAnimSequence* Right = nullptr;
	const FAnimationCurveIdentifier RotationCurve(Project_J::Animation::LeftHandCarryAuthoredRotationCurve, ERawCurveTrackTypes::RCT_Float);
	for (const auto& Take : GuardTakes)
	{
		auto* Target = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + Take.Name);
		const auto* Candidate = Asset<UAnimSequence>(Sequences + TEXT("SourceTransient/StanceAudit/SourceCarryTrial_") + Take.Name);
		if (!Target || !Candidate || Target->GetSkeleton() != Candidate->GetSkeleton()
			|| Target->GetDataModel()->GetFrameRate() != Candidate->GetDataModel()->GetFrameRate()
			|| Target->GetDataModel()->GetNumberOfKeys() != Candidate->GetDataModel()->GetNumberOfKeys()) { return false; }

		TArray<FName> Bones = {TEXT("root"), TEXT("spine_03"), TEXT("neck_01"), TEXT("upperarm_l"), TEXT("lowerarm_l"),
			TEXT("hand_l"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("hand_r")};
		if (FString(Take.Name) == TEXT("Forward"))
		{
			Forward = Target;
			for (FName Bone : ForwardGuardSupport) { Bones.AddUnique(Bone); }
		}
		for (const TCHAR* Finger : {TEXT("thumb"), TEXT("index"), TEXT("middle"), TEXT("ring"), TEXT("pinky")})
			for (int32 Joint = 1; Joint <= 3; ++Joint)
				Bones.Add(FName(*FString::Printf(TEXT("%s_%02d_l"), Finger, Joint)));
		for (FName Bone : Bones)
		{
			if (!Candidate->GetDataModel()->IsValidBoneTrackName(Bone)) { return false; }
			TArray<FTransform> Keys;
			Candidate->GetDataModel()->GetBoneTrackTransforms(Bone, Keys);
			if (Keys.IsEmpty() || !SetTrack(*Target, Bone, Keys)) { return false; }
		}
		auto& Controller = Target->GetController();
		if ((!Target->GetDataModel()->FindFloatCurve(RotationCurve) && !Controller.AddCurve(RotationCurve, AACF_DefaultCurve, false))
			|| !Controller.SetCurveKeys(RotationCurve, {{0.f, 1.f}, {Target->GetPlayLength(), 1.f}}, false)) { return false; }
		if (FStringView(Take.Name) == TEXT("Left")) { Left = Target; }
		if (FStringView(Take.Name) == TEXT("Right")) { Right = Target; }
		Targets.Add(Target);
	}
	if (!Forward || !Left || !Right || !FitForwardGuard(*Forward, *Targets[0])
		|| !FitStrafeCarry(*Right, StrafeCarryLift(TEXT("Right")), *Right)
		|| !FitStrafeCarry(*Left, StrafeCarryLift(TEXT("Left")), *Right)
		|| !VerifyGuardBlendArmEnvelope(Left) || !PrepareCarryContact(false)) { return false; }
	Forward->GetOutermost()->GetMetaData().SetValue(Forward, ForwardGuardMarker, TEXT("1"));
	Right->GetOutermost()->GetMetaData().SetValue(Right, StrafeCarryMarker, StrafeCarryVersion);
	Left->GetOutermost()->GetMetaData().SetValue(Left, StrafeCarryMarker, LeftStrafeBlendVersion);
	for (auto* Target : Targets) { if (!Save(Target)) { return false; } }
	return true;
}

bool AuthorGuardStance(const FString& Params, UBlendSpace& Blend)
{
	TArray<FStanceTake> Missing;
	for (const auto& Take : GuardTakes)
	{
		if (!FPackageName::DoesPackageExist(StanceFolder + FString(GuardPrefix) + Take.Name)) { Missing.Add(Take); }
	}
	if (!Missing.IsEmpty())
	{
		UAnimSequence* Source = nullptr;
		const auto Clips = RetargetStanceTakes(Params, Missing, GuardPrefix, true, StanceFolder, &Source);
		if (Clips.Num() != Missing.Num()) { return false; }
		for (auto* Clip : Clips)
		{
			if (Clip->GetName() == FString(GuardPrefix) + GuardTakes[0].Name && (!Source || !FitReadyGrip(*Clip, *Source)))
			{
				return false;
			}
		}
		for (auto* Clip : Clips) { if (!Save(Clip)) { return false; } }
	}
	for (int32 Index = 0; Index < Blend.GetBlendSamples().Num(); ++Index)
	{
		const auto& Sample = Blend.GetBlendSamples()[Index];
		auto* Clip = Asset<UAnimSequence>(StanceFolder + GuardSampleName(Sample.SampleValue));
		if (!Clip || (Sample.Animation != Clip && !Blend.ReplaceSampleAnimation(Index, Clip))) { return false; }
	}
	return UProject_JAssetMigrationLibrary::RebuildBlendSpaceInterpolation(&Blend) && VerifyStance(Blend)
		&& PrepareCarryContact(true) && AuthorStrafeCarry() && Save(&Blend);
}

bool PrepareStance(const FString& Params, UBlendSpace& Blend, bool bApply)
{
	if (bApply)
	{
		return AuthorGuardStance(Params, Blend);
	}

	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	if (!Body) { return false; }
	UE_LOG(LogTemp, Display, TEXT("SpellbladeStanceSkeleton Blend=%s Body=%s"),
		*GetPathNameSafe(Blend.GetSkeleton()), *GetPathNameSafe(Body->GetSkeleton()));
	{
		TSet<UAnimSequence*> Active;
		for (const auto& Sample : Blend.GetBlendSamples())
		{
			if (!Sample.Animation || Active.Contains(Sample.Animation)) { continue; }
			Active.Add(Sample.Animation);
			InspectStancePose(*Sample.Animation, *Body, false);
			FString OriginalName = Sample.Animation->GetName();
			if (OriginalName.RemoveFromEnd(TEXT("_Grip")))
			{
				if (auto* Original = Asset<UAnimSequence>(StanceFolder + OriginalName))
				{
					InspectStancePose(*Original, *Body, false);
				}
			}
		}
	}

	return !RetargetStanceTakes(Params, StanceTakes, TEXT("AuditBody_"), false).IsEmpty();
}

bool VerifyGuardPose(UProject_JCombatStyleDefinition& Style, UBlendSpace& Blend)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Node = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.Light.1")));
	const auto* Palm = Body ? Body->FindSocket(TEXT("PalmGrip_R")) : nullptr;
	if (!Palm || !Node || !Node->AttackDefinition || !VerifyStance(Blend)) { return false; }
	TSet<const UAnimSequence*> Checked;
	for (const auto& Sample : Blend.GetBlendSamples())
	{
		const auto* Clip = Sample.Animation.Get();
		if (Checked.Contains(Clip)) { continue; }
		Checked.Add(Clip);
		FAnimPoseEvaluationOptions Options;
		Options.OptionalSkeletalMesh = Body;
		Options.bShouldRetarget = false;
		double MinTipRise = TNumericLimits<double>::Max();
		double MinTipForward = TNumericLimits<double>::Max();
		for (int32 Index = 0; Index <= 12; ++Index)
		{
			FAnimPose Pose;
			UAnimPoseExtensions::GetAnimPoseAtTime(Clip, Clip->GetPlayLength() * Index / 12., Options, Pose);
			if (!Pose.IsValid()) { return false; }
			const FTransform Hand = UAnimPoseExtensions::GetBonePose(Pose, TEXT("hand_r"), EAnimPoseSpaces::World);
			const FTransform Grip = Palm->GetSocketLocalTransform() * Hand;
			const FVector Direction = Grip.TransformVectorNoScale(Node->AttackDefinition->HitSpec.CanonicalBladeTipOffset);
			MinTipRise = FMath::Min(MinTipRise, Direction.Z);
			MinTipForward = FMath::Min(MinTipForward, Direction.Y);
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeGuardVerified %s TipRise>=%.1fcm TipForward>=%.1fcm"),
			*Clip->GetName(), MinTipRise, MinTipForward);
	}
	return Checked.Num() == UE_ARRAY_COUNT(GuardTakes);
}

bool InspectGuardGraph()
{
	for (const TCHAR* Path : {TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master"),
		TEXT("/Game/Animation_Logic/ABPs/Spellblade/ABP_Spellblade")})
	{
		const auto* Blueprint = Asset<UAnimBlueprint>(Path);
		if (!Blueprint) { return false; }
		TArray<UEdGraph*> Graphs;
		Blueprint->GetAllGraphs(Graphs);
		for (const auto* Graph : Graphs)
		{
			for (const UEdGraphNode* Node : Graph->Nodes)
			{
				const FString Class = Node->GetClass()->GetName();
				if (!Class.StartsWith(TEXT("AnimGraphNode")) || (!Graph->GetName().Contains(TEXT("Combat"))
					&& !Class.Contains(TEXT("Layer")) && !Class.Contains(TEXT("Blend")) && !Class.Contains(TEXT("Slot")))) { continue; }
				FString Links;
				for (const auto* Pin : Node->Pins)
				{
					for (const auto* Target : Pin->LinkedTo)
					{
						Links += FString::Printf(TEXT(" %s=%s.%s"), *Pin->PinName.ToString(),
							*GetNameSafe(Target->GetOwningNode()), *Target->PinName.ToString());
					}
				}
				UE_LOG(LogTemp, Display, TEXT("GuardGraph %s %s %s %s"), *Blueprint->GetName(), *Graph->GetName(), *Node->GetName(), *Links);
				if (const auto* Layer = Cast<UAnimGraphNode_LayeredBoneBlend>(Node))
				{
					FString Bones;
					for (const auto& FilterSet : Layer->Node.LayerSetup)
					{
						for (const auto& Filter : FilterSet.BranchFilters)
						{
							Bones += FString::Printf(TEXT(" %s:%d"), *Filter.BoneName.ToString(), Filter.BlendDepth);
						}
					}
					for (const auto& Profile : Layer->Node.BlendMasks) { Bones += TEXT(" Mask=") + GetNameSafe(Profile); }
					UE_LOG(LogTemp, Display, TEXT("GuardGraphMask %s MeshSpace=%d Mode=%d%s"), *Layer->GetName(),
						Layer->Node.bMeshSpaceRotationBlend, int32(Layer->Node.BlendMode), *Bones);
				}
			}
		}
	}
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	auto* Clip = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + GuardTakes[0].Name);
	if (!Body || !Clip) { return false; }
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = Body;
	for (const bool bRetarget : {false, true})
	{
		Options.bShouldRetarget = bRetarget;
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(Clip, 0., Options, Pose);
		if (!Pose.IsValid()) { return false; }
		for (const FName Bone : {FName(TEXT("root")), FName(TEXT("pelvis")), FName(TEXT("spine_01")), FName(TEXT("spine_02")),
			FName(TEXT("spine_03")), FName(TEXT("spine_04")), FName(TEXT("spine_05")), FName(TEXT("clavicle_r")), FName(TEXT("upperarm_r")),
			FName(TEXT("lowerarm_r")), FName(TEXT("hand_r")), FName(TEXT("middle_01_r")), FName(TEXT("clavicle_l")),
			FName(TEXT("upperarm_l")), FName(TEXT("lowerarm_l")), FName(TEXT("hand_l")), FName(TEXT("middle_01_l"))})
		{
			const FTransform CS = UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
			const FTransform Local = UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::Local);
			UE_LOG(LogTemp, Display, TEXT("GuardAsset retarget=%d bone=%s pos={%s} q={%s} localQ={%s}"),
				bRetarget, *Bone.ToString(), *CS.GetLocation().ToString(), *CS.GetRotation().ToString(), *Local.GetRotation().ToString());
		}
	}
	return true;
}

UAnimGraphNode_LayeredBoneBlend* FindGuardLayer(UAnimBlueprint*& Blueprint)
{
	Blueprint = Asset<UAnimBlueprint>(TEXT("/Game/Animation_Logic/ABPs/Spellblade/ABP_Spellblade"));
	if (!Blueprint) { return nullptr; }
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (const auto* Graph : Graphs)
	{
		if (Graph->GetFName() != TEXT("CombatUpperBody")) { continue; }
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Layer = Cast<UAnimGraphNode_LayeredBoneBlend>(Node)) { return Layer; }
		}
	}
	return nullptr;
}

bool VerifyGuardLayer()
{
	UAnimBlueprint* Blueprint = nullptr;
	const auto* Layer = FindGuardLayer(Blueprint);
	if (!Layer || Layer->Node.BlendMode != ELayeredBoneBlendMode::BranchFilter
		|| Layer->Node.bMeshSpaceRotationBlend || Layer->Node.bRootSpaceRotationBlend
		|| Layer->Node.bMeshSpaceScaleBlend || Layer->Node.LayerSetup.Num() != 1) { return false; }
	const auto& Filters = Layer->Node.LayerSetup[0].BranchFilters;
	if (Filters.Num() != 2) { return false; }
	for (const FName Clavicle : {FName(TEXT("clavicle_l")), FName(TEXT("clavicle_r"))})
	{
		if (!Filters.ContainsByPredicate([Clavicle](const FBranchFilter& Filter)
		{
			return Filter.BoneName == Clavicle && Filter.BlendDepth == 0;
		})) { return false; }
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeGuardLayerVerified LocalSpace clavicle_l:0 clavicle_r:0"));
	return true;
}

bool ApplyGuardLayer()
{
	UAnimBlueprint* Blueprint = nullptr;
	auto* Layer = FindGuardLayer(Blueprint);
	if (!Layer || Layer->Node.BlendMode != ELayeredBoneBlendMode::BranchFilter
		|| Layer->Node.LayerSetup.Num() != 1) { return false; }
	if (VerifyGuardLayer()) { return true; }
	auto& Filters = Layer->Node.LayerSetup[0].BranchFilters;
	Blueprint->Modify();
	Layer->Modify();
	// Inherit the evaluated locomotion chest so the guard cannot bring the
	// source clip's spine or head posture into the two clavicle chains.
	Layer->Node.bMeshSpaceRotationBlend = false;
	Layer->Node.bRootSpaceRotationBlend = false;
	Layer->Node.bMeshSpaceScaleBlend = false;
	Filters = {{TEXT("clavicle_l"), 0}, {TEXT("clavicle_r"), 0}};
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	return Blueprint->Status != BS_Error && VerifyGuardLayer() && Save(Blueprint);
}

bool VerifySpellbladeAssetPreviews()
{
	auto& Registry = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get();
	const TArray<FString> Roots = {TEXT("/Game/Anim_Assets/Spellblade"), TEXT("/Game/Characters/Spellblade"),
		TEXT("/Game/Animation_Logic/BSs/Spellblade")};
	Registry.ScanPathsSynchronous(Roots);
	FARFilter Filter;
	Filter.bRecursivePaths = true;
	Filter.bRecursiveClasses = true;
	Filter.ClassPaths.Add(UAnimationAsset::StaticClass()->GetClassPathName());
	for (const FString& Root : Roots) { Filter.PackagePaths.Add(*Root); }
	TArray<FAssetData> Assets;
	Registry.GetAssets(Filter, Assets);
	FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false));
	UDebugSkelMeshComponent* Preview = nullptr;
	int32 Count = 0, Guarded = 0, Missing = 0;
	for (const FAssetData& Data : Assets)
	{
		auto* Animation = Cast<UAnimationAsset>(Data.GetAsset());
		USkeleton* Skeleton = Animation ? Animation->GetSkeleton() : nullptr;
		USkeletalMesh* Mesh = Skeleton ? Skeleton->GetAssetPreviewMesh(Animation) : nullptr;
		if (!Mesh)
		{
			UE_LOG(LogTemp, Display, TEXT("SpellbladePreviewMissing %s"), *Data.GetObjectPathString());
			++Missing;
			continue;
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladePreviewEvaluate %s using %s"), *Animation->GetPathName(), *Mesh->GetName());
		if (!Preview || Preview->GetSkeletalMeshAsset() != Mesh)
		{
			if (Preview) { Scene.RemoveComponent(Preview); }
			// Match the component and mesh assignment used by Persona's asset tooltip.
			Preview = NewObject<UDebugSkelMeshComponent>();
			Preview->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
			Scene.AddComponent(Preview, FTransform::Identity);
			if (Preview->GetOwner()) { return false; }
			Preview->SetSkeletalMesh(Mesh);
		}
		if (Cast<UProject_JRetargetAnimInstance>(Preview->GetPostProcessInstance()))
		{
			if (!Preview->GetDisablePostProcessBlueprint()) { return false; }
			++Guarded;
		}
		Preview->EnablePreview(true, Animation);
		if (!Preview->PreviewInstance) { return false; }
		Preview->PreviewInstance->PlayAnim(true);
		const auto TickPreview = [&]()
		{
			Preview->TickAnimation(1.f / 30.f, false);
			Preview->RefreshBoneTransforms();
		};
		if (const auto* Blend = Cast<UBlendSpace>(Animation))
		{
			for (const FBlendSample& Sample : Blend->GetBlendSamples())
			{
				Preview->PreviewInstance->SetBlendSpacePosition(Sample.SampleValue);
				TickPreview();
			}
		}
		else
		{
			if (const auto* Sequence = Cast<UAnimSequenceBase>(Animation))
			{
				Preview->PreviewInstance->SetPosition(Sequence->GetPlayLength() * .5f, false);
			}
			TickPreview();
		}
		++Count;
	}
	if (Preview) { Scene.RemoveComponent(Preview); }
	UE_LOG(LogTemp, Display, TEXT("SpellbladePreviewVerified %d evaluated, %d actorless post-processes guarded, %d missing mesh"), Count, Guarded, Missing);
	return Count > 0 && Guarded > 0;
}

bool AuditSpellbladeArmPreview(UBlendSpace& Blend)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	auto* Quinn = Asset<USkeletalMesh>(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Quinn_Simple"));
	auto* Legacy = Asset<UBlendSpace>(LegacyBlendPath);
	if (!Body || !Quinn || !Legacy) { return false; }
	FString Output = TEXT("mesh,asset,direction,speed,phase,bone,x,y,z,qx,qy,qz,qw,sx,sy,sz\n");
	FPreviewScene Scene(FPreviewScene::ConstructionValues().SetCreatePhysicsScene(false));
	for (USkeletalMesh* Mesh : {Quinn, Body})
	{
		auto* Preview = NewObject<UDebugSkelMeshComponent>();
		Preview->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
		Scene.AddComponent(Preview, FTransform::Identity);
		Preview->SetSkeletalMesh(Mesh);
		const auto Sample = [&](UAnimationAsset& Animation, const FVector& Point)
		{
			Preview->EnablePreview(true, &Animation);
			auto* Instance = Preview->PreviewInstance.Get();
			if (!Instance || Instance->GetCurrentAsset() != &Animation) { return false; }
			Instance->SetPlaying(false);
			if (Animation.IsA<UBlendSpace>())
			{
				Instance->SetBlendSpacePosition(Point);
				Instance->UpdateBlendspaceSamples(Point);
			}
			for (int32 Frame = 0; Frame <= 96; ++Frame)
			{
				const float Phase = Frame / 96.f;
				Instance->SetPosition(Phase * Instance->GetLength(), false);
				Preview->TickAnimation(0.f, false);
				Preview->RefreshBoneTransforms();
				const TArray<FTransform>& Transforms = Preview->GetComponentSpaceTransforms();
				for (int32 Index = 0; Index < Transforms.Num(); ++Index)
				{
					const FString Bone = Preview->GetBoneName(Index).ToString();
					if (!Bone.Contains(TEXT("arm"), ESearchCase::IgnoreCase) && !Bone.StartsWith(TEXT("hand"))
						&& !Bone.StartsWith(TEXT("clavicle")) && Bone != TEXT("spine_05")) { continue; }
					const FVector P = Transforms[Index].GetTranslation(), S = Transforms[Index].GetScale3D();
					const FQuat Q = Transforms[Index].GetRotation();
					Output += FString::Printf(TEXT("%s,%s,%.1f,%.1f,%.6f,%s,%.5f,%.5f,%.5f,%.7f,%.7f,%.7f,%.7f,%.5f,%.5f,%.5f\n"),
						*Mesh->GetName(), *Animation.GetName(), Point.X, Point.Y, Phase, *Bone,
						P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W, S.X, S.Y, S.Z);
				}
			}
			return true;
		};
		for (const FStanceTake& Take : GuardTakes)
		{
			auto* Sequence = Asset<UAnimSequence>(StanceFolder + FString(GuardPrefix) + Take.Name);
			if (!Sequence || !Sample(*Sequence, FVector::ZeroVector)) { return false; }
		}
		for (const double Direction : {-180., -135., -90., -52., 0., 45., 90., 135., 180.})
		{
			if (!Sample(Blend, FVector(Direction, 500., 0.)) || !Sample(Blend, FVector(Direction, 250., 0.))) { return false; }
			if (Mesh == Quinn && (!Sample(*Legacy, FVector(Direction, 500., 0.))
				|| !Sample(*Legacy, FVector(Direction, 250., 0.)) || !Sample(*Legacy, FVector(Direction, 700., 0.)))) { return false; }
		}
		Scene.RemoveComponent(Preview);
	}
	const FString File = FPaths::ProjectSavedDir() / TEXT("SpellbladeCombatNext/SpellbladeArmPreviewSweep.csv");
	UE_LOG(LogTemp, Display, TEXT("SpellbladeArmPreviewSweep %s"), *File);
	return FFileHelper::SaveStringToFile(Output, *File);
}

bool PrepareCastLayer(bool bApply)
{
	const FName CastSlotName(Project_J::Animation::CastUpperBodySlot);
	auto* Blueprint = Asset<UAnimBlueprint>(TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master"));
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	if (!Blueprint || !Blueprint->TargetSkeleton || !Body || !Body->GetSkeleton()) { return false; }

	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	UEdGraph* Graph = nullptr;
	UAnimGraphNode_Slot* CastSlot = nullptr;
	for (auto* Candidate : Graphs)
	{
		for (UEdGraphNode* Node : Candidate->Nodes)
		{
			if (auto* Slot = Cast<UAnimGraphNode_Slot>(Node))
			{
				if (Slot->Node.SlotName == TEXT("UpperBody")) { Graph = Candidate; }
				if (Slot->Node.SlotName == CastSlotName) { CastSlot = Slot; }
			}
		}
	}
	if (!Graph) { return false; }

	const auto Peer = [](const UEdGraphPin* Pin) -> UEdGraphPin*
	{
		return Pin && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0] : nullptr;
	};
	const auto Owner = [](const UEdGraphPin* Pin) { return Pin ? Pin->GetOwningNode() : nullptr; };
	UEdGraphPin* AimOutput = nullptr;
	for (UEdGraphNode* Node : Graph->Nodes)
	{
		if (Node->FindPin(TEXT("PreAimOffsetPose")))
		{
			if (AimOutput) { return false; }
			AimOutput = Node->FindPin(TEXT("Pose"));
		}
	}
	auto* AimCache = Cast<UAnimGraphNode_SaveCachedPose>(Owner(Peer(AimOutput)));
	if (!AimCache) { return false; }
	const auto Verify = [&]()
	{
		const auto* SlotOutput = CastSlot ? Owner(Peer(CastSlot->FindPin(TEXT("Pose")))) : nullptr;
		const auto* CachedCast = Cast<UAnimGraphNode_SaveCachedPose>(SlotOutput);
		auto* Layer = Cast<UAnimGraphNode_LayeredBoneBlend>(SlotOutput);
		if (CachedCast)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				auto* Candidate = Cast<UAnimGraphNode_LayeredBoneBlend>(Node);
				const auto* Reader = Candidate ? Cast<UAnimGraphNode_UseCachedPose>(Owner(Peer(Candidate->FindPin(TEXT("BlendPoses_0"))))) : nullptr;
				if (Reader && Reader->SaveCachedPoseNode == CachedCast && Candidate->Node.bRootSpaceRotationBlend)
				{
					Layer = Candidate;
					break;
				}
			}
		}
		if (!Layer || !Layer->Node.bMeshSpaceRotationBlend || !Layer->Node.bRootSpaceRotationBlend
			|| Layer->Node.LayerSetup.Num() != 1 || Layer->Node.LayerSetup[0].BranchFilters.Num() != 1) { return false; }
		const auto& Filter = Layer->Node.LayerSetup[0].BranchFilters[0];
		const auto* Base = Cast<UAnimGraphNode_UseCachedPose>(Owner(Peer(Layer->FindPin(TEXT("BasePose")))));
		const auto* SlotSource = Cast<UAnimGraphNode_UseCachedPose>(Owner(Peer(CastSlot->FindPin(TEXT("Source")))));
		const auto* OutputNode = Owner(Peer(Layer->FindPin(TEXT("Pose"))));
		if (const auto* Lower = Cast<UAnimGraphNode_LayeredBoneBlend>(OutputNode))
		{
			OutputNode = Owner(Peer(Lower->FindPin(TEXT("Pose"))));
		}
		const auto* Output = Cast<UAnimGraphNode_SaveCachedPose>(OutputNode);
		return Base && SlotSource && Output && Output != AimCache && Base->SaveCachedPoseNode.Get() == AimCache
			&& SlotSource->SaveCachedPoseNode.Get() == AimCache && Filter.BoneName == TEXT("spine_01") && Filter.BlendDepth == 5
			&& Blueprint->TargetSkeleton->ContainsSlotName(CastSlotName) && Body->GetSkeleton()->ContainsSlotName(CastSlotName);
	};
	if (CastSlot || !bApply)
	{
		if (!Verify()) { return false; }
		UE_LOG(LogTemp, Display, TEXT("SpellbladeCastLayerVerified %s root-space spine_01:5 after AimOffset"), *CastSlotName.ToString());
		return true;
	}

	Blueprint->Modify();
	Graph->Modify();
	TSet<USkeleton*> ChangedSkeletons;
	for (auto* Skeleton : {Blueprint->TargetSkeleton.Get(), Body->GetSkeleton()})
	{
		if (!Skeleton->ContainsSlotName(CastSlotName))
		{
			Skeleton->Modify();
			Skeleton->SetSlotGroupName(CastSlotName, Skeleton->GetSlotGroupName(TEXT("UpperBody")));
			ChangedSkeletons.Add(Skeleton);
		}
	}
	const int32 X = AimCache->NodePosX, Y = AimCache->NodePosY + 400;
	FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> CacheCreator(*Graph);
	auto* PreCast = CacheCreator.CreateNode();
	PreCast->CacheName = TEXT("PreCastUpperBody");
	PreCast->NodePosX = X - 640;
	PreCast->NodePosY = Y;
	CacheCreator.Finalize();

	const auto UseCache = [Graph, PreCast](int32 X, int32 Y)
	{
		FGraphNodeCreator<UAnimGraphNode_UseCachedPose> Creator(*Graph);
		auto* Node = Creator.CreateNode();
		Node->SaveCachedPoseNode = PreCast;
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	};
	auto* Base = UseCache(X - 435, Y - 95);
	auto* Source = UseCache(X - 435, Y + 90);
	FGraphNodeCreator<UAnimGraphNode_Slot> SlotCreator(*Graph);
	CastSlot = SlotCreator.CreateNode();
	CastSlot->Node.SlotName = CastSlotName;
	CastSlot->Node.bAlwaysUpdateSourcePose = true;
	CastSlot->NodePosX = X - 240;
	CastSlot->NodePosY = Y + 90;
	SlotCreator.Finalize();
	FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> LayerCreator(*Graph);
	auto* Layer = LayerCreator.CreateNode();
	Layer->Node.bMeshSpaceRotationBlend = true;
	Layer->Node.bRootSpaceRotationBlend = true;
	Layer->Node.LayerSetup[0].BranchFilters = {{TEXT("spine_01"), 5}};
	Layer->NodePosX = X - 10;
	Layer->NodePosY = Y;
	LayerCreator.Finalize();
	const auto Connect = [Graph](UEdGraphPin* Output, UEdGraphPin* Input)
	{
		return Output && Input && Graph->GetSchema()->TryCreateConnection(Output, Input);
	};
	// The cast owns the upper pose after movement lean and aim offsets.
	// The original final-pose cache and all downstream readers stay in place.
	auto* FinalCache = AimCache;
	AimOutput->BreakLinkTo(FinalCache->FindPin(TEXT("Pose")));
	if (!Connect(AimOutput, PreCast->FindPin(TEXT("Pose")))
		|| !Connect(Base->FindPin(TEXT("Pose")), Layer->FindPin(TEXT("BasePose")))
		|| !Connect(Source->FindPin(TEXT("Pose")), CastSlot->FindPin(TEXT("Source")))
		|| !Connect(CastSlot->FindPin(TEXT("Pose")), Layer->FindPin(TEXT("BlendPoses_0")))
		|| !Connect(Layer->FindPin(TEXT("Pose")), FinalCache->FindPin(TEXT("Pose")))) { return false; }
	AimCache = PreCast;
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FKismetEditorUtilities::CompileBlueprint(Blueprint);
	if (Blueprint->Status == BS_Error || !Verify()) { return false; }
	for (auto* Skeleton : ChangedSkeletons) { if (!Save(Skeleton)) { return false; } }
	return Save(Blueprint);
}

bool PrepareActionHandoffGraph(bool bApply)
{
	auto* Master = Asset<UAnimBlueprint>(TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master"));
	if (!Master || !PrepareCastLayer(false)) { return false; }
	TArray<UEdGraph*> Graphs;
	Master->GetAllGraphs(Graphs);
	UAnimGraphNode_Slot* Full = nullptr;
	UAnimGraphNode_Slot* CastSlot = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (auto* Slot = Cast<UAnimGraphNode_Slot>(Node))
			{
				if (Slot->Node.SlotName == TEXT("DefaultSlot")) { Full = Slot; }
				if (Slot->Node.SlotName == Project_J::Animation::CastUpperBodySlot) { CastSlot = Slot; }
			}
		}
	}
	if (!Full || !CastSlot || Full->GetGraph() != CastSlot->GetGraph()) { return false; }
	auto& Graph = *Full->GetGraph();
	const auto Peer = [](const UEdGraphPin* Pin) -> UEdGraphPin*
	{
		return Pin && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0] : nullptr;
	};
	const auto Owner = [](const UEdGraphPin* Pin) { return Pin ? Pin->GetOwningNode() : nullptr; };
	const FName RecoveryWeight = GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance, GraphRecoveryLocomotionWeight);
	const FName CastWeight = GET_MEMBER_NAME_CHECKED(UProject_JPlayerAnimInstance, GraphStationaryCastLegWeight);
	const auto HasWeight = [&Graph, &Peer, &Owner](FName Name)
	{
		for (UEdGraphNode* Node : Graph.Nodes)
		{
			const auto* Blend = Cast<UAnimGraphNode_LayeredBoneBlend>(Node);
			const auto* Read = Blend ? Cast<UK2Node_VariableGet>(Owner(Peer(Blend->FindPin(TEXT("BlendWeights_0"))))) : nullptr;
			if (Read && Read->VariableReference.GetMemberName() == Name) { return true; }
		}
		return false;
	};
	if (HasWeight(RecoveryWeight) || HasWeight(CastWeight))
	{
		return HasWeight(RecoveryWeight) && HasWeight(CastWeight);
	}
	if (!bApply) { return false; }

	auto* FullOutput = Full->FindPin(TEXT("Pose"));
	auto* FullInput = Full->FindPin(TEXT("Source"));
	auto* FullDestination = Peer(FullOutput);
	auto* Locomotion = Peer(FullInput);
	auto* CastOutput = CastSlot->FindPin(TEXT("Pose"));
	auto* CastUpperInput = Peer(CastOutput);
	auto* CastUpper = Cast<UAnimGraphNode_LayeredBoneBlend>(Owner(CastUpperInput));
	auto* CastUpperOutput = CastUpper ? CastUpper->FindPin(TEXT("Pose")) : nullptr;
	auto* CastDestination = Peer(CastUpperOutput);
	if (!FullDestination || !Locomotion || !CastUpperInput || !CastDestination) { return false; }
	Master->Modify();
	Graph.Modify();
	const auto Connect = [&Graph](UEdGraphPin* A, UEdGraphPin* B)
	{
		return A && B && Graph.GetSchema()->TryCreateConnection(A, B);
	};
	const auto MakeCache = [&Graph](const TCHAR* Name, int32 X, int32 Y)
	{
		FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> Creator(Graph);
		auto* Node = Creator.CreateNode();
		Node->CacheName = Name;
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node;
	};
	const auto ReadCache = [&Graph](UAnimGraphNode_SaveCachedPose* Cache, int32 X, int32 Y)
	{
		FGraphNodeCreator<UAnimGraphNode_UseCachedPose> Creator(Graph);
		auto* Node = Creator.CreateNode();
		Node->SaveCachedPoseNode = Cache;
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		return Node->FindPin(TEXT("Pose"));
	};
	const auto MakeLowerBlend = [&Graph, &Connect](FName WeightName, bool bKeepActionCurves, int32 X, int32 Y)
	{
		FGraphNodeCreator<UAnimGraphNode_LayeredBoneBlend> Creator(Graph);
		auto* Node = Creator.CreateNode();
		Node->Node.LayerSetup[0].BranchFilters = {{TEXT("pelvis"), 0}, {TEXT("spine_01"), -1}, {TEXT("ik_foot_root"), 0}};
		Node->Node.bBlendRootMotionBasedOnRootBone = true;
		// Recovery restores leg transforms while retaining the active action's hand/contact curves.
		Node->Node.CurveBlendOption = bKeepActionCurves ? ECurveBlendOption::UseBasePose : ECurveBlendOption::Override;
		Node->NodePosX = X;
		Node->NodePosY = Y;
		Creator.Finalize();
		FGraphNodeCreator<UK2Node_VariableGet> WeightCreator(Graph);
		auto* Weight = WeightCreator.CreateNode();
		Weight->VariableReference.SetSelfMember(WeightName);
		Weight->NodePosX = X - 245;
		Weight->NodePosY = Y + 160;
		WeightCreator.Finalize();
		return Connect(Weight->GetValuePin(), Node->FindPin(TEXT("BlendWeights_0"))) ? Node : nullptr;
	};

	const int32 FX = Full->NodePosX, FY = Full->NodePosY;
	auto* PreAction = MakeCache(TEXT("ProjectJ_PreFullBodyAction"), FX - 425, FY + 165);
	auto* Recovery = MakeLowerBlend(RecoveryWeight, true, FX + 250, FY + 125);
	const int32 CX = CastSlot->NodePosX, CY = CastSlot->NodePosY;
	auto* CastPerformance = MakeCache(TEXT("ProjectJ_CastPerformance"), CX + 170, CY - 165);
	auto* CastLegs = MakeLowerBlend(CastWeight, true, CX + 610, CY + 135);
	if (!Recovery || !CastLegs) { return false; }
	FullInput->BreakLinkTo(Locomotion);
	FullOutput->BreakLinkTo(FullDestination);
	CastOutput->BreakLinkTo(CastUpperInput);
	CastUpperOutput->BreakLinkTo(CastDestination);
	if (!Connect(Locomotion, PreAction->FindPin(TEXT("Pose")))
		|| !Connect(ReadCache(PreAction, FX - 190, FY), FullInput)
		|| !Connect(FullOutput, Recovery->FindPin(TEXT("BasePose")))
		|| !Connect(ReadCache(PreAction, FX + 15, FY + 210), Recovery->FindPin(TEXT("BlendPoses_0")))
		|| !Connect(Recovery->FindPin(TEXT("Pose")), FullDestination)
		|| !Connect(CastOutput, CastPerformance->FindPin(TEXT("Pose")))
		|| !Connect(ReadCache(CastPerformance, CX + 140, CY), CastUpperInput)
		|| !Connect(CastUpperOutput, CastLegs->FindPin(TEXT("BasePose")))
		|| !Connect(ReadCache(CastPerformance, CX + 365, CY + 240), CastLegs->FindPin(TEXT("BlendPoses_0")))
		|| !Connect(CastLegs->FindPin(TEXT("Pose")), CastDestination)) { return false; }
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Master);
	FKismetEditorUtilities::CompileBlueprint(Master);
	return Master->Status != BS_Error && HasWeight(RecoveryWeight) && HasWeight(CastWeight) && PrepareCastLayer(false) && Save(Master);
}

bool PrepareWeaponCarryLayerBlend(bool bApply)
{
	auto* Master = Asset<UAnimBlueprint>(TEXT("/Game/Animation_Logic/ABPs/ABP_Player_Master"));
	if (!Master || !Master->ParentClass->IsChildOf(UProject_JCharacterAnimInstance::StaticClass())) { return false; }
	TArray<UEdGraph*> Graphs;
	Master->GetAllGraphs(Graphs);
	UAnimGraphNode_LinkedAnimLayer* WeaponLayer = nullptr;
	for (UEdGraph* Graph : Graphs)
	{
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			if (Node->GetFName() == TEXT("ProjectJ_CombatLayer"))
			{
				if (WeaponLayer) { return false; }
				WeaponLayer = Cast<UAnimGraphNode_LinkedAnimLayer>(Node);
			}
		}
	}
	if (!WeaponLayer) { return false; }
	const auto Peer = [](UEdGraphPin* Pin) -> UEdGraphPin*
	{
		return Pin && Pin->LinkedTo.Num() == 1 ? Pin->LinkedTo[0] : nullptr;
	};
	const auto Owner = [](UEdGraphPin* Pin) { return Pin ? Pin->GetOwningNode() : nullptr; };
	auto* Input = WeaponLayer->FindPin(TEXT("BasePose"));
	auto* Output = WeaponLayer->FindPin(TEXT("Pose"));
	auto* Previous = Peer(Input);
	auto* Next = Peer(Output);
	if (!Previous || !Next) { return false; }
	const FName WeightName = GET_MEMBER_NAME_CHECKED(UProject_JCharacterAnimInstance, GraphCombatWeaponLayerWeight);
	const auto Verify = [&]()
	{
		const auto* Blend = Cast<UAnimGraphNode_TwoWayBlend>(Owner(Peer(Output)));
		const auto* LayerInput = Cast<UAnimGraphNode_UseCachedPose>(Owner(Peer(Input)));
		if (!Blend || !LayerInput || Peer(Blend->FindPin(TEXT("B"))) != Output) { return false; }
		const auto* Base = Cast<UAnimGraphNode_UseCachedPose>(Owner(Peer(Blend->FindPin(TEXT("A")))));
		const auto* Weight = Cast<UK2Node_VariableGet>(Owner(Peer(Blend->FindPin(TEXT("Alpha")))));
		return Base && Weight && Base->SaveCachedPoseNode == LayerInput->SaveCachedPoseNode
			&& Weight->VariableReference.GetMemberName() == WeightName
			&& Cast<UAnimGraphNode_SaveCachedPose>(Owner(Peer(Blend->FindPin(TEXT("Pose")))));
	};
	if (Verify())
	{
		UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponLayerBlend verified in %s"), *Master->GetName());
		return true;
	}
	if (!bApply || !Owner(Next)->IsA<UAnimGraphNode_SaveCachedPose>()) { return false; }

	auto* Graph = WeaponLayer->GetGraph();
	Master->Modify();
	Graph->Modify();
	const int32 X = WeaponLayer->NodePosX, Y = WeaponLayer->NodePosY;
	FGraphNodeCreator<UAnimGraphNode_SaveCachedPose> CacheCreator(*Graph);
	auto* Cache = CacheCreator.CreateNode();
	Cache->CacheName = TEXT("PreCombatWeaponLayer");
	Cache->NodePosX = X - 600;
	Cache->NodePosY = Y + 235;
	CacheCreator.Finalize();
	const auto ReadBase = [Graph, Cache, X](int32 Y)
	{
		FGraphNodeCreator<UAnimGraphNode_UseCachedPose> Creator(*Graph);
		auto* Reader = Creator.CreateNode();
		Reader->SaveCachedPoseNode = Cache;
		Reader->NodePosX = X - 225;
		Reader->NodePosY = Y;
		Creator.Finalize();
		return Reader->FindPin(TEXT("Pose"));
	};
	auto* LayerBase = ReadBase(Y);
	auto* BlendBase = ReadBase(Y - 150);
	FGraphNodeCreator<UAnimGraphNode_TwoWayBlend> BlendCreator(*Graph);
	auto* Blend = BlendCreator.CreateNode();
	Blend->NodePosX = X + 300;
	Blend->NodePosY = Y;
	BlendCreator.Finalize();
	FGraphNodeCreator<UK2Node_VariableGet> WeightCreator(*Graph);
	auto* Weight = WeightCreator.CreateNode();
	Weight->VariableReference.SetSelfMember(WeightName);
	Weight->NodePosX = X + 40;
	Weight->NodePosY = Y + 180;
	WeightCreator.Finalize();
	const auto Connect = [Graph](UEdGraphPin* From, UEdGraphPin* To)
	{
		return From && To && Graph->GetSchema()->TryCreateConnection(From, To);
	};
	Input->BreakLinkTo(Previous);
	Output->BreakLinkTo(Next);
	if (!Connect(Previous, Cache->FindPin(TEXT("Pose"))) || !Connect(LayerBase, Input)
		|| !Connect(BlendBase, Blend->FindPin(TEXT("A"))) || !Connect(Output, Blend->FindPin(TEXT("B")))
		|| !Connect(Weight->GetValuePin(), Blend->FindPin(TEXT("Alpha")))
		|| !Connect(Blend->FindPin(TEXT("Pose")), Next)) { return false; }
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Master);
	FKismetEditorUtilities::CompileBlueprint(Master);
	return Master->Status != BS_Error && Verify() && Save(Master);
}

bool BalanceDirectBodyNeck(UAnimSequence& Clip, USkeletalMesh& Body, bool bApply)
{
	const FName NeckBase(TEXT("neck_01"));
	const FName NeckTip(TEXT("neck_02"));
	const auto& Skeleton = Body.GetRefSkeleton();
	const int32 BaseIndex = Skeleton.FindBoneIndex(NeckBase);
	const int32 TipIndex = Skeleton.FindBoneIndex(NeckTip);
	if (BaseIndex == INDEX_NONE || TipIndex == INDEX_NONE || Skeleton.GetParentIndex(TipIndex) != BaseIndex
		|| Clip.GetSkeleton() != Body.GetSkeleton() || Clip.HasRootMotion()) { return false; }

	const auto& Reference = Skeleton.GetRefBonePose();
	const int32 KeyCount = Clip.GetDataModel()->GetNumberOfKeys();
	if (KeyCount < 2) { return false; }
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Body;
	Options.bShouldRetarget = false;
	TArray<FTransform> BaseKeys, TipKeys;
	TArray<FQuat> HeadRotations;
	double BasePeak = 0.;
	double TipPeak = 0.;
	for (int32 Key = 0; Key < KeyCount; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		auto Base = UAnimPoseExtensions::GetBonePose(Pose, NeckBase, EAnimPoseSpaces::Local);
		auto Tip = UAnimPoseExtensions::GetBonePose(Pose, NeckTip, EAnimPoseSpaces::Local);
		const FQuat OldBase = Base.GetRotation();
		const FQuat OldTip = Tip.GetRotation();
		BasePeak = FMath::Max(BasePeak, OldBase.AngularDistance(Reference[BaseIndex].GetRotation()));
		TipPeak = FMath::Max(TipPeak, OldTip.AngularDistance(Reference[TipIndex].GetRotation()));
		HeadRotations.Add(UAnimPoseExtensions::GetBonePose(Pose, TEXT("head"), EAnimPoseSpaces::World).GetRotation());

		// This retargeted pose animates only the base of the neck. Share its
		// rotation on the UE5 chain while keeping the authored end orientation.
		const FQuat NewBase = FQuat::Slerp(Reference[BaseIndex].GetRotation(), OldBase, .6);
		Base.SetRotation(NewBase);
		Tip.SetRotation((NewBase.Inverse() * OldBase * OldTip).GetNormalized());
		BaseKeys.Add(Base);
		TipKeys.Add(Tip);
	}
	if (TipPeak > FMath::DegreesToRadians(.1))
	{
		UE_LOG(LogTemp, Display, TEXT("SpellbladeNeck %s base=%.2f tip=%.2f"), *Clip.GetName(),
			FMath::RadiansToDegrees(BasePeak), FMath::RadiansToDegrees(TipPeak));
		return true;
	}
	if (!bApply || !SetTrack(Clip, NeckBase, BaseKeys) || !SetTrack(Clip, NeckTip, TipKeys)) { return false; }
	for (int32 Key = 0; Key < KeyCount; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid() || UAnimPoseExtensions::GetBonePose(Pose, TEXT("head"), EAnimPoseSpaces::World)
			.GetRotation().AngularDistance(HeadRotations[Key]) > FMath::DegreesToRadians(.1)) { return false; }
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeNeck %s base=%.2f -> %.2f tip=%.2f; head orientation preserved at %d keys"),
		*Clip.GetName(), FMath::RadiansToDegrees(BasePeak), FMath::RadiansToDegrees(BasePeak * .6),
		FMath::RadiansToDegrees(BasePeak * .4), KeyCount);
	return true;
}

bool ShapeCastGaze(UAnimSequence& Clip, USkeletalMesh& Body)
{
	const FName NeckBase(TEXT("neck_01")), NeckTip(TEXT("neck_02")), Head(TEXT("head"));
	const auto& Skeleton = Body.GetRefSkeleton();
	const int32 Base = Skeleton.FindBoneIndex(NeckBase), Tip = Skeleton.FindBoneIndex(NeckTip);
	const int32 HeadIndex = Skeleton.FindBoneIndex(Head);
	if (Base == INDEX_NONE || Tip == INDEX_NONE || HeadIndex == INDEX_NONE
		|| Skeleton.GetParentIndex(Tip) != Base || Skeleton.GetParentIndex(HeadIndex) != Tip) { return false; }

	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Body;
	Options.bShouldRetarget = false;
	const int32 Count = Clip.GetDataModel()->GetNumberOfKeys();
	const double Rate = Clip.GetDataModel()->GetFrameRate().AsDecimal();
	if (Count < 3 || Rate <= 0.) { return false; }
	TArray<FTransform> BaseKeys, TipKeys, HeadKeys;
	TArray<FQuat> ChestGaze;
	for (int32 Key = 0; Key < Count; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		BaseKeys.Add(UAnimPoseExtensions::GetBonePose(Pose, NeckBase, EAnimPoseSpaces::Local));
		TipKeys.Add(UAnimPoseExtensions::GetBonePose(Pose, NeckTip, EAnimPoseSpaces::Local));
		HeadKeys.Add(UAnimPoseExtensions::GetBonePose(Pose, Head, EAnimPoseSpaces::Local));
		ChestGaze.Add(BaseKeys.Last().GetRotation() * TipKeys.Last().GetRotation() * HeadKeys.Last().GetRotation());
	}

	// Author gaze in chest space so corrections survive upper-body composition.
	// Symmetric smoothing keeps the gesture timed with the hands.
	const int32 Radius = FMath::Max(1, FMath::RoundToInt(.12 * Rate));
	const auto Smooth = [Count, Radius](int32 Key, auto Read)
	{
		const FQuat Center = Read(Key);
		FQuat Sum(0., 0., 0., 0.);
		for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
		{
			const FQuat Q = Read(FMath::Clamp(Key + Offset, 0, Count - 1));
			const double Weight = Radius + 1 - FMath::Abs(Offset);
			Sum += Q * ((Center | Q) >= 0. ? Weight : -Weight);
		}
		return Sum.GetNormalized();
	};
	TArray<FTransform> SmoothBase = BaseKeys, SmoothTip = TipKeys;
	FQuat Prior = ChestGaze[0];
	double Before = 0., After = 0.;
	for (int32 Key = 0; Key < Count; ++Key)
	{
		const FQuat Gaze = Smooth(Key, [&](int32 I) { return ChestGaze[I]; });
		const FQuat B = Smooth(Key, [&](int32 I) { return BaseKeys[I].GetRotation(); });
		const FQuat T = Smooth(Key, [&](int32 I) { return TipKeys[I].GetRotation(); });
		SmoothBase[Key].SetRotation(B);
		SmoothTip[Key].SetRotation(T);
		HeadKeys[Key].SetRotation(((B * T).Inverse() * Gaze).GetNormalized());
		if (Key)
		{
			Before = FMath::Max(Before, ChestGaze[Key - 1].AngularDistance(ChestGaze[Key]));
			After = FMath::Max(After, Prior.AngularDistance(Gaze));
		}
		Prior = Gaze;
	}
	UE_LOG(LogTemp, Display, TEXT("LongbladeCastGaze %s chest-relative peak %.1f -> %.1f deg/s at native pace"),
		*Clip.GetName(), FMath::RadiansToDegrees(Before) * Rate, FMath::RadiansToDegrees(After) * Rate);
	return After < Before && SetTrack(Clip, NeckBase, SmoothBase) && SetTrack(Clip, NeckTip, SmoothTip)
		&& SetTrack(Clip, Head, HeadKeys);
}

bool PrepareDirectBodyUpperMontages(const FString& Params, UProject_JCombatStyleDefinition& Style, bool bApply)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Profile = Style.WeaponAnimationProfile.Get();
	if (!Body || !Profile) { return false; }
	TSet<UAnimSequence*> ClipsToSave;
	TArray<UAnimMontage*> MontagesToSave;
	for (auto* Montage : {Profile->CombatIntroMontage.Get(), Profile->CombatOutroMontage.Get()})
	{
		if (!Montage || Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].SlotName != TEXT("UpperBody"))
		{
			return false;
		}
		const auto& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
		auto* Clip = Segments.Num() == 1 ? Cast<UAnimSequence>(Segments[0].GetAnimReference()) : nullptr;
		if (!Clip || !BalanceDirectBodyNeck(*Clip, *Body, bApply)) { return false; }
		ClipsToSave.Add(Clip);
	}

	TArray<FStanceTake> MissingTakes;
	for (const auto& Take : CastTakes)
	{
		if (!Asset<UAnimSequence>(DirectSkillPath + Take.Name)) { MissingTakes.Add(Take); }
	}
	if (!MissingTakes.IsEmpty() && (!bApply || RetargetStanceTakes(Params, MissingTakes, TEXT("AS_SpellbladeDirect_"),
		true, Sequences + TEXT("DirectBody/Skill")).Num() != MissingTakes.Num())) { return false; }

	for (const auto& Cast : BuffCasts())
	{
		const auto* Node = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.") + FString(Cast.Name)));
		auto* Montage = Node && Node->AttackDefinition ? Node->AttackDefinition->Montage.Get() : nullptr;
		const FString PerformancePath = CastPerformancePath + Cast.Clip;
		auto* Clip = FPackageName::DoesPackageExist(PerformancePath) ? Asset<UAnimSequence>(PerformancePath) : nullptr;
		if (!Clip && bApply)
		{
			Clip = Duplicate(Asset<UAnimSequence>(DirectSkillPath + Cast.Clip), PerformancePath);
			if (!Clip || !BalanceDirectBodyNeck(*Clip, *Body, true) || !ShapeCastGaze(*Clip, *Body)) { return false; }
		}
		if (!Montage || !Clip || Montage->SlotAnimTracks.Num() != 1) { return false; }
		const FName Slot = Montage->SlotAnimTracks[0].SlotName;
		if (Slot != Project_J::Animation::CastUpperBodySlot && (!bApply || Slot != TEXT("UpperBody"))) { return false; }
		auto& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
		const bool bBound = Slot == Project_J::Animation::CastUpperBodySlot
			&& Montage->GetSkeleton() == Body->GetSkeleton() && Segments.Num() == 1
			&& Segments[0].GetAnimReference() == Clip && FMath::IsNearlyEqual(Segments[0].AnimPlayRate, 1.f)
			&& FMath::IsNearlyEqual(Montage->BlendIn.GetBlendTime(), CastBlendIn)
			&& FMath::IsNearlyEqual(Montage->BlendOut.GetBlendTime(), CastBlendOut)
			&& FMath::IsNearlyEqual(Montage->GetPlayLength(), Clip->GetPlayLength(), .001);
		if ((!bBound && !bApply) || !BalanceDirectBodyNeck(*Clip, *Body, false)) { return false; }
		if (!bBound)
		{
			if (!Montage->Notifies.IsEmpty() || Montage->CompositeSections.Num() > 1) { return false; }
			SetBuffPerformance(*Montage, *Clip);
			MontagesToSave.Add(Montage);
		}
		ClipsToSave.Add(Clip);
	}
	if (bApply)
	{
		for (auto* Clip : ClipsToSave) { if (Clip->GetPackage()->IsDirty() && !Save(Clip)) { return false; } }
		for (auto* Montage : MontagesToSave) { if (!Save(Montage)) { return false; } }
	}
	return true;
}

// The shared driver joints still own the arm and weapon. The legacy children
// carry the original costume and face deformation in the target chest space.
bool TransferLongbladeDeformation(UAnimSequence& Clip, UAnimSequence& Source, USkeletalMesh& Body,
	USkeletalMesh& Legacy, double HeadFitCm, bool bIncludeUpperTwist)
{
	const auto& Skeleton = Body.GetRefSkeleton();
	const auto& SourceSkeleton = Legacy.GetRefSkeleton();
	const FName Chest(TEXT("Bip01_Spine2")), SourceChest(TEXT("Bip01-Spine2"));
	const FName Bones[] = {TEXT("Bip01_Neck"), TEXT("Bip01_Head"), TEXT("Bip01_L_Clavicle"), TEXT("Bip01_L_ClavicleTwist"),
		TEXT("Bip01_LUpArmTwist"), TEXT("Bip01_LUpArmTwist1")};
	constexpr int32 MaxBones = UE_ARRAY_COUNT(Bones);
	const int32 BoneCount = bIncludeUpperTwist ? MaxBones : MaxBones - 2;
	const int32 ChestIndex = Skeleton.FindBoneIndex(Chest);
	if (ChestIndex == INDEX_NONE || SourceSkeleton.FindBoneIndex(SourceChest) == INDEX_NONE
		|| !FMath::IsNearlyEqual(Clip.GetPlayLength(), Source.GetPlayLength(), 1.f / 30.f + UE_KINDA_SMALL_NUMBER))
	{
		return false;
	}
	const FTransform ChestBind = FAnimationRuntime::GetComponentSpaceTransformRefPose(Skeleton, ChestIndex);
	const FVector HeadFit = ChestBind.InverseTransformVectorNoScale(FVector::UpVector * HeadFitCm);
	FName SourceNames[MaxBones];
	FName Parents[MaxBones];
	TArray<FTransform> SourceKeys[MaxBones], Keys[MaxBones];
	const int32 KeyCount = Clip.GetDataModel()->GetNumberOfKeys();
	if (KeyCount < 2) { return false; }
	for (int32 Index = 0; Index < BoneCount; ++Index)
	{
		const int32 Bone = Skeleton.FindBoneIndex(Bones[Index]);
		SourceNames[Index] = FName(*Bones[Index].ToString().Replace(TEXT("_"), TEXT("-")));
		if (Bone <= 0 || SourceSkeleton.FindBoneIndex(SourceNames[Index]) == INDEX_NONE) { return false; }
		Parents[Index] = Skeleton.GetBoneName(Skeleton.GetParentIndex(Bone));
		SourceKeys[Index].Reserve(KeyCount);
		Keys[Index].Reserve(KeyCount);
	}
	FAnimPoseEvaluationOptions TargetOptions;
	TargetOptions.OptionalSkeletalMesh = &Body;
	TargetOptions.bShouldRetarget = false;
	FAnimPoseEvaluationOptions SourceOptions = TargetOptions;
	SourceOptions.OptionalSkeletalMesh = &Legacy;
	const auto Read = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
	};
	const double SourceLength = Source.GetPlayLength();
	for (int32 Key = 0; Key < KeyCount; ++Key)
	{
		FAnimPose Original;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Source, SourceLength * (double(Key) / (KeyCount - 1)), SourceOptions, Original);
		if (!Original.IsValid()) { return false; }
		const FTransform OriginalChest = Read(Original, SourceChest);
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			SourceKeys[Bone].Add(Read(Original, SourceNames[Bone]).GetRelativeTransform(OriginalChest));
		}
	}

	// Filter the original gesture symmetrically in chest space. Filtering the
	// legacy local tracks would include compensating motion from the new parents.
	const int32 Radius = FMath::Max(1, FMath::RoundToInt(.12 * Clip.GetDataModel()->GetFrameRate().AsDecimal()));
	const auto Smooth = [&](int32 Bone, int32 Key)
	{
		const FQuat Center = SourceKeys[Bone][Key].GetRotation();
		FQuat Rotation(0., 0., 0., 0.);
		FVector Position = FVector::ZeroVector;
		double WeightSum = 0.;
		for (int32 Offset = -Radius; Offset <= Radius; ++Offset)
		{
			const FTransform& Sample = SourceKeys[Bone][FMath::Clamp(Key + Offset, 0, KeyCount - 1)];
			const double Weight = Radius + 1 - FMath::Abs(Offset);
			const FQuat Q = Sample.GetRotation();
			Rotation += Q * ((Center | Q) >= 0. ? Weight : -Weight);
			Position += Sample.GetLocation() * Weight;
			WeightSum += Weight;
		}
		return FTransform(Rotation.GetNormalized(), Position / WeightSum);
	};
	for (int32 Key = 0; Key < KeyCount; ++Key)
	{
		FAnimPose Target;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, TargetOptions, Target);
		if (!Target.IsValid()) { return false; }
		const FTransform NewChest = Read(Target, Chest);
		FTransform Deformed[MaxBones];
		for (int32 Index = 0; Index < BoneCount; ++Index)
		{
			FTransform& Desired = Deformed[Index];
			Desired = Smooth(Index, Key);
			if (Bones[Index] == TEXT("Bip01_Head")) { Desired.AddToTranslation(HeadFit); }
			Desired *= NewChest;
			Desired.SetScale3D(Read(Target, Bones[Index]).GetScale3D());
			FTransform Parent = Read(Target, Parents[Index]);
			for (int32 Prev = 0; Prev < Index; ++Prev)
			{
				if (Bones[Prev] == Parents[Index]) { Parent = Deformed[Prev]; break; }
			}
			Keys[Index].Add(Desired.GetRelativeTransform(Parent));
		}
	}
	for (int32 Index = 0; Index < BoneCount; ++Index)
	{
		if (!SetTrack(Clip, Bones[Index], Keys[Index])) { return false; }
	}
	return true;
}

bool RetimeLongbladeCastGaze(UAnimSequence& Clip, USkeletalMesh& Body)
{
	const FName Chest(TEXT("Bip01_Spine2"));
	const FName Bones[] = {TEXT("Bip01_Neck"), TEXT("Bip01_Head")};
	constexpr int32 BoneCount = UE_ARRAY_COUNT(Bones);
	constexpr double Window = .86;
	constexpr double Strength = .90;
	const int32 Samples = FMath::CeilToInt(Window * 240.);
	const double Length = Clip.GetPlayLength();
	const int32 KeyCount = Clip.GetDataModel()->GetNumberOfKeys();
	const auto& Ref = Body.GetRefSkeleton();
	if (Length <= Window || KeyCount < 2 || Ref.FindBoneIndex(Chest) == INDEX_NONE) { return false; }

	FName Parents[BoneCount];
	TArray<FTransform> ChestSamples[BoneCount], Keys[BoneCount];
	for (int32 Index = 0; Index < BoneCount; ++Index)
	{
		const int32 Bone = Ref.FindBoneIndex(Bones[Index]);
		if (Bone <= 0) { return false; }
		Parents[Index] = Ref.GetBoneName(Ref.GetParentIndex(Bone));
		ChestSamples[Index].Reserve(Samples + 1);
		Keys[Index].Reserve(KeyCount);
	}
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Body;
	Options.bShouldRetarget = false;
	const auto Read = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
	};
	TArray<double> Arc;
	Arc.Reserve(Samples + 1);
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Window * (double(Index) / Samples), Options, Pose);
		if (!Pose.IsValid()) { return false; }
		const FTransform ChestPose = Read(Pose, Chest);
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
			ChestSamples[Bone].Add(Read(Pose, Bones[Bone]).GetRelativeTransform(ChestPose));
		const auto& Head = ChestSamples[1];
		const double Distance = Index == 0 ? 0. : Arc.Last()
			+ Head[Index - 1].GetRotation().AngularDistance(Head[Index].GetRotation());
		Arc.Add(Distance);
	}
	if (Arc.Last() <= UE_SMALL_NUMBER) { return false; }

	// Redistribute the measured angular path over the gesture interval. The
	// blend fades to zero at both ends, leaving the adjacent poses continuous.
	double PreviousClock = 0.;
	for (int32 Key = 0; Key < KeyCount; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		const double Time = Length * (double(Key) / (KeyCount - 1));
		if (Time <= 0. || Time >= Window)
		{
			for (int32 Bone = 0; Bone < BoneCount; ++Bone)
				Keys[Bone].Add(UAnimPoseExtensions::GetBonePose(Pose, Bones[Bone], EAnimPoseSpaces::Local));
			continue;
		}
		const double Phase = Time / Window;
		const double Sample = Phase * Samples;
		const int32 Original = FMath::FloorToInt(Sample);
		const double OriginalDistance = FMath::Lerp(Arc[Original], Arc[Original + 1], Sample - Original);
		const double Distance = FMath::Lerp(OriginalDistance, Phase * Arc.Last(), Strength * FMath::Sin(UE_DOUBLE_PI * Phase));
		const int32 Lower = FMath::Clamp(Algo::UpperBound(Arc, Distance) - 1, 0, Samples - 1);
		const double Span = Arc[Lower + 1] - Arc[Lower];
		const double Alpha = Span <= UE_SMALL_NUMBER ? 0. : FMath::Clamp((Distance - Arc[Lower]) / Span, 0., 1.);
		const double Clock = Lower + Alpha;
		if (Clock < PreviousClock) { return false; }
		PreviousClock = Clock;
		const FTransform ChestPose = Read(Pose, Chest);
		for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		{
			FTransform Desired;
			Desired.Blend(ChestSamples[Bone][Lower], ChestSamples[Bone][Lower + 1], Alpha);
			Keys[Bone].Add((Desired * ChestPose).GetRelativeTransform(Read(Pose, Parents[Bone])));
		}
	}
	for (int32 Bone = 0; Bone < BoneCount; ++Bone)
		if (!SetTrack(Clip, Bones[Bone], Keys[Bone])) { return false; }
	return true;
}

bool FitLongbladeBasicShoulder(UAnimSequence& Clip, USkeletalMesh& Body)
{
	const FName Chest(TEXT("Bip01_Spine2")), Collar(TEXT("Bip01_L_Clavicle"));
	const auto& Ref = Body.GetRefSkeleton();
	const int32 ChestIndex = Ref.FindBoneIndex(Chest), CollarIndex = Ref.FindBoneIndex(Collar);
	const int32 TwistIndex = Ref.FindBoneIndex(TEXT("Bip01_L_ClavicleTwist"));
	const int32 KeyCount = Clip.GetDataModel()->GetNumberOfKeys();
	if (ChestIndex == INDEX_NONE || CollarIndex <= 0 || TwistIndex <= 0 || KeyCount < 2
		|| Ref.GetParentIndex(TwistIndex) != CollarIndex) { return false; }
	const FName Parent = Ref.GetBoneName(Ref.GetParentIndex(CollarIndex));
	const FTransform ChestBind = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, ChestIndex);
	const FVector FitRotation(0., 20., -10.);
	const FVector Axis = ChestBind.InverseTransformVectorNoScale(FitRotation.GetSafeNormal());
	const double Angle = FMath::DegreesToRadians(FitRotation.Size());
	const double Length = Clip.GetPlayLength();
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Body;
	Options.bShouldRetarget = false;
	TArray<FTransform> Keys;
	Keys.Reserve(KeyCount);
	for (int32 Key = 0; Key < KeyCount; ++Key)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		const auto Read = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World); };
		const double Time = Length * (double(Key) / (KeyCount - 1));
		// The source costume crosses the jaw from 0.63 to 1.95 seconds.
		// Open the pauldron about its existing pivot over that swing.
		const double Fit = FMath::SmoothStep(.32, .58, Time) * (1. - FMath::SmoothStep(2.02, 2.38, Time));
		FTransform Shoulder = Read(Collar);
		const FQuat Open(Read(Chest).TransformVectorNoScale(Axis), Fit * Angle);
		Shoulder.SetRotation((Open * Shoulder.GetRotation()).GetNormalized());
		Keys.Add(Shoulder.GetRelativeTransform(Read(Parent)));
	}
	return SetTrack(Clip, Collar, Keys);
}
