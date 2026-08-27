#include "Enemy/Components/EnemyDecisionComponent.h"

#include "Enemy/Components/EnemyCombatMemoryComponent.h"
#include "Enemy/EnemyChara.h"
#include "Weapons/EnemyGun.h"
#include "EngineUtils.h"

//敵の戦闘行動の意思決定に関する定数、列挙型、構造体、関数を定義する
namespace
{
//通常攻撃を戦闘記憶へ記録する識別名
const FName BasicAttackAction(TEXT("BasicAttack"));
//跳躍攻撃を戦闘記憶へ記録する識別名
const FName LeapAction(TEXT("Leap"));
//地面叩きつけを戦闘記憶へ記録する識別名
const FName SmashAction(TEXT("GroundSmash"));
//連続射撃を戦闘記憶へ記録する識別名
const FName BarrageAction(TEXT("Barrage"));
//レーザー攻撃を戦闘記憶へ記録する識別名
const FName LaserAction(TEXT("Laser"));
//離脱Teleportを戦闘記憶へ記録する識別名
const FName PhaseAwayAction(TEXT("PhaseAway"));
//接近Teleportを戦闘記憶へ記録する識別名
const FName AmbushAction(TEXT("Ambush"));
//ボスが攻撃Styleを再評価する最短間隔
constexpr float MinBossDecisionTime = 2.5f;
//ボスが攻撃Styleを再評価する最長間隔
constexpr float MaxBossDecisionTime = 4.5f;
//ボスが近接攻撃を優先するプレイヤーとの距離
constexpr float BossMeleeDistance = 650.f;
//ボスが遠距離攻撃を優先するプレイヤーとの距離
constexpr float BossFarDistance = 1000.f;
//ボスが次の攻撃を選べるまでの最短待機時間
constexpr float BossActionIntervalMin = 1.8f;
//ボスが次の攻撃を選べるまでの最長待機時間
constexpr float BossActionIntervalMax = 2.6f;
//跳躍攻撃の連続使用を防ぐ待機時間
constexpr float LeapCooldown = 5.f;
//地面叩きつけの連続使用を防ぐ待機時間
constexpr float SmashCooldown = 6.f;
//連続射撃の連続使用を防ぐ待機時間
constexpr float BarrageCooldown = 7.f;
//レーザー攻撃の連続使用を防ぐ待機時間
constexpr float LaserCooldown = 8.f;
//離脱Teleportの連続使用を防ぐ待機時間
constexpr float PhaseAwayCooldown = 7.f;
//接近Teleportの連続使用を防ぐ待機時間
constexpr float AmbushCooldown = 9.f;

//指定したボスの周囲で戦闘可能な雑魚敵の人数を数える関数
int32 CountNearbyCombatAllies(const AEnemyChara *_boss)
{
	//ボスが有効でない場合、0を返す
	if (!_boss) { return 0; }

	//陣形を考慮した行動選択へ使用する周囲の雑魚敵数
	int32 allyCount = 0;
	//World内の敵を順番に調べて同じ戦闘へ参加中の雑魚敵を探す
	for (TActorIterator<AEnemyChara> iterator(_boss->GetWorld()); iterator; ++iterator)
	{
		//周囲判定の対象となる敵
		const AEnemyChara *ally = *iterator;
		//無効な敵、ボス自身、死亡済みの敵、ボスRankを人数から除外する
		if (!IsValid(ally) || ally == _boss || ally->GetHealthRatio() <= 0.f || ally->m_EnemyRank != EEnemyRank::Minion) { continue; }

		//同じ戦闘空間に相当する距離内の雑魚敵だけを人数へ加える
		if (FVector::DistSquared2D(_boss->GetActorLocation(), ally->GetActorLocation()) <= FMath::Square(5000.f)) { ++allyCount; }
	}
	//ボスが連携行動へ利用できる雑魚敵数を返す
	return allyCount;
}
}

//EnemyDecisionComponentが使用するComponentと初期値を構築する関数
UEnemyDecisionComponent::UEnemyDecisionComponent()
	: m_enemy(nullptr), m_styleDecisionTime(0.f), m_adaptiveMeleeDistance(200.f), m_bossMeleeDistance(BossMeleeDistance),
	  b_mStyleProfileInitialized(false), b_mUsesAdaptiveStyle(false), m_nextBossActionTime(0.f), m_lastBossAction(NAME_None)
{
	//このコンポーネントは毎フレームの更新を必要としないため、Tickを無効化する
	PrimaryComponentTick.bCanEverTick = false;
}

//Decisionを最新の入力と状態へ同期する関数
void UEnemyDecisionComponent::UpdateDecision(float _deltaTime, float _targetDistance)
{
	//初回判断時にComponentの所有者から敵を取得する
	if (!m_enemy) m_enemy = Cast<AEnemyChara>(GetOwner());
	//敵が無効または別行動中の場合は新しい判断を開始しない
	if (!m_enemy || m_enemy->IsAttacking() || m_enemy->IsReloading()) { return; }

	//敵のランクがミニオンの場合、Adaptive Styleを更新する
	if (m_enemy->m_EnemyRank == EEnemyRank::Minion)
	{
		UpdateAdaptiveStyle(_targetDistance);
		return;
	}

	//敵のランクがボスの場合、ボス Styleを更新する
	UpdateBossStyle(_deltaTime, _targetDistance);
}

//攻撃 を現在の攻撃対象へ実行する関数
bool UEnemyDecisionComponent::ExecuteAttack()
{
	//初回攻撃時にComponentの所有者から敵を取得する
	if (!m_enemy) m_enemy = Cast<AEnemyChara>(GetOwner());
	//敵が無効または別行動中の場合は攻撃を開始しない
	if (!m_enemy || m_enemy->IsAttacking() || m_enemy->IsReloading()) { return false; }

	//敵のランクがミニオンの場合、通常攻撃を実行する
	if (m_enemy->m_EnemyRank == EEnemyRank::Minion)
	{
		m_enemy->PerformAttack();
		return true;
	}

	//Cooldown判定へ使用する現在のゲーム時間
	const float currentTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	//前回攻撃から待機時間が経過するまでは次の攻撃を選択しない
	if (currentTime < m_nextBossActionTime) { return false; }

	//プレイヤーの行動傾向を攻撃選択へ反映する戦闘記憶Component
	UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	//攻撃選択へ使用するプレイヤーとの距離
	const float distance = memory ? memory->GetTargetDistance() : BossMeleeDistance;
	//連携射撃を選べるか判断する周囲の雑魚敵数
	const int32 nearbyAllies = CountNearbyCombatAllies(m_enemy);

	//戦況から特殊攻撃へ置き換える前の基本行動
	FName selectedAction = BasicAttackAction;
	//中間ボスは変身、攻撃、照準、静止へ対抗する特殊攻撃を選ぶ
	if (memory && m_enemy->m_EnemyRank == EEnemyRank::MiddleBoss)
	{
		//近距離で攻め続けるプレイヤーには地面叩きつけで反撃する
		if ((memory->IsPlayerWerewolf() || memory->IsUnderPressure() || memory->IsPlayerAttacking()) && distance <= BossFarDistance &&
			memory->CanUseAction(SmashAction, SmashCooldown))
		{
			selectedAction = SmashAction;
		}
		//離れて照準中のプレイヤーには跳躍攻撃で間合いを詰める
		else if (memory->IsPlayerAiming() && distance > BossMeleeDistance && memory->CanUseAction(LeapAction, LeapCooldown))
		{
			selectedAction = LeapAction;
		}
		//離れて静止するプレイヤーには連続射撃で移動を促す
		else if (memory->IsPlayerStationary() && distance > BossMeleeDistance && m_enemy->m_CurrentStyle == EEnemyAttackStyle::Gun &&
				 memory->CanUseAction(BarrageAction, BarrageCooldown))
		{
			selectedAction = BarrageAction;
		}
	}
	//ラストボスは味方との連携とTeleportを含む攻撃候補から選ぶ
	if (memory && m_enemy->m_EnemyRank == EEnemyRank::LastBoss)
	{
		//近距離の狼男には地面叩きつけで接近戦を拒否する
		if (memory->IsPlayerWerewolf() && distance <= BossMeleeDistance && memory->CanUseAction(SmashAction, SmashCooldown))
		{
			selectedAction = SmashAction;
		}
		//近距離で攻め続けられた場合は離脱Teleportで間合いを作る
		else if ((memory->IsUnderPressure() || memory->IsPlayerAttacking()) && distance < BossMeleeDistance &&
				 memory->CanUseAction(PhaseAwayAction, PhaseAwayCooldown))
		{
			selectedAction = PhaseAwayAction;
		}
		//遠距離から照準中のプレイヤーには接近Teleportで背後を狙う
		else if (memory->IsPlayerAiming() && distance > BossFarDistance && memory->CanUseAction(AmbushAction, AmbushCooldown))
		{
			selectedAction = AmbushAction;
		}
		//雑魚敵が交戦中なら別方向から連続射撃を重ねる
		else if (nearbyAllies > 0 && distance > BossMeleeDistance && m_enemy->m_CurrentStyle == EEnemyAttackStyle::Gun &&
				 memory->CanUseAction(BarrageAction, BarrageCooldown))
		{
			selectedAction = BarrageAction;
		}
		//離れて静止するプレイヤーには予兆付きレーザーを選択する
		else if (memory->IsPlayerStationary() && distance > BossMeleeDistance && memory->CanUseAction(LaserAction, LaserCooldown))
		{
			selectedAction = LaserAction;
		}
	}

	//近接Styleで特殊行動が未選択の場合は戦況に合う近接攻撃を補う
	if (selectedAction == BasicAttackAction && m_enemy->m_CurrentStyle == EEnemyAttackStyle::Melee)
	{
		//プレイヤーが狼男で、SmashActionが使用可能な場合、SmashActionを選択する
		if (memory && memory->IsPlayerWerewolf() && memory->CanUseAction(SmashAction, SmashCooldown)) { selectedAction = SmashAction; }
		//プレイヤーが近接範囲外なら跳躍攻撃で追跡する
		else if (distance > BossMeleeDistance && (!memory || memory->CanUseAction(LeapAction, LeapCooldown))) { selectedAction = LeapAction; }
		//近接戦で押されている場合は地面叩きつけで反撃する
		else if (memory && memory->IsUnderPressure() && memory->CanUseAction(SmashAction, SmashCooldown)) { selectedAction = SmashAction; }
	}
	//射撃Styleで特殊行動が未選択の場合は静止または照準中の相手へ連続射撃する
	else if (selectedAction == BasicAttackAction && m_enemy->m_CurrentStyle == EEnemyAttackStyle::Gun)
	{
		//狙いやすい状態のプレイヤーには連続射撃を選択する
		if (memory && (memory->IsPlayerStationary() || memory->IsPlayerAiming()) && memory->CanUseAction(BarrageAction, BarrageCooldown))
		{
			selectedAction = BarrageAction;
		}
	}
	//レーザーStyleではCooldownと連続使用を確認してから攻撃する
	else if (selectedAction == BasicAttackAction && m_enemy->m_CurrentStyle == EEnemyAttackStyle::Laser)
	{
		//レーザーがCooldown中または直前にも使用した場合は通常武器へ戻す
		if ((memory && !memory->CanUseAction(LaserAction, LaserCooldown)) || m_lastBossAction == LaserAction)
		{
			m_enemy->EquipWeapon(distance <= BossMeleeDistance ? EEnemyAttackStyle::Melee : EEnemyAttackStyle::Gun);
			m_lastBossAction = NAME_None;
			return true;
		}
		//レーザーを使用可能な場合は攻撃候補として確定する
		selectedAction = LaserAction;
	}

	//同じ特殊攻撃が連続しないように通常攻撃へ戻す
	if (selectedAction == m_lastBossAction && selectedAction != BasicAttackAction)
	{
		//前回と同じ行動を選択した場合、BasicAttackにフォールバックする
		if (selectedAction == LaserAction)
		{
			m_enemy->EquipWeapon(distance <= BossMeleeDistance ? EEnemyAttackStyle::Melee : EEnemyAttackStyle::Gun);
			m_lastBossAction = NAME_None;
			return true;
		}
		selectedAction = BasicAttackAction;
	}
	//確定した行動を実行して戦闘記憶へ記録する
	return CommitBossAction(selectedAction);
}

//適応攻撃形式を更新する関数
void UEnemyDecisionComponent::UpdateAdaptiveStyle(float _targetDistance)
{
	//Adaptive Styleの初期化がまだ行われていない場合、現在の攻撃方式がAdaptiveかどうかを判定する
	if (!b_mStyleProfileInitialized)
	{
		b_mUsesAdaptiveStyle = m_enemy->m_CurrentStyle == EEnemyAttackStyle::Adaptive;
		b_mStyleProfileInitialized = true;
	}
	//Adaptive Styleを使用しない場合、処理を終了する
	if (!b_mUsesAdaptiveStyle) { return; }

	//プレイヤーとの距離からAdaptive型が装備する攻撃Style
	const EEnemyAttackStyle desiredStyle = _targetDistance <= m_adaptiveMeleeDistance ? EEnemyAttackStyle::Melee : EEnemyAttackStyle::Gun;
	//距離に対応する武器へ切り替える
	m_enemy->EquipWeapon(desiredStyle);
}

//ボスStyleを最新の入力と状態へ同期する関数
void UEnemyDecisionComponent::UpdateBossStyle(float _deltaTime, float _targetDistance)
{
	//弾切れ時に射撃Styleを候補から除外するための装備中の銃
	AEnemyGun *currentGun = m_enemy->GetCurrentGun();
	//銃と予備弾薬が空の場合は近接Styleへ固定する
	if (currentGun && currentGun->IsOutOfAmmo() && currentGun->GetTotalAmmo() <= 0)
	{
		m_enemy->StopCombatIdleAnimation();
		m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
		m_styleDecisionTime = MinBossDecisionTime;
		return;
	}

	//スタイル決定のための時間を減算する
	m_styleDecisionTime -= _deltaTime;
	//再評価時刻までは現在の攻撃Styleを維持する
	if (m_styleDecisionTime > 0.f) { return; }

	//各攻撃の評価点から最終的に装備する攻撃Style
	EEnemyAttackStyle desiredStyle = EEnemyAttackStyle::Melee;
	//距離と戦闘記憶から算出した近接Styleの評価点
	const float meleeScore = ScoreMeleeStyle(_targetDistance);
	//比較中に最も高い攻撃Styleの評価点
	float bestScore = meleeScore;
	//距離と戦闘記憶から算出した射撃Styleの評価点
	const float gunScore = ScoreGunStyle(_targetDistance);
	//距離と戦闘記憶から算出したレーザーStyleの評価点
	const float laserScore = ScoreLaserStyle(_targetDistance);

	//最も高いスコアの攻撃方式を選択する
	if (gunScore > bestScore)
	{
		bestScore = gunScore;
		desiredStyle = EEnemyAttackStyle::Gun;
	}

	//レーザー攻撃のスコアが最も高い場合、レーザー攻撃を選択する
	if (laserScore > bestScore) { desiredStyle = EEnemyAttackStyle::Laser; }

	//現在の攻撃方式と選択された攻撃方式が異なる場合、攻撃方式を切り替える
	if (desiredStyle != m_enemy->m_CurrentStyle)
	{
		m_enemy->StopCombatIdleAnimation();
		m_enemy->SwitchWeapon(desiredStyle);
	}

	//攻撃方法の選択に使用する戦闘記憶を取得する
	const UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	//次に攻撃方法を再評価するまでの時間を設定する
	m_styleDecisionTime = FMath::RandRange(MinBossDecisionTime, MaxBossDecisionTime);
}

//距離、体力、直前の攻撃履歴から近接攻撃の選択点を算出する関数
float UEnemyDecisionComponent::ScoreMeleeStyle(float _targetDistance) const
{
	//近接評価へプレイヤーの行動傾向を反映する戦闘記憶Component
	const UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	//近距離を優先する近接Styleの基礎評価点
	float score = _targetDistance <= BossMeleeDistance ? 130.f : 25.f;
	//味方が射撃陣形を組める場合に近接優先度を下げるための雑魚敵数
	const int32 nearbyAllies = CountNearbyCombatAllies(m_enemy);

	//プレイヤーが狼男である場合、スコアを増加させる
	if (memory && memory->IsPlayerWerewolf()) { score += 70.f; }
	//プレイヤーから攻め続けられている場合は近接反撃の評価を上げる
	if (memory && memory->IsUnderPressure()) { score += 25.f; }
	//照準を多用するプレイヤーには接近攻撃の評価を上げる
	if (memory) { score += memory->GetAimHabit() * 35.f; }
	//雑魚敵が交戦中の場合は射線を塞がないように近接評価を下げる
	if (nearbyAllies > 0) { score -= 18.f; }
	//銃の残弾が少ない場合は近接Styleの評価を上げる
	if (const AEnemyGun *gun = m_enemy->GetCurrentGun())
	{
		//マガジン残弾が四分の一以下なら近接Styleの評価を上げる
		if (gun->GetCurrentAmmo() <= FMath::Max(1, gun->GetClipSize() / 4)) { score += 20.f; }
	}
	return score;
}

//射線、距離、遮蔽物状況から銃撃の選択点を算出する関数
float UEnemyDecisionComponent::ScoreGunStyle(float _targetDistance) const
{
	//射撃評価へプレイヤーの行動傾向を反映する戦闘記憶Component
	const UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	//中距離以上を優先する射撃Styleの基礎評価点
	float score = _targetDistance > BossMeleeDistance ? 75.f : 15.f;
	//連携射撃の評価へ使用する周囲の雑魚敵数
	const int32 nearbyAllies = CountNearbyCombatAllies(m_enemy);

	//プレイヤーが静止している場合、スコアを増加させる
	if (memory && memory->IsPlayerStationary()) { score += 30.f; }
	//狼男との近接戦では射撃を中断しやすくする
	if (memory && memory->IsPlayerWerewolf()) { score -= 45.f; }
	//静止を多用するプレイヤーには射撃評価を上げる
	if (memory) { score += memory->GetStationaryHabit() * 35.f; }
	//雑魚敵が交戦中の場合は別方向からの連携射撃を優先する
	if (nearbyAllies > 0 && _targetDistance > BossMeleeDistance) { score += 22.f; }
	//マガジンの残弾割合に応じて射撃評価を下げる
	if (const AEnemyGun *gun = m_enemy->GetCurrentGun())
	{
		//射撃継続能力を評価するマガジン残弾割合
		const float ammoRatio = gun->GetClipSize() > 0 ? static_cast<float>(gun->GetCurrentAmmo()) / static_cast<float>(gun->GetClipSize()) : 0.f;
		score -= (1.f - ammoRatio) * 85.f;
		//弾切れ中は射撃Styleを選ばないように評価を大きく下げる
		if (gun->IsOutOfAmmo()) score -= 100.f;
	}
	return score;
}

//プレイヤーの移動傾向、距離、クールダウンからレーザー攻撃の選択点を算出する関数
float UEnemyDecisionComponent::ScoreLaserStyle(float _targetDistance) const
{
	//レーザー評価へプレイヤーの行動傾向を反映する戦闘記憶Component
	const UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	//遠距離を優先するレーザーStyleの基礎評価点
	float score = _targetDistance >= BossFarDistance ? 90.f : 35.f;
	//連携攻撃の評価へ使用する周囲の雑魚敵数
	const int32 nearbyAllies = CountNearbyCombatAllies(m_enemy);

	//プレイヤーが狼男である場合、スコアを減少させる
	//近距離では溜め中に攻撃されるためレーザー評価を下げる
	if (_targetDistance < BossMeleeDistance) { score -= 45.f; }
	//静止中のプレイヤーにはレーザー評価を上げる
	if (memory && memory->IsPlayerStationary()) { score += 25.f; }
	//体力が少ないプレイヤーには回避を要求するレーザー評価を上げる
	if (memory && memory->GetPlayerHealthRatio() <= 0.25f) { score += 20.f; }
	//静止を多用するプレイヤーにはレーザー評価を上げる
	if (memory) { score += memory->GetStationaryHabit() * 45.f; }
	//雑魚敵が交戦中の場合は別方向からレーザーを重ねる
	if (nearbyAllies > 0 && _targetDistance > BossMeleeDistance) { score += 15.f; }
	//銃弾が少ない場合は弾薬を消費しないレーザー評価を上げる
	if (const AEnemyGun *gun = m_enemy->GetCurrentGun())
	{
		//マガジン残弾が四分の一以下ならレーザーStyleの評価を上げる
		if (gun->GetCurrentAmmo() <= FMath::Max(1, gun->GetClipSize() / 4)) { score += 25.f; }
	}
	return score;
}

//選択したボス攻撃を戦闘記憶へ記録し、連続使用を制限する関数
bool UEnemyDecisionComponent::CommitBossAction(FName _actionName)
{

	//跳躍攻撃では接近移動の後に近接攻撃を続ける
	if (_actionName == LeapAction)
	{
		m_enemy->PerformLeapAttack();
		m_enemy->PerformAttack();
	}
	//地面叩きつけでは地面Effectと同じ範囲へDamageを適用する
	else if (_actionName == SmashAction) { m_enemy->PerformGroundSmash(); }
	//連続射撃では通常Burstより長い射撃を実行する
	else if (_actionName == BarrageAction) { m_enemy->PerformBarrageShot(); }
	//レーザーでは溜めAnimationと予兆Effectから攻撃を開始する
	else if (_actionName == LaserAction) { m_enemy->BeginLaserAttackSequence(); }
	//離脱Teleportでは射撃を止めてプレイヤーから距離を取る
	else if (_actionName == PhaseAwayAction)
	{
		m_enemy->StopFiring();
		m_enemy->PerformTeleportAwayFromTarget();
	}
	//接近Teleportでは射撃を止めてプレイヤーの近くへ移動する
	else if (_actionName == AmbushAction)
	{
		m_enemy->StopFiring();
		m_enemy->PerformTeleportToTarget();
	}
	else
	{
		//特殊行動を選ばなかった場合は現在武器の通常攻撃を実行する
		m_enemy->PerformAttack();
	}

	//ボス攻撃を戦闘記憶へ記録する
	if (m_enemy->m_combatMemoryComponent) { m_enemy->m_combatMemoryComponent->SetActionCommitted(_actionName); }

	//同じ特殊攻撃の連続使用を防ぐために記録する直前の行動名
	m_lastBossAction = _actionName;
	//最終Phaseで攻撃間隔を短縮するか示す状態
	const bool b_finalPhase = m_enemy->m_EnemyRank == EEnemyRank::LastBoss && m_enemy->GetBossPhase() == EBossPhase::Phase3;
	//RankとPhaseに対応する待機時間を加えて次の行動可能時刻を設定する
	m_nextBossActionTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) +
						   (b_finalPhase ? FMath::RandRange(1.4f, 2.f) : FMath::RandRange(BossActionIntervalMin, BossActionIntervalMax));
	return true;
}
