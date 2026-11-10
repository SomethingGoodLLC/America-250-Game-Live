#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "PA250ArmyToken.generated.h"

class USceneComponent;
class UStaticMeshComponent;
class UTextRenderComponent;
class UTexture2D;
class UMaterialInterface;

/**
 * Illustration shown for an army at readable zoom. Chosen from campaign state only:
 * a variant is used only when the matching state exists (see APA250ProvinceMapActor::ChooseArmyArt).
 * The art is an artistic reconstruction; figure count never encodes strength. No militia art exists:
 * a militia-majority force keeps the compact symbol.
 */
enum class EPA250ArmyArt : uint8
{
	None,			 // compact symbol at every zoom (no neutral art supplied for this side/state)
	ProvincialRegulars, // provincial regulars standing with shouldered arms (Continental, mostly line infantry)
	BritishMarching, // British column on the march (army has a path)
	BritishFiring	 // British firing line (army is a party to the pending battle)
};

struct FPA250ArmyArtSpec
{
	const TCHAR* TexturePath = nullptr;
	/** Content rectangle inside the power-of-two texture (top-left aligned), from prepare_army_sprites.py. */
	float UScale = 1.f;
	float VScale = 1.f;
	/** Content width / height in pixels. */
	float Aspect = 1.f;
	/** Standing figure height (hat to feet) / content height, so every variant draws men at one scale. */
	float FigureFrac = 1.f;
};

UCLASS()
class PLAYAMERICA250_API APA250ArmyToken : public AActor
{
	GENERATED_BODY()
public:
	APA250ArmyToken();

	/** World height of one standing figure (UU). The map plane is 1600 UU wide. */
	static constexpr float FigureHeightUU = 16.f;
	/** Compact counter target size on screen, clamped in world units. */
	static constexpr float CompactSymbolPx = 38.f;
	static constexpr float CompactMinUU = 16.f;
	static constexpr float CompactMaxUU = 34.f;
	/** Illustration appears when one figure is at least this tall on screen; hides below the lower value. */
	static constexpr float RevealFigurePx = 22.f;
	static constexpr float HideFigurePx = 19.f;
	/** Name plate text sizes (UU) — game-rendered text, never baked into the art. */
	static constexpr float NameSizeUU = 15.f;
	static constexpr float SideSizeUU = 12.f;
	/** Soft parchment ground under an illustration (separates it from soldiers printed on the engraving). 0 = off. */
	static constexpr float GroundOpacity = 0.8f;
	/** At wide zoom the plate keeps the name at least this tall on screen, growing to at most PlateMaxGrow x. */
	static constexpr float MinNamePx = 11.f;
	static constexpr float PlateMaxGrow = 1.6f;

	static const FPA250ArmyArtSpec& ArtSpec(EPA250ArmyArt Art);
	/** Width/height in UU of the art's content rectangle (zero for None). */
	static FVector2D ArtWorldSize(EPA250ArmyArt Art);
	/** Largest art footprint a faction can switch between; slots are sized for it so art changes never move the anchor. */
	static FVector2D FactionFootprintUU(const FString& Faction);
	/** Name plate block under the feet (two lines: short name, side word), in UU. */
	static FVector2D PlateSizeUU(const FString& DisplayName, const FString& SideWord);

	/**
	 * Anchor = actor location = ground point (bottom-centre of the figures' feet).
	 * Every representation (illustration, compact symbol, bars, plate) is laid out from it.
	 */
	void Configure(const FString& InArmyId, const FString& Faction, const FString& DisplayName, const FString& SideWord, bool bSelected,
				   EPA250ArmyArt InArt, bool bMirror);

	/** Per-frame LOD: illustration when readable, otherwise the compact symbol at constant screen size. */
	void ApplyLod(float PxPerUU);

	/** True when the world point is on what is currently drawn (illustration rect or compact disc). */
	bool HitTest(const FVector& World) const;
	bool IsShowingIllustration() const { return bShowIllustration; }
	EPA250ArmyArt GetArt() const { return Art; }
	/** Currently drawn footprint in world XY (for tests/logs). */
	FBox2D GetDrawnRect() const;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<USceneComponent> Root;

	/** Compact symbol: faction disc with a light bar (an army; towns are diamonds). */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FlagMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> MarkMesh;
	/** Gold ring behind the compact symbol when selected. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SelectRing;

	/** Compact counter: parchment-framed rectangle with crossed bars (infantry). Towns are diamonds; armies are rectangles. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CounterMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> BodyMesh;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CrossMeshA;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> CrossMeshB;

	/** Illustration quad (translucent unlit, content rect only — no transparent padding). */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SpriteMesh;
	/** Soft parchment oval under the illustration (not baked into the art; no rectangle). */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> GroundMesh;
	/** Thin faction rule under the feet (restrained; the plate also names the side). */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> FactionBar;
	/** Gold rule under the faction rule when selected. */
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> SelectBar;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> NameText;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UTextRenderComponent> StrengthText;

	UPROPERTY()
	FString ArmyId;

private:
	void LayoutCompact(float DiameterUU);
	void LayoutPlate(float Scale);

	UPROPERTY()
	TObjectPtr<UMaterialInterface> SpriteMaterial;
	UPROPERTY()
	TObjectPtr<UTexture2D> ArtTextures[4];
	UPROPERTY()
	TObjectPtr<UTexture2D> GroundTexture;

	EPA250ArmyArt Art = EPA250ArmyArt::None;
	bool bSelected = false;
	bool bShowIllustration = false;
	float CompactDiameterUU = 20.f;
	float PlateScale = 0.f;
	FVector2D ArtSizeUU = FVector2D::ZeroVector;
};
