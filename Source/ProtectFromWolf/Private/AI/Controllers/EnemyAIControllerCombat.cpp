#include "AI/Controllers/EnemyAIController.h"
#include "Enemy/EnemyChara.h"
#include "TimerManager.h"
#include "Player/PlayerChara.h"
#include "Weapons/EnemyGun.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Spawning/SpawnEnemy.h"
#include "GameFramework/CharacterMovementComponent.h"

//近接距離、攻撃Cooldown、ボス行動から接近と攻撃を更新する関数
void AEnemyAIController::UpdateCombatMelee(float _deltaTime)
{
	//敵本体または攻撃対象を取得できない場合は近接行動を更新しない
	if (!m_enemy || !m_targetActor) { return; }
	//テレポート中は移動命令を出さず待機する
	if (m_enemy->IsTeleporting())
	{
		StopMovement();
		return;
	}

	//雑魚敵以外は武器の準備が完了するまで行動を待つ
	if (m_enemy->m_enemyRank != EEnemyRank::Minion)
	{
		//武器の構えが完了するまでボスの攻撃判断を待機する
		if (!m_enemy->IsWeaponReady()) { return; }
	}

	//カプセルの厚みを考慮して攻撃範囲を計算する
	//近接武器とプレイヤーのカプセルが接触できる有効攻撃距離
	const float effectiveAttackRange = 250.f;
	//平方根計算を避けて攻撃距離を比較する有効距離の二乗
	const float attackRangeSq = FMath::Square(effectiveAttackRange);
	//敵とプレイヤーの現在距離の二乗
	const float distSq = FVector::DistSquared(m_enemy->GetActorLocation(), m_targetActor->GetActorLocation());
	//敵が現在プレイヤーを視認できているか示す変数
	bool bCanSeePlayer = m_enemy->CanSeePlayer();

	//視界に入っていればプレイヤーを注視する
	if (distSq <= FMath::Square(m_enemy->GetChaseRange() * 1.1f)) { SetFocus(m_targetActor); }
	else { ClearFocus(EAIFocusPriority::Gameplay); }

	//攻撃中や武器の切り替え中は移動を停止する
	if (m_enemy->IsAttacking() || m_enemy->m_isSwitchingWeapon)
	{
		StopMovement();
		return;
	}

	//攻撃範囲内に到達した場合の処理
	if (distSq <= attackRangeSq)
	{
		//足を止めてダッシュ状態を解除しプレイヤーの方へ向き直る
		StopMovement();
		m_enemy->RestoreDefaultSpeed();

		//近接攻撃をプレイヤーへ向ける平面方向
		FVector dirToTarget = (m_targetActor->GetActorLocation() - m_enemy->GetActorLocation()).GetSafeNormal2D();
		m_enemy->SetActorRotation(dirToTarget.Rotation());

		//Cooldownが終了しプレイヤーを視認できる時だけ攻撃を開始する
		if (!m_isAttackCooldown && bCanSeePlayer)
		{
			//ラストボスの発狂時は確率で大技であるグランドスマッシュを発動する
			if (m_enemy->m_enemyRank == EEnemyRank::LastBoss && m_enemy->GetBossPhase() == EBossPhase::Phase3 && FMath::RandRange(1, 100) <= 25)
			{
				m_enemy->PerformGroundSmash();
			}
			else
			{
				//通常の近接攻撃
				m_enemy->PerformAttack();
			}

			//攻撃後にクールダウンを設定する
			m_isAttackCooldown = true;
			GetWorld()->GetTimerManager().SetTimer(m_attackCooldownTimerHandle,
				FTimerDelegate::CreateWeakLambda(this, [this]() { m_isAttackCooldown = false; }), 1.5f, false);
		}
	}
	else
	{
		//ボスキャラの場合はスマッシュかテレポートをランダムで使用して接近する
		//接近用の特殊行動を使用できるボスか示す変数
		bool bIsBoss = (m_enemy->m_enemyRank == EEnemyRank::LastBoss || m_enemy->m_enemyRank == EEnemyRank::MiddleBoss);
		//待機中のボスはCooldown終了後に接近用の特殊行動を選択する
		if (bIsBoss && !m_isAttackCooldown && !m_enemy->IsTeleporting() && m_enemy->GetActionState() == EActionState::Idle &&
			!m_enemy->m_isSwitchingWeapon)
		{
			//50%でグランドスマッシュ、50%でテレポートで接近
			if (FMath::RandRange(1, 100) <= 50) { m_enemy->PerformGroundSmash(); }
			else { m_enemy->PerformTeleportToTarget(); }
			//次のスマッシュ/テレポートまでクールダウン
			m_isAttackCooldown = true;
			GetWorld()->GetTimerManager().SetTimer(m_attackCooldownTimerHandle,
				FTimerDelegate::CreateWeakLambda(this, [this]() { m_isAttackCooldown = false; }), 3.5f, false);
		}
		//特殊行動を使えない場合は通常移動で近接距離まで接近する
		else if (!m_enemy->IsTeleporting() && m_enemy->GetActionState() != EActionState::Attacking)
		{
			//通常移動で接近する
			//通常の追跡でボスだけ固定速度へ加速せず、調整済みの移動速度へ戻す
			m_enemy->RestoreDefaultSpeed();
			MoveToActor(m_targetActor, 150.f);
		}
	}
}

//武器の射程、視認状態、遮蔽物から射撃と再配置を更新する関数
void AEnemyAIController::UpdateCombatRange(float _deltaTime)
{
	//ターゲットが存在しない場合は処理を中断する
	if (!m_enemy || !m_targetActor) { return; }

	//テレポート中は移動命令を出さず待機する
	if (m_enemy->IsTeleporting())
	{
		StopMovement();
		return;
	}

	//距離の二乗を計算して、射程距離内かつ視界が通っているかを判定する
	//敵とプレイヤーの現在距離の二乗
	const float distSq = FVector::DistSquared(m_enemy->GetActorLocation(), m_targetActor->GetActorLocation());
	//敵が現在プレイヤーを視認できているか示す変数
	bool bCanSeePlayer = m_enemy->CanSeePlayer();

	//装備中の武器に合わせて決定した有効攻撃距離
	float effectiveAttackRange = m_enemy->GetAttackRange();
	//銃装備時はバースト射撃に適した距離へ上書きする
	if (m_enemy->m_currentStyle == EEnemyAttackStyle::Gun) { effectiveAttackRange = 1800.f; }
	//レーザー装備時は予兆を確認できる長距離を維持する
	else if (m_enemy->m_currentStyle == EEnemyAttackStyle::Laser) { effectiveAttackRange = 2500.f; }

	//弾薬の状況を確認する
	bool bCompletelyOutOfAmmo = false;
	//銃を装備している場合はマガジンと予備弾の両方を確認する
	if (m_enemy->GetCurrentGun())
	{
		bCompletelyOutOfAmmo = (m_enemy->GetCurrentGun()->IsOutOfAmmo() && m_enemy->GetCurrentGun()->GetTotalAmmo() <= 0);
	}

	//完全に弾切れの場合は近接戦闘へ移行する
	if (bCompletelyOutOfAmmo)
	{
		//弾薬を失ったボスは近接武器へ切り替えて戦闘を継続する
		if (m_enemy->m_enemyRank == EEnemyRank::LastBoss || m_enemy->m_enemyRank == EEnemyRank::MiddleBoss)
		{
			m_enemy->SwitchWeapon(EEnemyAttackStyle::Melee);
			m_currentState = EAIState::CombatMelee;
		}
		return;
	}

	//マガジンが空の場合はリロードを実行する
	if (m_enemy->GetCurrentGun() && m_enemy->GetCurrentGun()->IsOutOfAmmo())
	{
		//まだリロードを開始していない場合は射撃を止めてリロードする
		if (!m_enemy->IsReloading())
		{
			ClearFocus(EAIFocusPriority::Gameplay);
			m_enemy->ReloadWeapon();
		}
		//リロード中のラストボスは条件を満たす時だけレーザーへ派生する
		else if (m_enemy->m_enemyRank == EEnemyRank::LastBoss && m_enemy->GetCurrentGun()->GetTotalAmmo() > 0 &&
				 distSq <= FMath::Square(effectiveAttackRange) && !m_doingPostAmmoLaser)
		{
			//弾切れ中のラストボスをレーザー準備状態へ移行する
			m_doingPostAmmoLaser = true;
			m_enemy->SwitchWeapon(EEnemyAttackStyle::Laser);
			m_enemy->StartLaserCharge();
			m_currentState = EAIState::LaserPreparation;
			m_stateTimer = 1.5f;
			return;
		}
	}

	//射程距離内かつ視界が通っている場合の攻撃処理
	if (distSq <= FMath::Square(effectiveAttackRange) && bCanSeePlayer)
	{
		//プレイヤーを注視する
		SetFocus(m_targetActor);

		//射撃方向を補間するためのプレイヤー方向
		FVector dirToTarget = (m_targetActor->GetActorLocation() - m_enemy->GetActorLocation()).GetSafeNormal();
		//Pitchを除外して敵本体へ適用する目標回転
		FRotator targetRot = dirToTarget.Rotation();
		targetRot.Pitch = 0.f;

		//回転を補間してスムーズに向き直す
		m_enemy->SetActorRotation(FMath::RInterpTo(m_enemy->GetActorRotation(), targetRot, _deltaTime, 5.f));

		//射撃可能な状態であれば攻撃を実行する
		if (m_enemy->IsWeaponReady() && !m_enemy->IsAttacking() && !m_enemy->IsReloading())
		{
			//ラストボスの発狂時は確率でレーザー攻撃の準備に入る
			if (m_enemy->m_enemyRank == EEnemyRank::LastBoss &&
				(m_enemy->GetBossPhase() == EBossPhase::Phase3 || m_enemy->GetBossPhase() == EBossPhase::Phase2) && FMath::RandRange(1, 100) <= 25)
			{
				m_enemy->SwitchWeapon(EEnemyAttackStyle::Laser);
				//レーザーの溜めアニメーションと発光予兆を開始する
				m_enemy->StartLaserCharge();
				m_currentState = EAIState::LaserPreparation;
				m_stateTimer = 1.5f;
				return;
			}
			//ボスの場合は確率で散弾攻撃を行う
			else if ((m_enemy->m_enemyRank == EEnemyRank::MiddleBoss || m_enemy->m_enemyRank == EEnemyRank::LastBoss) &&
					 FMath::RandRange(1, 100) <= 20)
			{
				m_enemy->PerformBarrageShot();
			}
			else { m_enemy->PerformAttack(); }
		}

		//射撃後も同じ場所に留まらないように次の遮蔽位置を探す
		if (m_currentState != EAIState::CombatRangeMove)
		{
			//射撃後にプレイヤーの射線を切るために移動する遮蔽位置
			FVector coverSpot;
			//射線を遮る位置を取得できた場合は遮蔽移動を開始する
			if (FindCoverSpot(coverSpot))
			{
				//攻撃を続けながら発見した遮蔽位置へ移動する
				m_targetCoverPos = coverSpot;
				MoveToLocation(m_targetCoverPos);
				m_currentState = EAIState::CombatRangeMove;

				//ボスと雑魚敵で遮蔽物に留まる時間を切り替える
				if (m_enemy->m_enemyRank == EEnemyRank::MiddleBoss || m_enemy->m_enemyRank == EEnemyRank::LastBoss)
				{
					//ボスが遮蔽物に留まり続けないように待機時間を短縮する
					m_stateTimer = m_hideDuration * 0.5f;
				}
				else
				{
					//雑魚敵の遮蔽待機時間を設定する
					m_stateTimer = m_hideDuration;
				}
			}
			else
			{
				//遮蔽物がなければそのまま適切な距離を維持しながら攻撃
				m_currentState = EAIState::CombatRangeHide;
				m_stateTimer = 1.5f;
			}
		}
	}
	//射程距離外または視界が通っていない場合の接近処理
	else if (!bCanSeePlayer)
	{
		//雑魚敵は視界が切れたら射撃を止める
		if (m_enemy->m_enemyRank == EEnemyRank::Minion && m_enemy->IsAttacking()) { m_enemy->StopFiring(); }

		//視界が通っていない場合はリロードしながら接近する
		ClearFocus(EAIFocusPriority::Gameplay);

		//視界を失っている間に弾切れした場合は接近と同時にリロードする
		if (m_enemy->GetCurrentGun() && m_enemy->GetCurrentGun()->IsOutOfAmmo())
		{
			//リロード状態へ移行していない場合だけリロードを開始する
			if (m_enemy->GetActionState() != EActionState::Reloading) { m_enemy->ReloadWeapon(); }
		}
		MoveToActor(m_targetActor, effectiveAttackRange * 0.8f);
	}
	else
	{
		//プレイヤーを視認していても射程外の場合は射撃を止めて有効射程まで接近する
		if (m_enemy->IsAttacking()) { m_enemy->StopFiring(); }
		MoveToActor(m_targetActor, effectiveAttackRange * 0.8f);
	}

	//移動と隠蔽のステート管理
	if (m_currentState == EAIState::CombatRangeMove)
	{
		//遮蔽位置へ到着したら隠蔽状態へ移行する
		if (GetMoveStatus() == EPathFollowingStatus::Idle)
		{
			m_currentState = EAIState::CombatRangeHide;
			//ボスと雑魚敵で遮蔽物に留まる時間を切り替える
			if (m_enemy->m_enemyRank == EEnemyRank::MiddleBoss || m_enemy->m_enemyRank == EEnemyRank::LastBoss)
			{
				m_stateTimer = m_hideDuration * 0.5f;
			}
			else { m_stateTimer = m_hideDuration; }

			//遮蔽物が低い場合はしゃがむ
			if (IsLowCover(m_targetCoverPos)) { m_enemy->Crouch(); }
		}
	}
	//隠蔽状態では待機終了後に次の射撃位置を探す
	else if (m_currentState == EAIState::CombatRangeHide)
	{
		//遮蔽物に隠れている時間が経過したら再び移動して射撃位置を探す
		m_stateTimer -= _deltaTime;
		//隠蔽時間が終了したら次の射撃位置へ再配置する
		if (m_stateTimer <= 0.f)
		{
			//次の射撃位置へ移動できるようにしゃがみを解除する
			m_enemy->UnCrouch();

			//プレイヤーとの間に遮蔽物がある次の射撃位置
			FVector attackPos = FVector::ZeroVector;
			//次の射撃位置をNavMesh上で発見したか示す変数
			bool bFoundPos = false;
			//移動可能な射撃位置を検索するナビゲーションシステム
			UNavigationSystemV1 *navSys = UNavigationSystemV1::GetCurrent(GetWorld());
			//NavMeshが有効な場合だけ次の射撃位置を検索する
			if (navSys)
			{
				//プレイヤーから見えない新しい射撃位置を探す
				for (int i = 0; i < 20; i++)
				{
					//NavMesh上から取得した射撃候補位置
					FNavLocation randomLoc;
					//プレイヤー周辺から移動可能な候補を取得できた場合だけ射線を調べる
					if (navSys->GetRandomPointInNavigableRadius(m_targetActor->GetActorLocation(), 1500.f, randomLoc))
					{
						//候補位置とプレイヤーの間を遮る物体を取得する結果
						FHitResult hit;
						GetWorld()->LineTraceSingleByChannel(hit, randomLoc.Location + FVector(0, 0, 50),
															 m_targetActor->GetActorLocation() + FVector(0, 0, 50), ECC_Visibility);

						//遮蔽物がある場合はその位置を射撃位置として採用する
						if (!hit.bBlockingHit || hit.GetActor() == m_targetActor)
						{
							attackPos = randomLoc.Location;
							bFoundPos = true;
							break;
						}
					}
				}
			}
			//遮蔽物が見つかった場合はそこへ移動する
			if (bFoundPos)
			{
				m_targetCoverPos = attackPos;
				MoveToLocation(m_targetCoverPos);
				m_currentState = EAIState::CombatRangeMove;
			}
			else
			{
				MoveToActor(m_targetActor, effectiveAttackRange * 0.8f);
				m_currentState = EAIState::CombatRangeMove;
			}
		}
	}
}

//レーザーの照準と発射タイミングを更新する関数
void AEnemyAIController::UpdateLaserPreparation(float _deltaTime)
{
	//ターゲットが存在しない場合は処理を中断する
	if (!m_enemy || !m_targetActor) { return; }

	//その場で立ち止まってプレイヤーを確実に狙う
	StopMovement();
	SetFocus(m_targetActor);

	//指定した時間が経過したらレーザーを発射して遠距離モードに戻る
	m_stateTimer -= _deltaTime;
	//レーザーの溜め時間が終了したら攻撃を実行する
	if (m_stateTimer <= 0.f)
	{
		m_enemy->PerformAttack();

		//弾切れ後のレーザー攻撃の場合は銃に戻してリロードへ遷移する
		if (m_doingPostAmmoLaser)
		{
			m_doingPostAmmoLaser = false;

			//レーザーアニメーション終了後に銃に切り替えてリロードする
			//遅延処理から破棄済みControllerを参照しないための弱参照
			TWeakObjectPtr<AEnemyAIController> weakThis(this);
			//レーザー終了後に通常行動へ戻すタイマー
			FTimerHandle postLaserTimer;
			GetWorldTimerManager().SetTimer(
				postLaserTimer,
				[weakThis]()
				{
					//Controllerまたは敵が破棄済みの場合は遅延処理を終了する
					if (!weakThis.IsValid() || !weakThis->m_enemy) { return; }
					//通常射撃へ復帰できるように武器と戦闘状態を戻す
					weakThis->m_enemy->SwitchWeapon(EEnemyAttackStyle::Gun);
					weakThis->m_currentState = EAIState::CombatRangeMove;
				},
				2.0f, false);
		}
		else { m_currentState = EAIState::CombatRangeMove; }
	}
}

//レーザー攻撃後の退避移動を更新する関数
void AEnemyAIController::UpdateRetreat(float _deltaTime)
{
	//ターゲットが存在しない場合は処理を中断する
	if (!m_enemy || !m_targetActor) { return; }

	//退避方向を求める基準となるプレイヤー位置
	const FVector playerLoc = m_targetActor->GetActorLocation();
	//退避開始地点となる敵の現在位置
	const FVector enemyLoc = m_enemy->GetActorLocation();

	//プレイヤーから離れる退避方向
	FVector runDir = (enemyLoc - playerLoc).GetSafeNormal();
	//プレイヤーと反対方向へ確保するレーザー後の退避候補
	FVector retreatPos = enemyLoc + (runDir * 2000.f);

	//退避候補を移動可能な位置へ補正するナビゲーションシステム
	UNavigationSystemV1 *navSys = UNavigationSystemV1::GetCurrent(GetWorld());
	//NavMeshが有効な場合は退避候補を移動可能な位置へ補正する
	if (navSys)
	{
		//退避候補をNavMesh上へ補正した実際の移動先
		FNavLocation validRetreatLoc;
		//退避候補の周辺に移動可能な位置があれば実際の移動先へ採用する
		if (navSys->GetRandomPointInNavigableRadius(retreatPos, 500.f, validRetreatLoc)) { retreatPos = validRetreatLoc.Location; }
	}

	//敵キャラクターを退避位置へ移動させる
	MoveToLocation(retreatPos);
	SetFocus(m_targetActor);

	//退避完了を判定するための二乗距離
	float distSq = FVector::DistSquared(enemyLoc, playerLoc);
	//プレイヤーから十分に離れたら射撃と遮蔽移動へ復帰する
	if (distSq > FMath::Square(2500.f))
	{
		m_enemy->SwitchWeapon(EEnemyAttackStyle::Gun);
		m_currentState = EAIState::CombatRangeMove;

		//プレイヤーの射線を最も安全に遮る位置
		FVector bestCover;
		//退避後に遮蔽物が見つかった場合は次の射撃位置として利用する
		if (FindCoverSpot(bestCover))
		{
			m_targetCoverPos = bestCover;
			MoveToLocation(m_targetCoverPos);
		}
	}
}
