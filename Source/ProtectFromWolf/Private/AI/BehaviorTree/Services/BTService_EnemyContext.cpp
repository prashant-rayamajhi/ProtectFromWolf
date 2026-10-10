#include "AI/BehaviorTree/Services/BTService_EnemyContext.h"

#include "AI/BehaviorTree/Blackboard/EnemyBlackboardKeys.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Enemy/Components/EnemyDecisionComponent.h"
#include "Enemy/Components/EnemyCombatMemoryComponent.h"
#include "Enemy/Components/EnemyCoverComponent.h"
#include "Enemy/EnemyChara.h"
#include "Weapons/EnemyGun.h"
#include "Player/PlayerChara.h"
#include "Kismet/GameplayStatics.h"
#include "Spawning/SpawnEnemy.h"
#include "AIController.h"
#include "AI/Controllers/EnemyAIController.h"
#include "Navigation/PathFollowingComponent.h"

//敵のコンテキスト更新間隔、ボスの視認範囲倍率、戦闘アリーナの認識範囲の定義
namespace
{
//Blackboardへ認識情報を書き戻す更新間隔
constexpr float ContextUpdateInterval = 0.2f;
//ボスが雑魚敵より遠くのプレイヤーを認識するための視界倍率
constexpr float BossSightRangeMultiplier = 1.5f;
//戦闘空間内の敵へプレイヤー位置を共有する最大距離
constexpr float CombatArenaAwarenessRange = 6000.f;
}

//敵の認識情報をBlackboardへ更新する間隔とサービス名を初期化する関数
UBTService_EnemyContext::UBTService_EnemyContext()
{
	//サービスの名前、更新間隔、ランダム偏差、ティック通知を設定する
	NodeName = TEXT("Update Enemy Context");
	Interval = ContextUpdateInterval;
	RandomDeviation = 0.f;
	bNotifyTick = true;
}

//Blackboardへ距離、射線、体力、攻撃可否を定期更新し、Behavior Treeの判断材料を揃える関数
void UBTService_EnemyContext::TickNode(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory, float _deltaSeconds)
{
	//親クラスのTickNodeを呼び出す
	Super::TickNode(_ownerComp, _nodeMemory, _deltaSeconds);

	//AIControllerを取得し、敵キャラクターとBlackboardコンポーネントを取得する
	AAIController *controller = _ownerComp.GetAIOwner();
	AEnemyChara *enemy = controller ? Cast<AEnemyChara>(controller->GetPawn()) : nullptr;
	UBlackboardComponent *blackboard = _ownerComp.GetBlackboardComponent();
	//敵本体またはBlackboardを取得できない場合は認識情報を更新しない
	if (!enemy || !blackboard) { return; }

	//Blackboardからターゲットアクターを取得する。ターゲットが存在しない場合、プレイヤーをターゲットとして設定する
	AActor *targetActor = Cast<AActor>(blackboard->GetValueAsObject(EnemyBlackboardKeys::TargetActor));
	//Blackboardに攻撃対象がない場合はプレイヤーPawnから参照を補う
	if (!targetActor)
	{
		enemy->StopCombatIdleAnimation();
		targetActor = UGameplayStatics::GetPlayerPawn(enemy->GetWorld(), 0);
		blackboard->SetValueAsObject(EnemyBlackboardKeys::TargetActor, targetActor);
	}

	//敵がガンスタイルで攻撃中かつ弾切れの場合、リロードを開始する
	const bool isPaused = ASpawnEnemy::IsPhaseDisplaying();
	//武器の取り出しと収納の途中に遮蔽物待機を割り込ませない
	const bool changingWeapon = enemy->GetWeaponState() == EWeaponState::Drawing || enemy->GetWeaponState() == EWeaponState::Holstering;
	AEnemyGun *gun = enemy->GetCurrentGun();
	//銃の弾切れ中はプレイヤーを認識していても射撃可能距離として扱わない
	if (enemy->m_currentStyle == EEnemyAttackStyle::Gun && gun && gun->IsOutOfAmmo() && gun->GetTotalAmmo() > 0 && enemy->IsAttacking())
	{
		enemy->StopFiring();
		enemy->SetActionState(EActionState::Idle);
	}

	//Blackboardに敵の状態を更新する
	blackboard->SetValueAsBool(EnemyBlackboardKeys::IsPaused, isPaused);
	blackboard->SetValueAsBool(EnemyBlackboardKeys::IsDead, enemy->GetHealthRatio() <= 0.f);
	blackboard->SetValueAsBool(EnemyBlackboardKeys::ShouldRetreat, enemy->ShouldRetreat());
	blackboard->SetValueAsBool(EnemyBlackboardKeys::IsActionLocked,
		enemy->IsAttacking() || enemy->IsReloading() || enemy->m_isSwitchingWeapon || enemy->IsTeleporting() || enemy->IsKnockedBack() ||
		enemy->GetWeaponState() == EWeaponState::Drawing || enemy->GetWeaponState() == EWeaponState::Holstering);
	//演出停止中と死亡後には、遮蔽物移動や装備変更を再開しない
	if (isPaused || enemy->GetHealthRatio() <= 0.f || enemy->IsTeleporting() || enemy->IsKnockedBack()) { return; }

	//ターゲットが存在しない場合、敵はプレイヤーを視認できず、攻撃範囲にも入っていないと判断する
	if (!targetActor)
	{
		enemy->SetCanSeePlayer(false);
		blackboard->SetValueAsBool(EnemyBlackboardKeys::CanSeeTarget, false);
		blackboard->SetValueAsBool(EnemyBlackboardKeys::IsInAttackRange, false);
		return;
	}

	//ターゲットまでの距離を計算し、敵の視認範囲を決定する。戦闘アリーナ内の敵の場合、特別な認識範囲を使用する
	const float targetDistance = FVector::Distance(enemy->GetActorLocation(), targetActor->GetActorLocation());
	float sightRange = enemy->m_enemyRank == EEnemyRank::Minion ? enemy->GetChaseRange() : enemy->GetChaseRange() * BossSightRangeMultiplier;

	//戦闘アリーナ内の敵かどうかを判定するため、敵のタグを確認する。タグが "CombatRoom_"
	//で始まる場合、戦闘アリーナ内の敵とみなす。
	const bool isCombatArenaEnemy =
		enemy->Tags.ContainsByPredicate([](const FName &_tag) { return _tag.ToString().StartsWith(TEXT("CombatRoom_")); });

	//戦闘アリーナ内の敵の場合、視認範囲を戦闘アリーナの認識範囲に拡張する
	if (isCombatArenaEnemy) { sightRange = FMath::Max(sightRange, CombatArenaAwarenessRange); }

	//ターゲットが視認範囲内にあり、かつ敵のコントローラーがターゲットに対して直接視線を持っているかどうかを判定する
	const AEnemyAIController *sightController = Cast<AEnemyAIController>(controller);
	const bool hasDirectLineOfSight = sightController && sightController->CanObserveTarget(targetActor);

	//戦闘アリーナ内の敵で、ターゲットが戦闘アリーナの認識範囲内にある場合、戦闘アリーナ認識を持っていると判断する
	const bool hasArenaAwareness = isCombatArenaEnemy && targetDistance <= CombatArenaAwarenessRange;

	//戦闘アリーナ認識を持っており、かつターゲットが近距離（900ユニット以内）にある場合、近距離認識を持っていると判断する
	const bool hasCloseRangeAwareness = hasArenaAwareness && targetDistance <= 900.f;

	//敵のコントローラーがAEnemyAIControllerである場合、視覚的接触情報を更新する
	AEnemyAIController *enemyController = Cast<AEnemyAIController>(controller);
	//直接視認できたプレイヤー位置をAI Controllerの視覚記憶へ保存する
	if (enemyController)
	{
		//敵のコントローラーに対して、ターゲットアクターとの視覚的接触情報を更新する
		enemyController->UpdateVisualContact(targetActor, hasDirectLineOfSight, targetActor->GetActorLocation());
	}

	//敵がターゲットを直接視認できるか、または敵のコントローラーがターゲットに対して視覚的接触情報を持っているかを判定する
	const bool canSeeTarget = hasDirectLineOfSight;

	//敵の戦闘記憶コンポーネントが存在する場合、ターゲットの情報を観測して戦闘記憶を更新する
	if (enemy->m_combatMemoryComponent)
	{
		enemy->m_combatMemoryComponent->ObservePlayer(Cast<APlayerChara>(targetActor), targetDistance, canSeeTarget, _deltaSeconds);
	}
	//召喚中も知覚は更新するが、遮蔽物への移動や待機で演出を中断しない
	if (enemy->IsSummoning()) { return; }

	//敵がガンスタイルで攻撃中かつカバーを使用している場合、カバーの状態を更新し、必要に応じて移動や停止を行う
	//接近されたボスは射撃位置への移動を打ち切り、近接への切替を許可する
	if (enemy->m_currentStyle != EEnemyAttackStyle::Gun && canSeeTarget && targetDistance < 450.f &&
		enemy->m_coverComponent && enemy->m_coverComponent->IsUsingCover())
	{
		enemy->m_coverComponent->FinishCoverUse();
	}
	const bool isUsingRangedCover =
		enemy->m_coverComponent && enemy->m_coverComponent->IsUsingCover();

	//カバーを使用している場合、カバーの状態を更新し、必要に応じて移動や停止を行う
	if (!changingWeapon && enemy->m_currentStyle == EEnemyAttackStyle::Gun && enemy->m_coverComponent &&
		(canSeeTarget || isUsingRangedCover || (enemyController && enemyController->HasCombatAwareness())))
	{
		enemy->m_coverComponent->UpdateRangedCombat(targetActor, canSeeTarget);
	}
	if (!changingWeapon && !enemy->IsAttacking() && isUsingRangedCover && enemy->m_currentStyle != EEnemyAttackStyle::Gun)
	{
		//カバーの状態を更新し、カバーに到達していない場合は移動を開始する
		if (!enemy->m_coverComponent->UpdateAndIsAtCover())
		{
			//カバーに到達していない場合、移動を開始する
			if (controller->GetMoveStatus() != EPathFollowingStatus::Moving)
			{
				//カバー移動要求をリセットし、カバー移動要求がある場合はカバーの目的地に移動する
				enemy->m_coverComponent->ResetCoverMoveRequest();

				//カバー移動要求がある場合、カバーの目的地に移動する
				if (enemy->m_coverComponent->ConsumeCoverMoveRequest())
				{
					//カバーの目的地に移動する
					const EPathFollowingRequestResult::Type moveResult =
						controller->MoveToLocation(enemy->m_coverComponent->GetCoverDestination(), 35.f, false, true, true, true);

					//移動要求が失敗した場合、カバーの使用を終了する
					if (moveResult == EPathFollowingRequestResult::Failed) { enemy->m_coverComponent->FinishCoverUse(); }
				}
			}
		}
		//カバーに到達している場合、リロード中またはカバー保持中の場合は移動を停止し、必要に応じて武器を下ろす
		else if (enemy->IsReloading() || enemy->m_coverComponent->ShouldHoldCover())
		{
			//カバーに到達している場合、リロード中またはカバー保持中の場合は移動を停止する
			controller->StopMovement();

			//カバー保持中でリロード中でない場合、武器を下ろす
			if (!enemy->IsReloading())
			{
				controller->ClearFocus(EAIFocusPriority::Gameplay);
				enemy->StopFiring();
				enemy->LowerWeapon();
				enemy->SetActionState(EActionState::Idle);
			}
		}
		//カバーに到達しており、リロード中でもカバー保持中でもない場合、カバーの使用を終了する
		else if (enemy->m_currentStyle == EEnemyAttackStyle::Melee || enemy->m_coverComponent->IsPeeking() ||
			!enemy->m_coverComponent->BeginPeek(targetActor))
		{
			enemy->m_coverComponent->FinishCoverUse();
		}
	}

	//敵がターゲットを視認できるか、最近視覚的接触情報を持っているか、戦闘アリーナ認識を持っているか、音の認識を持っているか、またはカバーを使用している場合、戦闘認識を持っていると判断する
	const bool hasCombatAwareness = canSeeTarget || (enemyController && enemyController->HasCombatAwareness()) || isUsingRangedCover;
	//敵の視認状態、ターゲットまでの距離、攻撃範囲、戦闘認識をBlackboardに更新する
	enemy->SetCanSeePlayer(canSeeTarget);
	//武器の変更後に射程を算出し、前の武器の射程で攻撃を選ぶことを防ぐ
	if (canSeeTarget && enemy->m_decisionComponent) { enemy->m_decisionComponent->UpdateDecision(_deltaSeconds, targetDistance); }
	const float effectiveAttackRange = enemy->GetEffectiveAttackRange(targetActor);
	//近接の分岐と攻撃直前で同じ間合いを使い、高低差による判定の食い違いを防ぐ
	const bool inRange = enemy->m_currentStyle == EEnemyAttackStyle::Melee
		? enemy->IsTargetWithinMeleeStrikeRange(targetActor) : targetDistance <= effectiveAttackRange;

	//Blackboardにターゲットまでの距離、戦闘認識、攻撃範囲内かどうかを更新する
	blackboard->SetValueAsFloat(EnemyBlackboardKeys::TargetDistance, targetDistance);
	blackboard->SetValueAsBool(EnemyBlackboardKeys::CanSeeTarget, hasCombatAwareness);
	blackboard->SetValueAsBool(EnemyBlackboardKeys::IsInAttackRange,
							   (canSeeTarget || (isUsingRangedCover && hasDirectLineOfSight) ||
														   (hasCloseRangeAwareness && hasDirectLineOfSight)) &&
														  inRange);

	//敵の意思決定コンポーネントが存在する場合、ターゲットまでの距離を基に意思決定を更新する

	//敵の戦闘アイドルアニメーションを更新する
	enemy->UpdateCombatIdleAnimation();
}
