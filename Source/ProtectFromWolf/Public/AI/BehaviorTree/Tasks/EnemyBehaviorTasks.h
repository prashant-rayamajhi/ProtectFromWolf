#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "EnemyBehaviorTasks.generated.h"

UCLASS()
//敵の選択済み攻撃を開始し、完了までBehavior Treeを待機させるTask
class PROTECTFROMWOLF_API UBTTask_EnemyAttack : public UBTTaskNode
{
	GENERATED_BODY()

  public:
	//敵の攻撃タスク名とインスタンス生成設定を初期化する関数
	UBTTask_EnemyAttack();
	//Behavior Tree上でUBTTask 敵 攻撃を開始し、実行結果を返す関数
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) override;
};

UCLASS()
//近接・遠距離の陣形位置を計算し、敵をプレイヤー周囲へ移動させるTask
class PROTECTFROMWOLF_API UBTTask_EnemyMoveToTarget : public UBTTaskNode
{
	GENERATED_BODY()

  public:
	//攻撃対象へ接近するタスク名とインスタンス生成設定を初期化する関数
	UBTTask_EnemyMoveToTarget();
	//Behavior Tree上でUBTTask 敵 移動 To 攻撃対象を開始し、実行結果を返す関数
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) override;
};

UCLASS()
//NavMesh上の重複しない巡回地点へ移動し、到着後3秒待機させるTask
class PROTECTFROMWOLF_API UBTTask_EnemyPatrol : public UBTTaskNode
{
	GENERATED_BODY()

  public:
	//敵の巡回タスク名とインスタンス生成設定を初期化する関数
	UBTTask_EnemyPatrol();
	//Behavior Tree上でUBTTask 敵 巡回を開始し、実行結果を返す関数
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory, float _deltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) override;
	//Behavior Treeタスクが使用するメモリサイズを取得する関数
	virtual uint16 GetInstanceMemorySize() const override;

  private:
	//現在位置から次の巡回候補を探索する最大半径
	float m_patrolRadius;
};

UCLASS()
//低体力の中間ボスをプレイヤーから離れる安全地点へ退避させるTask
class PROTECTFROMWOLF_API UBTTask_EnemyRetreat : public UBTTaskNode
{
	GENERATED_BODY()

  public:
	//敵の退避タスク名とインスタンス生成設定を初期化する関数
	UBTTask_EnemyRetreat();
	//Behavior Tree上でUBTTask 敵 Retreatを開始し、実行結果を返す関数
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory) override;
};
