#include "AI/Controllers/EnemyAIController.h"
#include "AI/BehaviorTree/Blackboard/EnemyBlackboardKeys.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Enemy/EnemyChara.h"
#include "Enemy/EnemyTeamTactics.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/CharacterMovementComponent.h"

void AEnemyAIController::RestoreAlertPace()
{
	if (m_cautious && IsValid(m_enemy) && m_enemy->GetCharacterMovement())
	{
		UCharacterMovementComponent *movement = m_enemy->GetCharacterMovement();
		if (FMath::IsNearlyEqual(movement->MaxWalkSpeed, m_alertSpeed)) { movement->MaxWalkSpeed = m_beforeAlertSpeed; }
	}
	m_cautious = false;
}

//短い遮蔽や攻撃終了だけでは戦闘を忘れず、情報が途絶えてから巡回へ戻す関数
bool AEnemyAIController::HasCombatAwareness() const
{
	//遮蔽物への移動と捜索を終える前に八秒で警戒を解かず、情報が途絶えた場合だけ段階的に戦闘を終える
	return GetWorld() && (HasActiveVisualContact() || HasRecentVisualContact(20.f) || HasRecentPlayerNoise(20.f) ||
		GetWorld()->GetTimeSeconds() - m_teamSeenTime <= 20.f);
}

//発見報告を受けても壁越しに見えている扱いにはせず、最後の確認位置を捜索対象にする関数
void AEnemyAIController::NotifyTeamContact(AActor *_player, const FVector &_location, float _seenTime)
{
	if (!IsValid(_player) || !GetWorld() || _seenTime <= m_teamSeenTime) { return; }
	//初めて届いた交戦報告では巡回経路を打ち切り、報告位置の捜索へ移る
	const bool wasAware = HasCombatAwareness();
	m_targetActor = _player;
	m_teamSeenTime = _seenTime;
	m_teamSeenLocation = _location;
	if (!wasAware && IsValid(m_enemy) && !m_enemy->IsAttacking() && !m_enemy->IsKnockedBack()) { StopMovement(); }
	if (UBlackboardComponent *board = GetBlackboardComponent())
	{
		board->SetValueAsObject(EnemyBlackboardKeys::TargetActor, _player);
		board->SetValueAsBool(EnemyBlackboardKeys::CanSeeTarget, HasCombatAwareness());
	}
}

//直接視認した情報を同じ戦闘空間の味方へ一秒間隔で共有する関数
void AEnemyAIController::ShareVisualContact(AActor *_player, const FVector &_location)
{
	if (!m_enemy || !GetWorld() || GetWorld()->GetTimeSeconds() < m_nextTeamAlertTime) { return; }
	const float seenTime = GetWorld()->GetTimeSeconds();
	m_nextTeamAlertTime = seenTime + 1.f;
	for (TActorIterator<AEnemyChara> it(GetWorld()); it; ++it)
	{
		AEnemyChara *peer = *it;
		if (peer == m_enemy || peer->IsHidden() || peer->GetHealthRatio() <= 0.f || !FEnemyTeamTactics::SharesRoom(m_enemy, peer)) { continue; }
		if (AEnemyAIController *controller = Cast<AEnemyAIController>(peer->GetController()))
		{
			controller->NotifyTeamContact(_player, _location, seenTime);
		}
	}
}

//背後の相手を壁がないだけで発見せず、至近距離でも遮蔽物を無視しない関数
bool AEnemyAIController::CanObserveTarget(const AActor *_target) const
{
	if (!IsValid(m_enemy) || !IsValid(_target)) { return false; }
	//警戒中も現在の体の向きを基準にし、共有された対象参照だけで全周を見ない
	const FVector offset = _target->GetActorLocation() - m_enemy->GetActorLocation();
	const float distance = offset.Size();
	//ボスは雑魚より遠くを確認し、戦闘空間の奥でも正面の相手を見落とさない
	const bool boss = m_enemy->m_enemyRank != EEnemyRank::Minion;
	float range = FMath::Max(m_enemy->GetChaseRange(), 3000.f) * (boss ? 1.5f : 1.f);
	if (m_enemy->Tags.ContainsByPredicate([](FName _tag) { return _tag.ToString().StartsWith(TEXT("CombatRoom_")); }))
	{
		range = FMath::Max(range, boss ? 7500.f : 6000.f);
	}
	if (distance > range) { return false; }
	const float facing = FVector::DotProduct(m_enemy->GetActorForwardVector().GetSafeNormal2D(), offset.GetSafeNormal2D());
	//直前まで交戦していた近距離の相手は、移動で体が横を向いただけでは見失わない
	//未発見の相手や壁越しの相手を無条件で発見する処理にはしない
	const bool trackingNearby = distance <= 650.f && (HasRecentVisualContact(3.f) || HasRecentPlayerNoise(3.f) ||
		(GetWorld() && GetWorld()->GetTimeSeconds() - m_teamSeenTime <= 3.f));
	//視野は半角で判定する。距離の条件を広げても最後に遮蔽を検査する
	const float sightDot = FMath::Cos(FMath::DegreesToRadians(boss ? 100.f : 85.f));
	if (distance > 250.f && facing < sightDot && !trackingNearby) { return false; }
	return LineOfSightTo(_target);
}

//巡回の開始前にも射線を確認し、古いBlackboard値で目の前の相手を無視しない関数
void AEnemyAIController::RefreshCombatAwareness()
{
	if (!IsValid(m_enemy) || m_enemy->IsHidden() || m_enemy->GetHealthRatio() <= 0.f) { return; }
	if (!IsValid(m_targetActor)) { m_targetActor = UGameplayStatics::GetPlayerPawn(GetWorld(), 0); }
	if (!IsValid(m_targetActor)) { return; }
	const bool visible = CanObserveTarget(m_targetActor);
	UpdateVisualContact(m_targetActor, visible, m_targetActor->GetActorLocation());
	//発見報告を取り逃した敵も、味方が直接得た情報から警戒と捜索を開始する処理
	if (!visible)
	{
		for (TActorIterator<AEnemyChara> it(GetWorld()); it; ++it)
		{
			AEnemyChara *peer = *it;
			if (peer == m_enemy || peer->IsHidden() || peer->GetHealthRatio() <= 0.f || !FEnemyTeamTactics::SharesRoom(m_enemy, peer)) { continue; }
			const AEnemyAIController *source = Cast<AEnemyAIController>(peer->GetController());
			if (!source || !IsValid(source->m_targetActor)) { continue; }
			//伝聞を再送して警戒が永久に続かないよう、元の視認・聴覚時刻を維持する処理
			const float seenTime = FMath::Max(source->m_lastVisualContactTime, source->m_lastPlayerNoiseTime);
			if (GetWorld()->GetTimeSeconds() - seenTime > 20.f) { continue; }
			const FVector location = source->m_lastVisualContactTime >= source->m_lastPlayerNoiseTime
				? source->m_lastVisualContactLocation : source->m_lastPlayerNoiseLocation;
			NotifyTeamContact(source->m_targetActor, location, seenTime);
		}
	}
	m_enemy->SetCanSeePlayer(visible);
	if (UBlackboardComponent *board = GetBlackboardComponent())
	{
		board->SetValueAsObject(EnemyBlackboardKeys::TargetActor, m_targetActor);
		board->SetValueAsBool(EnemyBlackboardKeys::CanSeeTarget, HasCombatAwareness());
	}
}
