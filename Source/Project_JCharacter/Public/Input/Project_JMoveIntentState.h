#pragma once
#include "CoreMinimal.h"

/** Device-independent semantic movement directions used only by locomotion presentation. */
enum class EProject_JMoveIntentDirection : uint8 { Forward, Backward, Left, Right };

/** One bounded owner of held directions and their press order; no cumulative sequence. */
class FProject_JMoveIntentState
{
public:
    bool Press(EProject_JMoveIntentDirection Direction)
    {
        if ((Direction != EProject_JMoveIntentDirection::Forward && Direction != EProject_JMoveIntentDirection::Backward
            && Direction != EProject_JMoveIntentDirection::Left && Direction != EProject_JMoveIntentDirection::Right)
            || Held.Contains(Direction)) { return false; }
        Held.Add(Direction); return true;
    }
    bool Release(EProject_JMoveIntentDirection Direction) { return Held.Remove(Direction) != 0; }
    void Clear() { Held.Reset(); }
    bool HasActiveIntent() const { return !Held.IsEmpty(); }
    /** Zero means released; 1..4 is relative order among currently held directions. */
    int32 GetPressRank(EProject_JMoveIntentDirection Direction) const { return Held.IndexOfByKey(Direction) + 1; }
    FVector2D GetIntent() const
    {
        const int32 Horizontal = GetPressRank(EProject_JMoveIntentDirection::Right) - GetPressRank(EProject_JMoveIntentDirection::Left);
        const int32 Vertical = GetPressRank(EProject_JMoveIntentDirection::Forward) - GetPressRank(EProject_JMoveIntentDirection::Backward);
        return FVector2D(FMath::Sign(Horizontal), FMath::Sign(Vertical)).GetSafeNormal();
    }
private:
    TArray<EProject_JMoveIntentDirection, TInlineAllocator<4>> Held;
};
