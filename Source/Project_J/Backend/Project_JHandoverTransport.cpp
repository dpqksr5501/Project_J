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

	for (int32 Index = PendingResponses.Num() - 1; Index >= 0; --Index)
	{
		FPendingResponse& Pending = PendingResponses[Index];
		Pending.RemainingDelay -= DeltaSeconds;
		if (Pending.RemainingDelay <= 0.0f)
		{
			if (Pending.Completion)
			{
				Pending.Completion(Pending.Response);
			}
			PendingResponses.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		}
	}
}
