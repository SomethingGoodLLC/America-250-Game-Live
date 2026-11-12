// PA250ProvinceMapActor.h — sole campaign map layer (de Costa engraving + hotspot markers + tokens).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250ArmyToken.h"
#include "pa250/types.hpp"
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
	/** Set an explicit view (probe/tests): centre UV and zoom, clamped like the interactive paths. */
	void SetView(FVector2D CenterUV, float Zoom);
	/** World location of the army's token actor (its ground anchor), if one is spawned. */
	bool GetArmyTokenLocation(const FString& ArmyId, FVector& OutWorld) const;
	/** One line per army token: id, anchor, representation (for probe logs). */
	FString DescribeArmyTokens() const;
	/**
	 * Hover text for an army (preferred) or a town, from read-only sim state. Your own force shows
	 * strength and destination; enemy strength and destination stay unreported.
	 */
	FString DescribeHover(const FString& ArmyId, const FString& ProvinceId) const;
	/** World XY box of what is drawn for the hovered army (figures + plate) or town (mark + caption). */
	FBox2D HoverWorldBox(const FString& ArmyId, const FString& ProvinceId) const;
	void PanByUV(FVector2D DeltaUV);
	/** Absolute pan target (screen-space drag); clamped to what the camera can show. */
	void SetCenterUV(FVector2D CenterUV);
	/** Screen pixels per world unit, measured from the real projection (0 if no view yet). */
	float MeasurePxPerUU() const;
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

	/** Hand-colour wash around a held town (marks the town, not a territory boundary). */
	static constexpr float TownWashOpacity = 0.30f;
	/** Default wash radius = this x hotspot click radius, capped at TownWashNeighbourCap x the nearest other hotspot. */
	static constexpr float TownWashRadiusScale = 1.8f;
	static constexpr float TownWashNeighbourCap = 0.75f;
	/** Wash radius in image pixels for one town (override from decosta_locations.json wash_radius_px, then the cap). */
	static float TownWashRadiusPx(const FString& Id, const TMap<FString, FVector2D>& HotspotUV, const TMap<FString, float>& HitRadiusPx,
								  const TMap<FString, float>& OverridePx, float ImageW, float ImageH);

	/** Town geometry an army slot must not cover (world XY, UU). */
	struct FTownObstacles
	{
		TArray<FString> Ids;
		TArray<FVector2D> Centers;	 // hotspot centres
		TArray<float> Radii;		 // hotspot click radius
		TArray<FBox2D> Captions;	 // caption text boxes (selected size, always reserved)
	};
	/** Builds obstacles from hotspot UVs/radii/names exactly as SpawnHotspotMarkers lays them out. */
	static FTownObstacles BuildTownObstacles(const TMap<FString, FVector2D>& HotspotUV, const TMap<FString, float>& RadiusPx,
											 const TMap<FString, FString>& Names, float ImageWidthPx, float MapW, float MapH);
	/** Footprint of an army slot: art standing on the anchor plus the name plate below it. */
	static void ArmySlotBoxes(const FVector2D& Anchor, const FVector2D& ArtSize, const FVector2D& PlateSize, FBox2D& OutArt, FBox2D& OutPlate);
	/**
	 * Deterministic slot choice beside the home town: first candidate (E, W, S, SE, SW, N, then a wider ring)
	 * that covers no town hotspot, caption or already-placed army. Returns the anchor; OutOverlap is the
	 * residual overlap area (0 when a clean slot exists).
	 */
	/** Illustration for an army from campaign state only (tests call this; see ChooseArmyArt in the .cpp). */
	static EPA250ArmyArt ArtForArmy(const pa250::CampaignState& St, const pa250::Army& A);
	/** Point at fraction T (0..1) of a polyline's arc length. */
	static FVector2D PointAlongPath(const TArray<FVector2D>& Path, float T);
	static FVector2D ChooseArmyAnchor(const FString& HomeId, const FTownObstacles& Towns, const TArray<FBox2D>& Placed,
									  const FVector2D& ArtSize, const FVector2D& PlateSize, float& OutOverlap, int32& OutCandidate);
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
	void RefreshBuildingIcons();
	void RefreshTownOwnership();
	void ApplyTownWash(UStaticMeshComponent* Wash, const std::string& Owner) const;
	void RefreshArmyTokens();
	/** Per frame: measured pixels per UU drives illustration vs compact symbol. */
	void UpdateArmyLod();
	/** Per frame: ease each token toward its sim position (presentation only; the sim stays hourly). */
	void MoveArmyTokens(float DeltaSeconds);
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
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> TownWashes;
	TMap<FString, UStaticMeshComponent*> WashById;
	TMap<FString, UStaticMeshComponent*> RingById;
	TMap<FString, UStaticMeshComponent*> FillById;
	/** Soft parchment backing behind each caption (legible over engraved lettering and the colour wash). */
	TMap<FString, UStaticMeshComponent*> CaptionPlateById;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> CaptionPlates;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> MapTextMaterial;
	float LastMarkerPxPerUU = -1.f;
	FString LastMarkerSelection;
	/** Town mark and caption target sizes on screen. */
	static constexpr float TownMarkPx = 14.f;
	static constexpr float TownCaptionPx = 15.f;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> BuildingIconMarkers;
	TMap<FString, float> WashRadiusOverridePx;
	int32 BuildingIconHudEpoch = -1;
	/** Buildings the current markers show; the markers are rebuilt only when this changes. */
	FString BuildingIconSignature;

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
	/** Last logged representation per army (log only on change). */
	TMap<FString, bool> LoggedIllustrationById;
	/** Where each army should stand now: its town slot, or part-way along its current edge. */
	TMap<FString, FVector> ArmyTargetAnchor;
	/** Traced road polylines (UV) by sim edge id, as stored (first point = one end's hotspot). */
	TMap<FString, TArray<FVector2D>> RouteUVByEdge;
};
