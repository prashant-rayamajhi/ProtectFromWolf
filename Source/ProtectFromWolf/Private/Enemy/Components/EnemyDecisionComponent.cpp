#include "Enemy/Components/EnemyDecisionComponent.h"

#include "Enemy/Components/EnemyCombatMemoryComponent.h"
#include "Enemy/EnemyChara.h"
#include "Weapons/EnemyGun.h"
#include "EngineUtils.h"
#include "AIController.h"
#include "Kismet/GameplayStatics.h"
#include "Enemy/EnemyActionChoice.h"
#include "Enemy/Components/EnemyCoverComponent.h"
#include "Enemy/EnemyTeamTactics.h"
#include "AI/Controllers/EnemyAIController.h"

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
		if (!IsValid(ally) || ally == _boss || ally->GetHealthRatio() <= 0.f || ally->m_enemyRank != EEnemyRank::Minion) { continue; }

		//同じ戦闘空間に相当する距離内の雑魚敵だけを人数へ加える
		if (FEnemyTeamTactics::SharesRoom(_boss, ally) &&
			FVector::DistSquared2D(_boss->GetActorLocation(), ally->GetActorLocation()) <= FMath::Square(5000.f)) { ++allyCount; }
	}
	//ボスが連携行動へ利用できる雑魚敵数を返す
	return allyCount;
}
}

//EnemyDecisionComponentが使用するComponentと初期値を構築する関数
UEnemyDecisionComponent::UEnemyDecisionComponent()
	: m_enemy(nullptr), m_styleDecisionTime(0.f), m_adaptiveMeleeDistance(200.f), m_bossMeleeDistance(BossMeleeDistance),
	  m_styleProfileInitialized(false), m_usesAdaptiveStyle(false), m_nextBossActionTime(0.f), m_lastBossAction(NAME_None)
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
	if (!IsValid(m_enemy) || m_enemy->GetHealthRatio() <= 0.f || m_enemy->IsTeleporting() || m_enemy->m_isSwitchingWeapon) { return; }
	if (m_enemy->IsAttacking() || m_enemy->IsReloading()) { return; }
	//装備中のアニメーションが完了するまでは別の武器を選び直さない
	if (m_enemy->GetWeaponState() == EWeaponState::Drawing || m_enemy->GetWeaponState() == EWeaponState::Holstering) { return; }
	if (m_enemy->IsKnockedBack()) { return; }
	//遮蔽物への移動と射撃位置からの反撃を武器切替で中断しない
	if (_targetDistance >= 450.f && m_enemy->m_currentStyle == EEnemyAttackStyle::Gun && m_enemy->m_coverComponent &&
		(m_enemy->m_coverComponent->IsUsingCover() || m_enemy->m_coverComponent->HasFiringWindow())) { return; }

	//敵のランクがミニオンの場合、Adaptive Styleを更新する
	if (m_enemy->m_enemyRank == EEnemyRank::Minion)
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
	//所有者が死亡、装備変更、別行動の最中なら新しい攻撃を重ねない
	if (!m_enemy) { m_enemy = Cast<AEnemyChara>(GetOwner()); }
	if (!IsValid(m_enemy) || m_enemy->GetHealthRatio() <= 0.f || m_enemy->IsTeleporting()) { return false; }
	if (m_enemy->IsAttacking() || m_enemy->IsReloading() || m_enemy->m_isSwitchingWeapon) { return false; }
	if (m_enemy->IsKnockedBack() || (m_enemy->m_coverComponent && m_enemy->m_coverComponent->IsUsingCover() &&
		!m_enemy->m_coverComponent->HasFiringWindow())) { return false; }

	//攻撃開始時にも射線を調べ、直前に遮蔽物へ隠れた相手を狙い続けない
	AActor *target = UGameplayStatics::GetPlayerPawn(GetWorld(), 0);
	AEnemyAIController *controller = Cast<AEnemyAIController>(m_enemy->GetController());
	if (!IsValid(target) || !controller || !controller->CanObserveTarget(target)) { return false; }
	if (m_enemy->m_enemyRank == EEnemyRank::Minion)
	{
		m_enemy->PerformAttack();
		return m_enemy->IsAttacking();
	}

	//前回の攻撃後に反撃できる間を残す
	const float currentTime = GetWorld()->GetTimeSeconds();
	if (currentTime < m_nextBossActionTime) { return false; }
	UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	const float distance = FVector::Dist2D(m_enemy->GetActorLocation(), target->GetActorLocation());
	const bool closeTarget = distance <= BossMeleeDistance;
	const bool underPressure = memory && memory->IsUnderPressure();
	const bool aiming = memory && memory->IsPlayerAiming();
	const bool stationary = memory && memory->IsPlayerStationary();
	const bool werewolf = memory && memory->IsPlayerWerewolf();
	const bool lastBoss = m_enemy->m_enemyRank == EEnemyRank::LastBoss;
	//実際に観測した居座りに対し、予兆付き攻撃で足場の変更を促す
	const float holdPressure = memory ? memory->GetHoldPressure() : 0.f;
	const float shotFailure = memory ? memory->GetShotFailure() : 0.f;

	//候補を独立して採点し、同点では行動名を比較して追加順による偏りを防ぐ
	FEnemyActionChoice choice;
	auto consider = [&](FName _action, float _score, bool _eligible, float _cooldown)
	{
		if (!_eligible || (memory && !memory->CanUseAction(_action, _cooldown))) { return; }
		if (_action != BasicAttackAction && _action == m_lastBossAction) { _score -= 45.f; }
		choice.Consider(_action, _score, true);
	};

	//通常攻撃は現在の武器で命中が期待できる場合だけ候補にする
	const AEnemyGun *gun = m_enemy->GetCurrentGun();
	const bool canShoot = m_enemy->m_currentStyle == EEnemyAttackStyle::Gun && gun && !gun->IsOutOfAmmo();
	const bool canStrike = m_enemy->m_currentStyle == EEnemyAttackStyle::Melee && m_enemy->CanCommitMeleeAttack(target);
	const bool coverFire = m_enemy->m_coverComponent && m_enemy->m_coverComponent->HasFiringWindow();
	consider(BasicAttackAction, coverFire ? 105.f : 60.f, canStrike || (canShoot && distance <= gun->GetFireRange()), 0.f);

	//接近戦、静止狙い、離脱、接近を比較し、溜めのある攻撃も戦況に応じて選ぶ
	consider(SmashAction, 45.f + (werewolf ? 35.f : 0.f) + (underPressure ? 25.f : 0.f) + holdPressure * 60.f,
		closeTarget && m_enemy->CanGroundSmash(), SmashCooldown);
	consider(LaserAction, 40.f + (stationary ? 35.f : 0.f) + (aiming ? 10.f : 0.f) + holdPressure * 110.f + shotFailure * 40.f,
		distance >= 650.f && distance <= 2500.f, LaserCooldown);
	consider(PhaseAwayAction, 50.f + (underPressure ? 40.f : 0.f), lastBoss && closeTarget && underPressure, PhaseAwayCooldown);
	consider(AmbushAction, 45.f + (aiming ? 25.f : 0.f), lastBoss && distance > BossFarDistance && aiming, AmbushCooldown);
	if (choice.m_action.IsNone()) { return false; }
	return CommitBossAction(choice.m_action);
}

//適応攻撃形式を更新する関数
void UEnemyDecisionComponent::UpdateAdaptiveStyle(float _targetDistance)
{
	//Adaptive Styleの初期化がまだ行われていない場合、現在の攻撃方式がAdaptiveかどうかを判定する
	if (!m_styleProfileInitialized)
	{
		m_usesAdaptiveStyle = m_enemy->m_currentStyle == EEnemyAttackStyle::Adaptive;
		m_styleProfileInitialized = true;
	}
	//Adaptive Styleを使用しない場合、処理を終了する
	if (!m_usesAdaptiveStyle) { return; }

	//プレイヤーとの距離からAdaptive型が装備する攻撃Style
	//境界付近を往復しても武器を切り替え続けないよう、近接から戻る距離には余裕を持たせる
	const float switchDistance = m_adaptiveMeleeDistance + (m_enemy->m_currentStyle == EEnemyAttackStyle::Melee ? 100.f : 0.f);
	const EEnemyAttackStyle desiredStyle = _targetDistance <= switchDistance ? EEnemyAttackStyle::Melee : EEnemyAttackStyle::Gun;
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
	//距離外や再使用待ちのレーザーを維持して、近くの相手の前で固まることを防ぐ
	const bool laserUnavailable = m_enemy->m_currentStyle == EEnemyAttackStyle::Laser &&
		(_targetDistance < BossMeleeDistance || _targetDistance > 2500.f ||
		 (m_enemy->m_combatMemoryComponent && !m_enemy->m_combatMemoryComponent->CanUseAction(LaserAction, LaserCooldown)));
	if (m_styleDecisionTime > 0.f && !laserUnavailable) { return; }

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
	if (desiredStyle != m_enemy->m_currentStyle)
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
	//使用できる銃も予備弾薬もない状態では射撃を選ばない
	const AEnemyGun *availableGun = m_enemy->GetCurrentGun();
	//未生成と弾切れを区別し、近接装備で出現したボスの銃撃候補を永久に消さない
	if (!IsValid(availableGun) && !m_enemy->CanEquipGun()) { return -BIG_NUMBER; }
	if (IsValid(availableGun) && availableGun->IsOutOfAmmo() && availableGun->GetTotalAmmo() <= 0) { return -BIG_NUMBER; }
	//射撃評価へプレイヤーの行動傾向を反映する戦闘記憶Component
	const UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	//中距離以上を優先する射撃Styleの基礎評価点
	float score = _targetDistance > BossMeleeDistance ? 110.f : 15.f;
	if (memory) { score -= memory->GetShotFailure() * 40.f; }
	//連携射撃の評価へ使用する周囲の雑魚敵数
	const int32 nearbyAllies = CountNearbyCombatAllies(m_enemy);

	//プレイヤーが静止している場合、スコアを増加させる
	if (memory && memory->IsPlayerStationary()) { score += 30.f; }
	//狼男との近接戦では射撃を中断しやすくする
	if (memory && memory->IsPlayerWerewolf()) { score -= 45.f; }
	//静止を多用するプレイヤーには射撃評価を上げる
	if (memory) { score += memory->GetStationaryHabit() * 35.f; }
	//撃ち続ける相手には溜め攻撃より遮蔽物からの短い反撃を選びやすくする
	if (memory && _targetDistance > BossMeleeDistance) { score += memory->GetAttackHabit() * 15.f - memory->GetHoldPressure() * 50.f; }
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
	//実行できない距離では候補に入れず、射撃か接近を選ぶ
	if (_targetDistance < BossMeleeDistance || _targetDistance > 2500.f) { return -BIG_NUMBER; }
	//再使用待ちのレーザーへ武器を切り替えて立ち止まることを防ぐ
	if (m_enemy->m_combatMemoryComponent && !m_enemy->m_combatMemoryComponent->CanUseAction(LaserAction, LaserCooldown)) { return -BIG_NUMBER; }
	//レーザー評価へプレイヤーの行動傾向を反映する戦闘記憶Component
	const UEnemyCombatMemoryComponent *memory = m_enemy->m_combatMemoryComponent;
	//遠距離を優先するレーザーStyleの基礎評価点
	float score = _targetDistance >= BossFarDistance ? 90.f : 35.f;
	if (memory) { score += memory->GetShotFailure() * 40.f; }
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
	//継続射撃を観測した時は、無防備な長い溜めを控える
	if (memory) { score += memory->GetHoldPressure() * 100.f - memory->GetAttackHabit() * 15.f; }
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

	//武器の準備不足などで開始できなかった行動には待機時間を発生させない
	//レーザーの収納待ちも開始済みとして記録し、選択し直しや連続使用を防ぐ
	if (!m_enemy->IsAttacking() && !m_enemy->IsTeleporting() && !m_enemy->IsLaserSequenceActive()) { return false; }

	//ボス攻撃を戦闘記憶へ記録する
	if (m_enemy->m_combatMemoryComponent) { m_enemy->m_combatMemoryComponent->SetActionCommitted(_actionName); }

	//同じ特殊攻撃の連続使用を防ぐために記録する直前の行動名
	m_lastBossAction = _actionName;
	//最終Phaseで攻撃間隔を短縮するか示す状態
	const bool finalPhase = m_enemy->m_enemyRank == EEnemyRank::LastBoss && m_enemy->GetBossPhase() == EBossPhase::Phase3;
	//RankとPhaseに対応する待機時間を加えて次の行動可能時刻を設定する
	m_nextBossActionTime = (GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f) +
						   (finalPhase ? FMath::RandRange(1.4f, 2.f) : FMath::RandRange(BossActionIntervalMin, BossActionIntervalMax));
	return true;
}
