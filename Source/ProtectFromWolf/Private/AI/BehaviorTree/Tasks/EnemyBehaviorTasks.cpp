#include "AI/BehaviorTree/Tasks/EnemyBehaviorTasks.h"

#include "AI/BehaviorTree/Blackboard/EnemyBlackboardKeys.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "Enemy/Components/EnemyDecisionComponent.h"
#include "Enemy/Components/EnemyCombatMemoryComponent.h"
#include "Enemy/Components/EnemyCoverComponent.h"
#include "Enemy/EnemyChara.h"
#include "Weapons/EnemyGun.h"
#include "NavigationSystem.h"
#include "AIController.h"
#include "AI/Controllers/EnemyAIController.h"
#include "Navigation/PathFollowingComponent.h"
#include "EngineUtils.h"

//敵の行動タスクに関する定数、列挙型、構造体、関数を定義する
namespace
{
//敵のカプセル半径から移動完了距離を求める倍率
constexpr float TargetAcceptanceRadiusMultiplier = 0.8f;
//巡回地点を現在位置から探索する標準半径
constexpr float DefaultPatrolRadius = 1500.f;
//巡回地点へ到着した敵を待機させる秒数
constexpr float PatrolIdleDuration = 3.f;
//同じ場所で巡回し続けないために確保する最低移動距離
constexpr float MinPatrolTravelDistance = 500.f;
//直前の巡回地点を連続して選ばないための最低距離
constexpr float MinPatrolRepeatDistance = 650.f;
//複数の敵が同じ巡回地点へ集まらないための最低距離
constexpr float MinPatrolPeerDistance = 325.f;
//NavMesh上で有効な巡回地点を探す最大試行回数
constexpr int32 MaxPatrolCandidateAttempts = 18;
//ボスがプレイヤーから安全にリロードできる最低距離
constexpr float BossSafeReloadDistance = 700.f;

//巡回タスクの移動と待機を管理する列挙型
enum class EEnemyPatrolPhase : uint8
{
	Moving,
	Waiting
};

//敵の巡回タスクのメモリ構造体
struct FEnemyPatrolTaskMemory
{
	//巡回地点へ到着してから移動を再開するまでの残り秒数
	float m_waitRemaining = 0.f;
	EEnemyPatrolPhase m_phase = EEnemyPatrolPhase::Moving;
};

//敵の戦闘ルームタグを取得する関数
FName GetCombatRoomTag(const AEnemyChara *_enemy)
{
	//敵を取得できない場合は戦闘空間タグなしとして返す
	if (!_enemy) { return NAME_None; }
	//敵が持つTagから戦闘空間を識別するTagを検索する
	for (const FName tag : _enemy->Tags)
	{
		//戦闘空間用の接頭辞を持つTagを見つけた時点で返す
		if (tag.ToString().StartsWith(TEXT("CombatRoom_"))) { return tag; }
	}
	return NAME_None;
}

//敵が同じ戦闘グループに属しているかを判定する関数
bool SharesCombatGroup(const AEnemyChara *_left, const AEnemyChara *_right)
{
	//戦闘ルームタグが一致する場合、同じ戦闘グループに属していると判定する
	if (!_left || !_right) { return false; }

	//戦闘ルームタグを取得し、右側の敵が左側の敵の戦闘ルームタグを持っているかを確認する
	const FName leftRoom = GetCombatRoomTag(_left);
	//左側の敵に戦闘空間Tagがある場合は右側の敵とのTag一致で判定する
	if (!leftRoom.IsNone()) { return _right->Tags.Contains(leftRoom); }
	return FVector::DistSquared2D(_left->GetActorLocation(), _right->GetActorLocation()) <= FMath::Square(5000.f);
}

//敵が近接攻撃スロットを取得できるかを判定する関数
bool CanClaimMeleeAttackSlot(AEnemyChara *_enemy)
{
	//近接攻撃スロットは、ミニオンランクの敵のみが取得できる
	if (!_enemy || _enemy->m_enemyRank != EEnemyRank::Minion) { return true; }

	//近接攻撃スロットを取得できるかを判定するために、同じ戦闘グループに属する他の敵の数をカウントする
	int32 committedMeleeAttackers = 0;
	//同じ戦闘グループで攻撃中の近接ミニオン数を数える
	for (TActorIterator<AEnemyChara> iterator(_enemy->GetWorld()); iterator; ++iterator)
	{
		//同じ戦闘グループに属する敵の中で、近接攻撃を行っている敵の数をカウントする
		AEnemyChara *peer = *iterator;
		//死亡済み、自身、別の攻撃方式、別戦闘空間の敵を集計から除外する
		if (!IsValid(peer) || peer == _enemy || peer->GetHealthRatio() <= 0.f || peer->m_enemyRank != EEnemyRank::Minion ||
			peer->m_currentStyle != EEnemyAttackStyle::Melee || !SharesCombatGroup(_enemy, peer))
		{
			continue;
		}

		//近接攻撃を行っている敵の数が2以上の場合、近接攻撃スロットを取得できないと判定する
		if (peer->IsAttacking() && ++committedMeleeAttackers >= 2) { return false; }
	}
	return true;
}

//敵が戦術的リロードを希望するかを判定する関数
bool WantsTacticalReload(AEnemyChara *_enemy)
{
	//戦術的リロードは、銃を持っている敵のみが希望する
	AEnemyGun *gun = _enemy ? _enemy->GetCurrentGun() : nullptr;

	//銃を持っていない、または銃の攻撃スタイルがGunでない、または弾薬が0以下の場合、戦術的リロードを希望しないと判定する
	if (!gun || _enemy->m_currentStyle != EEnemyAttackStyle::Gun || gun->GetTotalAmmo() <= 0) { return false; }

	//銃の弾薬が0の場合、戦術的リロードを希望する
	if (gun->IsOutOfAmmo()) { return true; }

	//ミニオンランクの敵は戦術的リロードを希望しない
	if (_enemy->m_enemyRank == EEnemyRank::Minion) { return false; }

	//銃の弾薬が少ない場合、戦術的リロードを希望する
	const bool isLowAmmo = gun->GetCurrentAmmo() <= FMath::Max(1, gun->GetClipSize() / 4);
	//安全距離とプレッシャー状態を確認する戦闘記憶コンポーネント
	const UEnemyCombatMemoryComponent *memory = _enemy->m_combatMemoryComponent;

	//弾薬が少なく、戦闘メモリが存在し、ターゲットとの距離が安全なリロード距離以上であり、プレッシャーを受けていない場合、戦術的リロードを希望する
	return isLowAmmo && memory && memory->GetTargetDistance() >= BossSafeReloadDistance && !memory->IsUnderPressure();
}

//敵がカバー移動を要求している場合、AIコントローラーにカバー移動を指示する関数
void IssueCoverMove(AAIController *_controller, AEnemyChara *_enemy)
{
	//カバー移動を要求している場合、AIコントローラーにカバー移動を指示する
	if (!_controller || !_enemy || !_enemy->m_coverComponent) { return; }

	//カバー移動中でない場合、カバー移動要求をリセットする
	if (_controller->GetMoveStatus() != EPathFollowingStatus::Moving) { _enemy->m_coverComponent->ResetCoverMoveRequest(); }

	//カバー移動要求を消費できる場合、AIコントローラーにカバー移動を指示する
	if (_enemy->m_coverComponent->ConsumeCoverMoveRequest())
	{
		_controller->MoveToLocation(_enemy->m_coverComponent->GetCoverDestination(), 35.f, true, true, true, true);
	}
}

//敵が戦術的リロードを希望する場合、AIコントローラーに戦術的リロードを指示する関数
bool HandleTacticalReload(AAIController *_controller, AEnemyChara *_enemy, AActor *_target)
{
	//戦術的リロードを希望する場合、AIコントローラーに戦術的リロードを指示する
	if (!_controller || !_enemy || !_target || !WantsTacticalReload(_enemy)) { return false; }
	//すでにリロード中の場合は新しい移動や攻撃を開始しない
	if (_enemy->IsReloading()) { return true; }

	//コンバットコンポネントのプレッシャー状態を確認し、必要に応じて武器を切り替える
	UEnemyCombatMemoryComponent *memory = _enemy->m_combatMemoryComponent;
	//プレッシャーを受けたボスは距離に応じて近接またはレーザーへ切り替える
	if (_enemy->m_enemyRank != EEnemyRank::Minion && memory && memory->IsUnderPressure())
	{
		_enemy->SwitchWeapon(memory->GetTargetDistance() <= BossSafeReloadDistance ? EEnemyAttackStyle::Melee : EEnemyAttackStyle::Laser);
		return true;
	}

	//カバーコンポネントを取得し、カバー移動中かどうかを確認する
	UEnemyCoverComponent *cover = _enemy->m_coverComponent;
	//確保済みの遮蔽物がある場合は到着してからリロードする
	if (cover && cover->IsUsingCover())
	{
		//カバー移動中でない場合、AIコントローラーにカバー移動を指示する
		if (!cover->UpdateAndIsAtCover())
		{
			IssueCoverMove(_controller, _enemy);
			return true;
		}

		//カバー移動中であり、カバー位置に到達している場合、AIコントローラーの移動を停止し、ターゲットに向かってリロードを実行する
		_controller->StopMovement();
		_enemy->FaceTarget(_target);
		_enemy->PeformReload();
		return true;
	}

	//カバーが存在する場合、最適なカバー位置を検索し、カバー移動を指示する
	FVector coverLocation;
	//利用中の遮蔽物がない場合は安全にリロードできる遮蔽物を探す
	if (cover && cover->FindBestCover(_target, coverLocation))
	{
		cover->CommitCover(coverLocation);
		IssueCoverMove(_controller, _enemy);
		return true;
	}

	//カバーが存在しない場合、最適な回避位置を検索し、回避移動を指示する
	FVector dodgeLocation;
	//遮蔽物を確保できない場合は緊急回避地点へ移動する
	if (cover && cover->FindDodgeLocation(_target, dodgeLocation, true))
	{
		cover->CommitReposition();
		_controller->MoveToLocation(dodgeLocation, 20.f, true, true, true, true);
	}

	//AIコントローラーの移動を停止し、ターゲットに向かってリロードを実行する
	_enemy->FaceTarget(_target);
	_enemy->PeformReload();
	return true;
}

//敵が近接攻撃のフォーメーション位置を取得できるかを判定する関数
bool FindMeleeFormationPosition(AEnemyChara *_enemy, AActor *_target, FVector &_outPosition)
{
	//近接攻撃のフォーメーション位置を取得するために、同じ戦闘グループに属する近接攻撃スタイルの敵を収集する
	if (!_enemy || !_target) { return false; }
	//同じ戦闘空間で近接陣形へ参加する敵の一覧
	TArray<AEnemyChara *> meleeEnemies;

	//同じ戦闘グループに属する近接攻撃スタイルの敵を収集する
	for (TActorIterator<AEnemyChara> iterator(_enemy->GetWorld()); iterator; ++iterator)
	{
		//近接陣形の並び順を比較する同じ戦闘グループの敵
		AEnemyChara *peer = *iterator;
		//死亡済みまたは別戦闘グループの敵を近接陣形から除外する
		if (!IsValid(peer) || peer->GetHealthRatio() <= 0.f || peer->m_enemyRank != EEnemyRank::Minion ||
			peer->m_currentStyle != EEnemyAttackStyle::Melee || !SharesCombatGroup(_enemy, peer) ||
			FVector::DistSquared2D(peer->GetActorLocation(), _target->GetActorLocation()) > FMath::Square(4500.f))
		{
			continue;
		}
		meleeEnemies.Add(peer);
	}
	//近接攻撃スタイルの敵をユニークIDでソートすることで、フォーメーション位置を決定する
	meleeEnemies.Sort([](const AEnemyChara &_left, const AEnemyChara &_right) { return _left.GetUniqueID() < _right.GetUniqueID(); });

	//近接攻撃スタイルの敵のインデックスとスロット数を計算する
	const int32 slotIndex = FMath::Max(meleeEnemies.IndexOfByKey(_enemy), 0);
	const int32 slotCount = FMath::Max(meleeEnemies.Num(), 3);
	//プレイヤーを均等に囲む近接陣形の配置角度
	const float angle = 2.f * PI * static_cast<float>(slotIndex) / static_cast<float>(slotCount);

	//近接攻撃スタイルの敵のフォーメーション位置を計算する
	const float formationRadius = slotIndex < 3 ? 185.f : 360.f;
	const FVector desiredPosition = _target->GetActorLocation() + FVector(FMath::Cos(angle), FMath::Sin(angle), 0.f) * formationRadius;

	//ナビゲーションシステムを使用して、フォーメーション位置をナビゲーションメッシュ上に投影する
	UNavigationSystemV1 *navigation = UNavigationSystemV1::GetCurrent(_enemy);
	//近接陣形の候補をNavMesh上へ補正した移動先
	FNavLocation projectedPosition;
	//陣形候補をNavMesh上へ補正できない場合は近接配置を失敗として返す
	if (!navigation || !navigation->ProjectPointToNavigation(desiredPosition, projectedPosition)) { return false; }

	//フォーメーション位置を出力パラメータに設定する
	_outPosition = projectedPosition.Location;
	//フォーメーション位置の取得に成功した場合、trueを返す
	return true;
}

//敵が遠距離攻撃のフォーメーション位置を取得できるかを判定する関数
bool FindRangedFormationPosition(AEnemyChara *_enemy, AActor *_target, FVector &_outPosition)
{
	//遠距離攻撃のフォーメーション位置を取得するために、同じ戦闘グループに属する遠距離攻撃スタイルの敵を収集する
	if (!_enemy || !_target) { return false; }
	//同じ戦闘空間で遠距離陣形へ参加する敵の一覧
	TArray<AEnemyChara *> rangedEnemies;

	//同じ戦闘グループに属する遠距離攻撃スタイルの敵を収集する
	for (TActorIterator<AEnemyChara> iterator(_enemy->GetWorld()); iterator; ++iterator)
	{
		//遠距離陣形の並び順を比較する同じ戦闘グループの敵
		AEnemyChara *peer = *iterator;
		//死亡済みまたは別戦闘グループの敵を遠距離陣形から除外する
		if (!IsValid(peer) || peer->GetHealthRatio() <= 0.f || peer->m_enemyRank != EEnemyRank::Minion ||
			peer->m_currentStyle != EEnemyAttackStyle::Gun || !SharesCombatGroup(_enemy, peer) ||
			FVector::DistSquared2D(peer->GetActorLocation(), _target->GetActorLocation()) > FMath::Square(5000.f))
		{
			continue;
		}
		rangedEnemies.Add(peer);
	}
	//遠距離攻撃スタイルの敵をユニークIDでソートすることで、フォーメーション位置を決定する
	rangedEnemies.Sort([](const AEnemyChara &_left, const AEnemyChara &_right) { return _left.GetUniqueID() < _right.GetUniqueID(); });

	//遠距離攻撃スタイルの敵のインデックスとスロット数を計算する
	const int32 slotIndex = FMath::Max(rangedEnemies.IndexOfByKey(_enemy), 0);
	const int32 slotCount = FMath::Max(rangedEnemies.Num(), 4);

	//遠距離攻撃スタイルの敵のフォーメーション位置を計算する
	const float angle = 2.f * PI * (static_cast<float>(slotIndex) + 0.5f) / static_cast<float>(slotCount);
	//隣接する射撃役が重ならないよう交互に切り替える陣形半径
	const float radius = (slotIndex % 2 == 0) ? 1000.f : 1300.f;
	const FVector desiredPosition = _target->GetActorLocation() + FVector(FMath::Cos(angle), FMath::Sin(angle), 0.f) * radius;

	//ナビゲーションシステムを使用して、フォーメーション位置をナビゲーションメッシュ上に投影する
	UNavigationSystemV1 *navigation = UNavigationSystemV1::GetCurrent(_enemy);
	//遠距離陣形の候補をNavMesh上へ補正した移動先
	FNavLocation projectedPosition;
	//陣形候補をNavMesh上へ補正できない場合は遠距離配置を失敗として返す
	if (!navigation || !navigation->ProjectPointToNavigation(desiredPosition, projectedPosition, FVector(350.f, 350.f, 450.f))) { return false; }

	//フォーメーション位置を出力パラメータに設定する
	_outPosition = projectedPosition.Location;
	return true;
}
}

//敵の攻撃タスク名とインスタンス生成設定を初期化する関数
UBTTask_EnemyAttack::UBTTask_EnemyAttack()
{
	NodeName = TEXT("Execute Enemy Attack");
}

//敵の攻撃タスクを実行する関数
EBTNodeResult::Type UBTTask_EnemyAttack::ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory)
{
	//AIコントローラー、敵キャラクター、ブラックボードコンポーネント、ターゲットアクターを取得する
	AAIController *controller = _ownerComp.GetAIOwner();
	AEnemyChara *enemy = controller ? Cast<AEnemyChara>(controller->GetPawn()) : nullptr;
	UBlackboardComponent *blackboard = _ownerComp.GetBlackboardComponent();
	AActor *targetActor = blackboard ? Cast<AActor>(blackboard->GetValueAsObject(EnemyBlackboardKeys::TargetActor)) : nullptr;

	//いずれかのコンポーネントが無効な場合、タスクを失敗として終了する
	if (!controller || !enemy || !targetActor) { return EBTNodeResult::Failed; }

	//戦術的リロードを処理し、成功した場合はタスクを成功として終了する
	if (HandleTacticalReload(controller, enemy, targetActor)) { return EBTNodeResult::Succeeded; }
	//別の攻撃またはリロードが進行中の場合は新しい攻撃を開始しない
	if (enemy->IsAttacking() || enemy->IsReloading()) { return EBTNodeResult::Failed; }

	//敵の戦闘アイドルアニメーションを停止する
	enemy->StopCombatIdleAnimation();

	//ミニオンランクの敵で、銃攻撃スタイルを持ち、カバーコンポーネントが存在し、カバーを使用している場合の処理
	if (enemy->m_enemyRank == EEnemyRank::Minion && enemy->m_currentStyle == EEnemyAttackStyle::Gun && enemy->m_coverComponent &&
		enemy->m_coverComponent->IsUsingCover())
	{
		//カバーを使用している場合、カバー位置に到達していない場合はカバー移動を指示する
		if (!enemy->m_coverComponent->UpdateAndIsAtCover())
		{
			//カバー移動中でない場合、カバー移動要求をリセットする
			if (controller->GetMoveStatus() != EPathFollowingStatus::Moving) { enemy->m_coverComponent->ResetCoverMoveRequest(); }
			//カバー移動要求を消費できる場合、AIコントローラーにカバー移動を指示する
			if (enemy->m_coverComponent->ConsumeCoverMoveRequest())
			{
				controller->MoveToLocation(enemy->m_coverComponent->GetCoverDestination(), 35.f, true, true, true, true);
			}
			return EBTNodeResult::Succeeded;
		}

		//カバー位置に到達している場合、カバーを保持するかどうかを判定し、保持する場合は移動を停止し、攻撃を停止し、武器を下げる
		if (enemy->m_coverComponent->ShouldHoldCover())
		{
			controller->StopMovement();
			controller->ClearFocus(EAIFocusPriority::Gameplay);
			enemy->StopFiring();
			enemy->LowerWeapon();
			enemy->SetActionState(EActionState::Idle);
			return EBTNodeResult::Succeeded;
		}
		//カバーを保持しない場合、カバー使用を終了する
		else { enemy->m_coverComponent->FinishCoverUse(); }
	}
	//ミニオンランクの敵で、銃攻撃スタイルを持ち、カバーコンポーネントが存在し、カバーを使用していない場合の処理
	if (enemy->m_enemyRank == EEnemyRank::Minion && enemy->m_currentStyle == EEnemyAttackStyle::Gun && enemy->m_coverComponent &&
		!enemy->m_coverComponent->IsUsingCover() && enemy->m_combatMemoryComponent && enemy->m_combatMemoryComponent->GetTargetDistance() > 850.f &&
		(enemy->m_combatMemoryComponent->IsPlayerAiming() || enemy->m_combatMemoryComponent->IsPlayerAttacking() ||
		 enemy->m_combatMemoryComponent->IsUnderPressure()))
	{
		//カバーを使用していない場合、最適なカバー位置を検索し、カバー移動を指示する
		FVector coverLocation;
		//射線を遮る有効な候補がある場合は遮蔽物の利用を開始する
		if (enemy->m_coverComponent->FindBestCover(targetActor, coverLocation))
		{
			//カバー位置に移動する前に、攻撃を停止し、カバーをコミットする
			enemy->StopFiring();
			enemy->m_coverComponent->CommitCover(coverLocation);
			//新しい遮蔽移動要求を確定できた場合だけAIへ経路移動を指示する
			if (enemy->m_coverComponent->ConsumeCoverMoveRequest())
			{
				const EPathFollowingRequestResult::Type moveResult = controller->MoveToLocation(coverLocation, 35.f, true, true, true, true);

				//カバー移動が失敗した場合、カバー使用を終了する
				//遮蔽位置までの経路を作れない場合は確保状態を解除する
				if (moveResult == EPathFollowingRequestResult::Failed) { enemy->m_coverComponent->FinishCoverUse(); }
			}
			//カバー移動を指示した場合、タスクを成功として終了する
			return EBTNodeResult::Succeeded;
		}
	}
	//AIコントローラーにターゲットをフォーカスさせ、敵キャラクターにターゲットを向かせる
	controller->SetFocus(targetActor);
	enemy->FaceTarget(targetActor);

	//近接攻撃へ参加できる空きがあるか判定するための変数
	const bool hasMeleeAttackSlot = CanClaimMeleeAttackSlot(enemy);
	//近接攻撃枠を取得できない敵を空いている支援位置へ移動させる
	if (enemy->m_currentStyle == EEnemyAttackStyle::Melee && !hasMeleeAttackSlot)
	{
		//近接攻撃役と重ならない遠距離支援位置
		FVector supportPosition;
		//有効な陣形位置を取得できた場合だけ移動を指示する
		if (FindMeleeFormationPosition(enemy, targetActor, supportPosition))
		{
			controller->MoveToLocation(supportPosition, 45.f, true, true, true, true);
		}
		return EBTNodeResult::Succeeded;
	}
	//近接攻撃スタイルの敵で、近接攻撃を実行できない場合、ターゲットに向かって移動を指示する
	if (enemy->m_currentStyle == EEnemyAttackStyle::Melee && !enemy->CanCommitMeleeAttack(targetActor))
	{
		controller->MoveToActor(targetActor, enemy->GetMeleeStrikeRange(targetActor) * TargetAcceptanceRadiusMultiplier, true, true, true, nullptr,
								true);
		return EBTNodeResult::Succeeded;
	}
	//遠距離攻撃スタイルの敵で、遠距離攻撃を実行できない場合、遠距離フォーメーション位置を検索し、移動を指示する
	controller->StopMovement();
	//Decision Componentが選択した攻撃を開始できたか示す変数
	bool executedAttack = false;
	//戦況判断用コンポーネントがある場合は選択済みの攻撃を実行する
	if (enemy->m_decisionComponent) { executedAttack = enemy->m_decisionComponent->ExecuteAttack(); }
	else
	{
		enemy->PerformAttack();
		executedAttack = true;
	}
	//プレッシャーで攻撃できなかった敵を射線外へ退避させる
	if (!executedAttack && enemy->m_coverComponent && enemy->m_combatMemoryComponent && enemy->m_combatMemoryComponent->IsUnderPressure())
	{
		//プレイヤーの照準から外れるために使用する回避先
		FVector dodgeLocation;
		//NavMesh上の回避先を取得できた場合は再配置として移動する
		if (enemy->m_coverComponent->FindDodgeLocation(targetActor, dodgeLocation))
		{
			enemy->m_coverComponent->CommitReposition();
			controller->MoveToLocation(dodgeLocation, 20.f, true, true, true, true);
		}
	}
	return EBTNodeResult::Succeeded;
}

//敵の移動タスクに関する初期値とコンポーネントを設定する関数 UBTTask_EnemyMoveToTarget
//のコンストラクタ関数
UBTTask_EnemyMoveToTarget::UBTTask_EnemyMoveToTarget()
{
	NodeName = TEXT("Move To Target");
}

//Behavior Tree上でUBTTask 敵 移動 To 攻撃対象を開始し、成功・失敗・継続を返す関数
EBTNodeResult::Type UBTTask_EnemyMoveToTarget::ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory)
{
	//AIコントローラー、ブラックボードコンポーネント、敵キャラクター、ターゲットアクターを取得する
	AAIController *controller = _ownerComp.GetAIOwner();
	UBlackboardComponent *blackboard = _ownerComp.GetBlackboardComponent();
	AEnemyChara *enemy = controller ? Cast<AEnemyChara>(controller->GetPawn()) : nullptr;
	AActor *targetActor = blackboard ? Cast<AActor>(blackboard->GetValueAsObject(EnemyBlackboardKeys::TargetActor)) : nullptr;

	//いずれかのコンポーネントが無効な場合、タスクを失敗として終了する
	if (!controller || !enemy || !targetActor) { return EBTNodeResult::Failed; }
	//リロードまたは安全地点への移動を開始した場合は移動Taskを完了する
	if (HandleTacticalReload(controller, enemy, targetActor)) { return EBTNodeResult::Succeeded; }

	//敵のAIコントローラーが視覚的接触を持っていない場合、最近の視覚的接触またはプレイヤーのノイズを確認し、既知の位置がない場合はターゲットアクターの位置を通知する
	AEnemyAIController *enemyController = Cast<AEnemyAIController>(controller);
	//直接視認を失った敵は最後に記憶したプレイヤー位置へ移動させる
	if (enemyController && !enemyController->HasActiveVisualContact())
	{
		//敵のAIコントローラーが最近の視覚的接触またはプレイヤーのノイズを持っていない場合、ターゲットアクターの位置を通知する
		bool hasKnownLocation = enemyController->HasRecentVisualContact() || enemyController->HasRecentPlayerNoise();
		//戦闘開始直後のArena内では現在のプレイヤー位置を認識情報へ補う
		if (!hasKnownLocation && enemy->Tags.ContainsByPredicate([](const FName &_tag) { return _tag.ToString().StartsWith(TEXT("CombatRoom_")); }))
		{
			//敵のAIコントローラーにターゲットアクターの位置を通知する
			enemyController->NotifyPlayerNoise(targetActor, targetActor->GetActorLocation());
			hasKnownLocation = true;
		}
		//敵のAIコントローラーが既知の位置を持っている場合、フォーカスをクリアし、最近の視覚的接触またはプレイヤーのノイズの位置に移動する
		if (hasKnownLocation)
		{
			controller->ClearFocus(EAIFocusPriority::Gameplay);
			controller->MoveToLocation(enemyController->GetLastKnownPlayerLocation(), 90.f, true, true, true, true);
			return EBTNodeResult::Succeeded;
		}
	}
	//敵の戦闘アイドルアニメーションを停止する
	enemy->StopCombatIdleAnimation();
	//遮蔽物を使用中の遠距離ミニオンを確保済みの位置まで移動させる
	if (enemy->m_enemyRank == EEnemyRank::Minion && enemy->m_currentStyle == EEnemyAttackStyle::Gun && enemy->m_coverComponent &&
		enemy->m_coverComponent->IsUsingCover())
	{
		//カバーを使用している場合、カバー位置に到達していない場合はカバー移動を指示する
		if (!enemy->m_coverComponent->UpdateAndIsAtCover())
		{
			//カバー移動中でない場合、カバー移動要求をリセットする
			if (controller->GetMoveStatus() != EPathFollowingStatus::Moving) { enemy->m_coverComponent->ResetCoverMoveRequest(); }
			//カバー移動要求を消費できる場合、AIコントローラーにカバー移動を指示する
			if (enemy->m_coverComponent->ConsumeCoverMoveRequest())
			{
				controller->MoveToLocation(enemy->m_coverComponent->GetCoverDestination(), 35.f, true, true, true, true);
			}
			return EBTNodeResult::Succeeded;
		}
		//カバー位置に到達している場合、カバーを保持するかどうかを判定し、保持する場合は移動を停止し、攻撃を停止し、武器を下げる
		if (enemy->m_coverComponent->ShouldHoldCover())
		{
			controller->StopMovement();
			controller->ClearFocus(EAIFocusPriority::Gameplay);
			enemy->StopFiring();
			enemy->LowerWeapon();
			enemy->SetActionState(EActionState::Idle);
			return EBTNodeResult::Succeeded;
		}
		//カバーを保持しない場合、カバー使用を終了する
		enemy->m_coverComponent->FinishCoverUse();
	}
	//敵のAIコントローラーにターゲットアクターをフォーカスさせる
	controller->SetFocus(targetActor);

	//ミニオンランクの敵で、銃攻撃スタイルを持ち、カバーコンポーネントが存在し、カバーを使用していない場合の処理
	if (enemy->m_enemyRank == EEnemyRank::Minion && enemy->m_currentStyle == EEnemyAttackStyle::Gun && enemy->m_coverComponent &&
		!enemy->m_coverComponent->IsUsingCover() && enemy->m_combatMemoryComponent && enemy->m_combatMemoryComponent->GetTargetDistance() > 850.f &&
		(enemy->m_combatMemoryComponent->IsPlayerAiming() || enemy->m_combatMemoryComponent->IsPlayerAttacking() ||
		 enemy->m_combatMemoryComponent->IsUnderPressure()))
	{
		//カバーを使用していない場合、最適なカバー位置を検索し、カバー移動を指示する
		FVector coverLocation;
		//プレイヤーの射線を遮る候補を取得できた場合は遮蔽移動を開始する
		if (enemy->m_coverComponent->FindBestCover(targetActor, coverLocation))
		{
			//カバー位置に移動する前に、攻撃を停止し、カバーをコミットする
			enemy->StopFiring();
			enemy->m_coverComponent->CommitCover(coverLocation);
			//新しい遮蔽移動要求が確定した場合だけAIへ移動を指示する
			if (enemy->m_coverComponent->ConsumeCoverMoveRequest())
			{
				//AIコントローラーにカバー移動を指示する
				const EPathFollowingRequestResult::Type moveResult = controller->MoveToLocation(coverLocation, 35.f, true, true, true, true);
				if (moveResult == EPathFollowingRequestResult::Failed) { enemy->m_coverComponent->FinishCoverUse(); }
			}
			return EBTNodeResult::Succeeded;
		}

		//遮蔽物を取得できない時にプレイヤーの射線から外れる回避先
		FVector dodgeLocation;
		//NavMesh上に回避先がある場合は再配置として移動する
		if (enemy->m_coverComponent->FindDodgeLocation(targetActor, dodgeLocation))
		{
			enemy->m_coverComponent->CommitReposition();
			controller->MoveToLocation(dodgeLocation, 20.f, true, true, true, true);
			return EBTNodeResult::Succeeded;
		}
	}

	//近接ミニオンをプレイヤー周囲へ分散させるための陣形位置
	FVector formationPosition;
	//有効な近接陣形位置を取得できた場合は攻撃距離まで移動する
	if (enemy->m_enemyRank == EEnemyRank::Minion && enemy->m_currentStyle == EEnemyAttackStyle::Melee &&
		FindMeleeFormationPosition(enemy, targetActor, formationPosition))
	{
		controller->MoveToLocation(formationPosition, 45.f, true, true, true, true);
		return EBTNodeResult::Succeeded;
	}
	//有効な射撃陣形位置を取得できた場合は他の敵と重ならない位置へ移動する
	if (enemy->m_enemyRank == EEnemyRank::Minion && enemy->m_currentStyle == EEnemyAttackStyle::Gun &&
		FindRangedFormationPosition(enemy, targetActor, formationPosition))
	{
		controller->MoveToLocation(formationPosition, 100.f, true, true, true, true);
		return EBTNodeResult::Succeeded;
	}

	//AIコントローラーにターゲットアクターに移動するよう指示する
	controller->MoveToActor(targetActor,
							(enemy->m_currentStyle == EEnemyAttackStyle::Melee ? enemy->GetMeleeStrikeRange(targetActor) : enemy->GetAttackRange()) *
								TargetAcceptanceRadiusMultiplier,
							true, true, true, nullptr, true);
	return EBTNodeResult::Succeeded;
}

//敵の巡回タスク名とインスタンス生成設定を初期化する関数
UBTTask_EnemyPatrol::UBTTask_EnemyPatrol() : m_patrolRadius(DefaultPatrolRadius)
{
	//Behavior Tree上でのタスク名を設定し、Tick通知を有効にする
	NodeName = TEXT("Select Patrol Location");
	bNotifyTick = true;
}

//Behavior Tree上でUBTTask 敵 巡回を開始し、成功・失敗・継続を返す関数
EBTNodeResult::Type UBTTask_EnemyPatrol::ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory)
{
	//AIコントローラー、敵キャラクター、ナビゲーションシステムを取得する
	AAIController *controller = _ownerComp.GetAIOwner();
	APawn *enemyPawn = controller ? controller->GetPawn() : nullptr;
	UNavigationSystemV1 *navigationSystem = UNavigationSystemV1::GetCurrent(enemyPawn);

	//いずれかのコンポーネントが無効な場合、タスクを失敗として終了する
	if (!controller || !enemyPawn || !navigationSystem) { return EBTNodeResult::Failed; }

	//敵キャラクターを取得し、戦闘アイドルアニメーションを停止し、攻撃を停止し、武器を下げる
	AEnemyChara *enemy = Cast<AEnemyChara>(enemyPawn);
	//敵本体を取得できた場合は巡回用の非戦闘状態へ戻す
	if (enemy)
	{
		enemy->StopCombatIdleAnimation();
		enemy->StopFiring();
		enemy->LowerWeapon();
	}

	//ブラックボードコンポーネントを取得し、前回の巡回位置を取得する
	UBlackboardComponent *blackboard = _ownerComp.GetBlackboardComponent();
	const FVector previousLocation = blackboard ? blackboard->GetValueAsVector(EnemyBlackboardKeys::PatrolLocation) : FVector::ZeroVector;

	//ナビゲーションシステムを使用して、ランダムな巡回位置を取得する
	FNavLocation patrolLocation;
	//距離条件を満たす巡回地点をNavMesh上で発見したか示す変数
	bool foundPatrolLocation = false;
	//距離条件を満たす候補が見つかるまで上限回数内で探索する
	for (int32 attempt = 0; attempt < MaxPatrolCandidateAttempts; ++attempt)
	{
		//ナビゲーションシステムを使用して、敵キャラクターの現在位置から半径 m_patrolRadius
		//内のランダムな到達可能なポイントを取得する
		FNavLocation candidate;
		//NavMesh上に到達可能な候補がない場合は次の探索へ進む
		if (!navigationSystem->GetRandomReachablePointInRadius(enemyPawn->GetActorLocation(), m_patrolRadius, candidate)) { continue; }
		//現在位置に近すぎる候補は移動が発生しないため除外する
		if (FVector::DistSquared2D(candidate.Location, enemyPawn->GetActorLocation()) < FMath::Square(MinPatrolTravelDistance)) { continue; }
		//直前の巡回地点に近すぎる候補は往復を防ぐため除外する
		if (!previousLocation.IsNearlyZero() && FVector::DistSquared2D(candidate.Location, previousLocation) < FMath::Square(MinPatrolRepeatDistance))
		{
			continue;
		}

		//他の敵キャラクターとの距離を確認し、最小距離 MinPatrolPeerDistance
		//より近い場合は候補を破棄する
		bool tooCloseToPeer = false;
		//候補地点と同じ戦闘空間の敵との距離を確認する
		for (TActorIterator<AEnemyChara> it(enemyPawn->GetWorld()); it; ++it)
		{
			//他の敵キャラクターが無効な場合、または現在の敵キャラクターと同じ場合はスキップする
			if (*it == enemyPawn || !IsValid(*it)) { continue; }
			//別の敵に近すぎる候補を選択不可として記録する
			if (FVector::DistSquared2D(candidate.Location, it->GetActorLocation()) < FMath::Square(MinPatrolPeerDistance))
			{
				tooCloseToPeer = true;
				break;
			}
		}

		//候補が他の敵キャラクターに近すぎる場合は、次の候補を試す
		if (tooCloseToPeer) { continue; }
		patrolLocation = candidate;
		foundPatrolLocation = true;
		break;
	}

	//有効な巡回位置が見つからなかった場合、タスクを失敗として終了する
	if (!foundPatrolLocation) { return EBTNodeResult::Failed; }

	//ブラックボードコンポーネントに巡回位置を設定し、AIコントローラーのフォーカスをクリアする
	if (blackboard) { blackboard->SetValueAsVector(EnemyBlackboardKeys::PatrolLocation, patrolLocation.Location); }

	//AIコントローラーのフォーカスをクリアし、タスクメモリに巡回フェーズと待機時間を設定する
	controller->ClearFocus(EAIFocusPriority::Gameplay);
	//巡回の移動段階と待機時間をTask実行中に保持するメモリ
	FEnemyPatrolTaskMemory *memory = reinterpret_cast<FEnemyPatrolTaskMemory *>(_nodeMemory);
	memory->m_phase = EEnemyPatrolPhase::Moving;
	memory->m_waitRemaining = PatrolIdleDuration;
	const EPathFollowingRequestResult::Type moveResult = controller->MoveToLocation(patrolLocation.Location, 50.f, true, true, true, true);

	//移動要求の結果に応じて、タスクの結果を返す
	if (moveResult == EPathFollowingRequestResult::Failed) { return EBTNodeResult::Failed; }

	//移動要求がすでに目標に到達している場合、巡回フェーズを待機に設定し、敵のアクション状態をアイドルに設定する
	if (moveResult == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		memory->m_phase = EEnemyPatrolPhase::Waiting;
		//敵本体を取得できた場合は到着直後から待機姿勢へ切り替える
		if (enemy) enemy->SetActionState(EActionState::Idle);
	}
	return EBTNodeResult::InProgress;
}

//継続型Behavior Treeタスクの移動または攻撃進行を監視し、成功・失敗を確定する関数
void UBTTask_EnemyPatrol::TickTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory, float _deltaSeconds)
{
	//AIコントローラーと敵キャラクターを取得する
	AAIController *controller = _ownerComp.GetAIOwner();
	AEnemyChara *enemy = controller ? Cast<AEnemyChara>(controller->GetPawn()) : nullptr;

	//いずれかのコンポーネントが無効な場合、タスクを失敗として終了する
	if (!controller || !enemy)
	{
		FinishLatentTask(_ownerComp, EBTNodeResult::Failed);
		return;
	}

	//敵が移動中の場合、移動が完了するまで待機する
	FEnemyPatrolTaskMemory *memory = reinterpret_cast<FEnemyPatrolTaskMemory *>(_nodeMemory);
	//移動段階では経路追従の完了を待ってから待機段階へ切り替える
	if (memory->m_phase == EEnemyPatrolPhase::Moving)
	{
		//経路追従中はTaskを継続して次のTickを待つ
		if (controller->GetMoveStatus() == EPathFollowingStatus::Moving) { return; }

		//移動が完了した場合、敵の移動を停止し、攻撃を停止し、武器を下げ、待機フェーズに移行する
		controller->StopMovement();
		controller->ClearFocus(EAIFocusPriority::Gameplay);
		enemy->StopFiring();
		enemy->LowerWeapon();
		enemy->SetActionState(EActionState::Idle);
		memory->m_phase = EEnemyPatrolPhase::Waiting;
		memory->m_waitRemaining = PatrolIdleDuration;
		return;
	}

	//敵が待機中の場合、待機時間を減算し、待機時間が終了した場合はタスクを成功として終了する
	controller->ClearFocus(EAIFocusPriority::Gameplay);
	//別の状態へ変化した敵を巡回待機姿勢へ戻す
	if (enemy->GetActionState() != EActionState::Idle)
	{
		enemy->StopFiring();
		enemy->LowerWeapon();
		enemy->SetActionState(EActionState::Idle);
	}

	//待機時間を減算し、待機時間が終了した場合はタスクを成功として終了する
	memory->m_waitRemaining -= _deltaSeconds;
	//三秒間の待機が完了した時点で次の巡回選択へ進める
	if (memory->m_waitRemaining <= 0.f)
	{
		FinishLatentTask(_ownerComp, EBTNodeResult::Succeeded);
	}
}

//中断されたBehavior Treeタスクの移動要求と攻撃予約を解除する関数
EBTNodeResult::Type UBTTask_EnemyPatrol::AbortTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory)
{
	//Behavior Treeタスクの中断時に、AIコントローラーの移動を停止する
	if (AAIController *controller = _ownerComp.GetAIOwner()) { controller->StopMovement(); }
	return EBTNodeResult::Aborted;
}

//Behavior Treeタスクが使用するメモリサイズを取得する関数
uint16 UBTTask_EnemyPatrol::GetInstanceMemorySize() const
{
	return sizeof(FEnemyPatrolTaskMemory);
}

//敵の退避タスク名とインスタンス生成設定を初期化する関数
UBTTask_EnemyRetreat::UBTTask_EnemyRetreat()
{
	NodeName = TEXT("Retreat Enemy");
}

//Behavior Tree上でUBTTask 敵 Retreatを開始し、成功・失敗・継続を返す関数
EBTNodeResult::Type UBTTask_EnemyRetreat::ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory)
{
	//AIコントローラーと敵キャラクターを取得する
	AAIController *controller = _ownerComp.GetAIOwner();
	AEnemyChara *enemy = controller ? Cast<AEnemyChara>(controller->GetPawn()) : nullptr;
	//敵本体を取得できない場合は退避を実行せずTaskを失敗として返す
	if (!enemy) { return EBTNodeResult::Failed; }

	//敵キャラクターの移動を停止し、回避行動を実行する
	controller->StopMovement();
	enemy->PerformRetreat();
	return EBTNodeResult::Succeeded;
}
