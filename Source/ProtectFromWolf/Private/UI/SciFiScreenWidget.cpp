#include "UI/SciFiScreenWidget.h"

#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "UI/MenuBackdrop.h"
#include "UI/SciFiScreenSubsystem.h"
#include "Engine/World.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Styling/CoreStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Sound/SoundBase.h"
#include "Framework/Application/SlateApplication.h"

//画面表示直後の操作対象をPrimaryボタンへ固定し、ゲームパッドだけで決定可能にする関数
void USciFiScreenWidget::FocusPrimaryAction()
{
	if (m_cinematic) { return; }
	if (m_primaryButton.IsValid())
	{
		FSlateApplication::Get().SetKeyboardFocus(m_primaryButton, EFocusCause::SetDirectly);
	}
}

//近未来画面の背景、見出し、操作ボタン、進捗表示をSlate部品として構築する関数
TSharedRef<SWidget> USciFiScreenWidget::RebuildWidget()
{
	//画面の目的に合わせて、結果と次の操作を短く伝える
	const bool isLoading = m_mode == ESciFiScreenMode::Loading;
	const bool isTitle = m_mode == ESciFiScreenMode::Title;
	const bool isClear = m_mode == ESciFiScreenMode::GameClear;
	const FText heading = FText::FromString(isLoading ? TEXT("LOADING") : isTitle ? TEXT("PROTECT\nFROM WOLF")
		: isClear ? TEXT("MISSION COMPLETE") : TEXT("GAME OVER"));
	const FText message = FText::FromString(isLoading ? TEXT("Preparing the next battle.")
		: isTitle ? TEXT("Fight with your rifle.\nTransform when the battle turns against you.")
		: isClear ? TEXT("The last enemy has fallen.\nThank you for playing.")
		: TEXT("Take cover, reload, and try another approach."));
	const FText primary = FText::FromString(isTitle ? TEXT("START GAME") : isClear ? TEXT("BACK TO TITLE") : TEXT("TRY AGAIN"));
	const FText secondary = FText::FromString(isTitle ? TEXT("QUIT GAME") : isClear ? TEXT("PLAY AGAIN") : TEXT("BACK TO TITLE"));
	const FLinearColor accent = m_mode == ESciFiScreenMode::GameOver ? FLinearColor(0.85f, 0.39f, 0.28f)
		: FLinearColor(0.46f, 0.76f, 0.78f);

	return SNew(SOverlay)
		+ SOverlay::Slot()[SNew(SMenuBackdrop).ShowScene(m_hasScene && !isLoading)
			.Visibility_Lambda([this]() { return m_cinematic ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })]
		+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(40.f)
			[SNew(STextBlock).Text(FText::FromString(TEXT("LOADING")))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16)).ColorAndOpacity(FLinearColor(0.7f, 0.85f, 0.9f))
				.Visibility_Lambda([this, isLoading]() { return isLoading ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })]
		+ SOverlay::Slot()[SNew(SScaleBox).Stretch(EStretch::ScaleToFit)
			.Visibility_Lambda([this, isLoading]() { return m_cinematic || isLoading ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible; })
			[SNew(SBox).WidthOverride(1280.f).HeightOverride(720.f)
				[SNew(SOverlay)
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Center).Padding(72.f, 30.f)
						[SNew(SBox).WidthOverride(550.f)
							[SNew(SVerticalBox)
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 24.f)
									[SNew(STextBlock).Text(FText::FromString(TEXT("PROTECT FROM WOLF")))
										.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13)).ColorAndOpacity(accent)]
								+ SVerticalBox::Slot().AutoHeight()
									[SNew(STextBlock).Text(heading).Font(FCoreStyle::GetDefaultFontStyle("Bold", isTitle ? 58 : 38))
										.ColorAndOpacity(FLinearColor(0.91f, 0.94f, 0.95f))]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 22.f, 0.f, 35.f)
									[SNew(STextBlock).Text(message).Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
										.ColorAndOpacity(FLinearColor(0.58f, 0.67f, 0.7f)).WrapTextAt(510.f)]
								+ SVerticalBox::Slot().AutoHeight()
									[SNew(SProgressBar).Percent(TOptional<float>()).FillColorAndOpacity(accent)
										.Visibility(isLoading ? EVisibility::Visible : EVisibility::Collapsed)]
								+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 0.f, 0.f, 12.f)
									[SAssignNew(m_primaryButton, SButton)
										.Visibility(isLoading ? EVisibility::Collapsed : EVisibility::Visible)
										.OnClicked_UObject(this, &USciFiScreenWidget::HandlePrimaryAction)
										.ContentPadding(FMargin(24.f, 15.f)).ButtonColorAndOpacity(FLinearColor(0.13f, 0.28f, 0.31f))
										[SNew(STextBlock).Text(primary).Font(FCoreStyle::GetDefaultFontStyle("Bold", 20))
											.ColorAndOpacity(FLinearColor::White)]]
								+ SVerticalBox::Slot().AutoHeight()
									[SAssignNew(m_secondaryButton, SButton)
										.Visibility(isLoading ? EVisibility::Collapsed : EVisibility::Visible)
										.OnClicked_UObject(this, &USciFiScreenWidget::HandleSecondaryAction)
										.ContentPadding(FMargin(24.f, 12.f)).ButtonColorAndOpacity(FLinearColor(0.035f, 0.055f, 0.065f))
										[SNew(STextBlock).Text(secondary).Font(FCoreStyle::GetDefaultFontStyle("Regular", 17))
											.ColorAndOpacity(FLinearColor(0.7f, 0.77f, 0.8f))]]
							]]
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(72.f, 0.f, 0.f, 28.f)
						[SNew(STextBlock).Text(FText::FromString(TEXT("ENTER / GAMEPAD A   SELECT     UP / DOWN   NAVIGATE")))
							.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12)).ColorAndOpacity(FLinearColor(0.5f, 0.59f, 0.63f))
							.Visibility(isLoading ? EVisibility::Collapsed : EVisibility::Visible)]
				]]];
}

//主ボタンからゲーム開始または再挑戦を実行する関数
FReply USciFiScreenWidget::HandlePrimaryAction()
{
	if (m_cinematic) { return FReply::Handled(); }
	if (m_mode == ESciFiScreenMode::Title)
	{
		if (USciFiScreenSubsystem *screen = GetWorld()->GetSubsystem<USciFiScreenSubsystem>())
		{
			screen->StartGameTransition();
			return FReply::Handled();
		}
	}
	//設定されている主操作を一度だけ実行する
	if (USoundBase *confirm = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_UIConfirm.SFX_UIConfirm")))
	{
		UGameplayStatics::PlaySound2D(this, confirm, 0.45f);
	}

	//ゲームオーバー画面ではPrimaryボタンはリトライ用に有効化されているため、ゲームレベルを再度開く。
	const FName destination = m_mode == ESciFiScreenMode::GameClear ? FName(TEXT("GameTitle")) : FName(TEXT("MainLevel"));
	UGameplayStatics::OpenLevel(this, destination);
	return FReply::Handled();
}

//副ボタンから終了、再挑戦、タイトルへの復帰を実行する関数
FReply USciFiScreenWidget::HandleSecondaryAction()
{
	if (m_cinematic) { return FReply::Handled(); }
	//タイトルの終了ボタンでは、エンジンの終了処理に従ってゲームを閉じる
	if (m_mode == ESciFiScreenMode::Title)
	{
		UKismetSystemLibrary::QuitGame(this, GetOwningPlayer(), EQuitPreference::Quit, false);
		return FReply::Handled();
	}

	//クリア画面ではSecondaryボタンはリプレイ用に有効化されているため、ゲームレベルを再度開く。
	if (USoundBase *confirm = LoadObject<USoundBase>(nullptr, TEXT("/Game/Audio/SciFi/SelectedForUnreal/SFX_UIConfirm.SFX_UIConfirm")))
	{
		UGameplayStatics::PlaySound2D(this, confirm, 0.45f);
	}

	//ゲームオーバー画面ではSecondaryボタンはタイトル画面へ戻る用に有効化されているため、タイトルレベルを開く。
	const FName destination = m_mode == ESciFiScreenMode::GameClear ? FName(TEXT("MainLevel")) : FName(TEXT("GameTitle"));
	UGameplayStatics::OpenLevel(this, destination);
	return FReply::Handled();
}
