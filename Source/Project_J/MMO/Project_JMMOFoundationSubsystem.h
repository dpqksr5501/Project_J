#pragma once
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "MMO/FeatureCatalog.h"
#include "Project_JMMOFoundationSubsystem.generated.h"

/** Composition-root access to extension metadata. Resolving a plan does not
 * instantiate a service, grant permissions, connect to a backend or enable content.
 */
UCLASS()
class PROJECT_J_API UProject_JMMOFoundationSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()
public:
    void Initialize(FSubsystemCollectionBase& Collection) override;
    UFUNCTION(BlueprintCallable, Category = "MMO|Architecture")
    bool ResolveContentDependencies(const TArray<FName>& Requested, TArray<FName>& OutOrder, FString& Error) const;
    const ProjectJ::MMO::FFeatureCatalog& GetCatalog() const { return Catalog; }
private:
    ProjectJ::MMO::FFeatureCatalog Catalog;
};
