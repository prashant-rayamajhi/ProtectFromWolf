#include "Enemy/Components/EnemyHealthComponent.h"

//敵の体力、死亡確定、撃破通知を管理する部品の定数を定義する
namespace
{
//中間ボスが戦闘から撤退する体力割合
constexpr float MiddleBossRetreatRatio = 0.3f;
//ボスが第二Phaseへ移行する体力割合
constexpr float BossPhaseTwoRatio = 0.6f;
//ボスが第三Phaseへ移行する体力割合
constexpr float BossPhaseThreeRatio = 0.3f;
//ラストボスが雑魚敵を追加召喚する体力減少間隔
constexpr float BossSummonIntervalRatio = 0.3f;
}

//EnemyHealthComponentが使用するComponentと初期値を構築する関数
UEnemyHealthComponent::UEnemyHealthComponent()
	: m_maxHealth(1.f), m_currentHealth(1.f), m_lastSummonHealthRatio(1.f), m_enemyRank(EEnemyRank::Minion), m_bossPhase(EBossPhase::Phase1),
	  m_canRetreat(true), m_hasRetreated(false)
{
	//このコンポーネントは毎フレームの更新を必要としないため、Tickを無効化する
	PrimaryComponentTick.bCanEverTick = false;
}

//敵ランクに対応する最大体力を設定し、死亡通知を受け付ける初期状態へ戻す関数
void UEnemyHealthComponent::InitializeHealth(float _maxHealth, EEnemyRank _enemyRank, bool _canRetreat)
{
	//最大体力を1以上に制限し、現在の体力を最大体力で初期化する
	m_maxHealth = FMath::Max(_maxHealth, 1.f);
	m_currentHealth = m_maxHealth;
	m_enemyRank = _enemyRank;
	m_canRetreat = _canRetreat;
	m_hasRetreated = false;
	m_bossPhase = EBossPhase::Phase1;
	m_lastSummonHealthRatio = 1.f;
}

//ダメージを適用する関数
FEnemyDamageResult UEnemyHealthComponent::ApplyDamage(float _damage)
{
	//敵へ適用したダメージと死亡状態を返す結果
	FEnemyDamageResult result;
	result.m_previousHealth = m_currentHealth;
	result.m_previousPhase = m_bossPhase;
	m_currentHealth = FMath::Clamp(m_currentHealth - FMath::Max(_damage, 0.f), 0.f, m_maxHealth);
	result.m_currentHealth = m_currentHealth;

	//Phase、死亡、撤退、召喚を判定するDamage適用後の体力割合
	const float healthRatio = GetHealthRatio();
	//ボスRankの場合だけ体力割合から現在Phaseを更新する
	if (m_enemyRank != EEnemyRank::Minion)
	{
		//体力30パーセント以下では最終Phaseへ移行する
		if (healthRatio <= BossPhaseThreeRatio) { m_bossPhase = EBossPhase::Phase3; }
		//体力60パーセント以下では第二Phaseへ移行する
		else if (healthRatio <= BossPhaseTwoRatio) { m_bossPhase = EBossPhase::Phase2; }
		else
		{
			//体力60パーセントより多い場合は第一Phaseを維持する
			m_bossPhase = EBossPhase::Phase1;
		}
	}

	//結果構造体に現在のフェーズと状態変化を反映する
	result.m_currentPhase = m_bossPhase;
	result.m_phaseChanged = result.m_previousPhase != result.m_currentPhase;
	result.m_died = result.m_previousHealth > 0.f && m_currentHealth <= 0.f;

	//ボス召喚の閾値を超えたかどうかを判定し、結果構造体に反映する
	if (m_enemyRank == EEnemyRank::LastBoss &&
		FMath::FloorToInt(m_lastSummonHealthRatio / BossSummonIntervalRatio) > FMath::FloorToInt(healthRatio / BossSummonIntervalRatio))
	{
		m_lastSummonHealthRatio = healthRatio;
		result.m_crossedSummonThreshold = true;
	}

	//中ボスが退却すべき条件を満たしているかどうかを判定し、結果構造体に反映する
	if (m_canRetreat && m_enemyRank == EEnemyRank::MiddleBoss && !m_hasRetreated && healthRatio < MiddleBossRetreatRatio)
	{
		m_hasRetreated = true;
		result.m_shouldRetreat = true;
	}

	return result;
}

//全体HPを復元する関数
void UEnemyHealthComponent::RestoreFullHealth()
{
	m_currentHealth = m_maxHealth;
	m_lastSummonHealthRatio = 1.f;
	m_bossPhase = EBossPhase::Phase1;
	m_hasRetreated = false;
}

//HP割合を取得する関数
float UEnemyHealthComponent::GetHealthRatio() const
{
	//最大体力が有効な場合だけ現在体力を割合へ変換する
	return m_maxHealth > 0.f ? m_currentHealth / m_maxHealth : 0.f;
}
