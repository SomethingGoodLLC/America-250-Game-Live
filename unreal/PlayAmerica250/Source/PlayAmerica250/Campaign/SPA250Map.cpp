#include "SPA250Map.h"
#include "Campaign/PA250Subsystem.h"
#include "Engine/Texture2D.h"
#include "ImageUtils.h"
#include "Misc/Paths.h"
#include "Fonts/FontMeasure.h"
#include "Framework/Application/SlateApplication.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Styling/AppStyle.h"

namespace
{
constexpr int32 MapW = 1000;
constexpr int32 MapH = 700;
FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
std::string ToStd(const FString& S) { return std::string(TCHAR_TO_UTF8(*S)); }
FLinearColor FactionColor(const std::string& Faction)
{
	return Faction == "british" ? FLinearColor(0.85f, 0.12f, 0.1f) : FLinearColor(0.12f, 0.28f, 0.85f);
}
}

void SPA250Map::Construct(const FArguments& InArgs)
{
	Subsystem = InArgs._Subsystem;
	OnSelectionChanged = InArgs._OnSelectionChanged;
	SetCanTick(false);
}

bool SPA250Map::PointInPolygon(const TArray<FVector2D>& Poly, const FVector2D& P)
{
	bool bInside = false;
	for (int32 i = 0, j = Poly.Num() - 1; i < Poly.Num(); j = i++)
	{
		if (((Poly[i].Y > P.Y) != (Poly[j].Y > P.Y)) &&
		    (P.X < (Poly[j].X - Poly[i].X) * (P.Y - Poly[i].Y) / (Poly[j].Y - Poly[i].Y) + Poly[i].X))
		{
			bInside = !bInside;
		}
	}
	return bInside;
}

FString SPA250Map::ProvinceAt(const FVector2D& MapPoint) const
{
	if (!Subsystem.IsValid())
	{
		return FString();
	}
	for (const auto& KV : Subsystem->GetState().provinces)
	{
		TArray<FVector2D> Poly;
		for (const auto& P : KV.second.polygon)
		{
			Poly.Add(FVector2D(P.x, P.y));
		}
		if (PointInPolygon(Poly, MapPoint))
		{
			return ToFStr(KV.first);
		}
	}
	return FString();
}

void SPA250Map::ClearSelection()
{
	SelectedArmyId.Reset();
	SelectedProvinceId.Reset();
	if (Subsystem.IsValid())
	{
		Subsystem->ClearOrders();
	}
	LastMessage = TEXT("Orders cleared.");
	OnSelectionChanged.ExecuteIfBound();
}

FString SPA250Map::HandleProvinceClick(const FString& ProvinceId)
{
	if (!Subsystem.IsValid() || ProvinceId.IsEmpty())
	{
		return FString();
	}
	const pa250::CampaignState& S = Subsystem->GetState();
	const std::string Pid = ToStd(ProvinceId);
	if (!S.provinces.count(Pid))
	{
		return FString();
	}
	if (Subsystem->GetFlow() != EPA250Flow::Campaign || Subsystem->IsBattlePending())
	{
		LastMessage = TEXT("Resolve the pending battle first.");
		OnSelectionChanged.ExecuteIfBound();
		return LastMessage;
	}

	// An army of the player's side in the clicked province?
	FString OwnArmy;
	for (const auto& KV : S.armies)
	{
		if (KV.second.province == Pid && KV.second.faction == S.player_faction)
		{
			OwnArmy = ToFStr(KV.first);
			break;
		}
	}

	if (!SelectedArmyId.IsEmpty())
	{
		const pa250::Army& A = S.armies.at(ToStd(SelectedArmyId));
		FString Err;
		if (Pid == A.province)
		{
			SelectedProvinceId = ProvinceId;  // clicking the army's own province just keeps it selected
			LastMessage = TEXT("Selected ") + ToFStr(A.name) + TEXT(". Click an adjacent province to march.");
		}
		else if (Subsystem->QueueMove(SelectedArmyId, ProvinceId, Err))
		{
			LastMessage = ToFStr(A.name) + TEXT(" ordered to ") + ToFStr(S.provinces.at(Pid).name) + TEXT(". End the turn to march.");
			SelectedProvinceId = ProvinceId;
		}
		else
		{
			LastMessage = TEXT("Illegal move: ") + Err;
		}
	}
	else if (!OwnArmy.IsEmpty())
	{
		SelectedArmyId = OwnArmy;
		SelectedProvinceId = ProvinceId;
		LastMessage = TEXT("Selected ") + ToFStr(S.armies.at(ToStd(OwnArmy)).name) + TEXT(". Click an adjacent province to march.");
	}
	else
	{
		SelectedProvinceId = ProvinceId;
		LastMessage = ToFStr(S.provinces.at(Pid).name) + TEXT(" (") + ToFStr(S.provinces.at(Pid).owner) + TEXT(")");
	}
	OnSelectionChanged.ExecuteIfBound();
	return LastMessage;
}

void SPA250Map::ComputeTransform(const FGeometry& G, float& OutScale, FVector2D& OutOffset) const
{
	const FVector2D Size = FVector2D(G.GetLocalSize());
	OutScale = FMath::Max(0.01f, FMath::Min(Size.X / MapW, Size.Y / MapH));
	OutOffset = (Size - FVector2D(MapW, MapH) * OutScale) * 0.5;
}

FReply SPA250Map::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	float Scale;
	FVector2D Offset;
	ComputeTransform(MyGeometry, Scale, Offset);
	if (MouseEvent.GetEffectingButton() == EKeys::RightMouseButton)
	{
		ClearSelection();
		InvalidateOverlay();
		return FReply::Handled();
	}
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D Local = MyGeometry.AbsoluteToLocal(MouseEvent.GetScreenSpacePosition());
		const FVector2D MapPt = (Local - Offset) / Scale;
		const FString Pid = ProvinceAt(MapPt);
		if (!Pid.IsEmpty())
		{
			HandleProvinceClick(Pid);
			return FReply::Handled();
		}
	}
	return FReply::Unhandled();
}

void SPA250Map::EnsureMapTexture() const
{
	if (bMapTried)
	{
		return;
	}
	bMapTried = true;
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/PA250/Textures/T_CampaignMap.T_CampaignMap"));
	if (!Tex)
	{
		// Fallback: read the generated PNG straight from the project (no editor import needed).
		const FString Png = FPaths::Combine(FPaths::ProjectContentDir(), TEXT("PA250/Source/campaign_map.png"));
		if (FPaths::FileExists(Png))
		{
			Tex = FImageUtils::ImportFileAsTexture2D(Png);
		}
	}
	if (Tex)
	{
		MapTexture.Reset(Tex);
		MapBrush.SetResourceObject(Tex);
		MapBrush.ImageSize = FVector2f(MapW, MapH);
		MapBrush.DrawAs = ESlateBrushDrawType::Image;
	}
}

void SPA250Map::RebuildOverlay() const
{
	if (!Subsystem.IsValid())
	{
		return;
	}
	const pa250::CampaignState& S = Subsystem->GetState();
	FString Sig;
	for (const auto& KV : S.provinces)
	{
		Sig += ToFStr(KV.first) + TEXT(":") + ToFStr(KV.second.owner) + TEXT(";");
	}
	if (!bOverlayDirty && Sig == OverlaySignature && OverlayTexture.IsValid())
	{
		return;
	}
	OverlaySignature = Sig;
	bOverlayDirty = false;

	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(MapW * MapH);
	for (const auto& KV : S.provinces)
	{
		TArray<FVector2D> Poly;
		double MinX = 1e9, MinY = 1e9, MaxX = -1e9, MaxY = -1e9;
		for (const auto& P : KV.second.polygon)
		{
			Poly.Add(FVector2D(P.x, P.y));
			MinX = FMath::Min(MinX, P.x);
			MinY = FMath::Min(MinY, P.y);
			MaxX = FMath::Max(MaxX, P.x);
			MaxY = FMath::Max(MaxY, P.y);
		}
		const FColor C = FactionColor(KV.second.owner).ToFColor(true);
		const FColor Tint(C.R, C.G, C.B, 78);
		for (int32 Y = FMath::Max(0, (int32)MinY); Y <= FMath::Min(MapH - 1, (int32)MaxY); ++Y)
		{
			for (int32 X = FMath::Max(0, (int32)MinX); X <= FMath::Min(MapW - 1, (int32)MaxX); ++X)
			{
				if (PointInPolygon(Poly, FVector2D(X + 0.5, Y + 0.5)))
				{
					Pixels[Y * MapW + X] = Tint;
				}
			}
		}
	}
	if (!OverlayTexture.IsValid())
	{
		UTexture2D* Tex = UTexture2D::CreateTransient(MapW, MapH, PF_B8G8R8A8);
		Tex->Filter = TF_Bilinear;
		Tex->SRGB = true;
		Tex->UpdateResource();
		OverlayTexture.Reset(Tex);
		OverlayBrush.SetResourceObject(Tex);
		OverlayBrush.ImageSize = FVector2f(MapW, MapH);
		OverlayBrush.DrawAs = ESlateBrushDrawType::Image;
	}
	FTexture2DMipMap& Mip = OverlayTexture->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	OverlayTexture->UpdateResource();
}

int32 SPA250Map::OnPaint(const FPaintArgs& Args, const FGeometry& G, const FSlateRect& Cull, FSlateWindowElementList& Out,
                         int32 Layer, const FWidgetStyle& Style, bool bParentEnabled) const
{
	float Scale;
	FVector2D Offset;
	ComputeTransform(G, Scale, Offset);
	LastScale = Scale;
	LastOffset = Offset;
	const FVector2f MapSize(MapW * Scale, MapH * Scale);
	const FPaintGeometry MapGeo = G.ToPaintGeometry(MapSize, FSlateLayoutTransform(FVector2f(Offset)));

	EnsureMapTexture();
	if (MapTexture.IsValid())
	{
		FSlateDrawElement::MakeBox(Out, Layer, MapGeo, &MapBrush, ESlateDrawEffect::None, FLinearColor::White);
	}
	else
	{
		FSlateDrawElement::MakeBox(Out, Layer, MapGeo, FAppStyle::GetBrush("WhiteBrush"), ESlateDrawEffect::None, FLinearColor(0.8f, 0.72f, 0.5f));
	}
	if (!Subsystem.IsValid() || !Subsystem->GetController().has_campaign())
	{
		return Layer;
	}
	RebuildOverlay();
	if (OverlayTexture.IsValid())
	{
		FSlateDrawElement::MakeBox(Out, Layer + 1, MapGeo, &OverlayBrush, ESlateDrawEffect::None, FLinearColor::White);
	}

	const pa250::CampaignState& S = Subsystem->GetState();
	const FSlateFontInfo Bold = FCoreStyle::GetDefaultFontStyle("Bold", 13);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	int32 L = Layer + 2;

	// Province borders tinted by owner; selected province brighter.
	for (const auto& KV : S.provinces)
	{
		TArray<FVector2f> Line;
		for (const auto& P : KV.second.polygon)
		{
			Line.Add(FVector2f(ToScreen(FVector2D(P.x, P.y), Scale, Offset)));
		}
		if (Line.Num() > 1)
		{
			// Copy first. Line.Add(Line[0]) hands Add an element of the array it is growing, which asserts.
			const FVector2f Close = Line[0];
			Line.Add(Close);
		}
		const bool bSel = ToFStr(KV.first) == SelectedProvinceId;
		FLinearColor C = FactionColor(KV.second.owner);
		C.A = bSel ? 1.f : 0.75f;
		FSlateDrawElement::MakeLines(Out, L, G.ToPaintGeometry(), Line, ESlateDrawEffect::None, C, true, bSel ? 4.f : 2.f);
	}

	// Queued orders as arrows.
	for (const auto& O : Subsystem->GetController().orders().orders)
	{
		auto Ait = S.armies.find(O.army_id);
		auto Pit = S.provinces.find(O.to_province);
		if (Ait == S.armies.end() || Pit == S.provinces.end() || O.to_province == Ait->second.province)
		{
			continue;
		}
		const auto& From = S.provinces.at(Ait->second.province).label;
		const FVector2f A(ToScreen(FVector2D(From.x, From.y), Scale, Offset));
		const FVector2f B(ToScreen(FVector2D(Pit->second.label.x, Pit->second.label.y), Scale, Offset));
		FSlateDrawElement::MakeLines(Out, L + 1, G.ToPaintGeometry(), {A, B}, ESlateDrawEffect::None, FLinearColor(1.f, 0.9f, 0.1f), true, 4.f);
		const FVector2f Dir = (B - A).GetSafeNormal();
		const FVector2f Perp(-Dir.Y, Dir.X);
		FSlateDrawElement::MakeLines(Out, L + 1, G.ToPaintGeometry(), {B - Dir * 16.f + Perp * 8.f, B, B - Dir * 16.f - Perp * 8.f},
		                             ESlateDrawEffect::None, FLinearColor(1.f, 0.9f, 0.1f), true, 4.f);
	}

	// Army markers.
	int32 Slot[2] = {0, 0};
	for (const auto& KV : S.armies)
	{
		const pa250::Army& A = KV.second;
		auto Pit = S.provinces.find(A.province);
		if (Pit == S.provinces.end())
		{
			continue;
		}
		const bool bBrit = A.faction == "british";
		const FVector2f C(ToScreen(FVector2D(Pit->second.label.x, Pit->second.label.y), Scale, Offset));
		const FVector2f Size(46.f, 26.f);
		const FVector2f TL = C + FVector2f(bBrit ? 12.f : -12.f - Size.X, -44.f - 30.f * Slot[bBrit ? 0 : 1]++);
		FLinearColor Col = FactionColor(A.faction);
		const bool bSel = ToFStr(A.id) == SelectedArmyId;
		FSlateDrawElement::MakeBox(Out, L + 2, G.ToPaintGeometry(Size, FSlateLayoutTransform(TL)), FAppStyle::GetBrush("WhiteBrush"),
		                           ESlateDrawEffect::None, Col);
		FSlateDrawElement::MakeLines(Out, L + 3, G.ToPaintGeometry(), {TL, TL + FVector2f(Size.X, 0), TL + Size, TL + FVector2f(0, Size.Y), TL},
		                             ESlateDrawEffect::None, bSel ? FLinearColor(1.f, 0.95f, 0.2f) : FLinearColor::White, true, bSel ? 4.f : 2.f);
		int32 Total = 0;
		for (const auto& U : A.units) Total += U.second;
		FSlateDrawElement::MakeText(Out, L + 4, G.ToPaintGeometry(FVector2f(Size), FSlateLayoutTransform(TL + FVector2f(6.f, 4.f))),
		                            FString::FromInt(Total), Bold, ESlateDrawEffect::None, FLinearColor::White);
	}

	// Pending battle marker.
	if (S.pending_battle.active)
	{
		auto Pit = S.provinces.find(S.pending_battle.province);
		if (Pit != S.provinces.end())
		{
			const FVector2f C(ToScreen(FVector2D(Pit->second.label.x, Pit->second.label.y), Scale, Offset));
			const float R = 26.f;
			FSlateDrawElement::MakeLines(Out, L + 3, G.ToPaintGeometry(), {C + FVector2f(-R, -R), C + FVector2f(R, R)}, ESlateDrawEffect::None, FLinearColor(0.9f, 0.05f, 0.05f), true, 5.f);
			FSlateDrawElement::MakeLines(Out, L + 3, G.ToPaintGeometry(), {C + FVector2f(R, -R), C + FVector2f(-R, R)}, ESlateDrawEffect::None, FLinearColor(0.9f, 0.05f, 0.05f), true, 5.f);
			FSlateDrawElement::MakeText(Out, L + 4, G.ToPaintGeometry(FVector2f(160.f, 20.f), FSlateLayoutTransform(C + FVector2f(-30.f, R + 4.f))),
			                            TEXT("BATTLE"), Bold, ESlateDrawEffect::None, FLinearColor(0.9f, 0.05f, 0.05f));
		}
	}

	// Treaty indicator (top right of the map).
	int32 Row = 0;
	for (const auto& T : S.treaties)
	{
		const FString Txt = (T.type == "ceasefire")
			? FString::Printf(TEXT("CEASEFIRE  %d turn(s) left"), T.turns_remaining)
			: FString::Printf(TEXT("SAFE PASSAGE %s via %s  %d turn(s)"), UTF8_TO_TCHAR(T.army_id.c_str()), UTF8_TO_TCHAR(T.province_id.c_str()), T.turns_remaining);
		const FVector2f Pos = FVector2f(Offset) + FVector2f(MapW * Scale - 340.f, 14.f + 22.f * Row++);
		FSlateDrawElement::MakeBox(Out, L + 3, G.ToPaintGeometry(FVector2f(330.f, 20.f), FSlateLayoutTransform(Pos)), FAppStyle::GetBrush("WhiteBrush"),
		                           ESlateDrawEffect::None, FLinearColor(0.1f, 0.35f, 0.15f, 0.9f));
		FSlateDrawElement::MakeText(Out, L + 4, G.ToPaintGeometry(FVector2f(330.f, 20.f), FSlateLayoutTransform(Pos + FVector2f(6.f, 2.f))), Txt, Small,
		                            ESlateDrawEffect::None, FLinearColor::White);
	}
	return L + 5;
}
