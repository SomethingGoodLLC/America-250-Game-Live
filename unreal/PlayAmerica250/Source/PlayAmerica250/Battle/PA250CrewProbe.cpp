// -PA250CrewProbe: the gun crew's tools seen up close and measured, on any battle level (the
// campaign's included). See APA250BattleGameMode::MaybeStartCrewProbe.
#include "PA250BattleGameMode.h"
#include "PA250FieldGun.h"
#include "PA250AnimClips.h"
#include "RFCharacter.h"
#include "Camera/CameraActor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/PlatformMisc.h"
#include "Misc/CommandLine.h"
#include "Misc/Paths.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
struct FCrewProbe
{
	TWeakObjectPtr<APA250FieldGun> Gun;
	TWeakObjectPtr<ACameraActor> Camera;
	TMap<FName, int32> Shots;     // clip -> stills taken of it
	TSet<FName> Seen;              // clips seen acted at all
	float StartedAt = 0.f;
	int32 Fired = 0;
	int32 Lines = 0;
	bool bDone = false;
	FName CamClip;                 // the clip the camera was set for: it stays put through it
};

FVector GripOf(const USkeletalMeshComponent* Body, const TCHAR* Side)
{
	const FVector Hand = Body->GetBoneLocation(*FString::Printf(TEXT("hand_%s"), Side), EBoneSpaces::WorldSpace);
	const FName Tip(*FString::Printf(TEXT("middle_03_%s"), Side));
	return Body->GetBoneIndex(Tip) != INDEX_NONE ? 0.5f * (Hand + Body->GetBoneLocation(Tip, EBoneSpaces::WorldSpace)) : Hand;
}

/** Where the tool is against the hands and the gun, in words for the log. */
FString Measure(const APA250FieldGun* Gun, const ARFCharacter* Man, const UStaticMeshComponent* Tool, FName Clip)
{
	if (!Tool || !Tool->GetStaticMesh() || !Man->GetMesh()) return TEXT("no tool");
	const FPA250CrewToolRule* Rule = PA250AnimClips::CrewToolRule(Clip);
	const FPA250GunTools& Tools = PA250AnimClips::GunTools();
	const FTransform GS = Gun->GetGunSpace();
	const FVector C = GS.InverseTransformPosition(Tool->GetComponentLocation());
	const FVector X = GS.InverseTransformVectorNoScale(Tool->GetForwardVector()).GetSafeNormal();
	const float Half = Tool->GetStaticMesh()->GetBounds().BoxExtent.X * Tool->GetComponentScale().X;
	const FVector A = C - X * Half, B = C + X * Half;
	const FVector R = GS.InverseTransformPosition(GripOf(Man->GetMesh(), TEXT("r")));
	const FVector L = GS.InverseTransformPosition(GripOf(Man->GetMesh(), TEXT("l")));
	FString Out = FString::Printf(TEXT("%s %.0f cm long, %s: right hand %.1f cm off it, left %.1f"), *Tool->GetStaticMesh()->GetName(), 2.f * Half,
		Tool->IsVisible() ? TEXT("shown") : TEXT("HIDDEN"), FMath::PointDistToSegment(R, A, B), FMath::PointDistToSegment(L, A, B));
	if (!Rule) return Out;
	const FVector M = Tools.BoreOriginCm, D = Tools.BoreDirection;
	auto OnBore = [&](const FVector& P) { return M + D * FVector::DotProduct(P - M, D); };
	switch (Rule->Hold)
	{
	case EPA250ToolHold::Bore:
	{
		// The working end: the sponge head is the mesh's +X end, the rammer head its -X end.
		const FVector E = Rule->bSpongeHead ? B : A;
		const float Inside = FVector::DotProduct(M - E, D);
		Out += FString::Printf(TEXT("; %s head %.1f cm off the bore line, %.0f cm %s the muzzle face; staff %.0f deg off the bore"),
			Rule->bSpongeHead ? TEXT("sponge") : TEXT("rammer"), (E - OnBore(E)).Size(), FMath::Abs(Inside), Inside >= 0.f ? TEXT("inside") : TEXT("outside"),
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FMath::Abs(FVector::DotProduct(X, D)), 0.f, 1.f))));
		break;
	}
	case EPA250ToolHold::ToVent:
		Out += FString::Printf(TEXT("; match end %.1f cm from the vent"), (B - Tools.VentCm).Size());
		break;
	case EPA250ToolHold::InHand:
		Out += FString::Printf(TEXT("; round %.0f cm from the muzzle"), (C - M).Size());
		break;
	case EPA250ToolHold::Upright:
	case EPA250ToolHold::Planted:
	{
		const float Feet = GS.InverseTransformPosition(Man->GetActorLocation() - FVector(0.f, 0.f, Man->GetSimpleCollisionHalfHeight())).Z;
		Out += FString::Printf(TEXT("; foot %.0f cm above his feet, %.0f deg from upright"), A.Z - Feet,
			FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(X.Z, -1.f, 1.f))));
		break;
	}
	}
	return Out;
}
}

void APA250BattleGameMode::MaybeStartCrewProbe()
{
	if (!FParse::Param(FCommandLine::Get(), TEXT("PA250CrewProbe")) || !GetWorld()) return;
	UE_LOG(LogTemp, Display, TEXT("PA250 crew probe: armed"));
	TSharedRef<FCrewProbe> P = MakeShared<FCrewProbe>();
	// After the story's fade (a campaign battle) and the crew's walk to the gun.
	FTimerHandle Start;
	GetWorldTimerManager().SetTimer(Start, FTimerDelegate::CreateWeakLambda(this, [this, P]() {
		APlayerController* PC = GetWorld()->GetFirstPlayerController();
		ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
		auto Quit = [](const TCHAR* Why) { UE_LOG(LogTemp, Error, TEXT("PA250 crew probe: FAIL %s"), Why); FPlatformMisc::RequestExit(false); };
		if (!Me) { Quit(TEXT("no player pawn")); return; }
		const FName Side = GetPlayerFaction().IsNone() ? Me->FactionId : GetPlayerFaction();
		int32 OwnGuns = 0;
		for (APA250FieldGun* G : Guns) OwnGuns += G && G->GetFaction() == Side ? 1 : 0;
		UE_LOG(LogTemp, Display, TEXT("PA250 crew probe: %s, %s side has %d gun(s) on the field (%s)"), *GetWorld()->GetMapName(), *Side.ToString(), OwnGuns,
			bDebugBattle ? TEXT("a level opened directly") : TEXT("a campaign battle"));
		if (OwnGuns == 0) { Quit(TEXT("the player's side has no gun on this field")); return; }
		// As the class picker does: he takes the field as an artilleryman and is put at the gun.
		ConfigureSoldier(Me, Side, TEXT("artilleryman"));
		if (!PlacePlayerAtGun(Me)) { Quit(TEXT("could not place the player at a gun")); return; }
		for (APA250FieldGun* G : Guns) if (G && G->GetPlayerGunner() == Me) P->Gun = G;
		if (!P->Gun.IsValid()) { Quit(TEXT("no gun has the player")); return; }
		// Every post manned, from the side's own reserve (the render probe's way), so each step is seen.
		const int32 CrewBefore = P->Gun->GetCrew();
		int32 Called = 0;
		while (P->Gun->GetCrew() < Rules.cannon_crew_men)
		{
			const int32 Slot = P->Gun->FreePostIndex();
			ARFCharacter* Mate = Slot == INDEX_NONE ? nullptr : SummonGunMate(P->Gun.Get(), Slot);
			if (!Mate) break;
			P->Gun->ClaimPost(Mate);
			P->Gun->RecountCrew();
			++Called;
		}
		UE_LOG(LogTemp, Display, TEXT("PA250 crew probe: player at post %d; crew %d, %d more called from the reserve, now %d"),
			P->Gun->PostOf(Me), CrewBefore, Called, P->Gun->GetCrew());
		PC->ConsoleCommand(TEXT("slomo 0.3"));
		PC->ConsoleCommand(TEXT("r.MotionBlurQuality 0")); // the stills are for reading the hands, not for feel
		P->Camera = GetWorld()->SpawnActor<ACameraActor>(Me->GetActorLocation() + FVector(0.f, 0.f, 300.f), FRotator::ZeroRotator);
		if (P->Camera.IsValid()) PC->SetViewTarget(P->Camera.Get());
		P->StartedAt = GetWorld()->GetTimeSeconds();
		FTimerHandle Tick;
		GetWorldTimerManager().SetTimer(Tick, FTimerDelegate::CreateWeakLambda(this, [this, P, Me]() {
			APA250FieldGun* Gun = P->Gun.Get();
			if (P->bDone || !Gun) return;
			static const FName Wanted[] = {TEXT("gun_crew_sponge"), TEXT("gun_crew_vent"), TEXT("gun_crew_load"), TEXT("gun_crew_ram"),
				TEXT("gun_crew_prime"), TEXT("gun_crew_fire"), TEXT("gun_crew_idle_sponger"), TEXT("gun_crew_idle_ventsman")};
			constexpr int32 PerClip = 6;
			const float Elapsed = GetWorld()->GetTimeSeconds() - P->StartedAt;
			bool bAll = true;
			for (const FName& W : Wanted) bAll &= P->Shots.FindRef(W) >= (W.ToString().Contains(TEXT("idle")) ? 2 : PerClip);
			if (bAll || Elapsed > 150.f)
			{
				P->bDone = true;
				FString Missing;
				for (const FName& W : Wanted) if (P->Shots.FindRef(W) == 0) Missing += TEXT(" ") + W.ToString();
				UE_LOG(LogTemp, Display, TEXT("PA250 crew probe: done after %.0f game-s, %d shots fired, %d measured stills%s%s"), Elapsed, P->Fired, P->Lines,
					Missing.IsEmpty() ? TEXT("") : TEXT("; never seen:"), *Missing);
				FPlatformMisc::RequestExit(false);
				return;
			}
			const bool bDrilling = !Gun->IsLoaded() && Gun->GetDrill().IsReloading();
			const int32 Step = Gun->GetDrill().GetStepIndex();
			const float Progress = bDrilling ? Gun->GetDrill().GetPhaseProgress() : FMath::Fmod(Elapsed, 4.f) / 4.f;
			// The first man at work whose clip still wants stills; at rest, the sponger then the ventsman.
			for (int32 Post = 0; Post < 4; ++Post)
			{
				ARFCharacter* Man = Gun->GetPostMan(Post);
				if (!Man || Man == Me || Man->IsDead()) continue;
				const FName Clip = APA250FieldGun::CrewStepClip(Post, Step, bDrilling);
				const bool bIdle = Clip.ToString().Contains(TEXT("idle"));
				if (bDrilling == bIdle) continue;
				const int32 Want = bIdle ? 2 : PerClip;
				int32& Taken = P->Shots.FindOrAdd(Clip);
				if (Taken >= Want) continue;
				const float Next = (Taken + 0.5f) / Want;
				if (Progress > Next + 0.5f / Want) continue; // his moment has gone by: the next cycle
				ACameraActor* Cam = P->Camera.Get();
				if (Cam && P->CamClip != Clip)
				{
					// A new man: the camera goes to him and the still waits a beat, so nothing is smeared by the cut.
					P->CamClip = Clip;
					// Close and from his outer side, a little above, looking at his hands and the gun beside him.
					const FVector At = Man->GetActorLocation();
					FVector Out = (At - Gun->GetActorLocation()).GetSafeNormal2D();
					if (Out.IsNearlyZero()) Out = Gun->GetActorRightVector();
					const FVector Eye = At + Out * 250.f + Gun->GetActorForwardVector() * 60.f + FVector(0.f, 0.f, 70.f);
					const FVector Look = FMath::Lerp(At + FVector(0.f, 0.f, 20.f), Gun->GetMuzzle(), Clip == TEXT("gun_crew_sponge") || Clip == TEXT("gun_crew_ram") ? 0.35f : 0.15f);
					Cam->SetActorLocationAndRotation(Eye, (Look - Eye).Rotation());
					break;
				}
				if (Progress < Next) break; // this man is the one followed; wait for his next frame
				++Taken;
				++P->Lines;
				const FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
					FString::Printf(TEXT("crew_%s_%d.png"), *Clip.ToString().RightChop(9), Taken)));
				FScreenshotRequest::RequestScreenshot(Path, false, false);
				FVector Face, Station;
				const float Turned = Gun->GetCrewFacePoint(Post, Face) && Gun->GetCrewStation(Post, Station)
					? FMath::FindDeltaAngleDegrees((Face - Station).Rotation().Yaw, Man->GetActorRotation().Yaw) : 0.f;
				UE_LOG(LogTemp, Display, TEXT("PA250 crew probe: %s %d/%d post %d at %.2f, turned %.0f deg from his clip's facing, %.0f cm from its station: %s"),
					*Clip.ToString(), Taken, Want, Post, Progress, Turned, Gun->GetCrewStation(Post, Station) ? FVector::Dist2D(Man->GetActorLocation(), Station) : -1.f,
					*Measure(Gun, Man, Gun->GetCrewTool(Post), Clip));
				break;
			}
			// Keep it working: once loaded and the men at rest have been seen, fire.
			if (Gun->IsLoaded() && P->Shots.FindRef(TEXT("gun_crew_idle_sponger")) >= 2 && P->Shots.FindRef(TEXT("gun_crew_idle_ventsman")) >= 2)
			{
				if (Gun->PlayerFire(Gun->GetMuzzle() + Gun->GetActorForwardVector() * 15000.f)) ++P->Fired;
			}
		}), 0.03f, true);
	}), 12.f, false);
}
