#pragma once

#include "CoreMinimal.h"
#include "Project_JContinuousCurvePolicy.generated.h"

/** Shared physical evidence rules; does not own movement, clips or search. */
USTRUCT(BlueprintType)
struct PROJECT_JCHARACTER_API FProject_JContinuousCurveSettings
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Curve, meta=(ClampMin="0.4", ClampMax="1.2", Units="s"))
	float Window = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Curve, meta=(ClampMin="0.15", ClampMax="0.8", Units="s"))
	float Confirmation = 0.35f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Curve, meta=(ClampMin="1", ClampMax="15", Units="deg"))
	float HeadingSweep = 3.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Curve, meta=(ClampMin="10", ClampMax="300", Units="cm"))
	float TravelDistance = 100.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Curve, meta=(ClampMin="0.5", ClampMax="1"))
	float DirectionConsistency = 0.85f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category=Curve, meta=(ClampMin="0.05", ClampMax="0.25", Units="s"))
	float QuietGrace = 0.12f;
	FProject_JContinuousCurveSettings Resolved() const
	{
		const auto Safe = [](float V, float F, float Lo, float Hi) { return FMath::IsFinite(V) ? FMath::Clamp(V, Lo, Hi) : F; };
		auto S = *this;
		S.Window = Safe(Window, 1, .4f, 1.2f);
		S.Confirmation = FMath::Min(Safe(Confirmation, .35f, .15f, .8f), S.Window);
		S.HeadingSweep = Safe(HeadingSweep, 3, 1, 15);
		S.TravelDistance = Safe(TravelDistance, 100, 10, 300);
		S.DirectionConsistency = Safe(DirectionConsistency, .85f, .5f, 1);
		S.QuietGrace = Safe(QuietGrace, .12f, .05f, .25f);
		return S;
	}
};

/** Bounded time-based history shared by authored exit and regular Strafe routing. */
class FProject_JContinuousCurvePolicy
{
public:
	struct FInput
	{
		bool bEligible = false, bHasRequestedHeading = false;
		int32 OwnerRevision = 0, HistoryRevision = 0, Mode = 0, Gait = 0;
		double Now = 0;
		FVector Position = FVector::ZeroVector, Velocity = FVector::ZeroVector;
		float RequestedHeading = 0;
	};
	struct FResult
	{
		bool bActive = false, bRecent = false;
		float Distance = 0, Sweep = 0, Consistency = 0, Duration = 0;
	};
	void Reset()
	{
		bHasSample = false; Count = Next = 0; Last = {}; Result = {};
		Travel = Sweep = AbsoluteSweep = IntentSweep = 0; Started = LastTurn = LastIntentTurn = -1;
	}
	const FResult& GetResult() const { return Result; }
	FResult Update(const FInput& I, FProject_JContinuousCurveSettings Settings)
	{
		const auto S = Settings.Resolved();
		if (!I.bEligible || !FMath::IsFinite(I.Now) || I.Position.ContainsNaN() || I.Velocity.ContainsNaN() ||
			I.Velocity.SizeSquared2D() <= UE_SMALL_NUMBER || (I.bHasRequestedHeading && !FMath::IsFinite(I.RequestedHeading)))
		{ Reset(); return Result; }
		if (!bHasSample || I.OwnerRevision != Last.OwnerRevision || I.HistoryRevision != Last.HistoryRevision ||
			I.Mode != Last.Mode || I.Gait != Last.Gait || I.bHasRequestedHeading != Last.bHasRequestedHeading ||
			I.Now < Last.Now || I.Now - Last.Now > .1)
		{ Reset(); Seed(I); return Result; }
		const double Dt = I.Now - Last.Now;
		if (Dt <= UE_SMALL_NUMBER) return Result;
		const float Distance = FVector::Dist2D(Last.Position, I.Position);
		const float Step = FMath::FindDeltaAngleDegrees(Last.Velocity.Rotation().Yaw, I.Velocity.Rotation().Yaw);
		const float IntentStep = FMath::FindDeltaAngleDegrees(Last.RequestedHeading, I.RequestedHeading);
		const float Expected = FMath::Max(Last.Velocity.Size2D(), I.Velocity.Size2D()) * Dt;
		if (Distance <= UE_SMALL_NUMBER || Distance > Expected * 2 + 2 || FMath::Abs(Step) > 30 ||
			(I.bHasRequestedHeading && FMath::Abs(IntentStep) > 30))
		{ Reset(); Seed(I); return Result; }
		Last = I;
		if (FMath::Abs(Step) > .0001f)
		{
			if (Started < 0) Started = I.Now - Dt;
			LastTurn = I.Now;
		}
		if (I.bHasRequestedHeading && FMath::Abs(IntentStep) > .0001f) LastIntentTurn = I.Now;
		// An isolated target nudge followed by physical catch-up is not sustained intent.
		if ((LastTurn >= 0 && I.Now - LastTurn > S.QuietGrace) ||
			(I.bHasRequestedHeading && LastIntentTurn >= 0 && I.Now - LastIntentTurn > S.QuietGrace))
		{ Reset(); Seed(I); return Result; }
		Travel += Distance; Sweep += Step; AbsoluteSweep += FMath::Abs(Step); IntentSweep += IntentStep;
		if (I.Now - Samples[(Next + UE_ARRAY_COUNT(Samples) - 1) % UE_ARRAY_COUNT(Samples)].Time >= .02) Add(I.Now);
		const FSample Base = HistoryAt(FMath::Max(I.Now - S.Window, Started >= 0 ? Started : I.Now));
		Result.Distance = Travel - Base.Travel;
		Result.Sweep = Sweep - Base.Sweep;
		const float Absolute = AbsoluteSweep - Base.AbsoluteSweep;
		Result.Consistency = Absolute > UE_SMALL_NUMBER ? FMath::Abs(Result.Sweep) / Absolute : 0;
		const double ConfirmedThrough = I.bHasRequestedHeading ? FMath::Min(LastTurn, LastIntentTurn) : LastTurn;
		Result.Duration = Started >= 0 ? FMath::Max(0.f, float(ConfirmedThrough - FMath::Max(Started, Base.Time))) : 0;
		Result.bRecent = LastTurn >= 0 && I.Now - LastTurn <= S.QuietGrace &&
			(!I.bHasRequestedHeading || (LastIntentTurn >= 0 && I.Now - LastIntentTurn <= S.QuietGrace &&
				(IntentSweep - Base.IntentSweep) * Result.Sweep > 0));
		Result.bActive = Result.bRecent && Result.Duration + UE_SMALL_NUMBER >= S.Confirmation &&
			Result.Distance >= S.TravelDistance && FMath::Abs(Result.Sweep) >= S.HeadingSweep &&
			Result.Consistency >= S.DirectionConsistency;
		return Result;
	}
private:
	struct FSample { double Time = 0; float Travel = 0, Sweep = 0, AbsoluteSweep = 0, IntentSweep = 0; };
	void Seed(const FInput& I) { bHasSample = true; Last = I; Add(I.Now); }
	void Add(double T)
	{
		Samples[Next] = {T, Travel, Sweep, AbsoluteSweep, IntentSweep};
		Next = (Next + 1) % UE_ARRAY_COUNT(Samples); Count = FMath::Min(Count + 1, int32(UE_ARRAY_COUNT(Samples)));
	}
	FSample HistoryAt(double T) const
	{
		const FSample* Before = nullptr; const FSample* After = nullptr;
		for (int32 N = 0; N < Count; ++N)
		{
			const auto& P = Samples[N];
			if (P.Time <= T && (!Before || P.Time > Before->Time)) Before = &P;
			if (P.Time >= T && (!After || P.Time < After->Time)) After = &P;
		}
		if (Before && After && After->Time > Before->Time)
		{
			const float A = float((T - Before->Time) / (After->Time - Before->Time));
			return {T, FMath::Lerp(Before->Travel, After->Travel, A), FMath::Lerp(Before->Sweep, After->Sweep, A),
				FMath::Lerp(Before->AbsoluteSweep, After->AbsoluteSweep, A), FMath::Lerp(Before->IntentSweep, After->IntentSweep, A)};
		}
		return Before ? *Before : After ? *After : FSample{};
	}
	FSample Samples[64];
	int32 Count = 0, Next = 0;
	bool bHasSample = false;
	FInput Last;
	FResult Result;
	float Travel = 0, Sweep = 0, AbsoluteSweep = 0, IntentSweep = 0;
	double Started = -1, LastTurn = -1, LastIntentTurn = -1;
};
