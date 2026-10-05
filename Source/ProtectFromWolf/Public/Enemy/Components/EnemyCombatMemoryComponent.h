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
		return m_playerAiming;
	}
	bool IsPlayerAttacking() const
	{
		return m_playerAttacking;
	}
	bool IsPlayerWerewolf() const
	{
		return m_playerWerewolf;
	}
	bool IsPlayerStationary() const
	{
		return m_playerStationary;
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
	//視認中に攻撃を継続していた時間の割合を取得する関数
	float GetAttackHabit() const { return m_attackHabit; }
	//同じ足場に留まる様子を実際に見た時間から居座りの強さを返す関数
	float GetHoldPressure() const { return FMath::Clamp(m_holdTime / 6.f, 0.f, 1.f); }
	//実際に生成できた銃弾の数を記録する関数
	void RecordShot();
	//自分の銃弾がプレイヤーへダメージを与えた結果を記録する関数
	void RecordShotHit();
	//射撃を試しても成果が出ていない度合いを返す関数
	float GetShotFailure() const;

  private:
	//最近の射撃結果を評価する発射回数
	float m_shotCount = 0.f;
	//最近の射撃でプレイヤーへダメージを与えた回数
	float m_shotHits = 0.f;
	//最後に観測した足場から離れたかを比較する基準位置
	FVector m_holdPosition = FVector::ZeroVector;
	//同じ足場に留まっていることを視認できた秒数
	float m_holdTime = 0.f;
	//初回の観測位置が登録済みかを示す変数
	bool m_hasHoldPosition = false;
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
	//攻撃を連続する相手に遮蔽物で対抗するための観測割合
	float m_attackHabit = 0.f;
	//戦闘記憶継続時間
	float m_combatMemoryDuration;
	//ボスが攻撃頻度を調整するためのプレイヤー攻撃圧力
	float m_pressureThreshold;
	//可能視認ターゲットかどうか
	bool m_canSeeTarget;
	//プレイヤー照準中かどうか
	bool m_playerAiming;
	//プレイヤー攻撃中かどうか
	bool m_playerAttacking;
	//プレイヤー狼男かどうか
	bool m_playerWerewolf;
	//プレイヤー静止かどうか
	bool m_playerStationary;
};
