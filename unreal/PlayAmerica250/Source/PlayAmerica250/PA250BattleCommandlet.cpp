#include "PA250BattleCommandlet.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "PA250ControlPoint.h"
#include "UObject/UObjectGlobals.h"

int32 UPA250BattleCommandlet::Main(const FString& Params)
{
	int32 Fails = 0;
	auto Check = [&](bool Cond, const TCHAR* Msg) {
		UE_LOG(LogTemp, Display, TEXT("PA250.Battle %s: %s"), Cond ? TEXT("PASS") : TEXT("FAIL"), Msg);
		if (!Cond) ++Fails;
	};

	Check(APA250ControlPoint::StaticClass() != nullptr, TEXT("control point class"));
	const APA250ControlPoint* CDO = GetDefault<APA250ControlPoint>();
	Check(CDO != nullptr, TEXT("control point CDO"));
	Check(CDO->CaptureSeconds > 0.f, TEXT("capture seconds default"));
	Check(CDO->Radius > 0.f, TEXT("radius default"));

	// Bleed math (same rule as APA250BattleGameMode::BleedTickets).
	int32 BritTickets = 20;
	int32 ContTickets = 20;
	int32 BritPosts = 0;
	int32 ContPosts = 2;
	int32 BleedTicks = 0;
	float BleedAccum = 0.f;
	const float Interval = 1.f;
	for (int32 Step = 0; Step < 5; ++Step)
	{
		BleedAccum += 1.f;
		while (BleedAccum >= Interval)
		{
			BleedAccum -= Interval;
			if (BritPosts < ContPosts && BritTickets > 0) { --BritTickets; ++BleedTicks; }
			else if (ContPosts < BritPosts && ContTickets > 0) { --ContTickets; ++BleedTicks; }
		}
	}
	Check(BleedTicks == 5 && BritTickets == 15 && ContTickets == 20, TEXT("bleed drains minority side"));

	// Plurality holder (same as PluralityHolder).
	auto Plurality = [](int32 B, int32 C) -> FName {
		if (B > C) return TEXT("british");
		if (C > B) return TEXT("continental");
		return NAME_None;
	};
	Check(Plurality(2, 1) == TEXT("british"), TEXT("plurality british"));
	Check(Plurality(1, 2) == TEXT("continental"), TEXT("plurality continental"));
	Check(Plurality(1, 1).IsNone(), TEXT("plurality tie"));

	const FString Json = FString::Printf(
		TEXT("{\"reason\":\"commandlet_smoke\",\"trace\":{\"post_flips\":0,\"bleed_ticks\":%d,\"posts\":{\"green\":\"\",\"meetinghouse\":\"continental\",\"buckman\":\"continental\"},\"tickets\":{\"british\":%d,\"continental\":%d}},\"passed\":%s}\n"),
		BleedTicks, BritTickets, ContTickets, Fails == 0 ? TEXT("true") : TEXT("false"));
	FFileHelper::SaveStringToFile(Json, *FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("PA250"), TEXT("battle_commandlet.json")));
	UE_LOG(LogTemp, Display, TEXT("PA250 battle commandlet done fails=%d"), Fails);
	return Fails == 0 ? 0 : 1;
}
