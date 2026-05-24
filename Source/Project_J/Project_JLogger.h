#pragma once

#include "CoreMinimal.h"
#include "Engine/Engine.h"
#include "Backend/Project_JGatewaySubsystem.h"

DECLARE_LOG_CATEGORY_EXTERN(LogProject_JRemote, Log, All);

/**
 * Custom Macro for Remote Logging.
 * It behaves like UE_LOG but also captures Fatal/Error severity logs and sends them to the backend asynchronously.
 */
#define UE_LOG_REMOTE(CategoryName, Verbosity, Format, ...) \
{ \
	UE_LOG(CategoryName, Verbosity, Format, ##__VA_ARGS__); \
	if (Verbosity == ELogVerbosity::Error || Verbosity == ELogVerbosity::Fatal) \
	{ \
		if (GEngine) \
		{ \
			if (UWorld* World = GEngine->GetWorldContexts()[0].World()) \
			{ \
				if (UProject_JGatewaySubsystem* Gateway = World->GetGameInstance()->GetSubsystem<UProject_JGatewaySubsystem>()) \
				{ \
					FString LogMsg = FString::Printf(Format, ##__VA_ARGS__); \
					Gateway->EnqueueRemoteLog(LogMsg, TEXT(#Verbosity)); \
				} \
			} \
		} \
	} \
}
