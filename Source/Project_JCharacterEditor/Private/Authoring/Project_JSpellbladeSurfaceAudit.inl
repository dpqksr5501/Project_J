using FShoulderAuditWriter = TJsonWriter<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>;

void WriteShoulderAuditTransform(FShoulderAuditWriter& Writer, const FString& Name, const FTransform& Transform)
{
	const FVector P = Transform.GetLocation(), S = Transform.GetScale3D();
	const FQuat Q = Transform.GetRotation();
	Writer.WriteArrayStart(Name);
	for (double Value : {P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W, S.X, S.Y, S.Z}) { Writer.WriteValue(Value); }
	Writer.WriteArrayEnd();
}

bool SaveShoulderAudit(const FString& Suffix, const FString& Json)
{
	const FString Filename = TEXT("SpellbladeCombatNext/SpellbladeShoulderSurface_") + Suffix + TEXT(".json");
	return FFileHelper::SaveStringToFile(Json, *(FPaths::ProjectSavedDir() / Filename), FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

bool ExportShoulderSurface(const USkeletalMesh* Body, const FString& Part, TArray<FName>* PoseBones = nullptr, bool bFullSurface = false)
{
	const auto* Model = Body ? Body->GetImportedModel() : nullptr;
	if (!Model || Model->LODModels.IsEmpty()) { return false; }
	const FReferenceSkeleton& Skeleton = Body->GetRefSkeleton();
	const int32 ModernNeck = Skeleton.FindBoneIndex(TEXT("Bip01_Neck"));
	const int32 NeckIndex = ModernNeck != INDEX_NONE ? ModernNeck : Skeleton.FindBoneIndex(TEXT("Bip01-Neck"));
	if (NeckIndex == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("Shoulder surface mesh has no deform neck: %s"), *Body->GetPathName());
		return false;
	}
	TArray<FTransform> BindPose = Skeleton.GetRefBonePose();
	for (int32 Index = 1; Index < BindPose.Num(); ++Index)
	{
		BindPose[Index] *= BindPose[Skeleton.GetParentIndex(Index)];
	}

	const FSkeletalMeshLODModel& Lod = Model->LODModels[0];
	const double MinZ = BindPose[NeckIndex].GetLocation().Z - 35.;
	FString Json;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	TSet<int32> InfluencingBones;
	InfluencingBones.Add(NeckIndex);
	Writer->WriteObjectStart();
	Writer->WriteValue(TEXT("mesh"), Body->GetPathName());
	Writer->WriteValue(TEXT("neck"), Skeleton.GetBoneName(NeckIndex).ToString());
	Writer->WriteArrayStart(TEXT("sections"));
	for (const FSkelMeshSection& Section : Lod.Sections)
	{
		if (Section.bDisabled || Section.HasClothingData()) { continue; }
		TArray<FIntVector> Triangles;
		TSet<int32> VertexIndices;
		for (uint32 Tri = 0; Tri < Section.NumTriangles; ++Tri)
		{
			const uint32 BaseIndex = Section.BaseIndex + 3 * Tri;
			if (!Lod.IndexBuffer.IsValidIndex(BaseIndex + 2)) { return false; }
			const int32 A = Lod.IndexBuffer[BaseIndex] - Section.BaseVertexIndex;
			const int32 B = Lod.IndexBuffer[BaseIndex + 1] - Section.BaseVertexIndex;
			const int32 C = Lod.IndexBuffer[BaseIndex + 2] - Section.BaseVertexIndex;
			if (!Section.SoftVertices.IsValidIndex(A) || !Section.SoftVertices.IsValidIndex(B)
				|| !Section.SoftVertices.IsValidIndex(C)) { return false; }
			const auto& V = Section.SoftVertices;
			if (!bFullSurface && FMath::Max3(V[A].Position.Z, V[B].Position.Z, V[C].Position.Z) < MinZ) { continue; }
			Triangles.Emplace(A, B, C);
			VertexIndices.Add(A);
			VertexIndices.Add(B);
			VertexIndices.Add(C);
		}
		if (Triangles.IsEmpty()) { continue; }
		Writer->WriteObjectStart();
		if (!Body->GetMaterials().IsValidIndex(Section.MaterialIndex)) { return false; }
		const FSkeletalMaterial& Material = Body->GetMaterials()[Section.MaterialIndex];
		Writer->WriteValue(TEXT("slot"), Material.MaterialSlotName.ToString());
		Writer->WriteArrayStart(TEXT("triangles"));
		for (const FIntVector& Triangle : Triangles)
		{
			Writer->WriteArrayStart();
			for (int32 Vertex : {Triangle.X, Triangle.Y, Triangle.Z}) { Writer->WriteValue(Vertex); }
			Writer->WriteArrayEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectStart(TEXT("vertices"));
		for (int32 Index = 0; Index < Section.SoftVertices.Num(); ++Index)
		{
			if (!VertexIndices.Contains(Index)) { continue; }
			const FSoftSkinVertex& Vertex = Section.SoftVertices[Index];
			Writer->WriteArrayStart(LexToString(Index));
			for (float Value : {Vertex.Position.X, Vertex.Position.Y, Vertex.Position.Z}) { Writer->WriteValue(Value); }
			for (int32 I = 0; I < UE_ARRAY_COUNT(Vertex.InfluenceWeights); ++I)
			{
				const uint16 Weight = Vertex.InfluenceWeights[I];
				if (!Weight) { continue; }
				const int32 LocalBone = Vertex.InfluenceBones[I];
				if (!Section.BoneMap.IsValidIndex(LocalBone)) { return false; }
				const int32 Bone = Section.BoneMap[LocalBone];
				InfluencingBones.Add(Bone);
				Writer->WriteValue(Bone);
				Writer->WriteValue(int32(Weight));
			}
			Writer->WriteArrayEnd();
		}
		Writer->WriteObjectEnd();
		Writer->WriteObjectEnd();
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectStart(TEXT("bones"));
	for (int32 Bone : InfluencingBones)
	{
		if (!BindPose.IsValidIndex(Bone)) { return false; }
		const FName Name = Skeleton.GetBoneName(Bone);
		if (PoseBones) { PoseBones->AddUnique(Name); }
		Writer->WriteObjectStart(LexToString(Bone));
		Writer->WriteValue(TEXT("name"), Name.ToString());
		const int32 Parent = Skeleton.GetParentIndex(Bone);
		Writer->WriteValue(TEXT("parent"), Parent != INDEX_NONE ? Skeleton.GetBoneName(Parent).ToString() : FString());
		WriteShoulderAuditTransform(*Writer, TEXT("bind"), BindPose[Bone]);
		Writer->WriteObjectEnd();
	}
	Writer->WriteObjectEnd();
	Writer->WriteObjectEnd();
	if (!Writer->Close()) { return false; }
	UE_LOG(LogTemp, Display, TEXT("SpellbladeShoulderSurface %s: %d influencing bones"), *Part, InfluencingBones.Num());
	return SaveShoulderAudit(Part, Json);
}

bool ExportShoulderSurface()
{
	for (const TCHAR* Part : {TEXT("Complete"), TEXT("body"), TEXT("sho"), TEXT("head"), TEXT("upper"), TEXT("hel")})
	{
		const auto* Mesh = Asset<USkeletalMesh>(FString(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_")) + Part);
		if (!ExportShoulderSurface(Mesh, Part)) { return false; }
	}
	return true;
}

bool ExportClothingSurface()
{
	const TCHAR* Paths[][2] = {
		{TEXT("ClothingBody"), TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_body")},
		{TEXT("ClothingLower"), TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_lower")},
		{TEXT("ClothingLegacyBody"), TEXT("/Game/Characters/Spellblade/Spellblade_Woman")},
		{TEXT("ClothingLegacyLower"), TEXT("/Game/Characters/Spellblade/Clothes/lower")}
	};
	for (const auto& Pair : Paths)
	{
		if (!ExportShoulderSurface(Asset<USkeletalMesh>(Pair[1]), Pair[0], nullptr, true)) { return false; }
	}
	return true;
}

bool FitLongbladeArmor(bool bApply, bool bCollar)
{
	auto* Armor = Asset<USkeletalMesh>(bCollar ? TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_upper")
		: TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_sho"));
	const TCHAR* ExportName = bCollar ? TEXT("CollarFitted") : TEXT("PauldronFitted");
	const TCHAR* CollarMarker = TEXT("ProjectJ.LongbladeCollarFit");
	if (!Armor || !Armor->HasMeshDescription(0) || !Armor->GetSkinWeightProfiles().IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("SpellbladeArmorFit requires the source mesh without alternate skin weights"));
		return false;
	}
	if (bCollar && Armor->GetOutermost()->GetMetaData().HasValue(Armor, CollarMarker))
	{
		return ExportShoulderSurface(Armor, ExportName);
	}
	for (int32 LOD = 1; LOD < Armor->GetLODNum(); ++LOD)
	{
		// This fit expects the remaining LODs to reduce the same authored mesh.
		if (Armor->HasMeshDescription(LOD))
		{
			UE_LOG(LogTemp, Error, TEXT("SpellbladeArmorFit LOD %d is independently authored"), LOD);
			return false;
		}
	}
	FMeshDescription Mesh;
	if (!Armor->CloneMeshDescription(0, Mesh)) { return false; }
	FSkeletalMeshAttributes Attributes(Mesh);
	const auto Slots = Attributes.GetPolygonGroupMaterialSlotNames();
	const auto Names = Attributes.GetBoneNames();
	const FName ChestName(TEXT("Bip01_Spine2"));
	int32 Chest = INDEX_NONE;
	for (int32 Index = 0; Index < Attributes.GetNumBones(); ++Index)
	{
		if (Names.Get(FBoneID(Index)) == ChestName) { Chest = Index; break; }
	}
	auto Weights = Attributes.GetVertexSkinWeights();
	if (!Weights.IsValid() || Chest == INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("SpellbladeArmorFit has no usable chest weights, source bones=%d"), Attributes.GetNumBones());
		return false;
	}
	const FName SlotName(bCollar ? TEXT("M_pdw_00_ub_0001") : TEXT("M_pdw_00_sho_0001"));
	const FSkeletalMaterial* TargetMaterial = Armor->GetMaterials().FindByPredicate([SlotName](const FSkeletalMaterial& Material)
	{
		return Material.MaterialSlotName == SlotName;
	});
	if (!TargetMaterial || TargetMaterial->ImportedMaterialSlotName.IsNone()) { return false; }
	TSet<FVertexID> SectionVertices, OtherVertices;
	TSet<FName> MaterialNames;
	for (FPolygonID Polygon : Mesh.Polygons().GetElementIDs())
	{
		const FName Material = Slots.Get(Mesh.GetPolygonPolygonGroup(Polygon));
		MaterialNames.Add(Material);
		auto& Vertices = Material == TargetMaterial->ImportedMaterialSlotName ? SectionVertices : OtherVertices;
		Vertices.Append(Mesh.GetPolygonVertices(Polygon));
	}
	const int32 SharedCount = SectionVertices.Intersect(OtherVertices).Num();
	if (SectionVertices.IsEmpty() || OtherVertices.IsEmpty() || SharedCount != 0)
	{
		TArray<FString> Labels;
		for (FName Name : MaterialNames) { Labels.Add(Name.ToString()); }
		UE_LOG(LogTemp, Error, TEXT("SpellbladeArmorFit section selection plate=%d other=%d shared=%d slots=%s"),
			SectionVertices.Num(), OtherVertices.Num(), SharedCount, *FString::Join(Labels, TEXT(", ")));
		return false;
	}

	constexpr float ChestSupport = .7f;
	constexpr float EdgeStart = 149.f, EdgeRise = 13.f;
	constexpr float EdgeOut = 6.f, EdgeDown = 2.f;
	constexpr uint16 MaxWeight = TNumericLimits<uint16>::Max();
	auto Positions = Attributes.GetVertexPositions();
	auto Normals = Attributes.GetVertexInstanceNormals();
	auto Tangents = Attributes.GetVertexInstanceTangents();
	int32 ChestSupported = 0;
	int32 Adjusted = 0;
	for (FVertexID Vertex : SectionVertices)
	{
		FVector3f Position = Positions.Get(Vertex);
		if (bCollar)
		{
			// The front collar edge had a sub-centimetre overlap in the composed
			// head pose. Refit the edge without moving the skin weights or neck.
			const FVector3f Offset = Position - FVector3f(0.f, 7.2f, 163.f);
			constexpr float Radius = 3.25f, Drop = .40f;
			const float T = 1.f - Offset.Size() / Radius;
			if (T <= 0.f) { continue; }
			Position.Z -= Drop * T * T * (3.f - 2.f * T);
			Positions.Set(Vertex, Position);
			const FVector3f Slope = (Drop * 6.f * T * (1.f - T) / Radius) * Offset.GetSafeNormal();
			for (FVertexInstanceID Instance : Mesh.GetVertexVertexInstanceIDs(Vertex))
			{
				const FVector3f Normal = Normals.Get(Instance), Tangent = Tangents.Get(Instance);
				const float Z = Normal.Z / (1.f + Slope.Z);
				Normals.Set(Instance, FVector3f(Normal.X - Slope.X * Z, Normal.Y - Slope.Y * Z, Z).GetSafeNormal());
				Tangents.Set(Instance, FVector3f(Tangent.X, Tangent.Y, Tangent.Z + FVector3f::DotProduct(Tangent, Slope)).GetSafeNormal());
			}
			++Adjusted;
			continue;
		}
		using namespace UE::AnimationCore;
		TArray<FBoneWeight, TInlineAllocator<12>> Rest;
		uint16 Assigned = 0;
		for (const FBoneWeight Weight : Weights.Get(Vertex))
		{
			if (Weight.GetBoneIndex() == Chest)
			{
				ChestSupported += Weight.GetRawWeight() >= FMath::FloorToInt(ChestSupport * MaxWeight);
				continue;
			}
			const auto Raw = static_cast<uint16>(FMath::FloorToInt((1.f - ChestSupport) * Weight.GetRawWeight()));
			if (Raw) { Rest.Emplace(Weight.GetBoneIndex(), Raw); }
			Assigned += Raw;
		}
		Rest.Emplace(static_cast<FBoneIndexType>(Chest), static_cast<uint16>(MaxWeight - Assigned));
		Weights.Set(Vertex, Rest);

		const float Phase = FMath::Clamp((Position.Z - EdgeStart) / EdgeRise, 0.f, 1.f);
		const float Shift = Phase * Phase * (3.f - 2.f * Phase);
		Position.X += EdgeOut * Shift;
		Position.Z -= EdgeDown * Shift;
		Positions.Set(Vertex, Position);
		// Transform the tangent frame with the same smooth deformation as the edge.
		const float Slope = 6.f * Phase * (1.f - Phase) / EdgeRise;
		const float OutSlope = EdgeOut * Slope, HeightScale = 1.f - EdgeDown * Slope;
		for (FVertexInstanceID Instance : Mesh.GetVertexVertexInstanceIDs(Vertex))
		{
			const FVector3f N = Normals.Get(Instance), T = Tangents.Get(Instance);
			Normals.Set(Instance, FVector3f(N.X, N.Y, (N.Z - OutSlope * N.X) / HeightScale).GetSafeNormal());
			Tangents.Set(Instance, FVector3f(T.X + OutSlope * T.Z, T.Y, HeightScale * T.Z).GetSafeNormal());
		}
	}
	if (ChestSupported == SectionVertices.Num())
	{
		UE_LOG(LogTemp, Display, TEXT("SpellbladePauldron fit already present in %s"), *Armor->GetName());
		return ExportShoulderSurface(Armor, ExportName);
	}
	if (bCollar && Adjusted == 0) { return false; }
	auto* Candidate = bApply ? Armor : DuplicateObject<USkeletalMesh>(Armor, GetTransientPackage());
	if (!Candidate) { return false; }
	Candidate->Modify();
	if (!Candidate->CreateMeshDescription(0, MoveTemp(Mesh)) || !Candidate->CommitMeshDescription(0)) { return false; }
	Candidate->PostEditChange();
	USkinnedAsset* Compiling[] = {Candidate};
	FSkinnedAssetCompilingManager::Get().FinishCompilation(Compiling);
	if (!ExportShoulderSurface(Candidate, ExportName)) { return false; }
	UE_LOG(LogTemp, Display, TEXT("SpellbladeArmorFit %d fitted vertices in %s, LODs=%d"),
		bCollar ? Adjusted : SectionVertices.Num(), *Armor->GetName(), Armor->GetLODNum());
	if (!bApply) { return true; }
	if (bCollar) { Candidate->GetOutermost()->GetMetaData().SetValue(Candidate, CollarMarker, TEXT("1")); }
	return Save(Candidate);
}

bool FitSleeveSurface(bool bApply)
{
	const FString MeshRoot = TEXT("/Game/Characters/Spellblade/Meshes/");
	const FString BodyPath = MeshRoot + TEXT("SK_Spellblade_body_StarterOutfit");
	const FString LowerPath = MeshRoot + TEXT("SK_Spellblade_lower_StarterOutfit");
	auto* Body = Asset<USkeletalMesh>(MeshRoot + TEXT("SK_Spellblade_body"));
	auto* Lower = Asset<USkeletalMesh>(MeshRoot + TEXT("SK_Spellblade_lower"));
	if (!Body || !Lower || Body->GetSkeleton() != Lower->GetSkeleton()) { return false; }
	const FName LaceSlot(TEXT("M_pdw_00_lb_0001_dec"));
	const int32 LaceIndex = Lower->GetMaterials().IndexOfByPredicate([LaceSlot](const FSkeletalMaterial& M)
	{
		return M.MaterialSlotName == LaceSlot;
	});
	if (LaceIndex == INDEX_NONE || Body->GetMaterials().Num() != 1) { return false; }
	UMaterialInterface* LaceMat = Lower->GetMaterials()[LaceIndex].MaterialInterface;
	if (!LaceMat || LaceMat->GetBlendMode() != BLEND_Opaque) { return false; }
	UE_LOG(LogTemp, Display, TEXT("SpellbladeSleeveSurface opaque lace, LODs %d/%d"), Body->GetLODNum(), Lower->GetLODNum());
	for (const auto* Mesh : {Body, Lower})
	{
		for (int32 LOD = 1; LOD < Mesh->GetLODNum(); ++LOD)
		{
			if (Mesh->HasMeshDescription(LOD))
			{
				const FSkeletalMeshLODInfo* Info = Mesh->GetLODInfo(LOD);
				if (!Info || !Info->bHasBeenSimplified || Info->ReductionSettings.BaseLOD != 0)
				{
					UE_LOG(LogTemp, Error, TEXT("SpellbladeSleeveSurface independently authored LOD %s %d"), *Mesh->GetName(), LOD);
					return false;
				}
			}
		}
	}
	FMeshDescription Skin, Lace;
	if (!Body->CloneMeshDescription(0, Skin) || !Lower->CloneMeshDescription(0, Lace)) { return false; }
	FSkeletalMeshAttributes SkinAttrs(Skin), LaceAttrs(Lace);
	const auto SkinPosition = SkinAttrs.GetVertexPositions();
	const auto LacePosition = LaceAttrs.GetVertexPositions();
	const auto LaceSlots = LaceAttrs.GetPolygonGroupMaterialSlotNames();
	const auto LaceUVs = LaceAttrs.GetVertexInstanceUVs();
	const auto LaceTangents = LaceAttrs.GetVertexInstanceTangents();
	const auto LaceSigns = LaceAttrs.GetVertexInstanceBinormalSigns();
	auto SkinUVs = SkinAttrs.GetVertexInstanceUVs();
	auto SkinTangents = SkinAttrs.GetVertexInstanceTangents();
	auto SkinSigns = SkinAttrs.GetVertexInstanceBinormalSigns();
	UE_LOG(LogTemp, Display, TEXT("SpellbladeSleeveSurface source UVs %d/%d"), SkinUVs.GetNumChannels(), LaceUVs.GetNumChannels());
	if (SkinUVs.GetNumChannels() < 1 || LaceUVs.GetNumChannels() < 1) { return false; }
	const auto& Ref = Body->GetRefSkeleton();
	const int32 ElbowIndex = Ref.FindBoneIndex(TEXT("Bip01_R_Forearm"));
	if (ElbowIndex == INDEX_NONE) { return false; }
	FTransform ElbowTransform = Ref.GetRefBonePose()[ElbowIndex];
	for (int32 Parent = Ref.GetParentIndex(ElbowIndex); Parent != INDEX_NONE; Parent = Ref.GetParentIndex(Parent))
	{
		ElbowTransform *= Ref.GetRefBonePose()[Parent];
	}
	const FVector3f Elbow(ElbowTransform.GetLocation());
	constexpr float Radius = 32.f, MatchDistance = .12f;
	struct FFace
	{
		FPolygonID Polygon;
		TArray<FVertexInstanceID, TInlineAllocator<3>> Corners;
		FVector3f Points[3];
	};
	TArray<FFace> SkinFaces;
	for (FPolygonID Polygon : Skin.Polygons().GetElementIDs())
	{
		FFace Face{Polygon, {}, {}};
		Skin.GetPolygonVertexInstances(Polygon, Face.Corners);
		if (Face.Corners.Num() != 3) { return false; }
		for (int32 I = 0; I < 3; ++I) { Face.Points[I] = SkinPosition.Get(Skin.GetVertexInstanceVertex(Face.Corners[I])); }
		if (FVector3f::DistSquared((Face.Points[0] + Face.Points[1] + Face.Points[2]) / 3.f, Elbow) < FMath::Square(Radius))
		{
			SkinFaces.Add(MoveTemp(Face));
		}
	}
	const FName CompositeSlot(TEXT("StarterSleeveSurface"));
	const FPolygonGroupID Group = Skin.CreatePolygonGroup();
	SkinAttrs.GetPolygonGroupMaterialSlotNames().Set(Group, CompositeSlot);
	TSet<FPolygonID> MatchedSkin;
	TArray<FPolygonID> ReplacedLace;
	for (FPolygonID Polygon : Lace.Polygons().GetElementIDs())
	{
		if (LaceSlots.Get(Lace.GetPolygonPolygonGroup(Polygon)) != Lower->GetMaterials()[LaceIndex].ImportedMaterialSlotName) { continue; }
		const auto Corners = Lace.GetPolygonVertexInstances(Polygon);
		if (Corners.Num() != 3) { return false; }
		FVector3f Points[3];
		for (int32 I = 0; I < 3; ++I) { Points[I] = LacePosition.Get(Lace.GetVertexInstanceVertex(Corners[I])); }
		if (FVector3f::DistSquared((Points[0] + Points[1] + Points[2]) / 3.f, Elbow) > FMath::Square(Radius)) { continue; }
		for (const FFace& Face : SkinFaces)
		{
			int32 Map[3] = {INDEX_NONE, INDEX_NONE, INDEX_NONE};
			for (int32 I = 0; I < 3; ++I)
			{
				for (int32 J = 0; J < 3; ++J)
				{
					if (!Points[I].Equals(Face.Points[J], MatchDistance)) { continue; }
					if (Map[I] == INDEX_NONE || FVector3f::DistSquared(Points[I], Face.Points[J]) < FVector3f::DistSquared(Points[I], Face.Points[Map[I]]))
					{
						Map[I] = J;
					}
				}
			}
			if (Map[0] == INDEX_NONE || Map[1] == INDEX_NONE || Map[2] == INDEX_NONE
				|| Map[0] == Map[1] || Map[0] == Map[2] || Map[1] == Map[2]) { continue; }
			if (MatchedSkin.Contains(Face.Polygon)) { break; }
			for (int32 I = 0; I < 3; ++I)
			{
				const FVertexInstanceID Corner = Face.Corners[Map[I]];
				if (Skin.GetVertexInstanceConnectedPolygons(Corner).Num() != 1) { return false; }
				SkinUVs.Set(Corner, 0, LaceUVs.Get(Corners[I], 0));
				SkinTangents.Set(Corner, LaceTangents.Get(Corners[I]));
				SkinSigns.Set(Corner, LaceSigns.Get(Corners[I]));
			}
			Skin.SetPolygonPolygonGroup(Face.Polygon, Group);
			MatchedSkin.Add(Face.Polygon);
			ReplacedLace.Add(Polygon);
			break;
		}
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeSleeveSurface matched %d one-to-one faces out of %d skin faces"), ReplacedLace.Num(), SkinFaces.Num());
	if (ReplacedLace.Num() < 350) { return false; }
	Lace.DeletePolygons(ReplacedLace);
	FElementIDRemappings Remap;
	Lace.Compact(Remap);

	if (bApply && (FPackageName::DoesPackageExist(BodyPath) || FPackageName::DoesPackageExist(LowerPath))) { return false; }
	auto* BodyFit = bApply ? Duplicate(Body, BodyPath) : DuplicateObject<USkeletalMesh>(Body, GetTransientPackage());
	auto* LowerFit = bApply ? Duplicate(Lower, LowerPath) : DuplicateObject<USkeletalMesh>(Lower, GetTransientPackage());
	if (!BodyFit || !LowerFit) { return false; }
	BodyFit->GetMaterials().Emplace(LaceMat, CompositeSlot, CompositeSlot);
	if (!BodyFit->CreateMeshDescription(0, MoveTemp(Skin)) || !BodyFit->CommitMeshDescription(0)
		|| !LowerFit->CreateMeshDescription(0, MoveTemp(Lace)) || !LowerFit->CommitMeshDescription(0)) { return false; }
	BodyFit->PostEditChange();
	LowerFit->PostEditChange();
	USkinnedAsset* Compiling[] = {BodyFit, LowerFit};
	FSkinnedAssetCompilingManager::Get().FinishCompilation(Compiling);
	if (!ExportShoulderSurface(BodyFit, TEXT("StarterSleeveBody"), nullptr, true)
		|| !ExportShoulderSurface(LowerFit, TEXT("StarterSleeveLower"), nullptr, true)) { return false; }
	return !bApply || (Save(BodyFit) && Save(LowerFit));
}

bool WriteShoulderAuditClip(FShoulderAuditWriter& Writer, const FString& Label, UAnimSequence& Clip,
	USkeletalMesh& Mesh, TConstArrayView<FName> Bones)
{
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Mesh;
	Options.bShouldRetarget = false;
	const double Length = Clip.GetPlayLength();
	const int32 Samples = FMath::CeilToInt(Length * 30.);
	if (Samples < 1) { return false; }
	Writer.WriteArrayStart(Label);
	for (int32 Sample = 0; Sample <= Samples; ++Sample)
	{
		const double Time = Length * Sample / Samples;
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Time, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		Writer.WriteObjectStart();
		Writer.WriteValue(TEXT("t"), Time);
		Writer.WriteValue(TEXT("montage_position"), Time);
		Writer.WriteObjectStart(TEXT("bones"));
		for (FName Bone : Bones)
		{
			WriteShoulderAuditTransform(Writer, Bone.ToString(),
				UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World));
		}
		Writer.WriteObjectEnd();
		Writer.WriteObjectEnd();
	}
	Writer.WriteArrayEnd();
	return true;
}

bool AuditLongbladeGripFrames(const FString& Params)
{
	const FStanceTake Takes[] = {
		{TEXT("Ready"), TEXT("stand_idle_00")},
		{TEXT("BasicCut01"), TEXT("01_01_att_normal_01")},
		{TEXT("BasicCut02"), TEXT("01_01_att_normal_02")},
		{TEXT("BasicCut03Ground"), TEXT("01_01_att_normal_r_02")},
		{TEXT("Sheathe"), TEXT("stand_weapon_in_00")}
	};
	const double Durations[] = {2., 1.16, 1.02, 1.10, 1.067};
	auto* Legacy = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Spellblade_Woman"));
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* GripSocket = Weapon ? Weapon->FindSocket(TEXT("WeaponGrip_R_Aligned")) : nullptr;
	const auto* TipSocket = Weapon ? Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip")) : nullptr;
	FTransform Mount;
	if (!Legacy || !Body || !GripSocket || !TipSocket || !ResolveStanceWeaponMount(*Body, *Weapon, Mount)) { return false; }
	FTransform ReadyMount = Mount;
	const FVector GripLocal = GripSocket->GetSocketLocalTransform().GetLocation();
	const FVector TipLocal = TipSocket->GetSocketLocalTransform().GetLocation();
	for (const FName Bone : {FName(TEXT("Bip_Weapon_R")), FName(TEXT("ik_hand_gun")), FName(TEXT("ik_hand_root"))})
	{
		for (const auto* Mesh : {Legacy, Body})
		{
			const auto& Ref = Mesh->GetRefSkeleton();
			const int32 Index = Ref.FindBoneIndex(Bone);
			if (Index == INDEX_NONE) { continue; }
			const int32 Parent = Ref.GetParentIndex(Index);
			UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponAnchor %s %s parent=%s bind=%s"),
				*Mesh->GetName(), *Bone.ToString(), Parent >= 0 ? *Ref.GetBoneName(Parent).ToString() : TEXT("None"),
				*Ref.GetRefBonePose()[Index].ToString());
		}
	}
	TArray<UAnimSequence*> Source;
	const auto Retargeted = RetargetStanceTakes(Params, Takes, TEXT("GripFrameAudit_"), false, StanceFolder, nullptr, &Source);
	if (Source.Num() != UE_ARRAY_COUNT(Takes) || Retargeted.Num() != UE_ARRAY_COUNT(Takes)) { return false; }
	const auto World = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
	};
	const auto Palm = [&](const FAnimPose& Pose, bool bSource)
	{
		const FName Hand = bSource ? TEXT("Bip01-R-Hand") : TEXT("hand_r");
		const FName Index = bSource ? TEXT("Bip01-R-Finger1") : TEXT("index_01_r");
		const FName Middle = bSource ? TEXT("Bip01-R-Finger2") : TEXT("middle_01_r");
		const FName Pinky = bSource ? TEXT("Bip01-R-Finger4") : TEXT("pinky_01_r");
		return FRotationMatrix::MakeFromXY(World(Pose, Middle).GetLocation() - World(Pose, Hand).GetLocation(),
			World(Pose, Index).GetLocation() - World(Pose, Pinky).GetLocation()).ToQuat();
	};
	const auto Bend = [&](const FAnimPose& Pose, bool bSource)
	{
		const FName Hand = bSource ? TEXT("Bip01-R-Hand") : TEXT("hand_r");
		const FName Lower = bSource ? TEXT("Bip01-R-Forearm") : TEXT("lowerarm_r");
		const auto& Ref = (bSource ? Legacy : Body)->GetRefSkeleton();
		const FTransform Bind = Ref.GetRefBonePose()[Ref.FindBoneIndex(Hand)];
		const FVector Axis = Bind.GetRotation().UnrotateVector(Bind.GetLocation().GetSafeNormal());
		const FTransform Wrist = World(Pose, Hand);
		const FVector Direction = (Wrist.GetLocation() - World(Pose, Lower).GetLocation()).GetSafeNormal();
		return FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(
			Direction, Wrist.TransformVectorNoScale(Axis)), -1., 1.)));
	};
	FAnimPoseEvaluationOptions SrcOptions, DstOptions;
	SrcOptions.OptionalSkeletalMesh = Legacy;
	DstOptions.OptionalSkeletalMesh = Body;
	SrcOptions.bShouldRetarget = DstOptions.bShouldRetarget = false;
	FQuat ReadyPalmWeapon = FQuat::Identity;
	FString Json;
	const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
	Writer->WriteObjectStart();
	Writer->WriteArrayStart(TEXT("roles"));
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Takes); ++Index)
	{
		const FString Expected = FString(TEXT("GripFrameAudit_")) + Takes[Index].Name;
		const auto* Raw = Retargeted.FindByPredicate([&](const UAnimSequence* Clip) { return Clip->GetName() == Expected; });
		if (!Raw) { return false; }
		const double Duration = FMath::Min(Durations[Index], double(Source[Index]->GetPlayLength()));
		const int32 Samples = FMath::CeilToInt(Duration * 60.);
		FTransform InitialRelative;
		FQuat InitialPalmWeapon = FQuat::Identity;
		double MaxPosition = 0., MaxHandRotation = 0., MaxPalmRotation = 0., MaxFromReady = 0.;
		double MinMountError = 180., MaxMountError = 0.;
		double MaxSourceBend = 0., MaxRawBend = 0., MaxGripShift = 0.;
		FVector InitialGrip, MinTip(TNumericLimits<double>::Max());
		const FName Anchor(TEXT("ik_hand_gun"));
		const bool bHasAnchor = Body->GetRefSkeleton().FindBoneIndex(Anchor) != INDEX_NONE;
		double AnchorError = 0., AnchorTravel = 0.;
		FVector FirstAnchor;
		const bool bHasLegacyWeapon = Body->GetRefSkeleton().FindBoneIndex(TEXT("Bip_Weapon_R")) != INDEX_NONE
			&& Body->GetRefSkeleton().FindBoneIndex(TEXT("Bip01_R_Hand")) != INDEX_NONE;
		FVector2D LegacyTips(TNumericLimits<double>::Max());
		double LegacyWeaponError = 0.;
		Writer->WriteObjectStart();
		Writer->WriteValue(TEXT("role"), Takes[Index].Name);
		Writer->WriteArrayStart(TEXT("samples"));
		for (int32 Sample = 0; Sample <= Samples; ++Sample)
		{
			const double Time = Duration * Sample / Samples;
			FAnimPose Src, Dst;
			UAnimPoseExtensions::GetAnimPoseAtTime(Source[Index], Time, SrcOptions, Src);
			UAnimPoseExtensions::GetAnimPoseAtTime(*Raw, Time, DstOptions, Dst);
			if (!Src.IsValid() || !Dst.IsValid()) { return false; }
			const FTransform SrcHand = World(Src, TEXT("Bip01-R-Hand"));
			const FTransform SrcWeapon = World(Src, TEXT("Bip_Weapon_R"));
			const FTransform Relative = SrcWeapon.GetRelativeTransform(SrcHand);
			const FQuat SrcPalm = Palm(Src, true), DstPalm = Palm(Dst, false);
			const FQuat PalmWeapon = (SrcPalm.Inverse() * SrcWeapon.GetRotation()).GetNormalized();
			const FQuat IdealMount = (World(Dst, TEXT("hand_r")).GetRotation().Inverse() * DstPalm * PalmWeapon).GetNormalized();
			if (Sample == 0)
			{
				InitialRelative = Relative;
				InitialPalmWeapon = PalmWeapon;
				if (Index == 0) { ReadyPalmWeapon = PalmWeapon; }
				if (Index == 0)
				{
					const FVector PalmPoint = Mount.TransformPosition(GripLocal);
					ReadyMount.SetRotation(IdealMount);
					ReadyMount.SetTranslation(PalmPoint - ReadyMount.TransformVector(GripLocal));
				}
				InitialGrip = Relative.TransformPosition(GripLocal);
			}
			MaxPosition = FMath::Max(MaxPosition, FVector::Distance(Relative.GetLocation(), InitialRelative.GetLocation()));
			MaxHandRotation = FMath::Max(MaxHandRotation, FMath::RadiansToDegrees(Relative.GetRotation().AngularDistance(InitialRelative.GetRotation())));
			MaxPalmRotation = FMath::Max(MaxPalmRotation, FMath::RadiansToDegrees(PalmWeapon.AngularDistance(InitialPalmWeapon)));
			MaxFromReady = FMath::Max(MaxFromReady, FMath::RadiansToDegrees(PalmWeapon.AngularDistance(ReadyPalmWeapon)));
			const double MountError = FMath::RadiansToDegrees(IdealMount.AngularDistance(Mount.GetRotation()));
			MinMountError = FMath::Min(MinMountError, MountError);
			MaxMountError = FMath::Max(MaxMountError, MountError);
			MaxSourceBend = FMath::Max(MaxSourceBend, Bend(Src, true));
			MaxRawBend = FMath::Max(MaxRawBend, Bend(Dst, false));
			MaxGripShift = FMath::Max(MaxGripShift, FVector::Distance(Relative.TransformPosition(GripLocal), InitialGrip));
			const FTransform DstHand = World(Dst, TEXT("hand_r"));
			const FVector Tips(SrcWeapon.TransformPosition(TipLocal).Z, DstHand.TransformPosition(Mount.TransformPosition(TipLocal)).Z,
				DstHand.TransformPosition(ReadyMount.TransformPosition(TipLocal)).Z);
			MinTip = MinTip.ComponentMin(Tips);
			if (bHasAnchor)
			{
				const FTransform ExpectedWeapon = SrcWeapon.GetRelativeTransform(World(Src, TEXT("Bip01-Spine2")))
					* World(Dst, TEXT("spine_05"));
				const FVector Actual = World(Dst, Anchor).GetLocation();
				if (Sample == 0) { FirstAnchor = Actual; }
				AnchorError = FMath::Max(AnchorError, FVector::Distance(Actual, ExpectedWeapon.GetLocation()));
				AnchorTravel = FMath::Max(AnchorTravel, FVector::Distance(Actual, FirstAnchor));
			}
			if (bHasLegacyWeapon)
			{
				const FTransform Actual = World(Dst, TEXT("Bip_Weapon_R"));
				const FTransform Copied = Relative * World(Dst, TEXT("Bip01_R_Hand"));
				LegacyTips.X = FMath::Min(LegacyTips.X, Actual.TransformPosition(TipLocal).Z);
				LegacyTips.Y = FMath::Min(LegacyTips.Y, Copied.TransformPosition(TipLocal).Z);
				LegacyWeaponError = FMath::Max(LegacyWeaponError, FMath::RadiansToDegrees(
					Actual.GetRotation().AngularDistance(Copied.GetRotation())));
			}
			Writer->WriteObjectStart();
			Writer->WriteValue(TEXT("time"), Time);
			Writer->WriteValue(TEXT("mount_error"), MountError);
			Writer->WriteValue(TEXT("source_hand_weapon_angle"), FMath::RadiansToDegrees(Relative.GetRotation().AngularDistance(InitialRelative.GetRotation())));
			Writer->WriteValue(TEXT("source_palm_weapon_angle"), FMath::RadiansToDegrees(PalmWeapon.AngularDistance(InitialPalmWeapon)));
			Writer->WriteValue(TEXT("source_wrist_bend"), Bend(Src, true));
			Writer->WriteValue(TEXT("target_raw_wrist_bend"), Bend(Dst, false));
			Writer->WriteArrayStart(TEXT("tip_original_old_ready_mount"));
			for (double Value : {Tips.X, Tips.Y, Tips.Z}) { Writer->WriteValue(Value); }
			Writer->WriteArrayEnd();
			if (Index == 0 && Sample == 0)
			{
				Writer->WriteArrayStart(TEXT("ready_mount"));
				const FVector P = ReadyMount.GetLocation();
				const FQuat Q = ReadyMount.GetRotation();
				for (double Value : {P.X, P.Y, P.Z, Q.X, Q.Y, Q.Z, Q.W}) { Writer->WriteValue(Value); }
				Writer->WriteArrayEnd();
			}
			Writer->WriteObjectEnd();
		}
		Writer->WriteArrayEnd();
		Writer->WriteObjectEnd();
		UE_LOG(LogTemp, Display, TEXT("SpellbladeGripFrames %s source attachment variation %.3f cm, %.2f deg; palm %.2f deg, from Ready %.2f deg; calibrated mount error %.2f..%.2f deg"),
			Takes[Index].Name, MaxPosition, MaxHandRotation, MaxPalmRotation, MaxFromReady, MinMountError, MaxMountError);
		UE_LOG(LogTemp, Display, TEXT("SpellbladeGripFrames %s source/raw wrist bend %.2f/%.2f deg, source grip drift %.2f cm, tip original/old/readyMount %.2f/%.2f/%.2f cm"),
			Takes[Index].Name, MaxSourceBend, MaxRawBend, MaxGripShift, MinTip.X, MinTip.Y, MinTip.Z);
		UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponAnchor %s target track=%d travel %.2f cm, source-chest target error %.2f cm"),
			Takes[Index].Name, (*Raw)->GetDataModel()->IsValidBoneTrackName(Anchor), AnchorTravel, AnchorError);
		UE_LOG(LogTemp, Display, TEXT("SpellbladeLegacyWeapon %s target track=%d original-key rotation error %.2f deg, tip current/copied %.2f/%.2f cm"),
			Takes[Index].Name, (*Raw)->GetDataModel()->IsValidBoneTrackName(TEXT("Bip_Weapon_R")),
			LegacyWeaponError, LegacyTips.X, LegacyTips.Y);
	}
	Writer->WriteArrayEnd();
	Writer->WriteObjectEnd();
	return Writer->Close() && FFileHelper::SaveStringToFile(Json,
		*(FPaths::ProjectSavedDir() / TEXT("SpellbladeCombatNext/SpellbladeActionArcGripFrames.json")));
}

bool ConfigureLongbladeSourceDrive(const FString& Params, const UProject_JCombatStyleDefinition& Style)
{
	auto* Body = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* Item = Asset<UProject_JEquipmentItemDefinition>(SpellbladeDataRoot + TEXT("Animation_Profiles/Equip/DA_Spellblade_Equip"));
	auto* Profile = Item ? Item->WeaponPresentationProfile.Get() : nullptr;
	const auto* Defaults = StanceBodyDefaults();
	const auto* Hands = Defaults ? Defaults->HandGripProfile.Get() : nullptr;
	if (!Body || !Weapon || !Profile || !Hands || Item->CombatStyleDefinition.Get() != &Style
		|| Profile->DrawnAttachmentMode != EProject_JDrawnAttachmentMode::PrimaryGripContact) { return false; }
	const FName SourceBone(TEXT("Bip_Weapon_R")), SourceSocket(TEXT("WeaponSource_Longblade")), Hand(TEXT("hand_r"));
	const auto& Ref = Body->GetRefSkeleton();
	const int32 SourceIndex = Ref.FindBoneIndex(SourceBone), HandIndex = Ref.FindBoneIndex(Hand);
	const auto* Palm = Body->FindSocket(Hands->Calibration.PrimaryPalmSocketName);
	const auto* Grip = Weapon->FindSocket(Profile->MotionPresentation.PrimaryGripSocketName);
	const auto* Tip = Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip"));
	const auto& WeaponRef = Weapon->GetRefSkeleton();
	if (SourceIndex == INDEX_NONE || HandIndex == INDEX_NONE || !Palm || Palm->BoneName != Hand
		|| !Grip || !Tip || WeaponRef.GetNum() == 0 || Grip->BoneName != WeaponRef.GetBoneName(0) || Tip->BoneName != Grip->BoneName)
	{
		return false;
	}
	const FTransform WeaponRoot = WeaponRef.GetRefBonePose()[0];
	const FTransform SourceRef = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, SourceIndex);
	const FTransform HandRef = FAnimationRuntime::GetComponentSpaceTransformRefPose(Ref, HandIndex);
	const FTransform GripInRoot = Hands->Calibration.PrimaryHandOffset * Grip->GetSocketLocalTransform() * WeaponRoot;
	if (!Project_J::Animation::IsPositiveUniformContactScale(WeaponRoot.GetScale3D())
		|| !Project_J::Animation::IsPositiveUniformContactScale(SourceRef.GetScale3D())) { return false; }
	FTransform Mount = WeaponRoot.Inverse();
	// Keep the authored weapon-bone orientation. Calibrate the origin to
	// the actual palm and grip markers in the rig's reference pose.
	const FVector PalmInSource = SourceRef.InverseTransformPosition(HandRef.TransformPosition(Palm->RelativeLocation));
	const FVector Bias = PalmInSource - Mount.TransformPosition(GripInRoot.GetLocation());
	if (Bias.ContainsNaN() || Bias.Size() > 12.) { return false; }
	Mount.AddToTranslation(Bias);
	const FVector TipInSource = Mount.TransformPosition(WeaponRoot.TransformPosition(Tip->RelativeLocation));
	const auto* ExistingSocket = Body->FindSocket(SourceSocket);
	if (ExistingSocket && (ExistingSocket->BoneName != SourceBone
		|| !ExistingSocket->GetSocketLocalTransform().Equals(Mount, .001))) { return false; }

	struct FEdit
	{
		UProject_JAttackDefinition* Attack;
		UProject_JAnimNotifyState_MeleeHit* Hit;
	};
	TArray<FEdit, TInlineAllocator<3>> Edits;
	for (int32 Index = 1; Index <= 3; ++Index)
	{
		const auto* Node = Style.ComboDefinition->FindNode(Tag(FString::Printf(TEXT("Combo.Spellblade.Light.%d"), Index)));
		auto* Attack = Node ? Node->AttackDefinition.Get() : nullptr;
		auto* Montage = Attack ? Attack->Montage.Get() : nullptr;
		if (!Montage || Attack->MeleeTrajectory.bEnabled || Montage->SlotAnimTracks.Num() != 1
			|| Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1) { return false; }
		const auto* Clip = Cast<UAnimSequence>(Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0].GetAnimReference());
		if (!Clip || Clip->GetSkeleton() != Body->GetSkeleton() || !Clip->GetDataModel()->IsValidBoneTrackName(SourceBone)) { return false; }
		UProject_JAnimNotifyState_MeleeHit* Hit = nullptr;
		for (const auto& Notify : Montage->Notifies)
		{
			if (auto* Candidate = Cast<UProject_JAnimNotifyState_MeleeHit>(Notify.NotifyStateClass))
			{
				if (Hit) { return false; }
				Hit = Candidate;
			}
		}
		if (!Hit) { return false; }
		Edits.Add({Attack, Hit});
	}
	auto& Motion = Profile->MotionPresentation;
	const auto* AttackSet = Style.GetRuntimeAttackSet();
	if (!AttackSet) { return false; }
	for (const auto& Attack : AttackSet->Attacks)
	{
		if (!Attack || !Attack->bMontageDriven || Edits.ContainsByPredicate([&](const FEdit& Edit) { return Edit.Attack == Attack.Get(); }))
		{
			continue;
		}
		if (Attack->WeaponDrive == EProject_JAttackWeaponDrive::SourceAnimation
			|| (Motion.bSupportsIndependentMotion && Motion.bSourceDrivenMontageAttacks
				&& Attack->WeaponDrive == EProject_JAttackWeaponDrive::WeaponDefault))
		{
			UE_LOG(LogTemp, Error, TEXT("SpellbladeSourceDrive shared source motion is already used by %s"), *Attack->GetPathName());
			return false;
		}
	}
	const bool bApplied = ExistingSocket && Profile->DrawnSocketName == SourceSocket && Profile->VisualDrawnSocketName == Hand
		&& Motion.bSupportsIndependentMotion && !Motion.bSourceDrivenMontageAttacks
		&& !Edits.ContainsByPredicate([&](const FEdit& Edit)
		{
			const auto& Attack = *Edit.Attack;
			return Attack.WeaponDrive != EProject_JAttackWeaponDrive::SourceAnimation || !Attack.bOverrideGripIK
				|| Attack.PrimaryGripIKAlpha != 0.f || Attack.SecondaryGripIKAlpha != 0.f || !Attack.HitSpec.bUseCanonicalBladeTipOffset
				|| !Attack.HitSpec.CanonicalBladeTipOffset.Equals(TipInSource, .001) || Edit.Hit->GetTraceSocketName() != SourceBone;
		});
	TArray<UObject*, TInlineAllocator<8>> Packages = {Body, Profile};
	for (const FEdit& Edit : Edits) { Packages.Add(Edit.Attack); Packages.Add(Edit.Attack->Montage.Get()); }
	for (const auto* PackageAsset : Packages)
	{
		UE_LOG(LogTemp, Display, TEXT("SpellbladeSourceDrivePackage %s"), *PackageAsset->GetOutermost()->GetName());
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeSourceDrive applied=%d grip calibration %.3f cm, trace %s"),
		bApplied, Bias.Size(), *TipInSource.ToCompactString());
	if (bApplied) { return true; }
	if (!FParse::Param(*Params, TEXT("ApplyLongbladeSourceDrive")))
	{
		return FParse::Param(*Params, TEXT("TrialLongbladeSourceDrive"));
	}
	const auto* SocketProperty = FindFProperty<FNameProperty>(UProject_JAnimNotifyState_MeleeHit::StaticClass(), TEXT("SocketName"));
	if (!SocketProperty || !UProject_JAssetMigrationLibrary::AddMeshSocket(Body, SourceSocket, SourceBone, Mount)) { return false; }
	Profile->Modify();
	Profile->DrawnSocketName = SourceSocket;
	Profile->VisualDrawnSocketName = Hand;
	Motion.bSupportsIndependentMotion = true;
	Motion.bSourceDrivenMontageAttacks = false;
	for (const FEdit& Edit : Edits)
	{
		auto& Attack = *Edit.Attack;
		Attack.Modify();
		Attack.Montage->Modify();
		Edit.Hit->Modify();
		Attack.WeaponDrive = EProject_JAttackWeaponDrive::SourceAnimation;
		Attack.bOverrideGripIK = true;
		Attack.PrimaryGripIKAlpha = Attack.SecondaryGripIKAlpha = 0.f;
		Attack.HitSpec.bUseCanonicalBladeTipOffset = true;
		Attack.HitSpec.CanonicalBladeTipOffset = TipInSource;
		SocketProperty->SetPropertyValue_InContainer(Edit.Hit, SourceBone);
	}
	for (auto* PackageAsset : Packages) { if (!Save(PackageAsset)) { return false; } }
	return true;
}

bool FitLongbladeSheatheArc(UAnimSequence& Clip, USkeletalMesh& Body, FVector TipInHand, FVector GripInHand,
	const FAnimSegment& Segment, const FAnimNotifyEvent& Cue, bool bApply)
{
	const FName Hand(TEXT("hand_r")), Lower(TEXT("lowerarm_r"));
	const auto& Ref = Body.GetRefSkeleton();
	const int32 LowerIndex = Ref.FindBoneIndex(Lower), HandIndex = Ref.FindBoneIndex(Hand);
	if (LowerIndex <= 0 || HandIndex <= 0 || Ref.GetParentIndex(HandIndex) != LowerIndex) { return false; }
	const FName Parent = Ref.GetBoneName(Ref.GetParentIndex(LowerIndex));
	const double Handoff = Segment.ConvertTrackPosToAnimPos(Cue.GetTriggerTime());
	const double End = Handoff - .40;
	constexpr double Fade = .08;
	const double FullUntil = End - Fade;
	// Reserve the clearance lost when the backward locomotion pose is blended.
	constexpr double KeyHeight = 34.;
	constexpr double ContinuousHeight = 30.;
	if (End <= Segment.AnimStartTime || Clip.GetPlayLength() < Segment.AnimEndTime) { return false; }

	const auto* Model = Clip.GetDataModel();
	if (!Model || !Model->IsValidBoneTrackName(Lower)) { return false; }
	TArray<FTransform> Keys;
	Model->GetBoneTrackTransforms(Lower, Keys);
	if (Keys.Num() != Model->GetNumberOfKeys() || Keys.Num() < 2) { return false; }
	FAnimPoseEvaluationOptions Options;
	Options.OptionalSkeletalMesh = &Body;
	Options.bShouldRetarget = false;
	const auto Read = [](const FAnimPose& Pose, FName Bone)
	{
		return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World);
	};
	const int32 Intervals = FMath::CeilToInt(Handoff * 120.);
	TArray<double> Times;
	for (int32 Index = 0; Index <= Intervals; ++Index) { Times.Add(Handoff * Index / Intervals); }
	double ExistingHeight = TNumericLimits<double>::Max();
	double ExistingGlobalHeight = ExistingHeight;
	for (const double Time : Times)
	{
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Time, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		const double Height = Read(Pose, Hand).TransformPosition(TipInHand).Z;
		ExistingGlobalHeight = FMath::Min(ExistingGlobalHeight, Height);
		if (Time <= FullUntil) { ExistingHeight = FMath::Min(ExistingHeight, Height); }
	}
	if (ExistingHeight >= ContinuousHeight && ExistingGlobalHeight >= 6.)
	{
		UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc %s already clears at %.2f cm"), *Clip.GetName(), ExistingHeight);
		return true;
	}
	double WorstAngle = 0.;
	int32 Changed = 0;
	for (int32 Key = 0; Key < Keys.Num(); ++Key)
	{
		const double Time = Model->GetFrameRate().AsSeconds(FFrameTime(Key));
		if (Time > FullUntil + Fade) { break; }
		const double Weight = 1. - FMath::SmoothStep(FullUntil, FullUntil + Fade, Time);
		FAnimPose Pose;
		UAnimPoseExtensions::GetAnimPoseAtFrame(&Clip, Key, Options, Pose);
		if (!Pose.IsValid()) { return false; }
		const FTransform Wrist = Read(Pose, Hand);
		const FVector Tip = Wrist.TransformPosition(TipInHand);
		const FTransform Original = Read(Pose, Lower);
		FTransform Elbow = Original;
		FQuat Raise;
		if (!RaiseWeaponTipAtElbow(Tip, KeyHeight, Elbow, Raise)) { return false; }
		if (Raise.IsIdentity() || Weight <= 0.) { continue; }
		Elbow.SetRotation(FQuat::Slerp(Original.GetRotation(), Elbow.GetRotation(), Weight).GetNormalized());
		WorstAngle = FMath::Max(WorstAngle, FMath::RadiansToDegrees(Raise.GetAngle()) * Weight);
		Keys[Key] = Elbow.GetRelativeTransform(Read(Pose, Parent));
		++Changed;
	}
	if (Changed == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("SpellbladeWeaponArc %s needs clearance outside its editable window"), *Clip.GetName());
		return false;
	}
	auto* Candidate = DuplicateObject<UAnimSequence>(&Clip, GetTransientPackage());
	if (!Candidate || !SetTrack(*Candidate, Lower, Keys)) { return false; }

	FVector Before(TNumericLimits<double>::Max()), After = Before;
	double MaxWristShift = 0., MaxHandoffShift = 0., MinTime = 0.;
	double EditableHeight = TNumericLimits<double>::Max();
	for (const double Time : Times)
	{
		FAnimPose Original, Adjusted;
		UAnimPoseExtensions::GetAnimPoseAtTime(&Clip, Time, Options, Original);
		UAnimPoseExtensions::GetAnimPoseAtTime(Candidate, Time, Options, Adjusted);
		if (!Original.IsValid() || !Adjusted.IsValid()) { return false; }
		const FTransform OldHand = Read(Original, Hand), NewHand = Read(Adjusted, Hand);
		const auto Measure = [&](const FTransform& Wrist)
		{
			const FVector Tip = Wrist.TransformPosition(TipInHand), Grip = Wrist.TransformPosition(GripInHand);
			const FVector Head = Read(Original, TEXT("Bip01_Head")).GetLocation();
			const FVector Neck = Read(Original, TEXT("Bip01_Neck")).GetLocation();
			return FVector(FVector::Distance(Head, FMath::ClosestPointOnSegment(Head, Grip, Tip)),
				FVector::Distance(Neck, FMath::ClosestPointOnSegment(Neck, Grip, Tip)), Tip.Z);
		};
		Before = Before.ComponentMin(Measure(OldHand));
		const FVector NewClearance = Measure(NewHand);
		if (NewClearance.Z < After.Z) { MinTime = Time; }
		After = After.ComponentMin(NewClearance);
		if (Time <= FullUntil) { EditableHeight = FMath::Min(EditableHeight, NewClearance.Z); }
		MaxWristShift = FMath::Max(MaxWristShift, FVector::Distance(OldHand.GetLocation(), NewHand.GetLocation()));
		if (Time > End + .05)
		{
			MaxHandoffShift = FMath::Max(MaxHandoffShift,
				FVector::Distance(OldHand.TransformPosition(TipInHand), NewHand.TransformPosition(TipInHand)));
		}
	}
	UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc fit %s tip %.2f -> %.2f at %.3f (editable %.2f), head %.2f -> %.2f, neck %.2f -> %.2f, elbow %.2f deg, wrist %.2f, handoff %.3f, keys %d"),
		*Clip.GetName(), Before.Z, After.Z, MinTime, EditableHeight, Before.X, After.X, Before.Y, After.Y, WorstAngle, MaxWristShift,
		MaxHandoffShift, Changed);
	// The source is sampled at its native key rate. Keep the continuous
	// trajectory above the reserved clearance even between those keys.
	const bool bValid = After.Z >= 6. && EditableHeight >= ContinuousHeight && After.X >= Before.X - 1. && After.Y >= Before.Y - 1.
		&& WorstAngle <= 28. && MaxWristShift <= 15. && MaxHandoffShift <= .02;
	return bValid && (!bApply || (SetTrack(Clip, Lower, Keys) && Save(&Clip)));
}

bool AuditLongbladeWeaponArc(const FString& Params, const UProject_JCombatStyleDefinition& Style)
{
	const FStanceTake Takes[] = {
		{TEXT("BasicCut01"), TEXT("01_01_att_normal_01")},
		{TEXT("Sheathe"), TEXT("stand_weapon_in_00")}
	};
	auto* Legacy = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Spellblade_Woman"));
	auto* Current = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	const auto* Weapon = Asset<USkeletalMesh>(TEXT("/Game/Weapons/Spellblade/SK_Spellblade_longblade"));
	const auto* Tip = Weapon ? Weapon->FindSocket(TEXT("WeaponGroundProbe_Tip")) : nullptr;
	const auto* Grip = Weapon ? Weapon->FindSocket(TEXT("WeaponGrip_R_Aligned")) : nullptr;
	const auto* Light = Style.ComboDefinition->FindNode(Tag(TEXT("Combo.Spellblade.Light.1")));
	const auto* Attack = Light ? Light->AttackDefinition.Get() : nullptr;
	const auto* Sheathe = Asset<UAnimMontage>(Montages + TEXT("Equipment/AM_SpellbladeKit_Sheathe"));
	FTransform WeaponInHand;
	if (!Legacy || !Current || !Tip || !Grip || !Attack || !Attack->Montage || !Sheathe
		|| !ResolveStanceWeaponMount(*Current, *Weapon, WeaponInHand)) { return false; }
	const bool bApply = FParse::Param(*Params, TEXT("ApplyLongbladeWeaponArc"));
	if (bApply || FParse::Param(*Params, TEXT("TrialLongbladeWeaponArc")))
	{
		FString Role;
		if (!FParse::Value(*Params, TEXT("ArcRole="), Role)) { return false; }
		if (Role != TEXT("Sheathe") || Sheathe->SlotAnimTracks.Num() != 1
			|| Sheathe->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1) { return false; }
		const auto& Segment = Sheathe->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		auto* Clip = Cast<UAnimSequence>(Segment.GetAnimReference());
		const auto* Cue = Sheathe->Notifies.FindByPredicate([](const FAnimNotifyEvent& Notify)
		{
			return Notify.NotifyName == TEXT("Project_JWeaponPresentation");
		});
		return Clip && Cue && FitLongbladeSheatheArc(*Clip, *Current,
			(Tip->GetSocketLocalTransform() * WeaponInHand).GetLocation(), (Grip->GetSocketLocalTransform() * WeaponInHand).GetLocation(),
			Segment, *Cue, bApply);
	}

	const FName SourceWeapon(TEXT("Bip_Weapon_R"));
	if (Legacy->GetRefSkeleton().FindBoneIndex(SourceWeapon) == INDEX_NONE) { return false; }
	UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc original probe bone=%s, current weapon root=%s"),
		*SourceWeapon.ToString(), *Weapon->GetRefSkeleton().GetBoneName(0).ToString());
	UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc contact Attack=%s baked=%d offset=%s"),
		*Attack->GetPathName(), Attack->MeleeTrajectory.bEnabled, *Attack->HitSpec.CanonicalBladeTipOffset.ToCompactString());

	TArray<UAnimSequence*> Original;
	const auto Raw = RetargetStanceTakes(Params, Takes, TEXT("WeaponArcAudit_"), false, StanceFolder, nullptr, &Original);
	if (Original.Num() != UE_ARRAY_COUNT(Takes) || Raw.Num() != UE_ARRAY_COUNT(Takes)) { return false; }

	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Takes); ++Index)
	{
		const FString Role = Takes[Index].Name;
		const auto* Montage = Index == 0 ? Attack->Montage.Get() : Sheathe;
		if (Montage->SlotAnimTracks.Num() != 1 || Montage->SlotAnimTracks[0].AnimTrack.AnimSegments.Num() != 1) { return false; }
		const auto& Segment = Montage->SlotAnimTracks[0].AnimTrack.AnimSegments[0];
		const auto* Authored = Cast<UAnimSequence>(Segment.GetAnimReference());
		const FString ExpectedRaw = TEXT("WeaponArcAudit_") + Role;
		const auto* RawClip = Raw.FindByPredicate([&](const UAnimSequence* Clip) { return Clip->GetName() == ExpectedRaw; });
		if (!Authored || !RawClip) { return false; }
		UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc %s segment track=%.3f..%.3f clip=%.3f..%.3f rate=%.3f clipRate=%.3f"),
			*Role, Segment.StartPos, Segment.GetEndPos(), Segment.AnimStartTime, Segment.AnimEndTime,
			Segment.AnimPlayRate, Authored->RateScale);
		float HitStart = 0.f, HitEnd = 0.f;
		for (const auto& Notify : Montage->Notifies)
		{
			UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc %s notify=%s %.3f..%.3f clip=%.3f..%.3f"),
				*Role, *Notify.NotifyName.ToString(), Notify.GetTriggerTime(), Notify.GetEndTriggerTime(),
				Segment.ConvertTrackPosToAnimPos(Notify.GetTriggerTime()), Segment.ConvertTrackPosToAnimPos(Notify.GetEndTriggerTime()));
			if (const auto* Hit = Cast<UProject_JAnimNotifyState_MeleeHit>(Notify.NotifyStateClass))
			{
				HitStart = Segment.ConvertTrackPosToAnimPos(Notify.GetTriggerTime());
				HitEnd = Segment.ConvertTrackPosToAnimPos(Notify.GetEndTriggerTime());
				UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc %s trace socket=%s"), *Role, *Hit->GetTraceSocketName().ToString());
			}
		}
		const UAnimSequence* Clips[] = {Original[Index], *RawClip, Authored};
		for (int32 Stage = 0; Stage < UE_ARRAY_COUNT(Clips); ++Stage)
		{
			const bool bSource = Stage == 0;
			const auto* Clip = Clips[Stage];
			FAnimPoseEvaluationOptions Options;
			Options.OptionalSkeletalMesh = bSource ? Legacy : Current;
			Options.bShouldRetarget = false;
			const FName Anchor = bSource ? SourceWeapon : FName(TEXT("hand_r"));
			const FName FootL = bSource ? FName(TEXT("Bip01-L-Toe0")) : FName(TEXT("ball_l"));
			const FName FootR = bSource ? FName(TEXT("Bip01-R-Toe0")) : FName(TEXT("ball_r"));
			const FTransform Mount = bSource ? FTransform::Identity : WeaponInHand;
			const FVector Point = (Tip->GetSocketLocalTransform() * Mount).GetLocation();
			const FVector GripPoint = (Grip->GetSocketLocalTransform() * Mount).GetLocation();
			const double Duration = FMath::Min(Clip->GetPlayLength(), double(Segment.AnimEndTime));
			const int32 Samples = FMath::CeilToInt(Duration * 120.);
			if (Samples <= 0 || Options.OptionalSkeletalMesh->GetRefSkeleton().FindBoneIndex(FootL) == INDEX_NONE
				|| Options.OptionalSkeletalMesh->GetRefSkeleton().FindBoneIndex(FootR) == INDEX_NONE)
			{
				UE_LOG(LogTemp, Error, TEXT("SpellbladeWeaponArc invalid feet on %s: %s, %s"),
					*GetNameSafe(Options.OptionalSkeletalMesh), *FootL.ToString(), *FootR.ToString());
				return false;
			}
			FVector MinTip(0., 0., TNumericLimits<double>::Max());
			double MinTime = 0., MinRelative = TNumericLimits<double>::Max(), MinHit = TNumericLimits<double>::Max();
			FVector GripAtMin, FeetAtMin;
			FString Timeline;
			for (int32 Sample = 0; Sample <= Samples; ++Sample)
			{
				const double Time = Duration * Sample / Samples;
				FAnimPose Pose;
				UAnimPoseExtensions::GetAnimPoseAtTime(Clip, Time, Options, Pose);
				if (!Pose.IsValid()) { return false; }
				const auto Read = [&](FName Bone) { return UAnimPoseExtensions::GetBonePose(Pose, Bone, EAnimPoseSpaces::World); };
				const auto Transform = Read(Anchor);
				const FVector Position = Transform.TransformPosition(Point);
				const FVector Feet(Read(FootL).GetLocation().Z, Read(FootR).GetLocation().Z, 0.);
				if (Sample % 6 == 0) { Timeline += FString::Printf(TEXT(" %.2f:%.1f"), Time, Position.Z); }
				MinRelative = FMath::Min(MinRelative, Position.Z - FMath::Min(Feet.X, Feet.Y));
				if (Time >= HitStart && Time <= HitEnd && HitEnd > HitStart) { MinHit = FMath::Min(MinHit, Position.Z); }
				if (Position.Z >= MinTip.Z) { continue; }
				MinTip = Position;
				MinTime = Time;
				GripAtMin = Transform.TransformPosition(GripPoint);
				FeetAtMin = Feet;
			}
			UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc %s stage=%d clip=%s tip=%s time=%.3f grip=%s feet=%s relative=%.2f hit=%.2f"),
				*Role, Stage, *Clip->GetName(), *MinTip.ToCompactString(), MinTime, *GripAtMin.ToCompactString(), *FeetAtMin.ToCompactString(),
				MinRelative, HitEnd > HitStart ? MinHit : 0.);
			UE_LOG(LogTemp, Display, TEXT("SpellbladeWeaponArc %s stage=%d height:%s"), *Role, Stage, *Timeline);
		}
	}
	return true;
}

bool AuditCastShoulderStages(const FString& Params)
{
	const bool bApplyCast = FParse::Param(*Params, TEXT("ApplyCastShoulderDeformation"));
	const bool bApplyAction = FParse::Param(*Params, TEXT("ApplyLongbladeActionDeformation"));
	const bool bTrialAction = FParse::Param(*Params, TEXT("TrialLongbladeActionDeformation"));
	const bool bActions = bApplyAction || bTrialAction || FParse::Param(*Params, TEXT("AuditLongbladeActionShoulderStages"));
	if (bApplyCast && bActions) { return false; }
	const bool bApply = bApplyCast || bApplyAction;
	const bool bDeform = bApply || bTrialAction || FParse::Param(*Params, TEXT("TrialCastShoulderDeformation"));
	TConstArrayView<FStanceTake> Takes = bActions ? MakeArrayView(LongbladeActionTakes) : MakeArrayView(CastTakes);
	FString SelectedRole;
	const bool bSelected = FParse::Value(*Params, TEXT("DeformRole="), SelectedRole);
	if (bApplyAction && !bSelected) { return false; }
	if (bSelected)
	{
		const int32 Index = Takes.IndexOfByPredicate([&](const FStanceTake& Take) { return SelectedRole == Take.Name; });
		if (Index == INDEX_NONE) { return false; }
		Takes = Takes.Slice(Index, 1);
	}
	auto* Legacy = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Spellblade_Woman"));
	const auto* LegacyShoulder = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Clothes/sho"));
	auto* Current = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Meshes/SK_Spellblade_Complete"));
	TArray<FName> OriginalBones, CurrentBones;
	if ((!bDeform && (!ExportShoulderSurface(Legacy, TEXT("Legacy"), &OriginalBones)
		|| !ExportShoulderSurface(LegacyShoulder, TEXT("LegacyShoulder"), &OriginalBones)))
		|| !ExportShoulderSurface(Current, bDeform ? TEXT("DeformTrialMesh") : TEXT("Current"), &CurrentBones)) { return false; }
	if (bActions && !bDeform)
	{
		const auto* LegacyUpper = Asset<USkeletalMesh>(TEXT("/Game/Characters/Spellblade/Clothes/upper"));
		if (!ExportShoulderSurface(LegacyUpper, TEXT("LegacyUpper"), &OriginalBones)) { return false; }
	}
	TArray<UAnimSequence*> Source;
	const auto Retargeted = RetargetStanceTakes(Params, Takes, TEXT("ShoulderAudit_"), false, StanceFolder, nullptr, &Source);
	if (Source.Num() != Takes.Num() || Retargeted.Num() != Takes.Num()) { return false; }
	TArray<UAnimSequence*> Pending;
	for (const bool bOriginal : {true, false})
	{
		if (bDeform && bOriginal) { continue; }
		FString Json;
		const auto Writer = TJsonWriterFactory<TCHAR, TCondensedJsonPrintPolicy<TCHAR>>::Create(&Json);
		Writer->WriteObjectStart();
		Writer->WriteNull(TEXT("error"));
		Writer->WriteArrayStart(TEXT("mesh_assets"));
		Writer->WriteValue((bOriginal ? Legacy : Current)->GetPathName());
		Writer->WriteArrayEnd();
		Writer->WriteObjectStart(TEXT("cases"));
		for (int32 I = 0; I < Takes.Num(); ++I)
		{
			const FString Role = Takes[I].Name;
			if (bOriginal)
			{
				if (!WriteShoulderAuditClip(*Writer, Role, *Source[I], *Legacy, OriginalBones)) { return false; }
				continue;
			}
			const FString Expected = TEXT("ShoulderAudit_") + Role;
			UAnimSequence* const* Raw = Retargeted.FindByPredicate([&](const UAnimSequence* Clip) { return Clip->GetName() == Expected; });
			const bool bEquipment = Role == TEXT("Draw") || Role == TEXT("Sheathe");
			const FString Package = bActions ? Sequences + TEXT("DirectBody/")
				+ (bEquipment ? TEXT("Equipment/") : TEXT("CombatRevision/")) + TEXT("AS_SpellbladeDirect_") + Role
				: CastPerformancePath + Role;
			auto* Authored = Asset<UAnimSequence>(Package);
			if (bDeform)
			{
				if (!Authored) { return false; }
				const FString Label = Role + TEXT("_smoothed");
				if (bActions && !WriteShoulderAuditClip(*Writer, Role + TEXT("_authored"), *Authored, *Current, CurrentBones)) { return false; }
				auto* Candidate = bApply ? Authored : DuplicateObject<UAnimSequence>(Authored, GetTransientPackage(),
					MakeUniqueObjectName(GetTransientPackage(), UAnimSequence::StaticClass(), FName(Label)));
				// Fit the restored cast head to this costume's chest. The
				// source pose placed the face one centimetre inside its margin.
				const double HeadFitCm = bActions ? 0. : 1.;
				if (!Candidate || !TransferLongbladeDeformation(*Candidate, *Source[I], *Current, *Legacy, HeadFitCm, bActions)
					|| (!bActions && Role == TEXT("CastLeft") && !RetimeLongbladeCastGaze(*Candidate, *Current))
					|| (bActions && Role == TEXT("BasicCut01") && !FitLongbladeBasicShoulder(*Candidate, *Current))
					|| !WriteShoulderAuditClip(*Writer, Label, *Candidate, *Current, CurrentBones)) { return false; }
				if (bApply) { Pending.Add(Candidate); }
				continue;
			}
			if (!Raw || !Authored || !WriteShoulderAuditClip(*Writer, Role + TEXT("_retarget"), **Raw, *Current, CurrentBones)
				|| !WriteShoulderAuditClip(*Writer, Role + TEXT("_authored"), *Authored, *Current, CurrentBones)) { return false; }
		}
		Writer->WriteObjectEnd();
		Writer->WriteObjectEnd();
		FString Suffix;
		if (bDeform && bSelected)
			Suffix = (bApply ? TEXT("DeformApplied_") : TEXT("DeformTrial_")) + SelectedRole;
		else if (bTrialAction) { Suffix = TEXT("DeformActionTwistClips"); }
		else if (bActions)
		{
			Suffix = bOriginal ? TEXT("OriginalActionClips") : TEXT("DirectActionClips");
			if (bSelected) { Suffix += TEXT("_") + SelectedRole; }
		}
		else if (bDeform) { Suffix = bApply ? TEXT("DeformFittedAppliedClips") : TEXT("DeformFittedTrialClips"); }
		else { Suffix = bOriginal ? TEXT("OriginalClips") : TEXT("DirectClips"); }
		if (!Writer->Close() || !SaveShoulderAudit(Suffix, Json)) { return false; }
	}
	for (auto* Clip : Pending) { if (!Save(Clip)) { return false; } }
	return true;
}
