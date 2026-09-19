#include "Backend/Project_JHandoverTransport.h"

bool FProject_JLoopbackHandoverTransport::Send(
	const FProject_JHandoverTransportRequest& Request,
	FProject_JHandoverTransportCallback Completion)
{
	FProject_JHandoverTransportResponse Response;
	Response.TransferId = Request.Envelope.TransferId;
	Response.Attempt = Request.Attempt;
	Response.Outcome = ResponseOutcome;
	Response.Message = ResponseOutcome == EProject_JHandoverTransportOutcome::Accepted
		? TEXT("Loopback accepted")
		: TEXT("Loopback simulated failure");

	if (ResponseDelaySeconds <= 0.0f)
	{
		if (!bDropResponses && Completion)
		{
			Completion(Response);
		}
		return true;
	}

	if (!bDropResponses && Completion)
	{
		FPendingResponse Pending;
		Pending.Response = Response;
		Pending.Completion = MoveTemp(Completion);
		Pending.RemainingDelay = ResponseDelaySeconds;
		PendingResponses.Add(MoveTemp(Pending));
	}

	return true;
}

void FProject_JLoopbackHandoverTransport::Cancel(const FGuid& TransferId)
{
	PendingResponses.RemoveAll([TransferId](const FPendingResponse& Pending)
	{
		return Pending.Response.TransferId == TransferId;
	});
}

void FProject_JLoopbackHandoverTransport::Tick(float DeltaSeconds)
{
	if (PendingResponses.IsEmpty() || DeltaSeconds <= 0.0f)
	{
		return;
	}

	// Snapshot identities only: callbacks can cancel another ready response or
	// enqueue a new response. New work starts accumulating time on the next tick.
	struct FReadyResponse { FGuid TransferId; int32 Attempt; };
	TArray<FReadyResponse, TInlineAllocator<16>> Ready;
	for (FPendingResponse& Pending : PendingResponses)
	{
		Pending.RemainingDelay -= DeltaSeconds;
		if (Pending.RemainingDelay <= 0.0f)
		{
			Ready.Add({Pending.Response.TransferId, Pending.Response.Attempt});
		}
	}
	for (const FReadyResponse& Entry : Ready)
	{
		const int32 Index = PendingResponses.IndexOfByPredicate([&Entry](const FPendingResponse& Pending)
		{
			return Pending.Response.TransferId == Entry.TransferId && Pending.Response.Attempt == Entry.Attempt
				&& Pending.RemainingDelay <= 0.0f;
		});
		if (Index == INDEX_NONE) { continue; }
		FPendingResponse Pending = MoveTemp(PendingResponses[Index]);
		PendingResponses.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		if (Pending.Completion) { Pending.Completion(Pending.Response); }
	}
}
