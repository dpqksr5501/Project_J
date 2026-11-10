#include "Authoring/Project_JSpellbladePolishCommandlet.h"

#include "Algo/BinarySearch.h"
#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimData/IAnimationDataModel.h"
#include "Animation/AnimData/CurveIdentifier.h"
#include "Animation/AnimMontage.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/AnimSequence.h"
#include "Animation/BlendSpace.h"
#include "Animation/DebugSkelMeshComponent.h"
#include "Animation/Project_JAnimNotifyState_CombatPresentationCue.h"
#include "Animation/Project_JAnimNotifyState_ComboWindow.h"
#include "Animation/Project_JAnimNotifyState_MeleeHit.h"
#include "Animation/Project_JCharacterAnimInstance.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Animation/Project_JHandContact.h"
#include "Animation/Project_JHandGripProfile.h"
#include "Animation/Project_JRetargetAnimInstance.h"
#include "Animation/Project_JWeaponAnimProfile.h"
#include "AnimationBlueprintLibrary.h"
#include "AnimationRuntime.h"
#include "AnimGraphNode_LayeredBoneBlend.h"
#include "AnimGraphNode_LinkedAnimLayer.h"
#include "AnimGraphNode_SaveCachedPose.h"
#include "AnimGraphNode_Slot.h"
#include "AnimGraphNode_TwoWayBlend.h"
#include "AnimGraphNode_UseCachedPose.h"
#include "AnimPose.h"
#include "AnimPreviewInstance.h"
#include "AssetImportTask.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Authoring/Project_JAssetMigrationLibrary.h"
#include "Combat/Project_JAttackDefinition.h"
#include "Combat/Project_JCombatCommandSet.h"
#include "Combat/Project_JCombatPresentationSet.h"
#include "Combat/Project_JCombatStyleDefinition.h"
#include "Combat/Project_JComboDefinition.h"
#include "Combat/Project_JGameplayEffect_SpellbladeFocus.h"
#include "Components/Project_JSkillInputRouterComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Equipment/Project_JEquipmentItemDefinition.h"
#include "Equipment/Project_JWeaponPresentationProfile.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraph/EdGraphSchema.h"
#include "Factories/AnimMontageFactory.h"
#include "Factories/FbxFactory.h"
#include "Factories/FbxImportUI.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "K2Node_VariableGet.h"
#include "Materials/MaterialInterface.h"
#include "Math/RotationMatrix.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "Movement/Project_JMontageVerticalMotion.h"
#include "Movement/Project_JMontageLocomotionHandoff.h"
#include "NiagaraSystem.h"
#include "PreviewScene.h"
#include "RetargetEditor/IKRetargetBatchOperation.h"
#include "RetargetEditor/IKRetargeterController.h"
#include "Retargeter/IKRetargeter.h"
#include "Retargeter/RetargetOps/FKChainsOp.h"
#include "Rig/IKRigDefinition.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Serialization/JsonWriter.h"
#include "SkeletalMeshAttributes.h"
#include "SkinnedAssetCompiler.h"
#include "UObject/SavePackage.h"
#include "UObject/MetaData.h"
#include "UObject/UnrealType.h"

namespace
{
const FString Kit = TEXT("/Game/Anim_Assets/Spellblade/Animations/Sword/");
const FString Sequences = Kit + TEXT("CombatKit/Sequences/");
const FString DirectSkillPath = Sequences + TEXT("DirectBody/Skill/AS_SpellbladeDirect_");
const FString CastPerformancePath = Sequences + TEXT("DirectBody/Skill/AS_LongbladeCast_");
constexpr float CastBlendIn = .38f;
constexpr float CastBlendOut = .40f;
const FString Montages = Kit + TEXT("Montage/CombatKit/");
const FString SpellbladeDataRoot = TEXT("/Game/DataAssetSets/");
const FString StylePath = SpellbladeDataRoot + TEXT("CombatStyle/Spellblade/DA_CombatStyle_SpellbladeKit");
const FString MappingPath = SpellbladeDataRoot + TEXT("Skill_Input_Mapping_Data/Spellblade/DA_Spellblade_Mapping_Data");
const FString BlendPath = TEXT("/Game/Animation_Logic/BSs/Spellblade/CombatKit/BS_Spellblade");
const FString LegacyBlendPath = TEXT("/Game/Animation_Logic/BSs/Spellblade/BS_Greatsword_Legacy");
const FString InputPath = TEXT("/Game/Input/IMC_Default");
const FString DashPath = Sequences + TEXT("DirectBody/CombatRevision/AS_SpellbladeDirect_MovementDash");

FGameplayTag Tag(const FString& Name)
{
	return FGameplayTag::RequestGameplayTag(FName(Name));
}

template<class T>
T* Asset(const FString& Package)
{
	return LoadObject<T>(nullptr, *(Package + TEXT(".") + FPackageName::GetLongPackageAssetName(Package)));
}

template<class T>
T* Duplicate(T* Source, const FString& Destination)
{
	if (!Source || FPackageName::DoesPackageExist(Destination)) { return nullptr; }
	UPackage* Package = CreatePackage(*Destination);
	T* Copy = DuplicateObject<T>(Source, Package, *FPackageName::GetLongPackageAssetName(Destination));
	if (Copy)
	{
		Copy->SetFlags(RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Copy);
	}
	return Copy;
}

bool Save(UObject* AssetToSave)
{
	UPackage* Package = AssetToSave->GetOutermost();
	const FString File = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());
	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	Args.SaveFlags = SAVE_NoError;
	const bool bSaved = UPackage::SavePackage(Package, AssetToSave, *File, Args);
	UE_LOG(LogTemp, Display, TEXT("Spellblade polish save %s %s"), *Package->GetName(), bSaved ? TEXT("OK") : TEXT("FAILED"));
	return bSaved;
}

bool SetTrack(UAnimSequence& Clip, FName Bone, const TArray<FTransform>& Transforms)
{
	TArray<FVector> Locations, Scales;
	TArray<FQuat> Rotations;
	for (const FTransform& Key : Transforms)
	{
		Locations.Add(Key.GetTranslation());
		Rotations.Add(Key.GetRotation());
		Scales.Add(Key.GetScale3D());
	}
	IAnimationDataController& Controller = Clip.GetController();
	Controller.OpenBracket(FText::FromName(Bone), false);
	const bool bSet = Controller.SetBoneTrackKeys(Bone, Locations, Rotations, Scales, false);
	Controller.CloseBracket(false);
	return bSet;
}

bool SetDashDistance(UAnimSequence& Dash)
{
	TArray<FTransform> Keys;
	Dash.GetDataModel()->GetBoneTrackTransforms(TEXT("root"), Keys);
	if (Keys.Num() < 2) { return false; }
	const FVector Start = Keys[0].GetTranslation();
	const double OriginalDistance = FVector::Dist2D(Start, Keys.Last().GetTranslation());
	if (!FMath::IsNearlyEqual(OriginalDistance, 350., .5)) { return false; }
	for (FTransform& Key : Keys)
	{
		const FVector Position = Key.GetTranslation();
		const FVector Scaled = Start + (Position - Start) * (1000. / OriginalDistance);
		Key.SetTranslation(FVector(Scaled.X, Scaled.Y, Position.Z));
	}
	return SetTrack(Dash, TEXT("root"), Keys);
}

void UpdateMontage(UAnimMontage& Montage)
{
	// The raw length is serialized for montages, which use slot tracks instead of a sequence data model.
	FindFProperty<FFloatProperty>(UAnimSequenceBase::StaticClass(), TEXT("SequenceLength"))
		->SetPropertyValue_InContainer(&Montage, Montage.CalculateSequenceLength());
	Montage.UpdateLinkableElements();
	Montage.RefreshCacheData();
}

void Append(UAnimMontage& Montage, UAnimSequence& Clip, float End, float Rate = 1.f, float Start = 0.f)
{
	FAnimTrack& Track = Montage.SlotAnimTracks[0].AnimTrack;
	FAnimSegment& Segment = Track.AnimSegments.AddDefaulted_GetRef();
	Segment.SetAnimReference(&Clip, true);
	Segment.StartPos = Track.AnimSegments.Num() == 1 ? 0.f : Track.AnimSegments[Track.AnimSegments.Num() - 2].GetEndPos();
	Segment.AnimStartTime = Start;
	Segment.AnimEndTime = FMath::Min(End, Clip.GetPlayLength());
	Segment.AnimPlayRate = Rate;
}

struct FStrike
{
	const TCHAR* Role;
	const TCHAR* Node;
	float End;
	float BlendOut;
	FVector2f Hit;
	FVector2f ExtraHit;
	FVector2f Continue;
};

const FStrike Strikes[] = {
	{TEXT("Kick"), TEXT("Kick"), .96f, .20f, {.055f, .155f}, {}, {}},
	{TEXT("Light01"), TEXT("Flurry.1"), 1.35f, .26f, {.19f, .35f}, {.82f, .99f}, {.99f, 1.13f}},
	{TEXT("Light02"), TEXT("Flurry.2"), 1.20f, .25f, {.23f, .43f}, {.66f, .83f}, {.84f, .96f}},
	{TEXT("Light03"), TEXT("Flurry.3"), 1.35f, .25f, {.30f, .54f}, {}, {.91f, 1.07f}},
	{TEXT("Light04"), TEXT("Flurry.4"), .96f, .25f, {.53f, .75f}, {}, {}}
};

bool AddHit(UAnimMontage& Montage, FVector2f Window, FName Socket, bool bTrail)
{
	auto* Hit = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(&Montage, TEXT("Combat"), Window.X,
		Window.Y - Window.X, UProject_JAnimNotifyState_MeleeHit::StaticClass());
	if (!Hit) { return false; }
	FindFProperty<FNameProperty>(Hit->GetClass(), TEXT("SocketName"))->SetPropertyValue_InContainer(Hit, Socket);
	*FindFProperty<FStructProperty>(Hit->GetClass(), TEXT("HitEventTag"))->ContainerPtrToValuePtr<FGameplayTag>(Hit)
		= Tag(TEXT("ProjectJ.Tests.NPC.Hit"));
	if (bTrail)
	{
		auto* Cue = UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(&Montage, TEXT("Trail"), Window.X,
			Window.Y - Window.X, UProject_JAnimNotifyState_CombatPresentationCue::StaticClass());
		if (!Cue) { return false; }
		*FindFProperty<FStructProperty>(Cue->GetClass(), TEXT("CueTag"))->ContainerPtrToValuePtr<FGameplayTag>(Cue)
			= Tag(TEXT("PresentationCue.Combat.Trail"));
	}
	return true;
}

bool AuthorStrikes(UProject_JCombatStyleDefinition& Style, TArray<UObject*>& Assets)
{
	auto& Combo = *Style.ComboDefinition;
	auto& Presentation = *Style.CombatPresentationSet;
	const auto* Base = Combo.FindNode(Tag(TEXT("Combo.Spellblade.Light.1")));
	if (!Base || !Base->AttackDefinition || !Base->AttackDefinition->HitSpec.bUseCanonicalBladeTipOffset) { return false; }
	UProject_JAttackDefinition* BaseAttack = Base->AttackDefinition;
	const auto* Trail = Presentation.AttackPresentations.FindByPredicate(
		[BaseAttack](const auto& Entry) { return Entry.AttackTag == BaseAttack->AttackTag; });
	if (!Trail || !Trail->Profile) { return false; }
	const FProject_JCombatAttackPresentationEntry TrailEntry = *Trail;
	UAnimSequence* Return = Asset<UAnimSequence>(Sequences + TEXT("Recovery/AS_SpellbladeKit_Light04Recovery"));
	if (!Return) { return false; }

	const FGameplayTag GroundOnly = Tag(TEXT("State.Movement.InAir"));
	const FGameplayTag Unmounted = Tag(TEXT("State.Mounted"));
	const FGameplayTag Primary = Tag(TEXT("Command.Spellblade.Primary"));
	const FGameplayTag Flurry = Tag(TEXT("Command.Spellblade.Flurry"));
	const FGameplayTag Kick = Tag(TEXT("Command.Spellblade.Kick"));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Strikes); ++Index)
	{
		const FStrike& Strike = Strikes[Index];
		const FString Role = Strike.Role;
		const FString NodeSuffix = Strike.Node;
		const bool bKick = Index == 0;
		const bool bFinal = Index == UE_ARRAY_COUNT(Strikes) - 1;
		UAnimSequence* Clip = Asset<UAnimSequence>(Index >= 1 && Index <= 3
			? Sequences + TEXT("DirectBody/Melee/AS_SpellbladeDirect_") + Role
			: Sequences + TEXT("Melee/AS_SpellbladeKit_") + Role);
		UAnimMontage* Preview = Asset<UAnimMontage>(Montages + TEXT("Melee/AM_SpellbladeKit_") + Role);
		UAnimMontage* Montage = Duplicate(Preview, Montages + TEXT("Authored/AM_Longblade_") + NodeSuffix.Replace(TEXT("."), TEXT("_")));
		UProject_JAttackDefinition* Attack = Duplicate(BaseAttack, SpellbladeDataRoot + TEXT("AttackDefinition/Spellblade/CombatKit/DA_Attack_Longblade_")
			+ NodeSuffix.Replace(TEXT("."), TEXT("_")));
		if (!Clip || !Montage || !Attack || Montage->SlotAnimTracks.Num() != 1 || !Clip->GetSkeleton()
			|| (bFinal && Return->GetSkeleton() != Clip->GetSkeleton())) { return false; }

		Montage->SetSkeleton(Clip->GetSkeleton());
		Montage->SlotAnimTracks[0].SlotName = TEXT("DefaultSlot");
		Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Reset();
		Montage->Notifies.Reset();
		Montage->RateScale = 1.f;
		Montage->BlendIn.SetBlendTime(.10f);
		Montage->BlendOut.SetBlendTime(Strike.BlendOut);
		Append(*Montage, *Clip, Strike.End);
		if (bFinal)
		{
			// The separate take starts in the down-swing exit stance.
			Append(*Montage, *Return, Return->GetPlayLength(), 1.8f);
		}
		UpdateMontage(*Montage);
		UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, TEXT("Combat"));
		if (!bKick) { UAnimationBlueprintLibrary::AddAnimationNotifyTrack(Montage, TEXT("Trail")); }
		const FName HitSocket = bKick ? FName(TEXT("ball_l")) : FName(TEXT("PalmGrip_R"));
		if (!AddHit(*Montage, Strike.Hit, HitSocket, !bKick)) { return false; }
		if (Strike.ExtraHit.Y > 0.f && !AddHit(*Montage, Strike.ExtraHit, HitSocket, true)) { return false; }

		Attack->AttackTag = Tag(TEXT("Attack.Spellblade.") + NodeSuffix);
		Attack->Montage = Montage;
		Attack->MontageSectionName = NAME_None;
		Attack->PlayRate = 1.f;
		Attack->MovementPolicy = EProject_JAttackMovementPolicy::InPlace;
		Attack->MeleeTrajectory = {};
		if (bKick)
		{
			Attack->HitSpec.bUseCanonicalBladeTipOffset = false;
			Attack->HitSpec.CanonicalBladeTipOffset = FVector::ZeroVector;
			Attack->HitSpec.TraceRadius = 32.f;
		}
		else
		{
			auto Entry = TrailEntry;
			Entry.AttackTag = Attack->AttackTag;
			Presentation.AttackPresentations.Add(Entry);
		}

		FProject_JComboNode& Node = Combo.Nodes.AddDefaulted_GetRef();
		Node.NodeTag = Tag(TEXT("Combo.Spellblade.") + NodeSuffix);
		Node.AttackDefinition = Attack;
		Node.BlockedOwnerTags.AddTag(GroundOnly);
		Node.BlockedOwnerTags.AddTag(Unmounted);
		if (bKick || Index == 1) { Node.StartInputTags.AddTag(bKick ? Kick : Flurry); }
		if (Strike.Continue.Y > 0.f)
		{
			if (!UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(Montage, TEXT("Combat"), Strike.Continue.X,
				Strike.Continue.Y - Strike.Continue.X, UProject_JAnimNotifyState_ComboWindow::StaticClass())) { return false; }
			const FGameplayTag Next = Tag(FString(TEXT("Combo.Spellblade.")) + Strikes[Index + 1].Node);
			for (const FGameplayTag Input : {Primary, Flurry})
			{
				FProject_JComboTransition& Transition = Node.Transitions.AddDefaulted_GetRef();
				Transition.InputTag = Input;
				Transition.TargetNodeTag = Next;
				Transition.BlockedOwnerTags = Node.BlockedOwnerTags;
			}
		}
		UpdateMontage(*Montage);
		Assets.Append({Montage, Attack});
	}

	for (const auto& Route : {TPair<FGameplayTag, FGameplayTag>(Tag(TEXT("InputTag.Interact")), Kick),
		TPair<FGameplayTag, FGameplayTag>(Tag(TEXT("InputTag.Skill.Z")), Flurry)})
	{
		FProject_JCombatCommandDefinition& Command = Style.CommandSet->Commands.AddDefaulted_GetRef();
		Command.CommandTag = Route.Value;
		Command.ResultInputTag = Route.Value;
		Command.OrderedInputSequence.Add(Route.Key);
		Command.RequiredOwnerTags.AddTag(Tag(TEXT("State.CombatMode")));
		Command.BlockedOwnerTags.AddTag(GroundOnly);
		Command.BlockedOwnerTags.AddTag(Unmounted);
	}

	auto* Grant = Style.InlineAbilities.FindByPredicate([Primary](const auto& Entry)
		{ return Entry.InputTag == Primary || Entry.AdditionalInputTags.HasTagExact(Primary); });
	if (!Grant) { return false; }
	Grant->AdditionalInputTags.AddTag(Kick);
	Grant->AdditionalInputTags.AddTag(Flurry);
	return true;
}

struct FBuffCast
{
	const TCHAR* Name;
	const TCHAR* Clip;
	const FKey* Key;
	TSubclassOf<UGameplayEffect> Effect;
};

TConstArrayView<FBuffCast> BuffCasts()
{
	static const FBuffCast Casts[] = {
		{TEXT("Focus"), TEXT("Buff"), &EKeys::Z, UProject_JGameplayEffect_SpellbladeFocus::StaticClass()},
		{TEXT("Mend"), TEXT("CastLeft"), &EKeys::X, UProject_JGameplayEffect_SpellbladeMend::StaticClass()},
		{TEXT("Haste"), TEXT("CastRight"), &EKeys::C, UProject_JGameplayEffect_SpellbladeHaste::StaticClass()}
	};
	return Casts;
}

void SetBuffPerformance(UAnimMontage& Montage, UAnimSequence& Clip)
{
	Montage.SetSkeleton(Clip.GetSkeleton());
	auto& Slot = Montage.SlotAnimTracks[0];
	Slot.SlotName = Project_J::Animation::CastUpperBodySlot;
	Slot.AnimTrack.AnimSegments.Reset();
	Montage.RateScale = 1.f;
	Montage.BlendIn.SetBlendTime(CastBlendIn);
	Montage.BlendOut.SetBlendTime(CastBlendOut);
	Append(Montage, Clip, Clip.GetPlayLength());
	UpdateMontage(Montage);
}

FGameplayTag SkillKeyTag(const FKey& Key)
{
	return Tag(TEXT("InputTag.Skill.") + Key.GetFName().ToString());
}

bool AddSkillKey(const FKey& Key, UInputMappingContext& Input, UProject_JSkillInputMappingData& Mapping, TArray<UObject*>& Assets)
{
	if (Input.GetMappings().ContainsByPredicate([&Key](const auto& Row) { return Row.Key == Key; })) { return false; }
	const FString ActionPath = TEXT("/Game/Input/Actions/IA_Skill_") + Key.GetFName().ToString();
	auto* Action = Duplicate(Asset<UInputAction>(TEXT("/Game/Input/Actions/IA_Skill_T")), ActionPath);
	if (!Action) { return false; }
	Input.MapKey(Action, Key);
	auto& Binding = Mapping.DirectSkillBindings.AddDefaulted_GetRef();
	Binding.InputAction = Action;
	Binding.InputTag = SkillKeyTag(Key);
	Assets.Add(Action);
	return true;
}

bool AuthorBuffCasts(UProject_JCombatStyleDefinition& Style, UInputMappingContext& Input,
	UProject_JSkillInputMappingData& Mapping, TArray<UObject*>& Assets)
{
	auto& Combo = *Style.ComboDefinition;
	auto& Commands = Style.CommandSet->Commands;
	const auto* Base = Combo.FindNode(Tag(TEXT("Combo.Spellblade.Light.1")));
	const auto Primary = Tag(TEXT("Command.Spellblade.Primary"));
	auto* Grant = Style.InlineAbilities.FindByPredicate([Primary](const auto& Entry)
		{ return Entry.InputTag == Primary || Entry.AdditionalInputTags.HasTagExact(Primary); });
	auto* Flurry = Commands.FindByPredicate([](const auto& Command)
		{ return Command.ResultInputTag == Tag(TEXT("Command.Spellblade.Flurry")); });
	if (!Base || !Base->AttackDefinition || !Grant || !Flurry || Flurry->OrderedInputSequence.Num() != 1
		|| Flurry->OrderedInputSequence[0] != SkillKeyTag(EKeys::Z)) { return false; }
	UProject_JAttackDefinition* BaseAttack = Base->AttackDefinition;
	for (const FKey& Key : {EKeys::E, EKeys::X, EKeys::C})
	{
		if (!AddSkillKey(Key, Input, Mapping, Assets)) { return false; }
	}
	Flurry->OrderedInputSequence[0] = SkillKeyTag(EKeys::E);
	for (const auto& Cast : BuffCasts())
	{
		const FString Name = Cast.Name;
		const auto CommandTag = Tag(TEXT("Command.Spellblade.") + Name);
		auto* Clip = Asset<UAnimSequence>(CastPerformancePath + Cast.Clip);
		auto* Preview = Asset<UAnimMontage>(Montages + TEXT("Skill/AM_SpellbladeKit_") + Cast.Clip);
		auto* Montage = Duplicate(Preview, Montages + TEXT("Authored/AM_Longblade_") + Name);
		auto* Attack = Duplicate(BaseAttack, SpellbladeDataRoot + TEXT("AttackDefinition/Spellblade/CombatKit/DA_Attack_Longblade_") + Name);
		if (!Clip || !Montage || !Attack || Montage->SlotAnimTracks.Num() != 1 || Clip->HasRootMotion()) { return false; }
		Montage->Notifies.Reset();
		SetBuffPerformance(*Montage, *Clip);

		const auto BuffTag = Tag(TEXT("State.Spellblade.") + Name);
		Attack->AttackTag = Tag(TEXT("Attack.Spellblade.") + Name);
		Attack->Montage = Montage;
		Attack->MontageSectionName = NAME_None;
		Attack->PlayRate = 1.f;
		Attack->MovementPolicy = EProject_JAttackMovementPolicy::InPlace;
		Attack->bUseLocomotionFacing = true;
		Attack->DamageEffect = nullptr;
		Attack->SelfEffectOnCompletion = Cast.Effect;
		Attack->HitSpec = {};
		Attack->MeleeTrajectory = {};

		auto& Node = Combo.Nodes.AddDefaulted_GetRef();
		Node.NodeTag = Tag(TEXT("Combo.Spellblade.") + Name);
		Node.StartInputTags.AddTag(CommandTag);
		Node.AttackDefinition = Attack;
		for (const auto& Blocked : {Tag(TEXT("State.Movement.InAir")), Tag(TEXT("State.Mounted")),
			Tag(TEXT("State.HitReacting")), Tag(TEXT("State.Dead")), BuffTag})
		{
			Node.BlockedOwnerTags.AddTag(Blocked);
		}
		auto& Command = Commands.AddDefaulted_GetRef();
		Command.CommandTag = CommandTag;
		Command.ResultInputTag = CommandTag;
		Command.OrderedInputSequence.Add(SkillKeyTag(*Cast.Key));
		Command.RequiredOwnerTags.AddTag(Tag(TEXT("State.CombatMode")));
		Command.BlockedOwnerTags = Node.BlockedOwnerTags;
		Grant->AdditionalInputTags.AddTag(CommandTag);
		Assets.Append({Montage, Attack});
	}
	return true;
}

bool VerifyBuffCasts(const UProject_JCombatStyleDefinition& Style, const UInputMappingContext& Input,
	const UProject_JSkillInputMappingData& Mapping)
{
	const auto HasRoute = [&Style, &Input, &Mapping](const FKey& Key, const FGameplayTag& CommandTag)
	{
		const FGameplayTag KeyTag = SkillKeyTag(Key);
		const auto* Map = Input.GetMappings().FindByPredicate([&Key](const auto& Row) { return Row.Key == Key; });
		return Map && Map->Action && Mapping.DirectSkillBindings.ContainsByPredicate([&](const auto& Row)
			{ return Row.InputAction == Map->Action && Row.InputTag == KeyTag; })
			&& Style.CommandSet->Commands.ContainsByPredicate([&](const auto& Row)
			{ return Row.OrderedInputSequence.Num() == 1 && Row.OrderedInputSequence[0] == KeyTag && Row.ResultInputTag == CommandTag; });
	};
	if (!HasRoute(EKeys::E, Tag(TEXT("Command.Spellblade.Flurry")))) { return false; }
	for (const auto& Cast : BuffCasts())
	{
		const FString Name = Cast.Name;
		const auto* Node = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.") + Name));
		const auto* Attack = Node ? Node->AttackDefinition.Get() : nullptr;
		if (!HasRoute(*Cast.Key, Tag(TEXT("Command.Spellblade.") + Name)) || !Attack || !Attack->Montage
			|| Attack->DamageEffect || Attack->SelfEffectOnCompletion != Cast.Effect
			|| Attack->Montage->SlotAnimTracks.Num() != 1 || Attack->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1
			|| !Cast.Effect->GetDefaultObject<UGameplayEffect>()->GetGrantedTags().HasTagExact(Tag(TEXT("State.Spellblade.") + Name)))
		{
			return false;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("SPELLBLADE_BUFF_ROUTES_VALIDATED"));
	return true;
}

bool AuthorReserveAttacks(UProject_JCombatStyleDefinition& Style, UInputMappingContext& Input,
	UProject_JSkillInputMappingData& Mapping, const UAnimSequence& MovementDash, TArray<UObject*>& Assets)
{
	auto& Combo = *Style.ComboDefinition;
	const auto* Base = Combo.FindNode(Tag(TEXT("Combo.Spellblade.Light.1")));
	const auto Primary = Tag(TEXT("Command.Spellblade.Primary"));
	auto* Grant = Style.InlineAbilities.FindByPredicate([Primary](const auto& Entry)
		{ return Entry.InputTag == Primary || Entry.AdditionalInputTags.HasTagExact(Primary); });
	const auto Air = Tag(TEXT("State.Movement.InAir"));
	if (!Base || !Base->AttackDefinition || !Grant || Combo.FindNode(Tag(TEXT("Combo.Spellblade.ShortDash")))
		|| Combo.FindNode(Tag(TEXT("Combo.Spellblade.Air.DownCut")))) { return false; }
	const FGameplayTag BaseNodeTag = Base->NodeTag;
	auto* BaseAttack = Base->AttackDefinition.Get();
	auto* Rush = Asset<UProject_JAttackDefinition>(SpellbladeDataRoot + TEXT("AttackDefinition/Spellblade/CombatKit/DA_Attack_SpellbladeKit_ShortDash"));
	auto* RushMontage = Rush ? Rush->Montage.Get() : nullptr;
	const FString RushSource = Sequences + TEXT("DirectBody/Melee/AS_SpellbladeDirect_ShortDash");
	auto* RushClip = Duplicate(Asset<UAnimSequence>(RushSource), RushSource + TEXT("_Travel"));
	auto* DownMontage = Asset<UAnimMontage>(Montages + TEXT("Skill/AM_SpellbladeKit_GroundSkill"));
	auto* DownClip = Asset<UAnimSequence>(Sequences + TEXT("Skill/AS_SpellbladeKit_GroundSkill"));
	auto* Down = Duplicate(BaseAttack, SpellbladeDataRoot + TEXT("AttackDefinition/Spellblade/CombatKit/DA_Attack_Longblade_DownCut"));
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Trail = Style.CombatPresentationSet->AttackPresentations.FindByPredicate([BaseAttack](const auto& Entry)
		{ return Entry.AttackTag == BaseAttack->AttackTag; });
	if (!Rush || !RushMontage || !RushClip || !DownMontage || !DownClip || !Down || !Body || !Trail
		|| RushMontage->SlotAnimTracks.Num() != 1 || DownMontage->SlotAnimTracks.Num() != 1
		|| DownClip->HasRootMotion() || DownClip->GetSkeleton() != DownMontage->GetSkeleton()) { return false; }

	TArray<FTransform> DashRoot, RushRoot;
	MovementDash.GetDataModel()->GetBoneTrackTransforms(TEXT("root"), DashRoot);
	RushClip->GetDataModel()->GetBoneTrackTransforms(TEXT("root"), RushRoot);
	if (DashRoot.Num() < 2 || RushRoot.Num() < 2) { return false; }
	const FVector Forward = (DashRoot.Last().GetTranslation() - DashRoot[0].GetTranslation()).GetSafeNormal2D();
	if (Forward.IsNearlyZero()) { return false; }
	for (int32 Key = 0; Key < RushRoot.Num(); ++Key)
	{
		const double Time = RushClip->GetPlayLength() * Key / (RushRoot.Num() - 1);
		const double Phase = FMath::Clamp((Time - .10) / .39, 0., 1.);
		const FVector Travel = Forward * (180. * Phase * Phase * (3. - 2. * Phase));
		RushRoot[Key].SetTranslation(FVector(Travel.X, Travel.Y, RushRoot[Key].GetTranslation().Z));
	}
	if (!SetTrack(*RushClip, TEXT("root"), RushRoot)) { return false; }
	RushClip->bEnableRootMotion = true;
	RushClip->bForceRootLock = false;
	RushClip->RootMotionRootLock = ERootMotionRootLock::AnimFirstFrame;

	FAnimPoseEvaluationOptions PoseOptions;
	PoseOptions.bShouldRetarget = false;
	PoseOptions.OptionalSkeletalMesh = Body;
	FAnimPose ImpactPose;
	UAnimPoseExtensions::GetAnimPoseAtTime(RushClip, .38, PoseOptions, ImpactPose);
	const auto ElbowForward = [&ImpactPose, &Forward](FName Bone)
	{
		return FVector::DotProduct(Forward, UAnimPoseExtensions::GetBonePose(ImpactPose, Bone, EAnimPoseSpaces::World).GetTranslation());
	};
	const FName LeftElbow = TEXT("lowerarm_l"), RightElbow = TEXT("lowerarm_r");
	const FName HitElbow = ElbowForward(LeftElbow) >= ElbowForward(RightElbow) ? LeftElbow : RightElbow;
	if (Body->GetRefSkeleton().FindBoneIndex(HitElbow) == INDEX_NONE) { return false; }

	RushMontage->SetSkeleton(RushClip->GetSkeleton());
	RushMontage->SlotAnimTracks[0].SlotName = TEXT("DefaultSlot");
	RushMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.Reset();
	RushMontage->Notifies.Reset();
	RushMontage->RateScale = 1.f;
	RushMontage->BlendIn.SetBlendTime(.08f);
	RushMontage->BlendOut.SetBlendTime(.22f);
	Append(*RushMontage, *RushClip, .67f);
	Append(*RushMontage, *RushClip, RushClip->GetPlayLength(), 3.2f, .67f);
	UpdateMontage(*RushMontage);
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(RushMontage, TEXT("Rush"));
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(RushMontage, TEXT("Combat"));
	if (!AddHit(*RushMontage, {.32f, .47f}, HitElbow, false) ||
		!UAnimationBlueprintLibrary::AddAnimationNotifyStateEvent(RushMontage, TEXT("Rush"), .54f, .21f,
			UProject_JAnimNotifyState_ComboWindow::StaticClass())) { return false; }
	UpdateMontage(*RushMontage);
	Rush->MontageSectionName = NAME_None;
	Rush->PlayRate = 1.f;
	Rush->DamageEffect = BaseAttack->DamageEffect;
	Rush->MovementPolicy = EProject_JAttackMovementPolicy::RootMotionMontage;
	Rush->bFaceMoveInput = true;
	Rush->bUseFlyingMovementModeForRootMotion = false;
	Rush->HitSpec.bUseCanonicalBladeTipOffset = false;
	Rush->HitSpec.CanonicalBladeTipOffset = FVector::ZeroVector;
	Rush->HitSpec.TraceRadius = 40.f;
	Rush->MeleeTrajectory = {};

	// The source begins airborne. Keep the lower body's existing falling and landing pose.
	DownMontage->SlotAnimTracks[0].SlotName = TEXT("UpperBody");
	DownMontage->SlotAnimTracks[0].AnimTrack.AnimSegments.Reset();
	DownMontage->Notifies.Reset();
	DownMontage->RateScale = 1.f;
	DownMontage->BlendIn.SetBlendTime(.055f);
	DownMontage->BlendOut.SetBlendTime(.18f);
	Append(*DownMontage, *DownClip, .32f);
	Append(*DownMontage, *DownClip, DownClip->GetPlayLength(), 4.5f, .32f);
	UpdateMontage(*DownMontage);
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(DownMontage, TEXT("Combat"));
	UAnimationBlueprintLibrary::AddAnimationNotifyTrack(DownMontage, TEXT("Trail"));
	if (!AddHit(*DownMontage, {.105f, .255f}, TEXT("PalmGrip_R"), true)) { return false; }
	UpdateMontage(*DownMontage);
	Down->AttackTag = Tag(TEXT("Attack.Spellblade.Air.DownCut"));
	Down->Montage = DownMontage;
	Down->MontageSectionName = NAME_None;
	Down->PlayRate = 1.f;
	Down->MovementPolicy = EProject_JAttackMovementPolicy::InPlace;
	Down->RequiredOwnerTags.Reset();
	Down->BlockedOwnerTags.Reset();
	Down->MeleeTrajectory = {};
	auto DownPresentation = *Trail;
	DownPresentation.AttackTag = Down->AttackTag;
	Style.CombatPresentationSet->AttackPresentations.Add(DownPresentation);

	const auto AddRoute = [&](const FKey& Key, const TCHAR* NodeName, const TCHAR* Alias, UProject_JAttackDefinition* Attack, bool bAir)
	{
		if (!AddSkillKey(Key, Input, Mapping, Assets)) { return false; }
		const auto CommandTag = Tag(FString(TEXT("Command.Spellblade.")) + Alias);
		auto& Node = Combo.Nodes.AddDefaulted_GetRef();
		Node.NodeTag = Tag(FString(TEXT("Combo.Spellblade.")) + NodeName);
		Node.AttackDefinition = Attack;
		Node.StartInputTags.AddTag(CommandTag);
		Node.BlockedOwnerTags.AddTag(Tag(TEXT("State.Mounted")));
		if (bAir) { Node.RequiredOwnerTags.AddTag(Air); }
		else
		{
			Node.BlockedOwnerTags.AddTag(Air);
			auto& Follow = Node.Transitions.AddDefaulted_GetRef();
			Follow.InputTag = Primary;
			Follow.TargetNodeTag = BaseNodeTag;
			Follow.BlockedOwnerTags = Node.BlockedOwnerTags;
		}
		auto& Command = Style.CommandSet->Commands.AddDefaulted_GetRef();
		Command.CommandTag = CommandTag;
		Command.ResultInputTag = CommandTag;
		Command.OrderedInputSequence.Add(SkillKeyTag(Key));
		Command.RequiredOwnerTags = Node.RequiredOwnerTags;
		Command.RequiredOwnerTags.AddTag(Tag(TEXT("State.CombatMode")));
		Command.BlockedOwnerTags = Node.BlockedOwnerTags;
		Grant->AdditionalInputTags.AddTag(CommandTag);
		return true;
	};
	if (!AddRoute(EKeys::G, TEXT("ShortDash"), TEXT("ElbowRush"), Rush, false)
		|| !AddRoute(EKeys::V, TEXT("Air.DownCut"), TEXT("DownCut"), Down, true)) { return false; }
	Assets.Append({RushClip, RushMontage, Rush, DownMontage, Down, Style.CombatPresentationSet.Get()});
	UE_LOG(LogTemp, Display, TEXT("Spellblade elbow rush trace uses %s"), *HitElbow.ToString());
	return true;
}

bool VerifyReserveAttacks(const UProject_JCombatStyleDefinition& Style)
{
	const auto* Rush = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.ShortDash")));
	const auto* Down = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.Air.DownCut")));
	if (!Rush || !Down || !Rush->AttackDefinition || !Down->AttackDefinition) { return false; }
	const auto* RushMontage = Rush->AttackDefinition->Montage.Get();
	const auto* DownMontage = Down->AttackDefinition->Montage.Get();
	const auto Air = Tag(TEXT("State.Movement.InAir"));
	if (!RushMontage || !RushMontage->HasRootMotion() || Rush->Transitions.Num() != 1 || !Rush->BlockedOwnerTags.HasTagExact(Air)
		|| !DownMontage || DownMontage->SlotAnimTracks.Num() != 1
		|| DownMontage->HasRootMotion() != (DownMontage->FindMetaDataByClass<UProject_JMontageVerticalMotion>() != nullptr)
		|| DownMontage->SlotAnimTracks[0].SlotName != TEXT("UpperBody") || !Down->RequiredOwnerTags.HasTagExact(Air))
	{
		return false;
	}
	UE_LOG(LogTemp, Display, TEXT("SPELLBLADE_EXTRA_ATTACKS_VALIDATED"));
	return true;
}

void ExtendExistingReturns(UProject_JComboDefinition& Combo, TArray<UObject*>& Assets)
{
	const TPair<const TCHAR*, float> Returns[] = {{TEXT("Light.1"), .18f}, {TEXT("Light.2"), .18f},
		{TEXT("Light.3"), .22f}, {TEXT("Heavy"), .19f}, {TEXT("Quick"), .15f}, {TEXT("QuickFollow"), .17f},
		{TEXT("ChordLunge"), .16f}, {TEXT("Release"), .18f}, {TEXT("Finisher"), .22f}, {TEXT("SprintAttack"), .18f}};
	for (const auto& Entry : Returns)
	{
		const auto* Node = Combo.FindNode(Tag(FString(TEXT("Combo.Spellblade.")) + Entry.Key));
		UAnimMontage* Montage = Node && Node->AttackDefinition ? Node->AttackDefinition->Montage.Get() : nullptr;
		if (!Montage || Montage->SlotAnimTracks.Num() != 1) { continue; }
		TArray<FAnimSegment>& Segments = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments;
		if (Segments.IsEmpty()) { continue; }
		FAnimSegment& Tail = Segments.Last();
		if (!Tail.GetAnimReference() || Tail.AnimPlayRate <= 0.f || Tail.LoopingCount != 1) { continue; }
		float LastEvent = 0.f;
		for (const auto& Notify : Montage->Notifies) { LastEvent = FMath::Max(LastEvent, Notify.GetEndTriggerTime()); }
		const float OldEnd = Montage->GetPlayLength();
		const float Extra = FMath::Min(Entry.Value * Tail.AnimPlayRate, Tail.GetAnimReference()->GetPlayLength() - Tail.AnimEndTime);
		if (Extra <= .01f) { continue; }
		// Retain the previous fade start or move it later. Damage, combo and
		// root motion therefore retain at least their previous montage weight.
		const float NewFade = Montage->BlendOut.GetBlendTime() + Extra / Tail.AnimPlayRate;
		if (LastEvent > OldEnd - Montage->BlendOut.GetBlendTime()) { continue; }
		Tail.AnimEndTime += Extra;
		Montage->BlendOut.SetBlendTime(NewFade);
		UpdateMontage(*Montage);
		Assets.Add(Montage);
	}
}

#include "Project_JSpellbladeStance.inl"
#include "Project_JSpellbladeSurfaceAudit.inl"

bool Verify(UProject_JCombatStyleDefinition& Style, UBlendSpace& Blend, UAnimSequence& Dash, UInputMappingContext& Input,
	UProject_JSkillInputMappingData& Mapping)
{
	TArray<FTransform> Root;
	Dash.GetDataModel()->GetBoneTrackTransforms(TEXT("root"), Root);
	if (Root.Num() < 2 || !FMath::IsNearlyEqual(FVector::Dist2D(Root[0].GetTranslation(), Root.Last().GetTranslation()), 1000., .1)) { return false; }
	if (!VerifyStance(Blend)) { return false; }
	const bool bKeys = Input.GetMappings().ContainsByPredicate([](const auto& Row)
		{ return Row.Key == EKeys::T && Row.Action && Row.Action->GetFName() == TEXT("IA_Skill_T"); })
		&& Input.GetMappings().ContainsByPredicate([](const auto& Row)
		{ return Row.Key == EKeys::Z && Row.Action && Row.Action->GetFName() == TEXT("IA_Skill_Z"); });
	if (!bKeys || !Mapping.DirectSkillBindings.ContainsByPredicate([](const auto& Row) { return Row.InputTag == Tag(TEXT("InputTag.Skill.Z")); }))
	{
		return false;
	}
	for (const FStrike& Strike : Strikes)
	{
		const auto* Node = Style.ComboDefinition->FindNode(Tag(FString(TEXT("Combo.Spellblade.")) + Strike.Node));
		if (!Node || !Node->AttackDefinition || !Node->AttackDefinition->Montage) { return false; }
	}
	const auto* Final = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.Flurry.4")));
	return Final->AttackDefinition->Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() == 2;
}
#include "Project_JSpellbladeExpansion.inl"

void MoveSwingCueStart(UAnimMontage& Montage, FAnimNotifyEvent& Event, float NewStart)
{
	const float End = Event.GetTime() + Event.GetDuration();
	Montage.Modify();
	Event.SetTime(NewStart);
	Event.SetDuration(End - NewStart);
	Montage.RefreshCacheData();
}

bool ApplyBasicCut03Trail()
{
	auto* Montage = Asset<UAnimMontage>(Montages + TEXT("CombatRevision/AM_SpellbladeKit_BasicCut03Ground"));
	if (!Montage) { return false; }
	const FGameplayTag TrailTag = Tag(TEXT("PresentationCue.Combat.Trail"));
	auto* Event = Montage->Notifies.FindByPredicate([TrailTag](const FAnimNotifyEvent& Candidate)
	{
		const auto* Notify = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Candidate.NotifyStateClass);
		return Notify && !Notify->IsGameplayWarning() && Notify->GetCueTag() == TrailTag
			&& FMath::IsNearlyEqual(Candidate.GetTime() + Candidate.GetDuration(), .85f, .001f);
	});
	if (!Event) { return false; }
	constexpr float NewStart = .420f;
	if (FMath::IsNearlyEqual(Event->GetTime(), NewStart, .001f)) { return true; }
	if (!FMath::IsNearlyEqual(Event->GetTime(), 29.f / 60.f, .001f)) { return false; }
	MoveSwingCueStart(*Montage, *Event, NewStart);
	return Save(Montage);
}

bool PolishSwingTrailsAndCastFacing(bool bApply)
{
	auto* Style = Asset<UProject_JCombatStyleDefinition>(StylePath);
	const FString TrailPath = TEXT("/Game/Effects/ProjectJ/Combat/NS_CombatTrail");
	auto* TrailSystem = Asset<UNiagaraSystem>(TrailPath);
	if (!Style || !Style->ComboDefinition || !Style->CombatPresentationSet || !TrailSystem) { return false; }
	const FGameplayTag TrailTag = Tag(TEXT("PresentationCue.Combat.Trail"));
	const FNiagaraVariable LifetimeParameter(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.Lifetime_Trail"));
	const FNiagaraVariable SpawnRateParameter(FNiagaraTypeDefinition::GetFloatDef(), TEXT("User.SpawnRate_Trail"));
	auto& Parameters = TrailSystem->GetExposedParameters();
	if (Parameters.IndexOf(LifetimeParameter) == INDEX_NONE || Parameters.IndexOf(SpawnRateParameter) == INDEX_NONE) { return false; }
	constexpr float TrailLifetime = .16f;
	constexpr float MinimumSpawnRate = 64.f;
	const float OldLifetime = Parameters.GetParameterValue<float>(LifetimeParameter);
	const float SpawnRate = Parameters.GetParameterValue<float>(SpawnRateParameter);
	UE_LOG(LogTemp, Display, TEXT("SpellbladeTrail Niagara lifetime=%.3f spawnRate=%.2f"), OldLifetime, SpawnRate);

	TArray<UObject*> Changed;
	TSet<UProject_JAttackPresentationProfile*> TrailProfiles;
	for (const auto& Entry : Style->CombatPresentationSet->AttackPresentations)
	{
		auto* Profile = Entry.Profile.Get();
		if (Profile && Profile->Cues.ContainsByPredicate([TrailSystem](const auto& Cue)
			{ return Cue.NiagaraSystem.ToSoftObjectPath() == FSoftObjectPath(TrailSystem); }))
		{
			TrailProfiles.Add(Profile);
		}
	}
	if (TrailProfiles.IsEmpty()) { return false; }
	for (auto* Profile : TrailProfiles)
	{
		for (auto& Cue : Profile->Cues)
		{
			if (Cue.NiagaraSystem.ToSoftObjectPath() != FSoftObjectPath(TrailSystem)) { continue; }
			if (!Cue.bLooping) { return false; }
			UE_LOG(LogTemp, Display, TEXT("SpellbladeTrail Profile %s %s immediate=%d budget=%d"),
				*Profile->GetName(), *Cue.CueTag.ToString(), Cue.bDestroyImmediatelyOnStop, Cue.bUseRemoteSpawnBudget);
			if (Cue.bDestroyImmediatelyOnStop)
			{
				Profile->Modify();
				Cue.bDestroyImmediatelyOnStop = false;
				Changed.AddUnique(Profile);
			}
		}
	}
	for (const FBuffCast& Cast : BuffCasts())
	{
		const auto* Node = Style->ComboDefinition->FindNode(Tag(FString(TEXT("Combo.Spellblade.")) + Cast.Name));
		auto* Attack = Node ? Node->AttackDefinition.Get() : nullptr;
		if (!Attack || !Attack->Montage || Attack->Montage->HasRootMotion()
			|| Attack->MovementPolicy != EProject_JAttackMovementPolicy::InPlace) { return false; }
		if (!Attack->bUseLocomotionFacing)
		{
			Attack->Modify();
			Attack->bUseLocomotionFacing = true;
			Changed.Add(Attack);
		}
	}

	// 60 Hz weapon-hand pose review. The original end stays aligned with the end of the swing.
	struct FTrailTiming { const TCHAR* Montage; float OriginalStart; int32 FirstVisibleFrame; };
	const FTrailTiming Timings[] = {
		{TEXT("AM_SpellbladeKit_AirLight"), .14f, 5},
		{TEXT("AM_SpellbladeKit_BasicCut01"), .48f, 27},
		{TEXT("AM_SpellbladeKit_RightAttack"), .45f, 26},
		{TEXT("AM_SpellbladeKit_QuickSkill"), .40f, 21},
		{TEXT("AM_SpellbladeKit_Finisher"), 1.53f, 89},
		{TEXT("AM_SpellbladeKit_QuickFollow"), .44f, 25},
		{TEXT("AM_SpellbladeKit_ChordLunge"), .63f, 34},
		{TEXT("AM_Longblade_Flurry_1"), .19f, 8},
		{TEXT("AM_Longblade_Flurry_1"), .82f, 46},
		{TEXT("AM_Longblade_Flurry_2"), .23f, 11},
		{TEXT("AM_Longblade_Flurry_2"), .66f, 36},
		{TEXT("AM_Longblade_Bash4"), .17f, 7},
		{TEXT("AM_Longblade_GroundGush"), 1.33f, 76},
	};
	for (const auto& Timing : Timings)
	{
		const auto* Node = Style->ComboDefinition->Nodes.FindByPredicate([&Timing](const auto& Entry)
		{
			return Entry.AttackDefinition && Entry.AttackDefinition->Montage
				&& Entry.AttackDefinition->Montage->GetFName() == FName(Timing.Montage);
		});
		if (!Node) { return false; }
		auto* Montage = Node->AttackDefinition->Montage.Get();
		const auto* Profile = Style->CombatPresentationSet->FindProfile(Node->AttackDefinition->AttackTag);
		const float NewStart = Timing.FirstVisibleFrame / 60.0f;
		auto* Event = Montage->Notifies.FindByPredicate([&](const FAnimNotifyEvent& Candidate)
		{
			const auto* Notify = Cast<UProject_JAnimNotifyState_CombatPresentationCue>(Candidate.NotifyStateClass);
			const auto* Cue = Notify && Profile ? Profile->FindCue(Notify->GetCueTag()) : nullptr;
			const bool bUsesRibbon = Cue ? Cue->NiagaraSystem.ToSoftObjectPath() == FSoftObjectPath(TrailSystem)
				: Notify && Notify->GetCueTag() == TrailTag;
			return Notify && !Notify->IsGameplayWarning() && bUsesRibbon
				&& (FMath::IsNearlyEqual(Candidate.GetTime(), Timing.OriginalStart, .001f)
					|| FMath::IsNearlyEqual(Candidate.GetTime(), NewStart, .001f));
		});
		if (!Event)
		{
			UE_LOG(LogTemp, Display, TEXT("SpellbladeTrail timing skipped for different effect %s %.3f"), Timing.Montage, Timing.OriginalStart);
			continue;
		}
		const float OriginalEnd = Event->GetTime() + Event->GetDuration();
		if (!FMath::IsNearlyEqual(Event->GetTime(), NewStart, .001f))
		{
			MoveSwingCueStart(*Montage, *Event, NewStart);
			Changed.AddUnique(Montage);
		}
		UE_LOG(LogTemp, Display, TEXT("SpellbladeTrail Timing %s %.3f -> %.3f end %.3f"),
			Timing.Montage, Timing.OriginalStart, NewStart, OriginalEnd);
	}
	if (!FMath::IsNearlyEqual(OldLifetime, TrailLifetime) || SpawnRate < MinimumSpawnRate)
	{
		TrailSystem->Modify();
		if (!Parameters.SetParameterValue(TrailLifetime, LifetimeParameter)) { return false; }
		if (SpawnRate < MinimumSpawnRate && !Parameters.SetParameterValue(MinimumSpawnRate, SpawnRateParameter)) { return false; }
		Changed.Insert(TrailSystem, 0);
	}
	if (!bApply) { return Changed.IsEmpty(); }
	for (auto* Item : Changed)
	{
		if (!Save(Item)) { return false; }
	}
	return true;
}
} // namespace

int32 UProject_JSpellbladePolishCommandlet::Main(const FString& Params)
{
	if (FParse::Param(*Params, TEXT("ApplyBasicCut03Trail")))
	{
		return ApplyBasicCut03Trail() ? 0 : 66;
	}
	if (FParse::Param(*Params, TEXT("PolishSwingTrailsAndCastFacing")))
	{
		return PolishSwingTrailsAndCastFacing(FParse::Param(*Params, TEXT("Apply"))) ? 0 : 65;
	}
	if (FParse::Param(*Params, TEXT("ImportExpansion")))
	{
		return ImportExpansion(Params) ? 0 : 17;
	}
	if (FParse::Param(*Params, TEXT("InspectReserve")))
	{
		FAnimPoseEvaluationOptions Options;
		Options.bShouldRetarget = false;
		Options.OptionalSkeletalMesh = Asset<USkeletalMesh>(TEXT("/Game/Characters/Mannequins/Meshes/SKM_Manny_Simple"));
		for (const TCHAR* Role : {TEXT("GroundSkill"), TEXT("Buff"), TEXT("CastLeft"), TEXT("CastRight")})
		{
			const auto* Clip = Asset<UAnimSequence>(Sequences + TEXT("Skill/AS_SpellbladeKit_") + Role);
			if (!Clip || !Options.OptionalSkeletalMesh) { return 10; }
			for (double Time = 0.; Time <= Clip->GetPlayLength(); Time += .1)
			{
				FAnimPose Pose;
				UAnimPoseExtensions::GetAnimPoseAtTime(Clip, Time, Options, Pose);
				const auto Read = [&Pose](FName Bone)
				{
					return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World).GetTranslation();
				};
				UE_LOG(LogTemp, Display, TEXT("ReservePose %s %.2f L=%s R=%s FeetZ=%.1f/%.1f"), Role, Time,
					*Read(TEXT("hand_l")).ToCompactString(), *Read(TEXT("hand_r")).ToCompactString(),
					Read(TEXT("ball_l")).Z, Read(TEXT("ball_r")).Z);
			}
		}
		return 0;
	}
	if (FParse::Param(*Params, TEXT("AuditShoulderSurface"))) { return ExportShoulderSurface() ? 0 : 48; }
	if (FParse::Param(*Params, TEXT("AuditClothingSurface"))) { return ExportClothingSurface() ? 0 : 62; }
	if (FParse::Param(*Params, TEXT("TrialSleeveSurface"))) { return FitSleeveSurface(false) ? 0 : 64; }
	if (FParse::Param(*Params, TEXT("ApplySleeveSurface"))) { return FitSleeveSurface(true) ? 0 : 64; }
	if (FParse::Param(*Params, TEXT("VerifySpellbladeAssetPreviews"))) { return VerifySpellbladeAssetPreviews() ? 0 : 57; }
	if (FParse::Param(*Params, TEXT("TrialReadyGuard"))) { return TrialReadyGuard(FParse::Param(*Params, TEXT("SaveReadyGuardReview"))) ? 0 : 60; }
	if (FParse::Param(*Params, TEXT("InstallReadyGuardReview"))) { return InstallReadyGuardReview() ? 0 : 61; }
	if (FParse::Param(*Params, TEXT("AuditSpellbladeArmPreview")))
	{
		auto* Blend = Asset<UBlendSpace>(BlendPath);
		return Blend && AuditSpellbladeArmPreview(*Blend) ? 0 : 58;
	}
	if (FParse::Param(*Params, TEXT("AuditLongbladeGripFrames"))) { return AuditLongbladeGripFrames(Params) ? 0 : 50; }
	if (FParse::Param(*Params, TEXT("AuditCastShoulderStages")) || FParse::Param(*Params, TEXT("TrialCastShoulderDeformation"))
		|| FParse::Param(*Params, TEXT("ApplyCastShoulderDeformation")) || FParse::Param(*Params, TEXT("TrialLongbladeActionDeformation"))
		|| FParse::Param(*Params, TEXT("AuditLongbladeActionShoulderStages")) || FParse::Param(*Params, TEXT("ApplyLongbladeActionDeformation")))
	{
		return AuditCastShoulderStages(Params) ? 0 : 49;
	}
	auto* Style = Asset<UProject_JCombatStyleDefinition>(StylePath);
	auto* Blend = Asset<UBlendSpace>(BlendPath);
	auto* Dash = Asset<UAnimSequence>(DashPath);
	auto* Input = Asset<UInputMappingContext>(InputPath);
	auto* Mapping = Asset<UProject_JSkillInputMappingData>(MappingPath);
	if (!Style || !Style->ComboDefinition || !Style->CommandSet || !Style->CombatPresentationSet || !Blend || !Dash || !Input || !Mapping)
	{
		UE_LOG(LogTemp, Error, TEXT("Spellblade polish assets could not be loaded"));
		return 1;
	}
	if (FParse::Param(*Params, TEXT("AuditTravel"))) { return AuditAttackTravel(*Style) ? 0 : 21; }
	if (FParse::Param(*Params, TEXT("ApplyActionHandoffGraph")) || FParse::Param(*Params, TEXT("VerifyActionHandoffGraph")))
	{
		return PrepareActionHandoffGraph(FParse::Param(*Params, TEXT("ApplyActionHandoffGraph"))) ? 0 : 66;
	}
	if (FParse::Param(*Params, TEXT("TrialLongbladeSourceDrive")) || FParse::Param(*Params, TEXT("ApplyLongbladeSourceDrive"))
		|| FParse::Param(*Params, TEXT("VerifyLongbladeSourceDrive")))
	{
		return ConfigureLongbladeSourceDrive(Params, *Style) ? 0 : 51;
	}
	if (FParse::Param(*Params, TEXT("AuditLongbladeWeaponArc")) || FParse::Param(*Params, TEXT("TrialLongbladeWeaponArc"))
		|| FParse::Param(*Params, TEXT("ApplyLongbladeWeaponArc"))) { return AuditLongbladeWeaponArc(Params, *Style) ? 0 : 50; }
	if (FParse::Param(*Params, TEXT("AuditStance"))) { return PrepareStance(Params, *Blend, false) ? 0 : 27; }
	if (FParse::Param(*Params, TEXT("AuditSharedArmRetarget"))) { return AuditSharedArmRetarget(Params) ? 0 : 37; }
	if (FParse::Param(*Params, TEXT("TrialCarryWrist"))) { return RefitCarryWrist(false) ? 0 : 65; }
	if (FParse::Param(*Params, TEXT("ApplyCarryWrist"))) { return RefitCarryWrist(true) ? 0 : 65; }
	if (FParse::Param(*Params, TEXT("ApplyStance"))) { return PrepareStance(Params, *Blend, true) ? 0 : 28; }
	if (FParse::Param(*Params, TEXT("VerifyStance"))) { return VerifyStance(*Blend) && VerifyGuardBlendArmEnvelope() ? 0 : 29; }
	if (FParse::Param(*Params, TEXT("VerifyGuard"))) { return VerifyGuardPose(*Style, *Blend) ? 0 : 31; }
	if (FParse::Param(*Params, TEXT("InspectGuardGraph"))) { return InspectGuardGraph() ? 0 : 32; }
	if (FParse::Param(*Params, TEXT("ApplyGuardLayer"))) { return ApplyGuardLayer() ? 0 : 33; }
	if (FParse::Param(*Params, TEXT("VerifyGuardLayer"))) { return VerifyGuardLayer() ? 0 : 34; }
	if (FParse::Param(*Params, TEXT("ApplyStrafeCarry"))) { return AuthorStrafeCarry() ? 0 : 33; }
	if (FParse::Param(*Params, TEXT("TrialStrafeBlendCarry"))) { return AuthorStrafeCarry(false) ? 0 : 59; }
	if (FParse::Param(*Params, TEXT("ApplyCarryContact"))) { return PrepareCarryContact(true) ? 0 : 40; }
	if (FParse::Param(*Params, TEXT("TrialSourceCarry"))) { return PrepareCarryContact(true, &Params) ? 0 : 44; }
	if (FParse::Param(*Params, TEXT("InstallStanceGripTrial"))) { return InstallStanceGripTrial() ? 0 : 45; }
	if (FParse::Param(*Params, TEXT("TrialForwardGuard")) || FParse::Param(*Params, TEXT("ApplyForwardGuard")))
	{
		return AuthorForwardGuard(FParse::Param(*Params, TEXT("ApplyForwardGuard"))) ? 0 : 55;
	}
	if (FParse::Param(*Params, TEXT("TrialCastWeaponClearance"))) { return AuthorCastWeaponClearance(false) ? 0 : 46; }
	if (FParse::Param(*Params, TEXT("ApplyCastWeaponClearance"))) { return AuthorCastWeaponClearance(true) ? 0 : 46; }
	if (FParse::Param(*Params, TEXT("TrialCastGroundClearance"))) { return AuthorCastGroundClearance(false) ? 0 : 46; }
	if (FParse::Param(*Params, TEXT("ApplyCastGroundClearance"))) { return AuthorCastGroundClearance(true) ? 0 : 46; }
	if (FParse::Param(*Params, TEXT("TrialCarryElbowClearance"))) { return AuthorCarryElbowClearance(false) ? 0 : 47; }
	if (FParse::Param(*Params, TEXT("ApplyCarryElbowClearance"))) { return AuthorCarryElbowClearance(true) ? 0 : 47; }
	if (FParse::Param(*Params, TEXT("VerifyCarryContact"))) { return PrepareCarryContact(false) ? 0 : 41; }
	if (FParse::Param(*Params, TEXT("ApplyCastLayer"))) { return PrepareCastLayer(true) ? 0 : 42; }
	if (FParse::Param(*Params, TEXT("VerifyCastLayer"))) { return PrepareCastLayer(false) ? 0 : 43; }
	if (FParse::Param(*Params, TEXT("ApplyWeaponCarryLayerBlend")) || FParse::Param(*Params, TEXT("VerifyWeaponCarryLayerBlend")))
	{
		return PrepareWeaponCarryLayerBlend(FParse::Param(*Params, TEXT("ApplyWeaponCarryLayerBlend"))) ? 0 : 53;
	}
	if (FParse::Param(*Params, TEXT("TrialLongbladePauldronFit")) || FParse::Param(*Params, TEXT("ApplyLongbladePauldronFit")))
	{
		return FitLongbladeArmor(FParse::Param(*Params, TEXT("ApplyLongbladePauldronFit")), false) ? 0 : 52;
	}
	if (FParse::Param(*Params, TEXT("TrialLongbladeCollarFit")) || FParse::Param(*Params, TEXT("ApplyLongbladeCollarFit")))
	{
		return FitLongbladeArmor(FParse::Param(*Params, TEXT("ApplyLongbladeCollarFit")), true) ? 0 : 54;
	}
	if (FParse::Param(*Params, TEXT("ApplyDirectBodyUpperMontages"))) { return PrepareDirectBodyUpperMontages(Params, *Style, true) ? 0 : 38; }
	if (FParse::Param(*Params, TEXT("VerifyDirectBodyUpperMontages"))) { return PrepareDirectBodyUpperMontages(Params, *Style, false) ? 0 : 39; }
	if (FParse::Param(*Params, TEXT("ApplyDowncutTravel"))) { return ApplyDowncutTravel(*Style) ? 0 : 25; }
	if (FParse::Param(*Params, TEXT("VerifyDowncutTravel"))) { return VerifyDowncutTravel(*Style) ? 0 : 26; }
	if (FParse::Param(*Params, TEXT("ApplyAttackTravel")))
	{
		TArray<UObject*> Assets;
		if (!ApplyAttackTravel(*Style, Assets) || !VerifyAttackTravel(*Style))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not apply planar attack travel"));
			return 22;
		}
		for (auto* Item : Assets)
		{
			if (!Save(Item)) { return 23; }
		}
		return 0;
	}
	if (FParse::Param(*Params, TEXT("VerifyAttackTravel"))) { return VerifyAttackTravel(*Style) ? 0 : 24; }
	if (FParse::Param(*Params, TEXT("ApplyExpansion")))
	{
		TArray<UObject*> Assets;
		if (!AuthorExpansion(*Style, *Mapping, Assets) || !VerifyExpansion(*Style, *Mapping))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not author the Caps Lock expansion"));
			return 18;
		}
		Assets.Append({Style->ComboDefinition.Get(), Style->CommandSet.Get(), Style->CombatPresentationSet.Get(), Style, Mapping});
		for (auto* Item : Assets)
		{
			if (!Save(Item)) { return 19; }
		}
		return 0;
	}
	if (FParse::Param(*Params, TEXT("VerifyExpansion")) && !VerifyExpansion(*Style, *Mapping)) { return 20; }
	if (FParse::Param(*Params, TEXT("ApplyExtraAttacks")))
	{
		TArray<UObject*> Assets;
		if (!AuthorReserveAttacks(*Style, *Input, *Mapping, *Dash, Assets) || !VerifyReserveAttacks(*Style))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not author the G and V attacks"));
			return 14;
		}
		Assets.Append({Style->ComboDefinition.Get(), Style->CommandSet.Get(), Style, Mapping, Input});
		for (UObject* Item : Assets)
		{
			if (!Save(Item)) { return 15; }
		}
	}
	else if (FParse::Param(*Params, TEXT("VerifyExtraAttacks")) && !VerifyReserveAttacks(*Style))
	{
		UE_LOG(LogTemp, Error, TEXT("Saved G and V routes did not pass validation"));
		return 16;
	}
	else if (FParse::Param(*Params, TEXT("ApplyBuffs")))
	{
		TArray<UObject*> Assets;
		if (!AuthorBuffCasts(*Style, *Input, *Mapping, Assets) || !VerifyBuffCasts(*Style, *Input, *Mapping))
		{
			UE_LOG(LogTemp, Error, TEXT("Could not author the E and Z/X/C routes"));
			return 11;
		}
		Assets.Append({Style->ComboDefinition.Get(), Style->CommandSet.Get(), Style, Mapping, Input});
		for (UObject* Item : Assets)
		{
			if (!Save(Item)) { return 12; }
		}
	}
	else if (FParse::Param(*Params, TEXT("VerifyBuffs")) && !VerifyBuffCasts(*Style, *Input, *Mapping))
	{
		UE_LOG(LogTemp, Error, TEXT("Saved E and Z/X/C routes did not pass validation"));
		return 13;
	}
	else if (FParse::Param(*Params, TEXT("Apply")))
	{
		auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
		if (!Body || Body->GetRefSkeleton().FindBoneIndex(TEXT("ball_l")) == INDEX_NONE
			|| Input->GetMappings().ContainsByPredicate([](const auto& Row) { return Row.Key == EKeys::Z; })
			|| Style->ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.Kick")))) { return 2; }
		TArray<UObject*> Assets;
		UInputAction* Action = Duplicate(Asset<UInputAction>(TEXT("/Game/Input/Actions/IA_Skill_T")), TEXT("/Game/Input/Actions/IA_Skill_Z"));
		if (!Action) { return 3; }
		Input->MapKey(Action, EKeys::Z);
		FProject_JDirectSkillInputBinding& Binding = Mapping->DirectSkillBindings.AddDefaulted_GetRef();
		Binding.InputAction = Action;
		Binding.InputTag = Tag(TEXT("InputTag.Skill.Z"));
		Assets.Add(Action);

		if (!SetDashDistance(*Dash)) { UE_LOG(LogTemp, Error, TEXT("Could not scale the saved 350 cm dash")); return 4; }
		if (!PrepareStance(Params, *Blend, true)) { UE_LOG(LogTemp, Error, TEXT("Could not author the combat guard samples")); return 5; }
		if (!AuthorStrikes(*Style, Assets)) { UE_LOG(LogTemp, Error, TEXT("Could not author the kick and flurry")); return 6; }
		ExtendExistingReturns(*Style->ComboDefinition, Assets);
		Assets.Append({Dash, Style->CombatPresentationSet.Get(), Style->ComboDefinition.Get(), Style->CommandSet.Get(), Style, Mapping, Input});
		if (!Verify(*Style, *Blend, *Dash, *Input, *Mapping)) { return 7; }
		for (UObject* Item : Assets)
		{
			if (!Save(Item)) { return 8; }
		}
	}
	else if (!Verify(*Style, *Blend, *Dash, *Input, *Mapping))
	{
		UE_LOG(LogTemp, Error, TEXT("Saved Spellblade polish assets did not pass validation"));
		return 9;
	}
	UE_LOG(LogTemp, Display, TEXT("SPELLBLADE_POLISH_VALIDATED"));
	return 0;
}
