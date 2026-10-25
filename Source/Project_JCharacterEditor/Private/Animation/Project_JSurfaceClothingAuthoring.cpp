#include "Animation/Project_JPlayerLocomotionLibrary.h"
#include "ClothingAssetFactory.h"
#include "ClothingAsset.h"
#include "ClothingSimulationFactory.h"
#include "SkeletalMeshClothingSystemUtilities.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimBlueprint.h"
#include "AnimGraphNode_RigidBody.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "ScopedTransaction.h"
#include "Editor.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

bool UProject_JPlayerLocomotionLibrary::CopySurfaceClothingConfiguration(USkeletalMesh* Previous, USkeletalMesh* Target)
{
	if ((GEditor && GEditor->PlayWorld) || !Previous || !Target || Previous == Target) return false;
	const auto& From = Previous->GetMeshClothingAssets();
	const auto& To = Target->GetMeshClothingAssets();
	if (From.IsEmpty() || From.Num() != To.Num()) return false;
	for (int32 Index = 0; Index < From.Num(); ++Index)
	{
		const auto* A = Cast<UClothingAssetCommon>(From[Index]);
		const auto* B = Cast<UClothingAssetCommon>(To[Index]);
		if (!A || !B || B->GetOuter() != Target || A->ClothConfigs.IsEmpty() || A->ClothConfigs.Num() != B->ClothConfigs.Num()) return false;
		for (const auto& Pair : A->ClothConfigs)
		{
			const auto* Existing = B->ClothConfigs.Find(Pair.Key);
			if (!Pair.Value || !Existing || !*Existing || Pair.Value->GetClass() != (*Existing)->GetClass()) return false;
		}
	}
	const FScopedTransaction Transaction(NSLOCTEXT("ProjectJ", "CopyClothConfiguration", "Copy surface cloth configuration"));
	Target->Modify();
	TMap<const UClothConfigBase*, UClothConfigBase*> Copies;
	for (int32 Index = 0; Index < From.Num(); ++Index)
	{
		const auto* A = CastChecked<UClothingAssetCommon>(From[Index]);
		auto* B = CastChecked<UClothingAssetCommon>(To[Index]); B->Modify();
		for (const auto& Pair : A->ClothConfigs)
		{
			auto*& Copy = Copies.FindOrAdd(Pair.Value.Get());
			if (!Copy) Copy = DuplicateObject<UClothConfigBase>(Pair.Value.Get(), B,
				MakeUniqueObjectName(B, Pair.Value->GetClass(), Pair.Value->GetFName()));
			if (!Copy) return false;
			B->ClothConfigs[Pair.Key] = Copy;
		}
	}
	Target->MarkPackageDirty();
	return true;
}

FString UProject_JPlayerLocomotionLibrary::CreateSurfaceClothing(USkeletalMesh* Mesh, UPhysicsAsset* Collisions,
	float PinnedTopDepth, float TransitionDepth, float MaxDistance)
{
	if (!Mesh || !Collisions || (GEditor && GEditor->PlayWorld) || !Mesh->GetMeshClothingAssets().IsEmpty()
		|| PinnedTopDepth <= 0.f || TransitionDepth <= 0.f || MaxDistance <= 0.f) return TEXT("{}");
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (!Model || Model->LODModels.Num() != 1) return TEXT("{}");
	const int32 SectionCount = Model->LODModels[0].Sections.Num();
	for (const FSkelMeshSection& Section : Model->LODModels[0].Sections)
		if (Section.HasClothingData() || Section.SoftVertices.IsEmpty()) return TEXT("{}");
	const FScopedTransaction Transaction(NSLOCTEXT("ProjectJ", "CreateSurfaceClothing", "Create surface clothing"));
	Mesh->Modify();
	UClothingAssetFactory* Factory = NewObject<UClothingAssetFactory>();
	TArray<UClothingAssetCommon*> Created;
	TArray<TSharedPtr<FJsonValue>> Reports;
	bool bSuccess = true;
	for (int32 SectionIndex = 0; SectionIndex < SectionCount; ++SectionIndex)
	{
		FSkeletalMeshClothBuildParams Params;
		Params.AssetName = FString::Printf(TEXT("SurfaceCloth_Section%d"), SectionIndex);
		Params.LodIndex = 0;
		Params.SourceSection = SectionIndex;
		Params.bRemoveFromMesh = false;
		Params.PhysicsAsset = Collisions;
		UClothingAssetCommon* Asset = Cast<UClothingAssetCommon>(Factory->CreateFromSkeletalMesh(Mesh, Params));
		if (!Asset) { bSuccess = false; break; }
		Created.Add(Asset);
		Mesh->AddClothingAsset(Asset);
		FClothLODDataCommon& Lod = Asset->LodData[0];
		const auto& Physical = Lod.PhysicalMeshData;
		// The factory welds render seams. Pin each connected panel separately so small
		// disconnected decorations do not become unanchored free bodies.
		TArray<TArray<int32>> Neighbors;
		Neighbors.SetNum(Physical.Vertices.Num());
		for (int32 I = 0; I + 2 < Physical.Indices.Num(); I += 3)
			for (int32 E = 0; E < 3; ++E)
			{
				const int32 A = Physical.Indices[I + E], B = Physical.Indices[I + (E + 1) % 3];
				Neighbors[A].AddUnique(B); Neighbors[B].AddUnique(A);
			}
		FPointWeightMap& Mask = Lod.PointWeightMaps[0];
		Mask.Name = TEXT("PinnedTop_MaxDistance");
		Mask.CurrentTarget = static_cast<uint8>(EWeightMapTargetCommon::MaxDistance);
		Mask.bEnabled = true;
		TBitArray<> Visited(false, Physical.Vertices.Num());
		int32 Pinned = 0, Panels = 0;
		for (int32 Seed = 0; Seed < Physical.Vertices.Num(); ++Seed)
		{
			if (Visited[Seed]) continue;
			++Panels;
			TArray<int32> Panel{Seed}; Visited[Seed] = true;
			float Top = Physical.Vertices[Seed].Z;
			for (int32 Cursor = 0; Cursor < Panel.Num(); ++Cursor)
			{
				const int32 Vertex = Panel[Cursor];
				Top = FMath::Max(Top, Physical.Vertices[Vertex].Z);
				for (int32 Neighbor : Neighbors[Vertex])
					if (!Visited[Neighbor]) { Visited[Neighbor] = true; Panel.Add(Neighbor); }
			}
			for (int32 Vertex : Panel)
			{
				const float Alpha = FMath::Clamp((Top - PinnedTopDepth - Physical.Vertices[Vertex].Z) / TransitionDepth, 0.f, 1.f);
				Mask.Values[Vertex] = MaxDistance * Alpha * Alpha * (3.f - 2.f * Alpha);
				Pinned += Alpha == 0.f;
			}
		}
		Asset->ApplyParameterMasks();
		if (!FSkeletalMeshClothingSystemUtilities::AssignClothingToSection(Mesh, Asset, 0, SectionIndex, 0, nullptr)) { bSuccess = false; break; }
		auto Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("asset"), Asset->GetPathName());
		Report->SetNumberField(TEXT("section"), SectionIndex);
		Report->SetNumberField(TEXT("vertices"), Physical.Vertices.Num());
		Report->SetNumberField(TEXT("panels"), Panels);
		Report->SetNumberField(TEXT("pinned"), Pinned);
		Reports.Add(MakeShared<FJsonValueObject>(Report));
	}
	if (!bSuccess)
		for (UClothingAssetCommon* Asset : Created)
		{
			Asset->UnbindFromSkeletalMesh(Mesh, INDEX_NONE, INDEX_NONE);
			Mesh->GetMeshClothingAssets().Remove(Asset);
			Asset->Rename(nullptr, GetTransientPackage());
		}
	Mesh->MarkPackageDirty();
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), bSuccess);
	Result->SetArrayField(TEXT("sections"), Reports);
	FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text)); return Text;
}

bool UProject_JPlayerLocomotionLibrary::RefreshSurfaceClothing(USkeletalMesh* Mesh)
{
	if (!Mesh || (GEditor && GEditor->PlayWorld)) return false;
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	const auto& Assets = Mesh->GetMeshClothingAssets();
	if (!Model || Model->LODModels.Num() != 1 || Assets.IsEmpty() || Model->LODModels[0].Sections.Num() != Assets.Num()) return false;
	for (UClothingAssetBase* Base : Assets)
	{
		const auto* Asset = Cast<UClothingAssetCommon>(Base);
		if (!Asset || Asset->GetOuter() != Mesh || Asset->LodData.Num() != 1) return false;
	}
	const FScopedTransaction Transaction(NSLOCTEXT("ProjectJ", "RefreshSurfaceClothing", "Refresh surface clothing"));
	Mesh->Modify();
	for (int32 Section = 0; Section < Assets.Num(); ++Section)
	{
		auto* Asset = CastChecked<UClothingAssetCommon>(Assets[Section]);
		Asset->Modify(); Asset->InvalidateAllCachedData(); Asset->ApplyParameterMasks();
		if (!FSkeletalMeshClothingSystemUtilities::AssignClothingToSection(Mesh, Asset, 0, Section, 0, nullptr)) return false;
	}
	Mesh->MarkPackageDirty();
	return true;
}

FString UProject_JPlayerLocomotionLibrary::PaintSurfaceClothingHemBending(USkeletalMesh* Mesh, float HemDepth)
{
	if (!Mesh || (GEditor && GEditor->PlayWorld) || !FMath::IsFinite(HemDepth) || HemDepth <= 0.f) return TEXT("{}");
	const auto& Assets = Mesh->GetMeshClothingAssets();
	const auto* Model = Mesh->GetImportedModel();
	if (!Model || Model->LODModels.Num() != 1 || Assets.IsEmpty() || Model->LODModels[0].Sections.Num() != Assets.Num()) return TEXT("{}");
	const FName MaskName(TEXT("LowerHem_Bending"));
	TArray<uint8> Targets;
	for (UClothingAssetBase* Base : Assets)
	{
		const auto* Asset = Cast<UClothingAssetCommon>(Base);
		if (!Asset || Asset->GetOuter() != Mesh || Asset->LodData.Num() != 1) return TEXT("{}");
		const auto* Factory = UClothingSimulationFactory::GetClothingSimulationFactory(Asset);
		const UEnum* Enum = Factory ? Factory->GetWeightMapTargetEnum() : nullptr;
		const int64 Target = Enum ? Enum->GetValueByNameString(TEXT("BendingStiffness")) : INDEX_NONE;
		if (Target < 0 || Target > MAX_uint8) return TEXT("{}");
		const auto& Lod = Asset->LodData[0];
		const auto* MaxDistance = Lod.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
		if (!MaxDistance || MaxDistance->Values.Num() != Lod.PhysicalMeshData.Vertices.Num()) return TEXT("{}");
		for (const FPointWeightMap& Mask : Lod.PointWeightMaps)
			if ((Mask.CurrentTarget == Target && Mask.Name != MaskName) || (Mask.Name == MaskName && Mask.CurrentTarget != Target)) return TEXT("{}");
		Targets.Add(static_cast<uint8>(Target));
	}
	const FScopedTransaction Transaction(NSLOCTEXT("ProjectJ", "PaintSurfaceClothingHem", "Paint clothing hem bending"));
	Mesh->Modify();
	TArray<TSharedPtr<FJsonValue>> Reports;
	for (int32 Section = 0; Section < Assets.Num(); ++Section)
	{
		auto* Asset = CastChecked<UClothingAssetCommon>(Assets[Section]); Asset->Modify();
		auto& Lod = Asset->LodData[0];
		const auto& Physical = Lod.PhysicalMeshData;
		const auto& MaxDistance = Physical.GetWeightMap(EWeightMapTargetCommon::MaxDistance).Values;
		float MinimumZ = TNumericLimits<float>::Max();
		for (int32 I = 0; I < Physical.Vertices.Num(); ++I)
			if (MaxDistance[I] > 0.f) MinimumZ = FMath::Min(MinimumZ, Physical.Vertices[I].Z);
		FPointWeightMap* Mask = Lod.PointWeightMaps.FindByPredicate([&](const FPointWeightMap& Value) { return Value.Name == MaskName; });
		if (!Mask) Mask = &Lod.PointWeightMaps.AddDefaulted_GetRef();
		Mask->Name = MaskName; Mask->CurrentTarget = Targets[Section]; Mask->bEnabled = true;
		Mask->Values.Init(0.f, Physical.Vertices.Num());
		int32 Affected = 0;
		for (int32 I = 0; I < Physical.Vertices.Num(); ++I)
		{
			if (MaxDistance[I] <= 0.f) continue;
			const float Alpha = FMath::Clamp((Physical.Vertices[I].Z - MinimumZ) / HemDepth, 0.f, 1.f);
			Mask->Values[I] = 1.f - Alpha * Alpha * (3.f - 2.f * Alpha);
			Affected += Mask->Values[I] > 0.f;
		}
		auto Report = MakeShared<FJsonObject>();
		Report->SetStringField(TEXT("asset"), Asset->GetPathName());
		Report->SetNumberField(TEXT("vertices"), Physical.Vertices.Num());
		Report->SetNumberField(TEXT("hem_vertices"), Affected);
		Reports.Add(MakeShared<FJsonValueObject>(Report));
	}
	auto Result = MakeShared<FJsonObject>();
	Result->SetBoolField(TEXT("ok"), RefreshSurfaceClothing(Mesh));
	Result->SetNumberField(TEXT("hem_depth_cm"), HemDepth);
	Result->SetArrayField(TEXT("sections"), Reports);
	FString Text; FJsonSerializer::Serialize(Result, TJsonWriterFactory<>::Create(&Text)); return Text;
}

bool UProject_JPlayerLocomotionLibrary::RemoveSecondaryRigidBody(UAnimBlueprint* Blueprint, UPhysicsAsset* PhysicsAsset)
{
	if (!Blueprint || !PhysicsAsset || (GEditor && GEditor->PlayWorld)) return false;
	TArray<UEdGraph*> Graphs; Blueprint->GetAllGraphs(Graphs);
	TArray<UAnimGraphNode_RigidBody*> Matches;
	for (UEdGraph* Graph : Graphs)
		for (UEdGraphNode* Node : Graph->Nodes)
			if (auto* RigidBody = Cast<UAnimGraphNode_RigidBody>(Node); RigidBody && RigidBody->Node.OverridePhysicsAsset == PhysicsAsset)
				Matches.Add(RigidBody);
	if (Matches.Num() != 1) return false;
	auto* Node = Matches[0];
	UEdGraphPin* Input = Node->FindPin(TEXT("ComponentPose"));
	UEdGraphPin* Output = Node->FindPin(TEXT("ComponentPose"), EGPD_Output);
	// The component-space result pin is normally named Pose, unlike its input.
	if (!Output) Output = Node->FindPin(TEXT("Pose"), EGPD_Output);
	if (!Input || Input->LinkedTo.Num() != 1 || !Output || Output->LinkedTo.IsEmpty()) return false;
	const FScopedTransaction Transaction(NSLOCTEXT("ProjectJ", "RemoveSecondaryRigidBody", "Remove replaced RigidBody"));
	Blueprint->Modify(); Node->GetGraph()->Modify();
	UEdGraphPin* Source = Input->LinkedTo[0];
	const auto Consumers = Output->LinkedTo;
	Node->BreakAllNodeLinks();
	for (UEdGraphPin* Consumer : Consumers) Source->MakeLinkTo(Consumer);
	FBlueprintEditorUtils::RemoveNode(Blueprint, Node, true);
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return true;
}
