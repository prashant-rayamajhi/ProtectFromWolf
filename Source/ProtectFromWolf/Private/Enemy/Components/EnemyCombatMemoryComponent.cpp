#include "Enemy/Components/EnemyCombatMemoryComponent.h"

#include "Player/PlayerChara.h"
#include "AI/Controllers/EnemyAIController.h"

//敵の戦闘記憶コンポーネントに関する定数を定義する
namespace
{
//プレイヤーを静止中と判断する平面移動速度
constexpr float StationarySpeedThreshold = 20.f;
//被弾Pressureが一秒間に減少する量
constexpr float PressureDecayPerSecond = 18.f;
//最後の被弾を回避姿勢へ反映する時間
constexpr float PressureMemorySeconds = 2.5f;
//照準と静止の傾向を学習する時間幅
constexpr float PlayerHabitLearningWindow = 8.f;
}

//EnemyCombatMemoryComponentが使用するComponentと初期値を構築する関数
UEnemyCombatMemoryComponent::UEnemyCombatMemoryComponent()
	: m_posture(EEnemyCombatPosture::Relaxed), m_lastSeenTime(-BIG_NUMBER), m_lastDamageTime(-BIG_NUMBER), m_recentDamagePressure(0.f),
	  m_playerHealthRatio(1.f), m_playerSpeed(0.f), m_targetDistance(BIG_NUMBER), m_aimHabit(0.f), m_stationaryHabit(0.f),
	  m_combatMemoryDuration(6.f), m_pressureThreshold(30.f), m_canSeeTarget(false), m_playerAiming(false), m_playerAttacking(false),
	  m_playerWerewolf(false), m_playerStationary(false)
{
	//戦闘記憶はAI Serviceから更新するためComponentのTickを無効化する
	PrimaryComponentTick.bCanEverTick = false;
}

//プレイヤーとの距離、攻撃頻度、回避傾向を観測し、ボスの戦闘記憶へ反映する関数
void UEnemyCombatMemoryComponent::ObservePlayer(APlayerChara *_player, float _targetDistance, bool _canSeeTarget, float _deltaTime)
{
	//プレイヤーの観測情報を更新する
	m_observedPlayer = _player;
	if (_canSeeTarget) { m_targetDistance = _targetDistance; }
	m_canSeeTarget = _canSeeTarget;
	m_recentDamagePressure = FMath::Max(0.f, m_recentDamagePressure - PressureDecayPerSecond * _deltaTime);

	//プレイヤーが存在する場合、体力割合、速度、構え状態を更新する
	if (IsValid(_player) && _canSeeTarget)
	{
		//小さな足踏みでは居座りを解除せず、実際に足場を変えたら対策の優先度を下げる
		if (!m_hasHoldPosition || FVector::DistSquared2D(m_holdPosition, _player->GetActorLocation()) > FMath::Square(180.f))
		{
			m_holdPosition = _player->GetActorLocation();
			m_holdTime = 0.f;
			m_hasHoldPosition = true;
		}
		m_holdTime = FMath::Min(6.f, m_holdTime + FMath::Max(0.f, _deltaTime));
		m_playerHealthRatio = FMath::Clamp(_player->GetHealthRatio(), 0.f, 1.f);
		m_playerSpeed = _player->GetVelocity().Size2D();
		m_playerStationary = m_playerSpeed <= StationarySpeedThreshold;
		m_playerAiming = _player->IsAiming();
		//プレイヤーの攻撃状態と狼男状態を更新する
		m_playerAttacking = _player->IsAttacking();
		m_playerWerewolf = _player->IsWerewolf();
		//今回の観測値を行動傾向へ反映する補間割合
		const float learningAlpha = FMath::Clamp(_deltaTime / PlayerHabitLearningWindow, 0.f, 1.f);
		m_aimHabit = FMath::Lerp(m_aimHabit, m_playerAiming ? 1.f : 0.f, learningAlpha);
		m_stationaryHabit = FMath::Lerp(m_stationaryHabit, m_playerStationary ? 1.f : 0.f, learningAlpha);
		m_attackHabit = FMath::Lerp(m_attackHabit, m_playerAttacking ? 1.f : 0.f, learningAlpha);
	}
	else
	{
		//見えない間に移動や静止を推測せず、対策の確信だけを徐々に弱める
		m_holdTime = FMath::Max(0.f, m_holdTime - FMath::Max(0.f, _deltaTime) * 0.25f);
		//遮蔽物の向こうの操作を読み取らず、過去に観測した傾向だけを保持する
		m_playerAiming = false;
		m_playerAttacking = false;
		m_playerStationary = false;
	}

	//ターゲットを視認できる場合、最後に視認した時間を更新する
	if (_canSeeTarget) { m_lastSeenTime = GetCurrentTime(); }

	//戦闘姿勢を更新する
	UpdatePosture();
}

//過去の射撃を徐々に軽くして、新しい戦況へ追従できる履歴を残す関数
void UEnemyCombatMemoryComponent::RecordShot()
{
	if (m_shotCount >= 20.f) { m_shotCount *= 0.5f; m_shotHits *= 0.5f; }
	m_shotCount += 1.f;
}

//弾の命中通知を自分の射撃成績へ反映する関数
void UEnemyCombatMemoryComponent::RecordShotHit()
{
	m_shotHits = FMath::Min(m_shotCount, m_shotHits + 1.f);
}

//少数の外れだけでは戦術を変えず、繰り返し失敗した時に位置や攻撃を見直す関数
float UEnemyCombatMemoryComponent::GetShotFailure() const
{
	if (m_shotCount < 6.f) { return 0.f; }
	return FMath::Clamp(1.f - m_shotHits / FMath::Max(1.f, m_shotCount) / 0.4f, 0.f, 1.f);
}

//ダメージ受けたを通知する関数
void UEnemyCombatMemoryComponent::NotifyDamageReceived(float _damage)
{
	m_recentDamagePressure += FMath::Max(0.f, _damage);
	m_lastDamageTime = GetCurrentTime();
	UpdatePosture();
}

//ActionCommittedを指定内容へ更新する関数
void UEnemyCombatMemoryComponent::SetActionCommitted(FName _actionName)
{
	m_lastActionTimes.FindOrAdd(_actionName) = GetCurrentTime();
}

//Use Actionが現在成立しているかを判定する関数
bool UEnemyCombatMemoryComponent::CanUseAction(FName _actionName, float _cooldown) const
{
	//行動の最後の使用時間を取得し、クールダウンが経過しているかを判定する
	const float *lastActionTime = m_lastActionTimes.Find(_actionName);
	return !lastActionTime || GetCurrentTime() - *lastActionTime >= _cooldown;
}

//戦闘か判定する関数
bool UEnemyCombatMemoryComponent::IsInCombat() const
{
	//姿勢と行動で同じ警戒情報を使い、味方が交戦中なのに平常姿勢へ戻ることを防ぐ処理
	const APawn *pawn = Cast<APawn>(GetOwner());
	const AEnemyAIController *controller = pawn ? Cast<AEnemyAIController>(pawn->GetController()) : nullptr;
	if (controller && controller->HasCombatAwareness()) { return true; }
	return m_canSeeTarget || GetCurrentTime() - m_lastSeenTime <= m_combatMemoryDuration;
}

//戦闘姿勢を更新する関数
void UEnemyCombatMemoryComponent::UpdatePosture()
{
	//戦闘中でない場合、構えを Relaxed に設定して終了する
	if (!IsInCombat())
	{
		m_posture = EEnemyCombatPosture::Relaxed;
		return;
	}

	//プレイヤーの体力割合が低く、直前のダメージが圧力を与えている場合、構えを Evading
	//に設定して終了する
	if (GetCurrentTime() - m_lastDamageTime <= PressureMemorySeconds && IsUnderPressure())
	{
		m_posture = EEnemyCombatPosture::Evading;
		return;
	}

	//プレイヤーの体力割合が低い場合、構えを Pressuring に設定し、それ以外の場合は Guarded
	//に設定する
	m_posture = m_playerHealthRatio <= 0.3f ? EEnemyCombatPosture::Pressuring : EEnemyCombatPosture::Guarded;
}

//現在の時刻を取得する関数
float UEnemyCombatMemoryComponent::GetCurrentTime() const
{
	//Worldが有効な場合は戦闘記憶の基準となるゲーム時間を返す
	return GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
}
