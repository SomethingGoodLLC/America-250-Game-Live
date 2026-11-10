#include "PA250ArmyToken.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
// Content rectangles and aspects come from tools/art/prepare_army_sprites.py (1024x512 canvases,
// content top-left aligned). FigureFrac is hat-to-feet height / content height, measured on the
// prepared sprites, so a provincial and a British regular are drawn at the same size.
const FPA250ArmyArtSpec GArtSpecs[] = {
	{},
	{TEXT("/Game/PA250/Textures/Army/T_Army_ProvincialRegulars.T_Army_ProvincialRegulars"), 0.88379f, 1.00000f, 1.7676f, 0.70f},
	{TEXT("/Game/PA250/Textures/Army/T_Army_BritishMarching.T_Army_BritishMarching"), 1.00000f, 0.84961f, 2.3540f, 0.64f},
	{TEXT("/Game/PA250/Textures/Army/T_Army_BritishFiring.T_Army_BritishFiring"), 1.00000f, 0.73438f, 2.7234f, 0.77f},
};

constexpr float GlyphAdvance = 0.55f; // average TextRender glyph width / world size (engine Roboto)
constexpr float PlateGapUU = 2.5f;	  // feet -> first text line
constexpr float LineGapUU = 1.0f;

// Muted faction inks: same hues as the town diamonds, and never colour alone (the plate names the side).
const FColor BritInk(145, 24, 18);
const FColor ContInk(16, 48, 130);
const FLinearColor BritFill(0.66f, 0.13f, 0.10f);
const FLinearColor ContFill(0.12f, 0.27f, 0.62f);
const FLinearColor Gold(0.86f, 0.64f, 0.12f);
const FLinearColor Parchment(0.93f, 0.88f, 0.74f);

void Tint(UStaticMeshComponent* Mesh, const FLinearColor& C)
{
	if (!Mesh)
	{
		return;
	}
	UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Mesh->GetMaterial(0));
	if (!MID)
	{
		MID = Mesh->CreateAndSetMaterialInstanceDynamic(0);
	}
	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("Color"), C);
		MID->SetVectorParameterValue(TEXT("BaseColor"), C);
	}
}

void PlaceAbs(UStaticMeshComponent* Mesh, const FVector& Loc, const FVector& Scale)
{
	Mesh->SetAbsolute(true, true, true);
	Mesh->SetWorldLocation(Loc);
	Mesh->SetWorldRotation(FRotator::ZeroRotator);
	Mesh->SetWorldScale3D(Scale);
}

UStaticMeshComponent* MakeMesh(AActor* Owner, USceneComponent* Parent, const TCHAR* Name, UStaticMesh* Mesh, UMaterialInterface* Mat)
{
	UStaticMeshComponent* C = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
	C->SetupAttachment(Parent);
	if (Mesh)
	{
		C->SetStaticMesh(Mesh);
	}
	if (Mat)
	{
		C->SetMaterial(0, Mat);
	}
	// No collision anywhere: the map plane receives the click and the map hit-tests the drawn rect.
	C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	C->SetCastShadow(false);
	return C;
}

UTextRenderComponent* MakeText(AActor* Owner, USceneComponent* Parent, const TCHAR* Name)
{
	UTextRenderComponent* T = Owner->CreateDefaultSubobject<UTextRenderComponent>(Name);
	T->SetupAttachment(Parent);
	T->SetHorizontalAlignment(EHTA_Center);
	T->SetVerticalAlignment(EVRTA_TextTop);
	T->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	T->SetCastShadow(false);
	return T;
}
} // namespace

const FPA250ArmyArtSpec& APA250ArmyToken::ArtSpec(EPA250ArmyArt InArt)
{
	const int32 Idx = FMath::Clamp(static_cast<int32>(InArt), 0, static_cast<int32>(UE_ARRAY_COUNT(GArtSpecs)) - 1);
	return GArtSpecs[Idx];
}

FVector2D APA250ArmyToken::ArtWorldSize(EPA250ArmyArt InArt)
{
	if (InArt == EPA250ArmyArt::None)
	{
		return FVector2D::ZeroVector;
	}
	const FPA250ArmyArtSpec& S = ArtSpec(InArt);
	const float H = FigureHeightUU / FMath::Max(0.1f, S.FigureFrac);
	return FVector2D(H * S.Aspect, H);
}

FVector2D APA250ArmyToken::FactionFootprintUU(const FString& Faction)
{
	// Slot = the largest thing this side can draw, so switching between its variants (or to the
	// compact symbol) never moves the ground anchor.
	if (Faction.Equals(TEXT("british"), ESearchCase::IgnoreCase))
	{
		const FVector2D M = ArtWorldSize(EPA250ArmyArt::BritishMarching);
		const FVector2D F = ArtWorldSize(EPA250ArmyArt::BritishFiring);
		return FVector2D(FMath::Max(M.X, F.X), FMath::Max(M.Y, F.Y));
	}
	return ArtWorldSize(EPA250ArmyArt::ProvincialRegulars);
}

FVector2D APA250ArmyToken::PlateSizeUU(const FString& DisplayName, const FString& SideWord)
{
	// Line 1: short name. Line 2: side word. Same text selected or not, so selection never moves anything.
	const float NameW = DisplayName.Len() * NameSizeUU * GlyphAdvance;
	const float SideW = SideWord.Len() * SideSizeUU * GlyphAdvance;
	return FVector2D(FMath::Max(NameW, SideW), PlateGapUU + NameSizeUU + LineGapUU + SideSizeUU);
}

APA250ArmyToken::APA250ArmyToken()
{
	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> Disc(TEXT("/Engine/BasicShapes/Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Bar(TEXT("/Engine/BasicShapes/Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> ShapeMat(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> SpriteMat(TEXT("/Game/PA250/Materials/M_MapSprite.M_MapSprite"));
	static ConstructorHelpers::FObjectFinder<UTexture2D> TexProvincial(GArtSpecs[1].TexturePath);
	static ConstructorHelpers::FObjectFinder<UTexture2D> TexMarch(GArtSpecs[2].TexturePath);
	static ConstructorHelpers::FObjectFinder<UTexture2D> TexFire(GArtSpecs[3].TexturePath);
	static ConstructorHelpers::FObjectFinder<UTexture2D> TexGround(TEXT("/Game/PA250/Textures/Army/T_Army_GroundOval.T_Army_GroundOval"));

	UMaterialInterface* Shape = ShapeMat.Succeeded() ? ShapeMat.Object : nullptr;
	FlagMesh = MakeMesh(this, Root, TEXT("Flag"), Disc.Succeeded() ? Disc.Object : nullptr, Shape);
	MarkMesh = MakeMesh(this, Root, TEXT("Mark"), Bar.Succeeded() ? Bar.Object : nullptr, Shape);
	CounterMesh = MakeMesh(this, Root, TEXT("Counter"), Bar.Succeeded() ? Bar.Object : nullptr, Shape);
	BodyMesh = MakeMesh(this, Root, TEXT("CounterBody"), Bar.Succeeded() ? Bar.Object : nullptr, Shape);
	CrossMeshA = MakeMesh(this, Root, TEXT("CrossA"), Bar.Succeeded() ? Bar.Object : nullptr, Shape);
	CrossMeshB = MakeMesh(this, Root, TEXT("CrossB"), Bar.Succeeded() ? Bar.Object : nullptr, Shape);
	SelectRing = MakeMesh(this, Root, TEXT("SelectRing"), Disc.Succeeded() ? Disc.Object : nullptr, Shape);
	SpriteMesh = MakeMesh(this, Root, TEXT("Sprite"), Plane.Succeeded() ? Plane.Object : nullptr, nullptr);
	GroundMesh = MakeMesh(this, Root, TEXT("Ground"), Plane.Succeeded() ? Plane.Object : nullptr, nullptr);
	FactionBar = MakeMesh(this, Root, TEXT("FactionBar"), Bar.Succeeded() ? Bar.Object : nullptr, Shape);
	SelectBar = MakeMesh(this, Root, TEXT("SelectBar"), Bar.Succeeded() ? Bar.Object : nullptr, Shape);
	NameText = MakeText(this, Root, TEXT("Name"));
	StrengthText = MakeText(this, Root, TEXT("Strength"));

	SpriteMaterial = SpriteMat.Succeeded() ? SpriteMat.Object : nullptr;
	ArtTextures[0] = nullptr;
	ArtTextures[1] = TexProvincial.Succeeded() ? TexProvincial.Object : nullptr;
	ArtTextures[2] = TexMarch.Succeeded() ? TexMarch.Object : nullptr;
	ArtTextures[3] = TexFire.Succeeded() ? TexFire.Object : nullptr;
	GroundTexture = TexGround.Succeeded() ? TexGround.Object : nullptr;
	if (SpriteMaterial)
	{
		SpriteMesh->SetMaterial(0, SpriteMaterial);
		GroundMesh->SetMaterial(0, SpriteMaterial);
	}
	SpriteMesh->SetVisibility(false);
	GroundMesh->SetVisibility(false);
	SelectRing->SetVisibility(false);
	SelectBar->SetVisibility(false);
	// Compact symbol default scale (also what the framing check reads on the CDO).
	FlagMesh->SetRelativeScale3D(FVector(0.20f, 0.20f, 0.04f));
}

void APA250ArmyToken::Configure(const FString& InArmyId, const FString& Faction, const FString& DisplayName, const FString& SideWord,
								bool bInSelected, EPA250ArmyArt InArt, bool bMirror)
{
	ArmyId = InArmyId;
	bSelected = bInSelected;
	const bool bBrit = Faction.Equals(TEXT("british"), ESearchCase::IgnoreCase);
	const FColor Ink = bBrit ? BritInk : ContInk;
	const FLinearColor Fill = bBrit ? BritFill : ContFill;

	// Fall back to the compact symbol when the art asset is missing (never draw an empty quad).
	Art = InArt;
	if (Art != EPA250ArmyArt::None && (!SpriteMaterial || !ArtTextures[static_cast<int32>(Art)]))
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250 army art: %s missing texture/material for art %d; compact symbol only"), *ArmyId,
			   static_cast<int32>(Art));
		Art = EPA250ArmyArt::None;
	}
	ArtSizeUU = ArtWorldSize(Art);
	const FVector A = GetActorLocation(); // ground anchor (feet, bottom-centre)
	SetActorScale3D(FVector::OneVector);

	// Illustration quad: exactly the content rect, standing on the anchor.
	if (Art != EPA250ArmyArt::None)
	{
		const FPA250ArmyArtSpec& S = ArtSpec(Art);
		PlaceAbs(SpriteMesh, A + FVector(0.f, -ArtSizeUU.Y * 0.5f, 1.f), FVector(ArtSizeUU.X / 100.f, ArtSizeUU.Y / 100.f, 1.f));
		UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(SpriteMesh->GetMaterial(0));
		if (!MID)
		{
			MID = SpriteMesh->CreateAndSetMaterialInstanceDynamic(0);
		}
		if (MID)
		{
			MID->SetTextureParameterValue(TEXT("Sprite"), ArtTextures[static_cast<int32>(Art)]);
			// Mirroring flips U only; the anchor (bottom-centre) is unchanged by a flip.
			MID->SetVectorParameterValue(TEXT("UVRect"), bMirror ? FLinearColor(S.UScale, 0.f, -S.UScale, S.VScale)
															   : FLinearColor(0.f, 0.f, S.UScale, S.VScale));
		}
		// Translucent: keep a stable draw order between overlapping tokens (ground under figures).
		SpriteMesh->SetTranslucentSortPriority(bSelected ? 3 : 2);
		// Ground oval under the whole army (figures and name plate), so neither sits directly on printed
		// soldiers or script. Opaque core covers the middle; the rim eases to nothing (no edge, no rectangle).
		const FVector2D PlateUU = PlateSizeUU(DisplayName, SideWord);
		const float Top = -ArtSizeUU.Y - 2.f;
		const float Bottom = PlateUU.Y + 1.f;
		PlaceAbs(GroundMesh, A + FVector(0.f, (Top + Bottom) * 0.5f, 0.3f),
				 FVector(FMath::Max(ArtSizeUU.X, PlateUU.X) * 1.3f / 100.f, (Bottom - Top) * 1.2f / 100.f, 1.f));
		GroundMesh->SetTranslucentSortPriority(1);
		if (UMaterialInstanceDynamic* GMID = GroundMesh->CreateAndSetMaterialInstanceDynamic(0))
		{
			GMID->SetTextureParameterValue(TEXT("Sprite"), GroundTexture);
			GMID->SetVectorParameterValue(TEXT("UVRect"), FLinearColor(0.f, 0.f, 1.f, 1.f));
			GMID->SetScalarParameterValue(TEXT("Opacity"), GroundOpacity);
		}
		const float RuleW = ArtSizeUU.X * 0.72f;
		PlaceAbs(FactionBar, A + FVector(0.f, 0.9f, 0.5f), FVector(RuleW / 100.f, 1.3f / 100.f, 0.01f));
		PlaceAbs(SelectBar, A + FVector(0.f, 2.6f, 0.5f), FVector(ArtSizeUU.X * 0.86f / 100.f, 1.4f / 100.f, 0.01f));
		Tint(FactionBar, Fill);
		Tint(SelectBar, Gold);
	}

	// Compact counter colours (layout depends on zoom; see ApplyLod).
	// Frame goes gold when selected; the body always carries the faction colour,
	// crossed by parchment bars so it reads as an army, never as a town diamond.
	Tint(FlagMesh, Fill);
	Tint(MarkMesh, Parchment);
	Tint(SelectRing, Gold);
	Tint(CounterMesh, bSelected ? Gold : Parchment);
	Tint(BodyMesh, Fill);
	Tint(CrossMeshA, Parchment);
	Tint(CrossMeshB, Parchment);

	// Name plate under the feet (PlateSizeUU reserves its room). Game-rendered text; never baked into the art.
	const FRotator TextRot(90.f, 90.f, 0.f); // == APA250ProvinceMapActor::FlatMapTextRotation()
	NameText->SetAbsolute(true, true, true);
	NameText->SetWorldRotation(TextRot);
	NameText->SetWorldScale3D(FVector::OneVector);
	NameText->SetText(FText::FromString(DisplayName));
	NameText->SetTextRenderColor(Ink);
	StrengthText->SetAbsolute(true, true, true);
	StrengthText->SetWorldRotation(TextRot);
	StrengthText->SetWorldScale3D(FVector::OneVector);
	StrengthText->SetText(FText::FromString(SideWord));
	StrengthText->SetTextRenderColor(Ink);

	bShowIllustration = false;
	PlateScale = 0.f;
	ApplyLod(0.f);
}

void APA250ArmyToken::LayoutPlate(float Scale)
{
	PlateScale = Scale;
	const FVector A = GetActorLocation();
	NameText->SetWorldSize(NameSizeUU * Scale);
	NameText->SetWorldLocation(A + FVector(0.f, PlateGapUU, 6.f));
	StrengthText->SetWorldSize(SideSizeUU * Scale);
	StrengthText->SetWorldLocation(A + FVector(0.f, PlateGapUU + (NameSizeUU + LineGapUU) * Scale, 6.f));
}

void APA250ArmyToken::LayoutCompact(float DiameterUU)
{
	CompactDiameterUU = DiameterUU;
	const FVector A = GetActorLocation();
	// The counter stands on the ground line like the figures do, so switching LOD keeps the anchor.
	const FVector C = A + FVector(0.f, -DiameterUU * 0.5f, 2.f);
	PlaceAbs(FlagMesh, C, FVector(DiameterUU / 100.f, DiameterUU / 100.f, 0.04f));
	PlaceAbs(MarkMesh, C + FVector(0.f, 0.f, 2.5f), FVector(DiameterUU * 0.58f / 100.f, DiameterUU * 0.15f / 100.f, 0.04f));
	const float RingD = DiameterUU + 5.f;
	PlaceAbs(SelectRing, C + FVector(0.f, 0.f, -1.5f), FVector(RingD / 100.f, RingD / 100.f, 0.03f));
	// NATO-style infantry counter: framed rectangle, faction body, crossed bars.
	const float W = DiameterUU * 1.30f;
	const float H = DiameterUU * 0.86f;
	PlaceAbs(CounterMesh, C + FVector(0.f, 0.f, 0.6f), FVector(W / 100.f, H / 100.f, 0.04f));
	PlaceAbs(BodyMesh, C + FVector(0.f, 0.f, 1.4f), FVector((W - 3.5f) / 100.f, (H - 3.5f) / 100.f, 0.04f));
	auto PlaceCross = [&](UStaticMeshComponent* Mesh, float YawDeg) {
		if (!Mesh) return;
		Mesh->SetAbsolute(true, true, true);
		Mesh->SetWorldLocation(C + FVector(0.f, 0.f, 2.4f));
		Mesh->SetWorldRotation(FRotator(0.f, YawDeg, 0.f));
		Mesh->SetWorldScale3D(FVector(FMath::Sqrt(W * W + H * H) * 0.92f / 100.f, DiameterUU * 0.11f / 100.f, 0.04f));
	};
	const float Yaw = FMath::RadiansToDegrees(FMath::Atan2(H, W));
	PlaceCross(CrossMeshA, Yaw);
	PlaceCross(CrossMeshB, -Yaw);
}

void APA250ArmyToken::ApplyLod(float PxPerUU)
{
	bool bIllus = false;
	if (Art != EPA250ArmyArt::None && PxPerUU > 0.f)
	{
		const float FigurePx = FigureHeightUU * PxPerUU;
		bIllus = bShowIllustration ? FigurePx >= HideFigurePx : FigurePx >= RevealFigurePx;
	}
	bShowIllustration = bIllus;
	const float D = PxPerUU > 0.f ? FMath::Clamp(CompactSymbolPx / PxPerUU, CompactMinUU, CompactMaxUU) : CompactMaxUU * 0.75f;
	if (!FMath::IsNearlyEqual(D, CompactDiameterUU, 0.05f) || !FlagMesh->IsUsingAbsoluteLocation())
	{
		LayoutCompact(D);
	}
	// Plate: reserved size when the illustration shows; at wide zoom keep the name readable (capped growth).
	const float Scale = (!bIllus && PxPerUU > 0.f) ? FMath::Clamp(MinNamePx / (NameSizeUU * PxPerUU), 1.f, PlateMaxGrow) : 1.f;
	if (!FMath::IsNearlyEqual(Scale, PlateScale, 0.01f))
	{
		LayoutPlate(Scale);
	}
	SpriteMesh->SetVisibility(bIllus);
	GroundMesh->SetVisibility(bIllus && GroundTexture != nullptr && GroundOpacity > 0.f);
	FactionBar->SetVisibility(bIllus);
	SelectBar->SetVisibility(bIllus && bSelected);
	// Compact view draws the framed counter, not the old disc-and-bar.
	FlagMesh->SetVisibility(false);
	MarkMesh->SetVisibility(false);
	SelectRing->SetVisibility(false);
	if (CounterMesh) CounterMesh->SetVisibility(!bIllus);
	if (BodyMesh) BodyMesh->SetVisibility(!bIllus);
	if (CrossMeshA) CrossMeshA->SetVisibility(!bIllus);
	if (CrossMeshB) CrossMeshB->SetVisibility(!bIllus);
}

FBox2D APA250ArmyToken::GetDrawnRect() const
{
	const FVector A = GetActorLocation();
	if (bShowIllustration)
	{
		return FBox2D(FVector2D(A.X - ArtSizeUU.X * 0.5f, A.Y - ArtSizeUU.Y), FVector2D(A.X + ArtSizeUU.X * 0.5f, A.Y + 1.f));
	}
	// Framed counter centred half a diameter above the ground anchor.
	const float HalfW = CompactDiameterUU * 0.65f;
	const float HalfH = CompactDiameterUU * 0.43f;
	const FVector2D C(A.X, A.Y - CompactDiameterUU * 0.5f);
	return FBox2D(C - FVector2D(HalfW, HalfH), C + FVector2D(HalfW, HalfH));
}

bool APA250ArmyToken::HitTest(const FVector& World) const
{
	const FVector2D P(World.X, World.Y);
	const FVector A = GetActorLocation();
	if (bShowIllustration)
	{
		// Content rect only (padding was cropped at preparation) plus a 2 UU grace margin.
		return GetDrawnRect().ExpandBy(2.f).IsInside(P);
	}
	return GetDrawnRect().ExpandBy(2.f).IsInside(P);
}
