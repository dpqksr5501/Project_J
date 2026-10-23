#pragma once
#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
class UProject_JMeshLODSettings;

namespace ProjectJ::MeshLODAuthoring
{
    enum class ERole : uint8 { Unclassified, Surface, Hair };
    enum class EStatus : uint8 { Existing, Missing, Review, Invalid };
    struct FInspection
    {
        EStatus Status=EStatus::Invalid;
        int32 LODs=0;
        FString Reason;
        bool bExplicitNativeWorkflow=false;
    };
    const TCHAR* DefaultPolicyPath();
    UProject_JMeshLODSettings* LoadPolicy(const FString& Path=FString());
    bool ValidatePolicy(const UProject_JMeshLODSettings& Policy,FString& Error);
    FInspection Inspect(UObject* Asset,const UProject_JMeshLODSettings& Policy,ERole Role);
    /** Only mutates a transient duplicate; the source remains unchanged even on failure. */
    TStrongObjectPtr<UObject> Preview(UObject* Asset,const UProject_JMeshLODSettings& Policy,ERole Role,FString& Error);
    /** Native generation plus preservation check. Only scratch objects or an isolated commandlet can enter. Never saves. */
    bool Generate(UObject* Asset,const UProject_JMeshLODSettings& Policy,ERole Role,FString& Error);
    FString ProtectedFingerprint(UObject* Asset);
    bool ValidateGenerated(UObject* Asset,const UProject_JMeshLODSettings& Policy,FString& Error);
    void RegisterMenus();
}
