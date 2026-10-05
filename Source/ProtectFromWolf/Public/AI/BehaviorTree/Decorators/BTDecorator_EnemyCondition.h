#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BTDecorator_EnemyCondition.generated.h"

UENUM()
enum class EEnemyBTCondition : uint8
{
	Paused,
	Retreat,
	CanAttack,
	CanChase,
	CanPatrol
};

//敵のBlackboard値を条件名から評価してBehavior Treeの分岐を制御するクラス
UCLASS()
//Blackboardの距離、視認、退避、行動ロックを評価してAI分岐を制御するDecorator
class PROTECTFROMWOLF_API UBTDecorator_EnemyCondition : public UBTDecorator
{
	GENERATED_BODY()

  public:
	//敵の状態を判定するDecorator名と監視通知を設定する関数
	UBTDecorator_EnemyCondition();

	//Behavior Treeを通過させる敵状態または攻撃条件の種類
	UPROPERTY(EditAnywhere, Category = "Condition")
	EEnemyBTCondition m_condition;
	//同じBlackboardの状態から、実行可能な行動を一貫して判定する関数
	bool Evaluate(const UBlackboardComponent &_blackboard) const;

  protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) const override;
	//分岐が有効な間、知覚と攻撃状態の変化を監視する関数
	virtual void OnBecomeRelevant(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) override;
	//分岐の監視終了時に登録した通知を解除する関数
	virtual void OnCeaseRelevant(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) override;
	//状態変化に応じて巡回や待機を中断し、優先行動を再評価する関数
	EBlackboardNotificationResult OnStateChanged(const UBlackboardComponent &_blackboard, FBlackboard::FKey _key);
};
