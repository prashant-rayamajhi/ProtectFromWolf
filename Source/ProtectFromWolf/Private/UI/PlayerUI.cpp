#include "UI/PlayerUI.h"

#include "Combat/AmmoReload.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Player/PlayerChara.h"
#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Application/IInputProcessor.h"
#include "Input/Events.h"

//操作を遮らず、最後に使った機器だけを記録する入力監視役
class FHudInputTracker final : public IInputProcessor
{
public:
	//直近の操作がゲームパッドかを示す状態
	bool m_gamepad = false;
	//入力イベントだけで更新するため毎フレームの処理は不要
	void Tick(float _delta, FSlateApplication &_app, TSharedRef<ICursor> _cursor) override {}
	//ボタン操作から表示する入力機器を切り替える関数
	bool HandleKeyDownEvent(FSlateApplication &_app, const FKeyEvent &_event) override
	{
		m_gamepad = _event.GetKey().IsGamepadKey();
		return false;
	}
	//スティックの微小なずれを無視し、意図的な操作で切り替える関数
	bool HandleAnalogInputEvent(FSlateApplication &_app, const FAnalogInputEvent &_event) override
	{
		if (_event.GetKey().IsGamepadKey() && FMath::Abs(_event.GetAnalogValue()) > 0.25f) { m_gamepad = true; }
		return false;
	}
	//マウスの視点操作でキーボード表示へ戻す関数
	bool HandleMouseMoveEvent(FSlateApplication &_app, const FPointerEvent &_event) override
	{
		if (_event.GetCursorDelta().SizeSquared() > 1.f) { m_gamepad = false; }
		return false;
	}
	//クリックでキーボード表示へ戻す関数
	bool HandleMouseButtonDownEvent(FSlateApplication &_app, const FPointerEvent &_event) override
	{
		m_gamepad = false;
		return false;
	}
};

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHudInputTest, "ProtectFromWolf.UI.InputPrompt", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FHudInputTest::RunTest(const FString &_parameters)
{
	if (!FSlateApplication::IsInitialized()) { AddError(TEXT("Slate is required")); return false; }
	FHudInputTracker tracker;
	FSlateApplication &app = FSlateApplication::Get();
	TestFalse(TEXT("Keyboard default"), tracker.m_gamepad);
	TestFalse(TEXT("Input passes through"), tracker.HandleKeyDownEvent(app, FKeyEvent(EKeys::Gamepad_FaceButton_Right, {}, 0, false, 0, 0)));
	TestTrue(TEXT("Gamepad button"), tracker.m_gamepad);
	tracker.HandleKeyDownEvent(app, FKeyEvent(EKeys::W, {}, 0, false, 0, 0));
	TestFalse(TEXT("Keyboard restores ALT"), tracker.m_gamepad);
	tracker.HandleAnalogInputEvent(app, FAnalogInputEvent(EKeys::Gamepad_LeftX, {}, 0, false, 0, 0, 0.1f));
	TestFalse(TEXT("Ignore stick drift"), tracker.m_gamepad);
	tracker.HandleAnalogInputEvent(app, FAnalogInputEvent(EKeys::Gamepad_LeftX, {}, 0, false, 0, 0, 0.8f));
	TestTrue(TEXT("Stick selects gamepad"), tracker.m_gamepad);
	tracker.HandleMouseButtonDownEvent(app, FPointerEvent());
	TestFalse(TEXT("Mouse restores ALT"), tracker.m_gamepad);
	return true;
}
#endif

//HUDが参照するプレイヤーと各ゲージの初期表示値を設定する関数
void UPlayerUI::SetOwnerPlayer(APlayerChara *_player)
{
	//無効なプレイヤーをHUDの表示元に設定しない
	if (!_player) { return; }
	//初回表示でゲージが不要に補間されないように実際の値へ揃える
	m_ownerPlayer = _player;
	m_targetHealthRatio = _player->GetHealthRatio();
	m_displayedHealthRatio = m_targetHealthRatio;
	m_healthTrailRatio = m_targetHealthRatio;
	m_targetPowerRatio = _player->GetEnhanceGaugeRatio();
	m_displayedPowerRatio = m_targetPowerRatio;
}

//HUD生成時に旧ウィジェットを非表示にして現在の表示値を初期化する関数
void UPlayerUI::NativeConstruct()
{
	//基底Widgetの生成処理を実行する
	Super::NativeConstruct();
	//再表示時に重複登録せず、ゲーム操作を消費しない監視役を登録する
	if (FSlateApplication::IsInitialized() && !m_inputTracker)
	{
		m_inputTracker = MakeShared<FHudInputTracker>();
		FSlateApplication::Get().RegisterInputPreProcessor(m_inputTracker);
	}
	//Blueprint内の部品名を変えずに、表示に使う参照を取得する
	m_healthBar = Cast<UProgressBar>(GetWidgetFromName(TEXT("HealthBar")));
	m_currentHealthLabel = Cast<UTextBlock>(GetWidgetFromName(TEXT("CurrentHealthLabel")));
	m_maxHealthLabel = Cast<UTextBlock>(GetWidgetFromName(TEXT("MaxHealthLabel")));
	m_enhanceGauge = Cast<UProgressBar>(GetWidgetFromName(TEXT("EnhanceGauge")));
	m_reticle = Cast<UTextBlock>(GetWidgetFromName(TEXT("Reticle")));
	m_currentAmmo = Cast<UTextBlock>(GetWidgetFromName(TEXT("CurrentAmmo")));
	m_remainingAmmo = Cast<UTextBlock>(GetWidgetFromName(TEXT("RemainingAmmo")));
	//独自描画へ置き換えるクロスヘアーの旧文字表示を消す
	if (m_reticle)
	{
		m_reticle->SetColorAndOpacity(FSlateColor(FLinearColor(0.2f, 0.9f, 1.f, 0.95f)));
		m_reticle->SetText(FText::GetEmpty());
	}
	//Slateによる独自描画と重ならないように旧Blueprint部品を非表示にする
	if (m_healthBar) m_healthBar->SetRenderOpacity(0.f);
	//独自描画と重ならないように旧変身ゲージを非表示にする
	if (m_enhanceGauge) m_enhanceGauge->SetRenderOpacity(0.f);
	//独自描画と重ならないように旧現在体力表示を非表示にする
	if (m_currentHealthLabel) m_currentHealthLabel->SetRenderOpacity(0.f);
	//独自描画と重ならないように旧最大体力表示を非表示にする
	if (m_maxHealthLabel) m_maxHealthLabel->SetRenderOpacity(0.f);
	//独自描画と重ならないように旧マガジン残弾表示を非表示にする
	if (m_currentAmmo) m_currentAmmo->SetRenderOpacity(0.f);
	//独自描画と重ならないように旧予備弾薬表示を非表示にする
	if (m_remainingAmmo) m_remainingAmmo->SetRenderOpacity(0.f);
}

//画面遷移後に入力監視が残らないよう解除する関数
void UPlayerUI::NativeDestruct()
{
	if (FSlateApplication::IsInitialized() && m_inputTracker)
	{
		FSlateApplication::Get().UnregisterInputPreProcessor(m_inputTracker);
	}
	m_inputTracker.Reset();
	Super::NativeDestruct();
}

//最新のゲーム状態をHUD表示とアニメーションへ毎フレーム反映する関数
void UPlayerUI::NativeTick(const FGeometry &_geometry, float _deltaTime)
{
	//基底Widgetの毎フレーム処理を実行する
	Super::NativeTick(_geometry, _deltaTime);
	//旧ProgressBarを使わない画面でも、独自描画の体力を最新値へ近づける
	m_displayedHealthRatio = FMath::FInterpTo(m_displayedHealthRatio, m_targetHealthRatio, _deltaTime, 7.f);
	//旧体力バーを利用するBlueprintでも補間と危険色を反映する
	if (m_healthBar)
	{
		m_healthBar->SetPercent(m_displayedHealthRatio);

		//安全な体力を示すシアン色
		const FLinearColor safeColor(0.18f, 0.9f, 1.f, 1.f);
		//瀕死状態を示す赤色
		const FLinearColor dangerColor(1.f, 0.12f, 0.08f, 1.f);
		//体力35パーセント以下で増加する危険色の混合割合
		const float dangerAmount = 1.f - FMath::Clamp(m_displayedHealthRatio / 0.35f, 0.f, 1.f);
		m_healthBar->SetFillColorAndOpacity(FLinearColor::LerpUsingHSV(safeColor, dangerColor, dangerAmount));
		m_healthBar->SetRenderScale(FVector2D(1.f + m_damagePulse * 0.025f, 1.f + m_damagePulse * 0.12f));
	}
	//被弾、体力遅延、撃破確認の演出を時間経過で通常表示へ戻す
	m_healthTrailRatio = FMath::FInterpConstantTo(m_healthTrailRatio, m_targetHealthRatio, _deltaTime, 0.18f);
	m_damagePulse = FMath::FInterpTo(m_damagePulse, 0.f, _deltaTime, 8.f);
	m_damageScreenPulse = FMath::FInterpTo(m_damageScreenPulse, 0.f, _deltaTime, 5.2f);
	m_killConfirmPulse = FMath::FInterpTo(m_killConfirmPulse, 0.f, _deltaTime, 7.5f);
	m_hitConfirmTime = FMath::Max(0.f, m_hitConfirmTime - _deltaTime);

	//プレイヤーが存在する間だけ変身ゲージの増加と補間を更新する
	if (m_ownerPlayer.IsValid())
	{
		//プレイヤーから取得した最新の変身ゲージ割合
		const float newPowerRatio = m_ownerPlayer->GetEnhanceGaugeRatio();
		//変身ゲージが増加した瞬間に発光演出を開始する
		if (newPowerRatio > m_targetPowerRatio + KINDA_SMALL_NUMBER) m_powerPulse = 1.f;
		m_targetPowerRatio = newPowerRatio;
		m_displayedPowerRatio = FMath::FInterpTo(m_displayedPowerRatio, m_targetPowerRatio, _deltaTime, 5.5f);
		m_powerPulse = FMath::FInterpTo(m_powerPulse, 0.f, _deltaTime, 4.f);
	}

	//弾薬Componentが有効な間だけ円形弾薬表示を更新する
	if (m_ownerPlayer.IsValid() && m_ownerPlayer->m_ammoReloadComponent)
	{
		//残弾数と最大弾数を読み取る弾薬Component
		const UAmmoReload *ammo = m_ownerPlayer->m_ammoReloadComponent;
		//円形弾薬表示へ反映するマガジン残弾割合
		const float targetAmmoRatio = ammo->m_maxAmmo > 0 ? static_cast<float>(ammo->m_currentAmmo) / static_cast<float>(ammo->m_maxAmmo) : 0.f;
		m_displayedAmmoRatio = FMath::FInterpTo(m_displayedAmmoRatio, targetAmmoRatio, _deltaTime, 10.f);
		//前回表示から残弾数が変化した瞬間に点滅演出を開始する
		if (m_lastAmmo != INDEX_NONE && m_lastAmmo != ammo->m_currentAmmo) { m_ammoPulse = 1.f; }
		m_lastAmmo = ammo->m_currentAmmo;
		m_ammoPulse = FMath::FInterpTo(m_ammoPulse, 0.f, _deltaTime, 7.f);
		UpdateAmmoUI();
	}
}

//クロスヘアーと円形ゲージなどの動的HUD要素を描画する関数
int32 UPlayerUI::NativePaint(const FPaintArgs &_args, const FGeometry &_allottedGeometry, const FSlateRect &_cullingRect,
							 FSlateWindowElementList &_outDrawElements, int32 _layerId, const FWidgetStyle &_widgetStyle, bool _parentEnabled) const
{
	//基底Widgetが描画した後へ独自HUDを重ねる描画レイヤー
	int32 layer = Super::NativePaint(_args, _allottedGeometry, _cullingRect, _outDrawElements, _layerId, _widgetStyle, _parentEnabled);
	//表示元のプレイヤーまたは弾薬Componentが無効な場合は独自HUDを描画しない
	if (!m_ownerPlayer.IsValid() || !m_ownerPlayer->m_ammoReloadComponent) { return layer; }

	//解像度に合わせてHUD要素を配置するWidget領域
	const FVector2D size = _allottedGeometry.GetLocalSize();
	//HUDを配置できない小さな領域では独自描画を中止する
	if (size.X < 320.f || size.Y < 240.f) { return layer; }
	//矩形と線の描画へ使用するEngine標準の白色Brush
	const FSlateBrush *whiteBrush = FCoreStyle::Get().GetBrush("WhiteBrush");

	//体力35パーセント以下で強くなる瀕死警告
	const float lowHealthWarning = FMath::Clamp((0.35f - m_targetHealthRatio) / 0.35f, 0.f, 1.f) * 0.42f;
	//直近の被弾と瀕死状態のうち強い方を採用する画面警告強度
	const float damageFeedback = FMath::Max(m_damageScreenPulse, lowHealthWarning);
	//警告強度が残っている間だけ画面端へ被弾表現を描画する
	if (damageFeedback > 0.01f)
	{
		//画面全体へ薄く重ねる被弾色
		const FLinearColor redWash(0.55f, 0.005f, 0.008f, 0.025f + damageFeedback * 0.1f);
		//画面端へ強く表示する被弾色
		const FLinearColor redEdge(1.f, 0.015f, 0.01f, 0.12f + damageFeedback * 0.42f);
		FSlateDrawElement::MakeBox(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(size, FSlateLayoutTransform(FVector2D::ZeroVector)),
								   whiteBrush, ESlateDrawEffect::None, redWash);
		//被弾強度に合わせて拡大する画面端の警告幅
		const float edgeThickness = 18.f + damageFeedback * 34.f;
		FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
								   _allottedGeometry.ToPaintGeometry(FVector2D(size.X, edgeThickness), FSlateLayoutTransform(FVector2D::ZeroVector)),
								   whiteBrush, ESlateDrawEffect::None, redEdge);
		FSlateDrawElement::MakeBox(
			_outDrawElements, ++layer,
			_allottedGeometry.ToPaintGeometry(FVector2D(size.X, edgeThickness), FSlateLayoutTransform(FVector2D(0.f, size.Y - edgeThickness))),
			whiteBrush, ESlateDrawEffect::None, redEdge);
		FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
								   _allottedGeometry.ToPaintGeometry(FVector2D(edgeThickness, size.Y), FSlateLayoutTransform(FVector2D::ZeroVector)),
								   whiteBrush, ESlateDrawEffect::None, redEdge);
		FSlateDrawElement::MakeBox(
			_outDrawElements, ++layer,
			_allottedGeometry.ToPaintGeometry(FVector2D(edgeThickness, size.Y), FSlateLayoutTransform(FVector2D(size.X - edgeThickness, 0.f))),
			whiteBrush, ESlateDrawEffect::None, redEdge);
		//被弾中の走査線へ使用する薄い赤色
		const FLinearColor scanColor(1.f, 0.03f, 0.02f, damageFeedback * 0.08f);
		//被弾中の画面全体へ一定間隔の走査線を描画する
		for (float y = 8.f; y < size.Y; y += 28.f)
		{
			FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
									   _allottedGeometry.ToPaintGeometry(FVector2D(size.X, 1.f), FSlateLayoutTransform(FVector2D(0.f, y))),
									   whiteBrush, ESlateDrawEffect::None, scanColor);
		}
	}

	//クロスヘアーを画面中央へ配置する基準位置
	//照準は左右へ動かさず、導入完了時に画面中央へ表示する
	if (m_introProgress >= 1.f)
	{
	const FVector2D reticleCenter = size * 0.5f;
	//通常命中は白い四本の斜線、撃破は既存の赤い表示を優先する
	if (m_hitConfirmTime > 0.f && m_killConfirmPulse < 0.1f)
	{
		const float opacity = FMath::Min(1.f, m_hitConfirmTime / 0.08f);
		for (FVector2D direction : {FVector2D(-1.f, -1.f), FVector2D(1.f, -1.f), FVector2D(-1.f, 1.f), FVector2D(1.f, 1.f)})
		{
			const TArray<FVector2D> points{reticleCenter + direction * 7.f, reticleCenter + direction * 13.f};
			FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), points,
				ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, opacity), true, 4.f);
			FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), points,
				ESlateDrawEffect::None, FLinearColor(1.f, 1.f, 1.f, opacity), true, 2.f);
		}
	}
	//エイム状態に合わせてクロスヘアーの間隔を切り替えるための状態
	const bool ads = m_ownerPlayer->IsAiming();
	//クロスヘアー中心から各線までの間隔
	const float reticleGap = ads ? 5.f : 8.f;
	//クロスヘアーを構成する各線の長さ
	const float reticleArm = ads ? 11.f : 15.f;
	//通常時のクロスヘアー外縁色
	const FLinearColor normalOutline(0.002f, 0.012f, 0.018f, 0.94f);
	//通常時のクロスヘアー発光色
	const FLinearColor normalGlow(0.76f, 1.f, 0.56f, 1.f);
	//敵撃破時のクロスヘアー外縁色
	const FLinearColor killOutline(0.22f, 0.002f, 0.004f, 0.98f);
	//敵撃破時のクロスヘアー発光色
	const FLinearColor killGlow(1.f, 0.025f, 0.015f, 1.f);
	//撃破演出の強度で通常色から赤色へ変化する外縁色
	const FLinearColor reticleOutline = FLinearColor::LerpUsingHSV(normalOutline, killOutline, m_killConfirmPulse);
	//撃破演出の強度で通常色から赤色へ変化する発光色
	const FLinearColor reticleGlow = FLinearColor::LerpUsingHSV(normalGlow, killGlow, m_killConfirmPulse);
	//明暗のある背景でも見失わない二重線を描画する処理
	auto drawReticleLine = [&](const FVector2D &_start, const FVector2D &_end)
	{
		//クロスヘアーの一辺を構成する始点と終点
		const TArray<FVector2D> points{_start, _end};
		FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), points, ESlateDrawEffect::None, reticleOutline,
									 true, 5.f);
		FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), points, ESlateDrawEffect::None, reticleGlow,
									 true, 2.8f);
	};
	drawReticleLine(reticleCenter + FVector2D(-reticleGap - reticleArm, 0.f), reticleCenter + FVector2D(-reticleGap, 0.f));
	drawReticleLine(reticleCenter + FVector2D(reticleGap, 0.f), reticleCenter + FVector2D(reticleGap + reticleArm, 0.f));
	drawReticleLine(reticleCenter + FVector2D(0.f, -reticleGap - reticleArm), reticleCenter + FVector2D(0.f, -reticleGap));
	drawReticleLine(reticleCenter + FVector2D(0.f, reticleGap), reticleCenter + FVector2D(0.f, reticleGap + reticleArm));
	//クロスヘアー中央の菱形を構成する頂点
	const TArray<FVector2D> centerDiamond{reticleCenter + FVector2D(0.f, -3.f), reticleCenter + FVector2D(3.f, 0.f),
										  reticleCenter + FVector2D(0.f, 3.f), reticleCenter + FVector2D(-3.f, 0.f),
										  reticleCenter + FVector2D(0.f, -3.f)};
	FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), centerDiamond, ESlateDrawEffect::None,
								 reticleOutline, true, 5.f);
	FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), centerDiamond, ESlateDrawEffect::None, reticleGlow,
								 true, 2.f);
	//クロスヘアー中央点の描画へ使用する白色Brush
	const FSlateBrush *reticleBrush = FCoreStyle::Get().GetBrush("WhiteBrush");
	FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
							   _allottedGeometry.ToPaintGeometry(FVector2D(7.f, 7.f), FSlateLayoutTransform(reticleCenter - FVector2D(3.5f, 3.5f))),
							   reticleBrush, ESlateDrawEffect::None, reticleOutline);
	FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
							   _allottedGeometry.ToPaintGeometry(FVector2D(3.f, 3.f), FSlateLayoutTransform(reticleCenter - FVector2D(1.5f, 1.5f))),
							   reticleBrush, ESlateDrawEffect::None, reticleGlow);
	//敵撃破直後だけクロスヘアー周囲へ赤い確認マーカーを描画する
	if (m_killConfirmPulse > 0.02f)
	{
		//撃破演出の減衰に合わせて外側へ広がる確認マーカー半径
		const float confirmRadius = 15.f + (1.f - m_killConfirmPulse) * 12.f;
		//敵撃破確認マーカーの菱形を構成する頂点
		const TArray<FVector2D> killDiamond{reticleCenter + FVector2D(0.f, -confirmRadius), reticleCenter + FVector2D(confirmRadius, 0.f),
											reticleCenter + FVector2D(0.f, confirmRadius), reticleCenter + FVector2D(-confirmRadius, 0.f),
											reticleCenter + FVector2D(0.f, -confirmRadius)};
		FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), killDiamond, ESlateDrawEffect::None,
									 FLinearColor(1.f, 0.02f, 0.01f, m_killConfirmPulse), true, 2.f + m_killConfirmPulse * 2.f);
	}

	//円形残弾表示の数値を取得する弾薬Component
	}
	//左右の情報だけを薄く表示し、照準の透明度には影響させない
	const auto fade = [this](FLinearColor _color) { _color.A *= m_introProgress; return _color; };
	const UAmmoReload *ammo = m_ownerPlayer->m_ammoReloadComponent;
	//画面右下へ配置する円形残弾表示の中心
	const FVector2D center(size.X - 118.f + 100.f * (1.f - m_introProgress), size.Y - 118.f);
	//射撃時の点滅でわずかに拡大する円形残弾表示の半径
	const float radius = 62.f + m_ammoPulse * 4.f;
	//残弾割合を表現する円周上の分割数
	const int32 segmentCount = 36;
	//現在の残弾割合に対応する点灯Segment数
	const int32 activeSegments = FMath::RoundToInt(FMath::Clamp(m_displayedAmmoRatio, 0.f, 1.f) * segmentCount);
	//通常時の弾薬表示へ使用するシアン色
	const FLinearColor cyan(0.05f, 0.9f, 1.f, 0.95f * m_introProgress);
	//残弾低下を知らせる警告色
	const FLinearColor warning(1.f, 0.18f, 0.08f, 0.98f * m_introProgress);
	//残弾数に応じてシアンと警告色を切り替える点灯色
	const FLinearColor activeColor = ammo->m_currentAmmo <= FMath::Max(2, ammo->m_maxAmmo / 4) ? warning : cyan;
	//消灯中の弾薬Segmentへ使用する暗色
	const FLinearColor inactive(0.025f, 0.12f, 0.17f, 0.78f * m_introProgress);

	//HUDの項目名へ使用するFont
	const FSlateFontInfo hudLabelFont = FCoreStyle::GetDefaultFontStyle("Bold", 11);
	//HUDの数値へ使用するFont
	const FSlateFontInfo hudValueFont = FCoreStyle::GetDefaultFontStyle("Bold", 16);
	//補助線と武器Iconへ使用する暗いシアン色
	const FLinearColor cyanDim(0.07f, 0.38f, 0.48f, 0.68f * m_introProgress);
	//変身ゲージと狼の爪Iconへ使用する緑色
	const FLinearColor mint(0.08f, 1.f, 0.68f, 0.95f * m_introProgress);
	//体力と変身ゲージを載せる半透明Panel色
	const FLinearColor panel(0.003f, 0.025f, 0.045f, 0.96f * m_introProgress);

	//画面左下へ配置する状態Panelの開始位置
	const FVector2D statusOrigin(42.f - 100.f * (1.f - m_introProgress), size.Y - 154.f);
	//体力と変身ゲージを収める状態Panelの大きさ
	const FVector2D statusSize(510.f, 118.f);
	FSlateDrawElement::MakeBox(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(statusSize, FSlateLayoutTransform(statusOrigin)),
							   whiteBrush, ESlateDrawEffect::None, panel);
	FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
							   _allottedGeometry.ToPaintGeometry(FVector2D(4.f, statusSize.Y), FSlateLayoutTransform(statusOrigin)), whiteBrush,
							   ESlateDrawEffect::None, fade(FLinearColor(cyan.R, cyan.G, cyan.B, 0.9f)));

	//体力と変身ゲージを同じ分割Bar表現で描画する処理
	auto drawSegmentedBar = [&](const FVector2D &_origin, float _ratio, float _trailRatio, const FLinearColor &_fillColor, int32 _segments, float _pulse)
	{
		//分割Bar全体の描画領域
		const FVector2D barSize(412.f, 18.f);
		FSlateDrawElement::MakeBox(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(barSize, FSlateLayoutTransform(_origin)), whiteBrush,
								   ESlateDrawEffect::None, fade(FLinearColor(0.01f, 0.08f, 0.12f, 0.72f)));
		//Barの範囲外へ描画しないように制限した遅延表示割合
		const float clampedTrail = FMath::Clamp(_trailRatio, 0.f, 1.f);
		//実際の体力より遅延表示が多い範囲へ被弾差分を描画する
		if (clampedTrail > _ratio)
		{
			FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
									   _allottedGeometry.ToPaintGeometry(FVector2D(barSize.X * clampedTrail, barSize.Y - 4.f),
																		 FSlateLayoutTransform(_origin + FVector2D(0.f, 2.f))),
									   whiteBrush, ESlateDrawEffect::None, fade(FLinearColor(1.f, 0.12f, 0.06f, 0.72f)));
		}
		//各Segmentの間に設ける隙間
		const float gap = 3.f;
		//指定された分割数から算出する一つのSegment幅
		const float segmentWidth = (barSize.X - gap * (_segments - 1)) / _segments;
		//現在割合に対応する点灯Segment数
		const int32 active = FMath::CeilToInt(FMath::Clamp(_ratio, 0.f, 1.f) * _segments);
		//割合に合わせて各Segmentの点灯状態を描き分ける
		for (int32 index = 0; index < _segments; ++index)
		{
			//点灯状態とPulseを反映したSegment色
			const FLinearColor color = index < active ? FLinearColor(_fillColor.R, _fillColor.G, _fillColor.B, 0.82f + _pulse * 0.18f)
													  : FLinearColor(0.035f, 0.15f, 0.19f, 0.46f);
			FSlateDrawElement::MakeBox(
				_outDrawElements, ++layer,
				_allottedGeometry.ToPaintGeometry(FVector2D(segmentWidth, barSize.Y - 6.f),
												  FSlateLayoutTransform(_origin + FVector2D(index * (segmentWidth + gap), 3.f))),
				whiteBrush, ESlateDrawEffect::None, fade(color));
		}
	};

	//体力30パーセント未満で警告色へ切り替える体力Bar色
	const FLinearColor healthColor = m_displayedHealthRatio < 0.3f ? fade(FLinearColor(1.f, 0.12f, 0.07f, 0.98f)) : cyan;
	FSlateDrawElement::MakeText(
		_outDrawElements, ++layer,
		_allottedGeometry.ToPaintGeometry(FVector2D(120.f, 18.f), FSlateLayoutTransform(statusOrigin + FVector2D(20.f, 12.f))), TEXT("HEALTH"),
		hudLabelFont, ESlateDrawEffect::None, healthColor);
	//現在体力と最大体力を三桁で示す表示文字列
	const FString healthValue = FString::Printf(TEXT("%03d / %03d"), m_ownerPlayer->GetHP(), m_ownerPlayer->GetMaxHP());
	FSlateDrawElement::MakeText(
		_outDrawElements, ++layer,
		_allottedGeometry.ToPaintGeometry(FVector2D(120.f, 22.f), FSlateLayoutTransform(statusOrigin + FVector2D(385.f, 7.f))), healthValue,
		hudValueFont, ESlateDrawEffect::None, fade(FLinearColor::White));
	drawSegmentedBar(statusOrigin + FVector2D(20.f, 35.f), m_displayedHealthRatio, m_healthTrailRatio, healthColor, 24, m_damagePulse);
	FSlateDrawElement::MakeText(
		_outDrawElements, ++layer,
		_allottedGeometry.ToPaintGeometry(FVector2D(130.f, 18.f), FSlateLayoutTransform(statusOrigin + FVector2D(20.f, 70.f))), TEXT("TRANSFORM"),
		hudLabelFont, ESlateDrawEffect::None, mint);
	//変身ゲージ割合をパーセントで示す表示文字列
	const FString powerValue = FString::Printf(TEXT("%d%%"), FMath::RoundToInt(m_displayedPowerRatio * 100.f));
	FSlateDrawElement::MakeText(
		_outDrawElements, ++layer,
		_allottedGeometry.ToPaintGeometry(FVector2D(120.f, 18.f), FSlateLayoutTransform(statusOrigin + FVector2D(395.f, 68.f))), powerValue,
		hudLabelFont, ESlateDrawEffect::None, mint);
	drawSegmentedBar(statusOrigin + FVector2D(20.f, 92.f), m_displayedPowerRatio, m_displayedPowerRatio, mint, 16, m_powerPulse);

	//現在形態の武器Iconを描画する開始位置
	const FVector2D iconOrigin = statusOrigin + FVector2D(8.f, -54.f);
	//銃と狼の爪のIconを切り替える現在の変身状態
	const bool isWerewolf = m_ownerPlayer->IsWerewolf();
	//狼男形態では三本の爪を武器Iconとして描画する
	if (isWerewolf)
	{
		//三本の爪を間隔を空けて描画する
		for (int32 claw = 0; claw < 3; ++claw)
		{
			//一本の爪を構成する折れ線の頂点
			TArray<FVector2D> clawLine{iconOrigin + FVector2D(12.f + claw * 24.f, 36.f), iconOrigin + FVector2D(32.f + claw * 24.f, 8.f),
									   iconOrigin + FVector2D(40.f + claw * 24.f, 2.f)};
			FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), clawLine, ESlateDrawEffect::None, mint, true,
										 4.f);
		}
	}
	else
	{
		//通常形態のRifle上面を構成する折れ線の頂点
		TArray<FVector2D> rifleLine{iconOrigin + FVector2D(4.f, 24.f), iconOrigin + FVector2D(35.f, 17.f), iconOrigin + FVector2D(84.f, 17.f),
									iconOrigin + FVector2D(101.f, 13.f), iconOrigin + FVector2D(134.f, 13.f)};
		FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), rifleLine, ESlateDrawEffect::None, cyan, true,
									 5.f);
		//通常形態のRifleGripを構成する折れ線の頂点
		TArray<FVector2D> rifleGrip{iconOrigin + FVector2D(62.f, 20.f), iconOrigin + FVector2D(56.f, 38.f), iconOrigin + FVector2D(69.f, 38.f),
									iconOrigin + FVector2D(76.f, 20.f)};
		FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), rifleGrip, ESlateDrawEffect::None, cyanDim, true,
									 4.f);
	}
	FSlateDrawElement::MakeText(_outDrawElements, ++layer,
								_allottedGeometry.ToPaintGeometry(FVector2D(180.f, 18.f), FSlateLayoutTransform(iconOrigin + FVector2D(153.f, 10.f))),
								isWerewolf ? TEXT("CLAWS") : TEXT("RIFLE"), hudLabelFont, ESlateDrawEffect::None,
								isWerewolf ? mint : cyan);

	FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
							   _allottedGeometry.ToPaintGeometry(FVector2D(210.f, 154.f), FSlateLayoutTransform(center - FVector2D(105.f, 77.f))),
							   whiteBrush, ESlateDrawEffect::None, fade(FLinearColor(0.005f, 0.025f, 0.045f, 0.28f)));

	//残弾割合に合わせて円周上のSegmentを順番に描画する
	for (int32 index = 0; index < segmentCount; ++index)
	{
		//現在Segmentの開始角度
		const float startAngle = FMath::DegreesToRadians(-90.f + index * (360.f / segmentCount));
		//Segment間の隙間を含めた終了角度
		const float endAngle = FMath::DegreesToRadians(-90.f + (index + 0.68f) * (360.f / segmentCount));
		//円周上の一つのSegmentを構成する始点と終点
		TArray<FVector2D> points;
		points.Add(center + FVector2D(FMath::Cos(startAngle), FMath::Sin(startAngle)) * radius);
		points.Add(center + FVector2D(FMath::Cos(endAngle), FMath::Sin(endAngle)) * radius);
		FSlateDrawElement::MakeLines(_outDrawElements, ++layer, _allottedGeometry.ToPaintGeometry(), points, ESlateDrawEffect::None,
									 index < activeSegments ? activeColor : inactive, true, 6.f);
	}

	//マガジン残弾数を二桁で示す表示文字列
	const FString magazineText = FString::Printf(TEXT("%02d"), ammo->m_currentAmmo);
	//予備弾薬数を三桁で示す表示文字列
	const FString reserveText = FString::Printf(TEXT("RESERVE  %03d"), ammo->m_reserveAmmo);
	//マガジン残弾数へ使用するFont
	const FSlateFontInfo numberFont = FCoreStyle::GetDefaultFontStyle("Bold", 30);
	//弾薬項目名と予備弾薬数へ使用するFont
	const FSlateFontInfo labelFont = FCoreStyle::GetDefaultFontStyle("Regular", 11);
	FSlateDrawElement::MakeText(_outDrawElements, ++layer,
								_allottedGeometry.ToPaintGeometry(FVector2D(64.f, 38.f), FSlateLayoutTransform(center - FVector2D(27.f, 25.f))),
								magazineText, numberFont, ESlateDrawEffect::None, activeColor);
	FSlateDrawElement::MakeText(_outDrawElements, ++layer,
								_allottedGeometry.ToPaintGeometry(FVector2D(110.f, 18.f), FSlateLayoutTransform(center + FVector2D(-50.f, 72.f))),
								reserveText, labelFont, ESlateDrawEffect::None, cyan);
	FSlateDrawElement::MakeText(_outDrawElements, ++layer,
								_allottedGeometry.ToPaintGeometry(FVector2D(60.f, 16.f), FSlateLayoutTransform(center + FVector2D(-24.f, 18.f))),
								TEXT("MAG"), labelFont, ESlateDrawEffect::None, fade(FLinearColor(0.45f, 0.78f, 0.86f, 0.9f)));

	//照準と敵の姿を隠さないよう、回避受付を画面下寄りの小さな操作カードで知らせる
	if (m_ownerPlayer->IsPerfectDodgePromptVisible())
	{
		//受付開始から終了までの残り時間割合
		const float evadeRatio = FMath::Clamp(m_ownerPlayer->GetPerfectDodgeWindowRatio(), 0.f, 1.f);
		//ジャスト回避案内の中心位置
		const FVector2D promptCenter(size.X * 0.5f, size.Y * 0.73f);
		//ジャスト回避案内Panelの左上位置
		const FVector2D promptOrigin = promptCenter - FVector2D(112.f, 24.f);
		//受付時間の経過に合わせて赤から緑へ変化する案内色
		const FLinearColor evadeColor =
			FLinearColor::LerpUsingHSV(FLinearColor(1.f, 0.12f, 0.05f, 1.f), FLinearColor(0.1f, 1.f, 0.76f, 1.f), evadeRatio);
		FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
								   _allottedGeometry.ToPaintGeometry(FVector2D(224.f, 48.f), FSlateLayoutTransform(promptOrigin)), whiteBrush,
								   ESlateDrawEffect::None, FLinearColor(0.002f, 0.015f, 0.03f, 0.65f));
		FSlateDrawElement::MakeBox(
			_outDrawElements, ++layer,
			_allottedGeometry.ToPaintGeometry(FVector2D(224.f * evadeRatio, 3.f), FSlateLayoutTransform(promptOrigin + FVector2D(0.f, 45.f))),
			whiteBrush, ESlateDrawEffect::None, evadeColor);
		//ジャスト回避案内の見出しへ使用するFont
		const FSlateFontInfo evadeTitleFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
		//ジャスト回避の入力表示へ使用するFont
		const FSlateFontInfo evadeKeyFont = FCoreStyle::GetDefaultFontStyle("Bold", 12);
		//最後に操作した機器のボタンだけを表示する
		const bool gamepad = m_inputTracker && m_inputTracker->m_gamepad;
		{
			//ボタンを見分けやすく並べるための左上座標
			const FVector2D keyOrigin = promptOrigin + FVector2D(38.f, 9.f);
			FSlateDrawElement::MakeBox(_outDrawElements, ++layer,
				_allottedGeometry.ToPaintGeometry(FVector2D(40.f, 26.f), FSlateLayoutTransform(keyOrigin)),
				whiteBrush, ESlateDrawEffect::None, FLinearColor(0.15f, 0.28f, 0.32f, 0.9f));
			FSlateDrawElement::MakeText(_outDrawElements, ++layer,
				_allottedGeometry.ToPaintGeometry(FVector2D(34.f, 20.f), FSlateLayoutTransform(keyOrigin + FVector2D(gamepad ? 14.f : 5.f, 4.f))),
				gamepad ? TEXT("B") : TEXT("ALT"), evadeKeyFont, ESlateDrawEffect::None, FLinearColor::White);
		}
		FSlateDrawElement::MakeText(
			_outDrawElements, ++layer,
			_allottedGeometry.ToPaintGeometry(FVector2D(96.f, 24.f), FSlateLayoutTransform(promptOrigin + FVector2D(116.f, 14.f))),
			TEXT("EVADE"), evadeTitleFont, ESlateDrawEffect::None, evadeColor);
	}
	return layer;
}

//プレイヤーの現在体力を体力ゲージと被弾演出へ反映する関数
void UPlayerUI::UpdateHealthUI()
{
	//表示元または旧体力Barが無効な場合は更新を中止する
	if (!m_ownerPlayer.IsValid() || !m_healthBar) { return; }
	//プレイヤーから取得して描画範囲内へ制限した最新の体力割合
	const float newHealthRatio = FMath::Clamp(m_ownerPlayer->GetHealthRatio(), 0.f, 1.f);
	//体力が減少した瞬間だけ被弾演出を開始する
	if (newHealthRatio < m_targetHealthRatio)
	{
		//被弾演出の強さへ反映する今回の体力減少割合
		const float healthLost = m_targetHealthRatio - newHealthRatio;
		m_damagePulse = 1.f;
		m_damageScreenPulse = FMath::Clamp(FMath::Max(m_damageScreenPulse, 0.68f + healthLost * 2.2f), 0.f, 1.f);
	}
	m_targetHealthRatio = newHealthRatio;

	//旧Blueprintの現在体力表示が存在する場合は最新値へ更新する
	if (m_currentHealthLabel) { m_currentHealthLabel->SetText(FText::AsNumber(FMath::Max(0, m_ownerPlayer->GetHP()))); }
	//旧Blueprintの最大体力表示が存在する場合は最新値へ更新する
	if (m_maxHealthLabel) { m_maxHealthLabel->SetText(FText::AsNumber(m_ownerPlayer->GetMaxHP())); }
}

//敵撃破時にクロスヘアーの赤い確認演出を開始する関数
void UPlayerUI::NotifyEnemyKilled()
{
	//次回描画からクロスヘアーの撃破確認色を最大強度で開始する
	m_killConfirmPulse = 1.f;
	Invalidate(EInvalidateWidgetReason::Paint);
}

//ダメージが成立した命中だけを照準の近くへ短く表示する関数
void UPlayerUI::NotifyEnemyHit()
{
	m_hitConfirmTime = 0.22f;
	Invalidate(EInvalidateWidgetReason::Paint);
}

//マガジンと予備弾薬の残数を円形弾薬表示へ反映する関数
void UPlayerUI::UpdateAmmoUI()
{
	//表示元または弾薬Componentが無効な場合は弾薬表示を更新しない
	if (!m_ownerPlayer.IsValid() || !m_ownerPlayer->m_ammoReloadComponent) { return; }
	//旧Blueprintのマガジン残弾表示が存在する場合は最新値へ更新する
	if (m_currentAmmo) { m_currentAmmo->SetText(FText::AsNumber(m_ownerPlayer->m_ammoReloadComponent->m_currentAmmo)); }
	//旧Blueprintの予備弾薬表示が存在する場合は最新値へ更新する
	if (m_remainingAmmo) { m_remainingAmmo->SetText(FText::AsNumber(m_ownerPlayer->m_ammoReloadComponent->m_reserveAmmo)); }
}

//狼男への変身ゲージ残量をパワーゲージへ反映する関数
void UPlayerUI::UpdateEnhanceGauge()
{
	//表示元または旧変身ゲージが無効な場合は表示を更新しない
	if (!m_ownerPlayer.IsValid() || !m_enhanceGauge) { return; }
	//旧Blueprintの変身ゲージへプレイヤーの最新割合を反映する
	m_enhanceGauge->SetPercent(m_ownerPlayer->GetEnhanceGaugeRatio());
}
