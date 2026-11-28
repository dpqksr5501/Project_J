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
#include "SkeletalMeshEditorSubsystem.h"
#include "SkinnedAssetCompiler.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

FString UProject_JPlayerLocomotionLibrary::BuildSurfaceClothingPhysicsProxy(USkeletalMesh* TestMesh)
{
	if (!TestMesh || (GEditor && GEditor->PlayWorld) || !TestMesh->GetPathName().StartsWith(TEXT("/Game/Tests/Cloth/")))
		return {};
	USkinnedAsset* AssetsToFinish[] = {TestMesh};
	FSkinnedAssetCompilingManager::Get().FinishCompilation(AssetsToFinish);
	if (TestMesh->IsCompiling() ||
		!TestMesh->GetImportedModel() || TestMesh->GetImportedModel()->LODModels.Num() != 1)
		return {};

	struct FBinding
	{
		UClothingAssetCommon* Asset;
		int32 Section;
		int32 Material;
		int32 Vertices;
	};
	TArray<FBinding> Bindings;
	const auto& BaseModel = TestMesh->GetImportedModel()->LODModels[0];
	const int32 BaseRenderVertices = BaseModel.NumVertices;
	const int32 BaseRenderIndices = BaseModel.IndexBuffer.Num();
	const auto& BaseSections = BaseModel.Sections;
	for (int32 Index = 0; Index < BaseSections.Num(); ++Index)
	{
		const auto& Section = BaseSections[Index];
		if (!Section.HasClothingData()) continue;
		auto* Asset = Cast<UClothingAssetCommon>(TestMesh->GetClothingAsset(Section.ClothingData.AssetGuid));
		if (!Asset || Asset->GetOuter() != TestMesh || Asset->LodData.Num() != 1 ||
			Bindings.ContainsByPredicate([&](const FBinding& Entry) { return Entry.Asset == Asset || Entry.Material == Section.MaterialIndex; }))
			return {};
		Bindings.Add({Asset, Index, Section.MaterialIndex, Asset->LodData[0].PhysicalMeshData.Vertices.Num()});
	}
	if (!Bindings.Num() || Bindings.Num() != TestMesh->GetMeshClothingAssets().Num()) return {};

	TestMesh->Modify();
	if (!USkeletalMeshEditorSubsystem::RegenerateLOD(TestMesh, 2, false, false)) return {};
	FSkinnedAssetCompilingManager::Get().FinishCompilation(AssetsToFinish);
	if (TestMesh->GetImportedModel()->LODModels.Num() != 2) return {};
	UClothingAssetFactory* Factory = NewObject<UClothingAssetFactory>();
	int32 Before = 0, After = 0;
	for (const FBinding& Binding : Bindings)
	{
		const auto& ProxySections = TestMesh->GetImportedModel()->LODModels[1].Sections;
		int32 SourceSection = INDEX_NONE;
		for (int32 Index = 0; Index < ProxySections.Num(); ++Index)
		{
			if (ProxySections[Index].MaterialIndex != Binding.Material || ProxySections[Index].NumTriangles == 0) continue;
			if (SourceSection != INDEX_NONE) return {};
			SourceSection = Index;
		}
		if (SourceSection == INDEX_NONE) return {};
		FSkeletalMeshClothBuildParams Params;
		Params.LodIndex = 1;
		Params.SourceSection = SourceSection;
		Params.TargetAsset = Binding.Asset;
		Params.TargetLod = 1;
		Params.bRemapParameters = true;
		if (Factory->ImportLodToClothing(TestMesh, Params) != Binding.Asset) return {};
		const auto& Proxy = Binding.Asset->LodData[1];
		const auto* Limits = Proxy.PhysicalMeshData.FindWeightMap(EWeightMapTargetCommon::MaxDistance);
		const int32 Vertices = Proxy.PhysicalMeshData.Vertices.Num();
		if (Vertices <= 0 || Vertices >= Binding.Vertices || !Limits || Limits->Values.Num() != Vertices ||
			!Limits->Values.ContainsByPredicate([](float Limit) { return Limit <= KINDA_SMALL_NUMBER; }) ||
			!Limits->Values.ContainsByPredicate([](float Limit) { return Limit > KINDA_SMALL_NUMBER; })) return {};
		Binding.Asset->UnbindFromSkeletalMesh(TestMesh, 0, Binding.Section);
		if (!FSkeletalMeshClothingSystemUtilities::AssignClothingToSection(TestMesh, Binding.Asset, 0, Binding.Section, 1, nullptr))
			return {};
		FSkinnedAssetCompilingManager::Get().FinishCompilation(AssetsToFinish);
		Before += Binding.Vertices;
		After += Vertices;
	}
	const auto& ResultModel = TestMesh->GetImportedModel()->LODModels[0];
	if (ResultModel.NumVertices != BaseRenderVertices || ResultModel.IndexBuffer.Num() != BaseRenderIndices) return {};
	TestMesh->MarkPackageDirty();
	return FString::Printf(TEXT("sections=%d physical_vertices_before=%d physical_vertices_after=%d render_lod0_vertices=%d"),
		Bindings.Num(), Before, After, BaseRenderVertices);
}

FString UProject_JPlayerLocomotionLibrary::FinalizeRemoteSurfaceClothingProxy(USkeletalMesh* Source, USkeletalMesh* Proxy)
{
	if (!Source || !Proxy || Source == Proxy || (GEditor && GEditor->PlayWorld) ||
		!Proxy->GetPathName().StartsWith(TEXT("/Game/Characters/"))) return {};
	USkinnedAsset* Assets[] = {Source, Proxy};
	FSkinnedAssetCompilingManager::Get().FinishCompilation(Assets);
	if (!Source->GetImportedModel() || !Proxy->GetImportedModel() || Source->GetLODNum() != 1 ||
		Source->GetSkeleton() != Proxy->GetSkeleton() || Source->GetPhysicsAsset() != Proxy->GetPhysicsAsset())
		return {};
	if (Proxy->GetLODNum() == 2)
	{
		if (!USkeletalMeshEditorSubsystem::RemoveLODs(Proxy, {1})) return {};
		FSkinnedAssetCompilingManager::Get().FinishCompilation(Assets);
	}
	if (Proxy->GetLODNum() != 1) return {};
	const auto& Base = Source->GetImportedModel()->LODModels[0];
	const auto& Remote = Proxy->GetImportedModel()->LODModels[0];
	if (Base.NumVertices != Remote.NumVertices || Base.IndexBuffer != Remote.IndexBuffer ||
		Base.Sections.Num() != Remote.Sections.Num()) return {};
	int32 Count = 0, OriginalVertices = 0, ProxyVertices = 0;
	for (int32 Index = 0; Index < Base.Sections.Num(); ++Index)
	{
		const auto& A = Base.Sections[Index];
		const auto& B = Remote.Sections[Index];
		if (A.HasClothingData() != B.HasClothingData() || A.MaterialIndex != B.MaterialIndex) return {};
		if (!A.HasClothingData()) continue;
		const auto* OriginalCloth = Cast<UClothingAssetCommon>(Source->GetClothingAsset(A.ClothingData.AssetGuid));
		const auto* RemoteCloth = Cast<UClothingAssetCommon>(Proxy->GetClothingAsset(B.ClothingData.AssetGuid));
		if (!OriginalCloth || !RemoteCloth || OriginalCloth->PhysicsAsset != RemoteCloth->PhysicsAsset ||
			!OriginalCloth->LodData.IsValidIndex(0) || !RemoteCloth->LodData.IsValidIndex(1) ||
			A.ClothingData.AssetLodIndex != 0 || B.ClothingData.AssetLodIndex != 1 ||
			!RemoteCloth->LodMap.IsValidIndex(0) || RemoteCloth->LodMap[0] != 1) return {};
		const int32 OriginalCount = OriginalCloth->LodData[0].PhysicalMeshData.Vertices.Num();
		const int32 RemoteCount = RemoteCloth->LodData[1].PhysicalMeshData.Vertices.Num();
		if (RemoteCount <= 0 || RemoteCount >= OriginalCount) return {};
		++Count;
		OriginalVertices += OriginalCount;
		ProxyVertices += RemoteCount;
	}
	if (!Count) return {};
	return FString::Printf(TEXT("sections=%d physical_vertices_before=%d physical_vertices_after=%d render_lod0_vertices=%d render_lods=1"),
		Count, OriginalVertices, ProxyVertices, Base.NumVertices);
}

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
	float PinnedTopDepth, float TransitionDepth, float MaxDistance, FName MaterialSlot)
{
	if (!Mesh || !Collisions || (GEditor && GEditor->PlayWorld) || !Mesh->GetMeshClothingAssets().IsEmpty()
		|| PinnedTopDepth <= 0.f || TransitionDepth <= 0.f || MaxDistance <= 0.f) return TEXT("{}");
	const FSkeletalMeshModel* Model = Mesh->GetImportedModel();
	if (!Model || Model->LODModels.IsEmpty() || (MaterialSlot.IsNone() && Model->LODModels.Num() != 1))
		return TEXT("{}");
	const auto& Sections = Model->LODModels[0].Sections;
	const auto& Materials = Mesh->GetMaterials();
	TArray<int32> TargetSections;
	for (int32 Index = 0; Index < Sections.Num(); ++Index)
	{
		const FSkelMeshSection& Section = Sections[Index];
		if (!MaterialSlot.IsNone() && (!Materials.IsValidIndex(Section.MaterialIndex) ||
			Materials[Section.MaterialIndex].MaterialSlotName != MaterialSlot)) continue;
		if (Section.HasClothingData() || Section.SoftVertices.IsEmpty()) return TEXT("{}");
		TargetSections.Add(Index);
	}
	if (TargetSections.IsEmpty() || (!MaterialSlot.IsNone() && TargetSections.Num() != 1))
		return TEXT("{}");
	const FScopedTransaction Transaction(NSLOCTEXT("ProjectJ", "CreateSurfaceClothing", "Create surface clothing"));
	Mesh->Modify();
	UClothingAssetFactory* Factory = NewObject<UClothingAssetFactory>();
	TArray<UClothingAssetCommon*> Created;
	TArray<TSharedPtr<FJsonValue>> Reports;
	bool bSuccess = true;
	for (int32 SectionIndex : TargetSections)
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
		Asset->Modify(); Asset->InvalidateFlaggedCachedData(EClothingCachedDataFlagsCommon::All); Asset->ApplyParameterMasks();
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
