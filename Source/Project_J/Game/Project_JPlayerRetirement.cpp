#include "Game/Project_JPlayerRetirement.h"
#include "Game/Project_JPlayerState.h"
#include "Project_JPlayerCharacter.h"
#include "Project_JAbilitySystemComponent.h"
#include "Mount/Project_JMountCharacter.h"
#include "Mount/Project_JMountComponent.h"
#include "GameFramework/PlayerController.h"
#include "UObject/StrongObjectPtr.h"

bool ProjectJ::Lifecycle::RetireConnectionAvatar(APlayerController* Controller, TFunctionRef<bool()> IsRetirementCurrent)
{
    if (!Controller || !Controller->HasAuthority() || !IsRetirementCurrent()) return false;
    const TStrongObjectPtr<APlayerController> PC(Controller);
    const TStrongObjectPtr<AProject_JPlayerState> State(PC->GetPlayerState<AProject_JPlayerState>());
    const TStrongObjectPtr<UProject_JAbilitySystemComponent> ASC(State.IsValid() ? State->GetProjectJAbilitySystemComponent() : nullptr);
    const TStrongObjectPtr<AProject_JPlayerCharacter> Avatar(ASC.IsValid() ? Cast<AProject_JPlayerCharacter>(ASC->GetAvatarActor()) : nullptr);
    const TStrongObjectPtr<APawn> Controlled(PC->GetPawn());
    if (!State.IsValid() || !ASC.IsValid() || !Avatar.IsValid() || !Controlled.IsValid()
        || ASC->GetOwnerActor() != State.Get() || State->GetOwner() != PC.Get()
        || Avatar->GetAbilitySystemComponent() != ASC.Get() || Controlled->GetController() != PC.Get()) return false;
    const TStrongObjectPtr<AProject_JMountCharacter> Mount(Avatar->GetMountComponent() ? Avatar->GetMountComponent()->GetMountedMount() : nullptr);
    if (Controlled.Get() != Avatar.Get() && (!Mount.IsValid() || Controlled.Get() != Mount.Get() || Mount->GetRider() != Avatar.Get())) return false;
    const TStrongObjectPtr<AProject_JMountCharacter> OwnedMount(Avatar->GetSummonedMount());
    const uint64 Revision = ASC->GetActorInfoRevision();
    const auto Current = [&] { return IsRetirementCurrent() && IsValid(State.Get()) && IsValid(Avatar.Get())
        && !Avatar->IsActorBeingDestroyed() && PC->GetPlayerState<AProject_JPlayerState>() == State.Get()
        && State->GetOwner() == PC.Get() && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging()
        && ASC->GetOwnerActor() == State.Get() && ASC->GetAvatarActor() == Avatar.Get() && ASC->GetActorInfoRevision() == Revision; };
    TArray<FGameplayAbilitySpecHandle> Executions;
    for (const auto& Spec : ASC->GetActivatableAbilities()) if (Spec.IsActive()) Executions.Add(Spec.Handle);
    for (const auto Handle : Executions)
    {
        if (!Current()) return false;
        ASC->CancelAbilityHandle(Handle);
    }
    if (!Current() || PC->GetPawn() != Controlled.Get() || Controlled->GetController() != PC.Get()
        || (Avatar->GetMountComponent() && Avatar->GetMountComponent()->GetMountedMount() != Mount.Get())) return false;
    if (Mount.IsValid() && Mount->GetRider() == Avatar.Get()) Mount->DismountRider(true, false);
    if (!Current()) return false;
    if (PC->GetPawn() == Controlled.Get() && Controlled->GetController() == PC.Get()) PC->UnPossess();
    if (!Current() || PC->GetPawn() || Avatar->GetController()
        || (Avatar->GetMountComponent() && Avatar->GetMountComponent()->GetMountedMount())) return false;
    ASC->ClearActorInfo();
    const uint64 ClearedRevision = ASC->GetActorInfoRevision();
    const auto StillRetired = [&] { return IsRetirementCurrent() && IsValid(Avatar.Get()) && !Avatar->IsActorBeingDestroyed()
        && PC->GetPlayerState<AProject_JPlayerState>() == State.Get() && State->GetOwner() == PC.Get()
        && !PC->GetPawn() && !Avatar->GetController() && !ASC->IsBeingDestroyed() && !ASC->IsActorInfoChanging()
        && ASC->GetActorInfoRevision() == ClearedRevision && !ASC->GetAvatarActor(); };
    if (!StillRetired()) return false;
    // A borrowed/shared mount loses its seat, not its actor. Only this Avatar's
    // explicit summoned mount with unchanged owner and no new rider is disposable.
    if (OwnedMount.IsValid() && OwnedMount->GetOwner() == Avatar.Get()
        && !OwnedMount->GetRider() && !OwnedMount->GetController()) OwnedMount->Destroy();
    if (!StillRetired()) return false;
    return Avatar->Destroy();
}
