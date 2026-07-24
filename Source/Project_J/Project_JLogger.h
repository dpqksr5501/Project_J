#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Backend/Project_JGatewaySubsystem.h"

DECLARE_LOG_CATEGORY_EXTERN(LogProject_JRemote, Log, All);

/**
 * Custom Macro for Remote Logging.
 * It behaves like UE_LOG and can enqueue Error logs only when explicitly enabled
 * on an authoritative world. Fatal logs are intentionally excluded: process
 * termination is not a reliable time to schedule an asynchronous request.
 */
#define UE_LOG_REMOTE(CategoryName, Verbosity, Format, ...) \
{ \
	UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
	if (Verbosity == ELogVerbosity::Error) \
	{ \
		if (GEngine && GEngine->GetWorldContexts().Num() > 0) \
		{ \
			for (const FWorldContext& Context : GEngine->GetWorldContexts()) \
			{ \
				if (UWorld* World = Context.World(); World && World->GetGameInstance() && World->GetNetMode() != NM_Client) \
				{ \
					if (UProject_JGatewaySubsystem* Gateway = World->GetGameInstance()->GetSubsystem<UProject_JGatewaySubsystem>(); Gateway && Gateway->IsRemoteTelemetryEnabled()) \
					{ \
						FString LogMsg = FString::Printf(Format, ##__VA_ARGS__); \
						Gateway->EnqueueRemoteLog(LogMsg, TEXT(#Verbosity)); \
					} \
					break; \
				} \
			} \
		} \
	} \
}
