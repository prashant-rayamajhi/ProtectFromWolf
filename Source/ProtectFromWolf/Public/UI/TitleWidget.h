#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TitleWidget.generated.h"

//タイトルUIで参照する文字部品と操作ボタンの宣言
class UTextBlock;
class UButton;
class APlayerChara;

UCLASS()
//旧タイトル画面の見出しとゲーム開始ボタンを初期化するWidget
class PROTECTFROMWOLF_API UTitleWidget : public UUserWidget
{
	GENERATED_BODY()

  protected:
	//タイトル文字を設定し、ゲーム開始ボタンへクリックイベントを登録する関数
	void NativeConstruct() override;

	//タイトル画面のゲーム名を表示するTextBlock参照。
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock *m_title;

	//タイトル画面からゲーム開始処理を呼び出すButton参照。
	UPROPERTY(meta = (BindWidgetOptional))
	UButton *m_inGameButton;
};
