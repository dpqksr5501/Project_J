#include "Project_JMMOTypes.h"

FProject_JAccountId FProject_JAccountId::NewId()
{
	FProject_JAccountId NewAccountId;
	NewAccountId.Value = FGuid::NewGuid();
	return NewAccountId;
}

FProject_JCharacterId FProject_JCharacterId::NewId()
{
	FProject_JCharacterId NewCharacterId;
	NewCharacterId.Value = FGuid::NewGuid();
	return NewCharacterId;
}

FString FProject_JWorldInstanceId::ToDebugString() const
{
	return FString::Printf(
		TEXT("World=%s Zone=%s Instance=%s Channel=%s"),
		*WorldId.ToString(),
		*ZoneId.ToString(),
		*InstanceId.ToString(),
		*ChannelId.ToString());
}
