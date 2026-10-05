#pragma once

#include "CoreMinimal.h"

class AEnemyChara;

//瞬間移動先の床と体を置く空間を確認するクラス
struct FEnemyTeleportPlacement
{
	//歩ける経路上の候補を、カプセルの中心位置へ変換する関数
	static bool Resolve(AEnemyChara *_enemy, const FVector &_desired, FVector &_center);
	//中心と外周に床があり、体が壁や他の敵に重ならないか確認する関数
	static bool HasSupport(AEnemyChara *_enemy, const FVector &_center);
};
