#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "Components/SlateWrapperTypes.h"
#include "SciFiScreenSubsystem.generated.h"

class USciFiScreenWidget;
class AMenuCharacterStage;
class UUserWidget;

UCLASS()
//タイトル、ロード、クリア、ゲームオーバー画面を切り替えるサブシステム
class PROTECTFROMWOLF_API USciFiScreenSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

  public:
	//タイトルの歩行演出を始め、二重のマップ移動を防ぐ関数
	void StartGameTransition();
	//開始したマップに対応するタイトル、ロード、クリア、ゲームオーバー画面を表示する関数
	virtual void OnWorldBeginPlay(UWorld &_world) override;
	//サブシステム終了時にロード監視タイマーと生成済み画面を解除する関数
	virtual void Deinitialize() override;

  private:
	//タイトル中に本編HUDを隠し、元の表示設定を保持する関数
	void HideHud();
	//本編を読み込み済みのままタイトルを表示している状態
	bool m_titleInCombat = false;
	//タイトルのカメラ移動が終わったら暗転を始める関数
	void CheckDeparture();
	//暗転後に本編へ移動する関数
	void OpenCombatLevel();
	//ロード済みの本編でTPSからFPSへのカメラ移動を始める関数
	bool BeginArrival();
	//演出中に隠したHUDを元の表示状態へ戻す関数
	void RestoreHud();
	//本編に溶け込むようにHUDを左から出しながら透明度を戻す関数
	void FadeHud();
	//HUDを表示し始めた本編内時刻
	double m_hudStartedAt = -1.0;
	//HUDの移動と透明度を滑らかに更新するタイマー
	FTimerHandle m_hudTimer;
	//演出前のHUDの透明度
	TMap<TWeakObjectPtr<UUserWidget>, float> m_hudOpacity;
	//演出前のHUDの描画位置
	TMap<TWeakObjectPtr<UUserWidget>, FVector2D> m_hudPosition;
	//タイトルの開始ボタンを一度だけ受け付ける状態
	bool m_departing = false;
	//本編でカメラ導入を再生する予約
	bool m_playArrival = false;
	//本編側のカメラ導入を開始した状態
	bool m_arrivalStarted = false;
	//導入終了時に戻すプレイヤーの被ダメージ設定
	bool m_canTakeDamage = true;
	//歩行完了の監視と暗転後の移動に使うタイマー
	FTimerHandle m_travelTimer;
	//演出前の各HUDの表示状態
	TMap<TWeakObjectPtr<UUserWidget>, ESlateVisibility> m_hiddenWidgets;
	//マップと一緒に破棄されるメニュー専用の撮影セット
	UPROPERTY(Transient)
	TObjectPtr<AMenuCharacterStage> m_menuStage;
	//旧GameModeが生成した画面を削除してSci-Fi画面との二重表示を防ぐ関数
	void RemoveLegacyScreenWidgets(UWorld &_world);
	//戦闘用アセットと敵の事前生成完了を確認してロード画面を閉じる関数
	void CheckCombatPreloadComplete();
	//ロード画面を閉じてプレイヤーの移動とゲーム入力を有効にする関数
	void FinishCombatPreload();

	//現在のマップで最前面へ表示している全画面UI
	UPROPERTY(Transient)
	TObjectPtr<USciFiScreenWidget> m_screenWidget;

	//戦闘空間の事前生成完了を定期確認するタイマー
	FTimerHandle m_loadingCheckTimer;
	//ロード画面を最低表示時間だけ維持するための開始時刻
	double m_loadingStartedAt = 0.0;
};
