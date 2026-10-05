#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BTService_EnemyContext.generated.h"

//敵の認識情報と行動状態を一定間隔でBlackboardへ更新するクラス
UCLASS()
//敵とプレイヤーの距離、射線、体力、攻撃可否をBlackboardへ更新するService
class PROTECTFROMWOLF_API UBTService_EnemyContext : public UBTService
{
	GENERATED_BODY()

  public:
	//敵の認識情報を更新するService名と実行間隔を設定する関数
	UBTService_EnemyContext();

  protected:
	virtual void TickNode(UBehaviorTreeComponent &_ownerComp, uint8 *_nodeMemory, float _deltaSeconds) override;
};
