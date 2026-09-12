#pragma once
#include "CoreMinimal.h"

namespace ProjectJ::MMO
{
enum class EServiceStatus : uint8 { Success, Unavailable, Rejected, Conflict, Overloaded, Cancelled, UnknownOutcome };
struct FServiceRequest
{
    FGuid RequestId;
    FGuid IdempotencyKey;
    FName Service;
    FName Operation;
    int32 SchemaVersion = 1;
    TArray<uint8> Payload;
};
struct FServiceResponse
{
    FGuid RequestId;
    EServiceStatus Status = EServiceStatus::Unavailable;
    TArray<uint8> Payload;
};
/** Native port for future domain modules. No JSON/HTTP/UObject dependency.
 * Submit must be nonblocking with bounded admission and owned request data.
 * Completion thread, cancellation acknowledgement and deadline behavior must be
 * documented by the adapter. Network timeout is UnknownOutcome for mutations:
 * retry/query the same idempotency key; never assume cancellation undid a commit.
 */
class IServicePort
{
public:
    virtual ~IServicePort() = default;
    virtual void Submit(FServiceRequest Request, TFunction<void(FServiceResponse)> Completion) = 0;
    virtual void Cancel(const FGuid& RequestId) = 0;
};
}
