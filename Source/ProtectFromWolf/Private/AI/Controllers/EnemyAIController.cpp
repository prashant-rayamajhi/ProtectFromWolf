#include "AI/Controllers/EnemyAIController.h"
#include "Enemy/EnemyChara.h"
#include "Player/PlayerChara.h"
#include "Weapons/EnemyGun.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Spawning/SpawnEnemy.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BrainComponent.h"
#include "AI/BehaviorTree/Blackboard/EnemyBlackboardKeys.h"
#include "Perception/AISense.h"
#include "Perception/AISense_Hearing.h"
#include "Perception/AISense_Sight.h"
#include "Perception/AISenseConfig_Hearing.h"
#include "Perception/AISenseConfig_Sight.h"

//知覚設定、Behavior Tree、従来AIで使用する初期状態を設定する関数
AEnemyAIController::AEnemyAIController()
	: m_pEnemy(nullptr), m_aiPerception(nullptr), m_sightConfig(nullptr), m_hearingConfig(nullptr), b_mUsingBehaviorTree(false),
	  m_adaptiveMeleeDist(200.f), m_adaptiveRangeBackDist(800.f), m_hideDuration(3.f), m_losCheckTimer(0.f), m_losCheckInterval(0.2f),
	  m_bLastCanSeePlayer(false), m_bWeaponDrawn(false), m_strafeTimer(0.f), m_strafeInterval(2.f), m_currentStrafeDir(0.f),
	  m_bIsAttackCooldown(false), m_bossModeTimer(0.f), m_bIsMeleeMode(false), m_bDoingPostAmmoLaser(false), m_lastPlayerNoiseTime(-BIG_NUMBER),
	  m_lastPlayerNoiseLocation(FVector::ZeroVector), m_lastVisualContactTime(-BIG_NUMBER), m_lastVisualContactLocation(FVector::ZeroVector),
	  b_mHasActiveVisualContact(false), m_btPerceptionRefreshTime(0.f), m_btIdleRecoveryTime(0.f)
{
	//AI Controllerを毎フレーム更新できるようにする
	PrimaryActorTick.bCanEverTick = true;
	//プレイヤーの視覚情報を取得する知覚設定を構築する
	m_aiPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("EnemyPerception"));
	SetPerceptionComponent(*m_aiPerception);
	m_sightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("SightConfig"));
	m_sightConfig->SightRadius = 5500.f;
	m_sightConfig->LoseSightRadius = 6500.f;
	m_sightConfig->PeripheralVisionAngleDegrees = 82.f;
	m_sightConfig->SetMaxAge(5.f);
	m_sightConfig->DetectionByAffiliation.bDetectEnemies = true;
	m_sightConfig->DetectionByAffiliation.bDetectFriendlies = true;
	m_sightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	//銃声と足音を取得する聴覚設定を構築する
	m_hearingConfig = CreateDefaultSubobject<UAISenseConfig_Hearing>(TEXT("HearingConfig"));
	m_hearingConfig->HearingRange = 6500.f;
	m_hearingConfig->SetMaxAge(6.f);
	m_hearingConfig->DetectionByAffiliation.bDetectEnemies = true;
	m_hearingConfig->DetectionByAffiliation.bDetectFriendlies = true;
	m_hearingConfig->DetectionByAffiliation.bDetectNeutrals = true;
	m_aiPerception->ConfigureSense(*m_sightConfig);
	m_aiPerception->ConfigureSense(*m_hearingConfig);
	m_aiPerception->SetDominantSense(UAISense_Sight::StaticClass());
	m_aiPerception->OnTargetPerceptionUpdated.AddDynamic(this, &AEnemyAIController::HandleTargetPerceptionUpdated);
	//全ランクの敵が共有するBehavior Tree Assetを登録する
	m_behaviorTreeAsset = TSoftObjectPtr<UBehaviorTree>(FSoftObjectPath(TEXT("/Game/Enemy/AI/BehaviorTree/BT_Enemy.BT_Enemy")));

	//最初はパトロール状態から開始する
	m_currentState = EAIState::Move;
	m_stateTimer = 0.f;
}

//敵をPossessした時に移動設定、攻撃対象、Behavior Treeを初期化する関数
void AEnemyAIController::OnPossess(APawn *_pawn)
{
	//親クラスへPossess開始を通知する
	Super::OnPossess(_pawn);

	//操作対象とプレイヤーの参照を取得する
	m_pEnemy = Cast<AEnemyChara>(_pawn);
	m_targetActor = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	//知覚コンポーネントへ現在の刺激を再取得させる
	if (m_aiPerception) m_aiPerception->RequestStimuliListenerUpdate();

	//敵のCharacter MovementをAI移動向けの回転設定へ変更する
	if (m_pEnemy && m_pEnemy->GetCharacterMovement())
	{
		//ナビゲーション上でしゃがみ移動を許可する
		UCharacterMovementComponent *movement = m_pEnemy->GetCharacterMovement();
		movement->NavAgentProps.bCanCrouch = true;
		movement->bOrientRotationToMovement = true;
		movement->bUseControllerDesiredRotation = false;
		movement->RotationRate = FRotator(0.f, 540.f, 0.f);
		m_pEnemy->bUseControllerRotationYaw = false;
	}

	//ボス戦における初期モードを遠距離攻撃に設定しランダムな時間を割り当てる
	m_bossModeTimer = FMath::RandRange(2.0f, 6.0f);
	m_bIsMeleeMode = false;

	//敵へ実行するBehavior Tree Asset
	UBehaviorTree *behaviorTree = m_behaviorTreeAsset.LoadSynchronous();
	b_mUsingBehaviorTree = behaviorTree && RunBehaviorTree(behaviorTree);
}

//プレイヤーの物音位置を記憶してBlackboardの攻撃対象を更新する関数
void AEnemyAIController::NotifyPlayerNoise(AActor *_player, const FVector &_noiseLocation)
{
	//プレイヤーまたはWorldを取得できない場合は物音を登録しない
	if (!IsValid(_player) || !GetWorld()) { return; }

	m_targetActor = _player;
	m_lastPlayerNoiseLocation = _noiseLocation;
	m_lastPlayerNoiseTime = GetWorld()->GetTimeSeconds();
	//Blackboardがある場合は物音を出したプレイヤーを追跡対象へ設定する
	if (UBlackboardComponent *blackboard = GetBlackboardComponent())
	{
		blackboard->SetValueAsObject(TEXT("TargetActor"), _player);
		blackboard->SetValueAsBool(TEXT("CanSeeTarget"), true);
	}
}

//戦闘空間へ入ったプレイヤーを即座に認識してBehavior Treeを再開する関数
void AEnemyAIController::PrimeForArenaCombat(AActor *_player)
{
	//プレイヤーまたは敵本体を取得できない場合は戦闘を開始しない
	if (!IsValid(_player) || !m_pEnemy) { return; }
	NotifyPlayerNoise(_player, _player->GetActorLocation());
	//戦闘開始時点でプレイヤーを直接視認できるか示す変数
	const bool b_directSight = LineOfSightTo(_player);
	UpdateVisualContact(_player, b_directSight, _player->GetActorLocation());
	m_pEnemy->SetCanSeePlayer(b_directSight);
	//Blackboardへ攻撃対象を設定して初回判断の遅延を防ぐ
	if (UBlackboardComponent *blackboard = GetBlackboardComponent())
	{
		blackboard->SetValueAsObject(EnemyBlackboardKeys::TargetActor, _player);
		blackboard->SetValueAsBool(EnemyBlackboardKeys::CanSeeTarget, true);
	}
	//待機中のBehavior Treeを再開して戦闘判断を即時評価する
	if (UBrainComponent *brain = GetBrainComponent()) brain->RestartLogic();
}

//指定秒数以内にプレイヤーの物音を検知したか判定する関数
bool AEnemyAIController::HasRecentPlayerNoise(float _memorySeconds) const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() - m_lastPlayerNoiseTime <= _memorySeconds;
}

//プレイヤーの視認状態と最後に確認した位置を更新する関数
void AEnemyAIController::UpdateVisualContact(AActor *_player, bool _canSee, const FVector &_location)
{
	//プレイヤーまたはWorldを取得できない場合は視覚記憶を変更しない
	if (!IsValid(_player) || !GetWorld()) { return; }
	m_targetActor = _player;
	b_mHasActiveVisualContact = _canSee;
	//直接視認できた時だけ視認時刻と位置を最新値へ更新する
	if (_canSee)
	{
		m_lastVisualContactTime = GetWorld()->GetTimeSeconds();
		m_lastVisualContactLocation = _location;
	}
}

//指定秒数以内にプレイヤーを直接視認したか判定する関数
bool AEnemyAIController::HasRecentVisualContact(float _memorySeconds) const
{
	return GetWorld() && GetWorld()->GetTimeSeconds() - m_lastVisualContactTime <= _memorySeconds;
}

//物音と視覚のうち新しい方から最後に判明したプレイヤー位置を取得する関数
FVector AEnemyAIController::GetLastKnownPlayerLocation() const
{
	return m_lastPlayerNoiseTime > m_lastVisualContactTime ? m_lastPlayerNoiseLocation : m_lastVisualContactLocation;
}

//知覚した刺激の種類に応じて視覚記憶または物音記憶を更新する関数
void AEnemyAIController::HandleTargetPerceptionUpdated(AActor *_actor, FAIStimulus _stimulus)
{
	//プレイヤー以外の刺激は攻撃対象の認識へ使用しない
	if (!IsValid(_actor) || _actor != UGameplayStatics::GetPlayerPawn(GetWorld(), 0)) { return; }

	//視覚刺激の場合は現在の視認成否を更新する
	if (_stimulus.Type == UAISense::GetSenseID<UAISense_Sight>())
	{
		UpdateVisualContact(_actor, _stimulus.WasSuccessfullySensed(), _actor->GetActorLocation());
	}
	//聴覚刺激を正常に受けた場合は刺激位置を物音記憶へ登録する
	else if (_stimulus.Type == UAISense::GetSenseID<UAISense_Hearing>() && _stimulus.WasSuccessfullySensed())
	{
		NotifyPlayerNoise(_actor, _stimulus.StimulusLocation);
	}
}

//毎フレーム呼ばれる更新処理を行う関数
void AEnemyAIController::Tick(float _deltaTime)
{
	//親クラスのTick処理を実行する
	Super::Tick(_deltaTime);
	//Behavior Tree使用中は停止監視だけを更新して従来State処理を実行しない
	if (b_mUsingBehaviorTree)
	{
		TickBehaviorTreeSafety(_deltaTime);
		return;
	}

	//敵本体または攻撃対象を取得できない場合は行動を更新しない
	if (!m_pEnemy || !m_targetActor) { return; }

	//敵が倒されて消滅している場合は、AIコントローラー自身も破棄して処理を止める
	if (!IsValid(m_pEnemy))
	{
		Destroy();
		return;
	}

	//フェーズ表示中は移動と攻撃を止めて演出完了を待つ
	if (ASpawnEnemy::IsPhaseDisplaying())
	{
		StopMovement();
		//攻撃中の場合はフェーズ表示へ弾が残らないよう射撃を停止する
		if (m_pEnemy->IsAttacking()) { m_pEnemy->StopFiring(); }
		return;
	}

	//一定間隔ごとに視界のチェックを行う
	m_losCheckTimer += _deltaTime;
	//設定した更新間隔に達した時だけ射線判定を実行する
	if (m_losCheckTimer >= m_losCheckInterval)
	{
		m_losCheckTimer = 0.f;
		CheckLineOfSight();
	}

	//武器の表示非表示や構え状態を更新する
	UpdateWeaponState();

	//敵の体力が減り退避条件を満たした場合の処理
	if (m_pEnemy->ShouldRetreat() && m_currentState != EAIState::Retreat) { m_currentState = EAIState::Retreat; }

	//現在の状況に応じてAIの状態を決定する
	UpdateStateLogic();

	//決定された状態に基づいて具体的な行動を実行する
	switch (m_currentState)
	{
	case EAIState::Move:
	case EAIState::Wait:
		UpdatePatrol(_deltaTime);
		break;
	case EAIState::CombatMelee:
		UpdateCombatMelee(_deltaTime);
		break;
	case EAIState::CombatRangeMove:
	case EAIState::CombatRangeHide:
		UpdateCombatRange(_deltaTime);
		break;
	case EAIState::Retreat:
		UpdateRetreat(_deltaTime);
		break;
	case EAIState::LaserPreparation:
		UpdateLaserPreparation(_deltaTime);
		break;
	}
}

//Behavior Treeが停止した敵を戦闘へ復帰させる関数
void AEnemyAIController::TickBehaviorTreeSafety(float _deltaTime)
{
	//敵が無効またはフェーズ表示中の場合は停止監視時間を初期化する
	if (!IsValid(m_pEnemy) || ASpawnEnemy::IsPhaseDisplaying())
	{
		m_btIdleRecoveryTime = 0.f;
		return;
	}
	//攻撃対象を失った場合は現在のプレイヤーPawnを再取得する
	if (!IsValid(m_targetActor)) { m_targetActor = UGameplayStatics::GetPlayerPawn(GetWorld(), 0); }
	//再取得後も攻撃対象が無効な場合は停止監視を終了する
	if (!IsValid(m_targetActor)) { return; }

	m_btPerceptionRefreshTime += _deltaTime;
	//知覚の取りこぼしを防ぐため0.5秒間隔で視認状態を再確認する
	if (m_btPerceptionRefreshTime >= 0.5f)
	{
		m_btPerceptionRefreshTime = 0.f;
		//知覚コンポーネントへ現在の刺激を再取得させる
		if (m_aiPerception) m_aiPerception->RequestStimuliListenerUpdate();
		//直接視認できる場合は視覚記憶とBlackboardを即時更新する
		if (LineOfSightTo(m_targetActor))
		{
			UpdateVisualContact(m_targetActor, true, m_targetActor->GetActorLocation());
			m_pEnemy->SetCanSeePlayer(true);
			//Blackboardへ現在のプレイヤーと視認状態を書き戻す
			if (UBlackboardComponent *blackboard = GetBlackboardComponent())
			{
				blackboard->SetValueAsObject(EnemyBlackboardKeys::TargetActor, m_targetActor);
				blackboard->SetValueAsBool(EnemyBlackboardKeys::CanSeeTarget, true);
			}
		}
	}

	//視覚または物音からプレイヤー位置を把握しているか示す変数
	const bool b_combatAware = HasActiveVisualContact() || HasRecentVisualContact() || HasRecentPlayerNoise();
	//停止中の敵とプレイヤーの平面距離
	const float targetDistance = FVector::Dist2D(m_pEnemy->GetActorLocation(), m_targetActor->GetActorLocation());
	//現在距離では攻撃せず移動する必要があるか示す変数
	const bool b_needsMovement = targetDistance > FMath::Max(m_pEnemy->GetAttackRange() * 1.15f, 320.f);
	//認識中にもかかわらず移動経路を持たず待機しているか示す変数
	const bool b_idleWithoutPath = b_combatAware && m_pEnemy->GetActionState() == EActionState::Idle &&
								   GetMoveStatus() != EPathFollowingStatus::Moving &&
								   (m_pEnemy->m_EnemyRank != EEnemyRank::Minion || b_needsMovement);
	//正常に行動中の場合は停止監視時間を初期化する
	if (!b_idleWithoutPath)
	{
		m_btIdleRecoveryTime = 0.f;
		return;
	}

	m_btIdleRecoveryTime += _deltaTime;
	//三秒未満の一時的な待機は異常停止として扱わない
	if (m_btIdleRecoveryTime < 3.f) { return; }
	m_btIdleRecoveryTime = 0.f;
	//停止が三秒続いた場合はBehavior Treeを再評価して行動を復帰させる
	if (UBrainComponent *brain = GetBrainComponent()) brain->RestartLogic();
}

//敵ランクごとの視認距離と遮蔽物からプレイヤーを視認できるか判定する関数
bool AEnemyAIController::CheckLineOfSight()
{
	//敵本体または攻撃対象を取得できない場合は視認失敗として返す
	if (!m_pEnemy || !m_targetActor) { return false; }
	//視界距離と遮蔽物判定の両方を満たしたか示す変数
	bool bCanSee = false;

	//ボスの場合は視野角を無視して全方位かつ長距離の索敵判定を行う
	if (m_pEnemy->m_EnemyRank == EEnemyRank::MiddleBoss || m_pEnemy->m_EnemyRank == EEnemyRank::LastBoss)
	{
		//ボスとプレイヤーの現在距離
		float distanceToPlayer = FVector::Dist(m_pEnemy->GetActorLocation(), m_targetActor->GetActorLocation());
		//敵ランクと現在状態から決定する最大視認距離
		float maxSightDistance = 0.f;

		//中間ボスは最低二千Unitの視認距離を確保する
		if (m_pEnemy->m_EnemyRank == EEnemyRank::MiddleBoss)
		{
			//中間ボスの索敵距離設定
			maxSightDistance = FMath::Max(2000.f, m_pEnemy->GetChaseRange());
		}
		else
		{
			//ラストボスの索敵距離設定
			maxSightDistance = m_pEnemy->GetChaseRange() * 1.5f;
		}

		//プレイヤーが視認距離内にいる場合だけ遮蔽物をLine Traceで確認する
		if (distanceToPlayer <= maxSightDistance)
		{
			//敵の視点からプレイヤーまでの遮蔽物を受け取る結果
			FHitResult HitResult;
			//自身を射線判定から除外するためのCollision Query設定
			FCollisionQueryParams CollisionParams;
			CollisionParams.AddIgnoredActor(m_pEnemy);

			//目線の高さ同士で間に障害物がないかレイトレースで確認する
			//敵の目線高さから開始する射線位置
			FVector Start = m_pEnemy->GetActorLocation() + FVector(0, 0, 80.f);
			//プレイヤーの目線高さを狙う射線終端
			FVector End = m_targetActor->GetActorLocation() + FVector(0, 0, 80.f);

			//敵とプレイヤーの間に遮蔽物があるか示す変数
			bool bHit = GetWorld()->LineTraceSingleByChannel(HitResult, Start, End, ECC_Visibility, CollisionParams);

			//障害物がないかプレイヤー自身に当たった場合は見えていると判定する
			if (!bHit || HitResult.GetActor() == m_targetActor) { bCanSee = true; }
		}
	}
	else
	{
		//雑魚敵の場合は通常の視野角に基づく判定を行う
		bCanSee = LineOfSightTo(m_targetActor);
	}

	//視界の状態が切り替わった場合のみ処理を実行する
	if (bCanSee != m_bLastCanSeePlayer)
	{
		m_pEnemy->SetCanSeePlayer(bCanSee);
		m_bLastCanSeePlayer = bCanSee;

		//見失った場合は射撃を停止し視線を外す
		if (!bCanSee)
		{
			m_pEnemy->StopFiring();
			ClearFocus(EAIFocusPriority::Gameplay);
		}
	}

	return bCanSee;
}

//プレイヤーの視認状態と距離からボスの武器を構えるか収納するか判断する関数
void AEnemyAIController::UpdateWeaponState()
{
	//敵本体または攻撃対象を取得できない場合は武器状態を変更しない
	if (!m_pEnemy || !m_targetActor) { return; }
	//ミニオンの武器状態はBehavior Tree側で管理するため処理しない
	if (m_pEnemy->m_EnemyRank == EEnemyRank::Minion) { return; }
	//リロード中はモンタージュと武器Socketを維持するため処理しない
	if (m_pEnemy->IsReloading()) { return; }
	//武器を構える範囲判定に使用するプレイヤーまでの距離二乗
	const float distSq = FVector::DistSquared(m_pEnemy->GetActorLocation(), m_targetActor->GetActorLocation());
	//平方根計算を避けて比較する追跡距離の二乗
	const float chaseSq = FMath::Square(m_pEnemy->GetChaseRange());
	//敵が現在プレイヤーを視認できているか示す変数
	bool bCanSee = m_pEnemy->CanSeePlayer();

	//プレイヤーが見えていて追跡範囲内の場合は武器を構える
	if (bCanSee && distSq <= chaseSq)
	{
		//未装備または収納中の場合だけ武器を構える動作を開始する
		if (!m_bWeaponDrawn && (m_pEnemy->GetWeaponState() == EWeaponState::Holstered || m_pEnemy->GetWeaponState() == EWeaponState::Holstering))
		{
			m_pEnemy->DrawWeapon();
			m_bWeaponDrawn = true;
		}
	}
	//プレイヤーが遠すぎるか見失った場合は武器をしまう
	else if (distSq > chaseSq * 1.5f || !bCanSee)
	{
		//攻撃とリロードの途中では武器収納を開始しない
		if (!m_pEnemy->IsAttacking() && !m_pEnemy->IsReloading())
		{
			//ボスだけが非戦闘時の武器収納アニメーションを使用する
			if (m_pEnemy->m_EnemyRank != EEnemyRank::Minion)
			{
				//武器が構え済みまたは構え途中の場合だけ収納候補として扱う
				if (m_bWeaponDrawn && (m_pEnemy->GetWeaponState() == EWeaponState::Ready || m_pEnemy->GetWeaponState() == EWeaponState::Drawing))
				{
					//構え動作中のSocket切替と競合しない時だけ収納を開始する
					if (m_pEnemy->GetWeaponState() != EWeaponState::Drawing)
					{
						m_pEnemy->HolsterWeapon();
						m_bWeaponDrawn = false;
					}
				}
			}
		}
	}
}
