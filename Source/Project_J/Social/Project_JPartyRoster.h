#pragma once
#include "CoreMinimal.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "Project_JPartyRoster.generated.h"

class AProject_JPlayerState;
inline constexpr int32 ProjectJMaxPartyRosterMembers = 8;

/** Member-visible summary only. Never contains an account, remote actor or private combat state. */
USTRUCT(BlueprintType)
struct PROJECT_J_API FProject_JPartyMemberSummary
{
    GENERATED_BODY()
    UPROPERTY(BlueprintReadOnly) FGuid CharacterId;
    UPROPERTY(BlueprintReadOnly) FName PartyId;
    UPROPERTY(BlueprintReadOnly) uint8 Order = 0;
    UPROPERTY(BlueprintReadOnly) bool bLeader = false;
    /** A current binding in this server; not global/offline presence. */
    UPROPERTY(BlueprintReadOnly) bool bAvailableHere = false;
    UPROPERTY(BlueprintReadOnly) FString DisplayName;
    UPROPERTY(BlueprintReadOnly) int32 Level = 0;
    UPROPERTY(BlueprintReadOnly) bool bHealthKnown = false;
    UPROPERTY(BlueprintReadOnly) int32 HealthPermille = 0;

    bool operator==(const FProject_JPartyMemberSummary& Other) const
    {
        return CharacterId == Other.CharacterId && PartyId == Other.PartyId && Order == Other.Order
            && bLeader == Other.bLeader && bAvailableHere == Other.bAvailableHere
            && DisplayName == Other.DisplayName && Level == Other.Level
            && bHealthKnown == Other.bHealthKnown && HealthPermille == Other.HealthPermille;
    }
};

USTRUCT()
struct PROJECT_J_API FProject_JPartyRosterItem : public FFastArraySerializerItem
{
    GENERATED_BODY()
    UPROPERTY() FProject_JPartyMemberSummary Summary;
};

USTRUCT()
struct PROJECT_J_API FProject_JPartyRosterArray : public FFastArraySerializer
{
    GENERATED_BODY()
    UPROPERTY() TArray<FProject_JPartyRosterItem> Items;
    UPROPERTY(NotReplicated) TObjectPtr<AProject_JPlayerState> OwnerState;
    void PostReplicatedReceive(const FPostReplicatedReceiveParameters& Parameters);
    bool NetDeltaSerialize(FNetDeltaSerializeInfo& Params)
    {
        return FFastArraySerializer::FastArrayDeltaSerialize<FProject_JPartyRosterItem, FProject_JPartyRosterArray>(Items, Params, *this);
    }
};
template<> struct TStructOpsTypeTraits<FProject_JPartyRosterArray> : TStructOpsTypeTraitsBase2<FProject_JPartyRosterArray>
{
    enum { WithNetDeltaSerializer = true };
};
