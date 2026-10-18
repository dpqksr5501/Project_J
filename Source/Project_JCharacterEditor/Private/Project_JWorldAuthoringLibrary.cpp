#include "Project_JWorldAuthoringLibrary.h"
#include "Editor.h"
#include "Landscape.h"
#include "LandscapeInfo.h"
#include "LandscapeSubsystem.h"
#include "Misc/FileHelper.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionRuntimeHash.h"
#include "LandscapeEdit.h"
#include "LandscapeEditLayer.h"
#include "Components/SplineComponent.h"
#include "Components/SplineMeshComponent.h"
#include "Engine/StaticMesh.h"

bool UProject_JWorldAuthoringLibrary::ReimportRegionHeightmap(ALandscape* Landscape, const FString& File, int32 Resolution)
{
    ULandscapeInfo* Info = IsValid(Landscape) ? Landscape->GetLandscapeInfo() : nullptr;
    int32 MinX, MinY, MaxX, MaxY;
    if (!Info || Resolution < 127 || Resolution > 4033 || !Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY)
        || MaxX-MinX+1 != Resolution || MaxY-MinY+1 != Resolution) { return false; }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() != Resolution*Resolution*2) { return false; }
    ULandscapeEditLayerBase* BaseLayer = Landscape->GetEditLayer(0);
    if (!BaseLayer) { return false; }
    TArray<uint16> Heights;
    Heights.SetNumUninitialized(Resolution*Resolution);
    for (int32 Index=0; Index<Heights.Num(); ++Index) { Heights[Index] = uint16(Bytes[Index*2] | (Bytes[Index*2+1]<<8)); }
    Landscape->Modify();
    {
        // The accessor records changed components and requests heightmap/collision
        // updates. Writing only the edit-layer texture leaves the visible terrain stale.
        FHeightmapAccessor<false> Edit(Info);
        Edit.SetEditLayer(BaseLayer->GetGuid());
        Edit.SetData(MinX,MinY,MaxX,MaxY,Heights.GetData());
        Edit.Flush();
    }
    Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_All, true);
    Landscape->MarkPackageDirty();
    return true;
}

bool UProject_JWorldAuthoringLibrary::ImportRegionVisibilityPatch(ALandscape* Landscape, const FString& MaskFile, FIntPoint GridOrigin, FIntPoint Size)
{
    if (!GEditor || !IsValid(Landscape) || Landscape->GetWorld() != GEditor->GetEditorWorldContext().World()
        || Size.X < 2 || Size.Y < 2 || Size.X > 512 || Size.Y > 512) { return false; }
    ULandscapeInfo* Info = Landscape->GetLandscapeInfo();
    int32 MinX, MinY, MaxX, MaxY;
    if (!Info || !Info->GetLandscapeExtent(MinX, MinY, MaxX, MaxY)
        || GridOrigin.X < MinX || GridOrigin.Y < MinY
        || GridOrigin.X > MaxX - Size.X + 1 || GridOrigin.Y > MaxY - Size.Y + 1) { return false; }
    TArray<uint8> Mask;
    if (!FFileHelper::LoadFileToArray(Mask, *MaskFile) || Mask.Num() != Size.X * Size.Y) { return false; }
    Landscape->Modify();
    const FName LayerName(TEXT("ProjectJ_ExplorationVisibility"));
    ULandscapeEditLayerBase* Layer = Landscape->GetEditLayer(LayerName);
    if (!Layer)
    {
        const int32 Index = Landscape->CreateLayer(LayerName);
        if (Index == INDEX_NONE) { return false; }
        Layer = Landscape->GetEditLayer(Index);
    }
    if (!Layer || !ALandscapeProxy::VisibilityLayer) { return false; }
    Layer->Modify();
    Info->UpdateLayerInfoMap(Landscape);
    {
        // The engine accessor requests weightmap AND visibility collision updates.
        // A texture-only write can leave an invisible but solid entrance.
        TAlphamapAccessor<false> Edit(Info, ALandscapeProxy::VisibilityLayer);
        Edit.SetEditLayer(Layer->GetGuid());
        Edit.SetData(GridOrigin.X, GridOrigin.Y, GridOrigin.X + Size.X - 1, GridOrigin.Y + Size.Y - 1,
            Mask.GetData(), ELandscapeLayerPaintingRestriction::None);
        Edit.Flush();
        TArray<uint8> Readback;
        Readback.SetNumZeroed(Mask.Num());
        Edit.GetDataFast(GridOrigin.X, GridOrigin.Y, GridOrigin.X + Size.X - 1, GridOrigin.Y + Size.Y - 1, Readback.GetData());
        if (Readback != Mask)
        {
            UE_LOG(LogTemp, Error, TEXT("Region visibility edit-layer readback differs: %s"), *MaskFile);
            return false;
        }
    }
    Landscape->RequestLayersContentUpdateForceAll(ELandscapeLayerUpdateMode::Update_All, true);
    Landscape->ForceLayersFullUpdate();
    {
        TAlphamapAccessor<false> Merged(Info, ALandscapeProxy::VisibilityLayer);
        Merged.SetEditLayer(FGuid());
        TArray<uint8> Readback;
        Readback.SetNumZeroed(Mask.Num());
        Merged.GetDataFast(GridOrigin.X, GridOrigin.Y, GridOrigin.X + Size.X - 1, GridOrigin.Y + Size.Y - 1, Readback.GetData());
        int32 SourceHoles = 0, MergedHoles = 0;
        for (int32 Index = 0; Index < Mask.Num(); ++Index)
        {
            SourceHoles += Mask[Index] > 127;
            MergedHoles += Readback[Index] > 127;
        }
        UE_LOG(LogTemp, Display, TEXT("Region visibility source/merged hole vertices: %d/%d (%s)"), SourceHoles, MergedHoles, *MaskFile);
        if (SourceHoles > 0 && MergedHoles == 0) { return false; }
    }
    Landscape->MarkPackageDirty();
    return true;
}

AActor* UProject_JWorldAuthoringLibrary::CreateSplineRibbon(const FString& Label, const TArray<FVector>& Points, UMaterialInterface* Material, float WidthCm)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || Points.Num()<2 || Points.Num()>64 || !IsValid(Material) || !FMath::IsFinite(WidthCm) || WidthCm<=0) { return nullptr; }
    for (const FVector& Point : Points) { if (Point.ContainsNaN()) { return nullptr; } }
    UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Plane.Plane"));
    if (!Plane) { return nullptr; }
    AActor* Actor = World->SpawnActor<AActor>();
    if (!Actor) { return nullptr; }
    Actor->SetActorLabel(Label);
    USplineComponent* Spline = NewObject<USplineComponent>(Actor,TEXT("RouteSpline"),RF_Transactional);
    Actor->AddInstanceComponent(Spline);
    Actor->SetRootComponent(Spline);
    Spline->SetMobility(EComponentMobility::Static);
    Spline->RegisterComponent();
    Actor->SetActorLocation(Points[0]);
    Spline->ClearSplinePoints(false);
    for (const FVector& Point : Points) { Spline->AddSplinePoint(Point-Points[0],ESplineCoordinateSpace::Local,false); }
    Spline->UpdateSpline();
    for (int32 Index=0; Index<Points.Num()-1; ++Index)
    {
        USplineMeshComponent* Ribbon = NewObject<USplineMeshComponent>(Actor,NAME_None,RF_Transactional);
        Actor->AddInstanceComponent(Ribbon);
        Ribbon->SetupAttachment(Spline);
        Ribbon->SetMobility(EComponentMobility::Static);
        Ribbon->SetStaticMesh(Plane);
        Ribbon->SetMaterial(0,Material);
        Ribbon->SetForwardAxis(ESplineMeshAxis::X,false);
        Ribbon->SetStartScale(FVector2D(WidthCm/100.f,1),false);
        Ribbon->SetEndScale(FVector2D(WidthCm/100.f,1),false);
        Ribbon->SetStartAndEnd(Spline->GetLocationAtSplinePoint(Index,ESplineCoordinateSpace::Local),Spline->GetTangentAtSplinePoint(Index,ESplineCoordinateSpace::Local),
            Spline->GetLocationAtSplinePoint(Index+1,ESplineCoordinateSpace::Local),Spline->GetTangentAtSplinePoint(Index+1,ESplineCoordinateSpace::Local),false);
        Ribbon->SetCollisionEnabled(ECollisionEnabled::NoCollision);
        Ribbon->SetCanEverAffectNavigation(false);
        Ribbon->SetCastShadow(false);
        Ribbon->RegisterComponent();
        Ribbon->UpdateMesh();
    }
    Actor->MarkPackageDirty();
    return Actor;
}

bool UProject_JWorldAuthoringLibrary::EnableRegionStreaming()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    UWorldPartition* Partition = World ? World->GetWorldPartition() : nullptr;
    if (!Partition) { return false; }
    Partition->SetEnableStreaming(true);
    World->MarkPackageDirty();
    return Partition->IsStreamingEnabled();
}
FString UProject_JWorldAuthoringLibrary::GetRegionRuntimeHashName()
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    const UWorldPartition* Partition = World ? World->GetWorldPartition() : nullptr;
    return Partition && Partition->RuntimeHash ? Partition->RuntimeHash->GetClass()->GetName() : FString();
}

ALandscape* UProject_JWorldAuthoringLibrary::ImportRegionLandscape(const FString& File, int32 Resolution, float XYScale, float ZScale, UMaterialInterface* Material)
{
    UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
    if (!World || Resolution < 127 || Resolution > 4033 || (Resolution-1)%126 || !FMath::IsFinite(XYScale) || !FMath::IsFinite(ZScale) || XYScale <= 0 || ZScale <= 0) { return nullptr; }
    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *File) || Bytes.Num() != Resolution*Resolution*2) { return nullptr; }
    TArray<uint16> Heights;
    Heights.SetNumUninitialized(Resolution*Resolution);
    for (int32 Index=0; Index<Heights.Num(); ++Index) { Heights[Index] = static_cast<uint16>(Bytes[Index*2] | (Bytes[Index*2+1]<<8)); }
    ALandscape* Landscape = World->SpawnActor<ALandscape>();
    if (!Landscape) { return nullptr; }
    const float HalfExtent = (Resolution-1)*XYScale*.5f;
    Landscape->SetActorTransform(FTransform(FRotator::ZeroRotator, FVector(-HalfExtent,-HalfExtent,0), FVector(XYScale,XYScale,ZScale)));
    Landscape->LandscapeMaterial = Material;
    TMap<FGuid,TArray<uint16>> HeightData;
    HeightData.Add(FGuid(), MoveTemp(Heights));
    TMap<FGuid,TArray<FLandscapeImportLayerInfo>> Layers;
    Layers.Add(FGuid(), {});
    Landscape->Import(FGuid::NewGuid(),0,0,Resolution-1,Resolution-1,2,63,HeightData,*File,Layers,ELandscapeImportAlphamapType::Additive,{});
    Landscape->SetActorLabel(FString::Printf(TEXT("World_Landscape_%dm"), FMath::RoundToInt((Resolution-1)*XYScale/100.f)));
    if (World->GetWorldPartition()) { World->GetSubsystem<ULandscapeSubsystem>()->ChangeGridSize(Landscape->GetLandscapeInfo(), 2); }
    Landscape->MarkPackageDirty();
    return Landscape;
}
