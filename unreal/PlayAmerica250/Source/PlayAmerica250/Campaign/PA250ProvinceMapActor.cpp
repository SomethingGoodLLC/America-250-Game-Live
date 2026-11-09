#include "PA250ProvinceMapActor.h"
#include "PA250ArmyToken.h"
#include "PA250CampaignGameMode.h"
#include "PA250MapWidget.h"
#include "PA250Subsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Engine/Scene.h"
#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "PixelFormat.h"
#include "TextureResource.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

namespace
{
FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

FString ShortArmyName(const pa250::Army& A)
{
	const FString Name = ToFStr(A.name);
	if (Name.Contains(TEXT("Militia")))
	{
		return TEXT("Militia");
	}
	if (Name.Contains(TEXT("Regulars")))
	{
		return TEXT("Regulars");
	}
	if (Name.Contains(TEXT("Reprisal")))
	{
		return TEXT("Reprisal");
	}
	return Name;
}

int32 TotalStrength(const pa250::Army& A)
{
	int32 N = 0;
	for (const auto& U : A.units)
	{
		N += FMath::Max(0, U.second);
	}
	return N;
}

int32 UnitCount(const pa250::Army& A, const char* Cls)
{
	const auto It = A.units.find(Cls);
	return It == A.units.end() ? 0 : FMath::Max(0, It->second);
}

/**
 * Illustration from campaign state only. A variant appears only when its state exists:
 * marching = the army has a path; firing = the army is a party to the pending battle.
 * Art is matched to nationality and role: British line infantry, and provincial regulars for a
 * Continental force that is at least half line infantry. Anything else keeps the compact symbol:
 * a militia-majority force (no militia art was supplied) and a British force standing still
 * (no neutral British art was supplied). Figure count never encodes strength.
 */
EPA250ArmyArt ChooseArmyArt(const pa250::CampaignState& St, const pa250::Army& A)
{
	const int32 Total = FMath::Max(1, TotalStrength(A));
	if (A.faction == "british")
	{
		if (UnitCount(A, "line_infantry") * 2 < Total)
		{
			return EPA250ArmyArt::None;
		}
		const bool bEngaged = St.pending_battle.active && (St.pending_battle.attacker_army == A.id || St.pending_battle.defender_army == A.id);
		if (bEngaged)
		{
			return EPA250ArmyArt::BritishFiring;
		}
		if (!A.path.empty())
		{
			return EPA250ArmyArt::BritishMarching;
		}
		return EPA250ArmyArt::None;
	}
	if (A.faction == "continental" && UnitCount(A, "line_infantry") * 2 >= Total)
	{
		return EPA250ArmyArt::ProvincialRegulars;
	}
	return EPA250ArmyArt::None;
}

/** Second plate line: never colour alone — the side is always written. */
FString SideWordFor(const pa250::CampaignState& St, const pa250::Army& A)
{
	if (A.faction == St.player_faction)
	{
		return TEXT("Yours");
	}
	return A.faction == "british" ? FString(TEXT("British")) : ToFStr(A.faction);
}

/** Province the army is heading into on its current edge, or empty. */
std::string NextProvinceOnPath(const pa250::CampaignState& St, const pa250::Army& A)
{
	if (A.path.empty())
	{
		return std::string();
	}
	const auto It = St.edges.find(A.path.front());
	if (It == St.edges.end())
	{
		return std::string();
	}
	return It->second.a == A.province ? It->second.b : It->second.a;
}

float BoxOverlapArea(const FBox2D& A, const FBox2D& B)
{
	const float W = FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X);
	const float H = FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y);
	return (W > 0.f && H > 0.f) ? W * H : 0.f;
}

float BoxCircleOverlap(const FBox2D& B, const FVector2D& C, float R)
{
	// Conservative: overlap of the box with the circle's bounding square, only if the circle touches the box.
	const FVector2D Closest(FMath::Clamp(C.X, B.Min.X, B.Max.X), FMath::Clamp(C.Y, B.Min.Y, B.Max.Y));
	if (FVector2D::DistSquared(Closest, C) > R * R)
	{
		return 0.f;
	}
	return FMath::Max(1.f, BoxOverlapArea(B, FBox2D(C - FVector2D(R, R), C + FVector2D(R, R))));
}

/** Water feature labels (GAME INVENTION positions for the provisional SVG). */
struct FWaterLabel
{
	const TCHAR* Name;
	float U;
	float V;
};
const FWaterLabel GWaterLabels[] = {
	{TEXT("Boston Harbor"), 0.88f, 0.58f},
	{TEXT("Massachusetts Bay"), 0.93f, 0.32f},
	{TEXT("Charles River"), 0.34f, 0.60f},
	{TEXT("Back Bay"), 0.64f, 0.58f},
	{TEXT("Charles Basin"), 0.50f, 0.55f},
};

FLinearColor TownOwnerColor(const std::string& Owner)
{
	if (Owner == "british")
	{
		return FLinearColor(0.78f, 0.12f, 0.10f);
	}
	if (Owner == "continental")
	{
		return FLinearColor(0.10f, 0.28f, 0.72f);
	}
	return FLinearColor(0.28f, 0.22f, 0.14f);
}

FColor TownOwnerInk(const std::string& Owner)
{
	if (Owner == "british")
	{
		return FColor(150, 28, 22);
	}
	if (Owner == "continental")
	{
		return FColor(16, 48, 130);
	}
	return FColor(40, 32, 20);
}

void ApplyShapeColor(UStaticMeshComponent* Mesh, const FLinearColor& Color)
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
		MID->SetVectorParameterValue(TEXT("Color"), Color);
		MID->SetVectorParameterValue(TEXT("BaseColor"), Color);
	}
}
} // namespace

FRotator APA250ProvinceMapActor::FlatMapTextRotation()
{
	// TextRender advance is -Y and glyph-up is +Z. Pitch 90 lays it flat facing +Z;
	// yaw 90 then turns advance to +X (east, screen right) and glyph-up to -Y
	// (north, screen up) for the top-down camera, so labels read horizontally.
	return FRotator(90.f, 90.f, 0.f);
}

// Caption layout shared by SpawnHotspotMarkers and army slot placement.
static const FVector2D GCaptionOffsetUV(0.014f, -0.02f);
static constexpr float GCaptionSizeUU = 19.f;
static constexpr float GCaptionSelectedSizeUU = 23.f;

APA250ProvinceMapActor::FTownObstacles APA250ProvinceMapActor::BuildTownObstacles(const TMap<FString, FVector2D>& HotspotUV,
																				  const TMap<FString, float>& RadiusPx,
																				  const TMap<FString, FString>& Names, float ImageWidthPx,
																				  float MapW, float MapH)
{
	FTownObstacles T;
	TArray<FString> Keys;
	HotspotUV.GetKeys(Keys);
	Keys.Sort();
	const float PxToUU = MapW / FMath::Max(1.f, ImageWidthPx);
	for (const FString& Id : Keys)
	{
		const FVector2D UV = HotspotUV[Id];
		const FVector2D P((UV.X - 0.5f) * MapW, (UV.Y - 0.5f) * MapH);
		const float* Rpx = RadiusPx.Find(Id);
		T.Ids.Add(Id);
		T.Centers.Add(P);
		T.Radii.Add((Rpx ? *Rpx : 90.f) * PxToUU);
		const FString* Name = Names.Find(Id);
		const int32 Len = Name ? Name->Len() : Id.Len();
		const FVector2D C((UV.X + GCaptionOffsetUV.X - 0.5f) * MapW, (UV.Y + GCaptionOffsetUV.Y - 0.5f) * MapH);
		const float W = Len * GCaptionSelectedSizeUU * 0.55f;
		const float H = GCaptionSelectedSizeUU * 1.2f;
		T.Captions.Add(FBox2D(FVector2D(C.X, C.Y - H * 0.5f), FVector2D(C.X + W, C.Y + H * 0.5f)));
	}
	return T;
}

void APA250ProvinceMapActor::ArmySlotBoxes(const FVector2D& Anchor, const FVector2D& ArtSize, const FVector2D& PlateSize, FBox2D& OutArt,
										   FBox2D& OutPlate)
{
	OutArt = FBox2D(FVector2D(Anchor.X - ArtSize.X * 0.5f, Anchor.Y - ArtSize.Y), FVector2D(Anchor.X + ArtSize.X * 0.5f, Anchor.Y + 1.f));
	OutPlate = FBox2D(FVector2D(Anchor.X - PlateSize.X * 0.5f, Anchor.Y), FVector2D(Anchor.X + PlateSize.X * 0.5f, Anchor.Y + PlateSize.Y));
}

FVector2D APA250ProvinceMapActor::ChooseArmyAnchor(const FString& HomeId, const FTownObstacles& Towns, const TArray<FBox2D>& Placed,
												   const FVector2D& ArtSize, const FVector2D& PlateSize, float& OutOverlap, int32& OutCandidate)
{
	const int32 Home = Towns.Ids.IndexOfByKey(HomeId);
	const FVector2D P = Home != INDEX_NONE ? Towns.Centers[Home] : FVector2D::ZeroVector;
	const float R = Home != INDEX_NONE ? Towns.Radii[Home] : 21.f;
	const float FW = FMath::Max(ArtSize.X, PlateSize.X);
	const float AH = ArtSize.Y;
	const float PH = PlateSize.Y;
	TArray<FVector2D> Cand;
	for (const float G : {R + 3.f, R + 27.f, R + 51.f})
	{
		// East/west sit 4 UU below the town line so the art clears the caption band above it.
		Cand.Add(P + FVector2D(G + FW * 0.5f, AH * 0.5f + 4.f));				 // east
		Cand.Add(P + FVector2D(-(G + FW * 0.5f), AH * 0.5f + 4.f));			 // west
		Cand.Add(P + FVector2D(0.f, G + AH));									 // south
		Cand.Add(P + FVector2D(0.7f * G + FW * 0.5f, 0.7f * G + AH));			 // south-east
		Cand.Add(P + FVector2D(-(0.7f * G + FW * 0.5f), 0.7f * G + AH));		 // south-west
		Cand.Add(P + FVector2D(0.f, -(G + PH)));								 // north (plate between art and town)
		Cand.Add(P + FVector2D(0.7f * G + FW * 0.5f, -(0.7f * G + PH)));		 // north-east
		Cand.Add(P + FVector2D(-(0.7f * G + FW * 0.5f), -(0.7f * G + PH)));	 // north-west
	}
	constexpr float Margin = 2.f;
	float Best = TNumericLimits<float>::Max();
	int32 BestIdx = 0;
	for (int32 i = 0; i < Cand.Num(); ++i)
	{
		FBox2D ArtBox, PlateBox;
		ArmySlotBoxes(Cand[i], ArtSize, PlateSize, ArtBox, PlateBox);
		ArtBox = ArtBox.ExpandBy(Margin);
		PlateBox = PlateBox.ExpandBy(Margin);
		float Score = 0.f;
		for (const FBox2D& B : {ArtBox, PlateBox})
		{
			for (int32 t = 0; t < Towns.Ids.Num(); ++t)
			{
				Score += BoxCircleOverlap(B, Towns.Centers[t], Towns.Radii[t]);
				Score += BoxOverlapArea(B, Towns.Captions[t]);
			}
			for (const FBox2D& O : Placed)
			{
				Score += BoxOverlapArea(B, O);
			}
		}
		if (Score < Best)
		{
			Best = Score;
			BestIdx = i;
			if (Score <= 0.f)
			{
				break;
			}
		}
	}
	OutOverlap = Best;
	OutCandidate = BestIdx;
	return Cand[BestIdx];
}

EPA250ArmyArt APA250ProvinceMapActor::ArtForArmy(const pa250::CampaignState& St, const pa250::Army& A) { return ChooseArmyArt(St, A); }

APA250ProvinceMapActor::APA250ProvinceMapActor()
{
	PrimaryActorTick.bCanEverTick = true;
	MapMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("MapMesh"));
	SetRootComponent(MapMesh);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane"));
	if (Plane.Succeeded())
	{
		MapMesh->SetStaticMesh(Plane.Object);
	}
	MapMesh->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	MapMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
	MapMesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	MapMesh->SetGenerateOverlapEvents(false);
}

void APA250ProvinceMapActor::BeginPlay()
{
	Super::BeginPlay();
	// Keep the plane in XY (facing +Z). A -90° pitch tips it edge-on to a top-down camera → black.
	SetActorRotation(FRotator::ZeroRotator);
	SetActorLocation(FVector(0.f, 0.f, MapPlaneZ));
	MapMesh->SetCastShadow(false);

	LoadTextures();
	LoadPresentation();
	if (ImageW > 0 && ImageH > 0)
	{
		MapHeightUU = MapWidthUU * (static_cast<float>(ImageH) / static_cast<float>(ImageW));
	}
	MapMesh->SetRelativeScale3D(FVector(MapWidthUU / 100.f, MapHeightUU / 100.f, 1.f));
	EnsureMaterial();
	RebuildStateTexture();
	if (bDeCostaMode)
	{
		// Engraving already carries town names; do not redraw stylized labels/cartouche.
		SpawnHotspotMarkers();
	}
	else
	{
		SpawnLabels();
		SpawnWaterLabels();
		SpawnFramingDecor();
	}
	RefreshArmyTokens();
	RefreshBattleMarker();
	ResetOverview();
}

void APA250ProvinceMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	static bool bLoggedResidentTexture = false;
	if (!bLoggedResidentTexture && DisplayTex && DisplayTex->GetSizeX() > 32)
	{
		bLoggedResidentTexture = true;
		const int32 NumMips = DisplayTex->GetPlatformData() ? DisplayTex->GetPlatformData()->Mips.Num() : -1;
		UE_LOG(LogTemp, Display, TEXT("PA250 display texture resident: %dx%d format=%s mips=%d"), DisplayTex->GetSizeX(),
			   DisplayTex->GetSizeY(), GetPixelFormatString(DisplayTex->GetPixelFormat()), NumMips);
	}
	RebuildStateTexture();
	RefreshHotspotVisibility();
	RefreshTownOwnership();
	RefreshArmyTokens();
	UpdateArmyLod();
	RefreshBattleMarker();
	RefreshRoutes();
}

UPA250Subsystem* APA250ProvinceMapActor::Sub() const
{
	return UPA250Subsystem::Get(const_cast<APA250ProvinceMapActor*>(this));
}

UTexture2D* APA250ProvinceMapActor::LoadPngToTexture(const FString& AbsPath)
{
	TArray<uint8> Compressed;
	if (!FFileHelper::LoadFileToArray(Compressed, *AbsPath) || Compressed.Num() < 8)
	{
		return nullptr;
	}
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()))
	{
		return nullptr;
	}
	TArray64<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return nullptr;
	}
	UTexture2D* Tex = UTexture2D::CreateTransient(Wrapper->GetWidth(), Wrapper->GetHeight(), PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Raw.GetData(), Raw.Num());
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	return Tex;
}

UTexture2D* APA250ProvinceMapActor::LoadJpegToTexture(const FString& AbsPath)
{
	TArray<uint8> Compressed;
	if (!FFileHelper::LoadFileToArray(Compressed, *AbsPath) || Compressed.Num() < 8)
	{
		return nullptr;
	}
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(FName("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::JPEG);
	if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Compressed.GetData(), Compressed.Num()))
	{
		return nullptr;
	}
	TArray64<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Raw))
	{
		return nullptr;
	}
	UTexture2D* Tex = UTexture2D::CreateTransient(Wrapper->GetWidth(), Wrapper->GetHeight(), PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	Tex->NeverStream = true;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Raw.GetData(), Raw.Num());
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	ImageW = Wrapper->GetWidth();
	ImageH = Wrapper->GetHeight();
	return Tex;
}

void APA250ProvinceMapActor::LoadTextures()
{
	ProvinceIdTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_ProvinceID.T_ProvinceID"));
	BorderDistTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_BorderDist.T_BorderDist"));
	CoastDistTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_CoastDist.T_CoastDist"));
	PaperTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_Parchment.T_Parchment"));

	DisplayTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/PA250/Textures/T_DeCostaBostonConcord.T_DeCostaBostonConcord"));
	if (DisplayTex)
	{
		bDeCostaMode = true;
		ImageW = DisplayTex->GetSizeX();
		ImageH = DisplayTex->GetSizeY();
		DisplayTex->NeverStream = true;
	}
	if (!DisplayTex)
	{
		const FString DeCostaJpg = FPaths::ProjectContentDir() / TEXT("PA250/Source/M01-de-costa-boston-concord.jpg");
		DisplayTex = LoadJpegToTexture(DeCostaJpg);
		if (DisplayTex)
		{
			bDeCostaMode = true;
			UE_LOG(LogTemp, Display, TEXT("PA250: loaded de Costa JPEG fallback %dx%d"), ImageW, ImageH);
		}
	}
	if (!DisplayTex)
	{
		DisplayTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_MapPreview.T_MapPreview"));
		bDeCostaMode = false;
	}
	if (!DisplayTex)
	{
		const FString PreviewPng = FPaths::ConvertRelativePathToFull(
			FPaths::Combine(FPaths::ProjectDir(), TEXT("../../tools/mapgen/out/preview_display.png")));
		DisplayTex = LoadPngToTexture(PreviewPng);
		if (!DisplayTex)
		{
			const FString Alt = FPaths::ConvertRelativePathToFull(
				FPaths::Combine(FPaths::ProjectDir(), TEXT("../../tools/mapgen/out/preview.png")));
			DisplayTex = LoadPngToTexture(Alt);
		}
	}
	if (!DisplayTex)
	{
		DisplayTex = ProvinceIdTex;
	}
	if (!PaperTex)
	{
		PaperTex = LoadObject<UTexture2D>(nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
	}
	// Prefer the simple Unlit display material — never depends on lights or Custom ush wiring.
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Map/Boston1775/M_MapDisplay.M_MapDisplay"));
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Map/Boston1775/M_ProvinceMap.M_ProvinceMap"));
	}
	if (!BaseMaterial)
	{
		BaseMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/EngineMaterials/DefaultTextMaterialOpaque.DefaultTextMaterialOpaque"));
	}
	UE_LOG(LogTemp, Display, TEXT("PA250 map assets: mat=%s display=%s decosta=%d"),
		BaseMaterial ? *BaseMaterial->GetName() : TEXT("NONE"), DisplayTex ? *DisplayTex->GetName() : TEXT("NONE"),
		bDeCostaMode ? 1 : 0);
	if (DisplayTex)
	{
		const int32 NumMips = DisplayTex->GetPlatformData() ? DisplayTex->GetPlatformData()->Mips.Num() : -1;
		UE_LOG(LogTemp, Display, TEXT("PA250 display texture: %dx%d format=%s mips=%d never_stream=%d"),
			   DisplayTex->GetSizeX(), DisplayTex->GetSizeY(), GetPixelFormatString(DisplayTex->GetPixelFormat()), NumMips,
			   DisplayTex->NeverStream ? 1 : 0);
	}

	// ProvinceIdLookup stays available for tooling; de Costa picking uses hotspot JSON instead.
	const FString BinPath = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/ProvinceIdLookup.bin");
	if (FFileHelper::LoadFileToArray(LookupBytes, *BinPath) && LookupBytes.Num() >= 8)
	{
		LookupW = static_cast<int32>(*reinterpret_cast<const uint32*>(LookupBytes.GetData()));
		LookupH = static_cast<int32>(*reinterpret_cast<const uint32*>(LookupBytes.GetData() + 4));
		if (!bDeCostaMode)
		{
			TexW = LookupW;
			TexH = LookupH;
		}
	}
	else if (!bDeCostaMode)
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250: missing ProvinceIdLookup.bin"));
	}
	if (bDeCostaMode && ImageW > 0 && ImageH > 0)
	{
		TexW = ImageW;
		TexH = ImageH;
	}
}

bool APA250ProvinceMapActor::LoadDeCostaLocations()
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/decosta_locations.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250: missing decosta_locations.json"));
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return false;
	}
	LabelUV.Reset();
	TokenUV.Reset();
	HitRadiusPx.Reset();
	VerifiedById.Reset();
	Edges.Reset();

	const TSharedPtr<FJsonObject>* SizeObj = nullptr;
	if (Root->TryGetObjectField(TEXT("image_size"), SizeObj) && SizeObj && SizeObj->IsValid())
	{
		ImageW = static_cast<int32>((*SizeObj)->GetNumberField(TEXT("width")));
		ImageH = static_cast<int32>((*SizeObj)->GetNumberField(TEXT("height")));
		TexW = ImageW;
		TexH = ImageH;
	}

	const TArray<TSharedPtr<FJsonValue>>* Provinces = nullptr;
	if (Root->TryGetArrayField(TEXT("provinces"), Provinces) && Provinces)
	{
		for (const TSharedPtr<FJsonValue>& V : *Provinces)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}
			const FString Id = Obj->GetStringField(TEXT("id"));
			const TArray<TSharedPtr<FJsonValue>>* Muv = nullptr;
			if (Obj->TryGetArrayField(TEXT("marker_uv"), Muv) && Muv && Muv->Num() >= 2)
			{
				const FVector2D UV((*Muv)[0]->AsNumber(), (*Muv)[1]->AsNumber());
				LabelUV.Add(Id, UV);
				TokenUV.Add(Id, UV);
			}
			HitRadiusPx.Add(Id, static_cast<float>(Obj->GetNumberField(TEXT("hit_radius_px"))));
			double WashPx = 0.0;
			if (Obj->TryGetNumberField(TEXT("wash_radius_px"), WashPx) && WashPx > 0.0)
			{
				WashRadiusOverridePx.Add(Id, static_cast<float>(WashPx));
			}
			VerifiedById.Add(Id, Obj->GetBoolField(TEXT("verified")));
		}
	}

	// Route polylines: straight segments between hotspot endpoints from sim adjacency (not pixel miles).
	if (UPA250Subsystem* S = Sub())
	{
		for (const auto& Kv : S->GetState().edges)
		{
			const FString A = ToFStr(Kv.second.a);
			const FString B = ToFStr(Kv.second.b);
			if (!TokenUV.Contains(A) || !TokenUV.Contains(B))
			{
				continue;
			}
			FEdgeVis E;
			E.From = A;
			E.To = B;
			E.Type = ToFStr(Kv.second.type);
			E.PolyUV.Add(TokenUV[A]);
			E.PolyUV.Add(TokenUV[B]);
			Edges.Add(MoveTemp(E));
		}
	}
	return LabelUV.Num() >= 13;
}

void APA250ProvinceMapActor::LoadPresentation()
{
	LabelUV.Reset();
	TokenUV.Reset();
	HitRadiusPx.Reset();
	WashRadiusOverridePx.Reset();
	VerifiedById.Reset();
	Edges.Reset();
	MapIndexById.Reset();
	IdByMapIndex.Reset();

	if (bDeCostaMode && LoadDeCostaLocations())
	{
		if (UPA250Subsystem* S = Sub())
		{
			for (const auto& Kv : S->GetState().provinces)
			{
				MapIndexById.Add(ToFStr(Kv.first), Kv.second.map_index);
				IdByMapIndex.Add(Kv.second.map_index, ToFStr(Kv.first));
			}
		}
		bPresentationLoaded = LabelUV.Num() >= 13;
		UE_LOG(LogTemp, Display, TEXT("PA250: de Costa locations loaded (%d provinces)"), LabelUV.Num());
		return;
	}

	const FString PresPath = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/map_presentation.json");
	FString Text;
	if (!FFileHelper::LoadFileToString(Text, *PresPath))
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250: missing map_presentation.json"));
		return;
	}
	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		return;
	}
	const TArray<TSharedPtr<FJsonValue>>* Provinces = nullptr;
	if (Root->TryGetArrayField(TEXT("provinces"), Provinces) && Provinces)
	{
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
				LabelUV.Add(Id, FVector2D((*Luv)[0]->AsNumber(), (*Luv)[1]->AsNumber()));
			}
			if (Obj->TryGetArrayField(TEXT("token_anchor_uv"), Tuv) && Tuv && Tuv->Num() >= 2)
			{
				TokenUV.Add(Id, FVector2D((*Tuv)[0]->AsNumber(), (*Tuv)[1]->AsNumber()));
			}
			else if (LabelUV.Contains(Id))
			{
				TokenUV.Add(Id, LabelUV[Id]);
			}
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* EdgeArr = nullptr;
	if (Root->TryGetArrayField(TEXT("edges"), EdgeArr) && EdgeArr)
	{
		for (const TSharedPtr<FJsonValue>& V : *EdgeArr)
		{
			const TSharedPtr<FJsonObject> Obj = V->AsObject();
			if (!Obj.IsValid())
			{
				continue;
			}
			FEdgeVis E;
			E.From = Obj->GetStringField(TEXT("a"));
			E.To = Obj->GetStringField(TEXT("b"));
			E.Type = Obj->GetStringField(TEXT("type"));
			const TArray<TSharedPtr<FJsonValue>>* Poly = nullptr;
			if (Obj->TryGetArrayField(TEXT("polyline_uv"), Poly) && Poly)
			{
				for (const TSharedPtr<FJsonValue>& P : *Poly)
				{
					const TArray<TSharedPtr<FJsonValue>>* Pt = nullptr;
					if (P->TryGetArray(Pt) && Pt && Pt->Num() >= 2)
					{
						E.PolyUV.Add(FVector2D((*Pt)[0]->AsNumber(), (*Pt)[1]->AsNumber()));
					}
				}
			}
			Edges.Add(MoveTemp(E));
		}
	}

	if (UPA250Subsystem* S = Sub())
	{
		for (const auto& Kv : S->GetState().provinces)
		{
			MapIndexById.Add(ToFStr(Kv.first), Kv.second.map_index);
			IdByMapIndex.Add(Kv.second.map_index, ToFStr(Kv.first));
		}
	}
	bPresentationLoaded = LabelUV.Num() >= 13;
}

void APA250ProvinceMapActor::EnsureMaterial()
{
	if (!BaseMaterial)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250: no BaseMaterial — map will be black"));
		return;
	}
	DynMat = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	MapMesh->SetMaterial(0, DynMat);
	MapMesh->SetVisibility(true);
	MapMesh->SetHiddenInGame(false);
	if (DisplayTex)
	{
		DynMat->SetTextureParameterValue(TEXT("Display"), DisplayTex);
	}
	if (ProvinceIdTex)
	{
		DynMat->SetTextureParameterValue(TEXT("ProvinceID"), ProvinceIdTex);
		// If Display param is missing on an older mat, still show something.
		if (!DisplayTex)
		{
			DynMat->SetTextureParameterValue(TEXT("Display"), ProvinceIdTex);
		}
	}
	if (BorderDistTex)
	{
		DynMat->SetTextureParameterValue(TEXT("BorderDist"), BorderDistTex);
	}
	if (CoastDistTex)
	{
		DynMat->SetTextureParameterValue(TEXT("CoastDist"), CoastDistTex);
	}
	if (PaperTex)
	{
		DynMat->SetTextureParameterValue(TEXT("Paper"), PaperTex);
	}
	DynMat->SetScalarParameterValue(TEXT("BandPx"), 18.f);
	DynMat->SetVectorParameterValue(TEXT("InkColor"), FLinearColor(0.12f, 0.08f, 0.04f));
	DynMat->SetVectorParameterValue(TEXT("PaperBlue"), FLinearColor(0.55f, 0.66f, 0.70f));
	DynMat->SetVectorParameterValue(TEXT("Color"), FLinearColor(0.82f, 0.74f, 0.55f));
	DynMat->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.82f, 0.74f, 0.55f));
}

void APA250ProvinceMapActor::RebuildStateTexture()
{
	// De Costa overview keeps the engraving free of ownership wash overlays.
	if (bDeCostaMode)
	{
		return;
	}
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign() || !DynMat)
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	int32 W = 1;
	while (W < static_cast<int32>(St.provinces.size()) + 1)
	{
		W <<= 1;
	}
	if (!StateTex || StateTex->GetSizeX() != W)
	{
		StateTex = UTexture2D::CreateTransient(W, 2, PF_B8G8R8A8);
		StateTex->Filter = TF_Nearest;
		StateTex->SRGB = false;
		StateTex->UpdateResource();
		DynMat->SetTextureParameterValue(TEXT("ProvinceState"), StateTex);
	}

	int32 SelIdx = 0;
	if (const int32* Found = MapIndexById.Find(SelectedProvinceId))
	{
		SelIdx = *Found;
	}

	TArray<FColor> Pixels;
	Pixels.SetNumZeroed(W * 2);
	auto OwnerColor = [](const std::string& Owner) -> FColor {
		if (Owner == "british")
		{
			return FColor(200, 48, 48, 255);
		}
		if (Owner == "continental")
		{
			return FColor(48, 72, 180, 255);
		}
		return FColor(0, 0, 0, 0); // unowned → bare parchment
	};
	for (const auto& Kv : St.provinces)
	{
		const int32 Idx = Kv.second.map_index;
		if (Idx <= 0 || Idx >= W)
		{
			continue;
		}
		Pixels[Idx] = OwnerColor(Kv.second.owner);
		Pixels[W + Idx] = FColor(Idx == SelIdx ? 255 : 0, 0, 0, Idx == SelIdx ? 255 : 0);
	}
	FTexture2DMipMap& Mip = StateTex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	StateTex->UpdateResource();
}

bool APA250ProvinceMapActor::WorldToUV(const FVector& World, FVector2D& OutUV) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(World);
	OutUV.X = FMath::Clamp(Local.X / 100.f + 0.5f, 0.f, 1.f);
	OutUV.Y = FMath::Clamp(Local.Y / 100.f + 0.5f, 0.f, 1.f);
	return true;
}

FVector APA250ProvinceMapActor::UVToWorld(FVector2D UV, float ZOffset) const
{
	const FVector Local((UV.X - 0.5f) * 100.f, (UV.Y - 0.5f) * 100.f, ZOffset);
	return GetActorTransform().TransformPosition(Local);
}

FString APA250ProvinceMapActor::ProvinceAtUV(FVector2D UV) const
{
	if (bDeCostaMode)
	{
		const float ImgW = ImageW > 0 ? static_cast<float>(ImageW) : 1.f;
		const float ImgH = ImageH > 0 ? static_cast<float>(ImageH) : 1.f;
		FString BestId;
		float BestDist = TNumericLimits<float>::Max();
		for (const TPair<FString, FVector2D>& Kv : LabelUV)
		{
			const float Du = (UV.X - Kv.Value.X) * ImgW;
			const float Dv = (UV.Y - Kv.Value.Y) * ImgH;
			const float Dist = FMath::Sqrt(Du * Du + Dv * Dv);
			float Radius = 90.f;
			if (const float* R = HitRadiusPx.Find(Kv.Key))
			{
				Radius = *R;
			}
			if (Dist <= Radius && Dist < BestDist)
			{
				BestDist = Dist;
				BestId = Kv.Key;
			}
		}
		return BestId;
	}
	if (LookupW <= 0 || LookupBytes.Num() < 8)
	{
		return FString();
	}
	const uint16* Data = reinterpret_cast<const uint16*>(LookupBytes.GetData() + 8);
	const int32 X = FMath::Clamp(int32(UV.X * LookupW), 0, LookupW - 1);
	const int32 Y = FMath::Clamp(int32(UV.Y * LookupH), 0, LookupH - 1);
	const uint16 Idx = Data[Y * LookupW + X];
	if (Idx == 0)
	{
		return FString();
	}
	if (const FString* Found = IdByMapIndex.Find(Idx))
	{
		return *Found;
	}
	return FString();
}

FString APA250ProvinceMapActor::ProvinceAtWorld(const FVector& World) const
{
	FVector2D UV;
	if (!WorldToUV(World, UV))
	{
		return FString();
	}
	return ProvinceAtUV(UV);
}

FString APA250ProvinceMapActor::ArmyAtWorld(const FVector& World) const
{
	// Only what is drawn is clickable: the illustration's content rect (padding was cropped) or the
	// compact disc. Slots never cover a town hotspot, so a town click is never taken by an army.
	// Selected army wins where two overlap (it draws on top).
	FString Hit;
	for (const APA250ArmyToken* Tok : ArmyTokens)
	{
		if (!IsValid(Tok) || !Tok->HitTest(World))
		{
			continue;
		}
		if (Tok->ArmyId == SelectedArmyId)
		{
			return Tok->ArmyId;
		}
		if (Hit.IsEmpty())
		{
			Hit = Tok->ArmyId;
		}
	}
	return Hit;
}

FString APA250ProvinceMapActor::ProvinceForArmy(const FString& ArmyId) const
{
	UPA250Subsystem* S = Sub();
	if (!S || ArmyId.IsEmpty() || !S->GetController().has_campaign())
	{
		return FString();
	}
	const auto It = S->GetState().armies.find(TCHAR_TO_UTF8(*ArmyId));
	if (It == S->GetState().armies.end())
	{
		return FString();
	}
	return ToFStr(It->second.province);
}

void APA250ProvinceMapActor::SpawnLabels()
{
	for (UTextRenderComponent* C : ProvinceLabels)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	ProvinceLabels.Reset();

	UPA250Subsystem* S = Sub();
	const pa250::CampaignState* St = (S && S->GetController().has_campaign()) ? &S->GetState() : nullptr;

	static const TCHAR* Required[] = {TEXT("concord"), TEXT("lincoln"), TEXT("lexington"), TEXT("menotomy"),
		TEXT("cambridge"), TEXT("charlestown"), TEXT("medford"), TEXT("watertown"), TEXT("brookline"), TEXT("roxbury"),
		TEXT("boston"), TEXT("dorchester"), TEXT("chelsea")};

	for (const TCHAR* Id : Required)
	{
		const FVector2D* UV = LabelUV.Find(Id);
		if (!UV)
		{
			UE_LOG(LogTemp, Warning, TEXT("PA250: missing label_anchor_uv for %s"), Id);
			continue;
		}
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetupAttachment(MapMesh);
		T->RegisterComponent();
		T->SetAbsolute(true, true, true);
		T->SetWorldLocation(UVToWorld(*UV, 4.f));
		T->SetWorldRotation(APA250ProvinceMapActor::FlatMapTextRotation());
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetVerticalAlignment(EVRTA_TextCenter);
		T->SetWorldSize(28.f);
		T->SetTextRenderColor(FColor(32, 24, 12));
		FString Name = Id;
		if (St)
		{
			auto It = St->provinces.find(TCHAR_TO_UTF8(Id));
			if (It != St->provinces.end())
			{
				Name = ToFStr(It->second.name);
			}
		}
		T->SetText(FText::FromString(Name));
		ProvinceLabels.Add(T);
	}
}

void APA250ProvinceMapActor::SpawnWaterLabels()
{
	for (UTextRenderComponent* C : WaterLabels)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	WaterLabels.Reset();
	for (const FWaterLabel& W : GWaterLabels)
	{
		UTextRenderComponent* T = NewObject<UTextRenderComponent>(this);
		T->SetupAttachment(MapMesh);
		T->RegisterComponent();
		T->SetAbsolute(true, true, true);
		T->SetWorldLocation(UVToWorld(FVector2D(W.U, W.V), 3.f));
		T->SetWorldRotation(APA250ProvinceMapActor::FlatMapTextRotation());
		T->SetHorizontalAlignment(EHTA_Center);
		T->SetWorldSize(22.f);
		T->SetTextRenderColor(FColor(40, 70, 90));
		T->SetText(FText::FromString(W.Name));
		WaterLabels.Add(T);
	}
}

void APA250ProvinceMapActor::SpawnFramingDecor()
{
	if (!CartoucheText)
	{
		CartoucheText = NewObject<UTextRenderComponent>(this);
		CartoucheText->SetupAttachment(MapMesh);
		CartoucheText->RegisterComponent();
	}
	CartoucheText->SetAbsolute(true, true, true);
	CartoucheText->SetWorldLocation(UVToWorld(FVector2D(0.08f, 0.08f), 5.f));
	CartoucheText->SetWorldRotation(APA250ProvinceMapActor::FlatMapTextRotation());
	CartoucheText->SetHorizontalAlignment(EHTA_Left);
	CartoucheText->SetWorldSize(36.f);
	CartoucheText->SetTextRenderColor(FColor(28, 20, 10));
	CartoucheText->SetText(FText::FromString(TEXT("Boston & Environs, 1775")));

	if (!CompassText)
	{
		CompassText = NewObject<UTextRenderComponent>(this);
		CompassText->SetupAttachment(MapMesh);
		CompassText->RegisterComponent();
	}
	CompassText->SetAbsolute(true, true, true);
	CompassText->SetWorldLocation(UVToWorld(FVector2D(0.07f, 0.90f), 5.f));
	CompassText->SetWorldRotation(APA250ProvinceMapActor::FlatMapTextRotation());
	CompassText->SetHorizontalAlignment(EHTA_Center);
	CompassText->SetWorldSize(40.f);
	CompassText->SetTextRenderColor(FColor(28, 20, 10));
	CompassText->SetText(FText::FromString(TEXT("N\nW+E\nS")));
}

void APA250ProvinceMapActor::RefreshArmyTokens()
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign())
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();

	// One entry per army: the player's forces first (they keep their slot when an enemy arrives),
	// then the rest; each group in id order (std::map), so placement is deterministic.
	struct FPlan
	{
		const pa250::Army* Army = nullptr;
		EPA250ArmyArt Art = EPA250ArmyArt::None;
		std::string Next;
	};
	TArray<FPlan> Plans;
	for (int32 Pass = 0; Pass < 2; ++Pass)
	{
		for (const auto& Kv : St.armies)
		{
			const bool bPlayer = Kv.second.faction == St.player_faction;
			if ((Pass == 0) != bPlayer)
			{
				continue;
			}
			Plans.Add({&Kv.second, ChooseArmyArt(St, Kv.second), NextProvinceOnPath(St, Kv.second)});
		}
	}

	FString Sig = SelectedArmyId + TEXT("|");
	for (const FPlan& Pl : Plans)
	{
		Sig += FString::Printf(TEXT("%s@%s>%s#%d;"), UTF8_TO_TCHAR(Pl.Army->id.c_str()), UTF8_TO_TCHAR(Pl.Army->province.c_str()),
							   UTF8_TO_TCHAR(Pl.Next.c_str()), static_cast<int32>(Pl.Art));
		for (const auto& U : Pl.Army->units)
		{
			Sig += FString::Printf(TEXT("%s%d"), UTF8_TO_TCHAR(U.first.c_str()), U.second);
		}
	}
	if (Sig == LastTokenSig && ArmyTokens.Num() == Plans.Num())
	{
		return;
	}
	LastTokenSig = Sig;

	for (APA250ArmyToken* T : ArmyTokens)
	{
		if (IsValid(T))
		{
			T->Destroy();
		}
	}
	ArmyTokens.Reset();
	LoggedIllustrationById.Reset();

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TMap<FString, FString> Names;
	for (const auto& Kv : St.provinces)
	{
		Names.Add(ToFStr(Kv.first), ToFStr(Kv.second.name));
	}
	const FTownObstacles Towns = BuildTownObstacles(TokenUV, HitRadiusPx, Names, ImageW > 0 ? ImageW : 6768.f, MapWidthUU, MapHeightUU);

	// Pass 1: anchors. Slot = this side's largest art + plate, so art/LOD switches never move the anchor.
	TArray<FBox2D> Placed;
	TArray<FVector2D> Anchors;
	for (const FPlan& Pl : Plans)
	{
		const FString Pid = ToFStr(Pl.Army->province);
		const FVector2D Foot = APA250ArmyToken::FactionFootprintUU(ToFStr(Pl.Army->faction));
		const FVector2D Plate = APA250ArmyToken::PlateSizeUU(ShortArmyName(*Pl.Army), SideWordFor(St, *Pl.Army));
		float Overlap = 0.f;
		int32 Candidate = 0;
		const FVector2D A = ChooseArmyAnchor(Pid, Towns, Placed, Foot, Plate, Overlap, Candidate);
		FBox2D ArtBox, PlateBox;
		ArmySlotBoxes(A, Foot, Plate, ArtBox, PlateBox);
		Placed.Add(ArtBox);
		Placed.Add(PlateBox);
		Anchors.Add(A);
		UE_LOG(LogTemp, Display, TEXT("PA250 army slot: %s at %s candidate=%d overlap=%.1f anchor=(%.1f,%.1f) art=%d next=%s"),
			   UTF8_TO_TCHAR(Pl.Army->id.c_str()), *Pid, Candidate, Overlap, A.X, A.Y, static_cast<int32>(Pl.Art),
			   UTF8_TO_TCHAR(Pl.Next.c_str()));
	}

	// Pass 2: spawn. Art faces east as drawn; mirror when marching west or when the enemy it is
	// engaged with stands to the west.
	for (int32 i = 0; i < Plans.Num(); ++i)
	{
		const FPlan& Pl = Plans[i];
		const pa250::Army& Army = *Pl.Army;
		bool bMirror = false;
		if (Pl.Art == EPA250ArmyArt::BritishMarching && !Pl.Next.empty())
		{
			const FVector2D* From = TokenUV.Find(ToFStr(Army.province));
			const FVector2D* To = TokenUV.Find(ToFStr(Pl.Next));
			bMirror = From && To && To->X < From->X;
		}
		else if (Pl.Art == EPA250ArmyArt::BritishFiring)
		{
			const std::string Foe =
				St.pending_battle.attacker_army == Army.id ? St.pending_battle.defender_army : St.pending_battle.attacker_army;
			for (int32 j = 0; j < Plans.Num(); ++j)
			{
				if (j != i && Plans[j].Army->id == Foe)
				{
					bMirror = Anchors[j].X < Anchors[i].X;
				}
			}
		}
		FActorSpawnParameters Params;
		Params.Owner = this;
		const FVector Loc(Anchors[i].X, Anchors[i].Y, 14.f + i * 0.5f);
		APA250ArmyToken* Tok =
			World->SpawnActor<APA250ArmyToken>(APA250ArmyToken::StaticClass(), Loc, APA250ProvinceMapActor::FlatMapTextRotation(), Params);
		if (!Tok)
		{
			continue;
		}
		// Plate = short name + side word. Strength stays in the HUD selection panel (and enemy strength unreported).
		Tok->Configure(ToFStr(Army.id), ToFStr(Army.faction), ShortArmyName(Army), SideWordFor(St, Army), ToFStr(Army.id) == SelectedArmyId,
					   Pl.Art, bMirror);
		ArmyTokens.Add(Tok);
	}
	UpdateArmyLod();
}

float APA250ProvinceMapActor::MeasurePxPerUU() const
{
	// Measured from the real projection, not from ZoomFactor: robust to camera aspect handling,
	// window resizes and DPI.
	const UWorld* World = GetWorld();
	const APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
	if (!PC)
	{
		return 0.f;
	}
	FVector2D S0, S1;
	const FVector O = UVToWorld(FVector2D(0.5f, 0.5f), 14.f);
	if (!PC->ProjectWorldLocationToScreen(O, S0) || !PC->ProjectWorldLocationToScreen(O + FVector(100.f, 0.f, 0.f), S1))
	{
		return 0.f;
	}
	return FMath::Abs(S1.X - S0.X) / 100.f;
}

void APA250ProvinceMapActor::UpdateArmyLod()
{
	const float PxPerUU = MeasurePxPerUU();
	for (APA250ArmyToken* Tok : ArmyTokens)
	{
		if (!IsValid(Tok))
		{
			continue;
		}
		Tok->ApplyLod(PxPerUU);
		const bool bIllus = Tok->IsShowingIllustration();
		const bool* Prev = LoggedIllustrationById.Find(Tok->ArmyId);
		if (!Prev || *Prev != bIllus)
		{
			LoggedIllustrationById.Add(Tok->ArmyId, bIllus);
			UE_LOG(LogTemp, Display, TEXT("PA250 army art: %s art=%d lod=%s figure_px=%.1f px_per_uu=%.3f"), *Tok->ArmyId,
				   static_cast<int32>(Tok->GetArt()), bIllus ? TEXT("illustration") : TEXT("compact"),
				   APA250ArmyToken::FigureHeightUU * PxPerUU, PxPerUU);
		}
	}
}

void APA250ProvinceMapActor::RefreshBattleMarker()
{
	UPA250Subsystem* S = Sub();
	const bool bActive = S && S->GetController().has_campaign() && S->GetState().pending_battle.active;
	if (!bActive)
	{
		if (BattleMarker)
		{
			BattleMarker->SetVisibility(false);
		}
		return;
	}
	if (!BattleMarker)
	{
		BattleMarker = NewObject<UTextRenderComponent>(this);
		BattleMarker->SetupAttachment(MapMesh);
		BattleMarker->RegisterComponent();
		BattleMarker->SetAbsolute(true, true, true);
		BattleMarker->SetWorldRotation(APA250ProvinceMapActor::FlatMapTextRotation());
		BattleMarker->SetHorizontalAlignment(EHTA_Center);
		BattleMarker->SetWorldSize(48.f);
		BattleMarker->SetTextRenderColor(FColor(160, 20, 20));
		BattleMarker->SetText(FText::FromString(TEXT("X")));
	}
	const FString Pid = ToFStr(S->GetState().pending_battle.province);
	const FVector2D* Token = TokenUV.Find(Pid);
	const FVector2D* Label = LabelUV.Find(Pid);
	FVector2D UV = Token ? *Token : FVector2D(0.5f, 0.5f);
	if (Label && Token)
	{
		FVector2D Delta = (*Token - *Label).GetSafeNormal();
		if (Delta.IsNearlyZero())
		{
			Delta = FVector2D(0.f, 0.04f);
		}
		UV = *Token + Delta * 0.045f + FVector2D(0.02f, -0.03f);
	}
	else
	{
		UV += FVector2D(0.02f, -0.04f);
	}
	BattleMarker->SetText(FText::FromString(TEXT("++"))); // crossed swords stand-in (engine font)
	BattleMarker->SetWorldLocation(UVToWorld(UV, 10.f));
	BattleMarker->SetVisibility(true);
}

void APA250ProvinceMapActor::RefreshRoutes()
{
	UPA250Subsystem* S = Sub();
	if (!S || !S->GetController().has_campaign() || SelectedArmyId.IsEmpty() || !GetWorld())
	{
		return;
	}
	const pa250::CampaignState& St = S->GetState();
	auto It = St.armies.find(TCHAR_TO_UTF8(*SelectedArmyId));
	if (It == St.armies.end())
	{
		return;
	}
	const pa250::Army& Army = It->second;
	TSet<FString> Planned;
	for (const auto& Eid : Army.path)
	{
		Planned.Add(ToFStr(Eid));
	}
	for (const auto& Cmd : St.pending_commands)
	{
		if (Cmd.payload.kind == pa250::CommandKind::MoveArmy && Cmd.payload.army_id == Army.id)
		{
			for (const auto& Eid : Cmd.payload.edge_path)
			{
				Planned.Add(ToFStr(Eid));
			}
		}
	}
	const FString ArmyProv = ToFStr(Army.province);

	for (const FEdgeVis& E : Edges)
	{
		bool bOnPlan = false;
		for (const auto& Kv : St.edges)
		{
			const bool bMatch = (ToFStr(Kv.second.a) == E.From && ToFStr(Kv.second.b) == E.To) ||
								(ToFStr(Kv.second.a) == E.To && ToFStr(Kv.second.b) == E.From);
			if (bMatch && Planned.Contains(ToFStr(Kv.first)))
			{
				bOnPlan = true;
				break;
			}
		}
		const bool bIncident = (E.From == ArmyProv || E.To == ArmyProv);
		if (!bIncident && !bOnPlan)
		{
			continue;
		}
		if (E.PolyUV.Num() < 2)
		{
			continue;
		}
		const bool bWater =
			E.Type.Equals(TEXT("ferry"), ESearchCase::IgnoreCase) || E.Type.Equals(TEXT("boat"), ESearchCase::IgnoreCase);
		const FColor Col = bOnPlan ? FColor(220, 180, 40) : FColor(90, 70, 40);
		const float Thickness = bOnPlan ? 6.f : 2.5f;
		for (int32 i = 0; i + 1 < E.PolyUV.Num(); ++i)
		{
			const FVector A = UVToWorld(E.PolyUV[i], 6.f);
			const FVector B = UVToWorld(E.PolyUV[i + 1], 6.f);
			if (bWater && !bOnPlan)
			{
				const FVector Delta = B - A;
				const float Len = Delta.Size();
				const FVector Dir = Delta.GetSafeNormal();
				for (float T = 0.f; T < Len; T += 40.f)
				{
					DrawDebugLine(GetWorld(), A + Dir * T, A + Dir * FMath::Min(T + 18.f, Len), Col, false, -1.f, 0, Thickness);
				}
			}
			else
			{
				DrawDebugLine(GetWorld(), A, B, Col, false, -1.f, 0, Thickness);
			}
		}
	}
}

float APA250ProvinceMapActor::FitOrthoWidth(float ViewW, float ViewH, float MapW, float MapH)
{
	const float UsableH = FMath::Max(1.f, ViewH - UPA250MapWidget::TopStripHeightPx - UPA250MapWidget::BottomStripHeightPx);
	// Fit width, and fit height into the usable strip between the HUD bands.
	return FMath::Max(MapW, MapH * ViewW / UsableH) * 1.04f;
}

float APA250ProvinceMapActor::CampaignOverviewZoom(float ViewW, float ViewH, float MapW, float MapH)
{
	// Boston (0.565,0.49) to Concord (0.108,0.235), with Cambridge/Lexington inside.
	const float SpanU = 0.565f - 0.108f;
	const float SpanV = 0.49f - 0.235f;
	const float Fit = FitOrthoWidth(ViewW, ViewH, MapW, MapH);
	const float DesiredW = FMath::Max(SpanU * MapW / 0.68f, SpanV * MapH * (ViewW / FMath::Max(1.f, ViewH)) / 0.68f);
	return FMath::Clamp(Fit / FMath::Max(1.f, DesiredW), 1.f, 4.f);
}

void APA250ProvinceMapActor::ZoomBy(float Factor)
{
	const float Old = ZoomFactor;
	ZoomFactor = FMath::Clamp(ZoomFactor * Factor, 1.f, MaxZoomFactor);
	if (ZoomFactor <= 1.01f && Old > ZoomFactor)
	{
		ZoomCenterUV = CampaignOverviewCenterUV();
	}
	else if (const FVector2D* TokUV = TokenUV.Find(SelectedProvinceId))
	{
		ZoomCenterUV = *TokUV;
	}
	else if (const FVector2D* LabUV = LabelUV.Find(SelectedProvinceId))
	{
		ZoomCenterUV = *LabUV;
	}
	FrameCamera();
}

void APA250ProvinceMapActor::ZoomAtUV(FVector2D CursorUV, float Factor)
{
	const float NewZoom = FMath::Clamp(ZoomFactor * Factor, 1.f, MaxZoomFactor);
	const float Applied = NewZoom / FMath::Max(0.01f, ZoomFactor);
	ZoomCenterUV = CursorUV - (CursorUV - ZoomCenterUV) / Applied;
	ZoomCenterUV.X = FMath::Clamp(ZoomCenterUV.X, 0.f, 1.f);
	ZoomCenterUV.Y = FMath::Clamp(ZoomCenterUV.Y, 0.f, 1.f);
	ZoomFactor = NewZoom;
	FrameCamera();
}

void APA250ProvinceMapActor::FocusSelected()
{
	if (const FVector2D* UV = TokenUV.Find(SelectedProvinceId))
	{
		ZoomCenterUV = *UV;
		ZoomFactor = FMath::Max(ZoomFactor, 3.f);
		FrameCamera();
		return;
	}
	if (const FVector2D* UV = LabelUV.Find(SelectedProvinceId))
	{
		ZoomCenterUV = *UV;
		ZoomFactor = FMath::Max(ZoomFactor, 3.f);
		FrameCamera();
		return;
	}
	ResetOverview();
}

void APA250ProvinceMapActor::ResetOverview()
{
	float ViewW = 1920.f;
	float ViewH = 1080.f;
	if (UWorld* World = GetWorld())
	{
		if (APlayerController* PC = World->GetFirstPlayerController())
		{
			int32 VX = 0;
			int32 VY = 0;
			PC->GetViewportSize(VX, VY);
			if (VX > 0 && VY > 0)
			{
				ViewW = static_cast<float>(VX);
				ViewH = static_cast<float>(VY);
			}
		}
	}
	ZoomFactor = CampaignOverviewZoom(ViewW, ViewH, MapWidthUU, MapHeightUU);
	ZoomCenterUV = CampaignOverviewCenterUV();
	FrameCamera();
}

void APA250ProvinceMapActor::SetView(FVector2D CenterUV, float Zoom)
{
	ZoomFactor = FMath::Clamp(Zoom, 1.f, MaxZoomFactor);
	ZoomCenterUV = FVector2D(FMath::Clamp(CenterUV.X, 0.f, 1.f), FMath::Clamp(CenterUV.Y, 0.f, 1.f));
	FrameCamera();
}

bool APA250ProvinceMapActor::GetArmyTokenLocation(const FString& ArmyId, FVector& OutWorld) const
{
	for (const APA250ArmyToken* Tok : ArmyTokens)
	{
		if (IsValid(Tok) && Tok->ArmyId == ArmyId)
		{
			OutWorld = Tok->GetActorLocation();
			return true;
		}
	}
	return false;
}

FString APA250ProvinceMapActor::DescribeArmyTokens() const
{
	TArray<FString> Lines;
	for (const APA250ArmyToken* Tok : ArmyTokens)
	{
		if (!IsValid(Tok))
		{
			continue;
		}
		FVector2D UV;
		WorldToUV(Tok->GetActorLocation(), UV);
		const FBox2D R = Tok->GetDrawnRect();
		Lines.Add(FString::Printf(TEXT("%s anchor=(%.1f,%.1f) uv=(%.4f,%.4f) art=%d lod=%s drawn=[%.1f,%.1f]-[%.1f,%.1f]"), *Tok->ArmyId,
								  Tok->GetActorLocation().X, Tok->GetActorLocation().Y, UV.X, UV.Y, static_cast<int32>(Tok->GetArt()),
								  Tok->IsShowingIllustration() ? TEXT("illustration") : TEXT("compact"), R.Min.X, R.Min.Y, R.Max.X, R.Max.Y));
	}
	return FString::Join(Lines, TEXT(" | "));
}

void APA250ProvinceMapActor::PanByUV(FVector2D DeltaUV)
{
	if (ZoomFactor <= 1.01f)
	{
		return;
	}
	ZoomCenterUV.X = FMath::Clamp(ZoomCenterUV.X + DeltaUV.X, 0.f, 1.f);
	ZoomCenterUV.Y = FMath::Clamp(ZoomCenterUV.Y + DeltaUV.Y, 0.f, 1.f);
	FrameCamera();
}

void APA250ProvinceMapActor::SetCenterUV(FVector2D CenterUV)
{
	if (ZoomFactor <= 1.01f)
	{
		return;
	}
	ZoomCenterUV = FVector2D(FMath::Clamp(CenterUV.X, 0.f, 1.f), FMath::Clamp(CenterUV.Y, 0.f, 1.f));
	FrameCamera();
}

bool APA250ProvinceMapActor::IsProvinceVerified(const FString& ProvinceId) const
{
	if (const bool* V = VerifiedById.Find(ProvinceId))
	{
		return *V;
	}
	return false;
}

void APA250ProvinceMapActor::SpawnHotspotMarkers()
{
	for (UStaticMeshComponent* C : HotspotMarkers)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	for (UTextRenderComponent* C : HotspotCaptions)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	for (UStaticMeshComponent* C : TownWashes)
	{
		if (C)
		{
			C->DestroyComponent();
		}
	}
	HotspotMarkers.Reset();
	HotspotCaptions.Reset();
	CaptionById.Reset();
	TownWashes.Reset();
	WashById.Reset();
	if (SelectionHalo)
	{
		SelectionHalo->DestroyComponent();
		SelectionHalo = nullptr;
	}

	UPA250Subsystem* S = Sub();
	const pa250::CampaignState* St = (S && S->GetController().has_campaign()) ? &S->GetState() : nullptr;

	UStaticMesh* PlaneMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	UMaterialInterface* ShapeMat =
		LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!PlaneMesh || !ShapeMat)
	{
		return;
	}

	// Selection halo: gold, never red. Red is British ownership; a red halo made
	// a selected Continental town look like it had changed hands.
	SelectionHalo = NewObject<UStaticMeshComponent>(this);
	SelectionHalo->SetupAttachment(MapMesh);
	SelectionHalo->RegisterComponent();
	SelectionHalo->SetAbsolute(true, true, true);
	SelectionHalo->SetStaticMesh(PlaneMesh);
	SelectionHalo->SetMaterial(0, ShapeMat);
	SelectionHalo->SetWorldRotation(FRotator(0.f, 45.f, 0.f));
	SelectionHalo->SetWorldScale3D(FVector(0.40f, 0.40f, 1.f));
	SelectionHalo->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	SelectionHalo->SetVisibility(false);
	ApplyShapeColor(SelectionHalo, FLinearColor(0.86f, 0.64f, 0.12f));

	UMaterialInterface* WashMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/PA250/Materials/M_MapSprite.M_MapSprite"));
	UTexture2D* WashTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/PA250/Textures/Army/T_Map_TownWash.T_Map_TownWash"));
	if (!WashMat || !WashTex)
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250: town wash assets missing (material=%d texture=%d); ownership shown by diamonds only"),
			   WashMat ? 1 : 0, WashTex ? 1 : 0);
	}
	const float PxToUU = MapWidthUU / FMath::Max(1.f, static_cast<float>(ImageW > 0 ? ImageW : 6768));

	for (const TPair<FString, FVector2D>& Kv : LabelUV)
	{
		// Ownership wash: a soft hand-colour tint around the town (period map convention), under the
		// diamond and caption. It marks the town that is held, never a territory boundary.
		std::string WashOwner;
		if (St)
		{
			auto It = St->provinces.find(TCHAR_TO_UTF8(*Kv.Key));
			if (It != St->provinces.end())
			{
				WashOwner = It->second.owner;
			}
		}
		if (WashMat && WashTex)
		{
			const float Rpx = TownWashRadiusPx(Kv.Key, LabelUV, HitRadiusPx, WashRadiusOverridePx, static_cast<float>(ImageW),
											   static_cast<float>(ImageH));
			const float R = Rpx * PxToUU;
			UStaticMeshComponent* Wash = NewObject<UStaticMeshComponent>(this);
			Wash->SetupAttachment(MapMesh);
			Wash->RegisterComponent();
			Wash->SetAbsolute(true, true, true);
			Wash->SetStaticMesh(PlaneMesh);
			Wash->SetMaterial(0, WashMat);
			Wash->SetWorldLocation(UVToWorld(Kv.Value, 3.f));
			Wash->SetWorldRotation(FRotator::ZeroRotator);
			Wash->SetWorldScale3D(FVector(2.f * R / 100.f, 2.f * R / 100.f, 1.f));
			Wash->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Wash->SetCastShadow(false);
			Wash->SetTranslucentSortPriority(0);
			if (UMaterialInstanceDynamic* MID = Wash->CreateAndSetMaterialInstanceDynamic(0))
			{
				MID->SetTextureParameterValue(TEXT("Sprite"), WashTex);
				MID->SetVectorParameterValue(TEXT("UVRect"), FLinearColor(0.f, 0.f, 1.f, 1.f));
				MID->SetScalarParameterValue(TEXT("Opacity"), TownWashOpacity);
			}
			ApplyTownWash(Wash, WashOwner);
			TownWashes.Add(Wash);
			WashById.Add(Kv.Key, Wash);
			UE_LOG(LogTemp, Display, TEXT("PA250 town wash: %s owner=%s radius=%.0fpx (%.1f UU)"), *Kv.Key, UTF8_TO_TCHAR(WashOwner.c_str()), Rpx, R);
		}

		// Town edge: a faction-coloured diamond ring. The inner diamond is parchment
		// so the marker reads as an outline, not as a solid army flag.
		std::string Owner;
		if (St)
		{
			auto It = St->provinces.find(TCHAR_TO_UTF8(*Kv.Key));
			if (It != St->provinces.end())
			{
				Owner = It->second.owner;
			}
		}
		UStaticMeshComponent* Ring = NewObject<UStaticMeshComponent>(this);
		Ring->SetupAttachment(MapMesh);
		Ring->RegisterComponent();
		Ring->SetAbsolute(true, true, true);
		Ring->SetStaticMesh(PlaneMesh);
		Ring->SetMaterial(0, ShapeMat);
		Ring->SetWorldLocation(UVToWorld(Kv.Value, 5.f));
		Ring->SetWorldRotation(FRotator(0.f, 45.f, 0.f));
		Ring->SetWorldScale3D(FVector(0.24f, 0.24f, 1.f));
		Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Ring->ComponentTags.Add(TEXT("town_ring"));
		Ring->ComponentTags.Add(FName(*Kv.Key));
		ApplyShapeColor(Ring, TownOwnerColor(Owner));
		HotspotMarkers.Add(Ring);

		UStaticMeshComponent* Fill = NewObject<UStaticMeshComponent>(this);
		Fill->SetupAttachment(MapMesh);
		Fill->RegisterComponent();
		Fill->SetAbsolute(true, true, true);
		Fill->SetStaticMesh(PlaneMesh);
		Fill->SetMaterial(0, ShapeMat);
		Fill->SetWorldLocation(UVToWorld(Kv.Value, 5.8f));
		Fill->SetWorldRotation(FRotator(0.f, 45.f, 0.f));
		Fill->SetWorldScale3D(FVector(0.13f, 0.13f, 1.f));
		Fill->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ApplyShapeColor(Fill, FLinearColor(0.86f, 0.78f, 0.62f));
		HotspotMarkers.Add(Fill);

		// Caption: short name only, offset north-east so it does not sit on the
		// engraved town name. Visibility is zoom/selection dependent (see
		// RefreshHotspotVisibility). Ink matches the town ring: blue yours, red British.
		UTextRenderComponent* Cap = NewObject<UTextRenderComponent>(this);
		Cap->SetupAttachment(MapMesh);
		Cap->RegisterComponent();
		Cap->SetAbsolute(true, true, true);
		const FVector2D CapUV = Kv.Value + GCaptionOffsetUV;
		Cap->SetWorldLocation(UVToWorld(CapUV, 5.5f));
		Cap->SetWorldRotation(APA250ProvinceMapActor::FlatMapTextRotation());
		Cap->SetHorizontalAlignment(EHTA_Left);
		Cap->SetVerticalAlignment(EVRTA_TextCenter);
		Cap->SetWorldSize(GCaptionSizeUU);
		Cap->SetTextRenderColor(TownOwnerInk(Owner));
		FString Name = Kv.Key;
		if (St)
		{
			auto It = St->provinces.find(TCHAR_TO_UTF8(*Kv.Key));
			if (It != St->provinces.end())
			{
				Name = ToFStr(It->second.name);
			}
		}
		Cap->SetText(FText::FromString(Name));
		HotspotCaptions.Add(Cap);
		CaptionById.Add(Kv.Key, Cap);
	}
	RefreshHotspotVisibility();
}

float APA250ProvinceMapActor::TownWashRadiusPx(const FString& Id, const TMap<FString, FVector2D>& HotspotUV,
												 const TMap<FString, float>& HitRadiusPx, const TMap<FString, float>& OverridePx,
												 float ImageW, float ImageH)
{
	const float* Hit = HitRadiusPx.Find(Id);
	float R = (Hit ? *Hit : 90.f) * TownWashRadiusScale;
	if (const float* O = OverridePx.Find(Id))
	{
		R = *O;
	}
	// Never wash into a neighbouring town: cap at a share of the distance to the nearest other hotspot.
	const FVector2D* Me = HotspotUV.Find(Id);
	if (Me)
	{
		for (const TPair<FString, FVector2D>& Kv : HotspotUV)
		{
			if (Kv.Key == Id)
			{
				continue;
			}
			const float D = FVector2D::Distance(FVector2D(Me->X * ImageW, Me->Y * ImageH), FVector2D(Kv.Value.X * ImageW, Kv.Value.Y * ImageH));
			R = FMath::Min(R, D * TownWashNeighbourCap);
		}
	}
	return R;
}

void APA250ProvinceMapActor::ApplyTownWash(UStaticMeshComponent* Wash, const std::string& Owner) const
{
	if (!Wash)
	{
		return;
	}
	// Unowned towns stay bare paper. Tints are linear values of muted ink colours:
	// Continental blue sRGB (40,70,150), British red sRGB (170,40,30).
	const bool bBrit = Owner == "british";
	const bool bCont = Owner == "continental";
	Wash->SetVisibility(bBrit || bCont);
	if (UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(Wash->GetMaterial(0)))
	{
		MID->SetVectorParameterValue(TEXT("Tint"), bBrit ? FLinearColor(0.402f, 0.021f, 0.013f) : FLinearColor(0.021f, 0.061f, 0.305f));
	}
}

void APA250ProvinceMapActor::RefreshTownOwnership()
{
	if (!bDeCostaMode)
	{
		return;
	}
	UPA250Subsystem* S = Sub();
	const pa250::CampaignState* St = (S && S->GetController().has_campaign()) ? &S->GetState() : nullptr;
	for (UStaticMeshComponent* Ring : HotspotMarkers)
	{
		if (!Ring || !Ring->ComponentHasTag(TEXT("town_ring")))
		{
			continue;
		}
		FString ProvinceId;
		for (const FName& Tag : Ring->ComponentTags)
		{
			if (Tag != FName(TEXT("town_ring")))
			{
				ProvinceId = Tag.ToString();
				break;
			}
		}
		std::string Owner;
		if (St && !ProvinceId.IsEmpty())
		{
			auto It = St->provinces.find(TCHAR_TO_UTF8(*ProvinceId));
			if (It != St->provinces.end())
			{
				Owner = It->second.owner;
			}
		}
		ApplyShapeColor(Ring, TownOwnerColor(Owner));
		if (UStaticMeshComponent** Wash = WashById.Find(ProvinceId))
		{
			ApplyTownWash(*Wash, Owner);
		}
		if (UTextRenderComponent** Cap = CaptionById.Find(ProvinceId))
		{
			if (*Cap)
			{
				(*Cap)->SetTextRenderColor(TownOwnerInk(Owner));
			}
		}
	}
}

void APA250ProvinceMapActor::RefreshHotspotVisibility()
{
	static const TCHAR* MajorIds[] = {TEXT("boston"), TEXT("lexington"), TEXT("concord"), TEXT("cambridge"),
									  TEXT("charlestown")};
	const bool bDetail = ZoomFactor >= 3.2f;
	for (const TPair<FString, UTextRenderComponent*>& Kv : CaptionById)
	{
		if (!Kv.Value)
		{
			continue;
		}
		bool bMajor = false;
		for (const TCHAR* Id : MajorIds)
		{
			if (Kv.Key.Equals(Id))
			{
				bMajor = true;
				break;
			}
		}
		const bool bShow = bMajor || bDetail || Kv.Key == SelectedProvinceId;
		Kv.Value->SetVisibility(bShow);
		Kv.Value->SetWorldSize(Kv.Key == SelectedProvinceId ? GCaptionSelectedSizeUU : GCaptionSizeUU);
	}
	if (SelectionHalo)
	{
		const FVector2D* SelUV = LabelUV.Find(SelectedProvinceId);
		SelectionHalo->SetVisibility(SelUV != nullptr);
		if (SelUV)
		{
			SelectionHalo->SetWorldLocation(UVToWorld(*SelUV, 4.5f));
		}
	}
}

void APA250ProvinceMapActor::ApplyCameraPostProcess()
{
	if (!MapCamera)
	{
		return;
	}
	UCameraComponent* CC = MapCamera->GetCameraComponent();
	if (!CC)
	{
		return;
	}
	// White paper + fine black lines wash out under auto-exposure / filmic tone curve.
	FPostProcessSettings& PPS = CC->PostProcessSettings;
	CC->PostProcessBlendWeight = 1.f;
	PPS.bOverride_AutoExposureMethod = true;
	PPS.AutoExposureMethod = AEM_Manual;
	PPS.bOverride_AutoExposureBias = true;
	PPS.AutoExposureBias = 0.f;
	PPS.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	PPS.AutoExposureApplyPhysicalCameraExposure = false;
	PPS.bOverride_ToneCurveAmount = true;
	PPS.ToneCurveAmount = 0.f;
	PPS.bOverride_BloomIntensity = true;
	PPS.BloomIntensity = 0.f;
}

void APA250ProvinceMapActor::FrameCamera()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	APlayerController* PC = World->GetFirstPlayerController();
	if (!PC)
	{
		return;
	}
	PC->bAutoManageActiveCameraTarget = false;

	float ViewW = 1920.f;
	float ViewH = 1080.f;
	int32 VX = 0;
	int32 VY = 0;
	PC->GetViewportSize(VX, VY);
	if (VX > 0 && VY > 0)
	{
		ViewW = static_cast<float>(VX);
		ViewH = static_cast<float>(VY);
	}
	const float OrthoW = ZoomedOrthoWidth(ViewW, ViewH, MapWidthUU, MapHeightUU, ZoomFactor);
	const float OrthoH = OrthoW * ViewH / ViewW;
	// Shift the map into the usable band: its centre sits (Bottom-Top)/2 px above screen centre.
	const float CenterBiasY = (UPA250MapWidget::BottomStripHeightPx - UPA250MapWidget::TopStripHeightPx) * 0.5f / ViewH * OrthoH;
	// Centre on the zoom target, clamped so the view never leaves the plane.
	const float MaxOffX = FMath::Max(0.f, (MapWidthUU - OrthoW) * 0.5f);
	const float MaxOffY = FMath::Max(0.f, (MapHeightUU - OrthoH) * 0.5f);
	const FVector CamLoc(FMath::Clamp((ZoomCenterUV.X - 0.5f) * MapWidthUU, -MaxOffX, MaxOffX),
						 FMath::Clamp((ZoomCenterUV.Y - 0.5f) * MapHeightUU + CenterBiasY, -MaxOffY, MaxOffY),
						 FMath::Max(MapWidthUU, MapHeightUU) * 1.25f);
	// Keep the stored centre equal to what the camera shows. Otherwise a pan past the clamp kept
	// accumulating unseen, and dragging back did nothing until it had been unwound (a dead zone).
	ZoomCenterUV = FVector2D(CamLoc.X / MapWidthUU + 0.5f, (CamLoc.Y - CenterBiasY) / MapHeightUU + 0.5f);
	if (!MapCamera)
	{
		FActorSpawnParameters Params;
		Params.Name = TEXT("PA250MapCamera");
		Params.Owner = this;
		MapCamera = World->SpawnActor<ACameraActor>(ACameraActor::StaticClass(), CamLoc, FRotator(-90.f, -90.f, 0.f), Params);
	}
	if (!MapCamera)
	{
		UE_LOG(LogTemp, Error, TEXT("PA250: failed to spawn map camera"));
		return;
	}
	MapCamera->SetActorLocation(CamLoc);
	MapCamera->SetActorRotation(FRotator(-90.f, -90.f, 0.f));
	if (UCameraComponent* CC = MapCamera->GetCameraComponent())
	{
		CC->SetProjectionMode(ECameraProjectionMode::Orthographic);
		CC->SetOrthoWidth(OrthoW);
		CC->SetConstraintAspectRatio(false);
		CC->SetOrthoNearClipPlane(1.f);
		CC->SetOrthoFarClipPlane(100000.f);
		CC->bConstrainAspectRatio = false;
	}
	if (bDeCostaMode)
	{
		ApplyCameraPostProcess();
	}
	PC->SetViewTarget(MapCamera);
	UE_LOG(LogTemp, Display, TEXT("PA250 map camera at %s ortho=%.0f viewTarget=%s zoom=%.2f"), *CamLoc.ToCompactString(), OrthoW,
		*GetNameSafe(PC->GetViewTarget()), ZoomFactor);
}

void APA250ProvinceMapActor::OnHudSelectionChanged(const FString& ArmyId, const FString& ProvinceId)
{
	SelectedArmyId = ArmyId;
	SelectedProvinceId = ProvinceId;
	LastTokenSig.Reset(); // force token refresh for selection highlight
	RefreshHotspotVisibility();
	RebuildStateTexture();
}

void APA250ProvinceMapActor::HandleProvinceClick(const FString& ProvinceId)
{
	if (APA250CampaignGameMode* GM = GetWorld() ? Cast<APA250CampaignGameMode>(GetWorld()->GetAuthGameMode()) : nullptr)
	{
		if (GM->MapWidget)
		{
			GM->MapWidget->ClickProvince(ProvinceId);
			return;
		}
	}
	for (TObjectIterator<UPA250MapWidget> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->ClickProvince(ProvinceId);
			return;
		}
	}
}

void APA250ProvinceMapActor::HandleRightClick()
{
	for (TObjectIterator<UPA250MapWidget> It; It; ++It)
	{
		if (It->GetWorld() == GetWorld())
		{
			It->ClearSelection();
			return;
		}
	}
}

FVector APA250ProvinceMapActor::GetLabelWorld(const FString& ProvinceId) const
{
	if (const FVector2D* UV = LabelUV.Find(ProvinceId))
	{
		return UVToWorld(*UV, 4.f);
	}
	return GetActorLocation();
}

FVector APA250ProvinceMapActor::GetTokenWorld(const FString& ProvinceId) const
{
	if (const FVector2D* UV = TokenUV.Find(ProvinceId))
	{
		return UVToWorld(*UV, 8.f);
	}
	return GetActorLocation();
}

FBox APA250ProvinceMapActor::GetCartoucheWorldBox() const
{
	const FVector C = CartoucheText ? CartoucheText->GetComponentLocation() : UVToWorld(FVector2D(0.08f, 0.08f), 5.f);
	return FBox(C - FVector(120.f, 40.f, 5.f), C + FVector(120.f, 40.f, 5.f));
}

TArray<FString> APA250ProvinceMapActor::GetAllProvinceIds() const
{
	TArray<FString> Out;
	LabelUV.GetKeys(Out);
	return Out;
}
