#include "Validation/Project_JCrossAssetEditorValidator.h"
#include "AI/Project_JThreatComponent.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Engine/StaticMesh.h"
#include "FoliageInstancedStaticMeshComponent.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "Misc/DataValidation.h"
#include "Project_JNPCCharacter.h"
#include "Project_JWorldAuthoringLibrary.h"
#include "RHIGlobals.h"
#include "Validation/Project_JCrossAssetValidator.h"
#include "WorldPartition/HLOD/HLODLayer.h"
#include "WorldPartition/HLOD/HLODActor.h"
#include "WorldPartition/IWorldPartitionEditorModule.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/RuntimeHashSet/WorldPartitionRuntimeHashSet.h"
#include "World/Project_JDistantMeshPolicy.h"

namespace
{
bool IsPartitionedAuthoringWorld(const UWorld* World)
{
	const UWorldPartition* Partition = World && !World->IsGameWorld() ? World->GetWorldPartition() : nullptr;
	return Partition && (!Partition->IsInitialized() || UProject_JWorldAuthoringLibrary::IsRegionStreamingEnabled(World));
}

bool IsAuthoredForestActor(const AActor* Actor)
{
	static const FName ForestTag(TEXT("ProjectJ.Starter.WorldForest.v1"));
	static const FName NaturalDiversityTag(TEXT("ProjectJ.Starter.NaturalDiversity.v1"));
	return IsValid(Actor) && (Actor->ActorHasTag(ForestTag) || Actor->ActorHasTag(NaturalDiversityTag));
}

bool IsDistantMeshCandidate(const AActor* Actor)
{
	if (!IsValid(Actor) || Actor->IsA<AWorldPartitionHLOD>() || Actor->IsEditorOnly()) return false;
	if (IsAuthoredForestActor(Actor)) return true;
	for (const auto* Component : TInlineComponentArray<UStaticMeshComponent*>(Actor))
	{
		UStaticMesh* Mesh = Component->GetStaticMesh();
		if (!Component->IsEditorOnly() && Mesh && (Component->IsA<UFoliageInstancedStaticMeshComponent>()
			|| Mesh->GetAssetUserDataOfClass(UProject_JDistantMeshPolicy::StaticClass()))) return true;
	}
	return false;
}

TOptional<int32> GetActiveWPODisableDistance(const UStaticMeshComponent& Component, EShaderPlatform ShaderPlatform)
{
	if (Component.bEvaluateWorldPositionOffset && ShaderPlatform != SP_NumPlatforms)
	{
		for (int32 MaterialIndex = 0; MaterialIndex < Component.GetNumMaterials(); ++MaterialIndex)
		{
			const UMaterialInterface* Material = Component.GetMaterial(MaterialIndex);
			if (Material && Material->IsUsingWorldPositionOffset_Concurrent(ShaderPlatform))
			{
				return Component.GetWorldPositionOffsetDisableDistance();
			}
		}
	}
	return {};
}

UClass* ResolveMountMoverPawnClass()
{
	return FindObject<UClass>(nullptr, TEXT("/Script/Project_JMount.Project_JMountMoverPawn"));
}

bool AnimBlueprintUsesMotionMatching(const UAnimBlueprint* AnimBP)
{
	if (!AnimBP) return false;
	if (AnimBP->ParentClass && AnimBP->ParentClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass()))
	{
		return true;
	}
	TArray<UEdGraph*> AllGraphs;
	AnimBP->GetAllGraphs(AllGraphs);
	for (const UEdGraph* Graph : AllGraphs)
	{
		if (!Graph) continue;
		for (const UEdGraphNode* Node : Graph->Nodes)
		{
			if (!Node) continue;
			const FString ClassName = Node->GetClass()->GetName();
			if (ClassName.Contains(TEXT("PoseSearch")) || ClassName.Contains(TEXT("MotionMatching")) || ClassName.Contains(TEXT("Chooser")))
			{
				return true;
			}
		}
	}
	return false;
}
}

bool UProject_JCrossAssetEditorValidator::CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext& Context) const
{
	if (const AActor* Actor = Cast<AActor>(Object))
	{
		return IsPartitionedAuthoringWorld(Actor->GetWorld()) && IsDistantMeshCandidate(Actor);
	}
	if (const UWorld* World = Cast<UWorld>(Object); IsPartitionedAuthoringWorld(World))
	{
		const TConstArrayView<FAssetData> ExternalObjects = Context.GetAssociatedExternalObjects();
		if (!ExternalObjects.IsEmpty())
		{
			for (const FAssetData& ExternalObject : ExternalObjects)
			{
				UObject* External = ExternalObject.FastGetAsset(false);
				const AActor* Actor = Cast<AActor>(External);
				if (!External || (IsDistantMeshCandidate(Actor) && Actor->GetWorld() == World))
				{
					return true;
				}
			}
			return false;
		}
		for (const ULevel* Level : World->GetLevels())
		{
			if (Level && Level->Actors.ContainsByPredicate([](const AActor* Actor) { return IsDistantMeshCandidate(Actor); }))
			{
				return true;
			}
		}
		return false;
	}
	if (Cast<UProject_JNPCBrainDefinition>(Object))
	{
		return true;
	}
	if (const UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Object))
	{
		const FString Path = AssetData.PackageName.ToString();
		return Path.Contains(TEXT("/NPC")) || Path.Contains(TEXT("NPC_"));
	}
	if (const UBlueprint* BP = Cast<UBlueprint>(Object))
	{
		if (UClass* GenClass = BP->GeneratedClass)
		{
			UClass* MountMoverClass = ResolveMountMoverPawnClass();
			return (MountMoverClass && GenClass->IsChildOf(MountMoverClass))
				|| GenClass->IsChildOf(AProject_JNPCCharacter::StaticClass());
		}
	}
	return false;
}

EDataValidationResult UProject_JCrossAssetEditorValidator::ValidateLoadedAsset_Implementation(const FAssetData&, UObject* Object, FDataValidationContext& Context)
{
	const AActor* ForestActor = Cast<AActor>(Object);
	const UWorld* World = ForestActor ? ForestActor->GetWorld() : Cast<UWorld>(Object);
	if (IsPartitionedAuthoringWorld(World))
	{
		if (!World->GetWorldPartition()->IsInitialized())
		{
			AssetWarning(Object, FText::FromString(World->GetPathName() + TEXT(": open the owning map to verify streaming and distant-mesh placement.")));
			AssetPasses(Object);
			return EDataValidationResult::Valid;
		}
		bool bInvalid = false;
		const EShaderPlatform ShaderPlatform = GetFeatureLevelShaderPlatform(World->GetFeatureLevel());
		const auto* RuntimeHash = Cast<UWorldPartitionRuntimeHashSet>(World->GetWorldPartition()->RuntimeHash);
		const bool bRequireLayerRegistration = IWorldPartitionEditorModule::Get().GetRequireExplicitHLODLayerPartitionAssignation();
		const auto ValidateForestActor = [this, Object, World, ShaderPlatform, RuntimeHash, bRequireLayerRegistration, &bInvalid](const AActor* Actor)
		{
			if (!IsValid(Actor) || Actor->GetWorld() != World || Actor->IsA<AWorldPartitionHLOD>() || Actor->IsEditorOnly()) return;
			const bool bLegacyForest = IsAuthoredForestActor(Actor);
			TOptional<bool> OwnerParticipates;
			const UHLODLayer* Layer = Actor->GetHLODLayer();
			const bool bRootContainer = Actor->GetLevel() == World->PersistentLevel && !Actor->IsInLevelInstance();
			if (!Layer && bRootContainer) Layer = World->GetWorldPartition()->GetDefaultHLODLayer();
			for (const auto* Component : TInlineComponentArray<UStaticMeshComponent*>(Actor))
			{
				UStaticMesh* Mesh = Component->GetStaticMesh();
				if (!Mesh || Component->IsEditorOnly()) continue;
				const auto* Policy = Cast<UProject_JDistantMeshPolicy>(Mesh->GetAssetUserDataOfClass(UProject_JDistantMeshPolicy::StaticClass()));
				const bool bLegacyComponent = bLegacyForest && Component->IsA<UInstancedStaticMeshComponent>();
				if (!Policy && !bLegacyComponent && !Component->IsA<UFoliageInstancedStaticMeshComponent>()) continue;
				static const FName NearOnly(TEXT("ProjectJ.World.NearOnly"));
				if (Component->ComponentHasTag(NearOnly) || (Policy && Policy->Role == EProject_JDistantMeshRole::NearOnly)) continue;
				const FString Path = Component->GetPathName();
				if (!Policy && !bLegacyComponent)
				{
					AssetWarning(Object, FText::FromString(Path + TEXT(": classify the foliage mesh with Project J Distant Mesh Policy or mark this placement NearOnly.")));
					continue;
				}
				if (!OwnerParticipates.IsSet()) OwnerParticipates = Actor->GetIsSpatiallyLoaded() && Actor->IsHLODRelevant();
				if (!OwnerParticipates.GetValue() || !Component->IsHLODRelevant())
				{
					AssetFails(Object, FText::FromString(Path + TEXT(": distant representation requires spatial loading and actor/component HLOD participation.")));
					bInvalid = true;
					continue;
				}
				if (!Layer && !bRootContainer)
				{
					AssetWarning(Object, FText::FromString(Path + TEXT(": inherited container HLOD layer requires streaming-generation validation.")));
					continue;
				}
				const bool bCanopy = !Policy || Policy->Role == EProject_JDistantMeshRole::InstancedCanopy;
				if (!Layer || (bCanopy && Layer->GetLayerType() != EHLODLayerType::Instancing))
				{
					AssetFails(Object, FText::FromString(Path + (bCanopy ? TEXT(": assign an effective Instancing HLOD layer.") : TEXT(": assign a distant HLOD layer."))));
					bInvalid = true;
				}
				else if (bRootContainer && bRequireLayerRegistration && RuntimeHash && !RuntimeHash->IsValidHLODLayer(Actor->GetRuntimeGrid(), FSoftObjectPath(Layer)))
				{
					const FName Grid = Actor->GetRuntimeGrid();
					AssetFails(Object, FText::FromString(FString::Printf(TEXT("%s: HLOD layer '%s' is not registered for runtime grid '%s'. Check the world's HLOD Partitions."),
						*Path, *Layer->GetPathName(), Grid.IsNone() ? TEXT("<default>") : *Grid.ToString())));
					bInvalid = true;
				}
				if (bCanopy)
				{
					const auto Result = Project_J::EditorValidation::ValidateCanopyWindDistance(Path, GetActiveWPODisableDistance(*Component, ShaderPlatform));
					for (const FString& Message : Result.Errors) AssetWarning(Object, FText::FromString(Message));
				}
			}
		};
		if (ForestActor)
		{
			ValidateForestActor(ForestActor);
		}
		else if (const TConstArrayView<FAssetData> ExternalObjects = Context.GetAssociatedExternalObjects(); !ExternalObjects.IsEmpty())
		{
			// OFPA saves are validated through the owning map with only the saved actors in the context.
			for (const FAssetData& ExternalObject : ExternalObjects)
			{
				if (UObject* External = ExternalObject.FastGetAsset(false)) ValidateForestActor(Cast<AActor>(External));
				else AssetWarning(Object, FText::FromString(ExternalObject.PackageName.ToString() + TEXT(": external object unavailable for distant-mesh validation.")));
			}
		}
		else
		{
			for (const ULevel* Level : World->GetLevels())
			{
				if (Level)
				{
					for (const AActor* Actor : Level->Actors)
					{
						ValidateForestActor(Actor);
					}
				}
			}
		}
		if (bInvalid) return EDataValidationResult::Invalid;
		AssetPasses(Object);
		return EDataValidationResult::Valid;
	}

	if (const UProject_JNPCBrainDefinition* Brain = Cast<UProject_JNPCBrainDefinition>(Object))
	{
		if (!FMath::IsFinite(Brain->SightRadiusCm) || !FMath::IsFinite(Brain->LeashRadiusCm)
			|| !FMath::IsFinite(Brain->MaxChaseZDeltaCm) || !FMath::IsFinite(Brain->UnreachableEvadeSeconds)
			|| Brain->SightRadiusCm <= 0.0f || Brain->LeashRadiusCm < Brain->SightRadiusCm * 0.5f
			|| Brain->MaxChaseZDeltaCm <= 0.0f || Brain->UnreachableEvadeSeconds <= 0.0f)
		{
			AssetFails(Object, NSLOCTEXT("ProjectJCrossAssetValidator", "InvalidBrain",
				"NPC sensing and evade values must be finite. SightRadiusCm, MaxChaseZDeltaCm and UnreachableEvadeSeconds must be positive. LeashRadiusCm must be at least half of SightRadiusCm."));
			return EDataValidationResult::Invalid;
		}
		AssetPasses(Object);
		return EDataValidationResult::Valid;
	}

	if (const UBlueprint* BP = Cast<UBlueprint>(Object))
	{
		UClass* GenClass = BP->GeneratedClass;
		UClass* MountMoverClass = ResolveMountMoverPawnClass();
		if (GenClass && MountMoverClass && GenClass->IsChildOf(MountMoverClass))
		{
			if (const AActor* CDO = Cast<AActor>(GenClass->GetDefaultObject()))
			{
				if (CDO->IsDataValid(Context) == EDataValidationResult::Invalid)
				{
					AssetFails(Object, NSLOCTEXT("ProjectJCrossAssetValidator", "InvalidMountBP",
						"MountMoverPawn Blueprint failed IsDataValid checks (verify MaxHealth, MountInteractionDistance, and sockets)."));
					return EDataValidationResult::Invalid;
				}
				if (const USkeletalMeshComponent* Mesh = CDO->FindComponentByClass<USkeletalMeshComponent>();
					Mesh && Mesh->GetSkeletalMeshAsset() != nullptr)
				{
					if (!Mesh->DoesSocketExist(TEXT("Reins_L")) || !Mesh->DoesSocketExist(TEXT("Reins_R")))
					{
						AssetWarning(Object, NSLOCTEXT("ProjectJCrossAssetValidator", "MissingReinsSockets",
							"MountMoverPawn Mesh is missing Reins_L / Reins_R sockets; rider reins Hand IK will fall back to idle mount pose."));
					}
				}
			}
			AssetPasses(Object);
			return EDataValidationResult::Valid;
		}
		if (GenClass && GenClass->IsChildOf(AProject_JNPCCharacter::StaticClass()))
		{
			const auto* CDO = Cast<AProject_JNPCCharacter>(GenClass->GetDefaultObject());
			if (CDO)
			{
				// Enforce NPC non-Motion-Matching policy regardless of asset folder/name.
				if (const USkeletalMeshComponent* Mesh = CDO->GetMesh(); Mesh && Mesh->AnimClass)
				{
					UClass* AnimClass = Mesh->AnimClass;
					const UAnimBlueprint* AssignedAnimBP = Cast<UAnimBlueprint>(AnimClass->ClassGeneratedBy);
					const bool bViolates = AnimClass->IsChildOf(UProject_JPlayerAnimInstance::StaticClass())
						|| AnimBlueprintUsesMotionMatching(AssignedAnimBP);
					const auto Res = Project_J::EditorValidation::ValidateNPCAnimPolicy(BP->GetFName(), bViolates);
					if (!Res.bPassed)
					{
						for (const FString& Err : Res.Errors)
						{
							AssetFails(Object, FText::FromString(Err));
						}
						return EDataValidationResult::Invalid;
					}
				}
			}
			if (CDO && CDO->BrainDefinition && CDO->BrainDefinition->bIsGiantBoss)
			{
				bool bHasMultiPart = CDO->FindComponentByClass<UProject_JMultiPartHitComponent>() != nullptr;
				if (!bHasMultiPart && BP->SimpleConstructionScript)
				{
					for (const USCS_Node* Node : BP->SimpleConstructionScript->GetAllNodes())
					{
						if (Node && Node->ComponentClass && Node->ComponentClass->IsChildOf(UProject_JMultiPartHitComponent::StaticClass()))
						{
							bHasMultiPart = true;
							break;
						}
					}
				}
				if (!bHasMultiPart)
				{
					AssetWarning(Object, NSLOCTEXT("ProjectJCrossAssetValidator", "GiantBossMissingMultiPart",
						"Giant Boss NPC Blueprint (BrainDefinition.bIsGiantBoss=true) has no UProject_JMultiPartHitComponent attached; attacks will use capsule fallback only."));
				}
			}
			AssetPasses(Object);
			return EDataValidationResult::Valid;
		}
	}

	if (const UAnimBlueprint* AnimBP = Cast<UAnimBlueprint>(Object))
	{
		const bool bFoundMotionMatchingNode = AnimBlueprintUsesMotionMatching(AnimBP);
		const auto Res = Project_J::EditorValidation::ValidateNPCAnimPolicy(AnimBP->GetFName(), bFoundMotionMatchingNode);
		if (!Res.bPassed)
		{
			for (const FString& Err : Res.Errors)
			{
				AssetFails(Object, FText::FromString(Err));
			}
			return EDataValidationResult::Invalid;
		}
		AssetPasses(Object);
		return EDataValidationResult::Valid;
	}

	AssetPasses(Object);
	return EDataValidationResult::Valid;
}
