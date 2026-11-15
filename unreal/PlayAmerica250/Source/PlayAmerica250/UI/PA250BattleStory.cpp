#include "PA250BattleStory.h"
#include "PA250HudStyle.h"
#include "Campaign/PA250Subsystem.h"
#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
struct FStoryEntry
{
	PA250BattleStory::FStory Story;
	FString Province, Scenario, Terrain;
};
TArray<FStoryEntry> GStories, GFallbacks;
PA250BattleStory::FPan GPan;
FString GNarrationDir;
bool GStoriesLoaded = false;

FString StoryF(const std::string& S) { return FString(UTF8_TO_TCHAR(S.c_str())); }
}

bool PA250BattleStory::Load(FString& OutError, bool bForce)
{
	if (GStoriesLoaded && !bForce) return true;
	GStories.Reset();
	GFallbacks.Reset();
	GStoriesLoaded = false;
	FString Text;
	const FString Path = UPA250Subsystem::SimDataPath(TEXT("battle_stories.json"));
	if (!FFileHelper::LoadFileToString(Text, *Path))
	{
		OutError = FString::Printf(TEXT("cannot read %s"), *Path);
		return false;
	}
	TSharedPtr<FJsonObject> Root;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
	{
		OutError = TEXT("battle_stories.json is not valid JSON");
		return false;
	}
	TMap<FString, FString> Credits;
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("images")))
	{
		const TSharedPtr<FJsonObject> I = V->AsObject();
		Credits.Add(I->GetStringField(TEXT("key")), FString::Printf(TEXT("%s. %s, %s. %s."), *I->GetStringField(TEXT("work")), *I->GetStringField(TEXT("artist")),
			*I->GetStringField(TEXT("year")), *I->GetStringField(TEXT("collection"))));
	}
	if (const TSharedPtr<FJsonObject>* PanObj = nullptr; Root->TryGetObjectField(TEXT("pan"), PanObj))
	{
		GPan.Seconds = FMath::Max(1.f, static_cast<float>((*PanObj)->GetNumberField(TEXT("seconds"))));
		GPan.Zoom = FMath::Max(1.f, static_cast<float>((*PanObj)->GetNumberField(TEXT("zoom"))));
		GPan.Travel = FMath::Max(0.f, static_cast<float>((*PanObj)->GetNumberField(TEXT("travel"))));
	}
	if (const TSharedPtr<FJsonObject>* Nar = nullptr; Root->TryGetObjectField(TEXT("narration"), Nar)) GNarrationDir = (*Nar)->GetStringField(TEXT("asset_dir"));
	auto Read = [&Credits](const TSharedPtr<FJsonObject>& S) {
		FStoryEntry E;
		E.Story.bFound = true;
		E.Story.Key = S->GetStringField(TEXT("key"));
		E.Story.Title = S->GetStringField(TEXT("title"));
		E.Story.Date = S->GetStringField(TEXT("date"));
		E.Story.Image = S->GetStringField(TEXT("image"));
		E.Story.Credit = Credits.FindRef(E.Story.Image);
		E.Story.Narration = S->GetStringField(TEXT("narration"));
		for (const TSharedPtr<FJsonValue>& T : S->GetArrayField(TEXT("text"))) E.Story.Sentences.Add(T->AsString());
		if (const TSharedPtr<FJsonObject>* M = nullptr; S->TryGetObjectField(TEXT("match"), M))
		{
			(*M)->TryGetStringField(TEXT("province"), E.Province);
			(*M)->TryGetStringField(TEXT("scenario"), E.Scenario);
		}
		S->TryGetStringField(TEXT("terrain"), E.Terrain);
		return E;
	};
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("stories"))) GStories.Add(Read(V->AsObject()));
	for (const TSharedPtr<FJsonValue>& V : Root->GetArrayField(TEXT("fallbacks")))
	{
		FStoryEntry E = Read(V->AsObject());
		E.Story.bFallback = true;
		GFallbacks.Add(MoveTemp(E));
	}
	GStoriesLoaded = true;
	return true;
}

int32 PA250BattleStory::NumStories() { return GStories.Num(); }
int32 PA250BattleStory::NumFallbacks() { return GFallbacks.Num(); }
PA250BattleStory::FPan PA250BattleStory::Pan() { return GPan; }

PA250BattleStory::FStory PA250BattleStory::StoryFor(const pa250::BattleSetup& Setup)
{
	const FString Town = StoryF(Setup.province_id);
	const FString Scenario = StoryF(Setup.scenario_preset);
	for (const FStoryEntry& E : GStories)
	{
		if (!E.Province.IsEmpty() && E.Province == Town) return E.Story;
	}
	for (const FStoryEntry& E : GStories)
	{
		// A scenario fought away from its own town (the Battle Road preset covers several).
		if (E.Province.IsEmpty() && !E.Scenario.IsEmpty() && E.Scenario == Scenario) return E.Story;
	}
	for (const std::string& Tag : Setup.terrain_tags)
	{
		for (const FStoryEntry& E : GFallbacks)
		{
			if (E.Terrain == StoryF(Tag)) return E.Story;
		}
	}
	return GFallbacks.Num() > 0 ? GFallbacks[0].Story : FStory();
}

FString PA250BattleStory::SituationLine(const pa250::BattleSetup& Setup)
{
	const std::string Mine = Setup.player_faction.empty() ? std::string("continental") : Setup.player_faction;
	const std::string Theirs = Mine == "british" ? "continental" : "british";
	const auto Me = Setup.sides.find(Mine);
	const auto Them = Setup.sides.find(Theirs);
	if (Me == Setup.sides.end()) return FString();
	int32 Men = 0;
	for (const pa250::SideFactor& F : Me->second.factors)
	{
		if (F.label == "men") Men += F.value;
	}
	const bool bAttack = Setup.attacker_faction == Mine;
	FString Line = FString::Printf(TEXT("Today you %s %s with %d men, %d on the field at a time, %d rounds a man"), bAttack ? TEXT("attack at") : TEXT("defend"),
		*StoryF(Setup.province_name), Men, Me->second.field_strength, Me->second.rounds_per_man);
	Line += Me->second.commander_name.empty() ? FString(TEXT(", and no general to lead them")) : TEXT(", under ") + StoryF(Me->second.commander_name);
	if (Me->second.fortification_level > 0) Line += FString::Printf(TEXT(", behind works of level %d"), Me->second.fortification_level);
	if (Them != Setup.sides.end())
	{
		if (Them->second.intel == "estimate")
		{
			Line += FString::Printf(TEXT(". The enemy is thought to be %d to %d men"), Them->second.intel_men_low, Them->second.intel_men_high);
		}
		else
		{
			int32 Theirs_ = 0;
			for (const pa250::SideFactor& F : Them->second.factors)
			{
				if (F.label == "men") Theirs_ += F.value;
			}
			Line += FString::Printf(TEXT(". The enemy has %d"), Theirs_);
		}
	}
	return Line + TEXT(".");
}

FVector2D PA250BattleStory::PanOffset(float Seconds, const FPan& P)
{
	// There and back in two passes, easing at each end so the picture never jerks.
	const float Phase = FMath::Fmod(FMath::Max(0.f, Seconds), 2.f * P.Seconds) / P.Seconds;  // 0..2
	const float T = Phase <= 1.f ? Phase : 2.f - Phase;
	const float Eased = T * T * (3.f - 2.f * T);
	return FVector2D((Eased - 0.5f) * P.Travel, (0.5f - Eased) * P.Travel * 0.35f);
}

FString PA250BattleStory::NarrationAssetPath(const FStory& Story)
{
	if (Story.Narration.IsEmpty() || GNarrationDir.IsEmpty()) return FString();
	return FString::Printf(TEXT("%s/%s.%s"), *GNarrationDir, *Story.Narration, *Story.Narration);
}

void SPA250BattleStoryScreen::Construct(const FArguments& InArgs)
{
	Picture = InArgs._Picture;
	Pan = PA250BattleStory::Pan();
	StartSeconds = InArgs._StartSeconds;
	IsReady = InArgs._IsReady;
	OnContinue = InArgs._OnContinue;
	const PA250BattleStory::FStory Story = InArgs._Story;
	const FLinearColor Ink = FPA250HudStyle::Ink;
	const FLinearColor InkDim(0.12f, 0.08f, 0.04f, 0.72f);
	auto Line = [](const FString& S, const TCHAR* Face, int32 Size, const FLinearColor& Color, float Wrap) {
		return SNew(STextBlock).Text(FText::FromString(S)).Font(FPA250HudStyle::Font(Face, Size)).ColorAndOpacity(Color).WrapTextAt(Wrap).LineHeightPercentage(1.15f);
	};
	TSharedRef<SVerticalBox> Card = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()[Line(Story.Date, TEXT("Italic"), 15, InkDim, 900.f)]
		+ SVerticalBox::Slot().AutoHeight()[Line(Story.Title, TEXT("Bold"), 34, Ink, 900.f)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 8.f, 0.f, 0.f)[Line(FString::Join(Story.Sentences, TEXT(" ")), TEXT("Regular"), 17, Ink, 900.f)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)[Line(InArgs._Situation, TEXT("Bold"), 15, Ink, 900.f)]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
			  [SNew(SHorizontalBox)
			   + SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					 [Line(InArgs._bPlaceholder ? FString::Printf(TEXT("Picture wanted: %s"), *Story.Credit) : Story.Credit, TEXT("Italic"), 11, InkDim, 640.f)]
			   + SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					 [SNew(SOverlay)
					  + SOverlay::Slot().VAlign(VAlign_Center)
							[SNew(STextBlock)
								 .Font(FPA250HudStyle::Font(TEXT("Italic"), 14))
								 .ColorAndOpacity(InkDim)
								 .Text(FText::FromString(TEXT("The field is being made ready...")))
								 .Visibility_Lambda([this]() { return IsReady && IsReady() ? EVisibility::Hidden : EVisibility::Visible; })]
					  + SOverlay::Slot().HAlign(HAlign_Right)
							[SNew(SButton)
								 .ButtonStyle(&FPA250HudStyle::Get().Button)
								 .IsFocusable(false)
								 .ContentPadding(FMargin(18.f, 6.f))
								 .Visibility_Lambda([this]() { return IsReady && IsReady() ? EVisibility::Visible : EVisibility::Hidden; })
								 .OnClicked_Lambda([this]() {
									 OnContinue.ExecuteIfBound();
									 return FReply::Handled();
								 })
									 [SNew(STextBlock).Font(FPA250HudStyle::Font(TEXT("Bold"), 15)).ColorAndOpacity(FPA250HudStyle::Cream).Text(FText::FromString(TEXT("Continue")))]]]];
	ChildSlot
		[SNew(SOverlay)
		 + SOverlay::Slot()[SNew(SImage).Image(&FPA250HudStyle::Get().White).ColorAndOpacity(FLinearColor(0.02f, 0.015f, 0.01f, 1.f))]
		 // The picture, a little larger than the screen, drifting slowly.
		 + SOverlay::Slot()
			   [SNew(SImage)
					.Image(&Picture)
					.RenderTransformPivot(FVector2D(0.5f, 0.5f))
					.RenderTransform_Lambda([this]() -> TOptional<FSlateRenderTransform> {
						const FVector2D Off = PA250BattleStory::PanOffset(static_cast<float>(FPlatformTime::Seconds() - StartSeconds), Pan);
						const FVector2D Size = GetTickSpaceGeometry().GetLocalSize();
						return FSlateRenderTransform(FScale2D(Pan.Zoom), FVector2D(Off.X * Size.X, Off.Y * Size.Y));
					})]
		 + SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Center).Padding(FMargin(0.f, 0.f, 0.f, 70.f))
			   [SNew(SBox).WidthOverride(980.f)
					[SNew(SBorder).BorderImage(&FPA250HudStyle::Get().Parchment).Padding(FMargin(30.f, 20.f))[Card]]]];
}
