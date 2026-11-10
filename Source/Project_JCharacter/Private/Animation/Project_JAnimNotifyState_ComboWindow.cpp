#include "Animation/Project_JAnimNotifyState_ComboWindow.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "AbilitySystemBlueprintLibrary.h"
#include "Animation/ActiveMontageInstanceScope.h"
#include "Animation/AnimNotifyEndDataContext.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "Animation/AnimSequenceBase.h"
#include "Combat/Project_JGameplayAbility_Melee.h"
#include "Components/SkeletalMeshComponent.h"
#include "Project_JGameplayTags.h"

UProject_JAnimNotifyState_ComboWindow::UProject_JAnimNotifyState_ComboWindow()
{
	ComboWindowTag = FProject_JGameplayTags::Get().Event_Combat_ComboWindow;
}

void UProject_JAnimNotifyState_ComboWindow::BranchingPointNotifyBegin(FBranchingPointNotifyPayload& Payload)
{
	// The engine's base forwarding supplies an empty reference for branching
	// points. Keep their montage provenance just as queued notifies do.
	if (!Payload.NotifyEvent || Payload.MontageInstanceID == INDEX_NONE) { return; }
	FAnimNotifyEventReference Reference(Payload.NotifyEvent, Payload.SequenceAsset);
	Reference.AddContextData<UE::Anim::FAnimNotifyMontageInstanceContext>(Payload.MontageInstanceID);
	Reference.SetNotifyInstanceID(Payload.MontageInstanceID);
	NotifyBegin(Payload.SkelMeshComponent, Payload.SequenceAsset, Payload.NotifyEvent->GetDuration(), Reference);
}

void UProject_JAnimNotifyState_ComboWindow::BranchingPointNotifyEnd(FBranchingPointNotifyPayload& Payload)
{
	FAnimNotifyEventReference Reference(Payload.NotifyEvent, Payload.SequenceAsset);
	Reference.SetNotifyInstanceID(Payload.MontageInstanceID);
	if (Payload.bReachedEnd)
	{
		Reference.AddContextData<UE::Anim::FAnimNotifyEndDataContext>(true);
	}
	NotifyEnd(Payload.SkelMeshComponent, Payload.SequenceAsset, Reference);
}

void UProject_JAnimNotifyState_ComboWindow::NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration, const FAnimNotifyEventReference& EventReference)
{
	Super::NotifyBegin(MeshComp, Animation, TotalDuration, EventReference);
	AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr;
	auto* ASC = Owner ? UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Owner) : nullptr;
	auto* Ability = ASC ? Cast<UProject_JGameplayAbility_Melee>(ASC->GetAnimatingAbility()) : nullptr;
	if (!Ability || !Ability->MatchesAttackMontageSource(MeshComp, EventReference)) { return; }
	for (auto It = Windows.CreateIterator(); It; ++It) { if (!It.Key().IsValid()) { It.RemoveCurrent(); } }
	auto& MeshWindows = Windows.FindOrAdd(MeshComp);
	for (auto It = MeshWindows.CreateIterator(); It; ++It)
	{
		if (!It.Value().Ability.IsValid() || !It.Value().Ability->IsComboWindowCurrent(It.Value().Token)) { It.RemoveCurrent(); }
	}
	const int32 ID = EventReference.GetNotifyInstanceID();
	if (MeshWindows.Contains(ID)) { return; }
	const uint64 Token = Ability->BeginComboWindow();
	if (!Token) { return; }
	MeshWindows.Add(ID, { Ability, Token });
	FGameplayEventData Payload;
	Payload.EventTag = ComboWindowTag;
	Payload.EventMagnitude = 1.0f;
	Payload.Instigator = Owner;
	Payload.Target = Owner;
	Payload.OptionalObject = Ability;
	UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, ComboWindowTag, Payload);
}

void UProject_JAnimNotifyState_ComboWindow::NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, const FAnimNotifyEventReference& EventReference)
{
	if (auto* MeshWindows = Windows.Find(MeshComp))
	{
		FWindowLease Lease;
		const bool bFound = MeshWindows->RemoveAndCopyValue(EventReference.GetNotifyInstanceID(), Lease);
		if (MeshWindows->IsEmpty()) { Windows.Remove(MeshComp); }
		if (bFound && Lease.Ability.IsValid() && Lease.Ability->EndComboWindow(Lease.Token))
		{
			if (AActor* Owner = MeshComp ? MeshComp->GetOwner() : nullptr)
			{
				FGameplayEventData Payload;
				Payload.EventTag = ComboWindowTag;
				Payload.Instigator = Owner;
				Payload.Target = Owner;
				Payload.OptionalObject = Lease.Ability.Get();
				UAbilitySystemBlueprintLibrary::SendGameplayEventToActor(Owner, ComboWindowTag, Payload);
			}
		}
	}
	Super::NotifyEnd(MeshComp, Animation, EventReference);
}
