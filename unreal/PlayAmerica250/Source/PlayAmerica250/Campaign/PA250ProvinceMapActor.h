// PA250ProvinceMapActor.h — sole campaign map layer (de Costa engraving + hotspot markers + tokens).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250ProvinceMapActor.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;
class UTexture2D;
class UTextRenderComponent;
class APA250ArmyToken;
class UPA250Subsystem;
class ACameraActor;

UCLASS()
class PLAYAMERICA250_API APA250ProvinceMapActor : public AActor
{
	GENERATED_BODY()

public:
	APA250ProvinceMapActor();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual bool ShouldTickIfViewportsOnly() const override { return true; }

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "PA250")
	TObjectPtr<UStaticMeshComponent> MapMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PA250")
	TObjectPtr<UMaterialInterface> BaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PA250")
	float MapWidthUU = 1600.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "PA250")
	// Derived from de Costa image_size at load (default ≈ 6761/5314).
	float MapHeightUU = 1257.f;

	/** World Z of map plane (actors sit slightly above). */
	UPROPERTY(EditAnywhere, Category = "PA250")
	float MapPlaneZ = 0.f;

	FString ProvinceAtUV(FVector2D UV) const;
	FString ProvinceAtWorld(const FVector& World) const;
	/** Army whose round counter is under this world point, or empty. */
	FString ArmyAtWorld(const FVector& World) const;
	FString ProvinceForArmy(const FString& ArmyId) const;
	bool WorldToUV(const FVector& World, FVector2D& OutUV) const;
	FVector UVToWorld(FVector2D UV, float ZOffset = 2.f) const;

	void OnHudSelectionChanged(const FString& ArmyId, const FString& ProvinceId);
	/** Multiply zoom (centred on the selected province, else map centre) and re-frame. */
	void ZoomBy(float Factor);
	/** Cursor-centred zoom: the UV under the cursor stays under the cursor. */
	void ZoomAtUV(FVector2D CursorUV, float Factor);
	/** Frame the selected province (or overview when nothing is selected). */
	void FocusSelected();
	float GetZoomFactor() const { return ZoomFactor; }
	FVector2D GetZoomCenterUV() const { return ZoomCenterUV; }
	void ResetOverview();
	void PanByUV(FVector2D DeltaUV);
	void HandleProvinceClick(const FString& ProvinceId);
	void HandleRightClick();
	void FrameCamera();

	/** Framing helpers for tests / camera. */
	FVector GetLabelWorld(const FString& ProvinceId) const;
	FVector GetTokenWorld(const FString& ProvinceId) const;
	FBox GetCartoucheWorldBox() const;
	TArray<FString> GetAllProvinceIds() const;

	bool IsPresentationLoaded() const { return bPresentationLoaded; }
	bool IsDeCostaMode() const { return bDeCostaMode; }
	UTexture2D* GetDisplayTexture() const { return DisplayTex; }
	int32 GetImageWidthPx() const { return ImageW; }
	int32 GetImageHeightPx() const { return ImageH; }
	bool IsProvinceVerified(const FString& ProvinceId) const;

	/** Rotation that lays TextRender flat and reads left-to-right, north up. */
	static FRotator FlatMapTextRotation();
	/** Centre of the opening campaign view: Boston–Cambridge–Lexington–Concord. */
	static FVector2D CampaignOverviewCenterUV() { return FVector2D(0.337f, 0.333f); }
	/** Zoom at which that relationship fills the usable viewport without excessive magnification. */
	static float CampaignOverviewZoom(float ViewW, float ViewH, float MapW, float MapH);
	/** Ortho width that fits the whole plane between the HUD strips at a viewport size. */
	static float FitOrthoWidth(float ViewW, float ViewH, float MapW, float MapH);
	static float ZoomedOrthoWidth(float ViewW, float ViewH, float MapW, float MapH, float Zoom)
	{
		return FitOrthoWidth(ViewW, ViewH, MapW, MapH) / FMath::Max(1.f, Zoom);
	}

protected:
	void LoadTextures();
	void LoadPresentation();
	bool LoadDeCostaLocations();
	void EnsureMaterial();
	void RebuildStateTexture();
	void SpawnLabels();
	void SpawnWaterLabels();
	void SpawnFramingDecor();
	void SpawnHotspotMarkers();
	void RefreshHotspotVisibility();
	void RefreshTownOwnership();
	void RefreshArmyTokens();
	void RefreshBattleMarker();
	void RefreshRoutes();
	void ApplyCameraPostProcess();
	UTexture2D* LoadPngToTexture(const FString& AbsPath);
	UTexture2D* LoadJpegToTexture(const FString& AbsPath);
	UPA250Subsystem* Sub() const;

	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> DynMat;
	UPROPERTY()
	TObjectPtr<UTexture2D> ProvinceIdTex;
	UPROPERTY()
	TObjectPtr<UTexture2D> BorderDistTex;
	UPROPERTY()
	TObjectPtr<UTexture2D> CoastDistTex;
	UPROPERTY()
	TObjectPtr<UTexture2D> PaperTex;
	UPROPERTY()
	TObjectPtr<UTexture2D> DisplayTex;
	UPROPERTY()
	TObjectPtr<UTexture2D> StateTex;
	UPROPERTY()
	TObjectPtr<ACameraActor> MapCamera;

	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> ProvinceLabels;
	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> WaterLabels;
	UPROPERTY()
	TObjectPtr<UTextRenderComponent> CartoucheText;
	UPROPERTY()
	TObjectPtr<UTextRenderComponent> CompassText;
	UPROPERTY()
	TArray<TObjectPtr<APA250ArmyToken>> ArmyTokens;
	UPROPERTY()
	TObjectPtr<UTextRenderComponent> BattleMarker;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> HotspotMarkers;
	UPROPERTY()
	TArray<TObjectPtr<UTextRenderComponent>> HotspotCaptions;
	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> SelectionHalo;
	TMap<FString, UTextRenderComponent*> CaptionById;

	TMap<FString, FVector2D> LabelUV;
	TMap<FString, FVector2D> TokenUV;
	TMap<FString, float> HitRadiusPx;
	TMap<FString, bool> VerifiedById;
	TMap<FString, int32> MapIndexById;
	TMap<int32, FString> IdByMapIndex;
	struct FEdgeVis
	{
		FString From;
		FString To;
		FString Type; // road | ferry | boat
		TArray<FVector2D> PolyUV;
	};
	TArray<FEdgeVis> Edges;

	TArray<uint8> LookupBytes;
	int32 LookupW = 0;
	int32 LookupH = 0;

	FString SelectedArmyId;
	FString SelectedProvinceId;
	float ZoomFactor = 1.f;
	float MaxZoomFactor = 6.f;
	FVector2D ZoomCenterUV = FVector2D(0.5f, 0.5f);
	int32 TexW = 0;
	int32 TexH = 0;
	int32 ImageW = 0;
	int32 ImageH = 0;
	bool bPresentationLoaded = false;
	bool bDeCostaMode = false;
	FString LastTokenSig;
};
