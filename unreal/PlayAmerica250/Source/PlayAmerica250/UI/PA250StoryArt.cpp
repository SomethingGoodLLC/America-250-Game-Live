#include "PA250StoryArt.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

FString PA250StoryArt::AssetPathFor(const FString& Key)
{
	return FString::Printf(TEXT("/Game/PA250/Stories/T_Story_%s.T_Story_%s"), *Key, *Key);
}

FColor PA250StoryArt::PlaceholderPixel(const FString& Key, float U, float V)
{
	// A wash, not a picture: warm sky over a dark horizon, the tones shifted a little by the key so
	// two placeholders do not look the same. Nothing here imitates a painting.
	const uint32 H = GetTypeHash(Key);
	const float Warm = 0.85f + 0.15f * ((H & 0xFF) / 255.f);
	const float Horizon = 0.56f + 0.10f * (((H >> 8) & 0xFF) / 255.f);
	const float Glow = FMath::Exp(-FMath::Square((U - (0.3f + 0.4f * (((H >> 16) & 0xFF) / 255.f))) * 2.2f)) * FMath::Exp(-FMath::Square((V - Horizon) * 5.f));
	FLinearColor C;
	if (V < Horizon)
	{
		const float T = V / Horizon;
		C = FMath::Lerp(FLinearColor(0.18f, 0.17f, 0.20f), FLinearColor(0.62f * Warm, 0.50f * Warm, 0.34f), T * T);
	}
	else
	{
		const float T = (V - Horizon) / (1.f - Horizon);
		C = FMath::Lerp(FLinearColor(0.16f, 0.13f, 0.09f), FLinearColor(0.05f, 0.04f, 0.03f), T);
		// Low rolling ground.
		C *= 0.85f + 0.15f * FMath::Sin(U * 23.f + (H & 0x3F)) * FMath::Sin(V * 31.f);
	}
	C += FLinearColor(0.35f, 0.24f, 0.10f) * Glow;
	const float Vignette = 1.f - 0.55f * FMath::Square(FMath::Max(FMath::Abs(U - 0.5f), FMath::Abs(V - 0.5f)) * 2.f);
	C *= Vignette;
	return FLinearColor(FMath::Clamp(C.R, 0.f, 1.f), FMath::Clamp(C.G, 0.f, 1.f), FMath::Clamp(C.B, 0.f, 1.f), 1.f).ToFColor(true);
}

namespace
{
// Kept for the life of the process and rooted: the loading screen paints a picture on its own
// thread while the level that asked for it is torn down, so it must not be collected under it.
TMap<FString, UTexture2D*> GStoryTextures;
TSet<FString> GStoryPlaceholders;
}

UTexture2D* PA250StoryArt::TextureFor(UObject* Outer, const FString& Key, bool& bOutPlaceholder)
{
	bOutPlaceholder = GStoryPlaceholders.Contains(Key);
	if (UTexture2D** Cached = GStoryTextures.Find(Key))
	{
		return *Cached;
	}
	bOutPlaceholder = false;
	if (UTexture2D* Painting = LoadObject<UTexture2D>(nullptr, *AssetPathFor(Key), nullptr, LOAD_NoWarn | LOAD_Quiet))
	{
		Painting->AddToRoot();
		GStoryTextures.Add(Key, Painting);
		return Painting;
	}
	bOutPlaceholder = true;
	GStoryPlaceholders.Add(Key);
	const int32 W = 384, H = 216;
	UTexture2D* Tex = UTexture2D::CreateTransient(W, H, PF_B8G8R8A8);
	if (!Tex)
	{
		return nullptr;
	}
	Tex->SRGB = true;
	Tex->Filter = TF_Bilinear;
	Tex->AddressX = TA_Clamp;
	Tex->AddressY = TA_Clamp;
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	FColor* Px = static_cast<FColor*>(Mip.BulkData.Lock(LOCK_READ_WRITE));
	for (int32 Y = 0; Y < H; ++Y)
	{
		for (int32 X = 0; X < W; ++X)
		{
			Px[Y * W + X] = PlaceholderPixel(Key, (X + 0.5f) / W, (Y + 0.5f) / H);
		}
	}
	Mip.BulkData.Unlock();
	Tex->UpdateResource();
	Tex->AddToRoot();
	GStoryTextures.Add(Key, Tex);
	UE_LOG(LogTemp, Warning, TEXT("PA250 story art: no painting imported for '%s' (%s): a placeholder wash is shown"), *Key, *AssetPathFor(Key));
	return Tex;
}
