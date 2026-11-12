#include "PA250MapFramingCommandlet.h"
#include "Campaign/PA250MapWidget.h"
#include "Campaign/PA250ProvinceMapActor.h"
#include "Campaign/PA250ArmyToken.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "pa250/sim.hpp"

int32 UPA250MapFramingCommandlet::Main(const FString& Params)
{
	// Headless stand-in for Automation PA250.Map.Framing at 1920×1080.
	// Projects presentation UVs through the same ortho framing the map actor uses and asserts
	// label/token/cartouche anchors fall outside the top/bottom HUD strips.
	constexpr float ViewW = 1920.f;
	constexpr float ViewH = 1080.f;
	constexpr float TopH = UPA250MapWidget::TopStripHeightPx;
	constexpr float BotH = UPA250MapWidget::BottomStripHeightPx;
	const APA250ProvinceMapActor* CDO = GetDefault<APA250ProvinceMapActor>();
	const float MapW = CDO ? CDO->MapWidthUU : 1600.f;
	const float MapH = CDO ? CDO->MapHeightUU : 1280.f;
	const float UsableTop = TopH;
	const float UsableBot = ViewH - BotH;

	const FString PresPath = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/map_presentation.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PresPath))
	{
		UE_LOG(LogTemp, Error, TEXT("Framing: missing presentation"));
		return 1;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Framing: bad json"));
		return 2;
	}

	const float OrthoW = APA250ProvinceMapActor::FitOrthoWidth(ViewW, ViewH, MapW, MapH);
	const float OrthoH = OrthoW * (ViewH / ViewW);
	const float CenterBiasY = (BotH - TopH) * 0.5f / ViewH * OrthoH;
	auto ProjectUV = [&](float U, float V) -> FVector2D {
		// Same fit/bias as ProvinceMapActor::FrameCamera. Screen right = +X, down = +Y
		// (V is top-left origin, v-down, so small V projects above centre).
		const float WorldX = (U - 0.5f) * MapW;
		const float WorldY = (V - 0.5f) * MapH;
		const float Sx = ViewW * 0.5f + (WorldX / OrthoW) * ViewW;
		const float Sy = ViewH * 0.5f + ((WorldY - CenterBiasY) / OrthoH) * ViewH;
		return FVector2D(Sx, Sy);
	};

	int32 Failures = 0;

	auto InHud = [&](const FVector2D& P, const TCHAR* What) -> bool {
		if (P.Y < UsableTop || P.Y > UsableBot)
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: %s at (%.1f,%.1f) falls in HUD strip"), What, P.X, P.Y);
			return true;
		}
		return false;
	};

	// Plane must match the de Costa image (6761x5314 per decosta_locations.json), not stretch it.
	if (!FMath::IsNearlyEqual(MapW / MapH, 6761.f / 5314.f, 0.02f))
	{
		UE_LOG(LogTemp, Error, TEXT("Framing: plane aspect %.3f != de Costa aspect %.3f"), MapW / MapH, 6761.f / 5314.f);
		++Failures;
	}
	// Opening overview: Boston, Lexington and Concord must be on screen together,
	// inside the usable band between the HUD strips.
	{
		const FVector2D Ctr = APA250ProvinceMapActor::CampaignOverviewCenterUV();
		const float Zoom = APA250ProvinceMapActor::CampaignOverviewZoom(ViewW, ViewH, MapW, MapH);
		const float OW = APA250ProvinceMapActor::ZoomedOrthoWidth(ViewW, ViewH, MapW, MapH, Zoom);
		const float OH = OW * ViewH / ViewW;
		const float BiasY = (BotH - TopH) * 0.5f / ViewH * OH;
		const float MaxOffX = FMath::Max(0.f, (MapW - OW) * 0.5f);
		const float MaxOffY = FMath::Max(0.f, (MapH - OH) * 0.5f);
		const float CamX = FMath::Clamp((Ctr.X - 0.5f) * MapW, -MaxOffX, MaxOffX);
		const float CamY = FMath::Clamp((Ctr.Y - 0.5f) * MapH + BiasY, -MaxOffY, MaxOffY);
		auto ProjectOverview = [&](float U, float V) -> FVector2D {
			return FVector2D(ViewW * 0.5f + (((U - 0.5f) * MapW - CamX) / OW) * ViewW,
							 ViewH * 0.5f + (((V - 0.5f) * MapH - CamY) / OH) * ViewH);
		};
		const TCHAR* Names[] = {TEXT("concord"), TEXT("lexington"), TEXT("boston")};
		const FVector2D UVs[] = {FVector2D(0.143f, 0.306f), FVector2D(0.300f, 0.230f), FVector2D(0.564f, 0.49f)};
		for (int32 i = 0; i < 3; ++i)
		{
			const FVector2D P = ProjectOverview(UVs[i].X, UVs[i].Y);
			if (P.X < 0.f || P.X > ViewW || P.Y < UsableTop || P.Y > UsableBot)
			{
				UE_LOG(LogTemp, Error, TEXT("Framing: overview %s at (%.1f,%.1f) off screen / in HUD"), Names[i], P.X, P.Y);
				++Failures;
			}
		}
	}
	// Horizontal orientation: east (U=1) right of west, north (V=0) above south.
	{
		const FVector2D C = ProjectUV(0.5f, 0.5f);
		if (!(ProjectUV(1.f, 0.5f).X > C.X && ProjectUV(0.f, 0.5f).X < C.X && ProjectUV(0.5f, 0.f).Y < C.Y &&
			  ProjectUV(0.5f, 1.f).Y > C.Y))
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: UV orientation is mirrored/rotated (E/N check failed)"));
			++Failures;
		}
		// Whole plane inside the usable band at this viewport.
		if (InHud(ProjectUV(0.f, 0.f), TEXT("plane NW corner")) || InHud(ProjectUV(1.f, 1.f), TEXT("plane SE corner")))
		{
			++Failures;
		}
	}
	// Text must read horizontally: TextRender advance (-Y) -> +X, glyph-up (+Z) -> -Y.
	{
		const FRotator R = APA250ProvinceMapActor::FlatMapTextRotation();
		const FVector Advance = R.RotateVector(-FVector::YAxisVector);
		const FVector Up = R.RotateVector(FVector::ZAxisVector);
		if (!Advance.Equals(FVector::XAxisVector, 0.01f) || !Up.Equals(-FVector::YAxisVector, 0.01f))
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: text rotation advance=%s up=%s (labels would not read horizontally)"),
				*Advance.ToCompactString(), *Up.ToCompactString());
			++Failures;
		}
	}


	const TArray<TSharedPtr<FJsonValue>>* Provinces = nullptr;
	if (!Root->TryGetArrayField(TEXT("provinces"), Provinces) || !Provinces)
	{
		UE_LOG(LogTemp, Error, TEXT("Framing: no provinces"));
		return 3;
	}
	for (const TSharedPtr<FJsonValue>& V : *Provinces)
	{
		const TSharedPtr<FJsonObject> Obj = V->AsObject();
		if (!Obj.IsValid())
		{
			continue;
		}
		const FString Id = Obj->GetStringField(TEXT("id"));
		const TArray<TSharedPtr<FJsonValue>>* Luv = nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Tuv = nullptr;
		if (Obj->TryGetArrayField(TEXT("label_anchor_uv"), Luv) && Luv && Luv->Num() >= 2)
		{
			const FVector2D P = ProjectUV((*Luv)[0]->AsNumber(), (*Luv)[1]->AsNumber());
			if (InHud(P, *FString::Printf(TEXT("label %s"), *Id)))
			{
				++Failures;
			}
		}
		if (Obj->TryGetArrayField(TEXT("token_anchor_uv"), Tuv) && Tuv && Tuv->Num() >= 2)
		{
			const FVector2D P = ProjectUV((*Tuv)[0]->AsNumber(), (*Tuv)[1]->AsNumber());
			if (InHud(P, *FString::Printf(TEXT("token %s"), *Id)))
			{
				++Failures;
			}
		}
	}

	// Cartouche top-left inside neatline (UV 0.08, 0.08).
	if (InHud(ProjectUV(0.08f, 0.08f), TEXT("cartouche")))
	{
		++Failures;
	}
	// Compass bottom-left (UV 0.07, 0.90).
	if (InHud(ProjectUV(0.07f, 0.90f), TEXT("compass")))
	{
		++Failures;
	}

	// Zoom narrows the ortho width proportionally (Zoom + button path).
	{
		const float Full = APA250ProvinceMapActor::FitOrthoWidth(ViewW, ViewH, MapW, MapH);
		const float Zoom2 = APA250ProvinceMapActor::ZoomedOrthoWidth(ViewW, ViewH, MapW, MapH, 2.f);
		if (!FMath::IsNearlyEqual(Zoom2, Full * 0.5f, 0.01f))
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: zoomed ortho %.1f != half of %.1f"), Zoom2, Full);
			++Failures;
		}
	}
	// Army tokens: compact symbol at wide zoom (constant screen size, a disc — towns are diamonds),
	// illustration only when a figure is readable, and every art asset present.
	{
		const APA250ArmyToken* TokenCDO = GetDefault<APA250ArmyToken>();
		if (!TokenCDO || !TokenCDO->FlagMesh || !TokenCDO->SpriteMesh || !TokenCDO->NameText || !TokenCDO->StrengthText)
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: army token components missing"));
			++Failures;
		}
		if (APA250ArmyToken::CompactSymbolPx < 20.f || APA250ArmyToken::CompactSymbolPx > 44.f ||
			APA250ArmyToken::RevealFigurePx <= APA250ArmyToken::HideFigurePx)
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: army LOD thresholds out of range (symbol %.0fpx, reveal %.0f / hide %.0f)"),
				   APA250ArmyToken::CompactSymbolPx, APA250ArmyToken::RevealFigurePx, APA250ArmyToken::HideFigurePx);
			++Failures;
		}
		for (const EPA250ArmyArt Art :
			 {EPA250ArmyArt::ProvincialRegulars, EPA250ArmyArt::BritishMarching, EPA250ArmyArt::BritishFiring, EPA250ArmyArt::Militia})
		{
			const FPA250ArmyArtSpec& Spec = APA250ArmyToken::ArtSpec(Art);
			const UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, Spec.TexturePath);
			const FVector2D Size = APA250ArmyToken::ArtWorldSize(Art);
			int32 TexW = 0;
			int32 TexH = 0;
#if WITH_EDITORONLY_DATA
			if (Tex)
			{
				// -nullrhi has no GPU resource; the imported source keeps the real size.
				TexW = static_cast<int32>(Tex->Source.GetSizeX());
				TexH = static_cast<int32>(Tex->Source.GetSizeY());
			}
#endif
			if (!Tex || TexW <= 0 || TexH <= 0 || Size.X <= 0.f || Size.Y <= 0.f || Spec.UScale <= 0.f || Spec.VScale <= 0.f)
			{
				UE_LOG(LogTemp, Error, TEXT("Framing: army art %d missing or empty (%s)"), static_cast<int32>(Art), Spec.TexturePath);
				++Failures;
				continue;
			}
			// Content rect must match the texture's content aspect (prepare_army_sprites.py output).
			const float TexAspect = TexW * Spec.UScale / FMath::Max(1.f, TexH * Spec.VScale);
			if (!FMath::IsNearlyEqual(TexAspect, Spec.Aspect, 0.02f))
			{
				UE_LOG(LogTemp, Error, TEXT("Framing: army art %d aspect %.3f != spec %.3f"), static_cast<int32>(Art), TexAspect, Spec.Aspect);
				++Failures;
			}
			UE_LOG(LogTemp, Display, TEXT("Framing: army art %d %dx%d world %.1fx%.1f UU"), static_cast<int32>(Art), TexW, TexH, Size.X,
				   Size.Y);
		}
		if (!LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/M_MapSprite.M_MapSprite")))
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: M_MapSprite missing"));
			++Failures;
		}
	}
	// Art follows state and troop role only: provincial regulars art never stands for a militia force,
	// and the (Patriot) militia art never stands for a British force.
	{
		pa250::CampaignState St;
		auto Make = [](const char* Id, const char* Faction, int Line, int Militia) {
			pa250::Army A;
			A.id = Id;
			A.faction = Faction;
			A.province = "boston";
			A.units = {{"line_infantry", Line}, {"militia", Militia}};
			return A;
		};
		struct FCase
		{
			const TCHAR* Name;
			pa250::Army Army;
			bool bMarching;
			bool bEngaged;
			EPA250ArmyArt Want;
		};
		const FCase Cases[] = {
			{TEXT("continental militia-majority (slice opening army 4+18)"), Make("c1", "continental", 4, 18), false, false, EPA250ArmyArt::Militia},
			{TEXT("continental militia only, marching and engaged (no variant)"), Make("c4", "continental", 0, 18), true, true, EPA250ArmyArt::Militia},
			{TEXT("continental regular-majority"), Make("c2", "continental", 12, 4), false, false, EPA250ArmyArt::ProvincialRegulars},
			{TEXT("continental regulars marching (no variant)"), Make("c3", "continental", 12, 0), true, false, EPA250ArmyArt::ProvincialRegulars},
			{TEXT("british at rest"), Make("b1", "british", 24, 0), false, false, EPA250ArmyArt::None},
			{TEXT("british marching"), Make("b2", "british", 24, 0), true, false, EPA250ArmyArt::BritishMarching},
			{TEXT("british engaged"), Make("b3", "british", 24, 0), true, true, EPA250ArmyArt::BritishFiring},
			{TEXT("british militia-majority"), Make("b4", "british", 2, 10), true, false, EPA250ArmyArt::None},
		};
		for (const FCase& C : Cases)
		{
			pa250::Army A = C.Army;
			if (C.bMarching)
			{
				A.path = {"some_edge"};
			}
			St.pending_battle = pa250::PendingBattle();
			if (C.bEngaged)
			{
				St.pending_battle.active = true;
				St.pending_battle.attacker_army = A.id;
			}
			const EPA250ArmyArt Got = APA250ProvinceMapActor::ArtForArmy(St, A);
			if (Got != C.Want)
			{
				UE_LOG(LogTemp, Error, TEXT("Framing: art rule %s -> %d, want %d"), C.Name, static_cast<int32>(Got), static_cast<int32>(C.Want));
				++Failures;
			}
		}
		UE_LOG(LogTemp, Display, TEXT("Framing: art rule cases checked %d"), static_cast<int32>(UE_ARRAY_COUNT(Cases)));
	}
	// Army slots on the de Costa sheet: for every town and both sides, alone and with an enemy
	// in the same town, the slot covers no town hotspot (click radius) and no caption.
	{
		const FString LocPath = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/decosta_locations.json");
		FString LocText;
		TSharedPtr<FJsonObject> Loc;
		if (!FFileHelper::LoadFileToString(LocText, *LocPath) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(LocText), Loc) ||
			!Loc.IsValid())
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: cannot read decosta_locations.json"));
			++Failures;
		}
		else
		{
			FString ScenText;
			FFileHelper::LoadFileToString(ScenText, *FPaths::ConvertRelativePathToFull(
														FPaths::Combine(FPaths::ProjectDir(), TEXT("../PA250Sim/data/boston1775.json"))));
			const pa250::CampaignState Scenario = pa250::load_scenario(TCHAR_TO_UTF8(*ScenText));
			if (Scenario.provinces.size() != 13)
			{
				UE_LOG(LogTemp, Error, TEXT("Framing: scenario provinces %d != 13"), static_cast<int32>(Scenario.provinces.size()));
				++Failures;
			}
			const float ImgW = Loc->GetObjectField(TEXT("image_size"))->GetNumberField(TEXT("width"));
			const float ImgH = Loc->GetObjectField(TEXT("image_size"))->GetNumberField(TEXT("height"));
			const float DW = CDO ? CDO->MapWidthUU : 1600.f;
			const float DH = DW * ImgH / FMath::Max(1.f, ImgW);
			TMap<FString, FVector2D> UVs;
			TMap<FString, float> Radii;
			TMap<FString, FString> Names;
			for (const TSharedPtr<FJsonValue>& V : Loc->GetArrayField(TEXT("provinces")))
			{
				const TSharedPtr<FJsonObject> P = V->AsObject();
				const FString Id = P->GetStringField(TEXT("id"));
				const TArray<TSharedPtr<FJsonValue>>& M = P->GetArrayField(TEXT("marker_uv"));
				UVs.Add(Id, FVector2D(M[0]->AsNumber(), M[1]->AsNumber()));
				Radii.Add(Id, P->GetNumberField(TEXT("hit_radius_px")));
				// Caption text = scenario display name, exactly as the runtime lays it out.
				const auto It = Scenario.provinces.find(TCHAR_TO_UTF8(*Id));
				Names.Add(Id, It != Scenario.provinces.end() ? FString(UTF8_TO_TCHAR(It->second.name.c_str())) : Id);
			}
			const APA250ProvinceMapActor::FTownObstacles Towns =
				APA250ProvinceMapActor::BuildTownObstacles(UVs, Radii, Names, ImgW, DW, DH);
			const FVector2D ContFoot = APA250ArmyToken::FactionFootprintUU(TEXT("continental"));
			const FVector2D BritFoot = APA250ArmyToken::FactionFootprintUU(TEXT("british"));
			const FVector2D ContPlate = APA250ArmyToken::PlateSizeUU(TEXT("Militia"), TEXT("Yours"));
			const FVector2D BritPlate = APA250ArmyToken::PlateSizeUU(TEXT("Reprisal"), TEXT("British"));
			int32 Checked = 0;
			for (const FString& Id : Towns.Ids)
			{
				// Player's militia first (it keeps its slot), then the British arriving in the same town.
				TArray<FBox2D> Placed;
				float Ov = 0.f;
				int32 Cand = 0;
				for (int32 Side = 0; Side < 2; ++Side)
				{
					const FVector2D& Foot = Side == 0 ? ContFoot : BritFoot;
					const FVector2D& Plate = Side == 0 ? ContPlate : BritPlate;
					const FVector2D A = APA250ProvinceMapActor::ChooseArmyAnchor(Id, Towns, Placed, Foot, Plate, Ov, Cand);
					++Checked;
					if (Ov > 0.f)
					{
						UE_LOG(LogTemp, Error, TEXT("Framing: army slot at %s (army %d) overlaps towns/captions/armies by %.1f UU^2"), *Id,
							   Placed.Num() / 2, Ov);
						++Failures;
					}
					FBox2D ArtBox, PlateBox;
					APA250ProvinceMapActor::ArmySlotBoxes(A, Foot, Plate, ArtBox, PlateBox);
					Placed.Add(ArtBox);
					Placed.Add(PlateBox);
				}
			}
			UE_LOG(LogTemp, Display, TEXT("Framing: army slots checked %d (13 towns x militia + regulars together)"), Checked);

			// Ownership wash: asset present; every town washed; no wash reaches past 75% of the way to a neighbour.
			if (!LoadObject<UTexture2D>(nullptr, TEXT("/Game/PA250/Textures/Army/T_Map_TownWash.T_Map_TownWash")))
			{
				UE_LOG(LogTemp, Error, TEXT("Framing: T_Map_TownWash missing"));
				++Failures;
			}
			TMap<FString, float> WashOverride;
			for (const TSharedPtr<FJsonValue>& V : Loc->GetArrayField(TEXT("provinces")))
			{
				double W = 0.0;
				if (V->AsObject()->TryGetNumberField(TEXT("wash_radius_px"), W))
				{
					WashOverride.Add(V->AsObject()->GetStringField(TEXT("id")), static_cast<float>(W));
				}
			}
			for (const TPair<FString, FVector2D>& Kv : UVs)
			{
				const float R = APA250ProvinceMapActor::TownWashRadiusPx(Kv.Key, UVs, Radii, WashOverride, ImgW, ImgH);
				float Nearest = TNumericLimits<float>::Max();
				for (const TPair<FString, FVector2D>& O : UVs)
				{
					if (O.Key != Kv.Key)
					{
						Nearest = FMath::Min(Nearest, FVector2D::Distance(FVector2D(Kv.Value.X * ImgW, Kv.Value.Y * ImgH),
																		  FVector2D(O.Value.X * ImgW, O.Value.Y * ImgH)));
					}
				}
				if (R <= 0.f || R > Nearest * APA250ProvinceMapActor::TownWashNeighbourCap + 0.5f)
				{
					UE_LOG(LogTemp, Error, TEXT("Framing: town wash %s radius %.0fpx (nearest town %.0fpx)"), *Kv.Key, R, Nearest);
					++Failures;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("Framing: town washes checked %d"), UVs.Num());

			// Traced roads: each joins its two towns' hotspots and is a real sim edge; a column halfway
			// along stands on the road (within 1 UU of a road vertex segment), not on the straight chord.
			const TSharedPtr<FJsonObject>* RoutesObj = nullptr;
			const TSharedPtr<FJsonObject>* RouteEdges = nullptr;
			int32 RoutesChecked = 0;
			if (Loc->TryGetObjectField(TEXT("routes"), RoutesObj) && (*RoutesObj)->TryGetObjectField(TEXT("edges"), RouteEdges))
			{
				for (const TPair<FString, TSharedPtr<FJsonValue>>& R : (*RouteEdges)->Values)
				{
					const auto E = Scenario.edges.find(TCHAR_TO_UTF8(*R.Key));
					if (E == Scenario.edges.end())
					{
						UE_LOG(LogTemp, Error, TEXT("Framing: route %s is not a sim edge"), *R.Key);
						++Failures;
						continue;
					}
					TArray<FVector2D> Road;
					for (const TSharedPtr<FJsonValue>& P : R.Value->AsObject()->GetArrayField(TEXT("polyline_uv")))
					{
						Road.Add(FVector2D(P->AsArray()[0]->AsNumber() * ImgW, P->AsArray()[1]->AsNumber() * ImgH)); // image px
					}
					const FVector2D* A = UVs.Find(UTF8_TO_TCHAR(E->second.a.c_str()));
					const FVector2D* B = UVs.Find(UTF8_TO_TCHAR(E->second.b.c_str()));
					auto Px = [&](const FVector2D* UV) { return FVector2D(UV->X * ImgW, UV->Y * ImgH); };
					const bool bEnds = A && B && Road.Num() >= 2 &&
									   ((FVector2D::Distance(Road[0], Px(A)) < 2.f && FVector2D::Distance(Road.Last(), Px(B)) < 2.f) ||
										(FVector2D::Distance(Road[0], Px(B)) < 2.f && FVector2D::Distance(Road.Last(), Px(A)) < 2.f));
					if (!bEnds)
					{
						UE_LOG(LogTemp, Error, TEXT("Framing: route %s does not start/end at its towns' hotspots"), *R.Key);
						++Failures;
						continue;
					}
					const FVector2D Mid = APA250ProvinceMapActor::PointAlongPath(Road, 0.5f);
					float OnRoad = TNumericLimits<float>::Max();
					for (int32 k = 0; k + 1 < Road.Num(); ++k)
					{
						OnRoad = FMath::Min(OnRoad, FMath::PointDistToSegment(FVector(Mid, 0.f), FVector(Road[k], 0.f), FVector(Road[k + 1], 0.f)));
					}
					const float OffChord = FMath::PointDistToSegment(FVector(Mid, 0.f), FVector(Road[0], 0.f), FVector(Road.Last(), 0.f));
					UE_LOG(LogTemp, Display, TEXT("Framing: route %s halfway point %.1f px from road, %.0f px from straight chord"), *R.Key, OnRoad,
						   OffChord);
					if (OnRoad > 1.f)
					{
						++Failures;
					}
					++RoutesChecked;
				}
			}
			UE_LOG(LogTemp, Display, TEXT("Framing: traced routes checked %d"), RoutesChecked);
		}
	}

	if (Failures > 0)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250.Map.Framing failed (%d anchors in HUD)"), Failures);
		return 4;
	}
	UE_LOG(LogTemp, Display, TEXT("PA250.Map.Framing passed"));
	return 0;
}
