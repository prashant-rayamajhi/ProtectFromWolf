#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Enemy/EnemyData.h"
#include "EnemyCombatMemoryComponent.generated.h"

class APlayerChara;

UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
//プレイヤーの行動傾向を蓄積してボス判断へ提供する部品
class PROTECTFROMWOLF_API UEnemyCombatMemoryComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//プレイヤー行動の観測回数と戦闘記憶を初期化する関数
	UEnemyCombatMemoryComponent();

	//プレイヤーの行動傾向を計測し、AIの戦闘記憶へ反映する関数
	void ObservePlayer(APlayerChara *_player, float _targetDistance, bool _canSeeTarget, float _deltaTime);
	//ダメージ受けたを通知する関数
	void NotifyDamageReceived(float _damage);
	//行動確定済みを設定する関数
	void SetActionCommitted(FName _actionName);
	//使用行動が可能か判定する関数
	bool CanUseAction(FName _actionName, float _cooldown) const;

	//戦闘か判定する関数
	UFUNCTION(BlueprintPure, Category = "Enemy|Combat")
	bool IsInCombat() const;

	//戦闘戦闘姿勢を取得する関数
	UFUNCTION(BlueprintPure, Category = "Enemy|Combat")
	EEnemyCombatPosture GetCombatPosture() const
	{
		return m_posture;
	}

	bool IsPlayerAiming() const
	{
		return b_mPlayerAiming;
	}
	bool IsPlayerAttacking() const
	{
		return b_mPlayerAttacking;
	}
	bool IsPlayerWerewolf() const
	{
		return b_mPlayerWerewolf;
	}
	bool IsPlayerStationary() const
	{
		return b_mPlayerStationary;
	}
	bool IsUnderPressure() const
	{
		return m_recentDamagePressure >= m_pressureThreshold;
	}
	float GetPlayerHealthRatio() const
	{
		return m_playerHealthRatio;
	}
	float GetPlayerSpeed() const
	{
		return m_playerSpeed;
	}
	float GetTargetDistance() const
	{
		return m_targetDistance;
	}
	float GetAimHabit() const
	{
		return m_aimHabit;
	}
	float GetStationaryHabit() const
	{
		return m_stationaryHabit;
	}

  private:
	//プレイヤーとの距離と直近の被弾からボスの攻守姿勢を更新する関数
	void UpdatePosture();
	//戦闘記憶の経過時間判定へ使用する現在のゲーム時間を返す関数
	float GetCurrentTime() const;

	//行動傾向の観測対象となるプレイヤーキャラクター参照
	UPROPERTY(Transient)
	TWeakObjectPtr<APlayerChara> m_observedPlayer;

	//通常待機、戦闘待機、攻撃中を区別する敵の構え状態
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Enemy|Combat", meta = (AllowPrivateAccess = "true"))
	EEnemyCombatPosture m_posture;

	//最後の行動時刻一覧
	TMap<FName, float> m_lastActionTimes;
	//敵がプレイヤーを最後に視認したワールド時刻
	float m_lastSeenTime;
	//最後のダメージ時刻
	float m_lastDamageTime;
	//直近のダメージ攻撃圧力
	float m_recentDamagePressure;
	//攻撃選択に使用するプレイヤーの現在体力割合
	float m_playerHealthRatio;
	//回避傾向の判断に使用するプレイヤーの移動速度
	float m_playerSpeed;
	//攻撃形式の判断に使用するプレイヤーまでの距離
	float m_targetDistance;
	//プレイヤーが照準状態を維持する頻度を表す割合
	float m_aimHabit;
	//プレイヤーがその場へ留まる頻度を表す割合
	float m_stationaryHabit;
	//戦闘記憶継続時間
	float m_combatMemoryDuration;
	//ボスが攻撃頻度を調整するためのプレイヤー攻撃圧力
	float m_pressureThreshold;
	//可能視認ターゲットかどうか
	bool b_mCanSeeTarget;
	//プレイヤー照準中かどうか
	bool b_mPlayerAiming;
	//プレイヤー攻撃中かどうか
	bool b_mPlayerAttacking;
	//プレイヤー狼男かどうか
	bool b_mPlayerWerewolf;
	//プレイヤー静止かどうか
	bool b_mPlayerStationary;
};
