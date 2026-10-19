#pragma once
#include "CoreMinimal.h"

//ジャンプの高さをセンチメートルで指定し、現在の重力に合う初速へ変換する
namespace JumpHeight
{
	//既存の低いジャンプは維持し、指定した頂点を超えない初速を求める関数
	inline float LimitSpeed(float _speed, float _gravity, float _height)
	{
		if (!FMath::IsFinite(_speed) || !FMath::IsFinite(_gravity) || !FMath::IsFinite(_height)) { return 0.f; }
		//v²=2ghから求め、無重力や上向き重力では上昇を開始しない
		const float speedLimit = FMath::Sqrt(2.f * FMath::Max(0.f, -_gravity) * FMath::Clamp(_height, 0.f, 200.f));
		return FMath::Min(FMath::Max(0.f, _speed), speedLimit);
	}
}
