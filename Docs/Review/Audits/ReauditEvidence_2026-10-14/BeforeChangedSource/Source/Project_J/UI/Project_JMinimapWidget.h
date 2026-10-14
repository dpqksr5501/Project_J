#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Engine/DataAsset.h"
#include "Project_JMinimapWidget.generated.h"

USTRUCT(BlueprintType)
struct FProject_JMapMarker
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FVector Position = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FText Label;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) FLinearColor Color = FLinearColor(1.f, 0.8f, 0.2f);
};
UCLASS(BlueprintType)
class PROJECT_J_API UProject_JMapDefinition : public UDataAsset
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FText MapName;
	/** X points north/up, Y points east/right. Bounds and texture share this convention. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector2D Center = FVector2D::ZeroVector;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FVector2D HalfExtent = FVector2D(5000, 5000);
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TSoftObjectPtr<UTexture2D> Texture;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TArray<FProject_JMapMarker> Markers;
	/** Optional top-down bounds of static level geometry; authored once, never queried at runtime. */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) TArray<FBox2D> Regions;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly) FName LevelName;
	static bool WorldToUV(FVector Position, FVector2D Center, FVector2D HalfExtent, FVector2D &OutUV);
};
UCLASS(Blueprintable)
class PROJECT_J_API UProject_JMinimapWidget : public UUserWidget
{
	GENERATED_BODY()
  public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite) TObjectPtr<UProject_JMapDefinition> Definition;
	void SetQuestMarkers(TArray<FProject_JMapMarker> InMarkers);
	void SetZoom(float Value);

  protected:
	virtual TSharedRef<SWidget> RebuildWidget() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	virtual int32 NativePaint(const FPaintArgs &, const FGeometry &, const FSlateRect &, FSlateWindowElementList &,
							  int32, const FWidgetStyle &, bool) const override;
	virtual FReply NativeOnMouseWheel(const FGeometry &, const FPointerEvent &) override;

  private:
	void Sample();
	FTimerHandle SampleTimer;
	TArray<FProject_JMapMarker> QuestMarkers;
	FVector PlayerPosition = FVector::ZeroVector;
	float PlayerYaw = 0;
	float Zoom = 1;
	bool bHasPlayer = false;
	UPROPERTY() TObjectPtr<UTexture2D> MapTexture;
	TSharedPtr<struct FStreamableHandle> LoadHandle;
	FSlateBrush TextureBrush;
};
