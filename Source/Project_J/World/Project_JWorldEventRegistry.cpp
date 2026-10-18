#include "World/Project_JWorldEventRegistry.h"
#include "GameFramework/Actor.h"
bool UProject_JWorldEventRegistry::Register(FName Id, AActor* Owner)
{
    if (Id.IsNone() || !IsValid(Owner) || Owner->GetWorld() != GetWorld() || !Owner->HasAuthority()) { return false; }
    TWeakObjectPtr<AActor>& Existing = Owners.FindOrAdd(Id);
    if (Existing.IsValid() && Existing.Get() != Owner) { return false; }
    Existing = Owner;
    return true;
}
void UProject_JWorldEventRegistry::Unregister(FName Id, AActor* Owner)
{
    if (const TWeakObjectPtr<AActor>* Existing = Owners.Find(Id); Existing && Existing->Get() == Owner) { Owners.Remove(Id); }
}
void UProject_JWorldEventRegistry::Deinitialize() { Owners.Reset(); Super::Deinitialize(); }
