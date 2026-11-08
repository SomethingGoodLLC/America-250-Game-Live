#include "PA250MapFramingCommandlet.h"
#include "Campaign/PA250MapWidget.h"
#include "Campaign/PA250ProvinceMapActor.h"
#include "Campaign/PA250ArmyToken.h"
#include "Components/StaticMeshComponent.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

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

	// Plane must match the 4096x3277 presentation texture, not stretch it.
	if (!FMath::IsNearlyEqual(MapW / MapH, 4096.f / 3277.f, 0.02f))
	{
		UE_LOG(LogTemp, Error, TEXT("Framing: plane aspect %.3f != texture aspect %.3f"), MapW / MapH, 4096.f / 3277.f);
		++Failures;
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
	// Army tokens must be findable at full-map zoom: a large flag, not a 22uu sliver.
	{
		const APA250ArmyToken* TokenCDO = GetDefault<APA250ArmyToken>();
		const FVector FlagScale = TokenCDO && TokenCDO->FlagMesh ? TokenCDO->FlagMesh->GetRelativeScale3D() : FVector::ZeroVector;
		if (FlagScale.X < 0.5f || FlagScale.Y < 0.35f || !TokenCDO->NameText)
		{
			UE_LOG(LogTemp, Error, TEXT("Framing: army token flag too small or name plate missing (scale %s)"),
				*FlagScale.ToCompactString());
			++Failures;
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
