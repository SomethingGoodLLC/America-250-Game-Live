// PA250OrderProbe.cpp — -PA250OrderProbe: do the player's men do what the order keys tell them?
// (Playtest 2026-11-15: "it doesn't seem like the commands for men to follow are obeyed".)
// The player's own soldier is walked across the field and the keys 1 (follow), 3 (hold) and 1 again
// are pressed through the real input path (APlayerController::InputKey), so a key that never reaches
// the soldier shows up as an order that did not change. The enemy is held still and silent so only
// obedience is measured (-PA250OrderProbeLive leaves the fight on). Results: log lines "PA250 order probe:" and Saved/PA250/order_probe.json.
// Run in a window: -game -windowed -PA250OrderProbe (no autoplay: it needs the player's soldier).
#include "PA250BattleGameMode.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "InputKeyEventArgs.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PA250BotController.h"
#include "PA250VolleyCoordinator.h"
#include "RFCharacter.h"

void APA250BattleGameMode::TickOrderProbe(float DeltaSeconds)
{
	static const bool bProbe = FParse::Param(FCommandLine::Get(), TEXT("PA250OrderProbe"));
	if (!bProbe || bBattleOver || !Volleys || OrderProbeStep >= 100) return;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ARFCharacter* Me = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	if (!Me || Me->IsDead()) return;
	OrderProbeTime += DeltaSeconds;

	auto Press = [PC](const FKey& Key) {
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Pressed, 1.f));
		PC->InputKey(FInputKeyEventArgs::CreateSimulated(Key, IE_Released, 0.f));
	};
	auto Status = [this, Me](int32& Men, int32& With) {
		int32 Loaded = 0, Presented = 0;
		EPA250SquadOrder Order = EPA250SquadOrder::None;
		Volleys->GetFileStatus(Me, Men, Loaded, Presented, Order, &With);
		return Order;
	};
	auto MeanGap = [this, Me]() {
		const TArray<ARFCharacter*> File = Volleys->GetFileMembers(Me);
		float Sum = 0.f;
		for (const ARFCharacter* C : File) Sum += FVector::Dist2D(C->GetActorLocation(), Me->GetActorLocation());
		return File.Num() > 0 ? Sum / File.Num() : -1.f;
	};
	auto Mark = [this, Me]() {
		OrderProbeMarks.Reset();
		for (const ARFCharacter* C : Volleys->GetFileMembers(Me)) OrderProbeMarks.Add(C->GetActorLocation());
	};
	auto MovedSinceMark = [this, Me](float MoreThanCm) {
		const TArray<ARFCharacter*> File = Volleys->GetFileMembers(Me);
		int32 N = 0;
		for (int32 i = 0; i < File.Num() && i < OrderProbeMarks.Num(); ++i) N += FVector::Dist2D(File[i]->GetActorLocation(), OrderProbeMarks[i]) > MoreThanCm ? 1 : 0;
		return N;
	};
	auto Walk = [Me]() { Me->AddMovementInput(Me->GetActorForwardVector(), 1.f); };
	auto Note = [this](const FString& Key, const FString& Value) {
		OrderProbeJson += FString::Printf(TEXT("%s\"%s\":%s"), OrderProbeJson.IsEmpty() ? TEXT("") : TEXT(","), *Key, *Value);
	};

	int32 Men = 0, With = 0;
	switch (OrderProbeStep)
	{
	case -1:
		// Once his file exists: the enemy stands still and silent, he cannot be hurt, and he faces open ground.
		if (OrderProbeTime < 3.f || Volleys->GetFileMembers(Me).Num() == 0) return;
		// -PA250OrderProbeLive: the enemy fights and men fall as in any battle (only he cannot be hurt),
		// to see whether a file under fire still obeys.
		if (FParse::Param(FCommandLine::Get(), TEXT("PA250OrderProbeLive")))
		{
			Me->SetCanBeDamaged(false);
			Note(TEXT("live"), TEXT("true"));
		}
		else
		{
			for (TActorIterator<ARFCharacter> It(GetWorld()); It; ++It)
			{
				if (It->FactionId != Me->FactionId)
				{
					if (APA250BotController* Bot = Cast<APA250BotController>(It->GetController())) Bot->bScriptedHold = true;
				}
				It->SetCanBeDamaged(false);
			}
		}
		OrderProbeLeaderStart = Me->GetActorLocation();
		Status(Men, With);
		UE_LOG(LogTemp, Display, TEXT("PA250 order probe: start, %d men in the file, %d within reach, mean %.1f m from him"), Men, With, MeanGap() / 100.f);
		Note(TEXT("file_men"), FString::FromInt(Men));
		Press(EKeys::Three); // hold first, so "follow" below is a change the key must make
		OrderProbeStep = 0;
		OrderProbeTime = 0.f;
		return;
	case 0:
	{
		if (OrderProbeTime < 1.f) return;
		const bool bHoldTook = Status(Men, With) == EPA250SquadOrder::Hold;
		UE_LOG(LogTemp, Display, TEXT("PA250 order probe: key 3 pressed -> order is %s (%s)"), PA250Squad::OrderWords(Status(Men, With)), bHoldTook ? TEXT("the key reached the game") : TEXT("THE KEY DID NOT REACH THE GAME"));
		Note(TEXT("key_3_sets_hold"), bHoldTook ? TEXT("true") : TEXT("false"));
		Press(EKeys::One);
		OrderProbeStep = 1;
		OrderProbeTime = 0.f;
		return;
	}
	case 1:
	{
		// Follow me: he walks for 12 s; the file should be with him at the end.
		if (OrderProbeTime < 12.f)
		{
			Walk();
			return;
		}
		const EPA250SquadOrder Order = Status(Men, With);
		const float Walked = FVector::Dist2D(Me->GetActorLocation(), OrderProbeLeaderStart) / 100.f;
		UE_LOG(LogTemp, Display, TEXT("PA250 order probe: FOLLOW (key 1 -> %s): he walked %.0f m; %d of %d men within reach of him, mean %.1f m"),
			PA250Squad::OrderWords(Order), Walked, With, Men, MeanGap() / 100.f);
		Note(TEXT("key_1_sets_follow"), Order == EPA250SquadOrder::FollowMe ? TEXT("true") : TEXT("false"));
		Note(TEXT("follow"), FString::Printf(TEXT("{\"leader_walked_m\":%.1f,\"with_him\":%d,\"men\":%d,\"mean_gap_m\":%.1f}"), Walked, With, Men, MeanGap() / 100.f));
		Press(EKeys::Three);
		Mark();
		OrderProbeLeaderStart = Me->GetActorLocation();
		OrderProbeStep = 2;
		OrderProbeTime = 0.f;
		return;
	}
	case 2:
	{
		// Hold: he walks on for 10 s; the file should stay where it stood.
		if (OrderProbeTime < 10.f)
		{
			Walk();
			return;
		}
		const EPA250SquadOrder Order = Status(Men, With);
		const int32 Strayed = MovedSinceMark(400.f);
		const float Walked = FVector::Dist2D(Me->GetActorLocation(), OrderProbeLeaderStart) / 100.f;
		UE_LOG(LogTemp, Display, TEXT("PA250 order probe: HOLD (key 3 -> %s): he walked on %.0f m; %d of %d men left their ground by more than 4 m; mean %.1f m from him now"),
			PA250Squad::OrderWords(Order), Walked, Strayed, Men, MeanGap() / 100.f);
		Note(TEXT("hold"), FString::Printf(TEXT("{\"leader_walked_m\":%.1f,\"strayed\":%d,\"men\":%d,\"mean_gap_m\":%.1f}"), Walked, Strayed, Men, MeanGap() / 100.f));
		Press(EKeys::One);
		OrderProbeStep = 3;
		OrderProbeTime = 0.f;
		return;
	}
	case 3:
	{
		// Follow me again, standing still: the file should come up to him.
		if (OrderProbeTime < 14.f) return;
		const EPA250SquadOrder Order = Status(Men, With);
		UE_LOG(LogTemp, Display, TEXT("PA250 order probe: FOLLOW AGAIN (key 1 -> %s), he stands: %d of %d men have come up to him, mean %.1f m"),
			PA250Squad::OrderWords(Order), With, Men, MeanGap() / 100.f);
		Note(TEXT("come_up"), FString::Printf(TEXT("{\"with_him\":%d,\"men\":%d,\"mean_gap_m\":%.1f}"), With, Men, MeanGap() / 100.f));
		const FString Path = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("order_probe.json"));
		IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path), true);
		FFileHelper::SaveStringToFile(TEXT("{") + OrderProbeJson + TEXT("}\n"), *Path);
		UE_LOG(LogTemp, Display, TEXT("PA250 order probe: done -> %s"), *Path);
		OrderProbeStep = 100;
		FPlatformMisc::RequestExit(false);
		return;
	}
	default:
		return;
	}
}
