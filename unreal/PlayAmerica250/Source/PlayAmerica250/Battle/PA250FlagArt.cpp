#include "PA250FlagArt.h"
#include "Engine/Texture2D.h"
#include "TextureResource.h"

namespace PA250FlagArt
{
namespace
{
const FLinearColor White(0.92f, 0.92f, 0.88f);

FLinearColor UnionFlag(float U, float V)
{
	// Flag units: 2 wide, 1 high, Y up.
	const float X = U * 2.f;
	const float Y = 1.f - V;
	const FLinearColor Blue(0.01f, 0.05f, 0.25f);
	const FLinearColor Red(0.62f, 0.02f, 0.04f);
	const float InvSqrt5 = 1.f / FMath::Sqrt(5.f);
	const bool bSaltire = FMath::Abs(X - 2.f * Y) * InvSqrt5 < 0.1f || FMath::Abs(X + 2.f * Y - 2.f) * InvSqrt5 < 0.1f;
	const float DX = FMath::Abs(X - 1.f);
	const float DY = FMath::Abs(Y - 0.5f);
	if (DX < 0.1f || DY < 0.1f) return Red;              // St George
	if (DX < 1.f / 6.f || DY < 1.f / 6.f) return White;  // its white border
	return bSaltire ? White : Blue;                      // St Andrew on blue
}

FLinearColor PineTree(float U, float V)
{
	const float X = U * 2.f;
	const float Y = 1.f - V;
	const FLinearColor Green(0.03f, 0.22f, 0.06f);
	const FLinearColor Trunk(0.25f, 0.13f, 0.05f);
	const float DX = FMath::Abs(X - 1.f);
	// Three stacked tiers (base, top, half-width at the base).
	const float Tiers[3][3] = {{0.26f, 0.60f, 0.30f}, {0.44f, 0.77f, 0.24f}, {0.61f, 0.92f, 0.17f}};
	for (const auto& T : Tiers)
	{
		if (Y >= T[0] && Y <= T[1] && DX < T[2] * (T[1] - Y) / (T[1] - T[0])) return Green;
	}
	if (DX < 0.045f && Y >= 0.1f && Y < 0.3f) return Trunk;
	return White;
}
} // namespace

FLinearColor Pixel(FName Faction, float U, float V)
{
	if (Faction == TEXT("british")) return UnionFlag(U, V);
	if (Faction == TEXT("continental")) return PineTree(U, V);
	return White;
}

UTexture2D* GetTexture(FName Faction)
{
	static TMap<FName, TWeakObjectPtr<UTexture2D>> Cache;
	if (TWeakObjectPtr<UTexture2D>* Hit = Cache.Find(Faction); Hit && Hit->IsValid())
	{
		return Hit->Get();
	}
	UTexture2D* Tex = UTexture2D::CreateTransient(TexW, TexH, PF_B8G8R8A8, *FString::Printf(TEXT("T_PA250_Flag_%s"), *Faction.ToString()));
	if (!Tex || !Tex->GetPlatformData() || Tex->GetPlatformData()->Mips.Num() == 0)
	{
		return nullptr;
	}
	TArray<FColor> Pixels;
	Pixels.SetNumUninitialized(TexW * TexH);
	for (int32 y = 0; y < TexH; ++y)
	{
		for (int32 x = 0; x < TexW; ++x)
		{
			FLinearColor Sum(0, 0, 0, 0);
			for (int32 s = 0; s < 4; ++s)
			{
				Sum += Pixel(Faction, (x + 0.25f + 0.5f * (s & 1)) / TexW, (y + 0.25f + 0.5f * (s >> 1)) / TexH);
			}
			Pixels[y * TexW + x] = (Sum * 0.25f).ToFColor(true);
		}
	}
	FTexture2DMipMap& Mip = Tex->GetPlatformData()->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, Pixels.GetData(), Pixels.Num() * sizeof(FColor));
	Mip.BulkData.Unlock();
	Tex->SRGB = true;
	Tex->UpdateResource();
	Cache.Add(Faction, Tex);
	return Tex;
}

FVector ClothOffset(float X01, float Z01, float T, float LengthCm)
{
	const float X = FMath::Clamp(X01, 0.f, 1.f);
	// Ripples travel away from the pole and grow toward the free end; a little droop at the fly.
	const float Amp = 0.09f * LengthCm * FMath::Pow(X, 1.2f);
	const float Phase = 2.f * PI * (X * 1.6f) - 5.5f * T + 0.6f * Z01;
	const float Side = Amp * FMath::Sin(Phase) + 0.25f * Amp * FMath::Sin(2.3f * Phase + 1.f);
	const float Droop = -0.07f * LengthCm * X * X;
	const float Pull = -0.03f * LengthCm * X * (1.f - FMath::Cos(Phase)); // fly shortens as it folds
	return FVector(Pull, Side, Droop);
}
} // namespace PA250FlagArt
