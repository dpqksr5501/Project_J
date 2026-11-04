#include "Combat/Project_JMeleeTrajectory.h"
#include "Components/Project_JCombatHitValidationComponent.h"

namespace
{
bool ValidUniformTransform(const FTransform& T)
{
    const auto S=T.GetScale3D();
    return !T.ContainsNaN() && T.GetRotation().IsNormalized() && S.GetMin()>0 &&
        FMath::IsNearlyEqual(S.X,S.Y) && FMath::IsNearlyEqual(S.X,S.Z);
}
}
FString FProject_JMeleeTrajectory::GetValidationFailure() const
{
    if (!bEnabled) return {};
    if (SchemaVersion!=1 || SourceMesh.IsNull() || SourceMontage.IsNull() || NotifyIndex<0 || SocketName.IsNone() || SourceStamp.IsEmpty())
        return TEXT("Enabled melee path needs its supported schema and complete selected source stamp.");
    if (!FMath::IsFinite(WindowStart) || !FMath::IsFinite(WindowEnd) || WindowStart<0 || WindowEnd<=WindowStart || BladeTipOffset.ContainsNaN())
        return TEXT("Melee path window and tip must be finite and ordered.");
    if (Keys.Num()<2 || Keys.Num()>MaxKeys || Keys[0].MontageTime!=WindowStart || Keys.Last().MontageTime!=WindowEnd)
        return TEXT("Melee path needs 2..64 keys including its exact window endpoints.");
    for (int32 I=0;I<Keys.Num();++I)
        if (!FMath::IsFinite(Keys[I].MontageTime) || Keys[I].MeshPosition.ContainsNaN() ||
            (I && Keys[I].MontageTime<=Keys[I-1].MontageTime))
            return TEXT("Melee path keys must be finite and strictly ordered.");
    return {};
}
bool FProject_JMeleeTrajectory::BuildWorldSegments(float PreviousMontageTime,float CurrentMontageTime,float From,float To,
    const FTransform& PreviousRoot,const FTransform& CurrentRoot,const FTransform& MeshToRoot,
    TArray<FProject_JAuthoritativeTraceSegment>& OutSegments) const
{
    OutSegments.Reset();
    if (!bEnabled || !GetValidationFailure().IsEmpty() || !FMath::IsFinite(PreviousMontageTime) || !FMath::IsFinite(CurrentMontageTime) ||
        !FMath::IsFinite(From) || !FMath::IsFinite(To) || CurrentMontageTime<=PreviousMontageTime || From<PreviousMontageTime || To>CurrentMontageTime ||
        From<WindowStart || To>WindowEnd || To<=From || !ValidUniformTransform(PreviousRoot) || !ValidUniformTransform(CurrentRoot) || !ValidUniformTransform(MeshToRoot) ||
        !PreviousRoot.GetScale3D().Equals(CurrentRoot.GetScale3D(),UE_KINDA_SMALL_NUMBER)) return false;
    const auto WorldPoint=[&](float Time,FVector& Out)
    {
        int32 Index=1;
        while (Index<Keys.Num() && Keys[Index].MontageTime<Time) ++Index;
        if (!Keys.IsValidIndex(Index)) return false;
        const auto& A=Keys[Index-1]; const auto& B=Keys[Index];
        const double LocalAlpha=(double(Time)-A.MontageTime)/(double(B.MontageTime)-A.MontageTime);
        const double RootAlpha=(double(Time)-PreviousMontageTime)/(double(CurrentMontageTime)-PreviousMontageTime);
        const FTransform Root(FQuat::Slerp(PreviousRoot.GetRotation(),CurrentRoot.GetRotation(),RootAlpha).GetNormalized(),
            FMath::Lerp(PreviousRoot.GetLocation(),CurrentRoot.GetLocation(),RootAlpha),PreviousRoot.GetScale3D());
        Out=Root.TransformPosition(MeshToRoot.TransformPosition(FMath::Lerp(A.MeshPosition,B.MeshPosition,LocalAlpha)));
        return !Out.ContainsNaN();
    };
    FVector Previous;
    if (!WorldPoint(From,Previous)) return false;
    OutSegments.Reserve(Keys.Num()-1);
    const auto Append=[&](float Time)
    {
        FVector Point;
        if (!WorldPoint(Time,Point)) return false;
        FProject_JAuthoritativeTraceSegment Segment; Segment.TraceStart=Previous; Segment.TraceEnd=Point;
        OutSegments.Add(Segment); Previous=Point; return true;
    };
    for (const auto& Key:Keys)
        if (Key.MontageTime>From && Key.MontageTime<To && !Append(Key.MontageTime)) { OutSegments.Reset(); return false; }
    if (!Append(To) || OutSegments.Num()>MaxKeys-1) { OutSegments.Reset(); return false; }
    return true;
}
