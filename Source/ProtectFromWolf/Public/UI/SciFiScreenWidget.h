#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "SciFiScreenWidget.generated.h"

enum class ESciFiScreenMode : uint8
{
	Loading,
	Title,
	GameClear,
	GameOver
};

class SButton;

UCLASS()
//タイトル、ロード、ゲームクリア、ゲームオーバーの近未来画面をSlateで構築するWidget
class PROTECTFROMWOLF_API USciFiScreenWidget : public UUserWidget
{
	GENERATED_BODY()

  public:
	//操作パネルを閉じ、カメラ演出中の小さなロード表示へ切り替える関数
	void SetCinematic(bool _enabled) { m_cinematic = _enabled; }
	//背景のキャラクターを撮影できた場合に右側を透過する関数
	void SetSceneVisible(bool _visible) { m_hasScene = _visible; }
	//表示する画面をタイトル、ロード、クリア、ゲームオーバーから選ぶ関数
	void SetScreenMode(ESciFiScreenMode _mode)
	{
		m_mode = _mode;
	}
	//タイトル・リザルト画面を開いた直後にPrimaryボタンへゲームパッドフォーカスを設定する関数
	void FocusPrimaryAction();

  protected:
	//メニュー画面の背景と操作パネルを構築する関数
	virtual TSharedRef<SWidget> RebuildWidget() override;

  private:
	//開始演出中のボタン連打とメニュー表示を止める状態
	bool m_cinematic = false;
	//メインボタンからゲーム開始または次の画面遷移を実行する関数
	FReply HandlePrimaryAction();
	//サブボタンから再挑戦またはタイトル画面への遷移を実行する関数
	FReply HandleSecondaryAction();

	//表示する画面の種類
	ESciFiScreenMode m_mode = ESciFiScreenMode::Title;
	//専用カメラでキャラクターを表示できるかどうか
	bool m_hasScene = false;

	//ゲームパッドのAボタンとキーボードEnterで決定するPrimaryボタン
	TSharedPtr<SButton> m_primaryButton;
	//D-padまたは左スティックでPrimaryから移動するSecondaryボタン
	TSharedPtr<SButton> m_secondaryButton;
};
