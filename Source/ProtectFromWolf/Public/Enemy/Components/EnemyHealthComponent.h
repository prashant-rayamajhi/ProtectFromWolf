#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Enemy/EnemyChara.h"
#include "EnemyHealthComponent.generated.h"

USTRUCT()
struct FEnemyDamageResult
{
	GENERATED_BODY()

	//今回のDamage適用前に残っていた体力
	float m_previousHealth = 0.f;
	//今回のDamage適用後に残っている体力
	float m_currentHealth = 0.f;
	//直前の フェーズを進行順、個数、または種類として識別する整数値
	EBossPhase m_previousPhase = EBossPhase::Phase1;
	//現在の フェーズを進行順、個数、または種類として識別する整数値
	EBossPhase m_currentPhase = EBossPhase::Phase1;
	//今回のDamageで敵が死亡したか示す状態
	bool m_died = false;
	//今回のDamageでボスPhaseが変化したか示す状態
	bool m_phaseChanged = false;
	//今回のDamageでボスの撤退条件を満たしたか示す状態
	bool m_shouldRetreat = false;
	//今回のDamageで雑魚召喚の体力境界を越えたか示す状態
	bool m_crossedSummonThreshold = false;
};

//敵のHPとボス段階と一度だけ行う退避判断を管理するコンポーネント
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
//敵の体力、死亡確定、撃破通知を管理する部品
class PROTECTFROMWOLF_API UEnemyHealthComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//敵の体力とボス段階と退避状態の初期値を設定する関数
	UEnemyHealthComponent();

	//敵ランクに応じて最大HPと現在HPを初期化する関数
	void InitializeHealth(float _maxHealth, EEnemyRank _enemyRank, bool _canRetreat);

	//ダメージを適用する関数
	FEnemyDamageResult ApplyDamage(float _damage);

	//全体HPを復元する関数
	void RestoreFullHealth();

	void SetCanRetreat(bool _canRetreat)
	{
		m_canRetreat = _canRetreat;
	}
	float GetCurrentHealth() const
	{
		return m_currentHealth;
	}
	float GetMaxHealth() const
	{
		return m_maxHealth;
	}
	//HP割合を取得する関数
	float GetHealthRatio() const;
	EBossPhase GetBossPhase() const
	{
		return m_bossPhase;
	}
	bool ShouldRetreat() const
	{
		return m_hasRetreated;
	}
	bool IsDead() const
	{
		return m_currentHealth <= 0.f;
	}

  private:
	//敵ランクに応じて設定した最大体力
	float m_maxHealth;
	//受けたダメージを反映した現在体力
	float m_currentHealth;
	//ラストボスが前回雑魚敵を召喚した時の体力割合
	float m_lastSummonHealthRatio;
	//ボス段階と退避判断を切り替える敵ランク
	EEnemyRank m_enemyRank;
	//ボスフェーズかどうか
	EBossPhase m_bossPhase;
	//中間ボスが一度だけ退避行動を選択できるか示す変数
	bool m_canRetreat;
	//保持退避済みかどうか
	bool m_hasRetreated;
};
