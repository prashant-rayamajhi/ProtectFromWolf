#include "AI/BehaviorTree/Decorators/BTDecorator_EnemyCondition.h"

#include "AI/BehaviorTree/Blackboard/EnemyBlackboardKeys.h"
#include "BehaviorTree/BlackboardComponent.h"

//BTDecorator_EnemyConditionが使用するComponentと初期値を構築する関数
UBTDecorator_EnemyCondition::UBTDecorator_EnemyCondition() : m_condition(EEnemyBTCondition::CanPatrol)
{
	//Behavior Treeのノード名を設定する。
	NodeName = TEXT("Enemy Condition");
	FlowAbortMode = EBTFlowAbortMode::Both;
	bNotifyBecomeRelevant = true;
	bNotifyCeaseRelevant = true;
}

//認識と攻撃状態の変更を監視し、巡回中でも戦闘へ割り込めるようにする関数
void UBTDecorator_EnemyCondition::OnBecomeRelevant(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory)
{
	if (UBlackboardComponent *board = _ownerComp.GetBlackboardComponent())
	{
		//巡回から戦闘への移行と、攻撃終了後の復帰に必要な値を監視する
		for (FName key : {EnemyBlackboardKeys::IsPaused, EnemyBlackboardKeys::IsDead, EnemyBlackboardKeys::ShouldRetreat,
			EnemyBlackboardKeys::CanSeeTarget, EnemyBlackboardKeys::IsInAttackRange, EnemyBlackboardKeys::IsActionLocked})
		{
			board->RegisterObserver(board->GetKeyID(key), this,
				FOnBlackboardChangeNotification::CreateUObject(this, &UBTDecorator_EnemyCondition::OnStateChanged));
		}
	}
}

//使わなくなった分岐の監視を解除する関数
void UBTDecorator_EnemyCondition::OnCeaseRelevant(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory)
{
	if (UBlackboardComponent *board = _ownerComp.GetBlackboardComponent()) { board->UnregisterObserversFrom(this); }
}

//分岐条件が変わった時だけ現在の行動を再評価する関数
EBlackboardNotificationResult UBTDecorator_EnemyCondition::OnStateChanged(const UBlackboardComponent &_blackboard, FBlackboard::FKey _key)
{
	UBehaviorTreeComponent *tree = Cast<UBehaviorTreeComponent>(_blackboard.GetBrainComponent());
	if (!tree) { return EBlackboardNotificationResult::RemoveObserver; }
	ConditionalFlowAbort(*tree, EBTDecoratorAbortRequest::ConditionResultChanged);
	return EBlackboardNotificationResult::ContinueObserving;
}

//Behavior Treeの条件を評価する関数
bool UBTDecorator_EnemyCondition::CalculateRawConditionValue(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) const
{
	//Behavior TreeのBlackboardコンポーネントを取得する。
	const UBlackboardComponent *blackboard = _ownerComp.GetBlackboardComponent();
	//Blackboardを取得できない場合は条件判定を失敗として終了する
	if (!blackboard) { return false; }
	return Evaluate(*blackboard);
}

//攻撃中も待機分岐で認識更新を続け、巡回への転落を防ぐ関数
bool UBTDecorator_EnemyCondition::Evaluate(const UBlackboardComponent &_blackboard) const
{
	const UBlackboardComponent *blackboard = &_blackboard;

	//フェーズ表示中に敵の判断を停止するための変数
	const bool isPaused = blackboard->GetValueAsBool(EnemyBlackboardKeys::IsPaused);
	const bool isDead = blackboard->GetValueAsBool(EnemyBlackboardKeys::IsDead);
	const bool shouldRetreat = blackboard->GetValueAsBool(EnemyBlackboardKeys::ShouldRetreat);
	const bool canSeeTarget = blackboard->GetValueAsBool(EnemyBlackboardKeys::CanSeeTarget);
	const bool isInAttackRange = blackboard->GetValueAsBool(EnemyBlackboardKeys::IsInAttackRange);
	const bool isActionLocked = blackboard->GetValueAsBool(EnemyBlackboardKeys::IsActionLocked);

	//条件に応じて、Behavior Treeの条件を評価する。
	switch (m_condition)
	{
	case EEnemyBTCondition::Paused:
		return (isPaused || isActionLocked) && !isDead;
	case EEnemyBTCondition::Retreat:
		return !isPaused && !isDead && shouldRetreat && !isActionLocked;
	case EEnemyBTCondition::CanAttack:
		return !isPaused && !isDead && !shouldRetreat && canSeeTarget && isInAttackRange && !isActionLocked;
	case EEnemyBTCondition::CanChase:
		return !isPaused && !isDead && !shouldRetreat && canSeeTarget && !isInAttackRange && !isActionLocked;
	case EEnemyBTCondition::CanPatrol:
		return !isPaused && !isDead && !shouldRetreat && !canSeeTarget && !isActionLocked;
	default:
		return false;
	}
}
