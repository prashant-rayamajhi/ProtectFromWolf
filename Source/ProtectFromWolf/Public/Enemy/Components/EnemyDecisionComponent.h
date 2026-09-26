#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Enemy/EnemyData.h"
#include "EnemyDecisionComponent.generated.h"

class AEnemyChara;

//移動や認識と分離して敵の攻撃形式とボス技を選択するコンポーネント
UCLASS(ClassGroup = (Enemy), meta = (BlueprintSpawnableComponent))
//戦況とプレイヤー傾向から敵の攻撃候補を評価する部品
class PROTECTFROMWOLF_API UEnemyDecisionComponent : public UActorComponent
{
	GENERATED_BODY()

  public:
	//敵ランクと戦況から攻撃優先度を計算する初期値を設定する関数
	UEnemyDecisionComponent();

	//判断を更新する関数
	void UpdateDecision(float _deltaTime, float _targetDistance);

	//攻撃を実行する関数
	bool ExecuteAttack();

  private:
	//適応攻撃形式を更新する関数
	void UpdateAdaptiveStyle(float _targetDistance);
	//プレイヤーとの距離と戦闘記憶からボスの攻撃Styleを更新する関数
	void UpdateBossStyle(float _deltaTime, float _targetDistance);
	//近接攻撃 Styleを現在の入力値と戦況から算出する関数
	float ScoreMeleeStyle(float _targetDistance) const;
	//Gun Styleを現在の入力値と戦況から算出する関数
	float ScoreGunStyle(float _targetDistance) const;
	//レーザー攻撃 Styleを現在の入力値と戦況から算出する関数
	float ScoreLaserStyle(float _targetDistance) const;
	//選択したボス行動を戦闘記憶へ記録して連続使用を制限する関数
	bool CommitBossAction(FName _actionName);

	//攻撃候補の評価結果を適用する所有敵キャラクター参照
	UPROPERTY(Transient)
	AEnemyChara *m_enemy;

	//攻撃形式判断時刻
	float m_styleDecisionTime;
	//適応近接攻撃距離
	float m_adaptiveMeleeDistance;
	//ボス近接攻撃距離かどうか
	float m_bossMeleeDistance;
	//攻撃形式設定初期化済みかどうか
	bool m_styleProfileInitialized;
	//使用回数適応攻撃形式かどうか
	bool m_usesAdaptiveStyle;
	//次のボス行動時刻
	float m_nextBossActionTime;
	//ラストボスが前回選択した攻撃行動
	FName m_lastBossAction;
};
