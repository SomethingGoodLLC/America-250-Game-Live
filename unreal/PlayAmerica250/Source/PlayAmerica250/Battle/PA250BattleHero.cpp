// PA250BattleHero.cpp — the playable general in the battle (M5, GAME INVENTION): the part of
// APA250BattleGameMode that scores the player toward him, hands him over, limits him with the meter,
// gives the player a body again, and works his rally and his trait ability. The rules are pure
// (Battle/PA250Hero.h); every number is the persons data's.
// The campaign is touched only through BattleResult: commander_events (as for any general who
// falls) and the hero report.
#include "PA250BattleGameMode.h"

#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Misc/Paths.h"
#include "PA250BotController.h"
#include "PA250FieldWork.h"
#include "PA250HeroScreen.h"
#include "PA250MeleeComponent.h"
#include "MusketWeapon.h"
#include "PA250UnitClasses.h"
#include "PA250VolleyCoordinator.h"
#include "RFCharacter.h"
#include "RFHealthComponent.h"
#include "TimerManager.h"
#include "UnrealClient.h"

namespace
{
FString HeroName(const pa250::SideSetup* Side)
{
	return Side && !Side->commander_name.empty() ? FString(UTF8_TO_TCHAR(Side->commander_name.c_str())) : FString(TEXT("the general"));
}
}

const pa250::HeroRules& APA250BattleGameMode::HeroRules() const
{
	if (!bHeroRulesRead)
	{
		bHeroRulesRead = true;
		HeroRulesCache = pa250::hero_rules();
	}
	return HeroRulesCache;
}

const pa250::SideSetup* APA250BattleGameMode::HeroSide() const
{
	const auto It = Setup.sides.find(TCHAR_TO_UTF8(*PlayerFaction.ToString()));
	return It == Setup.sides.end() ? nullptr : &It->second;
}

ARFCharacter* APA250BattleGameMode::HeroGeneral() const
{
	const TWeakObjectPtr<ARFCharacter>* G = Generals.Find(PlayerFaction);
	return G ? G->Get() : nullptr;
}

bool APA250BattleGameMode::IsPlayedGeneral(const ARFCharacter* Man) const
{
	return Hero.bPlaying && Man && Man->FactionId == PlayerFaction && IsGeneral(Man);
}

bool APA250BattleGameMode::CanOfferHero() const
{
	const pa250::SideSetup* Side = HeroSide();
	const ARFCharacter* General = HeroGeneral();
	return !bBattleOver && PA250Hero::CanOffer(Hero, HeroRules(), Side && Side->has_hero, General && !General->IsDead());
}

void APA250BattleGameMode::NoteHeroEvent(EPA250HeroEvent Event)
{
	const pa250::SideSetup* Side = HeroSide();
	const pa250::HeroRules& HR = HeroRules();
	if (!HR.loaded || !Side || !Side->has_hero) return;
	const TCHAR* What = Event == EPA250HeroEvent::Kill ? TEXT("kill") : Event == EPA250HeroEvent::Capture ? TEXT("post taken") : TEXT("rally");
	PA250Hero::Note(Hero, Event, HR);
	if (Hero.bPlaying)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 hero: %s as the general, %.0f s on the meter"), What, Hero.Meter);
	}
	else
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 hero: %s, score %d of %d"), What, Hero.Score, HR.score_threshold);
	}
}

FString APA250BattleGameMode::GetHeroLine(const ARFCharacter* Soldier) const
{
	const pa250::SideSetup* Side = HeroSide();
	const pa250::HeroRules& HR = HeroRules();
	if (!Soldier || !HR.loaded || !Side || !Side->has_hero) return FString();
	const FString Name = HeroName(Side);
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (IsPlayedGeneral(Soldier))
	{
		auto Ready = [Now](float At) { return At > Now ? FString::Printf(TEXT(" %.0f s"), At - Now) : FString(TEXT(" ready")); };
		FString Line = FString::Printf(TEXT("%s  -  %.0f s left   T Rally%s"), *Name.ToUpper(), Hero.Meter, *Ready(HeroRallyReadyAt));
		if (!Side->hero_ability.kind.empty())
		{
			Line += FString::Printf(TEXT("   U %s%s"), UTF8_TO_TCHAR(Side->hero_ability.label.c_str()), *Ready(HeroAbilityReadyAt));
		}
		return Line + TEXT("\nE sword   hold aim: pistol");
	}
	const ARFCharacter* General = HeroGeneral();
	if (Hero.bPlaying || !General || General->IsDead()) return FString();
	if (Hero.Score >= HR.score_threshold) return FString::Printf(TEXT("Y  Fight as %s"), *Name);
	return FString::Printf(TEXT("%s: %d of %d toward leading him (kills, posts, rallies)"), *Name, Hero.Score, HR.score_threshold);
}

FString APA250BattleGameMode::GetHeroScreenTitle() const
{
	const pa250::SideSetup* Side = HeroSide();
	return FString::Printf(TEXT("FIGHT AS %s"), *HeroName(Side).ToUpper());
}

FString APA250BattleGameMode::GetHeroScreenTraits() const
{
	const pa250::SideSetup* Side = HeroSide();
	FString Traits;
	if (Side)
	{
		for (const std::string& T : Side->commander_traits)
		{
			Traits += (Traits.IsEmpty() ? TEXT("") : TEXT(", ")) + FString(UTF8_TO_TCHAR(T.c_str()));
		}
	}
	return Traits;
}

FString APA250BattleGameMode::GetHeroScreenKit() const
{
	const pa250::SideSetup* Side = HeroSide();
	return Side ? PA250Hero::KitText(*Side, HeroRules()) : FString();
}

FString APA250BattleGameMode::GetHeroScreenCost() const
{
	return PA250Hero::FallCostText(pa250::commander_rules());
}

void APA250BattleGameMode::OpenHeroScreen(APlayerController* PC)
{
	if (!PC || bAutoplayBots || !CanOfferHero()) return;
	const ARFCharacter* Mine = Cast<ARFCharacter>(PC->GetPawn());
	if (!Mine || Mine->IsDead()) return;
	if (!HeroScreen) HeroScreen = CreateWidget<UPA250HeroScreen>(PC, UPA250HeroScreen::StaticClass());
	if (HeroScreen) HeroScreen->OpenFor(PC, this);
}

void APA250BattleGameMode::CloseHeroScreen(APlayerController* PC)
{
	if (HeroScreen && HeroScreen->IsInViewport()) HeroScreen->RemoveFromParent();
	if (!PC) return;
	PC->SetInputMode(FInputModeGameOnly());
	PC->bShowMouseCursor = false;
}

void APA250BattleGameMode::GiveBotController(ARFCharacter* Body, FName UnitClass)
{
	if (!Body || Body->IsDead() || Body->GetController()) return;
	Body->AIControllerClass = APA250BotController::StaticClass();
	Body->SpawnDefaultController();
	if (AController* Ctl = Body->GetController())
	{
		ControllerFaction.Add(Ctl, Body->FactionId);
		ControllerClass.Add(Ctl, UnitClass);
	}
	Body->GetCharacterMovement()->MaxWalkSpeed = 380.f * PA250UnitClasses::KitFor(Body->FactionId, Body->UnitClass).SpeedScale;
}

bool APA250BattleGameMode::HeroPossess(APlayerController* PC, ARFCharacter* Body)
{
	if (!PC || !Body || Body->IsDead()) return false;
	if (AController* BotCtl = Body->GetController(); BotCtl && BotCtl != PC)
	{
		BotCtl->StopMovement();
		BotCtl->UnPossess();
		ControllerFaction.Remove(BotCtl);
		ControllerClass.Remove(BotCtl);
		BotCtl->Destroy();
	}
	PC->Possess(Body); // leaves the body he had
	ControllerFaction.Add(PC, Body->FactionId);
	ControllerClass.Add(PC, Body->UnitClass);
	// He is the player's man now: none of the bot's habits stay with him.
	Body->SetDucking(false);
	Body->MisfireChance = 0.f;
	Body->MisfireStress = 0.f;
	Body->SquadId = -1;
	Body->bInLine = false;
	Body->GetCharacterMovement()->MaxWalkSpeed = Body->WalkSpeed;
	if (Body->MeleeComponent) Body->MeleeComponent->SetGuardHeld(false);
	const FRotator Facing = Body->GetActorRotation();
	FrameHumanCamera(PC, Body);
	PC->SetControlRotation(FRotator(-10.f, Facing.Yaw, 0.f));
	PC->SetViewTarget(Body);
	return PC->GetPawn() == Body;
}

bool APA250BattleGameMode::BeginHero(APlayerController* PC)
{
	const pa250::SideSetup* Side = HeroSide();
	ARFCharacter* General = HeroGeneral();
	ARFCharacter* Own = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	if (!PC || !Side || !CanOfferHero() || !General || !Own || Own->IsDead() || Own == General) return false;
	if (Own->IsMounted())
	{
		ShowNotice(TEXT("Dismount first: the general fights on foot"), 3.f);
		return false;
	}
	CloseHeroScreen(PC);
	const FName OwnClass = ControllerClass.FindRef(PC);
	if (!HeroPossess(PC, General)) return false;
	// His own man fights on in the ranks, and is remembered: the player comes back to him.
	GiveBotController(Own, OwnClass.IsNone() ? Own->UnitClass : OwnClass);
	HeroOwnBody = Own;
	PA250Hero::Begin(Hero, HeroRules());
	bHeroOffered = false;
	HeroRallyReadyAt = 0.f;
	HeroAbilityReadyAt = 0.f;
	// The kit: twice the man (health), the wider presence (GeneralAuraCm), sword and pistol.
	if (General->HealthComponent)
	{
		const float Scale = FMath::Max(1.f, Side->hero_health_scale);
		HeroBaseMaxHealth = General->HealthComponent->MaxHealth;
		General->HealthComponent->MaxHealth = HeroBaseMaxHealth * Scale;
		General->HealthComponent->CurrentHealth *= Scale;
	}
	const pa250::HeroRules& HR = HeroRules();
	General->SetHeroArms(true, HR.sword_damage, HR.sword_reach_cm, HR.sword_recover_seconds, HR.pistol_damage, HR.pistol_range_m,
		HR.pistol_reload_seconds);
	ShowNotice(FString::Printf(TEXT("You are %s for %.0f seconds"), *HeroName(Side), Hero.Meter), 5.f);
	UE_LOG(LogTemp, Display, TEXT("PA250 hero: the player takes %s (takeover %d), meter %.0f s, health %.0f, presence %.0f m; his own man %s fights on"),
		*HeroName(Side), Hero.Takeovers, Hero.Meter, General->HealthComponent ? General->HealthComponent->CurrentHealth : 0.f, Side->hero_aura_radius_m,
		*Own->GetName());
	return true;
}

void APA250BattleGameMode::RestoreGeneralKit(ARFCharacter* General)
{
	if (!General) return;
	const pa250::SideSetup* Side = HeroSide();
	General->SetHeroArms(false, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f);
	if (General->HealthComponent && HeroBaseMaxHealth > 0.f && Side)
	{
		// What he has taken stays with him, in the same share of the man he is again.
		const float Scale = FMath::Max(1.f, Side->hero_health_scale);
		General->HealthComponent->MaxHealth = HeroBaseMaxHealth;
		if (!General->IsDead()) General->HealthComponent->CurrentHealth = FMath::Max(1.f, General->HealthComponent->CurrentHealth / Scale);
	}
	HeroBaseMaxHealth = 0.f;
}

void APA250BattleGameMode::ReturnFromHero(APlayerController* PC)
{
	if (!PC || bBattleOver) return;
	ARFCharacter* Own = HeroOwnBody.Get();
	HeroOwnBody.Reset();
	// His own body, if it still stands.
	if (Own && !Own->IsDead() && HeroPossess(PC, Own))
	{
		ShowNotice(TEXT("You are back in your own boots"), 4.f);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero: the player returns to his own body %s"), *Own->GetName());
		return;
	}
	// Else the next man of his file, who leads it now.
	if (ARFCharacter* Next = (Volleys && Own) ? Volleys->PickSuccessor(Own) : nullptr; Next && HeroPossess(PC, Next))
	{
		Volleys->TransferLeadership(Own, Next);
		ShowNotice(TEXT("Your own man is down - you fight on as the next man of your file"), 5.f);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero: the player's own body is gone; he returns as %s of his file"), *Next->GetName());
		return;
	}
	// Else any man of his side still standing (never the general), else the spawn screen.
	const APawn* Before = PC->GetPawn();
	if (TakeOverSoldier(PC) && PC->GetPawn() != Before)
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 hero: the player returns as a man still standing"));
		return;
	}
	if (const ARFCharacter* Still = Cast<ARFCharacter>(PC->GetPawn()); Still && !Still->IsDead()) PC->UnPossess();
	UE_LOG(LogTemp, Display, TEXT("PA250 hero: no body to return to; the spawn screen"));
	ShowSpawnScreen(PC);
}

void APA250BattleGameMode::EndHero(APlayerController* PC)
{
	ARFCharacter* General = HeroGeneral();
	PA250Hero::End(Hero, false);
	RestoreGeneralKit(General);
	UE_LOG(LogTemp, Display, TEXT("PA250 hero: the meter has run out after %.0f s as the general"), Hero.SecondsPlayed);
	ReturnFromHero(PC);
	// The general is his own man again, at his post's orders.
	if (General && !General->IsDead())
	{
		if (PC && PC->GetPawn() == General) PC->UnPossess();
		GiveBotController(General, TEXT("officer"));
	}
}

void APA250BattleGameMode::HeroFell(APlayerController* PC)
{
	PA250Hero::End(Hero, true);
	RestoreGeneralKit(HeroGeneral());
	UE_LOG(LogTemp, Display, TEXT("PA250 hero: the general fell while the player fought as him (%.0f s played)"), Hero.SecondsPlayed);
	if (!PC) return;
	// The death cam on him, as on any body the player dies in; then a body again.
	if (APawn* Pawn = PC->GetPawn()) Pawn->SetLifeSpan(DeathCamSeconds + 2.f);
	TWeakObjectPtr<APlayerController> WeakPC = PC;
	FTimerHandle H;
	GetWorldTimerManager().SetTimer(H, FTimerDelegate::CreateWeakLambda(this, [this, WeakPC]() {
		if (APlayerController* C = WeakPC.Get(); C && !bBattleOver) ReturnFromHero(C);
	}), DeathCamSeconds, false);
}

bool APA250BattleGameMode::UseHeroRally(ARFCharacter* Soldier)
{
	const pa250::SideSetup* Side = HeroSide();
	if (!Side || !Volleys || !Soldier || !GetWorld()) return false;
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < HeroRallyReadyAt) return false;
	const int32 Rallied = Volleys->RallyAllRouting(Soldier->FactionId, Soldier->GetActorLocation(), Side->hero_rally_radius_m * 100.f);
	if (Rallied <= 0) return false;
	HeroRallyReadyAt = Now + Side->hero_rally_cooldown_seconds;
	++HeroRallies;
	NoteHeroEvent(EPA250HeroEvent::Rally);
	ShowNotice(FString::Printf(TEXT("\"To me!\" - %d %s to the general"), Rallied, Rallied == 1 ? TEXT("squad rallies") : TEXT("squads rally")), 4.f);
	UE_LOG(LogTemp, Display, TEXT("PA250 hero: rally reached %d squads within %.0f m"), Rallied, Side->hero_rally_radius_m);
	return true;
}

bool APA250BattleGameMode::RaiseHeroWork(const ARFCharacter* Soldier, float BuiltShare)
{
	UWorld* World = GetWorld();
	if (!World || !Soldier) return false;
	const FVector Front = Soldier->GetActorForwardVector().GetSafeNormal2D();
	FVector At = Soldier->GetActorLocation() + Front * 150.f;
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(PA250HeroWork), false, Soldier);
	if (World->LineTraceSingleByChannel(Hit, At + FVector(0.f, 0.f, 300.f), At - FVector(0.f, 0.f, 800.f), ECC_WorldStatic, Params) && Hit.bBlockingHit)
	{
		At.Z = Hit.ImpactPoint.Z;
	}
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	APA250FieldWork* Work = World->SpawnActor<APA250FieldWork>(At, FRotator(0.f, Front.Rotation().Yaw, 0.f), Spawn);
	if (!Work) return false;
	Work->Configure(EPA250WorkKind::Breastwork, Soldier->FactionId, FMath::Clamp(BuiltShare, 0.f, 1.f));
	Works.Add(Work);
	if (Volleys) Volleys->RefreshCoverAnchors(); // the men find it, and the crossing links follow
	return true;
}

void APA250BattleGameMode::UseHeroAbility(ARFCharacter* Soldier)
{
	const pa250::SideSetup* Side = HeroSide();
	if (!IsPlayedGeneral(Soldier) || Soldier->IsDead() || bBattleOver || !Side || !Volleys || !GetWorld()) return;
	const pa250::HeroAbility& A = Side->hero_ability;
	if (A.kind.empty())
	{
		ShowNotice(TEXT("He has no word of his own to give"), 3.f);
		return;
	}
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < HeroAbilityReadyAt)
	{
		ShowNotice(FString::Printf(TEXT("Not yet: %.0f s"), HeroAbilityReadyAt - Now), 2.f);
		return;
	}
	const FName Faction = Soldier->FactionId;
	const FVector Here = Soldier->GetActorLocation();
	const float Radius = A.radius_m * 100.f;
	const FString Kind(UTF8_TO_TCHAR(A.kind.c_str()));
	int32 Reached = 0;
	FString Detail;
	if (Kind == TEXT("hold_fast"))
	{
		Reached = Volleys->HeroHoldFast(Faction, Here, Radius, A.seconds, A.value);
	}
	else if (Kind == TEXT("charge"))
	{
		int32 Charging = 0;
		Reached = Volleys->HeroCharge(Faction, Here, Radius, A.seconds, A.value, Charging);
		Detail = FString::Printf(TEXT(", %d going in"), Charging);
	}
	else if (Kind == TEXT("steady"))
	{
		Reached = Volleys->HeroSteady(Faction, Here, Radius, A.value);
	}
	else if (Kind == TEXT("quick_step"))
	{
		Reached = Volleys->HeroQuickStep(Faction, Here, Radius, A.seconds, A.value);
	}
	else if (Kind == TEXT("throw_up_works"))
	{
		Reached = RaiseHeroWork(Soldier, A.value) ? 1 : 0;
	}
	else if (Kind == TEXT("huzza"))
	{
		const int32 Rallied = Volleys->RallyAllRouting(Faction, Here, Radius);
		HeroReinforceScale = FMath::Max(1.f, A.value);
		HeroReinforceUntil = Now + A.seconds;
		Reached = FMath::Max(1, Rallied); // the reserve comes up faster whether or not a squad was broken
		Detail = FString::Printf(TEXT(", %d rallied, the reserve %.2f times as fast for %.0f s"), Rallied, HeroReinforceScale, A.seconds);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("PA250 hero ability: unknown kind \"%s\" in the persons data"), *Kind);
		return;
	}
	if (Reached <= 0)
	{
		ShowNotice(FString::Printf(TEXT("No squad of yours within %.0f m to hear it"), A.radius_m), 3.f);
		return;
	}
	HeroAbilityReadyAt = Now + A.cooldown_seconds;
	++HeroAbilityUses;
	ShowNotice(FString::Printf(TEXT("\"%s!\" - %d %s it"), UTF8_TO_TCHAR(A.label.c_str()), Reached, Reached == 1 ? TEXT("squad hears") : TEXT("squads hear")), 4.f);
	UE_LOG(LogTemp, Display, TEXT("PA250 hero ability: %s reached %d squads within %.0f m%s"), *Kind, Reached, A.radius_m, *Detail);
}

float APA250BattleGameMode::HeroReinforceDelayScale(FName Faction) const
{
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	return (Faction == PlayerFaction && Now < HeroReinforceUntil && HeroReinforceScale > 1.f) ? 1.f / HeroReinforceScale : 1.f;
}

FString APA250BattleGameMode::HeroTraceJson() const
{
	const pa250::SideSetup* Side = HeroSide();
	return FString::Printf(TEXT(",\"hero\":{\"offered_side\":%s,\"score\":%d,\"takeovers\":%d,\"seconds_played\":%.1f,\"fell_while_played\":%s,\"rallies\":%d,\"abilities\":%d}"),
		Side && Side->has_hero ? TEXT("true") : TEXT("false"), Hero.Score, Hero.Takeovers, Hero.SecondsPlayed, Hero.bFellWhilePlayed ? TEXT("true") : TEXT("false"),
		HeroRallies, HeroAbilityUses);
}

void APA250BattleGameMode::TickHero(float DeltaSeconds)
{
	if (bBattleOver || !GetWorld()) return;
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	if (Hero.bPlaying)
	{
		ARFCharacter* General = HeroGeneral();
		if (!General || General->IsDead() || !PC || PC->GetPawn() != General)
		{
			// He is gone from under the player some other way than a fall the death path saw: close the account.
			if (General && !General->IsDead()) EndHero(PC);
			else HeroFell(PC);
		}
		else if (PA250Hero::Step(Hero, DeltaSeconds))
		{
			EndHero(PC);
		}
	}
	else if (!bHeroOffered && !bAutoplayBots && CanOfferHero())
	{
		bHeroOffered = true;
		ShowNotice(FString::Printf(TEXT("%s is yours to lead: press Y"), *HeroName(HeroSide())), 8.f);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero: offered (score %d of %d)"), Hero.Score, HeroRules().score_threshold);
	}
	TickHeroProbe(DeltaSeconds);
}

// ---- Debug probes ---------------------------------------------------------------------------
// -PA250HeroProbe=rally:<seconds>   a headless matchup: from that many seconds in, the general of the
//     player's side gives the hero's rally whenever a squad of his is routing and the rally is ready
//     (as a player would), and each use is logged with the routing squads before and after.
// -PA250HeroProbe=flow              a rendered run with the player in a body: the score is granted,
//     the screen opened (one still), the general taken, his rally and ability used, the meter run
//     out (back to his own body), the general taken again and shot down (commander_events, a body).
void APA250BattleGameMode::TickHeroProbe(float DeltaSeconds)
{
	if (HeroProbeMode < 0)
	{
		FString Arg;
		HeroProbeMode = 0;
		if (FParse::Value(FCommandLine::Get(), TEXT("PA250HeroProbe="), Arg))
		{
			if (Arg.StartsWith(TEXT("rally:")))
			{
				HeroProbeMode = 1;
				HeroProbeAt = FCString::Atof(*Arg.RightChop(6));
			}
			else if (Arg == TEXT("flow"))
			{
				HeroProbeMode = 2;
			}
			UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: %s"), *Arg);
		}
	}
	if (HeroProbeMode == 0) return;
	const pa250::SideSetup* Side = HeroSide();
	ARFCharacter* General = HeroGeneral();
	const float Now = GetWorld()->GetTimeSeconds();
	if (HeroProbeMode == 1)
	{
		if (!Side || !Side->has_hero || !General || General->IsDead() || !Volleys || Now - StartTime < HeroProbeAt || Now < HeroProbeNextAt) return;
		const int32 Before = Volleys->CountRouting(PlayerFaction);
		if (Before <= 0 && HeroProbeStep > 0) return;
		const int32 Reached = Volleys->RallyAllRouting(PlayerFaction, General->GetActorLocation(), Side->hero_rally_radius_m * 100.f);
		const int32 After = Volleys->CountRouting(PlayerFaction);
		++HeroProbeStep;
		if (Reached > 0) ++HeroRallies;
		HeroProbeNextAt = Now + (Reached > 0 ? Side->hero_rally_cooldown_seconds : 1.f);
		if (Reached > 0 || HeroProbeStep == 1)
		{
			UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: rally reached %d squads, routing %d -> %d (t=%.0f s)"), Reached, Before, After, Now - StartTime);
		}
		return;
	}
	// flow
	APlayerController* PC = GetWorld()->GetFirstPlayerController();
	ARFCharacter* Mine = PC ? Cast<ARFCharacter>(PC->GetPawn()) : nullptr;
	if (!PC || !Side) return;
	if (HeroProbeNextAt < 0.f)
	{
		if (!Mine || Mine->IsDead()) return;
		HeroProbeNextAt = Now + 10.f; // let the level settle
		return;
	}
	if (Now < HeroProbeNextAt) return;
	const pa250::HeroRules& HR = HeroRules();
	auto Next = [this, Now](float Seconds) { ++HeroProbeStep; HeroProbeNextAt = Now + Seconds; };
	switch (HeroProbeStep)
	{
	case 0:
	{
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: side has_hero=%d rules=%d general=%s can_offer_before=%d line=\"%s\""), Side->has_hero ? 1 : 0, HR.loaded ? 1 : 0,
			General ? *General->GetName() : TEXT("none"), CanOfferHero() ? 1 : 0, *GetHeroLine(Mine).Replace(TEXT("\n"), TEXT(" | ")));
		// What ten kills, a post and a rally are worth.
		for (int32 i = 0; i < 4; ++i) NoteHeroEvent(EPA250HeroEvent::Kill);
		NoteHeroEvent(EPA250HeroEvent::Capture);
		NoteHeroEvent(EPA250HeroEvent::Rally);
		while (Hero.Score < HR.score_threshold) NoteHeroEvent(EPA250HeroEvent::Kill);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: score %d of %d, can_offer=%d"), Hero.Score, HR.score_threshold, CanOfferHero() ? 1 : 0);
		Next(1.5f);
		break;
	}
	case 1:
		OpenHeroScreen(PC);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: screen open=%d title=\"%s\""), HeroScreen && HeroScreen->IsInViewport() ? 1 : 0, *GetHeroScreenTitle());
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: screen kit: %s"), *GetHeroScreenKit().Replace(TEXT("\n"), TEXT(" | ")));
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: screen cost: %s"), *GetHeroScreenCost().Replace(TEXT("\n"), TEXT(" | ")));
		Next(1.5f);
		break;
	case 2:
	{
		FString Tag;
		FParse::Value(FCommandLine::Get(), TEXT("PA250AnimProbeTag="), Tag);
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
			FString::Printf(TEXT("hero_screen_%s.png"), Tag.IsEmpty() ? TEXT("probe") : *Tag))), true, false);
		Next(1.5f);
		break;
	}
	case 3:
	{
		const FString OwnName = Mine ? Mine->GetName() : FString(TEXT("none"));
		if (HeroScreen) HeroScreen->Accept();
		const ARFCharacter* Now1 = Cast<ARFCharacter>(PC->GetPawn());
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: took command=%d pawn=%s is_general=%d own_body=%s own_is_bot=%d health=%.0f aura_m=%.0f line=\"%s\""), Hero.bPlaying ? 1 : 0,
			Now1 ? *Now1->GetName() : TEXT("none"), IsGeneral(Now1) ? 1 : 0, *OwnName, HeroOwnBody.IsValid() && Cast<APA250BotController>(HeroOwnBody->GetController()) ? 1 : 0,
			Now1 && Now1->HealthComponent ? Now1->HealthComponent->CurrentHealth : 0.f, GeneralAuraCm(PlayerFaction) / 100.f, *GetHeroLine(Now1).Replace(TEXT("\n"), TEXT(" | ")));
		if (ARFCharacter* Aimer = Cast<ARFCharacter>(PC->GetPawn())) Aimer->ProbeAim(true);
		{
			// A still of him with the pistol out, a little before the next step lets the aim go.
			FTimerHandle StillTimer;
			GetWorldTimerManager().SetTimer(StillTimer, FTimerDelegate::CreateWeakLambda(this, []() {
				FString PistolTag;
				FParse::Value(FCommandLine::Get(), TEXT("PA250AnimProbeTag="), PistolTag);
				FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
					FString::Printf(TEXT("hero_pistol_%s.png"), PistolTag.IsEmpty() ? TEXT("probe") : *PistolTag))), true, false);
			}), 1.2f, false);
		}
		Next(2.f);
		break;
	}
	case 4:
	{
		ARFCharacter* Me = Cast<ARFCharacter>(PC->GetPawn());
		{
			// The pistol is in his hand while he aims; one shot from it.
			const AMusketWeapon* Arm = Me ? Me->GetEquippedFirearm() : nullptr;
			const bool bFired = Me && Me->FireMusket();
			UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: arms=%d firearm_is_pistol=%d fired=%d sword=%d"), Me && Me->HasHeroArms() ? 1 : 0,
				Arm && Me && Arm != Me->Musket ? 1 : 0, bFired ? 1 : 0, Me && Me->MeleeComponent && Me->MeleeComponent->HasSword() ? 1 : 0);
			if (Me) Me->ProbeAim(false);
		}
		const bool bRallied = UseHeroRally(Me);
		const int32 Before = HeroAbilityUses;
		UseHeroAbility(Me);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: rally used=%d (routing squads %d), ability %s used=%d"), bRallied ? 1 : 0, Volleys ? Volleys->CountRouting(PlayerFaction) : -1,
			UTF8_TO_TCHAR(Side->hero_ability.kind.c_str()), HeroAbilityUses > Before ? 1 : 0);
		FString Tag;
		FParse::Value(FCommandLine::Get(), TEXT("PA250AnimProbeTag="), Tag);
		FScreenshotRequest::RequestScreenshot(FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectDir(), TEXT("../../docs/preview"),
			FString::Printf(TEXT("hero_played_%s.png"), Tag.IsEmpty() ? TEXT("probe") : *Tag))), true, false);
		Next(3.f);
		break;
	}
	case 5:
		Hero.Meter = FMath::Min(Hero.Meter, 0.5f); // run the meter out
		Next(2.f);
		break;
	case 6:
	{
		const ARFCharacter* Now1 = Cast<ARFCharacter>(PC->GetPawn());
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: meter out: playing=%d pawn=%s is_general=%d general_is_bot=%d general_health=%.0f seconds_played=%.1f"), Hero.bPlaying ? 1 : 0,
			Now1 ? *Now1->GetName() : TEXT("none"), IsGeneral(Now1) ? 1 : 0, General && Cast<APA250BotController>(General->GetController()) ? 1 : 0,
			General && General->HealthComponent ? General->HealthComponent->CurrentHealth : 0.f, Hero.SecondsPlayed);
		while (Hero.Score < HR.score_threshold) NoteHeroEvent(EPA250HeroEvent::Kill);
		Next(1.f);
		break;
	}
	case 7:
	{
		const bool bTook = BeginHero(PC);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: second takeover=%d takeovers=%d"), bTook ? 1 : 0, Hero.Takeovers);
		Next(2.f);
		break;
	}
	case 8:
		// He is shot down while the player fights as him.
		if (ARFCharacter* Me = Cast<ARFCharacter>(PC->GetPawn()); Me && IsGeneral(Me))
		{
			UGameplayStatics::ApplyDamage(Me, 100000.f, nullptr, nullptr, UDamageType::StaticClass());
		}
		Next(DeathCamSeconds + 2.f);
		break;
	case 9:
	{
		const ARFCharacter* Now1 = Cast<ARFCharacter>(PC->GetPawn());
		const FString* Fate = GeneralFate.Find(PlayerFaction);
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: general fell: fate=%s fell_while_played=%d playing=%d player_pawn=%s alive=%d is_general=%d can_offer_after=%d"),
			Fate ? **Fate : TEXT("none"), Hero.bFellWhilePlayed ? 1 : 0, Hero.bPlaying ? 1 : 0, Now1 ? *Now1->GetName() : TEXT("none"), Now1 && !Now1->IsDead() ? 1 : 0,
			IsGeneral(Now1) ? 1 : 0, CanOfferHero() ? 1 : 0);
		Next(1.f);
		break;
	}
	default:
		UE_LOG(LogTemp, Display, TEXT("PA250 hero probe: done%s"), *HeroTraceJson());
		FPlatformMisc::RequestExit(false);
		HeroProbeMode = 0;
		break;
	}
}
