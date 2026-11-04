#pragma once
#include "CoreMinimal.h"

namespace ProjectJ::MMO
{
/** Server-local participation evidence, not a reward wallet or durable receipt.
 * Stable character IDs survive disconnect/Avatar replacement. Closed rounds are
 * retained; capacity fails closed rather than evicting unpaid participation.
 */
class FWorldEventContributions
{
public:
    explicit FWorldEventContributions(int32 InMaxRounds = 128, int32 InMaxCharacters = 2048)
        : MaxRounds(FMath::Max(1, InMaxRounds)), MaxCharacters(FMath::Max(1, InMaxCharacters)) {}
    bool Begin(FGuid Round)
    {
        if (!Round.IsValid() || Rounds.Contains(Round) || Rounds.Num() >= MaxRounds) return false;
        Rounds.Add(Round); return true;
    }
    bool Record(FGuid Round, FGuid Character, double EffectiveDamage)
    {
        auto* State = Rounds.Find(Round);
        if (!State || State->bClosed || !Character.IsValid() || !FMath::IsFinite(EffectiveDamage) || EffectiveDamage <= 0) return false;
        auto* Previous = State->Damage.Find(Character);
        if (!Previous && State->Damage.Num() >= MaxCharacters) { State->bIncomplete = true; return false; }
        const double Total = (Previous ? *Previous : 0) + EffectiveDamage;
        if (!FMath::IsFinite(Total)) { State->bIncomplete = true; return false; }
        State->Damage.Add(Character, Total); return true;
    }
    void Invalidate(FGuid Round) { if (auto* State = Rounds.Find(Round)) State->bIncomplete = true; }
    bool Close(FGuid Round, bool bDefeated)
    {
        auto* State = Rounds.Find(Round);
        if (!State || State->bClosed) return false;
        State->bClosed = true; State->bDefeated = bDefeated; return true;
    }
    bool IsEligible(FGuid Round, FGuid Character) const
    {
        const auto* State = Rounds.Find(Round);
        return State && State->bClosed && State->bDefeated && !State->bIncomplete && State->Damage.Contains(Character);
    }
    double GetDamage(FGuid Round, FGuid Character) const
    {
        const auto* State = Rounds.Find(Round);
        const auto* Damage = State ? State->Damage.Find(Character) : nullptr;
        return Damage ? *Damage : 0;
    }
private:
    struct FRound { bool bClosed = false, bDefeated = false, bIncomplete = false; TMap<FGuid, double> Damage; };
    int32 MaxRounds, MaxCharacters;
    TMap<FGuid, FRound> Rounds;
};
}
