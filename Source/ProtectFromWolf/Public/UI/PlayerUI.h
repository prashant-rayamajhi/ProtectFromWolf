

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "PlayerUI.generated.h"

//プレイヤーHUDで参照するクラスの事前宣言
class UProgressBar;
class UTextBlock;
class APlayerChara;
class FHudInputTracker;

UCLASS(Abstract)
//体力、変身ゲージ、弾薬、被弾・撃破通知を描画するHUD
class PROTECTFROMWOLF_API UPlayerUI : public UUserWidget
{
	GENERATED_BODY()
	//製品の表示状態を変更せず、命中通知の回帰テストから表示時間を確認する
	friend class FShotFeedbackTest;

  public:
	//開始演出で左右のHUDを表示する割合を設定する関数
	void SetIntroProgress(float _progress) { m_introProgress = FMath::Clamp(_progress, 0.f, 1.f); }
	//HUDが参照するプレイヤーと各ゲージの初期表示値を設定する関数
	void SetOwnerPlayer(APlayerChara *_player);

	//プレイヤーの現在体力を体力ゲージと被弾演出へ反映する関数
	void UpdateHealthUI();

	//マガジンと予備弾薬の残数を円形弾薬表示へ反映する関数
	void UpdateAmmoUI();

	//狼男への変身ゲージ残量をパワーゲージへ反映する関数
	void UpdateEnhanceGauge();
	//敵撃破時にクロスヘアーの赤い確認演出を開始する関数
	void NotifyEnemyKilled();
	//敵へのダメージ成立を知らせる白い命中マーカーを表示する関数
	void NotifyEnemyHit();

  protected:
	//クロスヘアとは独立して体力と弾薬の登場演出に使う割合
	float m_introProgress = 1.f;
	//HUD生成時に表示部品と各アニメーション値を初期化する関数
	void NativeConstruct() override;
	//画面を閉じる際に入力監視を解除する関数
	void NativeDestruct() override;
	//最新のゲーム状態をUI表示と補間アニメーションへ反映する関数
	void NativeTick(const FGeometry &_geometry, float _deltaTime) override;
	//クロスヘアーと各ゲージをSlate要素として描画する関数
	virtual int32 NativePaint(const FPaintArgs &_args, const FGeometry &_allottedGeometry, const FSlateRect &_cullingRect,
							  FSlateWindowElementList &_outDrawElements, int32 _layerId, const FWidgetStyle &_widgetStyle,
							  bool _parentEnabled) const override;

	//破棄済みプレイヤーへのアクセスを防ぎながらHUDの表示元を参照する変数
	TWeakObjectPtr<APlayerChara> m_ownerPlayer;

	//旧Blueprintとの互換性を維持する体力ProgressBar
	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar *m_healthBar;

	//旧Blueprintとの互換性を維持する現在体力TextBlock
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock *m_currentHealthLabel;

	//旧Blueprintとの互換性を維持する最大体力TextBlock
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock *m_maxHealthLabel;

	//旧Blueprintとの互換性を維持する変身ゲージProgressBar
	UPROPERTY(meta = (BindWidgetOptional))
	UProgressBar *m_enhanceGauge;

	//クロスヘアーの色と表示状態を制御するTextBlock
	UPROPERTY(BlueprintReadOnly, meta = (BindWidgetOptional))
	TObjectPtr<UTextBlock> m_reticle;

	//旧Blueprintとの互換性を維持するマガジン残弾TextBlock
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock *m_currentAmmo;

	//旧Blueprintとの互換性を維持する予備弾薬TextBlock
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock *m_remainingAmmo;

  private:
	//最後に操作した入力機器を判定する監視役
	TSharedPtr<FHudInputTracker> m_inputTracker;
	//体力表示の補間先となる実際の体力割合
	float m_targetHealthRatio = 1.f;
	//体力減少を滑らかに見せるため画面へ表示中の体力割合
	float m_displayedHealthRatio = 1.f;
	//受けたダメージ量を遅れて追従表示する体力割合
	float m_healthTrailRatio = 1.f;
	//体力ゲージを被弾時に点滅させる強度
	float m_damagePulse = 0.f;
	//変身ゲージ表示の補間先となる実際のゲージ割合
	float m_targetPowerRatio = 0.f;
	//変身ゲージ増減を滑らかに見せるため画面へ表示中の割合
	float m_displayedPowerRatio = 0.f;
	//変身可能になったことをパワーゲージで知らせる点滅強度
	float m_powerPulse = 0.f;
	//円形弾薬表示を滑らかに変化させるため画面へ表示中の残弾割合
	float m_displayedAmmoRatio = 1.f;
	//射撃とリロードによる残弾変化を円形表示で知らせる点滅強度
	float m_ammoPulse = 0.f;
	//弾薬点滅の開始判定に使用する前回表示時のマガジン残弾数
	int32 m_lastAmmo = INDEX_NONE;
	//被弾時に画面端へ表示する赤い警告の強度
	float m_damageScreenPulse = 0.f;
	//敵撃破時にクロスヘアーを赤く発光させる強度
	float m_killConfirmPulse = 0.f;
	//命中マーカーを短時間だけ表示するための残り秒数
	float m_hitConfirmTime = 0.f;
};
