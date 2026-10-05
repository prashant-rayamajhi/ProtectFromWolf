#pragma once

#include "CoreMinimal.h"

class AEnemyChara;

//同じ戦闘空間の味方と射撃位置を調整する共通判断
struct PROTECTFROMWOLF_API FEnemyTeamTactics
{
	//別の戦闘空間にいる敵を連携相手から除外する関数
	static bool SharesRoom(const AEnemyChara *_left, const AEnemyChara *_right);
	//味方の現在位置と予約済みの遮蔽物に重なる地点を除外する関数
	static bool IsOccupied(const AEnemyChara *_enemy, const FVector &_position, float _spacing);
	//味方と射線を分け、壁を越えずに到達できる射撃位置を選ぶ関数
	static bool FindFirePosition(AEnemyChara *_enemy, AActor *_target, FVector &_position, const FVector *_knownPosition = nullptr);
	//射撃可能な味方を残し、同時に移動する人数を半数までに抑える関数
	static bool CanRelocate(const AEnemyChara *_enemy);
	//攻撃開始前から接近役を二体に絞り、全員が相手の足元へ集まることを防ぐ関数
	static bool CanApproachMelee(const AEnemyChara *_enemy, const AActor *_target);
	//既知の相手の足元を横切る経路を除外し、接近されている時は外向きの退避を許可する関数
	static bool IsSafeCombatPath(const TArray<FVector> &_points, const FVector &_threat);
};
