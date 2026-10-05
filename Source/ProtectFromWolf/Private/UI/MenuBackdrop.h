#pragma once

#include "Rendering/DrawElements.h"
#include "Styling/CoreStyle.h"
#include "Widgets/SLeafWidget.h"

//右側のキャラクターを隠さず、左側のメニューだけを暗くする背景
class SMenuBackdrop : public SLeafWidget
{
public:
	SLATE_BEGIN_ARGS(SMenuBackdrop) : _ShowScene(false) {}
		SLATE_ARGUMENT(bool, ShowScene)
	SLATE_END_ARGS()

	//ロード画面では全面を覆い、メニュー画面ではキャラクターを見せる関数
	void Construct(const FArguments &_args)
	{
		m_showScene = _args._ShowScene;
	}
	virtual FVector2D ComputeDesiredSize(float _scale) const override { return FVector2D(1280.f, 720.f); }

	//メニューと立体背景の境目をなだらかな暗幕でつなぐ関数
	virtual int32 OnPaint(const FPaintArgs &_args, const FGeometry &_geometry, const FSlateRect &_clip,
		FSlateWindowElementList &_elements, int32 _layer, const FWidgetStyle &_style, bool _enabled) const override
	{
		const FVector2D size = _geometry.GetLocalSize();
		TArray<FSlateGradientStop> stops;
		//一枚のグラデーションとして描き、帯の境界や重なりを作らない
		for (int32 index = 0; index <= 20; ++index)
		{
			const float ratio = static_cast<float>(index) / 20.f;
			const float fade = FMath::Clamp((ratio - 0.36f) / 0.25f, 0.f, 1.f);
			const float alpha = m_showScene ? FMath::Lerp(0.98f, 0.06f, fade * fade * (3.f - 2.f * fade)) : 1.f;
			stops.Emplace(FVector2D(size.X * ratio, 0.f), FLinearColor(0.009f, 0.017f, 0.027f, alpha));
		}
		FSlateDrawElement::MakeGradient(_elements, _layer, _geometry.ToPaintGeometry(), stops, Orient_Vertical);
		return _layer;
	}

private:
	//表示専用キャラクターが準備できたときだけ背景を透過する
	bool m_showScene = false;
};
