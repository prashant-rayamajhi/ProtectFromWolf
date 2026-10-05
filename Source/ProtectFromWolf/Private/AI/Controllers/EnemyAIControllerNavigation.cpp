#include "AI/Controllers/EnemyAIController.h"
#include "Enemy/EnemyChara.h"
#include "Player/PlayerChara.h"
#include "Weapons/EnemyGun.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Spawning/SpawnEnemy.h"
#include "GameFramework/CharacterMovementComponent.h"

//被弾した敵を現在の戦闘Styleに合う追跡状態へ移行する関数
void AEnemyAIController::OnEnemyDamaged()
{
	//敵または攻撃者が無効な状態で位置を参照しない
	if (!m_enemy || !m_targetActor) { return; }
	//巡回中または待機中に被弾した場合だけ戦闘状態へ移行する
	if (m_currentState == EAIState::Move || m_currentState == EAIState::Wait)
	{
		//Adaptive型はプレイヤーとの距離から近接と射撃を選択する
		if (m_enemy->m_currentStyle == EEnemyAttackStyle::Adaptive)
		{
			//戦闘Styleの切り替え判定へ使用するプレイヤーとの二乗距離
			const float distSq = FVector::DistSquared(m_enemy->GetActorLocation(), m_targetActor->GetActorLocation());

			//近接戦闘の距離内であれば近接戦闘へ、そうでなければ遠距離戦闘へ移行する
			if (distSq <= FMath::Square(m_adaptiveMeleeDist))
			{
				m_enemy->EquipWeapon(EEnemyAttackStyle::Melee);
				m_currentState = EAIState::CombatMelee;
			}
			else
			{
				m_enemy->EquipWeapon(EEnemyAttackStyle::Gun);
				m_currentState = EAIState::CombatRangeMove;
				MoveToActor(m_targetActor, m_enemy->GetAttackRange() * .8f);
			}
			return;
		}

		//近接戦闘スタイルの場合は近接戦闘へ移行する
		if (m_enemy->m_currentStyle == EEnemyAttackStyle::Melee)
		{
			m_currentState = EAIState::CombatMelee;
			return;
		}

		//プレイヤーの射線を最も安全に遮る位置
		FVector bestCover;
		//射撃型は被弾地点に留まらないように遮蔽物へ移動する
		if (FindCoverSpot(bestCover))
		{
			m_targetCoverPos = bestCover;
			m_currentState = EAIState::CombatRangeMove;
			MoveToLocation(m_targetCoverPos);
			m_enemy->UnCrouch();
		}
		else
		{
			m_currentState = EAIState::CombatRangeMove;
			MoveToActor(m_targetActor, m_enemy->GetAttackRange() * 0.8f);
		}
	}
}

//プレイヤーの射線を遮る移動可能な遮蔽位置を探す関数
bool AEnemyAIController::FindCoverSpot(FVector &_outCoverSpot)
{
	//遮蔽候補を移動可能な位置から探すナビゲーションシステム
	UNavigationSystemV1 *navSys = UNavigationSystemV1::GetCurrent(GetWorld());
	//NavMeshが存在しないLevelでは遮蔽検索を中止する
	if (!navSys) { return false; }
	//遮蔽候補の検索中心となる敵の現在位置
	const FVector enemyLoc = m_enemy->GetActorLocation();
	//候補との射線と最低距離を調べるプレイヤー位置
	const FVector playerLoc = m_targetActor->GetActorLocation();

	//周囲のランダムなポイントを調べプレイヤーから射線が通らない場所を探す
	for (int i = 0; i < 20; i++)
	{
		//敵の周囲から取得した移動可能な遮蔽候補位置
		FNavLocation randomLoc;
		//移動可能な候補を取得できた場合だけ距離と射線を調べる
		if (navSys->GetRandomPointInNavigableRadius(enemyLoc, 2000.f, randomLoc))
		{
			//カバー候補として射線と高さを確認するNavMesh上の位置
			FVector candidatePosition = randomLoc.Location;
			//プレイヤーに近すぎる候補は射撃陣形を崩すため除外する
			if (FVector::DistSquared(candidatePosition, playerLoc) < FMath::Square(800.f)) { continue; }

			//プレイヤーからそのポイントまでの射線を引き、遮蔽物があるかを確認する
			FHitResult hit;
			GetWorld()->LineTraceSingleByChannel(hit, playerLoc + FVector(0, 0, 50), candidatePosition + FVector(0, 0, 50), ECC_Visibility);

			//遮蔽物がある場合はそのポイントを隠れる場所として採用する
			if (hit.bBlockingHit && hit.GetActor() != m_enemy)
			{
				_outCoverSpot = candidatePosition;
				return true;
			}
		}
	}
	return false;
}

//遮蔽物が立ち姿勢を隠せない低さか判定する関数
bool AEnemyAIController::IsLowCover(FVector _coverPosition)
{
	//遮蔽物越しの射線を調べるプレイヤー上半身の位置
	FVector playerLoc = m_targetActor->GetActorLocation();
	playerLoc.Z += 50.f;

	//立ち姿勢の高さから調べる射線結果
	FHitResult hitHigh, hitLow;
	//自身をカバー射線判定から除外するためのCollision Query設定
	FCollisionQueryParams params;
	params.AddIgnoredActor(m_enemy);

	//高い位置からの射線と低い位置からの射線の両方を確認する
	const bool bHitHigh = GetWorld()->LineTraceSingleByChannel(hitHigh, _coverPosition + FVector(0, 0, 160.f), playerLoc, ECC_Visibility, params);
	//しゃがみ姿勢の高さで遮蔽物に当たったか示す状態
	bool bHitLow = GetWorld()->LineTraceSingleByChannel(hitLow, _coverPosition + FVector(0, 0, 40.f), playerLoc, ECC_Visibility, params);

	//低い位置からの射線が遮蔽物に当たり、高い位置からの射線が遮蔽物に当たらない場合は低い遮蔽物であると判断する
	if (!bHitHigh && bHitLow) { return true; }

	return false;
}

