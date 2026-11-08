#include "PA250SpawnScreen.h"
#include "PA250BattleGameMode.h"
#include "PA250ControlPoint.h"
#include "Styling/AppStyle.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

void UPA250SpawnScreen::OpenFor(APlayerController* PC, APA250BattleGameMode* GM)
{
	OwnerPC = PC;
	GameMode = GM;
	SelectedClass = TEXT("line_infantry");
	SelectedPost.Reset();
	if (GM)
	{
		for (const TWeakObjectPtr<APA250ControlPoint>& P : GM->GetPosts())
		{
			if (P.IsValid() && P->GetHolder() == GM->GetPlayerFaction())
			{
				SelectedPost = P;
				break;
			}
		}
	}
	if (!IsInViewport())
	{
		AddToViewport(100);
	}
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
	if (GM->RequestSpawn(PC, SelectedClass, SelectedPost.Get()))
	{
		RemoveFromParent();
		PC->SetInputMode(FInputModeGameOnly());
		PC->bShowMouseCursor = false;
	}
}

TSharedRef<SWidget> UPA250SpawnScreen::RebuildWidget()
{
	const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	const FSlateFontInfo Small = FCoreStyle::GetDefaultFontStyle("Regular", 14);
	TWeakObjectPtr<UPA250SpawnScreen> WeakThis(this);

	auto ClassBtn = [WeakThis](const FName Cls, const FString& Label) {
		return SNew(SButton)
			.OnClicked_Lambda([WeakThis, Cls]() {
				if (WeakThis.IsValid()) WeakThis->SelectedClass = Cls;
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 14)).Text_Lambda([WeakThis, Cls, Label]() {
					const bool Sel = WeakThis.IsValid() && WeakThis->SelectedClass == Cls;
					return FText::FromString(Sel ? FString::Printf(TEXT("[%s]"), *Label) : Label);
				})
			];
	};

	TSharedRef<SVerticalBox> PostBox = SNew(SVerticalBox);
	PostBox->AddSlot().AutoHeight().Padding(2)
	[
		SNew(SButton)
		.OnClicked_Lambda([WeakThis]() {
			if (WeakThis.IsValid()) WeakThis->SelectedPost.Reset();
			return FReply::Handled();
		})
		[
			SNew(STextBlock).Font(Small).Text_Lambda([WeakThis]() {
				const bool Sel = WeakThis.IsValid() && !WeakThis->SelectedPost.IsValid();
				return FText::FromString(Sel ? TEXT("[Team Base]") : TEXT("Team Base"));
			})
		]
	];

	if (APA250BattleGameMode* GM = GameMode.Get())
	{
		for (const TWeakObjectPtr<APA250ControlPoint>& WP : GM->GetPosts())
		{
			APA250ControlPoint* Post = WP.Get();
			if (!Post || Post->GetHolder() != GM->GetPlayerFaction())
			{
				continue;
			}
			TWeakObjectPtr<APA250ControlPoint> WeakPost = Post;
			const FString Label = Post->PostId.ToString();
			PostBox->AddSlot().AutoHeight().Padding(2)
			[
				SNew(SButton)
				.OnClicked_Lambda([WeakThis, WeakPost]() {
					if (WeakThis.IsValid()) WeakThis->SelectedPost = WeakPost;
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Font(Small).Text_Lambda([WeakThis, WeakPost, Label]() {
						const bool Sel = WeakThis.IsValid() && WeakThis->SelectedPost.Get() == WeakPost.Get();
						return FText::FromString(Sel ? FString::Printf(TEXT("[%s]"), *Label) : Label);
					})
				]
			];
		}
	}

	return SNew(SBorder)
		.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
		.BorderBackgroundColor(FLinearColor(0.f, 0.f, 0.f, 0.72f))
		.Padding(24.f)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[SNew(STextBlock).Font(Font).ColorAndOpacity(FLinearColor::White).Text(FText::FromString(TEXT("RESPAWN")))]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4)
			[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.7f)).Text(FText::FromString(TEXT("Class")))]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().Padding(4)[ClassBtn(TEXT("line_infantry"), TEXT("Line Infantry"))]
				+ SHorizontalBox::Slot().AutoWidth().Padding(4)[ClassBtn(TEXT("militia"), TEXT("Militia"))]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 12, 0, 4)
			[SNew(STextBlock).Font(Small).ColorAndOpacity(FLinearColor(0.9f, 0.9f, 0.7f)).Text(FText::FromString(TEXT("Spawn point")))]
			+ SVerticalBox::Slot().AutoHeight()[PostBox]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 16, 0, 0)
			[
				SNew(SButton)
				.OnClicked_Lambda([WeakThis]() {
					if (WeakThis.IsValid()) WeakThis->Confirm();
					return FReply::Handled();
				})
				[SNew(STextBlock).Font(Font).Text(FText::FromString(TEXT("Deploy")))]
			]
		];
}
