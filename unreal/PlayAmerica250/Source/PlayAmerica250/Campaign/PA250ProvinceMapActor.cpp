#include "PA250ProvinceMapActor.h"
#include "PA250ArmyToken.h"
#include "PA250CampaignGameMode.h"
#include "PA250MapWidget.h"
#include "PA250Subsystem.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
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
#include "TextureResource.h"
#include "UObject/ConstructorHelpers.h"
#include "UObject/UObjectIterator.h"

namespace
{
FString ToFStr(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }

FString FormatUnitsPlate(const pa250::Army& A)
{
	TArray<FString> Parts;
	for (const auto& U : A.units)
	{
		if (U.second <= 0)
		{
			continue;
		}
		Parts.Add(FString::Printf(TEXT("%s %d"), UTF8_TO_TCHAR(U.first.c_str()), U.second));
	}
	return Parts.Num() > 0 ? FString::Join(Parts, TEXT(" · ")) : TEXT("0");
}

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
	return Name;
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
} // namespace

FRotator APA250ProvinceMapActor::FlatMapTextRotation()
{
	// TextRender advance is -Y and glyph-up is +Z. Pitch 90 lays it flat facing +Z;
	// yaw 90 then turns advance to +X (east, screen right) and glyph-up to -Y
	// (north, screen up) for the top-down camera, so labels read horizontally.
	return FRotator(90.f, 90.f, 0.f);
}

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
	MapMesh->SetRelativeScale3D(FVector(MapWidthUU / 100.f, MapHeightUU / 100.f, 1.f));
	MapMesh->SetCastShadow(false);

	LoadTextures();
	LoadPresentation();
	EnsureMaterial();
	RebuildStateTexture();
	SpawnLabels();
	SpawnWaterLabels();
	SpawnFramingDecor();
	RefreshArmyTokens();
	RefreshBattleMarker();
	FrameCamera();
}

void APA250ProvinceMapActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RebuildStateTexture();
	RefreshArmyTokens();
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

void APA250ProvinceMapActor::LoadTextures()
{
	ProvinceIdTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_ProvinceID.T_ProvinceID"));
	BorderDistTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_BorderDist.T_BorderDist"));
	CoastDistTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_CoastDist.T_CoastDist"));
	PaperTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_Parchment.T_Parchment"));
	DisplayTex = LoadObject<UTexture2D>(nullptr, TEXT("/Game/Map/Boston1775/T_MapPreview.T_MapPreview"));
	if (!DisplayTex)
	{
		// Label-free display texture first: TextRender draws the names in-engine.
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
	UE_LOG(LogTemp, Display, TEXT("PA250 map assets: mat=%s display=%s pid=%s"),
		BaseMaterial ? *BaseMaterial->GetName() : TEXT("NONE"), DisplayTex ? *DisplayTex->GetName() : TEXT("NONE"),
		ProvinceIdTex ? *ProvinceIdTex->GetName() : TEXT("NONE"));

	const FString BinPath = FPaths::ProjectContentDir() / TEXT("Map/Boston1775/Raw/ProvinceIdLookup.bin");
	if (FFileHelper::LoadFileToArray(LookupBytes, *BinPath) && LookupBytes.Num() >= 8)
	{
		LookupW = static_cast<int32>(*reinterpret_cast<const uint32*>(LookupBytes.GetData()));
		LookupH = static_cast<int32>(*reinterpret_cast<const uint32*>(LookupBytes.GetData() + 4));
		TexW = LookupW;
		TexH = LookupH;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("PA250: missing ProvinceIdLookup.bin"));
	}
}

void APA250ProvinceMapActor::LoadPresentation()
{
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
	LabelUV.Reset();
	TokenUV.Reset();
	Edges.Reset();
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
	FString Sig = SelectedArmyId + TEXT("|");
	for (const auto& Kv : St.armies)
	{
		Sig += ToFStr(Kv.first) + TEXT("@") + ToFStr(Kv.second.province) + TEXT(";");
		for (const auto& U : Kv.second.units)
		{
			Sig += FString::Printf(TEXT("%s%d"), UTF8_TO_TCHAR(U.first.c_str()), U.second);
		}
	}
	if (Sig == LastTokenSig && ArmyTokens.Num() == static_cast<int32>(St.armies.size()))
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

	TMap<FString, int32> StackCount;
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	for (const auto& Kv : St.armies)
	{
		const FString Pid = ToFStr(Kv.second.province);
		const int32 Stack = StackCount.FindOrAdd(Pid);
		StackCount[Pid] = Stack + 1;
		const FVector2D* UV = TokenUV.Find(Pid);
		if (!UV)
		{
			continue;
		}
		FActorSpawnParameters Params;
		Params.Owner = this;
		const FVector Loc = UVToWorld(*UV, 8.f) + FVector(Stack * 28.f, Stack * 20.f, Stack * 2.f);
		APA250ArmyToken* Tok =
			World->SpawnActor<APA250ArmyToken>(APA250ArmyToken::StaticClass(), Loc, APA250ProvinceMapActor::FlatMapTextRotation(), Params);
		if (!Tok)
		{
			continue;
		}
		Tok->Configure(ToFStr(Kv.first), ToFStr(Kv.second.faction), ShortArmyName(Kv.second), FormatUnitsPlate(Kv.second), Stack,
				   ToFStr(Kv.first) == SelectedArmyId);
		ArmyTokens.Add(Tok);
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

void APA250ProvinceMapActor::ZoomBy(float Factor)
{
	ZoomFactor = FMath::Clamp(ZoomFactor * Factor, 1.f, 5.f);
	if (ZoomFactor <= 1.01f)
	{
		ZoomCenterUV = FVector2D(0.5f, 0.5f);
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
	PC->SetViewTarget(MapCamera);
	UE_LOG(LogTemp, Display, TEXT("PA250 map camera at %s ortho=%.0f viewTarget=%s"), *CamLoc.ToCompactString(), OrthoW,
		*GetNameSafe(PC->GetViewTarget()));
}

void APA250ProvinceMapActor::OnHudSelectionChanged(const FString& ArmyId, const FString& ProvinceId)
{
	SelectedArmyId = ArmyId;
	SelectedProvinceId = ProvinceId;
	LastTokenSig.Reset(); // force token refresh for selection highlight
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
