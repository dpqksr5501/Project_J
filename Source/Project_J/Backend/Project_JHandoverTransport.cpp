#include "Backend/Project_JHandoverTransport.h"

bool FProject_JLoopbackHandoverTransport::Send(
	const FProject_JHandoverTransportRequest& Request,
	FProject_JHandoverTransportCallback Completion)
{
	if (!Request.Envelope.TransferId.IsValid() || !Completion)
	{
		return false;
	}

	if (bDropResponses)
	{
		return true;
	}

	FPendingResponse& Pending = PendingResponses.AddDefaulted_GetRef();
	Pending.Response.TransferId = Request.Envelope.TransferId;
	Pending.Response.Attempt = Request.Attempt;
	Pending.Response.Outcome = ResponseOutcome;
	Pending.Response.Message =
		ResponseOutcome == EProject_JHandoverTransportOutcome::Accepted
			? TEXT("Loopback destination accepted the envelope.")
			: TEXT("Loopback destination rejected the envelope.");
	Pending.Completion = MoveTemp(Completion);
	Pending.RemainingDelay = ResponseDelaySeconds;
	return true;
}

void FProject_JLoopbackHandoverTransport::Cancel(const FGuid& TransferId)
{
	PendingResponses.RemoveAll(
		[&TransferId](const FPendingResponse& Pending)
		{
			return Pending.Response.TransferId == TransferId;
		});
}

void FProject_JLoopbackHandoverTransport::Tick(float DeltaSeconds)
{
	for (int32 Index = PendingResponses.Num() - 1; Index >= 0; --Index)
	{
		FPendingResponse& Pending = PendingResponses[Index];
		Pending.RemainingDelay -= FMath::Max(0.0f, DeltaSeconds);
		if (Pending.RemainingDelay > 0.0f)
		{
			continue;
		}

		FPendingResponse Completed = MoveTemp(Pending);
		PendingResponses.RemoveAtSwap(Index, 1, EAllowShrinking::No);
		if (Completed.Completion)
		{
			Completed.Completion(Completed.Response);
		}
	}
}
