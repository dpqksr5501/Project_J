#include "Validation/Project_JCrossAssetEditorValidator.h"
#include "AI/Project_JThreatComponent.h"
#include "Animation/AnimBlueprint.h"
#include "Animation/Project_JPlayerAnimInstance.h"
#include "Combat/Project_JMultiPartHitComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "Engine/Blueprint.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Misc/DataValidation.h"
#include "Project_JNPCCharacter.h"
#include "Validation/Project_JCrossAssetValidator.h"

namespace
{
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

bool UProject_JCrossAssetEditorValidator::CanValidateAsset_Implementation(const FAssetData& AssetData, UObject* Object, FDataValidationContext&) const
{
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
	if (const UProject_JNPCBrainDefinition* Brain = Cast<UProject_JNPCBrainDefinition>(Object))
	{
		if (Brain->SightRadiusCm <= 0.0f || Brain->LeashRadiusCm < Brain->SightRadiusCm * 0.5f || Brain->MaxChaseZDeltaCm <= 0.0f)
		{
			AssetFails(Object, NSLOCTEXT("ProjectJCrossAssetValidator", "InvalidBrain",
				"UProject_JNPCBrainDefinition requires positive SightRadiusCm/MaxChaseZDeltaCm and LeashRadiusCm >= 0.5 * SightRadiusCm."));
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
