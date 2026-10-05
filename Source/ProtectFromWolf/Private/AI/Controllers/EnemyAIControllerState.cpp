#include "AI/Controllers/EnemyAIController.h"
#include "Enemy/EnemyChara.h"
#include "Player/PlayerChara.h"
#include "Weapons/EnemyGun.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Spawning/SpawnEnemy.h"
#include "GameFramework/CharacterMovementComponent.h"

//敵の視認距離、弾薬、Rankから戦闘状態を切り替える関数
void AEnemyAIController::UpdateStateLogic()
{
	//敵キャラクターとターゲットアクターが存在しない場合は処理を終了する
	if (!m_enemy || !m_targetActor) { return; }
	//退避中やレーザー準備中は状態の切り替えを行わない
	if (m_currentState == EAIState::Retreat || m_currentState == EAIState::LaserPreparation) { return; }

	//状態切り替え判定へ使用するプレイヤーとの二乗距離
	const float distSq = FVector::DistSquared(m_enemy->GetActorLocation(), m_targetActor->GetActorLocation());
	//戦闘を開始または終了する追跡範囲の二乗値
	const float chaseSq = FMath::Square(m_enemy->GetChaseRange());

	//戦闘中にプレイヤーが遠ざかったり見失ったりした場合はパトロールに戻る
	if (m_currentState == EAIState::CombatMelee || m_currentState == EAIState::CombatRangeMove || m_currentState == EAIState::CombatRangeHide)
	{
		//追跡範囲を離れるか視認を失った場合は戦闘を終了する
		if (distSq > chaseSq * 1.2f || !m_enemy->CanSeePlayer())
		{
			//戦闘中にプレイヤーを見失った場合は、攻撃やリロード中でなければパトロール状態に戻す
			if (m_enemy->IsAttacking() || m_enemy->IsReloading()) { return; }
			m_currentState = EAIState::Move;
			m_enemy->SetActionState(EActionState::Idle);
			ClearFocus(EAIFocusPriority::Gameplay);
			return;
		}
	}

	//パトロール中にプレイヤーを発見していない場合はそのまま処理を終了する
	if (m_currentState == EAIState::Move || m_currentState == EAIState::Wait)
	{
		//プレイヤーを視認して追跡範囲へ入るまでは巡回を継続する
		if (!m_enemy->CanSeePlayer() || distSq > chaseSq) { return; }
	}

	//銃攻撃を継続できず近接へ切り替える必要があるか示す状態
	bool bOutOfAmmo = false;
	//装備中の銃が存在する場合だけ弾切れ状態を取得する
	if (m_enemy->GetCurrentGun()) { bOutOfAmmo = m_enemy->GetCurrentGun()->IsOutOfAmmo() && m_enemy->GetCurrentGun()->GetTotalAmmo() <= 0; }

	//ラストボスが一定時間ごとに距離と直前行動から次の攻撃形式を選び直す処理
	if (m_enemy->m_enemyRank == EEnemyRank::LastBoss)
	{
		//攻撃または武器切り替えの途中で別の行動へ割り込まない
		if (m_enemy->IsAttacking() || m_enemy->m_isSwitchingWeapon) { return; }
		m_bossModeTimer -= GetWorld()->GetDeltaSeconds();

		//弾切れ時はテレポートで即近接に切り替える
		if (bOutOfAmmo)
		{
			//射撃Modeから近接Modeへ一度だけ切り替える
			if (!m_isMeleeMode)
			{
				m_isMeleeMode = true;
				m_bossModeTimer = 5.0f;
				m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
				//プレイヤーの近くへ移動して近接戦闘を開始する
				m_enemy->PerformTeleportToTarget();
				m_currentState = EAIState::CombatMelee;
			}
			else { m_bossModeTimer = 5.0f; }
			return;
		}

		//タイマーが切れたらランダムにスタイルを切り替える
		if (m_bossModeTimer <= 0.f)
		{
			//近接Modeの終了後は射撃またはレーザーを選択する
			if (m_isMeleeMode)
			{
				//次の行動選択まで射撃Modeを維持する時間を設定する
				m_isMeleeMode = false;
				m_bossModeTimer = FMath::RandRange(6.0f, 10.0f);

				//射撃Modeへ移行する際は30パーセントの確率でレーザーを選択する
				if (FMath::RandRange(1, 100) <= 30)
				{
					//テレポート中でなければレーザー準備に入る
					if (!m_enemy->IsTeleporting())
					{
						m_enemy->SwitchWeapon(EEnemyAttackStyle::Laser);
						m_enemy->StartLaserCharge();

						//テレポートは行わない（レーザーとテレポートの同時実行防止）
						m_currentState = EAIState::LaserPreparation;
						m_stateTimer = 1.5f;
					}
				}
				else
				{
					m_enemy->SwitchWeapon(EEnemyAttackStyle::Gun);
					//レーザーを選択しなかった場合はプレイヤーから距離を取る
					if (!m_enemy->IsTeleporting()) { m_enemy->PerformTeleportAwayFromTarget(); }
					m_currentState = EAIState::CombatRangeMove;
				}
			}
			else
			{
				//射撃Modeの終了後はプレイヤーの近くへ移動して近接Modeへ戻す
				m_isMeleeMode = true;
				m_bossModeTimer = FMath::RandRange(4.0f, 7.0f);
				m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
				m_enemy->PerformTeleportToTarget();
				m_currentState = EAIState::CombatMelee;
			}
			return;
		}

		//タイマー中は現在のモードを維持する
		if (m_isMeleeMode)
		{
			//装備が近接武器でない場合は現在Modeに合わせて切り替える
			if (m_enemy->m_currentStyle != EEnemyAttackStyle::Melee) m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
			m_currentState = EAIState::CombatMelee;
		}
		else
		{
			//プレイヤーが近距離にいる場合は遠距離モードをキャンセルして近接に切り替える
			if (distSq <= FMath::Square(400.f) && m_currentState != EAIState::LaserPreparation)
			{
				m_isMeleeMode = true;
				m_bossModeTimer = FMath::RandRange(5.0f, 8.0f);
				m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
				m_enemy->PerformTeleportToTarget();
				m_currentState = EAIState::CombatMelee;
			}
			//レーザー準備中は状態を維持してキャンセルしない
			else if (m_currentState != EAIState::LaserPreparation)
			{
				//装備が銃でない場合は射撃Modeに合わせて切り替える
				if (m_enemy->m_currentStyle != EEnemyAttackStyle::Gun) m_enemy->SwitchWeapon(EEnemyAttackStyle::Gun);
				m_currentState = EAIState::CombatRangeMove;
			}
		}
		return;
	}

	//中間ボスが一定時間ごとに距離と直前行動から次の攻撃形式を選び直す処理
	else if (m_enemy->m_enemyRank == EEnemyRank::MiddleBoss)
	{
		//攻撃または武器切り替えの途中で別の行動へ割り込まない
		if (m_enemy->IsAttacking() || m_enemy->m_isSwitchingWeapon) { return; }
		//弾切れ時はテレポートで即近接に切り替える
		if (bOutOfAmmo)
		{
			//射撃Modeから近接Modeへ一度だけ切り替える
			if (!m_isMeleeMode)
			{
				m_isMeleeMode = true;
				m_bossModeTimer = 5.0f;
				m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
				//プレイヤーの近くへ移動して近接戦闘を開始する
				m_enemy->PerformTeleportToTarget();
				m_currentState = EAIState::CombatMelee;
			}
			else { m_bossModeTimer = 5.0f; }
			return;
		}

		m_bossModeTimer -= GetWorld()->GetDeltaSeconds();

		//タイマーが切れたらランダムにスタイルを切り替える
		if (m_bossModeTimer <= 0.f)
		{
			//近接Modeの終了後は射撃またはレーザーを選択する
			if (m_isMeleeMode)
			{

				m_isMeleeMode = false;
				m_bossModeTimer = FMath::RandRange(5.0f, 8.0f);

				//射撃Modeへ移行する際は30パーセントの確率でレーザーを選択する
				if (FMath::RandRange(1, 100) <= 30)
				{
					//テレポート中でなければレーザー準備に入る
					if (!m_enemy->IsTeleporting())
					{
						m_enemy->SwitchWeapon(EEnemyAttackStyle::Laser);
						m_enemy->StartLaserCharge();

						//テレポートは行わない（レーザーとテレポートの同時実行防止）
						m_currentState = EAIState::LaserPreparation;
						m_stateTimer = 1.5f;
					}
				}
				else
				{
					m_enemy->SwitchWeapon(EEnemyAttackStyle::Gun);

					//レーザーを選択しなかった場合はプレイヤーから距離を取る
					if (!m_enemy->IsTeleporting()) { m_enemy->PerformTeleportAwayFromTarget(); }
					m_currentState = EAIState::CombatRangeMove;
				}
			}
			else
			{
				//射撃Modeの終了後はプレイヤーの近くへ移動して近接Modeへ戻す
				m_isMeleeMode = true;
				m_bossModeTimer = FMath::RandRange(4.0f, 6.0f);
				m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
				m_enemy->PerformTeleportToTarget();
				m_currentState = EAIState::CombatMelee;
			}
			return;
		}

		//タイマー中は現在のモードを維持する
		if (m_isMeleeMode)
		{
			//装備が近接武器でない場合は現在Modeに合わせて切り替える
			if (m_enemy->m_currentStyle != EEnemyAttackStyle::Melee) { m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee); }
			//近接モード中は常に近接戦闘状態にする
			m_currentState = EAIState::CombatMelee;
		}
		else
		{
			//プレイヤーが近い場合は近接に切り替えて積極的に攻める
			if (distSq <= FMath::Square(400.f) && m_currentState != EAIState::LaserPreparation)
			{
				m_isMeleeMode = true;
				m_bossModeTimer = FMath::RandRange(4.0f, 6.0f);
				m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
				m_enemy->PerformTeleportToTarget();
				m_currentState = EAIState::CombatMelee;
			}
			else if (m_currentState != EAIState::LaserPreparation)
			{
				//装備が銃でない場合は射撃Modeに合わせて切り替える
				if (m_enemy->m_currentStyle != EEnemyAttackStyle::Gun) { m_enemy->SwitchWeapon(EEnemyAttackStyle::Gun); }
				//遠距離モード中は常に遠距離戦闘状態にする
				m_currentState = EAIState::CombatRangeMove;
			}
		}
		return;
	}

	//通常敵の戦闘状態を決める現在の攻撃Style
	const EEnemyAttackStyle style = m_enemy->m_currentStyle;
	//Adaptive型は距離と弾薬から近接または射撃を選択する
	if (style == EEnemyAttackStyle::Adaptive)
	{
		//プレイヤーが近いか弾切れの場合は近接攻撃
		if (distSq <= FMath::Square(m_adaptiveMeleeDist) && !bOutOfAmmo)
		{
			m_enemy->EquipWeapon(EEnemyAttackStyle::Melee);
			m_currentState = EAIState::CombatMelee;
		}
		else
		{
			//離れている場合は銃攻撃とし隠れ場所を探す
			m_enemy->EquipWeapon(EEnemyAttackStyle::Gun);
			//まだ射撃行動へ入っていない場合だけ移動先を新しく決定する
			if (m_currentState != EAIState::CombatRangeMove && m_currentState != EAIState::CombatRangeHide)
			{
				m_currentState = EAIState::CombatRangeMove;
				//射撃型が移動を開始する際に利用する遮蔽位置
				FVector bestCover;
				//遮蔽物が見つかった場合は射線を切る位置へ移動する
				if (FindCoverSpot(bestCover))
				{
					m_targetCoverPos = bestCover;
					MoveToLocation(m_targetCoverPos);
				}
				else { MoveToActor(m_targetActor, m_enemy->GetAttackRange() * .8f); }
			}
		}
		return;
	}

	//近接型は近接戦闘状態を維持する
	if (m_enemy->m_currentStyle == EEnemyAttackStyle::Melee) { m_currentState = EAIState::CombatMelee; }
	//射撃型は遮蔽位置を探す遠距離戦闘状態を維持する
	else if (m_enemy->m_currentStyle == EEnemyAttackStyle::Gun) { m_currentState = EAIState::CombatRangeMove; }
}

//巡回地点への移動と到着後の待機時間を更新する関数
void AEnemyAIController::UpdatePatrol(float _deltaTime)
{
	//敵が破棄済みの場合は巡回処理を終了する
	if (!m_enemy) { return; }
	//パトロール中は必ず通常スピードに戻す
	m_enemy->RestoreDefaultSpeed();

	//パトロール中はターゲットを見つけても追跡しないため、フォーカスを解除する
	ClearFocus(EAIFocusPriority::Gameplay);

	//移動状態では巡回地点への到着を監視する
	if (m_currentState == EAIState::Move)
	{
		//目的地に到着したら待機状態へ移行する
		if (GetMoveStatus() == EPathFollowingStatus::Idle)
		{
			m_currentState = EAIState::Wait;
			m_stateTimer = 3.0f;
		}
	}
	//待機状態では三秒経過後に次の巡回地点を探す
	else if (m_currentState == EAIState::Wait)
	{
		//待機時間が終了したらランダムな場所へ移動を開始する
		m_stateTimer -= _deltaTime;
		//待機時間が終了したら次の巡回地点を検索する
		if (m_stateTimer <= 0.f)
		{
			//次の巡回地点をNavMesh上から探すナビゲーションシステム
			UNavigationSystemV1 *navSys = UNavigationSystemV1::GetCurrent(GetWorld());
			//NavMeshが有効な場合だけ巡回地点を検索する
			if (navSys)
			{
				//敵の周囲から取得した移動可能な巡回地点
				FNavLocation randomLoc;
				//巡回地点を取得できた場合は移動状態へ戻す
				if (navSys->GetRandomPointInNavigableRadius(m_enemy->GetActorLocation(), 1500.f, randomLoc))
				{
					MoveToLocation(randomLoc.Location);
					m_currentState = EAIState::Move;
				}
			}
		}
	}
}
