#pragma once
#include "Backend/Project_JLocalProfileSnapshot.h"
#include "Commandlets/Commandlet.h"
#include "Project_JProfileMigration.generated.h"

struct FProject_JMigrationRecord
{
	FString Kind;
	TArray<uint8> Id, Payload;
	int64 Revision = 0;
	int32 Schema = 0, Deleted = 0;
};
struct FProject_JMigrationInspection
{
	FString SourceHash, OtherStateHash, Error;
	int32 DatabaseVersion = 0;
	TArray<FProject_JMigrationRecord> Records;
	TArray<FGuid> MissingOwnership;
	bool IsValid() const
	{
		return Error.IsEmpty();
	}
};
/** Offline worker-only I/O. Never opens a source for writing; never switches a live DB.
 * Ownership is a complete, explicitly supplied v2 snapshot for each v1 character.
 * Caller validates definitions/rules on the game thread before creating a result. */
class FProject_JProfileMigration
{
  public:
	static FProject_JMigrationInspection Inspect(const FString &Source);
	static bool Plan(const FProject_JMigrationInspection &Source,
					 const TArray<FProject_JLocalProfileSnapshot> &Ownership, TArray<FProject_JMigrationRecord> &Result,
					 FString &Error);
	static bool Create(const FString &Source, const FString &Destination,
					   const FProject_JMigrationInspection &Inspection,
					   const TArray<FProject_JMigrationRecord> &Planned, FString &Error);
	static bool RollbackCopy(const FString &Original, const FString &Migrated, const FString &Destination,
							 FString &Error);
#if WITH_DEV_AUTOMATION_TESTS
	static void FailAfterWritesForTest(int32 Count);
#endif
};

UCLASS()
class UProject_JProfileMigrationCommandlet : public UCommandlet
{
	GENERATED_BODY()
  public:
	UProject_JProfileMigrationCommandlet();
	virtual int32 Main(const FString &Params) override;
};
