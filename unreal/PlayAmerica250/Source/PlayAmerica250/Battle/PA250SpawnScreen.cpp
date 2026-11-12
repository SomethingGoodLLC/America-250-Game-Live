#include "PA250SpawnScreen.h"
#include "PA250BattleGameMode.h"
#include "PA250ControlPoint.h"
#include "PA250UnitClasses.h"
#include "RFCharacter.h"
#include "PA250HudStyle.h"
#include "Engine/Texture2D.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void UPA250SpawnScreen::OpenFor(APlayerController* PC, APA250BattleGameMode* GM)
{
	OwnerPC = PC;
	GameMode = GM;
	SelectedPost.Reset();
	if (GM)
	{
		// Keep the class he last fought as while the army still has men of it; else the first open one.
		const TArray<APA250BattleGameMode::FClassChoice> Choices = GM->GetClassChoices(GM->GetPlayerFaction());
		const FName Last = GM->GetControllerClass(PC);
		FName Pick;
		for (const APA250BattleGameMode::FClassChoice& C : Choices)
		{
			if (C.MenLeft <= 0) continue;
			if (Pick.IsNone() || C.Id == Last) Pick = C.Id;
		}
		SelectedClass = Pick.IsNone() ? (Last.IsNone() ? FName(TEXT("line_infantry")) : Last) : Pick;
		for (const TWeakObjectPtr<APA250ControlPoint>& P : GM->GetPosts())
		{
			if (P.IsValid() && P->GetHolder() == GM->GetPlayerFaction())
			{
				SelectedPost = P;
				break;
			}
		}
	}
	// CreateWidget builds the slate before GameMode is assigned, so the held-CP list
	// was empty for the rest of the match. Drop it and rebuild now that posts are known.
	if (IsInViewport())
	{
		RemoveFromParent();
	}
	ReleaseSlateResources(true);
	AddToViewport(100);
	if (PC)
	{
		PC->SetInputMode(FInputModeUIOnly());
		PC->bShowMouseCursor = true;
	}
}

void UPA250SpawnScreen::Confirm()
{
	APA250BattleGameMode* GM = GameMode.Get();
	APlayerController* PC = OwnerPC.Get();
	if (!GM || !PC)
	{
		return;
	}
	bool bDone = false;
	const ARFCharacter* Me = Cast<ARFCharacter>(PC->GetPawn());
	if (GM->IsOpeningClassPick() && Me && !Me->IsDead())
	{
		// Before the first shot: he is already on the field, so he only changes kit.
		GM->RekitPlayer(PC, SelectedClass);
		GM->EndOpeningClassPick();
		bDone = true;
	}
	else
	{
		GM->EndOpeningClassPick();
		bDone = GM->RequestSpawn(PC, SelectedClass, SelectedPost.Get());
	}
	if (bDone)
	{
		RemoveFromParent();
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

TSharedRef<SWidget> UPA250SpawnScreen::RebuildWidget()
{
	const FPA250HudStyle& St = FPA250HudStyle::Get();
	const FSlateFontInfo Title = FPA250HudStyle::Font(TEXT("Bold"), 22);
	const FSlateFontInfo Head = FPA250HudStyle::Font(TEXT("Bold"), 15);
	const FSlateFontInfo Body = FPA250HudStyle::Font(TEXT("Regular"), 12);
	const FSlateFontInfo Italic = FPA250HudStyle::Font(TEXT("Italic"), 12);
	const FSlateFontInfo Label = FPA250HudStyle::Font(TEXT("Bold"), 11);
	TWeakObjectPtr<UPA250SpawnScreen> WeakThis(this);
	APA250BattleGameMode* GM = GameMode.Get();
	const FName Side = GM ? GM->GetPlayerFaction() : NAME_None;
	const bool bOpening = GM && GM->IsOpeningClassPick();

	// ---- Class cards: one per class the army brought, with what it is good and bad at. ----
	TSharedRef<SWrapBox> Cards = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(10.f, 10.f));
	PortraitTextures.Reset();
	PortraitBrushes.Reset();
	if (GM)
	{
		for (const APA250BattleGameMode::FClassChoice& Choice : GM->GetClassChoices(Side))
		{
			const FName Cls = Choice.Id;
			const FPA250UnitClassKit& Kit = PA250UnitClasses::KitFor(Side, Cls);
			auto MenLeft = [WeakThis, Side, Cls]() {
				const APA250BattleGameMode* G = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
				return G ? G->ClassMenLeft(Side, Cls) : 0;
			};
			auto Selected = [WeakThis, Cls]() { return WeakThis.IsValid() && WeakThis->SelectedClass == Cls; };
			// The class as the battle draws him (a still of the in-game soldier). No image, no gap.
			TSharedRef<SWidget> Portrait = SNullWidget::NullWidget;
			const FString TexPath = FString::Printf(TEXT("/Game/PA250/UI/Classes/T_Class_%s_%s.T_Class_%s_%s"), *Side.ToString(), *Cls.ToString(), *Side.ToString(), *Cls.ToString());
			if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *TexPath, nullptr, LOAD_NoWarn | LOAD_Quiet))
			{
				PortraitTextures.Add(Tex);
				TSharedPtr<FSlateBrush> Brush = MakeShared<FSlateBrush>();
				Brush->SetResourceObject(Tex);
				Brush->ImageSize = FVector2D(222.f, 166.5f); // 4:3, the card's inner width
				Brush->DrawAs = ESlateBrushDrawType::Image;
				PortraitBrushes.Add(Brush);
				Portrait = SNew(SBox).WidthOverride(222.f).HeightOverride(166.5f)[SNew(SImage).Image(Brush.Get())];
			}
			Cards->AddSlot()
			[
				SNew(SBox).WidthOverride(250.f).MinDesiredHeight(360.f)
				[
					// A brass edge marks the chosen card.
					SNew(SBorder).BorderImage(&St.White).Padding(2.f)
					.BorderBackgroundColor_Lambda([Selected]() { return Selected() ? FPA250HudStyle::Brass : FLinearColor(0.f, 0.f, 0.f, 0.f); })
					[
						SNew(SButton).ButtonStyle(&St.Button).IsFocusable(false).ContentPadding(FMargin(12.f, 10.f))
						.IsEnabled_Lambda([MenLeft]() { return MenLeft() > 0; })
						.OnClicked_Lambda([WeakThis, Cls]() {
							if (WeakThis.IsValid()) WeakThis->SelectedClass = Cls;
							return FReply::Handled();
						})
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 8.f)[Portrait]
							+ SVerticalBox::Slot().AutoHeight()
							[SNew(STextBlock).Font(Head).ColorAndOpacity(FPA250HudStyle::Cream).Text(FText::FromString(FString(Kit.Label).ToUpper()))]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 1.f, 0.f, 6.f)
							[SNew(STextBlock).Font(Italic).ColorAndOpacity(FPA250HudStyle::CreamDim).Text(FText::FromString(Kit.Weapon))]
							+ SVerticalBox::Slot().AutoHeight()
							[SNew(STextBlock).Font(Body).ColorAndOpacity(FPA250HudStyle::Cream).WrapTextAt(222.f).Text(FText::FromString(Kit.Blurb))]
							+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 6.f, 0.f, 0.f)
							[SNew(STextBlock).Font(Label).ColorAndOpacity(FPA250HudStyle::Brass).WrapTextAt(222.f).Text(FText::FromString(PA250UnitClasses::TraitLine(Kit)))]
							+ SVerticalBox::Slot().FillHeight(1.f).VAlign(VAlign_Bottom).Padding(0.f, 8.f, 0.f, 0.f)
							[SNew(STextBlock).Font(Label)
								.ColorAndOpacity_Lambda([MenLeft]() { return FSlateColor(MenLeft() > 0 ? FPA250HudStyle::CreamDim : FPA250HudStyle::Red); })
								.Text_Lambda([MenLeft, Selected]() {
									const int32 N = MenLeft();
									if (N <= 0) return FText::FromString(TEXT("None left"));
									return FText::FromString(FString::Printf(TEXT("%d %s left%s"), N, N == 1 ? TEXT("man") : TEXT("men"), Selected() ? TEXT("   ·   CHOSEN") : TEXT("")));
								})]
						]
					]
				]
			];
		}
	}

	// ---- Where to come back in (respawn only). ----
	TSharedRef<SWrapBox> PostBox = SNew(SWrapBox).UseAllottedSize(true).InnerSlotPadding(FVector2D(8.f, 6.f));
	auto PostBtn = [&St, &Label, WeakThis](TWeakObjectPtr<APA250ControlPoint> WeakPost, const FString& Name, bool bBase) {
		return SNew(SBorder).BorderImage(&St.White).Padding(2.f)
			.BorderBackgroundColor_Lambda([WeakThis, WeakPost, bBase]() {
				const bool bSel = WeakThis.IsValid() && (bBase ? !WeakThis->SelectedPost.IsValid() : WeakThis->SelectedPost.Get() == WeakPost.Get());
				return bSel ? FPA250HudStyle::Brass : FLinearColor(0.f, 0.f, 0.f, 0.f);
			})
			[
				SNew(SButton).ButtonStyle(&St.Button).IsFocusable(false).ContentPadding(FMargin(12.f, 5.f))
				.OnClicked_Lambda([WeakThis, WeakPost, bBase]() {
					if (WeakThis.IsValid())
					{
						if (bBase) WeakThis->SelectedPost.Reset();
						else WeakThis->SelectedPost = WeakPost;
					}
					return FReply::Handled();
				})
				[SNew(STextBlock).Font(Label).ColorAndOpacity(FPA250HudStyle::Cream).Text(FText::FromString(Name))]
			];
	};
	PostBox->AddSlot()[PostBtn(nullptr, TEXT("Rear (your side's start)"), true)];
	if (GM)
	{
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : GM->GetPosts())
		{
			APA250ControlPoint* Post = WP.Get();
			if (!Post || Post->GetHolder() != Side) continue;
			PostBox->AddSlot()[PostBtn(Post, Post->GetObjectiveTitle(), false)];
		}
	}

	auto Section = [&Label](const TCHAR* Text) {
		return SNew(STextBlock).Font(Label).ColorAndOpacity(FPA250HudStyle::BrassDim).Text(FText::FromString(Text));
	};
	const FString SideName = Side == TEXT("british") ? TEXT("His Majesty's troops") : TEXT("The Provincials");

	TSharedRef<SVerticalBox> Column = SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[SNew(STextBlock).Font(Title).ColorAndOpacity(FPA250HudStyle::Cream).Text_Lambda([WeakThis, bOpening]() {
			const APA250BattleGameMode* G = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
			if (bOpening) return FText::FromString(TEXT("CHOOSE YOUR SOLDIER"));
			return FText::FromString(FString::Printf(TEXT("RETURN TO THE FIGHT     reserves %d"), G ? G->TicketsRemaining(G->GetPlayerFaction()) : 0));
		})]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 2.f, 0.f, 12.f)
		[SNew(STextBlock).Font(Italic).ColorAndOpacity(bOpening ? FPA250HudStyle::CreamDim : FPA250HudStyle::Red).Text_Lambda([WeakThis, bOpening, SideName]() {
			if (bOpening) return FText::FromString(SideName + TEXT(" - these are the troops your army brought to this field."));
			const APA250BattleGameMode* G = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
			const FString Killer = G ? G->GetLastKillerLabel() : FString();
			return FText::FromString(Killer.IsEmpty() ? FString(TEXT("Killed in action")) : FString::Printf(TEXT("Killed by %s"), *Killer));
		})]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 6.f)[Section(TEXT("CLASS"))]
		+ SVerticalBox::Slot().AutoHeight()[Cards];
	if (!bOpening)
	{
		Column->AddSlot().AutoHeight().Padding(0.f, 14.f, 0.f, 6.f)[Section(TEXT("SPAWN POINT"))];
		Column->AddSlot().AutoHeight()[PostBox];
	}
	Column->AddSlot().AutoHeight().HAlign(HAlign_Right).Padding(0.f, 16.f, 0.f, 0.f)
	[
		SNew(SButton).ButtonStyle(&St.Button).IsFocusable(false).ContentPadding(FMargin(34.f, 9.f))
		.OnClicked_Lambda([WeakThis]() {
			if (WeakThis.IsValid()) WeakThis->Confirm();
			return FReply::Handled();
		})
		[SNew(STextBlock).Font(Head).ColorAndOpacity(FPA250HudStyle::Cream).Text_Lambda([WeakThis]() {
			const FName Cls = WeakThis.IsValid() ? WeakThis->SelectedClass : NAME_None;
			const APA250BattleGameMode* G = WeakThis.IsValid() ? WeakThis->GameMode.Get() : nullptr;
			const FString Name = G ? FString(PA250UnitClasses::KitFor(G->GetPlayerFaction(), Cls).Label) : FString();
			return FText::FromString(FString::Printf(TEXT("DEPLOY AS %s"), *Name.ToUpper()));
		})]
	];

	return SNew(SBorder)
		.BorderImage(&St.White)
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.62f))
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox).WidthOverride(1100.f)
			[
				SNew(SBorder).BorderImage(&St.Panel).Padding(FMargin(26.f, 20.f))[Column]
			]
		];
}
